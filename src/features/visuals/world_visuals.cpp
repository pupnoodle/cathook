#include "features/visuals/world_visuals.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

#include "core/print.hpp"
#include "core/shared/sigs.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/entities/entity.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/material_system.hpp"
#include "libsigscan/libsigscan.h"
#include "funchook/funchook.h"

namespace world_visuals
{

particle_create_fn particle_create_original = nullptr;

namespace
{

struct modulation_state
{
  uint32_t mask = 0;
  RGBA_float world{};
  RGBA_float sky{};
  RGBA_float prop{};
  RGBA_float particle{};
  RGBA_float fog{};
  std::string level{};
  bool valid = false;
};

modulation_state last_state{};
bool fog_state_valid = false;
float original_fog_start = 0.0f;
float original_fog_end = 0.0f;
std::uint32_t maintenance_tick = 0;
struct material_restore_state
{
  RGBA_float color{};
  float alpha = 1.0f;
};
std::unordered_map<Material*, material_restore_state> original_materials{};

[[nodiscard]] bool same_color(const RGBA_float& left, const RGBA_float& right)
{
  return left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a && left.rainbow == right.rainbow;
}

[[nodiscard]] bool clean_render()
{
  return engine != nullptr && engine->is_drawing_loading_image();
}

[[nodiscard]] std::string lower(std::string_view value)
{
  std::string result{value};
  std::ranges::transform(result, result.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return result;
}

[[nodiscard]] bool has_prefix(std::string_view value, std::string_view prefix)
{
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

void set_material_color(Material* material, const RGBA_float& color)
{
  if (material == nullptr || material->is_error_material()) return;
  material->color_modulate(color);
  material->alpha_modulate(std::clamp(color.resolved().a, 0.0f, 1.0f));
}

void apply_materials(const uint32_t mask)
{
  if (material_system == nullptr) return;

  if (!original_materials.empty()) {
    for (const auto& [material, state] : original_materials) {
      if (material != nullptr && !material->is_error_material()) {
        material->color_modulate(state.color);
        material->alpha_modulate(state.alpha);
      }
    }
    original_materials.clear();
  }

  if (mask == 0) return;

  const RGBA_float world = (mask & Visuals::modulation_world) ? config.visuals.world.world_color : RGBA_float{};
  const RGBA_float sky = (mask & Visuals::modulation_sky) ? config.visuals.world.sky_color : RGBA_float{};
  const RGBA_float prop = (mask & Visuals::modulation_prop) ? config.visuals.world.prop_color : RGBA_float{};
  const RGBA_float particle = (mask & Visuals::modulation_particle) ? config.visuals.world.particle_color : RGBA_float{};

  for (auto handle = material_system->first_material(); handle != material_system->invalid_material();
       handle = material_system->next_material(handle)) {
    Material* material = material_system->get_material(handle);
    if (material == nullptr || material->is_error_material()) continue;

    const std::string group = lower(material->get_texture_group_name() != nullptr ? material->get_texture_group_name() : "");
    const std::string name = lower(material->get_name() != nullptr ? material->get_name() : "");
    const RGBA_float* replacement = nullptr;
    if (has_prefix(group, "world") && name.find("sky") == std::string::npos && (mask & Visuals::modulation_world)) {
      replacement = &world;
    } else if ((has_prefix(group, "sky") || name.find("sky") != std::string::npos) && (mask & Visuals::modulation_sky)) {
      replacement = &sky;
    } else if (group.find("static prop") != std::string::npos || group.find("staticprop") != std::string::npos) {
      if (mask & Visuals::modulation_prop) replacement = &prop;
    } else if (group.find("particle") != std::string::npos && (mask & Visuals::modulation_particle)) {
      replacement = &particle;
    }
    if (replacement == nullptr) continue;

    float red = 1.0f;
    float green = 1.0f;
    float blue = 1.0f;
    material->get_color_modulation(&red, &green, &blue);
    original_materials.emplace(material, material_restore_state{
      RGBA_float{red, green, blue, material->get_alpha_modulation(), false}, material->get_alpha_modulation()});
    set_material_color(material, *replacement);
  }
}

void apply_fog()
{
  if (material_system == nullptr) return;
  RenderContext* context = material_system->get_render_context();
  if (context == nullptr) return;
  context->begin_render();

  const bool enabled = (config.visuals.world.modulation_mask & Visuals::modulation_fog) != 0 && !clean_render();
  if (enabled) {
    float start = 0.0f;
    float end = 0.0f;
    context->get_fog_distances(&start, &end);
    if (!fog_state_valid) {
      original_fog_start = start;
      original_fog_end = end;
      fog_state_valid = true;
    }
    const RGBA color = config.visuals.world.fog_color.to_RGBA();
    context->fog_color3ub(static_cast<unsigned char>(color.r), static_cast<unsigned char>(color.g), static_cast<unsigned char>(color.b));
    const float alpha = std::clamp(config.visuals.world.fog_color.resolved().a, 0.01f, 1.0f);
    context->fog_start(start / alpha);
    context->fog_end(end / alpha);
  } else if (fog_state_valid) {
    context->fog_color3ub(255, 255, 255);
    context->fog_start(original_fog_start);
    context->fog_end(original_fog_end);
    fog_state_valid = false;
  }

  context->end_render();
  context->release();
}

void reset_state()
{
  last_state = {};
  fog_state_valid = false;
  maintenance_tick = 0;
}

[[nodiscard]] bool contains(std::string_view value, std::string_view needle)
{
  return value.find(needle) != std::string_view::npos;
}

const char* replacement_for(const char* original)
{
  if (original == nullptr) return nullptr;
  const std::string_view name{original};
  const bool projectile = contains(name, "rockettrail") || contains(name, "pipebombtrail") ||
    contains(name, "stickybombtrail") || contains(name, "flaregun_trail") || contains(name, "scorchshot_trail") ||
    contains(name, "manmelter_projectile") || contains(name, "flaming_arrow") || contains(name, "fireball_small_trail");
  if (projectile && config.visuals.effects.projectile_trail != "Default") {
    const std::string& choice = config.visuals.effects.projectile_trail;
    if (choice == "None") return nullptr;
    if (choice == "Rocket") return "rockettrail";
    if (choice == "Critical") return "critical_rocket_red";
    if (choice == "Energy") return "drg_cow_rockettrail_normal";
    if (choice == "Charged") return "drg_cow_rockettrail_charged";
    if (choice == "Fireball") return "spell_fireball_small_trail_red";
    if (choice == "Teleport") return "spell_teleport_red";
    if (choice == "Fire") return "flamethrower";
    if (choice == "Flame") return "flying_flaming_arrow";
    if (choice == "Sparks") return "critical_rocket_redsparks";
    if (choice == "Flare") return "flaregun_trail_red";
    if (choice == "Trail") return "stickybombtrail_red";
    if (choice == "Health") return "healshot_trail_red";
    if (choice == "Smoke") return "rockettrail_airstrike_line";
    if (choice == "Bubbles") return "pyrovision_scorchshot_trail_red";
    if (choice == "Halloween") return "halloween_rockettrail";
    if (choice == "Monoculus") return "eyeboss_projectile";
    if (choice == "Rainbow") return "flamethrower_rainbow";
  }

  const bool beam = contains(name, "medicgun_beam") || contains(name, "heal_beam");
  if (beam && config.visuals.effects.medigun_beam != "Default") {
    const std::string& choice = config.visuals.effects.medigun_beam;
    if (choice == "None") return nullptr;
    if (choice == "Uber") return "medicgun_beam_red_invun";
    if (choice == "Dispenser") return "dispenser_heal_red";
    if (choice == "Passtime") return "passtime_beam";
    if (choice == "Bombonomicon") return "bombonomicon_spell_trail";
    if (choice == "White") return "medicgun_beam_machinery_stage3";
    if (choice == "Orange") return "medicgun_beam_red_trail_stage3";
  }

  const bool charge = contains(name, "medicgun_charge") || contains(name, "medicgun_overheal");
  if (charge && config.visuals.effects.medigun_charge != "Default") {
    const std::string& choice = config.visuals.effects.medigun_charge;
    if (choice == "None") return nullptr;
    if (choice == "Electrocuted") return "electrocuted_red";
    if (choice == "Halloween") return "ghost_pumpkin";
    if (choice == "Fireball") return "spell_fireball_small_trail_red";
    if (choice == "Teleport") return "spell_teleport_red";
    if (choice == "Burning") return "superrare_burning1";
    if (choice == "Scorching") return "superrare_burning2";
    if (choice == "Purple energy") return "superrare_purpleenergy";
    if (choice == "Green energy") return "superrare_greenenergy";
    if (choice == "Nebula") return "unusual_invasion_nebula";
    if (choice == "Purple stars") return "unusual_star_purple_parent";
    if (choice == "Green stars") return "unusual_star_green_parent";
    if (choice == "Sunbeams") return "superrare_beams1";
    if (choice == "Spellbound") return "unusual_spellbook_circle_purple";
    if (choice == "Purple sparks") return "unusual_robot_orbiting_sparks2";
    if (choice == "Yellow sparks") return "unusual_robot_orbiting_sparks";
    if (choice == "Green zap") return "unusual_zap_green";
    if (choice == "Yellow zap") return "unusual_zap_yellow";
    if (choice == "Plasma") return "superrare_plasma1";
    if (choice == "Frostbite") return "unusual_eotl_frostbite";
    if (choice == "Purple souls") return "unusual_souls_purple_parent";
    if (choice == "Green souls") return "unusual_souls_green_parent";
    if (choice == "Bubbles") return "unusual_bubbles";
    if (choice == "Hearts") return "unusual_hearts_bubbling";
  }
  return original;
}

void* particle_create_hook(void* instance, const char* name, int attachment, const char* attachment_name)
{
  if (particle_create_original == nullptr) return nullptr;
  if (clean_render()) return particle_create_original(instance, name, attachment, attachment_name);
  const char* replacement = replacement_for(name);
  if (replacement == nullptr) return nullptr;
  return particle_create_original(instance, replacement, attachment, attachment_name);
}

}

void on_render_start()
{
  if (material_system == nullptr) return;
  if (engine != nullptr && !engine->is_in_game()) {
    if (last_state.valid) {
      apply_materials(0);
      reset_state();
    }
    apply_fog();
    return;
  }
  const char* level = engine != nullptr ? engine->get_level_name() : nullptr;
  const std::string current_level = level != nullptr ? level : "";
  const auto& world = config.visuals.world;
  const bool changed = !last_state.valid || last_state.mask != world.modulation_mask ||
    !same_color(last_state.world, world.world_color) || !same_color(last_state.sky, world.sky_color) ||
    !same_color(last_state.prop, world.prop_color) || !same_color(last_state.particle, world.particle_color) ||
    !same_color(last_state.fog, world.fog_color) || last_state.level != current_level;
  const bool refresh_materials = changed || ((++maintenance_tick % 30u) == 0u && world.modulation_mask != 0);
  if (refresh_materials && !clean_render()) {
    apply_materials(world.modulation_mask);
    last_state = {world.modulation_mask, world.world_color, world.sky_color, world.prop_color,
      world.particle_color, world.fog_color, current_level, true};
  } else if (clean_render() && last_state.valid) {
    apply_materials(0);
    reset_state();
  }
  apply_fog();
}

void on_shutdown()
{
  apply_materials(0);
  apply_fog();
  reset_state();
}

void resolve_particle_hook()
{
  particle_create_original = reinterpret_cast<particle_create_fn>(sigscan_module("client.so", sigs::particle_property_create));
  if (particle_create_original == nullptr) {
    print("Particle effect hook unavailable; effect replacement disabled\n");
  }
}

bool prepare_particle_hook(funchook_t* hooks)
{
  if (hooks == nullptr || particle_create_original == nullptr) return true;
  const int result = funchook_prepare(hooks, reinterpret_cast<void**>(&particle_create_original),
                                      reinterpret_cast<void*>(particle_create_hook));
  if (result != 0) {
    print("Particle effect hook preparation failed; effect replacement disabled\n");
    particle_create_original = nullptr;
    return false;
  }
  return true;
}

}
