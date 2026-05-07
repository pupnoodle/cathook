/*
/^-----^\   data: 2026-05-06
V  o o  V  file: src/features/visuals/glow/player_model_glow.cpp
 |  Y  |   autor: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "features/visuals/glow/player_model_glow.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

#include "features/menu/config.hpp"

#include "core/print.hpp"

#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/material_system.hpp"
#include "games/tf2/sdk/interfaces/model_render.hpp"
#include "games/tf2/sdk/interfaces/render_view.hpp"
#include "games/tf2/sdk/materials/keyvalues.hpp"
#include "games/tf2/sdk/materials/material_var.hpp"

namespace
{

constexpr float glow_fade_distance = 256.0f;
constexpr const char* render_buffer_1_name = "RenderBuffer1";
constexpr const char* render_buffer_2_name = "RenderBuffer2";
constexpr const char* texture_group_other = "Other textures";

struct glow_entity
{
  Player* player = nullptr;
  RGBA_float color{};
};

struct render_state
{
  RGBA_float color{1.0f, 1.0f, 1.0f, 1.0f};
  float blend = 1.0f;
  Material* material = nullptr;
  OverrideType override_type = OVERRIDE_NORMAL;
  bool material_ignore_z = false;
};

enum class glow_resource_status
{
  ready,
  missing_interfaces,
  invalid_render_size,
  missing_glow_color_material,
  missing_render_buffer_1,
  missing_render_buffer_2,
  missing_halo_add_to_screen_material,
  missing_blur_x_material,
  missing_blur_y_material,
  missing_bloom_amount,
};

struct scoped_rendering_flag
{
  bool previous = false;

  scoped_rendering_flag();
  ~scoped_rendering_flag();
};

struct scoped_player_invisibility
{
  Player* player = nullptr;
  float previous = 0.0f;

  explicit scoped_player_invisibility(Player* target_player)
    : player(target_player)
  {
    if (player == nullptr) {
      return;
    }

    previous = player->get_invisibility();
    player->set_invisibility(0.0f);
  }

  ~scoped_player_invisibility()
  {
    if (player != nullptr) {
      player->set_invisibility(previous);
    }
  }
};

std::vector<glow_entity> g_entities{};
Material* g_glow_color_material = nullptr;
Texture* g_render_buffer_1 = nullptr;
Texture* g_render_buffer_2 = nullptr;
Material* g_halo_add_to_screen_material = nullptr;
Material* g_blur_x_material = nullptr;
Material* g_blur_y_material = nullptr;
MaterialVar* g_bloom_amount = nullptr;
render_state g_original_state{};
int g_render_width = 0;
int g_render_height = 0;
bool g_rendering = false;
glow_resource_status g_last_resource_status = glow_resource_status::ready;

scoped_rendering_flag::scoped_rendering_flag()
  : previous(g_rendering)
{
  g_rendering = true;
}

scoped_rendering_flag::~scoped_rendering_flag()
{
  g_rendering = previous;
}

[[nodiscard]] float glow_distance_between(const Vec3& first, const Vec3& second)
{
  const auto delta = first - second;
  return std::sqrt((delta.x * delta.x) + (delta.y * delta.y) + (delta.z * delta.z));
}

[[nodiscard]] float glow_remap_value_clamped(float value, float input_min, float input_max, float output_min, float output_max)
{
  const auto input_range = input_max - input_min;
  if (std::fabs(input_range) <= 0.0001f) {
    return output_min;
  }

  const auto amount = std::clamp((value - input_min) / input_range, 0.0f, 1.0f);
  return output_min + ((output_max - output_min) * amount);
}

[[nodiscard]] int glow_outline_scale()
{
  return std::clamp(config.glow.outline_scale, 0, 10);
}

[[nodiscard]] float glow_blur_scale()
{
  return std::clamp(config.glow.blur_scale, 0.0f, 10.0f);
}

[[nodiscard]] bool glow_has_visible_effect()
{
  return glow_outline_scale() > 0 || glow_blur_scale() > 0.0f;
}

[[nodiscard]] RGBA_float player_glow_color(Player* player, Player* localplayer)
{
  if (player == localplayer) {
    return config.glow.player.local_color;
  }
  if (player->is_friend()) {
    return config.glow.player.friend_color;
  }
  if (player->get_team() == localplayer->get_team()) {
    return config.glow.player.team_color;
  }

  return config.glow.player.enemy_color;
}

[[nodiscard]] bool should_store_player(Player* player, Player* localplayer)
{
  if (player == nullptr || localplayer == nullptr) {
    return false;
  }
  if (player->get_class_id() != class_id::PLAYER || !player->is_alive() || player->is_dormant()) {
    return false;
  }
  if (player == localplayer) {
    return config.glow.player.local;
  }
  if (player->is_friend()) {
    return config.glow.player.friends || (config.glow.player.team && player->get_team() == localplayer->get_team());
  }
  if (player->get_team() != localplayer->get_team() && !config.glow.player.enemy) {
    return false;
  }
  if (player->get_team() == localplayer->get_team() && !config.glow.player.team) {
    return false;
  }

  return true;
}

[[nodiscard]] float player_glow_alpha(Player* player, Player* localplayer)
{
  if (player == nullptr || localplayer == nullptr) {
    return 0.0f;
  }
  if (player == localplayer) {
    return 1.0f;
  }

  const auto start = std::max(0.0f, config.glow.start);
  const auto end = std::max(start, config.glow.end);
  const auto distance = glow_distance_between(player->get_collision_origin(), localplayer->get_collision_origin());
  if (distance < start || distance > end) {
    return 0.0f;
  }

  auto alpha = 1.0f;
  if (config.glow.smooth_alpha) {
    alpha = glow_remap_value_clamped(distance, end - glow_fade_distance, end, alpha, 0.0f);
    if (start > 0.0f) {
      alpha = glow_remap_value_clamped(distance, start + glow_fade_distance, start, alpha, 0.0f);
    }
  }

  return std::clamp(alpha, 0.0f, 1.0f);
}

void release_material(Material*& material)
{
  if (material == nullptr) {
    return;
  }

  material->decrement_reference_count();
  material->delete_if_unreferenced();
  material = nullptr;
}

void release_texture(Texture*& texture)
{
  if (texture == nullptr) {
    return;
  }

  texture->decrement_reference_count();
  texture->delete_if_unreferenced();
  texture = nullptr;
}

void release_resources()
{
  g_bloom_amount = nullptr;
  release_material(g_glow_color_material);
  release_material(g_halo_add_to_screen_material);
  release_material(g_blur_x_material);
  release_material(g_blur_y_material);
  release_texture(g_render_buffer_1);
  release_texture(g_render_buffer_2);
  g_render_width = 0;
  g_render_height = 0;
}

void destroy_key_values(KeyValues* values)
{
  delete values;
}

[[nodiscard]] Material* create_material_from_buffer(
  const std::string_view name,
  const std::string_view shader,
  const std::string_view material_text)
{
  if (material_system == nullptr || key_values_constructor_original == nullptr || key_values_load_from_buffer_original == nullptr) {
    return nullptr;
  }

  auto* values = new KeyValues(shader.data());
  if (!values->load_from_buffer(name.data(), material_text.data())) {
    destroy_key_values(values);
    return nullptr;
  }

  auto* material = material_system->create_material(name.data(), values);
  if (material != nullptr) {
    material->increment_reference_count();
  }

  return material;
}

[[nodiscard]] const char* glow_resource_status_text(const glow_resource_status status)
{
  switch (status) {
    case glow_resource_status::ready:
      return "ready";
    case glow_resource_status::missing_interfaces:
      return "missing interfaces";
    case glow_resource_status::invalid_render_size:
      return "invalid render size";
    case glow_resource_status::missing_glow_color_material:
      return "missing dev/glow_color";
    case glow_resource_status::missing_render_buffer_1:
      return "missing render buffer 1";
    case glow_resource_status::missing_render_buffer_2:
      return "missing render buffer 2";
    case glow_resource_status::missing_halo_add_to_screen_material:
      return "missing halo material";
    case glow_resource_status::missing_blur_x_material:
      return "missing blur x material";
    case glow_resource_status::missing_blur_y_material:
      return "missing blur y material";
    case glow_resource_status::missing_bloom_amount:
      return "missing bloom amount";
  }

  return "unknown";
}

void report_resource_status(const glow_resource_status status)
{
  if (g_last_resource_status == status) {
    return;
  }

  g_last_resource_status = status;
  if (status != glow_resource_status::ready) {
    print("[player_model_glow] resources unavailable: %s\n", glow_resource_status_text(status));
  }
}

[[nodiscard]] Material* find_referenced_material(const char* name, const char* texture_group)
{
  if (material_system == nullptr) {
    return nullptr;
  }

  auto* material = material_system->find_material(name, texture_group, true, nullptr);
  if (material != nullptr) {
    material->increment_reference_count();
  }

  return material;
}

[[nodiscard]] Texture* create_render_buffer(const char* name, int width, int height)
{
  if (material_system == nullptr) {
    return nullptr;
  }

  auto* texture = material_system->create_named_render_target_texture_ex(
    name,
    width,
    height,
    rt_size_literal,
    image_format_rgb888,
    material_rt_depth_shared,
    texture_flags_clamps | texture_flags_clampt | texture_flags_eight_bit_alpha,
    create_render_target_flags_hdr);
  if (texture != nullptr) {
    texture->increment_reference_count();
  }

  return texture;
}

[[nodiscard]] bool get_render_size(int* width, int* height)
{
  if (width == nullptr || height == nullptr || engine == nullptr) {
    return false;
  }

  const auto screen_size = engine->get_screen_size();
  if (screen_size.x <= 0 || screen_size.y <= 0) {
    return false;
  }

  *width = screen_size.x;
  *height = screen_size.y;
  return true;
}

[[nodiscard]] glow_resource_status ensure_resources()
{
  if (material_system == nullptr || render_view == nullptr || model_render == nullptr || engine == nullptr) {
    return glow_resource_status::missing_interfaces;
  }

  auto width = 0;
  auto height = 0;
  if (!get_render_size(&width, &height)) {
    return glow_resource_status::invalid_render_size;
  }

  if (g_render_width != 0 && g_render_height != 0 && (g_render_width != width || g_render_height != height)) {
    release_resources();
  }

  g_render_width = width;
  g_render_height = height;

  if (g_glow_color_material == nullptr) {
    g_glow_color_material = find_referenced_material("dev/glow_color", texture_group_other);
  }

  if (g_render_buffer_1 == nullptr) {
    g_render_buffer_1 = create_render_buffer(render_buffer_1_name, width, height);
  }

  if (g_render_buffer_2 == nullptr) {
    g_render_buffer_2 = create_render_buffer(render_buffer_2_name, width, height);
  }

  if (g_halo_add_to_screen_material == nullptr) {
    g_halo_add_to_screen_material = create_material_from_buffer(
      "CatGlowHaloAddToScreen",
      "UnlitGeneric",
      R"#(
"UnlitGeneric" {
  "$basetexture" "RenderBuffer1"
  "$additive" "1"
}
)#");
  }

  const auto needs_blur = glow_blur_scale() > 0.0f;
  if (needs_blur && g_blur_x_material == nullptr) {
    g_blur_x_material = create_material_from_buffer(
      "CatGlowBlurX",
      "BlurFilterX",
      R"#(
"BlurFilterX" {
  "$basetexture" "RenderBuffer1"
}
)#");
  }

  if (needs_blur && g_blur_y_material == nullptr) {
    g_blur_y_material = create_material_from_buffer(
      "CatGlowBlurY",
      "BlurFilterY",
      R"#(
"BlurFilterY" {
  "$basetexture" "RenderBuffer2"
  "$bloomamount" "1"
}
)#");
    if (g_blur_y_material != nullptr) {
      g_bloom_amount = g_blur_y_material->find_var("$bloomamount");
    }
  }

  if (g_glow_color_material == nullptr) {
    return glow_resource_status::missing_glow_color_material;
  }
  if (g_render_buffer_1 == nullptr) {
    return glow_resource_status::missing_render_buffer_1;
  }
  if (g_render_buffer_2 == nullptr) {
    return glow_resource_status::missing_render_buffer_2;
  }
  if (g_halo_add_to_screen_material == nullptr) {
    return glow_resource_status::missing_halo_add_to_screen_material;
  }
  if (!needs_blur) {
    return glow_resource_status::ready;
  }
  if (g_blur_x_material == nullptr) {
    return glow_resource_status::missing_blur_x_material;
  }
  if (g_blur_y_material == nullptr) {
    return glow_resource_status::missing_blur_y_material;
  }
  if (g_bloom_amount == nullptr) {
    return glow_resource_status::missing_bloom_amount;
  }

  return glow_resource_status::ready;
}

[[nodiscard]] bool ensure_resources_ready()
{
  const auto status = ensure_resources();
  report_resource_status(status);
  return status == glow_resource_status::ready;
}

void restore_screen_space_modulation()
{
  const auto white = RGBA_float{1.0f, 1.0f, 1.0f, 1.0f};
  render_view->set_color_modulation(&white);
  render_view->set_blend(1.0f);
}

void begin_model_glow(const bool ignore_z)
{
  render_view->get_color_modulation(&g_original_state.color);
  g_original_state.blend = render_view->get_blend();
  g_original_state.material = nullptr;
  g_original_state.override_type = OVERRIDE_NORMAL;
  model_render->get_material_override(&g_original_state.material, &g_original_state.override_type);
  g_original_state.material_ignore_z = g_glow_color_material->get_material_flag(MATERIAL_VAR_IGNOREZ);

  const auto white = RGBA_float{1.0f, 1.0f, 1.0f, 1.0f};
  render_view->set_blend(0.0f);
  render_view->set_color_modulation(&white);
  g_glow_color_material->set_material_flag(MATERIAL_VAR_IGNOREZ, ignore_z);
  model_render->forced_material_override(g_glow_color_material);
}

void end_model_glow()
{
  render_view->set_color_modulation(&g_original_state.color);
  render_view->set_blend(g_original_state.blend);
  if (g_glow_color_material != nullptr) {
    g_glow_color_material->set_material_flag(MATERIAL_VAR_IGNOREZ, g_original_state.material_ignore_z);
  }
  model_render->forced_material_override(g_original_state.material, g_original_state.override_type);
}

void first_begin(RenderContext* render_context)
{
  begin_model_glow(false);

  render_context->clear_buffers(false, false, true);
  render_context->set_stencil_enable(true);
  render_context->set_stencil_compare_mode(STENCILCOMPARISONFUNCTION_ALWAYS);
  render_context->set_stencil_pass_mode(STENCILOPERATION_REPLACE);
  render_context->set_stencil_fail_mode(STENCILOPERATION_KEEP);
  render_context->set_stencil_zfail_mode(STENCILOPERATION_REPLACE);
  render_context->set_stencil_reference_count(1);
  render_context->set_stencil_write_mask(0xFF);
  render_context->set_stencil_test_mask(0x0);
}

void first_end(RenderContext* render_context)
{
  render_context->set_stencil_enable(false);
  end_model_glow();
}

void second_begin(RenderContext* render_context)
{
  begin_model_glow(true);

  render_context->push_render_target_and_viewport();
  render_context->set_render_target(g_render_buffer_1);
  render_context->viewport(0, 0, g_render_width, g_render_height);
  render_context->clear_color4ub(0, 0, 0, 0);
  render_context->clear_buffers(true, false, false);
}

void draw_halo_rectangle(RenderContext* render_context, int dest_x, int dest_y)
{
  render_context->draw_screen_space_rectangle(
    g_halo_add_to_screen_material,
    dest_x,
    dest_y,
    g_render_width,
    g_render_height,
    0.0f,
    0.0f,
    static_cast<float>(g_render_width - 1),
    static_cast<float>(g_render_height - 1),
    g_render_width,
    g_render_height);
}

void second_end(RenderContext* render_context)
{
  render_context->pop_render_target_and_viewport();
  restore_screen_space_modulation();

  const auto blur = glow_blur_scale();
  if (blur > 0.0f) {
    g_bloom_amount->set_float_value(blur);

    render_context->push_render_target_and_viewport();
    render_context->viewport(0, 0, g_render_width, g_render_height);
    render_context->set_render_target(g_render_buffer_2);
    render_context->draw_screen_space_rectangle(
      g_blur_x_material,
      0,
      0,
      g_render_width,
      g_render_height,
      0.0f,
      0.0f,
      static_cast<float>(g_render_width - 1),
      static_cast<float>(g_render_height - 1),
      g_render_width,
      g_render_height);
    render_context->set_render_target(g_render_buffer_1);
    render_context->draw_screen_space_rectangle(
      g_blur_y_material,
      0,
      0,
      g_render_width,
      g_render_height,
      0.0f,
      0.0f,
      static_cast<float>(g_render_width - 1),
      static_cast<float>(g_render_height - 1),
      g_render_width,
      g_render_height);
    render_context->pop_render_target_and_viewport();
  }

  render_context->set_stencil_enable(true);
  render_context->set_stencil_compare_mode(STENCILCOMPARISONFUNCTION_EQUAL);
  render_context->set_stencil_pass_mode(STENCILOPERATION_KEEP);
  render_context->set_stencil_fail_mode(STENCILOPERATION_KEEP);
  render_context->set_stencil_zfail_mode(STENCILOPERATION_KEEP);
  render_context->set_stencil_reference_count(0);
  render_context->set_stencil_write_mask(0x0);
  render_context->set_stencil_test_mask(0xFF);

  const auto outline_scale = glow_outline_scale();
  if (outline_scale > 0) {
    const auto side = (outline_scale + 1) / 2;
    draw_halo_rectangle(render_context, -side, 0);
    draw_halo_rectangle(render_context, 0, -side);
    draw_halo_rectangle(render_context, side, 0);
    draw_halo_rectangle(render_context, 0, side);

    const auto corner = outline_scale / 2;
    if (corner > 0) {
      draw_halo_rectangle(render_context, -corner, -corner);
      draw_halo_rectangle(render_context, corner, corner);
      draw_halo_rectangle(render_context, corner, -corner);
      draw_halo_rectangle(render_context, -corner, corner);
    }
  }

  if (blur > 0.0f) {
    draw_halo_rectangle(render_context, 0, 0);
  }

  render_context->set_stencil_enable(false);
  end_model_glow();
}

void draw_player_model(Player* player)
{
  if (player == nullptr) {
    return;
  }

  auto* entity = player->to_entity();
  if (entity == nullptr) {
    return;
  }

  const auto invisibility = scoped_player_invisibility(player);
  const auto rendering = scoped_rendering_flag();
  entity->draw_model(STUDIO_RENDER | STUDIO_NOSHADOWS);
}

} // namespace

namespace player_model_glow
{

void store()
{
  g_entities.clear();
  if (!config.glow.master || !glow_has_visible_effect() || engine == nullptr || entity_list == nullptr ||
      !engine->is_in_game()) {
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr) {
    return;
  }

  if (g_entities.capacity() < 64) {
    g_entities.reserve(64);
  }

  const auto max_entities = std::max(entity_list->get_max_entities(), 0);
  for (auto index = 1; index <= max_entities; ++index) {
    auto* player = entity_list->player_from_index(static_cast<unsigned int>(index));
    if (!should_store_player(player, localplayer)) {
      continue;
    }

    const auto alpha = player_glow_alpha(player, localplayer);
    if (alpha <= 0.0f) {
      continue;
    }

    auto color = player_glow_color(player, localplayer);
    color.a *= alpha;
    g_entities.emplace_back(glow_entity{.player = player, .color = color});
  }
}

void render_first()
{
  if (g_entities.empty() || !ensure_resources_ready()) {
    return;
  }

  auto* render_context = material_system->get_render_context();
  if (render_context == nullptr) {
    return;
  }

  first_begin(render_context);
  for (const auto& entity : g_entities) {
    draw_player_model(entity.player);
  }
  first_end(render_context);
}

void render_second()
{
  if (g_entities.empty() || !ensure_resources_ready()) {
    return;
  }

  auto* render_context = material_system->get_render_context();
  if (render_context == nullptr) {
    return;
  }

  second_begin(render_context);
  for (const auto& entity : g_entities) {
    render_view->set_color_modulation(&entity.color);
    render_view->set_blend(entity.color.a);
    draw_player_model(entity.player);
  }
  second_end(render_context);
}

void shutdown()
{
  g_entities.clear();
  release_resources();
  g_original_state = render_state{};
  g_last_resource_status = glow_resource_status::ready;
  g_rendering = false;
}

bool is_rendering()
{
  return g_rendering;
}

} // namespace player_model_glow
