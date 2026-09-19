#ifndef AIM_SPREAD_HPP
#define AIM_SPREAD_HPP
#include <algorithm>
#include <array>
#include <cmath>
#include <climits>
#include <cstdint>
#include <limits>
#include <vector>
#include "aim_utils.hpp"
#include "hitscan_aim.hpp"
#include "projectile_helpers.hpp"
#include "games/tf2/sdk/combat_offsets.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "seed_prediction.hpp"

namespace aim_spread {

using valve_random_stream = valve_random;

inline float weapon_hitscan_spread(Weapon* weapon) {
  return hitscan_aim_weapon_spread(weapon);
}

inline bool fixed_weapon_spreads_enabled() {
  if (convar_system == nullptr) {
    return false;
  }

  static Convar* fixed_weapon_spreads = convar_system->find_var("tf_use_fixed_weaponspreads");
  return fixed_weapon_spreads != nullptr && fixed_weapon_spreads->get_int() != 0;
}

inline int hitscan_weapon_data_bullets(Weapon* weapon) {
  if (weapon == nullptr) {
    return 1;
  }

  const uintptr_t data = weapon->get_weapon_data();
  if (data == 0) {
    return 1;
  }

  const int bullets_offset = Weapon::weapon_data_bullets_per_shot_offset();
  if (bullets_offset < 0) {
    return 1;
  }
  const int bullets = *reinterpret_cast<const int*>(data + bullets_offset);
  return bullets > 0 ? bullets : 1;
}

inline int hitscan_weapon_damage_type(Weapon* weapon) {
  if (weapon == nullptr) {
    return 0;
  }

  using get_damage_type_fn = int (*)(void*);
  if (auto* const direct = reinterpret_cast<get_damage_type_fn>(tf2_combat::get().get_damage_type_fn)) {
    return direct(weapon);
  }

  void** vtable = *reinterpret_cast<void***>(weapon);
  const std::size_t slot = tf2_combat::weapon::get_damage_type();
  if (vtable == nullptr || slot == 0 || vtable[slot] == nullptr) {
    return 0;
  }
  return reinterpret_cast<get_damage_type_fn>(vtable[slot])(weapon);
}

inline bool fixed_weapon_spread_active(Weapon* weapon, int pellet_count) {
  (void)pellet_count;
  if (weapon == nullptr || hitscan_weapon_data_bullets(weapon) <= 1) {
    return false;
  }

  constexpr int dmg_buckshot = 1 << 29;
  if ((hitscan_weapon_damage_type(weapon) & dmg_buckshot) == 0) {
    return false;
  }

  if (fixed_weapon_spreads_enabled()) {
    return true;
  }

  return attribute_manager != nullptr &&
    attribute_manager->attrib_hook_value(0, "fixed_shot_pattern", weapon->to_entity()) != 0;
}

inline int hitscan_spread_seed(user_cmd* user_cmd) {
  return seed_pred::hitscan_seed(user_cmd);
}

inline bool hitscan_perfect_first_shot(Weapon* weapon, int pellet_count) {
  if (weapon == nullptr || global_vars == nullptr) {
    return false;
  }

  const float elapsed = global_vars->curtime - weapon->get_last_attack();
  const float ready_time = pellet_count > 1 ? 0.25f : 1.25f;
  return std::isfinite(elapsed) && elapsed > ready_time;
}

inline float hitscan_first_shot_attrib_scale(Weapon* weapon) {
  if (weapon == nullptr || attribute_manager == nullptr) {
    return 0.0f;
  }

  const float scale = attribute_manager->attrib_hook_value(0.0f, "mult_spread_scale_first_shot", weapon->to_entity());
  return std::isfinite(scale) ? scale : 0.0f;
}

inline Vec3 fixed_hitscan_spread_offset(user_cmd* user_cmd, int pellet_index, int pellet_count, float spread) {
  static const std::array<Vec3, 10> small_pattern{{
    {0.0f, 0.0f, 0.0f},
    {1.0f, 0.0f, 0.0f},
    {-1.0f, 0.0f, 0.0f},
    {0.0f, -1.0f, 0.0f},
    {0.0f, 1.0f, 0.0f},
    {0.85f, -0.85f, 0.0f},
    {0.85f, 0.85f, 0.0f},
    {-0.85f, -0.85f, 0.0f},
    {-0.85f, 0.85f, 0.0f},
    {0.0f, 0.0f, 0.0f}
  }};
  static const std::array<Vec3, 15> large_pattern{{
    {0.0f, 0.0f, 0.0f},
    {-0.5f, 0.0f, 0.0f},
    {-1.0f, 0.0f, 0.0f},
    {0.5f, 0.0f, 0.0f},
    {1.0f, 0.0f, 0.0f},
    {0.0f, 0.5f, 0.0f},
    {-0.5f, 0.5f, 0.0f},
    {-1.0f, 0.5f, 0.0f},
    {0.5f, 0.5f, 0.0f},
    {1.0f, 0.5f, 0.0f},
    {0.0f, -0.5f, 0.0f},
    {-0.5f, -0.5f, 0.0f},
    {-1.0f, -0.5f, 0.0f},
    {0.5f, -0.5f, 0.0f},
    {1.0f, -0.5f, 0.0f}
  }};

  const int safe_pellet_index = std::max(0, pellet_index);
  if (pellet_count <= 14) {
    const Vec3 pattern = small_pattern[static_cast<std::size_t>(safe_pellet_index % 10)];
    return Vec3{pattern.x * spread * 0.5f, pattern.y * spread * 0.5f, 0.0f};
  }

  valve_random_stream stream{};
  stream.set_seed(hitscan_spread_seed(user_cmd) + safe_pellet_index);
  const Vec3 pattern = large_pattern[static_cast<std::size_t>(safe_pellet_index % 15)];
  return Vec3{
    (pattern.x + stream.random_float(-0.07f, 0.07f)) * spread,
    (pattern.y + stream.random_float(-0.07f, 0.07f)) * spread,
    0.0f
  };
}

inline bool hitscan_spread_offset(user_cmd* user_cmd,
  Weapon* weapon,
  int pellet_index,
  int pellet_count,
  float spread,
  bool fixed_spread,
  Vec3* offset_out) {
  if (offset_out == nullptr) {
    return false;
  }

  *offset_out = {};
  if (spread <= 0.0f) {
    return true;
  }
  if (user_cmd == nullptr) {
    return false;
  }

  if (fixed_spread) {
    *offset_out = fixed_hitscan_spread_offset(user_cmd, pellet_index, pellet_count, spread);
    return true;
  }

  const int safe_pellet_index = std::max(0, pellet_index);
  float spread_range = 0.5f;
  if (safe_pellet_index == 0 && hitscan_perfect_first_shot(weapon, pellet_count)) {
    const float first_shot_scale = hitscan_first_shot_attrib_scale(weapon);
    if (first_shot_scale == 0.0f) {
      return true;
    }
    spread_range = first_shot_scale;
  }

  valve_random_stream stream{};
  stream.set_seed(hitscan_spread_seed(user_cmd) + safe_pellet_index);
  offset_out->x = (stream.random_float(-spread_range, spread_range) + stream.random_float(-spread_range, spread_range)) * spread;
  offset_out->y = (stream.random_float(-spread_range, spread_range) + stream.random_float(-spread_range, spread_range)) * spread;
  return true;
}

inline Vec3 compensate_hitscan_spread(Player* localplayer, const Vec3& desired_command_angles, const Vec3& spread_offset) {
  if (localplayer == nullptr) {
    return desired_command_angles;
  }

  const Vec3 desired_bullet_angles = hitscan_aim_bullet_angles(localplayer, desired_command_angles);
  Vec3 adjusted_bullet_angles = desired_bullet_angles;
  for (int iteration = 0; iteration < 2; ++iteration) {
    Vec3 forward{};
    Vec3 right{};
    Vec3 up{};
    angle_vectors(adjusted_bullet_angles, &forward, &right, &up);
    Vec3 spread_direction = aimbot_normalize_vector(forward + (right * spread_offset.x) + (up * spread_offset.y));
    if (!aimbot_vec3_is_finite(spread_direction)) {
      break;
    }

    const Vec3 spread_angles = aimbot_direction_to_angles(spread_direction);
    const Vec3 error = aimbot_normalize_angle_delta(spread_angles, desired_bullet_angles);
    adjusted_bullet_angles = aimbot_clamp_angles(adjusted_bullet_angles - error);
  }

  return aimbot_clamp_angles(hitscan_aim_command_angles(localplayer, adjusted_bullet_angles));
}

inline bool& compensated_this_command = nospread::compensated_this_command;

inline void begin_command() {
  nospread::begin_command();
}

inline bool apply_command_projectile_nospread(Player* localplayer, Weapon* weapon, user_cmd* user_cmd) {
  projectile_aim::detail::projectile_info info{};
  if (!projectile_aim::detail::get_info(localplayer, weapon, info)) {
    return false;
  }
  user_cmd->view_angles = projectile_aim::detail::compensate_projectile_spread(
    localplayer, weapon, user_cmd, info, user_cmd->view_angles);
  nospread::compensated_this_command = true;
  return true;
}

inline bool apply_command_nospread(Player* localplayer, Weapon* weapon, user_cmd* user_cmd) {
  if (nospread::compensated_this_command ||
      localplayer == nullptr ||
      weapon == nullptr ||
      user_cmd == nullptr ||
      (user_cmd->buttons & IN_ATTACK) == 0 ||
      !config.aimbot.spread_compensation ||
      aimbot_is_melee_weapon(weapon)) {
    return false;
  }
  if (aimbot_is_projectile_weapon(weapon)) {
    return apply_command_projectile_nospread(localplayer, weapon, user_cmd);
  }

  const float spread = weapon_hitscan_spread(weapon);
  if (spread <= 0.00001f) {
    return false;
  }
  if (seed_pred::custom_random_seed() && !seed_pred::synced) {
    return false;
  }

  const int pellet_count = std::max(1, weapon->get_bullets_per_shot());
  if (hitscan_perfect_first_shot(weapon, pellet_count) &&
      hitscan_first_shot_attrib_scale(weapon) == 0.0f) {
    return false;
  }

  const bool use_fixed_spread = fixed_weapon_spread_active(weapon, pellet_count);
  Vec3 average_direction{};
  std::vector<Vec3> offsets(static_cast<std::size_t>(pellet_count));
  Vec3 base_forward{};
  Vec3 base_right{};
  Vec3 base_up{};
  angle_vectors(hitscan_aim_bullet_angles(localplayer, user_cmd->view_angles),
    &base_forward, &base_right, &base_up);

  for (int pellet_index = 0; pellet_index < pellet_count; ++pellet_index) {
    if (!hitscan_spread_offset(user_cmd, weapon, pellet_index, pellet_count, spread, use_fixed_spread,
          &offsets[static_cast<std::size_t>(pellet_index)])) {
      return false;
    }
    const Vec3 direction = aimbot_normalize_vector(
      base_forward + base_right * offsets[static_cast<std::size_t>(pellet_index)].x +
      base_up * offsets[static_cast<std::size_t>(pellet_index)].y);
    if (!aimbot_vec3_is_finite(direction)) {
      return false;
    }
    average_direction += direction;
  }

  average_direction = aimbot_normalize_vector(average_direction);
  if (!aimbot_vec3_is_finite(average_direction)) {
    return false;
  }

  int best_index = 0;
  float best_distance = std::numeric_limits<float>::max();
  for (int pellet_index = 0; pellet_index < pellet_count; ++pellet_index) {
    const Vec3 direction = aimbot_normalize_vector(
      base_forward + base_right * offsets[static_cast<std::size_t>(pellet_index)].x +
      base_up * offsets[static_cast<std::size_t>(pellet_index)].y);
    const Vec3 delta = direction - average_direction;
    const float distance = (delta.x * delta.x) + (delta.y * delta.y) + (delta.z * delta.z);
    if (distance < best_distance) {
      best_distance = distance;
      best_index = pellet_index;
    }
  }

  user_cmd->view_angles = compensate_hitscan_spread(
    localplayer, user_cmd->view_angles, offsets[static_cast<std::size_t>(best_index)]);
  compensated_this_command = true;
  return true;
}

struct hitscan_fire_solution {
  bool ready = false;
  bool spread_compensated = false;
  bool spread_signature = false;
  bool spread_fixed = false;
  bool seed_missing = false;
  bool hit_wrong_hitbox = false;
  int pellet_count = 0;
  int pellet_index = -1;
  int trace_hitbox = -1;
  int trace_entity_index = -1;
  int trace_contents = 0;
  float spread = 0.0f;
  float trace_fraction = 1.0f;
  Vec3 trace_end{};
  Vec3 command_angles{};
};

inline hitscan_fire_solution prepare_hitscan_fire_solution(Player* localplayer,
  Weapon* weapon,
  user_cmd* user_cmd,
  const aimbot_candidate& candidate,
  const Vec3& command_view_angles) {
  hitscan_fire_solution solution{};
  solution.command_angles = command_view_angles;
  solution.spread_signature = hitscan_aim_bullet_spread_signature_found;
  if (localplayer == nullptr || weapon == nullptr || user_cmd == nullptr || candidate.entity == nullptr) {
    return solution;
  }

  const float spread = weapon_hitscan_spread(weapon);
  const bool want_spread = config.aimbot.spread_compensation && spread > 0.00001f;
  const int pellet_count = want_spread ? std::max(1, weapon->get_bullets_per_shot()) : 1;
  const bool use_fixed_spread = want_spread && fixed_weapon_spread_active(weapon, pellet_count);
  const bool perfect_first = want_spread && hitscan_perfect_first_shot(weapon, pellet_count);
  const bool seed_ready = !seed_pred::custom_random_seed() || seed_pred::synced;
  const bool deterministic_first =
    (perfect_first && hitscan_first_shot_attrib_scale(weapon) == 0.0f) ||
    (use_fixed_spread && pellet_count <= 14);
  solution.spread = spread;
  solution.pellet_count = pellet_count;
  solution.spread_signature = hitscan_aim_bullet_spread_signature_found;
  solution.spread_fixed = use_fixed_spread;

  if (want_spread && !seed_ready && !deterministic_first) {
    solution.seed_missing = true;
  }

  const bool use_spread = want_spread && (seed_ready || deterministic_first);
  const int spread_pellet_count = (!seed_ready && deterministic_first) ? 1 : pellet_count;
  std::vector<std::pair<float, int>> pellet_order{};
  pellet_order.reserve(static_cast<std::size_t>(spread_pellet_count));
  Vec3 average_direction{};
  std::vector<Vec3> spread_offsets(static_cast<std::size_t>(spread_pellet_count));
  bool first_pellet_straight = !use_spread;
  if (use_spread) {
    Vec3 base_forward{};
    Vec3 base_right{};
    Vec3 base_up{};
    angle_vectors(hitscan_aim_bullet_angles(localplayer, command_view_angles),
      &base_forward, &base_right, &base_up);

    for (int pellet_index = 0; pellet_index < spread_pellet_count; ++pellet_index) {
      Vec3& spread_offset = spread_offsets[static_cast<std::size_t>(pellet_index)];
      if (!hitscan_spread_offset(user_cmd, weapon, pellet_index, pellet_count, spread, use_fixed_spread, &spread_offset)) {
        solution.seed_missing = true;
        return solution;
      }

      const Vec3 direction = aimbot_normalize_vector(
        base_forward + base_right * spread_offset.x + base_up * spread_offset.y);
      if (!aimbot_vec3_is_finite(direction)) {
        solution.seed_missing = true;
        return solution;
      }
      average_direction += direction;
    }

    first_pellet_straight =
      std::fabs(spread_offsets[0].x) <= 0.00001f && std::fabs(spread_offsets[0].y) <= 0.00001f;
    if (first_pellet_straight) {
      pellet_order.emplace_back(0.0f, 0);
    } else {
      average_direction = aimbot_normalize_vector(average_direction);
      if (!aimbot_vec3_is_finite(average_direction)) {
        average_direction = base_forward;
      }

      for (int pellet_index = 0; pellet_index < spread_pellet_count; ++pellet_index) {
        const Vec3 direction = aimbot_normalize_vector(
          base_forward + base_right * spread_offsets[static_cast<std::size_t>(pellet_index)].x +
          base_up * spread_offsets[static_cast<std::size_t>(pellet_index)].y);
        const Vec3 delta = direction - average_direction;
        pellet_order.emplace_back(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z, pellet_index);
      }
      std::sort(pellet_order.begin(), pellet_order.end(),
        [](const auto& left, const auto& right) { return left.first < right.first; });
    }
  } else {
    pellet_order.emplace_back(0.0f, 0);
  }

  for (const auto& [unused_score, pellet_index] : pellet_order) {
    (void)unused_score;
    const Vec3 spread_offset = use_spread
      ? spread_offsets[static_cast<std::size_t>(pellet_index)]
      : Vec3{};
    if (use_spread && !aimbot_vec3_is_finite(spread_offset)) {
      solution.seed_missing = true;
      break;
    }

    const bool compensate = use_spread && !first_pellet_straight;
    const Vec3 trace_angles = compensate
      ? compensate_hitscan_spread(localplayer, command_view_angles, spread_offset)
      : command_view_angles;

    hitscan_aim_trace_result trace_result{};
    if (!hitscan_aim_trace_candidate(
          localplayer, weapon, candidate, trace_angles, spread_offset, compensate, &trace_result)) {
      solution.trace_contents = trace_result.contents;
      solution.trace_fraction = trace_result.fraction;
      solution.trace_end = trace_result.end;
      if (hitscan_aim_same_entity(trace_result.entity, candidate.entity) &&
          candidate.hitbox >= 0 &&
          trace_result.hitbox >= 0 &&
          trace_result.hitbox != candidate.hitbox) {
        solution.hit_wrong_hitbox = true;
        solution.trace_hitbox = trace_result.hitbox;
        solution.trace_entity_index = trace_result.entity->get_index();
      }
      continue;
    }

    solution.ready = true;
    solution.command_angles = command_view_angles;
    solution.spread_compensated = compensate;
    solution.pellet_index = compensate ? pellet_index : (use_spread ? 0 : -1);
    solution.trace_hitbox = trace_result.hitbox;
    solution.trace_entity_index = trace_result.entity != nullptr ? trace_result.entity->get_index() : -1;
    solution.trace_contents = trace_result.contents;
    solution.trace_fraction = trace_result.fraction;
    solution.trace_end = trace_result.end;
    return solution;
  }

  return solution;
}

inline bool hitscan_candidate_ready_for_selection(Player* localplayer, Weapon* weapon, user_cmd* user_cmd, const aimbot_candidate& candidate) {
  if (localplayer == nullptr ||
      weapon == nullptr ||
      user_cmd == nullptr ||
      candidate.entity == nullptr ||
      aimbot_is_projectile_weapon(weapon) ||
      aimbot_is_melee_weapon(weapon)) {
    return false;
  }

  const Vec3 command_angles = candidate.player != nullptr
    ? candidate.command_angles
    : hitscan_aim_command_angles(localplayer, candidate.aim_angles);
  return prepare_hitscan_fire_solution(localplayer, weapon, user_cmd, candidate, command_angles).ready;
}

}
#endif
