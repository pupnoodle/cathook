/*
/^-----^\   data: 2026-05-15
V  o o  V  file: src/features/combat/aimbot/proj_aim/proj_aim_find_candidate.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef PROJ_AIM_FIND_CANDIDATE_HPP
#define PROJ_AIM_FIND_CANDIDATE_HPP

#include "proj_aim_budget.hpp"
#include "proj_aim_splash.hpp"


inline aimbot_candidate proj_aim_find_candidate(Player* localplayer, Weapon* weapon, Player* player, user_cmd* user_cmd, const Vec3& original_view_angles) {
  aimbot_candidate candidate{};
  if (localplayer == nullptr || weapon == nullptr || player == nullptr || user_cmd == nullptr) return candidate;

  const proj_aim_weapon_profile profile = proj_aim_profile_for_weapon(weapon);
  if (profile.params.speed <= 0.0f) {
    return candidate;
  }

  proj_aim_budget_guard budget_scope{};
  proj_aim_budget_begin_for_distance(distance_3d(localplayer->get_shoot_pos(), player->get_origin()));

  LocalPredictionEntityPath target_path = local_prediction_predict_entity_path(player, profile.params.max_time, true, true);
  if (!target_path.valid || target_path.positions.empty()) {
    return candidate;
  }

  const uint32_t configured_hitbox_mask = config.aimbot.projectile_hitboxes;
  const uint32_t hitbox_mask = proj_aim_effective_hitbox_mask(localplayer, weapon, player);
  proj_aim_reset_debug_stats(weapon, player, target_path, configured_hitbox_mask, hitbox_mask);
  const std::vector<proj_aim_direct_point> direct_points = proj_aim_direct_points(localplayer, weapon, player, hitbox_mask);
  if (config.aimbot.projectile_splash_debug) {
    proj_aim_current_debug_stats.direct_points = static_cast<int>(direct_points.size());
  }
  std::vector<proj_aim_direct_history> direct_history{};
  if (config.aimbot.projectile_splash_debug) {
    direct_history.reserve(direct_points.size());
  }

  aimbot_candidate direct_candidate{};
  int direct_candidate_priority = INT_MAX;
  if (profile.supports_direct) {
    for (const proj_aim_direct_point& sample : direct_points) {
      if (config.aimbot.projectile_splash_debug) {
        ++proj_aim_current_debug_stats.direct_solves;
      }
      LocalPredictionInterceptResult intercept = local_prediction_find_projectile_intercept(
        localplayer,
        weapon,
        target_path,
        sample.offset,
        user_cmd,
        profile.params.max_time);
      if (!intercept.valid || intercept.intercept_time < profile.arm_time) {
        continue;
      }
      if (config.aimbot.projectile_splash_debug) {
        ++proj_aim_current_debug_stats.direct_intercepts;
      }

      const float direct_fov = aimbot_calculate_fov(intercept.aim_angles, original_view_angles);
      if (!aimbot_fov_within_limit(direct_fov, 1.2f, 3.0f)) {
        continue;
      }

      if (!proj_aim_trace_path(localplayer, player, weapon, intercept)) {
        if (config.aimbot.projectile_splash_debug) {
          ++proj_aim_current_debug_stats.direct_trace_rejects;
        }
        continue;
      }

      if (config.aimbot.projectile_splash_debug) {
        direct_history.push_back({
          .predicted_origin = intercept.has_target_base_origin
            ? intercept.target_base_origin
            : intercept.target_origin - sample.offset,
          .point = sample,
          .intercept = intercept,
          .fov = direct_fov
        });
      }

      aimbot_candidate point_candidate{};
      point_candidate.entity = player;
      point_candidate.player = player;
      point_candidate.preferred = has_aimbot_preference(player);
      point_candidate.bone = sample.bone;
      point_candidate.hitbox = sample.hitbox;
      point_candidate.aim_position = intercept.target_origin;
      point_candidate.aim_angles = intercept.aim_angles;
      point_candidate.fov = direct_fov;
      point_candidate.distance = intercept.intercept_distance;
      point_candidate.health = player->get_health();
      point_candidate.visible = true;
      point_candidate.projectile_direct = true;
      point_candidate.projectile_has_target_base_origin = intercept.has_target_base_origin;
      point_candidate.projectile_intercept_time = intercept.intercept_time;
      point_candidate.projectile_miss_distance = intercept.miss_distance;
      point_candidate.projectile_target_base_origin = intercept.target_base_origin;
      point_candidate.projectile_target_offset = intercept.target_offset;

      const int point_priority = sample.priority;

      if (direct_candidate.entity == nullptr ||
          point_priority < direct_candidate_priority ||
          (point_priority == direct_candidate_priority && point_candidate.projectile_miss_distance + 1.0f < direct_candidate.projectile_miss_distance) ||
          (point_priority == direct_candidate_priority && std::fabs(point_candidate.projectile_miss_distance - direct_candidate.projectile_miss_distance) <= 1.0f &&
           point_candidate.fov < direct_candidate.fov) ||
          (point_priority == direct_candidate_priority && std::fabs(point_candidate.fov - direct_candidate.fov) <= 0.01f &&
           point_candidate.distance < direct_candidate.distance)) {
        proj_aim_store_debug_path(target_path, intercept, point_candidate);
        direct_candidate_priority = point_priority;
        if (config.aimbot.projectile_splash_debug) {
          ++proj_aim_current_debug_stats.direct_candidates;
          proj_aim_current_debug_stats.best_direct = true;
          proj_aim_current_debug_stats.best_time = intercept.intercept_time;
          proj_aim_current_debug_stats.best_fov = direct_fov;
          proj_aim_current_debug_stats.best_direct_miss = intercept.miss_distance;
        }
        direct_candidate = point_candidate;
      }
    }
  }

  const bool direct_confident = proj_aim_direct_candidate_confident(profile, direct_candidate);
  if (config.aimbot.projectile_mode == Aim::ProjectileMode::DIRECT_THEN_SPLASH && direct_confident) {
    if (config.aimbot.projectile_splash_debug) {
      proj_aim_last_direct_history = std::move(direct_history);
      proj_aim_last_splash_history.clear();
      proj_aim_commit_debug_stats();
    }
    return direct_candidate;
  }

  aimbot_candidate splash_candidate{};
  if (profile.supports_splash) {
    splash_candidate = proj_aim_find_splash_candidate(
      localplayer,
      weapon,
      player,
      user_cmd,
      original_view_angles,
      target_path);
  }

  if (config.aimbot.projectile_splash_debug) {
    proj_aim_last_direct_history = std::move(direct_history);
  }

  aimbot_candidate result{};
  switch (config.aimbot.projectile_mode) {
  case Aim::ProjectileMode::DIRECT_ONLY:
    result = direct_candidate;
    break;
  case Aim::ProjectileMode::DIRECT_THEN_SPLASH:
    if (direct_candidate.player != nullptr && (splash_candidate.player == nullptr || direct_confident)) {
      result = direct_candidate;
      break;
    }
    result = splash_candidate.player != nullptr ? splash_candidate : direct_candidate;
    break;
  case Aim::ProjectileMode::PREFER_SPLASH:
    result = splash_candidate.player != nullptr ? splash_candidate : direct_candidate;
    break;
  case Aim::ProjectileMode::SPLASH_ONLY:
    result = splash_candidate;
    break;
  default:
    result = direct_candidate;
    break;
  }

  if (config.aimbot.projectile_splash_debug) {
    proj_aim_commit_debug_stats();
  }
  return result;
}

#endif
