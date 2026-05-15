/*
/^-----^\   data: 2026-05-15
V  o o  V  file: src/features/combat/aimbot/proj_aim/proj_aim_splash.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef PROJ_AIM_SPLASH_HPP
#define PROJ_AIM_SPLASH_HPP

#include "proj_aim_budget.hpp"
#include "proj_aim_trace.hpp"

inline std::vector<proj_aim_path_sample> proj_aim_limited_path_samples(const LocalPredictionEntityPath& target_path) {
  std::vector<proj_aim_path_sample> samples{};
  if (!target_path.valid || target_path.positions.empty()) {
    return samples;
  }

  int path_steps_cfg = config.aimbot.projectile_path_steps;
  if (proj_aim_budget().active) {
    path_steps_cfg = std::max(2, (path_steps_cfg * proj_aim_budget().path_steps_percent) / 100);
  }
  const size_t desired_steps = static_cast<size_t>(std::clamp(path_steps_cfg, 2, 64));
  const size_t available_steps = target_path.positions.size();
  const size_t output_steps = std::min(desired_steps, available_steps);
  samples.reserve(output_steps);

  if (output_steps == 1) {
    samples.push_back({
      .time = target_path.start_time,
      .position = target_path.positions.front()
    });
    return samples;
  }

  const double last_index = static_cast<double>(available_steps - 1);
  const double step = last_index / static_cast<double>(output_steps - 1);
  for (size_t index = 0; index < output_steps; ++index) {
    const size_t source_index = static_cast<size_t>(std::clamp(
      static_cast<int>(std::llround(step * static_cast<double>(index))),
      0,
      static_cast<int>(available_steps - 1)));
    samples.push_back({
      .time = target_path.start_time + (static_cast<float>(source_index) * target_path.time_step),
      .position = target_path.positions[source_index]
    });
  }

  return samples;
}

inline void proj_aim_splash_sample_offsets(const Vec3& local_origin,
  const Vec3& target_origin,
  float radius,
  int sample_count,
  bool allow_wall_splash,
  bool allow_seam_shot,
  std::vector<Vec3>* offsets) {
  if (offsets == nullptr) {
    return;
  }

  const Vec3 to_local = local_prediction_normalize(local_origin - target_origin);
  const float outer = radius * 0.92f;
  const float seam = radius * 0.6f;
  const size_t max_samples = static_cast<size_t>(std::clamp(sample_count, 4, 64));

  offsets->clear();
  offsets->reserve(max_samples + 6);
  offsets->emplace_back(Vec3{to_local.x * outer, to_local.y * outer, -outer * 0.22f});
  offsets->emplace_back(Vec3{0.0f, 0.0f, -outer});
  offsets->emplace_back(Vec3{outer * 0.38f, 0.0f, -outer * 0.72f});
  offsets->emplace_back(Vec3{-outer * 0.38f, 0.0f, -outer * 0.72f});
  offsets->emplace_back(Vec3{0.0f, 0.0f, outer});
  offsets->emplace_back(Vec3{to_local.x * outer, to_local.y * outer, 0.0f});

  constexpr float golden_angle = 2.39996323f;
  for (size_t index = 0; offsets->size() < max_samples && index < max_samples * 3; ++index) {
    const float t = static_cast<float>(index) + 0.5f;
    const float y = 1.0f - (2.0f * t / static_cast<float>(max_samples * 3));
    const float radial = std::sqrt(std::max(0.0f, 1.0f - (y * y)));
    const float theta = golden_angle * static_cast<float>(index);
    Vec3 offset{
      std::cos(theta) * radial * outer,
      std::sin(theta) * radial * outer,
      y * outer
    };

    if (!allow_wall_splash && offset.z > 0.0f) {
      continue;
    }

    if (!allow_seam_shot && std::fabs(offset.z) > seam) {
      continue;
    }

    offsets->emplace_back(offset);
  }

  if (allow_seam_shot && offsets->size() < max_samples) {
    offsets->emplace_back(Vec3{to_local.x * seam, to_local.y * seam, seam});
    if (offsets->size() < max_samples) {
      offsets->emplace_back(Vec3{to_local.x * seam, to_local.y * seam, -seam});
    }
  }

  if (offsets->size() > max_samples) {
    offsets->resize(max_samples);
  }
}

inline std::vector<Vec3> proj_aim_splash_sample_offsets(const Vec3& local_origin,
  const Vec3& target_origin,
  float radius,
  int sample_count,
  bool allow_wall_splash,
  bool allow_seam_shot) {
  std::vector<Vec3> offsets{};
  proj_aim_splash_sample_offsets(
    local_origin,
    target_origin,
    radius,
    sample_count,
    allow_wall_splash,
    allow_seam_shot,
    &offsets);

  return offsets;
}

inline bool proj_aim_splash_point_exists(const std::vector<proj_aim_splash_point>& points, const Vec3& point) {
  constexpr float duplicate_distance_sqr = 9.0f;
  for (const proj_aim_splash_point& existing : points) {
    if (aimbot_distance_squared(existing.point, point) <= duplicate_distance_sqr) {
      return true;
    }
  }

  return false;
}

inline void proj_aim_add_splash_point(std::vector<proj_aim_splash_point>* points, const Vec3& point) {
  if (points == nullptr || !aimbot_vec3_is_finite(point) || proj_aim_splash_point_exists(*points, point)) {
    return;
  }

  points->push_back({
    .point = point
  });
}

inline bool proj_aim_trace_splash_surface(const Vec3& local_origin,
  const Vec3& trace_start,
  const Vec3& trace_end,
  const Vec3& hull,
  Vec3* point_out) {
  if (point_out == nullptr) {
    return false;
  }

  Vec3 hit_pos{};
  Vec3 hit_normal{};
  if (!splash_trace_cache::trace_world_hull_brush_cached(trace_start, trace_end, hull, &hit_pos, &hit_normal)) {
    return false;
  }

  const Vec3 to_local = local_prediction_normalize(local_origin - hit_pos);
  if (!local_prediction_vec3_is_zero(to_local)) {
    const float normal_dot =
      (to_local.x * hit_normal.x) +
      (to_local.y * hit_normal.y) +
      (to_local.z * hit_normal.z);
    if (normal_dot < -0.05f) {
      return false;
    }
  }

  *point_out = hit_pos;
  return true;
}

inline void proj_aim_splash_sample_points(Player* player,
  const proj_aim_weapon_profile& profile,
  const Vec3& local_origin,
  const Vec3& predicted_origin,
  int sample_count,
  bool allow_wall_splash,
  bool allow_seam_shot,
  std::vector<Vec3>* offsets,
  std::vector<proj_aim_splash_point>* points) {
  if (points != nullptr) {
    points->clear();
  }
  if (player == nullptr || offsets == nullptr || points == nullptr || profile.splash_radius <= 0.0f) {
    return;
  }

  const Vec3 mins = player->get_player_mins(player->is_ducking());
  const Vec3 maxs = player->get_player_maxs(player->is_ducking());
  const Vec3 bounds_center = (mins + maxs) * 0.5f;
  const Vec3 bounds_extent = (maxs - mins) * 0.5f;
  const Vec3 target_center = predicted_origin + bounds_center;
  const Vec3 target_eye = predicted_origin + player->get_view_offset();
  const float bounds_radius = local_prediction_vector_length(bounds_extent);
  const float surface_probe_radius = profile.splash_radius + bounds_radius;
  const int max_samples = std::clamp(sample_count, 4, 64);
  const int surface_samples = std::clamp((max_samples + 3) / 4, 3, 12);

  proj_aim_splash_sample_offsets(
    local_origin,
    predicted_origin,
    surface_probe_radius,
    surface_samples,
    allow_wall_splash,
    allow_seam_shot,
    offsets);

  points->reserve((offsets->size() * 2) + static_cast<size_t>(max_samples));

  constexpr float surface_probe_hull = 1.0f;
  const Vec3 trace_hull{surface_probe_hull, surface_probe_hull, surface_probe_hull};

  for (const Vec3& offset : *offsets) {
    const Vec3 probe = target_center + offset;
    Vec3 surface_point{};
    if (proj_aim_trace_splash_surface(local_origin, target_eye, probe, trace_hull, &surface_point) &&
        distance_3d(surface_point, target_center) <= surface_probe_radius + 2.0f) {
      proj_aim_add_splash_point(points, surface_point);
    }

    if (proj_aim_trace_splash_surface(local_origin, target_center, probe, trace_hull, &surface_point) &&
        distance_3d(surface_point, target_center) <= surface_probe_radius + 2.0f) {
      proj_aim_add_splash_point(points, surface_point);
    }
  }

  proj_aim_splash_sample_offsets(
    local_origin,
    predicted_origin,
    profile.splash_radius,
    max_samples,
    allow_wall_splash,
    allow_seam_shot,
    offsets);

  for (const Vec3& offset : *offsets) {
    proj_aim_add_splash_point(points, predicted_origin + offset);
  }
}

inline bool proj_aim_direct_candidate_confident(const proj_aim_weapon_profile& profile, const aimbot_candidate& candidate) {
  if (candidate.player == nullptr || !candidate.projectile_direct) {
    return false;
  }

  const float confidence_miss = std::max(profile.hull_radius * 1.5f, 6.0f);
  return candidate.projectile_miss_distance <= confidence_miss;
}

inline aimbot_candidate proj_aim_find_splash_candidate(Player* localplayer,
  Weapon* weapon,
  Player* player,
  user_cmd* user_cmd,
  const Vec3& original_view_angles,
  const LocalPredictionEntityPath& target_path) {
  aimbot_candidate best_candidate{};
  if (localplayer == nullptr || weapon == nullptr || player == nullptr || user_cmd == nullptr || !target_path.valid || target_path.positions.empty()) {
    return best_candidate;
  }

  const proj_aim_weapon_profile profile = proj_aim_profile_for_weapon(weapon);
  const float splash_radius = profile.splash_radius;
  if (splash_radius <= 0.0f) {
    return best_candidate;
  }

  const float arm_time = profile.arm_time;
  const Vec3 local_origin = localplayer->get_shoot_pos();
  const uint32_t hitbox_mask = proj_aim_effective_hitbox_mask(localplayer, weapon, player);
  const std::vector<proj_aim_path_sample> predicted_samples = proj_aim_limited_path_samples(target_path);
  const int splash_samples = std::clamp(config.aimbot.projectile_splash_samples, 4, 64);
  const std::vector<proj_aim_hitbox_sample> hitbox_samples = proj_aim_hitbox_samples(player, hitbox_mask);
  const int splash_solve_budget = [&]() {
    int base = std::clamp(
      static_cast<int>(predicted_samples.size()) * splash_samples,
      24,
      config.aimbot.projectile_splash_debug ? 768 : 384);
    if (proj_aim_budget().active) {
      base = std::max(16, (base * proj_aim_budget().splash_solve_budget_percent) / 100);
    }
    return base;
  }();
  int splash_solves = 0;
  std::vector<Vec3> sample_offsets{};
  sample_offsets.reserve((static_cast<size_t>(splash_samples) + 6) * 2);
  std::vector<proj_aim_splash_point> splash_points{};
  splash_points.reserve((static_cast<size_t>(splash_samples) + 6) * 2);
  std::vector<proj_aim_splash_history> splash_history{};
  if (config.aimbot.projectile_splash_debug) {
    splash_history.reserve(predicted_samples.size() * static_cast<size_t>(std::min(splash_samples, 12)));
    proj_aim_current_debug_stats.splash_path_samples = static_cast<int>(predicted_samples.size());
  }

  bool splash_budget_exhausted = false;
  for (const proj_aim_path_sample& predicted_sample : predicted_samples) {
    const int remaining_solves = splash_solve_budget - splash_solves;
    const int remaining_budget = proj_aim_budget_remaining_splash_candidates();
    const int available_samples = std::min(remaining_solves, remaining_budget);
    if (available_samples <= 0) {
      splash_budget_exhausted = remaining_budget <= 0;
      break;
    }

    const Vec3& predicted_origin = predicted_sample.position;
    const Vec3 predicted_angles = aimbot_calculate_angles_to_position(local_origin, predicted_origin);
    const float predicted_fov = aimbot_calculate_fov(predicted_angles, original_view_angles);
    if (!aimbot_fov_within_limit(predicted_fov, 1.35f, 3.0f)) {
      continue;
    }

    const int current_splash_samples = std::clamp(std::min(splash_samples, available_samples), 4, 64);
    proj_aim_splash_sample_points(
      player,
      profile,
      local_origin,
      predicted_origin,
      current_splash_samples,
      config.aimbot.projectile_wall_splash,
      config.aimbot.projectile_seam_shot,
      &sample_offsets,
      &splash_points);
    if (splash_points.size() > static_cast<size_t>(available_samples)) {
      splash_points.resize(static_cast<size_t>(available_samples));
    }
    if (config.aimbot.projectile_splash_debug) {
      proj_aim_current_debug_stats.splash_offsets += static_cast<int>(splash_points.size());
    }

    for (const proj_aim_splash_point& sample_point : splash_points) {
      if (splash_solves >= splash_solve_budget) {
        break;
      }
      if (!proj_aim_budget_try_splash_candidate()) {
        splash_budget_exhausted = true;
        break;
      }

      const Vec3& splash_point = sample_point.point;
      if (config.aimbot.projectile_splash_debug) {
        ++proj_aim_current_debug_stats.splash_solves;
      }
      ++splash_solves;

      const LocalPredictionInterceptResult intercept = local_prediction_find_projectile_intercept(
        localplayer,
        weapon,
        splash_point,
        Vec3{},
        user_cmd,
        profile.params.max_time);
      if (!intercept.valid) {
        continue;
      }
      if (config.aimbot.projectile_splash_debug) {
        ++proj_aim_current_debug_stats.splash_intercepts;
      }

      if (intercept.intercept_time < arm_time) {
        continue;
      }

      const float intercept_fov = aimbot_calculate_fov(intercept.aim_angles, original_view_angles);
      if (!aimbot_fov_within_limit(intercept_fov, 1.2f, 3.0f)) {
        continue;
      }

      const Vec3 impact_target_origin = local_prediction_path_position_at_time(target_path, intercept.intercept_time);
      const float time_error = std::fabs(intercept.intercept_time - predicted_sample.time);
      if (time_error > std::max(profile.params.time_step * 4.0f, 0.08f) &&
          distance_3d(impact_target_origin, predicted_origin) > splash_radius * 0.55f) {
        continue;
      }

      Vec3 explosion_origin{};
      if (!proj_aim_trace_splash_path(localplayer, player, weapon, intercept, splash_radius, hitbox_mask, &explosion_origin, &impact_target_origin, false)) {
        if (config.aimbot.projectile_splash_debug) {
          ++proj_aim_current_debug_stats.splash_trace_rejects;
        }
        continue;
      }

      if (config.aimbot.projectile_splash_debug) {
        splash_history.push_back({
          .predicted_origin = impact_target_origin,
          .splash_point = splash_point,
          .explosion_origin = explosion_origin,
          .intercept = intercept,
          .time_error = time_error,
          .fov = intercept_fov
        });
      }

      int best_hitbox = -1;
      int best_bone = 0;
      float best_point_distance = FLT_MAX;
      if (!proj_aim_predicted_explosion_can_damage(
          localplayer,
          player,
          explosion_origin,
          impact_target_origin,
          hitbox_samples,
          splash_radius,
          &best_hitbox,
          &best_bone,
          &best_point_distance)) {
        if (config.aimbot.projectile_splash_debug) {
          ++proj_aim_current_debug_stats.splash_damage_rejects;
        }
        continue;
      }

      aimbot_candidate splash_candidate{};
      splash_candidate.entity = player;
      splash_candidate.player = player;
      splash_candidate.preferred = has_aimbot_preference(player);
      splash_candidate.bone = best_bone;
      splash_candidate.hitbox = best_hitbox;
      splash_candidate.aim_position = explosion_origin;
      splash_candidate.aim_angles = intercept.aim_angles;
      splash_candidate.fov = intercept_fov;
      splash_candidate.distance = intercept.intercept_distance + 24.0f;
      splash_candidate.health = player->get_health();
      splash_candidate.visible = true;
      splash_candidate.projectile_splash = true;
      splash_candidate.projectile_has_target_base_origin = true;
      splash_candidate.projectile_intercept_time = intercept.intercept_time;
      splash_candidate.projectile_miss_distance = best_point_distance;
      splash_candidate.projectile_splash_radius = splash_radius;
      splash_candidate.projectile_target_base_origin = impact_target_origin;
      splash_candidate.projectile_target_offset = Vec3{};

      if (config.aimbot.projectile_splash_debug) {
        ++proj_aim_current_debug_stats.splash_candidates;
      }
      if (aimbot_candidate_better(splash_candidate, best_candidate)) {
        proj_aim_store_debug_path(target_path, intercept, splash_candidate);
        if (config.aimbot.projectile_splash_debug) {
          proj_aim_current_debug_stats.best_splash = true;
          proj_aim_current_debug_stats.best_time = intercept.intercept_time;
          proj_aim_current_debug_stats.best_fov = intercept_fov;
          proj_aim_current_debug_stats.best_splash_miss = best_point_distance;
        }
        best_candidate = splash_candidate;
      }
    }

    if (splash_budget_exhausted) {
      break;
    }
  }

  if (config.aimbot.projectile_splash_debug) {
    proj_aim_last_splash_history = std::move(splash_history);
  }
  return best_candidate;
}

#endif
