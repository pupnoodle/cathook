#ifndef PROJECTILE_HELPERS_HPP
#define PROJECTILE_HELPERS_HPP
#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <climits>
#include <cstdint>
#include "core/types.hpp"
#include "core/math/math.hpp"
#include "external/MD5/MD5.hpp"
#include "features/combat/backtrack/backtrack.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/entities/building.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/entities/weapon.hpp"
#include "games/tf2/sdk/interfaces/attribute_manager.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/engine_trace.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/net_channel.hpp"
#include "aimbot.hpp"
#include "aim_utils.hpp"

namespace projectile_aim {
namespace detail {

static constexpr unsigned int projectile_collision_mask =
  MASK_SOLID | CONTENTS_DEBRIS | CONTENTS_HITBOX;
static constexpr float grenade_check_interval = 0.195f;
static constexpr float rocket_radius_default = 146.0f;
static constexpr float flare_det_radius = 132.0f;
static constexpr float max_velocity_reference = 3500.0f;

enum class launch_type {
  fire_setup,
  hand,
  bat,
  grenade
};

struct projectile_info {
  int weapon_id_value = TF_WEAPON_NONE;
  short def_id = 0;
  float speed = 0.0f;
  float gravity_mod = 0.0f;
  float life_time = 0.0f;
  float splash_radius = 0.0f;
  float initial_up_velocity = 0.0f;
  float release_delay = 0.0f;
  float normal_offset = 0.0f;
  float arm_time = 0.0f;
  Vec3 offset{};
  Vec3 hull{2.0f, 2.0f, 2.0f};
  unsigned int collision_mask = projectile_collision_mask;
  launch_type launch = launch_type::fire_setup;
  bool trace_launch = false;
  bool direct_hit = true;
  bool secondary_attack = false;
  bool no_flip_offset = false;
  bool owner_velocity_projection = false;
  bool air_splash = false;
  bool spin_drag_key = false;
};

inline float interval() {
  if (global_vars != nullptr && std::isfinite(global_vars->interval_per_tick) &&
      global_vars->interval_per_tick > 0.0001f) {
    return global_vars->interval_per_tick;
  }
  return static_cast<float>(TICK_INTERVAL);
}

inline bool finite(const Vec3& value) {
  return aimbot_vec3_is_finite(value);
}

inline int time_to_ticks(float seconds) {
  return static_cast<int>(0.5f + seconds / interval());
}

inline float ticks_to_time(int ticks) {
  return static_cast<float>(ticks) * interval();
}

inline float attribute(float fallback, const char* name, Entity* entity) {
  return attribute_manager != nullptr
    ? attribute_manager->attrib_hook_value(fallback, name, entity)
    : fallback;
}

inline float dot(const Vec3& left, const Vec3& right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

inline float length_squared(const Vec3& value) {
  return value.x * value.x + value.y * value.y + value.z * value.z;
}

inline float length(const Vec3& value) {
  return std::sqrt(length_squared(value));
}

inline Vec3 normalized(const Vec3& value) {
  const float magnitude = length(value);
  return magnitude > 0.0001f ? value * (1.0f / magnitude) : Vec3{};
}

inline float remap_val_clamped(float value, float in_min, float in_max, float out_min, float out_max) {
  if (in_max - in_min <= 0.0001f) {
    return out_max >= out_min ? out_min : out_max;
  }
  const float fraction = std::clamp((value - in_min) / (in_max - in_min), 0.0f, 1.0f);
  return out_min + (out_max - out_min) * fraction;
}

inline float game_convar_float(const char* name, float fallback) {
  if (name == nullptr || convar_system == nullptr) {
    return fallback;
  }
  Convar* var = convar_system->find_var(name);
  if (var == nullptr) {
    return fallback;
  }
  const float value = var->get_float();
  return std::isfinite(value) && value >= 0.0f ? value : fallback;
}

inline bool solve_quadratic_front_root(float a, float b, float c, float& root) {
  if (std::fabs(a) <= 0.000001f) {
    if (std::fabs(b) <= 0.000001f) {
      return false;
    }
    const float solution = -c / b;
    if (solution <= 0.0f) {
      return false;
    }
    root = solution;
    return true;
  }

  const float discriminant = b * b - 4.0f * a * c;
  if (discriminant < 0.0f) {
    return false;
  }
  const float sqrt_discriminant = std::sqrt(discriminant);
  const float root_a = (-b - sqrt_discriminant) / (2.0f * a);
  const float root_b = (-b + sqrt_discriminant) / (2.0f * a);
  if (root_a > 0.0f && root_b > 0.0f) {
    root = std::min(root_a, root_b);
    return true;
  }
  if (root_a > 0.0f) {
    root = root_a;
    return true;
  }
  if (root_b > 0.0f) {
    root = root_b;
    return true;
  }
  return false;
}

inline int weapon_id(Weapon* weapon) {
  if (weapon == nullptr) {
    return TF_WEAPON_NONE;
  }
  const int id = weapon->get_weapon_id();
  if (id == TF_WEAPON_GRENADE_THROWABLE) {
    return TF_WEAPON_THROWABLE;
  }
  if (id != TF_WEAPON_NONE) {
    return id;
  }

  switch (weapon->get_def_id()) {
  case Soldier_m_RocketLauncher:
  case Soldier_m_RocketLauncherR:
  case Soldier_m_TheBlackBox:
  case Soldier_m_RocketJumper:
  case Soldier_m_TheLibertyLauncher:
  case Soldier_m_TheCowMangler5000:
  case Soldier_m_TheOriginal:
  case Soldier_m_FestiveRocketLauncher:
  case Soldier_m_TheBeggarsBazooka:
  case Soldier_m_FestiveBlackBox:
  case Soldier_m_TheAirStrike:
    return TF_WEAPON_ROCKETLAUNCHER;
  case Soldier_m_TheDirectHit:
    return TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT;
  case Soldier_s_TheRighteousBison:
    return TF_WEAPON_RAYGUN;
  case Demoman_m_GrenadeLauncher:
  case Demoman_m_GrenadeLauncherR:
  case Demoman_m_TheLochnLoad:
  case Demoman_m_TheLooseCannon:
  case Demoman_m_FestiveGrenadeLauncher:
  case Demoman_m_TheIronBomber:
    return weapon->get_def_id() == Demoman_m_TheLooseCannon ? TF_WEAPON_CANNON
                                                            : TF_WEAPON_GRENADELAUNCHER;
  case Demoman_s_StickybombLauncher:
  case Demoman_s_StickybombLauncherR:
  case Demoman_s_FestiveStickybombLauncher:
  case Demoman_s_TheScottishResistance:
  case Demoman_s_TheQuickiebombLauncher:
    return TF_WEAPON_PIPEBOMBLAUNCHER;
  case Medic_m_CrusadersCrossbow:
  case Medic_m_FestiveCrusadersCrossbow:
    return TF_WEAPON_CROSSBOW;
  case Medic_m_SyringeGun:
  case Medic_m_SyringeGunR:
  case Medic_m_TheBlutsauger:
  case Medic_m_TheOverdose:
    return TF_WEAPON_SYRINGEGUN_MEDIC;
  case Engi_m_TheRescueRanger:
    return TF_WEAPON_SHOTGUN_BUILDING_RESCUE;
  case Engi_m_ThePomson6000:
    return TF_WEAPON_DRG_POMSON;
  case Sniper_m_TheHuntsman:
  case Sniper_m_FestiveHuntsman:
  case Sniper_m_TheFortifiedCompound:
    return TF_WEAPON_COMPOUND_BOW;
  case Pyro_s_TheFlareGun:
  case Pyro_s_TheDetonator:
  case Pyro_s_TheManmelter:
  case Pyro_s_TheScorchShot:
  case Pyro_s_FestiveFlareGun:
    return TF_WEAPON_FLAREGUN;
  case Pyro_m_DragonsFury:
    return TF_WEAPON_FLAMETHROWER;
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
    return TF_WEAPON_JAR_MILK;
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
    return TF_WEAPON_CLEAVER;
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
  case Sniper_s_TheSelfAwareBeautyMark:
    return TF_WEAPON_JAR;
  case Pyro_s_GasPasser:
    return TF_WEAPON_GRENADE_GAS;
  default:
    return id;
  }
}

inline bool is_grenade_launcher(int weapon_id) {
  return weapon_id == TF_WEAPON_GRENADELAUNCHER ||
    weapon_id == TF_WEAPON_PIPEBOMBLAUNCHER ||
    weapon_id == TF_WEAPON_CANNON ||
    weapon_id == TF_WEAPON_STICKY_BALL_LAUNCHER;
}

inline bool is_rocket_weapon(int weapon_id) {
  return weapon_id == TF_WEAPON_ROCKETLAUNCHER ||
    weapon_id == TF_WEAPON_PARTICLE_CANNON ||
    weapon_id == TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT;
}

inline float projectile_speed(Weapon* weapon, float fallback) {
  if (weapon == nullptr) {
    return fallback;
  }

  const float data_speed = weapon->get_projectile_speed_from_data();
  const float base_speed = data_speed > 1.0f ? data_speed : fallback;
  const float modified_speed = attribute(base_speed, "mult_projectile_speed", weapon->to_entity());
  return std::isfinite(modified_speed) ? std::clamp(modified_speed, 1.0f, 5000.0f) : base_speed;
}

inline float effective_drag(const projectile_info& info, float velocity, bool lob) {
  const int id = info.weapon_id_value;
  switch (id) {
  case TF_WEAPON_GRENADELAUNCHER: {
    const bool no_spin = info.spin_drag_key;
    if (lob) {
      return no_spin ? remap_val_clamped(velocity, 1217.0f, max_velocity_reference, 0.030f, 0.033f)
                     : remap_val_clamped(velocity, 1217.0f, max_velocity_reference, 0.056f, 0.062f);
    }
    return no_spin ? remap_val_clamped(velocity, 1217.0f, max_velocity_reference, 0.060f, 0.085f)
                   : remap_val_clamped(velocity, 1217.0f, max_velocity_reference, 0.120f, 0.200f);
  }
  case TF_WEAPON_CANNON:
    return lob ? remap_val_clamped(velocity, 1454.0f, max_velocity_reference, 0.099f, 0.092f)
               : remap_val_clamped(velocity, 1454.0f, max_velocity_reference, 0.385f, 0.530f);
  case TF_WEAPON_PIPEBOMBLAUNCHER:
  case TF_WEAPON_STICKY_BALL_LAUNCHER:
    return lob ? remap_val_clamped(velocity, 922.0f, max_velocity_reference, 0.048f, 0.060f)
               : remap_val_clamped(velocity, 922.0f, max_velocity_reference, 0.090f, 0.190f);
  case TF_WEAPON_CLEAVER:
  case TF_WEAPON_GRENADE_CLEAVER:
    return lob ? 0.075f : 0.310f;
  case TF_WEAPON_BAT_WOOD:
    return lob ? 0.057f : 0.180f;
  case TF_WEAPON_BAT_GIFTWRAP:
    return lob ? 0.072f : 0.285f;
  case TF_WEAPON_JAR:
  case TF_WEAPON_JAR_MILK:
  case TF_WEAPON_THROWABLE:
    return lob ? 0.030f : 0.057f;
  case TF_WEAPON_GRENADE_GAS:
    return lob ? 0.089f : 0.530f;
  default:
    return 0.0f;
  }
}

struct projectile_random_stream {
  static constexpr int table_size = 32;
  static constexpr int ia = 16807;
  static constexpr int im = 2147483647;
  static constexpr int iq = 127773;
  static constexpr int ir = 2836;
  static constexpr int ndiv = 1 + ((im - 1) / table_size);
  static constexpr double am = 1.0 / static_cast<double>(im);
  static constexpr double rnmx = 1.0 - 1.2e-7;

  int seed_value = 0;
  int shuffle_value = 0;
  int table[table_size]{};

  void set_seed(int seed) {
    seed_value = seed < 0 ? seed : -seed;
    shuffle_value = 0;
  }

  int generate_random_number() {
    if (seed_value <= 0 || shuffle_value == 0) {
      seed_value = -seed_value < 1 ? 1 : -seed_value;
      for (int j = table_size + 7; j >= 0; --j) {
        const int k = seed_value / iq;
        seed_value = ia * (seed_value - (k * iq)) - (ir * k);
        if (seed_value < 0) {
          seed_value += im;
        }
        if (j < table_size) {
          table[j] = seed_value;
        }
      }
      shuffle_value = table[0];
    }

    const int k = seed_value / iq;
    seed_value = ia * (seed_value - (k * iq)) - (ir * k);
    if (seed_value < 0) {
      seed_value += im;
    }

    int j = shuffle_value / ndiv;
    if (j >= table_size || j < 0) {
      j &= table_size - 1;
    }
    shuffle_value = table[j];
    table[j] = seed_value;
    return shuffle_value;
  }

  float random_float(float lo, float hi) {
    double value = am * static_cast<double>(generate_random_number());
    if (value > rnmx) {
      value = rnmx;
    }
    return static_cast<float>((value * static_cast<double>(hi - lo)) +
                              static_cast<double>(lo));
  }

  int random_int(int lo, int hi) {
    if (hi <= lo) {
      return lo;
    }
    const std::uint32_t range = static_cast<std::uint32_t>(hi - lo) + 1U;
    const std::uint32_t limit = 0x80000000U - (0x80000000U % range);
    std::uint32_t value = 0;
    do {
      value = static_cast<std::uint32_t>(generate_random_number());
    } while (value >= limit);
    return lo + static_cast<int>(value % range);
  }
};

inline std::uint32_t projectile_crc32_byte(std::uint32_t crc, std::uint8_t byte) {
  crc ^= byte;
  for (int bit = 0; bit < 8; ++bit) {
    const std::uint32_t mask = 0U - (crc & 1U);
    crc = (crc >> 1) ^ (0xEDB88320U & mask);
  }
  return crc;
}

inline std::uint32_t projectile_seed_file_line_hash(int seed, const char* name,
                                                    int additional_seed) {
  std::uint32_t crc = 0xFFFFFFFFU;
  const auto process_int = [&crc](int value) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    for (std::size_t index = 0; index < sizeof(value); ++index) {
      crc = projectile_crc32_byte(crc, bytes[index]);
    }
  };
  process_int(seed);
  process_int(additional_seed);
  if (name != nullptr) {
    for (const char* cursor = name; *cursor != '\0'; ++cursor) {
      crc = projectile_crc32_byte(crc, static_cast<std::uint8_t>(*cursor));
    }
  }
  return crc ^ 0xFFFFFFFFU;
}

struct projectile_randomness {
  bool valid = false;
  float syringe_pitch = 0.0f;
  float syringe_yaw = 0.0f;
  float arrow_pitch = 0.0f;
  float arrow_yaw = 0.0f;
  float grenade_up = 0.0f;
  float grenade_right = 0.0f;
};

inline projectile_randomness projectile_randomness_for(Weapon* weapon, user_cmd* cmd) {
  projectile_randomness result{};
  if (weapon == nullptr || cmd == nullptr || cmd->command_number <= 0) {
    return result;
  }

  const int seed =
    static_cast<int>(MD5_PseudoRandom(static_cast<unsigned int>(cmd->command_number)) & INT_MAX);
  projectile_random_stream stream{};
  stream.set_seed(
    static_cast<int>(projectile_seed_file_line_hash(seed, "SelectWeightedSequence", 0)));
  for (int index = 0; index < 6; ++index) {
    (void)stream.random_float(0.0f, 1.0f);
  }

  const int id = weapon_id(weapon);
  if (id == TF_WEAPON_SYRINGEGUN_MEDIC) {
    result.syringe_pitch = stream.random_float(-1.5f, 1.5f);
    result.syringe_yaw = stream.random_float(-1.5f, 1.5f);
  } else if (id == TF_WEAPON_COMPOUND_BOW) {
    const float charge_begin = weapon->get_charge_begin_time();
    const float now = global_vars != nullptr ? global_vars->curtime : 0.0f;
    if (charge_begin > 0.0f && now - charge_begin >= 5.0f) {
      result.arrow_pitch =
        (static_cast<float>(stream.random_int(0, INT_MAX)) / static_cast<float>(INT_MAX)) * 12.0f -
        6.0f;
      result.arrow_yaw =
        (static_cast<float>(stream.random_int(0, INT_MAX)) / static_cast<float>(INT_MAX)) * 12.0f -
        6.0f;
    }
  } else if (id == TF_WEAPON_GRENADELAUNCHER || id == TF_WEAPON_PIPEBOMBLAUNCHER ||
             id == TF_WEAPON_CANNON) {
    result.grenade_up = stream.random_float(-10.0f, 10.0f);
    result.grenade_right = stream.random_float(-10.0f, 10.0f);
  }
  result.valid = true;
  return result;
}

inline bool get_info(Player* local, Weapon* weapon, projectile_info& out) {
  out = {};
  if (local == nullptr || weapon == nullptr) {
    return false;
  }

  const int id = weapon_id(weapon);
  const bool ducking = local->is_ducking();
  const float weapon_z = ducking ? 8.0f : -3.0f;
  out.weapon_id_value = id;
  out.def_id = weapon->get_def_id();

  if (weapon->get_def_id() == Pyro_m_DragonsFury) {
    const float fireball_speed = std::max(game_convar_float("tf_fireball_speed", 1500.0f), 1.0f);
    out.weapon_id_value = TF_WEAPON_FLAMETHROWER;
    out.def_id = weapon->get_def_id();
    out.speed = fireball_speed;
    out.life_time = std::min(game_convar_float("tf_fireball_distance", 686.0f) / fireball_speed,
                             game_convar_float("tf_fireball_lifetime", 3.5f));
    out.hull = {1.0f, 1.0f, 1.0f};
    out.offset = {3.0f, 7.0f, -9.0f};
    out.no_flip_offset = true;
    return true;
  }

  if (weapon->is_flamethrower()) {
    const float box = std::clamp(game_convar_float("tf_flamethrower_boxsize", 14.0f), 2.0f, 32.0f);
    out.speed = 2000.0f;
    out.life_time = 0.285f;
    out.offset = {40.0f, 12.0f, weapon_z};
    out.hull = {box * 0.5f, box * 0.5f, box * 0.5f};
    out.no_flip_offset = true;
    out.owner_velocity_projection = true;
    out.direct_hit = true;
    return true;
  }

  switch (id) {
  case TF_WEAPON_ROCKETLAUNCHER:
  case TF_WEAPON_PARTICLE_CANNON:
  case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT: {
    out.speed = projectile_speed(weapon, 1100.0f);
    out.splash_radius = id == TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT ? rocket_radius_default
      : (weapon->get_def_id() == Soldier_m_TheAirStrike ? 130.0f : rocket_radius_default);
    out.splash_radius = attribute(out.splash_radius, "mult_explosion_radius", weapon->to_entity());
    out.offset = {23.5f,
                  attribute(0.0f, "centerfire_projectile", weapon->to_entity()) != 0.0f ? 0.0f
                                                                                        : 12.0f,
                  weapon_z};
    out.trace_launch = true;
    out.normal_offset = 1.0f;
    out.life_time = 10.0f;
    return true;
  }

  case TF_WEAPON_GRENADELAUNCHER: {
    const bool iron_bomber = weapon->get_def_id() == Demoman_m_TheIronBomber;
    out.speed = std::min(projectile_speed(weapon, 1200.0f), 3500.0f);
    out.gravity_mod = 1.0f;
    out.life_time =
      iron_bomber ? 1.4f : game_convar_float("tf_grenadelauncher_livetime", 0.8f);
    out.splash_radius = attribute(rocket_radius_default, "mult_explosion_radius",
                                 weapon->to_entity());
    out.offset = {16.0f, 8.0f, -6.0f};
    out.hull = {2.0f, 2.0f, 2.0f};
    out.initial_up_velocity = 200.0f;
    out.trace_launch = true;
    out.spin_drag_key =
      attribute(0.0f, "grenade_no_spin", weapon->to_entity()) == 0.0f;
    return true;
  }

  case TF_WEAPON_PIPEBOMBLAUNCHER:
  case TF_WEAPON_STICKY_BALL_LAUNCHER: {
    const float charge_rate =
      std::max(attribute(4.0f, "stickybomb_charge_rate", weapon->to_entity()), 0.1f);
    const float charge_begin = weapon->get_charge_begin_time();
    const float now = global_vars != nullptr ? global_vars->curtime
                                             : local->get_tickbase() * interval();
    const float charge = std::clamp(now - charge_begin + interval(), 0.0f, charge_rate);
    out.speed = std::min(std::lerp(900.0f, 2400.0f, charge / charge_rate), 3500.0f);
    out.gravity_mod = 1.0f;
    out.life_time = 8.0f;
    out.splash_radius =
      attribute(rocket_radius_default, "mult_explosion_radius", weapon->to_entity());
    out.offset = {16.0f, 8.0f, -6.0f};
    out.hull = {2.0f, 2.0f, 2.0f};
    out.initial_up_velocity = 200.0f;
    out.trace_launch = true;
    out.air_splash = true;
    out.arm_time = attribute(game_convar_float("tf_grenadelauncher_livetime", 0.8f),
                             "sticky_arm_time", weapon->to_entity());
    return true;
  }

  case TF_WEAPON_CANNON:
    out.speed = 1454.0f;
    out.gravity_mod = 1.0f;
    out.life_time = 0.95f;
    out.splash_radius =
      attribute(rocket_radius_default, "mult_explosion_radius", weapon->to_entity());
    out.offset = {16.0f, 8.0f, -6.0f};
    out.hull = {3.0f, 3.0f, 3.0f};
    out.initial_up_velocity = 200.0f;
    out.trace_launch = true;
    return true;

  case TF_WEAPON_FLAREGUN: {
    out.speed = 2000.0f;
    out.gravity_mod = 0.3f;
    out.initial_up_velocity = weapon->get_def_id() == Pyro_s_TheScorchShot ? 150.0f : 0.0f;
    out.offset = {23.5f, 12.0f, weapon_z};
    out.life_time = 10.0f;
    if (weapon->get_def_id() == Pyro_s_TheScorchShot) {
      out.splash_radius = rocket_radius_default;
      out.air_splash = true;
    } else if (weapon->get_def_id() == Pyro_s_TheDetonator) {
      out.splash_radius = flare_det_radius;
      out.air_splash = true;
    }
    return true;
  }

  case TF_WEAPON_FLAREGUN_REVENGE:
    out.speed = 3000.0f;
    out.gravity_mod = 0.45f;
    out.offset = {23.5f, 12.0f, weapon_z};
    out.life_time = 10.0f;
    return true;

  case TF_WEAPON_RAYGUN:
  case TF_WEAPON_DRG_POMSON:
    out.speed = 1200.0f;
    out.hull = {1.0f, 1.0f, 1.0f};
    out.offset = {23.5f, 12.0f, weapon_z};
    out.trace_launch = true;
    out.normal_offset = id == TF_WEAPON_RAYGUN ? 1.0f : 0.0f;
    out.life_time = 10.0f;
    return true;

  case TF_WEAPON_FLAMETHROWER_ROCKET:
    out.speed = game_convar_float("tf_fireball_speed", 1500.0f);
    out.gravity_mod = 0.0f;
    out.hull = {1.0f, 1.0f, 1.0f};
    out.offset = {3.0f, 7.0f, -9.0f};
    out.no_flip_offset = true;
    out.life_time = std::min(game_convar_float("tf_fireball_distance", 686.0f) /
                               std::max(out.speed, 1.0f),
                             game_convar_float("tf_fireball_lifetime", 3.5f));
    return true;

  case TF_WEAPON_COMPOUND_BOW: {
    const float begin = weapon->get_charge_begin_time();
    const float now = global_vars != nullptr ? global_vars->curtime
                                             : local->get_tickbase() * interval();
    const float charge = begin > 0.0f ? std::clamp(now - begin, 0.0f, 1.0f) : 0.0f;
    out.speed = std::lerp(1800.0f, 2600.0f, charge);
    out.gravity_mod = std::lerp(0.5f, 0.1f, charge);
    out.life_time = 10.0f;
    out.offset = {23.5f, 8.0f, -3.0f};
    out.hull = {1.0f, 1.0f, 1.0f};
    return true;
  }

  case TF_WEAPON_CROSSBOW:
  case TF_WEAPON_SHOTGUN_BUILDING_RESCUE:
    out.speed = 2400.0f;
    out.gravity_mod = 0.2f;
    out.life_time = 10.0f;
    out.hull = {1.0f, 1.0f, 1.0f};
    out.offset = {23.5f, 8.0f, -3.0f};
    return true;

  case TF_WEAPON_SYRINGEGUN_MEDIC:
    out.speed = 1000.0f;
    out.gravity_mod = 0.3f;
    out.hull = {1.0f, 1.0f, 1.0f};
    out.offset = {16.0f, 6.0f, -8.0f};
    out.life_time = 3.5f;
    return true;

  case TF_WEAPON_JAR:
  case TF_WEAPON_JAR_MILK:
    out.speed = 1000.0f;
    out.gravity_mod = 1.0f;
    out.life_time = 2.2f;
    out.splash_radius = 200.0f;
    out.offset = {16.0f, 8.0f, -6.0f};
    out.hull = {3.0f, 3.0f, 3.0f};
    out.initial_up_velocity = 200.0f;
    out.release_delay = 0.1f;
    out.launch = launch_type::hand;
    out.trace_launch = true;
    return true;

  case TF_WEAPON_CLEAVER:
  case TF_WEAPON_GRENADE_CLEAVER:
    out.speed = 3000.0f * (10.0f / std::sqrt(101.0f));
    out.gravity_mod = 1.0f;
    out.life_time = 2.2f;
    out.offset = {16.0f, 8.0f, -6.0f};
    out.initial_up_velocity = 3000.0f / std::sqrt(101.0f);
    out.release_delay = 0.1f;
    out.hull = {1.0f, 1.0f, 10.0f};
    out.launch = launch_type::hand;
    out.trace_launch = true;
    return true;

  case TF_WEAPON_BAT_WOOD:
  case TF_WEAPON_BAT_GIFTWRAP:
    out.speed = 3000.0f * (10.0f / std::sqrt(101.0f));
    out.gravity_mod = 1.0f;
    out.life_time = 2.2f;
    out.splash_radius = id == TF_WEAPON_BAT_GIFTWRAP ? 50.0f : 0.0f;
    out.offset = {0.0f, 0.0f, 0.0f};
    out.hull = {3.0f, 3.0f, 3.0f};
    out.initial_up_velocity = 3000.0f / std::sqrt(101.0f);
    out.release_delay = 0.1f;
    out.launch = launch_type::bat;
    out.secondary_attack = true;
    return true;

  case TF_WEAPON_GRENADE_GAS:
    out.speed = 2000.0f;
    out.gravity_mod = 0.4f;
    out.life_time = 3.0f;
    out.splash_radius = 256.0f;
    out.offset = {3.0f, 7.0f, -9.0f};
    out.hull = {3.0f, 3.0f, 3.0f};
    out.initial_up_velocity = 200.0f;
    out.no_flip_offset = true;
    out.trace_launch = true;
    out.launch = launch_type::grenade;
    out.direct_hit = false;
    return true;

  case TF_WEAPON_THROWABLE:
    out.speed = 1000.0f;
    out.gravity_mod = 1.0f;
    out.life_time = attribute(5.0f, "throwable_detonation_time", weapon->to_entity());
    out.splash_radius = 250.0f;
    out.offset = {3.0f, 7.0f, -9.0f};
    out.hull = {2.0f, 2.0f, 2.0f};
    out.initial_up_velocity = 200.0f;
    out.release_delay = 0.1f;
    out.launch = launch_type::hand;
    out.trace_launch = true;
    return true;

  case TF_WEAPON_GRAPPLINGHOOK: {
    const bool scout = local->get_tf_class() == tf_class::SCOUT;
    out.speed = scout ? 3000.0f
                      : std::max(game_convar_float("tf_grapplinghook_projectile_speed", 2600.0f),
                                 1.0f);
    const float max_distance = game_convar_float("tf_grapplinghook_max_distance", 1450.0f);
    out.gravity_mod = 0.0f;
    out.life_time = out.speed > 1.0f ? max_distance / out.speed : 0.5f;
    out.offset = {23.5f, -8.0f, -3.0f};
    out.hull = {1.2f, 1.2f, 1.2f};
    return true;
  }

  default:
    break;
  }

  return false;
}

inline float latency_seconds() {
  const backtrack::backtrack_timing timing = backtrack::current_timing();
  if (timing.valid) {
    const float combined = timing.outgoing_latency + timing.incoming_latency;
    if (std::isfinite(combined) && combined >= 0.0f) {
      return std::clamp(combined, 0.0f, 0.25f);
    }
  }

  if (client_state == nullptr || client_state->m_NetChannel == nullptr) {
    return 0.0f;
  }
  const float latency = client_state->m_NetChannel->get_latency(0) +
    client_state->m_NetChannel->get_latency(1);
  return std::isfinite(latency) ? std::clamp(latency, 0.0f, 0.25f) : 0.0f;
}

inline bool launch_position(Player* local, const projectile_info& info, const Vec3& angles,
                            bool ignore_friendly_players, Vec3& out,
                            Vec3* launch_angles_out = nullptr) {
  if (local == nullptr || !finite(angles)) {
    return false;
  }

  Vec3 launch_angles = aimbot_clamp_angles(angles);
  Vec3 forward{};
  Vec3 right{};
  Vec3 up{};
  angle_vectors(launch_angles, &forward, &right, &up);

  const Vec3 eye = local->get_shoot_pos();

  if (info.launch == launch_type::bat) {
    const float model_scale = local->get_model_scale() > 0.0f ? local->get_model_scale() : 1.0f;
    out = local->get_origin() +
      (Vec3{0.0f, 0.0f, 50.0f} + forward * 32.0f) * model_scale;
    if (launch_angles_out != nullptr) {
      *launch_angles_out = launch_angles;
    }
    return finite(out);
  }

  if (info.launch == launch_type::fire_setup) {
    Vec3 fire_offset = info.offset;
    if (!info.no_flip_offset) {
      static Convar* cl_flipviewmodels = nullptr;
      if (cl_flipviewmodels == nullptr && convar_system != nullptr) {
        cl_flipviewmodels = convar_system->find_var("cl_flipviewmodels");
      }
      if (cl_flipviewmodels != nullptr && cl_flipviewmodels->get_int() != 0) {
        fire_offset.y *= -1.0f;
      }
    }
    out = eye + (forward * fire_offset.x) + (right * fire_offset.y) + (up * fire_offset.z);

    Vec3 fire_end = eye + forward * 2000.0f;
    Vec3 effective_end = fire_end;
    if (engine_trace != nullptr) {
      Vec3 trace_start = eye;
      Vec3 trace_end = fire_end;
      ray_t ray = engine_trace->init_ray(&trace_start, &trace_end);
      trace_filter filter{};
      if (ignore_friendly_players) {
        engine_trace->init_projectile_trace_filter(&filter, local->to_entity());
      } else {
        engine_trace->init_trace_filter(&filter, local->to_entity());
      }
      trace_t trace{};
      engine_trace->trace_ray(&ray, projectile_collision_mask, &filter, &trace);
      if (trace.start_solid || trace.all_solid) {
        return false;
      }
      effective_end = trace.fraction > 0.1f ? trace.endpos : fire_end;
    }

    launch_angles = aimbot_calculate_angles_to_position(out, effective_end);
    if (!finite(out) || !finite(launch_angles)) {
      return false;
    }

    if (launch_angles_out != nullptr) {
      *launch_angles_out = launch_angles;
    }
    return true;
  }

  if (info.launch == launch_type::grenade) {
    out = eye + forward * 16.0f - right * 8.0f - up * 20.0f;
  } else {
    out = eye + (forward * info.offset.x) + (right * info.offset.y) + (up * info.offset.z);
  }

  if (info.trace_launch && engine_trace != nullptr) {
    Vec3 mins = info.hull * -1.0f;
    Vec3 maxs = info.hull;
    Vec3 trace_start = eye;
    Vec3 trace_end = out;
    ray_t ray = engine_trace->init_ray(&trace_start, &trace_end, &mins, &maxs);
    trace_filter filter{};
    engine_trace->init_world_and_props_trace_filter(&filter);
    trace_t trace{};
    engine_trace->trace_ray(&ray, projectile_collision_mask, &filter, &trace);
    if (trace.start_solid || trace.all_solid || trace.fraction < 0.999f) {
      return false;
    }
    out = trace.endpos;
  }

  if (launch_angles_out != nullptr) {
    *launch_angles_out = launch_angles;
  }
  return finite(out);
}

inline Vec3 launch_velocity(const projectile_info& info, const Vec3& launch_angles,
                            Player* local) {
  Vec3 forward{};
  Vec3 up{};
  angle_vectors(launch_angles, &forward, nullptr, &up);
  Vec3 velocity = forward * info.speed + up * info.initial_up_velocity;
  if (info.owner_velocity_projection && local != nullptr) {
    const Vec3 owner_velocity = local->get_velocity();
    velocity = velocity + forward * dot(owner_velocity, forward);
  }
  return velocity;
}

inline bool solve_ballistic(const projectile_info& info, const Vec3& from, const Vec3& to,
                            float drag_factor, bool lob, float& pitch_out, float& yaw_out,
                            float& time_out) {
  const Vec3 delta = to - from;
  const float horizontal = std::hypot(delta.x, delta.y);
  if (horizontal <= 0.001f || !std::isfinite(info.speed) || info.speed <= 0.0f) {
    return false;
  }

  const float gravity = 800.0f * info.gravity_mod;
  const float v0 = std::hypot(info.speed, info.initial_up_velocity);
  const float launch_pitch = std::atan2(info.initial_up_velocity, info.speed);
  float velocity = v0;
  float pitch = 0.0f;

  const auto resolve_pitch = [&](float speed) -> bool {
    const float root = speed * speed * speed * speed -
      gravity * (gravity * horizontal * horizontal + 2.0f * delta.z * speed * speed);
    if (root < 0.0f) {
      return false;
    }
    if (gravity > 0.001f) {
      pitch = lob ? std::atan((speed * speed + std::sqrt(root)) / (gravity * horizontal))
                  : std::atan((speed * speed - std::sqrt(root)) / (gravity * horizontal));
    } else {
      pitch = std::atan2(delta.z, horizontal);
    }
    return true;
  };

  if (!resolve_pitch(velocity)) {
    return false;
  }

  if (drag_factor > 0.0f && gravity > 0.001f) {
    float time_estimate = horizontal / std::max(velocity * std::cos(pitch), 1.0f);
    velocity *= std::clamp(1.0f - drag_factor * time_estimate, 0.25f, 1.0f);
    if (!resolve_pitch(velocity)) {
      return false;
    }
    velocity /= (1.0f + 0.5f * (1.0f - std::exp(-drag_factor * time_estimate)));
    velocity *= std::clamp(1.0f + delta.z / std::max(velocity, 1.0f) *
                                     static_cast<float>(TICK_INTERVAL) * time_estimate,
                           0.25f, 4.0f);
    if (!resolve_pitch(velocity)) {
      return false;
    }
  }

  pitch_out = -((pitch - launch_pitch) * radpi);
  yaw_out = std::atan2(delta.y, delta.x) * radpi;
  time_out = horizontal / std::max(std::cos(pitch) * velocity, 1.0f);
  return std::isfinite(pitch_out) && std::isfinite(yaw_out) && std::isfinite(time_out) &&
    time_out >= 0.0f && (info.life_time <= 0.0f || time_out <= info.life_time);
}

inline Vec3 compensate_projectile_spread(Player* local, Weapon* weapon, user_cmd* cmd,
                                         const projectile_info& info,
                                         const Vec3& desired_angles) {
  if (local == nullptr || weapon == nullptr || cmd == nullptr || !finite(desired_angles)) {
    return desired_angles;
  }

  const projectile_randomness random = projectile_randomness_for(weapon, cmd);
  if (!random.valid) {
    return desired_angles;
  }

  Vec3 compensated = desired_angles;
  const int id = info.weapon_id_value;
  if (id == TF_WEAPON_SYRINGEGUN_MEDIC) {
    compensated.x -= random.syringe_pitch;
    compensated.y -= random.syringe_yaw;
    return aimbot_clamp_angles(compensated);
  }
  if (id == TF_WEAPON_COMPOUND_BOW) {
    compensated.x -= random.arrow_pitch;
    compensated.y -= random.arrow_yaw;
    return aimbot_clamp_angles(compensated);
  }

  if (random.grenade_up == 0.0f && random.grenade_right == 0.0f) {
    return aimbot_clamp_angles(compensated);
  }

  const bool ignore_friendly_players = !is_rocket_weapon(id);
  for (int iteration = 0; iteration < 4; ++iteration) {
    Vec3 launch{};
    Vec3 launch_angles{};
    if (!launch_position(local, info, compensated, ignore_friendly_players, launch,
                         &launch_angles)) {
      break;
    }

    Vec3 forward{};
    Vec3 right{};
    Vec3 up{};
    angle_vectors(launch_angles, &forward, &right, &up);
    const Vec3 base_velocity = forward * info.speed + up * info.initial_up_velocity;
    const Vec3 actual_velocity =
      base_velocity + up * random.grenade_up + right * random.grenade_right;
    const Vec3 actual_angles =
      aimbot_direction_to_angles(normalized(actual_velocity));
    const Vec3 error = aimbot_normalize_angle_delta(launch_angles, actual_angles);
    compensated = aimbot_clamp_angles(compensated + error);
    if (!finite(compensated)) {
      return desired_angles;
    }
    if (std::fabs(error.x) < 0.02f && std::fabs(error.y) < 0.02f) {
      break;
    }
  }

  return aimbot_clamp_angles(compensated);
}

}
}
#endif
