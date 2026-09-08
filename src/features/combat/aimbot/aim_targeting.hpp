#ifndef AIM_TARGETING_HPP
#define AIM_TARGETING_HPP
#include <algorithm>
#include <array>
#include <cstddef>
#include "aim_state.hpp"
#include "aim_spread.hpp"
#include "aim_utils.hpp"
#include "aimbot.hpp"
#include "hitscan_aim.hpp"
#include "melee_aim.hpp"
#include "core/entity_cache.hpp"
#include "features/combat/backtrack/backtrack.hpp"
#include "features/automation/navbot/navbot_controller.hpp"

namespace aim_targeting {

inline bool hitscan_fast_head_backtrack_better(const aimbot_candidate& candidate,
  const aimbot_candidate& best) {
  if (!candidate.backtrack ||
      best.entity == nullptr ||
      best.backtrack ||
      candidate.player == nullptr ||
      candidate.player != best.player ||
      candidate.hitbox != aim_hitbox_head ||
      best.hitbox != aim_hitbox_head) {
    return false;
  }

  const float target_speed = aimbot_candidate_target_speed(candidate);
  return target_speed >= 360.0f && candidate.fov <= best.fov + 1.5f;
}

inline bool hitscan_ready_candidate_better(const aimbot_candidate& candidate,
  const aimbot_candidate& best) {
  return hitscan_fast_head_backtrack_better(candidate, best) ||
    aimbot_candidate_better(candidate, best);
}

inline void consider_non_player_target(Player* localplayer,
  Weapon* weapon,
  Entity* entity,
  const Vec3& original_view_angles,
  aimbot_candidate* best_candidate) {
  if (best_candidate == nullptr || aimbot_is_projectile_weapon(weapon)) {
    return;
  }

  const aimbot_candidate candidate = aimbot_is_melee_weapon(weapon)
    ? aimbot_find_non_player_candidate(localplayer, weapon, entity, original_view_angles)
    : hitscan_aim_find_non_player_candidate(localplayer, weapon, entity, original_view_angles);
  if (candidate.entity == nullptr || !candidate.visible || !aimbot_fov_within_limit(candidate.fov)) {
    return;
  }

  if (aimbot_candidate_better(candidate, *best_candidate)) {
    *best_candidate = candidate;
  }
}

inline aimbot_candidate find_best_non_player_candidate(Player* localplayer,
  Weapon* weapon,
  const Vec3& original_view_angles) {
  aimbot_candidate best_candidate{};
  if (localplayer == nullptr || weapon == nullptr || aimbot_is_projectile_weapon(weapon)) {
    return best_candidate;
  }

  constexpr class_id building_ids[] = {
    class_id::SENTRY,
    class_id::DISPENSER,
    class_id::TELEPORTER
  };
  for (const class_id building_id : building_ids) {
    for (Entity* entity : entity_cache[building_id]) {
      consider_non_player_target(localplayer, weapon, entity, original_view_angles, &best_candidate);
    }
  }

  for (Entity* entity : entity_cache_npcs()) {
    consider_non_player_target(localplayer, weapon, entity, original_view_angles, &best_candidate);
  }
  for (Entity* entity : entity_cache_entities(class_id::PILL_OR_STICKY)) {
    consider_non_player_target(localplayer, weapon, entity, original_view_angles, &best_candidate);
  }
  for (Entity* entity : entity_cache_entities(class_id::PUMPKIN)) {
    consider_non_player_target(localplayer, weapon, entity, original_view_angles, &best_candidate);
  }

  return best_candidate;
}

struct target_hint {
  Player* player = nullptr;
  float fov = FLT_MAX;
  float distance = FLT_MAX;
  int health = 0;
  bool preferred = false;
};

inline bool target_hint_better(const target_hint& candidate, const target_hint& best) {
  if (candidate.player == nullptr) return false;
  if (best.player == nullptr) return true;

  switch (config.aimbot.target_type) {
  case Aim::TargetType::DISTANCE:
    return candidate.distance * (candidate.preferred ? 0.35f : 1.0f) <
      best.distance * (best.preferred ? 0.35f : 1.0f);
  case Aim::TargetType::LEAST_HEALTH:
    if (candidate.health - (candidate.preferred ? 500 : 0) ==
        best.health - (best.preferred ? 500 : 0)) {
      return candidate.fov * (candidate.preferred ? 0.2f : 1.0f) <
        best.fov * (best.preferred ? 0.2f : 1.0f);
    }
    return candidate.health - (candidate.preferred ? 500 : 0) <
      best.health - (best.preferred ? 500 : 0);
  case Aim::TargetType::MOST_HEALTH:
    if (candidate.health + (candidate.preferred ? 500 : 0) ==
        best.health + (best.preferred ? 500 : 0)) {
      return candidate.fov * (candidate.preferred ? 0.2f : 1.0f) <
        best.fov * (best.preferred ? 0.2f : 1.0f);
    }
    return candidate.health + (candidate.preferred ? 500 : 0) >
      best.health + (best.preferred ? 500 : 0);
  case Aim::TargetType::FOV:
  default:
    return candidate.fov * (candidate.preferred ? 0.2f : 1.0f) <
      best.fov * (best.preferred ? 0.2f : 1.0f);
  }
}

inline bool target_hint_selected(Player* player,
  const std::array<target_hint, 6>& hints,
  std::size_t hint_count) {
  for (std::size_t index = 0; index < hint_count; ++index) {
    if (hints[index].player == player) return true;
  }
  return false;
}

inline aimbot_candidate find_best_candidate(Player* localplayer,
  Weapon* weapon,
  user_cmd* user_cmd,
  const Vec3& original_view_angles) {
  aimbot_candidate best_candidate{};
  aimbot_candidate best_ready_hitscan_candidate{};
  aim_state::scan = {};

  if (localplayer == nullptr || weapon == nullptr || aimbot_is_projectile_weapon(weapon)) {
    return best_candidate;
  }

  const bool hitscan_ready_selection = !aimbot_is_melee_weapon(weapon);
  Player* navbot_melee_target = aimbot_is_melee_weapon(weapon)
    ? navbot::controller().melee_target()
    : nullptr;
  const std::size_t max_target_count = static_cast<std::size_t>(std::clamp(config.aimbot.max_targets, 1, 6));
  std::array<target_hint, 6> target_hints{};
  std::size_t target_hint_count = 0;
  const Vec3 shoot_pos = hitscan_ready_selection
    ? hitscan_aim_eye_position(localplayer)
    : localplayer->get_shoot_pos();

  for (const entity_cache_player_entry& entry : entity_cache_players()) {
    Player* player = entry.player;
    ++aim_state::scan.candidates_total;
    if (const auto skip_reason = aimbot_player_skip_reason_for(localplayer, entry, weapon);
        skip_reason != aimbot_player_skip_reason::none) {
      aim_state::record_player_skip(skip_reason, player);
      continue;
    }

    const Vec3 target_origin = player->get_origin();
    const target_hint hint{
      .player = player,
      .fov = aimbot_calculate_fov(
        aimbot_calculate_angles_to_position(shoot_pos, target_origin), original_view_angles),
      .distance = aimbot_distance_squared(shoot_pos, target_origin),
      .health = player->get_health(),
      .preferred = aimbot_player_is_preferred(player)
    };

    if (target_hint_count < max_target_count) {
      target_hints[target_hint_count++] = hint;
      continue;
    }

    std::size_t worst_index = 0;
    for (std::size_t index = 1; index < target_hint_count; ++index) {
      if (target_hint_better(target_hints[worst_index], target_hints[index])) {
        worst_index = index;
      }
    }
    if (target_hint_better(hint, target_hints[worst_index])) {
      target_hints[worst_index] = hint;
    }
  }

  for (const entity_cache_player_entry& entry : entity_cache_players()) {
    Player* player = entry.player;
    const bool forced_navbot_target = player == navbot_melee_target;
    if (!forced_navbot_target && !target_hint_selected(player, target_hints, target_hint_count)) continue;

    aimbot_candidate candidate = aimbot_is_melee_weapon(weapon)
      ? melee_aim_find_candidate(localplayer, weapon, player, user_cmd, original_view_angles)
      : hitscan_aim_find_candidate(localplayer, weapon, player, original_view_angles);
    if (!aimbot_is_melee_weapon(weapon)) {
      const aimbot_candidate backtrack_candidate = backtrack::find_hitscan_candidate(
        localplayer, weapon, player, original_view_angles, aimbot_player_is_preferred(player));
      if (aimbot_candidate_better(backtrack_candidate, candidate)) {
        candidate = backtrack_candidate;
      }
    }

    if (candidate.entity == nullptr) {
      const aimbot_reject_debug reject = candidate.reject_debug.reason != aimbot_reject_reason::none
        ? candidate.reject_debug
        : aim_state::make_reject_debug(player, aimbot_reject_reason::no_candidate);
      aim_state::record_reject(reject);
      continue;
    }

    if (candidate.visible) ++aim_state::scan.candidates_visible;
    const float fov_limit = aimbot_fov_limit(candidate.preferred ? 1.35f : 1.0f);
    if (!candidate.visible || (!forced_navbot_target &&
        aimbot_fov_exceeds_limit(candidate.fov, candidate.preferred ? 1.35f : 1.0f))) {
      aim_state::record_reject(aim_state::make_candidate_reject_debug(
        candidate,
        candidate.visible ? aimbot_reject_reason::fov : aimbot_reject_reason::not_visible,
        fov_limit));
      continue;
    }

    if (forced_navbot_target || aimbot_candidate_better(candidate, best_candidate)) {
      best_candidate = candidate;
    }

    if (hitscan_ready_selection &&
        aim_spread::hitscan_candidate_ready_for_selection(localplayer, weapon, user_cmd, candidate) &&
        hitscan_ready_candidate_better(candidate, best_ready_hitscan_candidate)) {
      best_ready_hitscan_candidate = candidate;
    }
  }

  const aimbot_candidate non_player_candidate = find_best_non_player_candidate(
    localplayer, weapon, original_view_angles);
  if (navbot_melee_target == nullptr && aimbot_candidate_better(non_player_candidate, best_candidate)) {
    best_candidate = non_player_candidate;
  }

  if (best_ready_hitscan_candidate.entity != nullptr &&
      best_candidate.player != nullptr &&
      (!aim_spread::hitscan_candidate_ready_for_selection(localplayer, weapon, user_cmd, best_candidate) ||
        hitscan_fast_head_backtrack_better(best_ready_hitscan_candidate, best_candidate))) {
    best_candidate = best_ready_hitscan_candidate;
  }

  return best_candidate;
}

inline aimbot_candidate find_best_scope_candidate(Player* localplayer,
  Weapon* weapon,
  const Vec3& original_view_angles) {
  aimbot_candidate best_candidate{};
  if (localplayer == nullptr || weapon == nullptr ||
      localplayer->get_tf_class() != tf_class::SNIPER || !weapon->is_sniper_rifle()) {
    return best_candidate;
  }

  for (const entity_cache_player_entry& entry : entity_cache_players()) {
    Player* player = entry.player;
    ++aim_state::scan.candidates_total;
    if (const auto skip_reason = aimbot_player_skip_reason_for(localplayer, entry, weapon);
        skip_reason != aimbot_player_skip_reason::none) {
      aim_state::record_player_skip(skip_reason, player);
      continue;
    }

    aimbot_candidate candidate = hitscan_aim_find_occluded_candidate(
      localplayer, weapon, player, original_view_angles);
    if (candidate.player == nullptr || !aimbot_fov_within_limit(
        candidate.fov, candidate.preferred ? 1.35f : 1.0f)) {
      continue;
    }
    if (aimbot_candidate_better(candidate, best_candidate)) {
      best_candidate = candidate;
    }
  }

  return best_candidate;
}

}
#endif
