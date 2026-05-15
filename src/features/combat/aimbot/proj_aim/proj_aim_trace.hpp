/*
/^-----^\   data: 2026-05-15
V  o o  V  file: src/features/combat/aimbot/proj_aim/proj_aim_trace.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef PROJ_AIM_TRACE_HPP
#define PROJ_AIM_TRACE_HPP

#include "proj_aim_budget.hpp"
#include "proj_aim_weapon.hpp"
#include "features/movement/local_prediction/local_prediction.hpp"

inline bool proj_aim_trace_between(const Vec3& start, const Vec3& end, Entity* skip_entity, Entity* target_entity) {
  if (engine_trace == nullptr) {
    return false;
  }

  const auto trace_clear = [&](const Vec3& trace_start) {
    trace_t trace{};
    if (!projectile_trace_ray(
        trace_start,
        end,
        nullptr,
        nullptr,
        projectile_trace_contract::radius_damage,
        skip_entity,
        skip_entity != nullptr ? static_cast<int>(skip_entity->get_team()) : -1,
        &trace)) {
      return false;
    }
    return trace.entity == target_entity || projectile_trace_clear(trace, 0.97f);
  };

  if (trace_clear(start)) {
    return true;
  }

  const Vec3 to_target = local_prediction_normalize(end - start);
  if (local_prediction_vec3_is_zero(to_target)) {
    return false;
  }

  return trace_clear(start + (to_target * 2.0f));
}

inline std::vector<proj_aim_hitbox_sample> proj_aim_hitbox_samples(Player* target, uint32_t hitbox_mask) {
  std::vector<proj_aim_hitbox_sample> samples{};
  if (target == nullptr) {
    return samples;
  }

  samples.reserve(8);
  const Vec3 origin = target->get_origin();
  for (int hitbox_id = aim_hitbox_head; hitbox_id <= aim_hitbox_right_foot; ++hitbox_id) {
    if (!aimbot_hitbox_matches_mask(hitbox_id, hitbox_mask)) {
      continue;
    }

    Vec3 point{};
    int bone = 0;
    if (!target->get_hitbox_center(hitbox_id, &point, &bone)) {
      continue;
    }

    samples.push_back({
      .hitbox = hitbox_id,
      .bone = bone,
      .priority = aimbot_hitbox_priority(nullptr, target, nullptr, hitbox_id),
      .offset = point - origin
    });
  }

  return samples;
}

inline std::vector<Vec3> proj_aim_target_points(Player* target, uint32_t hitbox_mask) {
  std::vector<Vec3> points{};
  if (target == nullptr) {
    return points;
  }

  points.reserve(8);
  for (int hitbox_id = aim_hitbox_head; hitbox_id <= aim_hitbox_right_foot; ++hitbox_id) {
    if (!aimbot_hitbox_matches_mask(hitbox_id, hitbox_mask)) {
      continue;
    }

    Vec3 point{};
    if (target->get_hitbox_center(hitbox_id, &point)) {
      points.emplace_back(point);
    }
  }

  if (points.empty()) {
    points.emplace_back(target->get_origin());
  }
  return points;
}

inline std::vector<proj_aim_direct_point> proj_aim_direct_points(Player* localplayer,
  Weapon* weapon,
  Player* target,
  uint32_t hitbox_mask) {
  std::vector<proj_aim_direct_point> points{};
  if (target == nullptr) {
    return points;
  }

  const Vec3 mins = target->get_player_mins(target->is_ducking());
  const Vec3 maxs = target->get_player_maxs(target->is_ducking());
  const auto add_point = [&](int hitbox, int bone, int priority, const Vec3& offset) {
    points.push_back({
      .hitbox = hitbox,
      .bone = bone,
      .priority = priority,
      .offset = offset
    });
  };

  points.reserve(12);
  for (const proj_aim_hitbox_sample& sample : proj_aim_hitbox_samples(target, hitbox_mask)) {
    add_point(
      sample.hitbox,
      sample.bone,
      aimbot_hitbox_priority(localplayer, target, weapon, sample.hitbox),
      sample.offset);
  }

  const int body_priority = aimbot_hitbox_priority(localplayer, target, weapon, aim_hitbox_spine_1);
  const int feet_priority = aimbot_hitbox_priority(localplayer, target, weapon, aim_hitbox_left_foot);
  const bool allow_body = aimbot_hitbox_matches_mask(aim_hitbox_spine_1, hitbox_mask);
  const bool allow_pelvis = aimbot_hitbox_matches_mask(aim_hitbox_pelvis, hitbox_mask);
  const bool allow_legs = aimbot_hitbox_matches_mask(aim_hitbox_left_foot, hitbox_mask);
  const bool prefer_low_direct =
    proj_aim_supports_splash(weapon) && target->is_on_ground() && allow_legs && !proj_aim_budget().active;
  if (allow_body) {
    add_point(aim_hitbox_spine_1, 0, body_priority, Vec3{0.0f, 0.0f, (maxs.z - mins.z) * 0.52f});
  }
  if (allow_pelvis) {
    add_point(aim_hitbox_pelvis, 0, body_priority + 1, Vec3{0.0f, 0.0f, (maxs.z - mins.z) * 0.34f});
  }
  if (target->is_on_ground() && allow_legs) {
    add_point(
      aim_hitbox_left_foot,
      0,
      prefer_low_direct ? body_priority - 1 : std::max(0, feet_priority - 1),
      Vec3{0.0f, 0.0f, mins.z + 6.0f});
  }

  // i have issues with huntsman
  if (points.empty()) {
    const bool head_in_mask = aimbot_hitbox_matches_mask(aim_hitbox_head, hitbox_mask);
    const int fallback_hitbox = head_in_mask ? aim_hitbox_head : aim_hitbox_spine_1;
    const float z_frac = head_in_mask ? 0.88f : 0.52f;
    add_point(fallback_hitbox, 0, 9999, Vec3{0.0f, 0.0f, (maxs.z - mins.z) * z_frac});
  }

  std::ranges::sort(points, [](const auto& left, const auto& right) {
    return left.priority < right.priority;
  });
  constexpr size_t max_direct_points = 4;
  if (points.size() > max_direct_points) {
    points.resize(max_direct_points);
  }
  return points;
}

inline bool proj_aim_explosion_can_damage(Player* localplayer,
  Player* target,
  const Vec3& explosion_origin,
  float splash_radius,
  uint32_t hitbox_mask) {
  if (localplayer == nullptr || target == nullptr || splash_radius <= 0.0f) {
    return false;
  }

  std::vector<Vec3> target_points = proj_aim_target_points(target, hitbox_mask);
  for (const Vec3& target_point : target_points) {
    if (distance_3d(explosion_origin, target_point) > splash_radius) {
      continue;
    }

    if (proj_aim_trace_between(explosion_origin, target_point, localplayer, target)) {
      return true;
    }
  }

  return false;
}

inline bool proj_aim_predicted_explosion_can_damage(Player* localplayer,
  Player* target,
  const Vec3& explosion_origin,
  const Vec3& predicted_origin,
  const std::vector<proj_aim_hitbox_sample>& hitbox_samples,
  float splash_radius,
  int* hitbox_out = nullptr,
  int* bone_out = nullptr,
  float* distance_out = nullptr) {
  if (localplayer == nullptr || target == nullptr || splash_radius <= 0.0f) {
    return false;
  }

  int best_hitbox = -1;
  int best_bone = 0;
  float best_distance = FLT_MAX;
  for (const proj_aim_hitbox_sample& sample : hitbox_samples) {
    const Vec3 predicted_point = predicted_origin + sample.offset;
    const float point_distance = distance_3d(explosion_origin, predicted_point);
    if (point_distance > splash_radius) {
      continue;
    }

    if (!proj_aim_trace_between(explosion_origin, predicted_point, localplayer, target)) {
      continue;
    }

    if (best_hitbox == -1 || point_distance < best_distance) {
      best_hitbox = sample.hitbox;
      best_bone = sample.bone;
      best_distance = point_distance;
    }
  }

  if (best_hitbox == -1 && hitbox_samples.empty()) {
    const float point_distance = distance_3d(explosion_origin, predicted_origin);
    if (point_distance <= splash_radius && proj_aim_trace_between(explosion_origin, predicted_origin, localplayer, target)) {
      best_distance = point_distance;
    }
  }

  if (best_hitbox == -1 && best_distance == FLT_MAX) {
    return false;
  }

  if (hitbox_out != nullptr) {
    *hitbox_out = best_hitbox;
  }
  if (bone_out != nullptr) {
    *bone_out = best_bone;
  }
  if (distance_out != nullptr) {
    *distance_out = best_distance;
  }
  return true;
}

inline projectile_sim_launch proj_aim_launch_from_intercept(Player* localplayer,
  Weapon* weapon,
  const LocalPredictionInterceptResult& intercept,
  const projectile_sim_profile& profile) {
  projectile_sim_launch launch{};
  if (localplayer == nullptr || weapon == nullptr || !intercept.valid || !profile.valid) {
    return launch;
  }

  return projectile_sim_build_launch_from_angles(localplayer, weapon, intercept.aim_angles, profile);
}

inline float proj_aim_segment_point_distance(const Vec3& start,
  const Vec3& end,
  const Vec3& point,
  float* fraction_out = nullptr) {
  const Vec3 segment = end - start;
  const float segment_length_sq = local_prediction_dot_3d(segment, segment);
  float fraction = 0.0f;
  if (segment_length_sq > 0.0001f) {
    fraction = std::clamp(local_prediction_dot_3d(point - start, segment) / segment_length_sq, 0.0f, 1.0f);
  }

  if (fraction_out != nullptr) {
    *fraction_out = fraction;
  }

  return distance_3d(start + (segment * fraction), point);
}

inline float proj_aim_direct_trace_point_tolerance(Weapon* weapon, const projectile_sim_profile& sim_profile) {
  const float hull_radius = std::max(
    proj_aim_hull_radius_for_weapon(weapon),
    std::max(sim_profile.hull.x, std::max(sim_profile.hull.y, sim_profile.hull.z)));
  if (proj_aim_is_huntsman_weapon(weapon)) {
    return std::max(5.0f, hull_radius + 3.0f);
  }

  if (!proj_aim_supports_splash(weapon)) {
    return std::max(8.0f, hull_radius + 6.0f);
  }

  return std::max(10.0f, hull_radius + 8.0f);
}

inline bool proj_aim_trace_path_segment_loop(Player* target,
  Weapon* weapon,
  const projectile_sim_profile& sim_profile,
  const LocalPredictionInterceptResult& intercept,
  const std::vector<LocalPredictionProjectileStep>& steps,
  size_t step_count) {
  if (target == nullptr || weapon == nullptr || engine_trace == nullptr || steps.size() < 2 || step_count < 2 ||
      step_count > steps.size()) {
    return false;
  }

  const Vec3 hull_mins = sim_profile.hull * -1.0f;
  const Vec3 hull_maxs = sim_profile.hull;

  // accept "reaches target" via the predicted player AABB (inflated by hull) OR a generous miss-distance
  // tolerance to the predicted intercept point. the previous point-only tolerance was tighter than
  // projectile_sim_direct_tolerance, so valid intercepts whose closest approach was 12-16 units off the
  // hitbox center always failed here, causing the projectile to "shoot around" the player.
  const float hull_radius = std::max(
    proj_aim_hull_radius_for_weapon(weapon),
    std::max(sim_profile.hull.x, std::max(sim_profile.hull.y, sim_profile.hull.z)));
  const Vec3 inflate{hull_radius, hull_radius, hull_radius};
  const Vec3 predicted_origin = intercept.has_target_base_origin
    ? intercept.target_base_origin
    : intercept.target_origin;
  const Vec3 target_mins = target->get_player_mins(target->is_ducking()) + predicted_origin - inflate;
  const Vec3 target_maxs = target->get_player_maxs(target->is_ducking()) + predicted_origin + inflate;
  const float point_tolerance = std::max(
    proj_aim_direct_trace_point_tolerance(weapon, sim_profile),
    projectile_sim_direct_tolerance(sim_profile) + hull_radius);

  for (size_t index = 1; index < step_count; ++index) {
    const Vec3 start = steps[index - 1].position;
    const Vec3 end = steps[index].position;

    float target_point_fraction = 1.0f;
    bool reaches_target_point =
      proj_aim_segment_point_distance(start, end, intercept.target_origin, &target_point_fraction) <= point_tolerance;
    float aabb_enter_fraction = 1.0f;
    if (aimbot_segment_aabb_enter_fraction(start, end, target_mins, target_maxs, &aabb_enter_fraction)) {
      // entering the predicted player box is the strongest signal we'd hit; prefer it for fraction comparison
      target_point_fraction = std::min(target_point_fraction, aabb_enter_fraction);
      reaches_target_point = true;
    }

    trace_t trace{};
    if (!projectile_trace_ray(
        start,
        end,
        sim_profile.hull_trace ? &hull_mins : nullptr,
        sim_profile.hull_trace ? &hull_maxs : nullptr,
        projectile_trace_contract::world_block,
        nullptr,
        -1,
        &trace)) {
      return false;
    }
    if (!projectile_trace_clear(trace, 0.97f) &&
        (!reaches_target_point || trace.fraction + 0.001f < target_point_fraction)) {
      return false;
    }

    if (reaches_target_point) {
      return true;
    }
  }

  return false;
}

inline bool proj_aim_intercept_trace_matches_launch(const projectile_sim_launch& launch,
  const LocalPredictionInterceptResult& intercept,
  const projectile_sim_profile& sim_profile,
  size_t* step_count_out) {
  if (step_count_out == nullptr || !launch.valid || !intercept.trace.valid || intercept.trace.steps.size() < 2) {
    return false;
  }

  const float cap_time = std::max(
    intercept.intercept_time + sim_profile.params.time_step,
    sim_profile.params.time_step);
  size_t use_count = intercept.trace.steps.size();
  while (use_count > 1 && intercept.trace.steps[use_count - 1].time > cap_time + 0.004f) {
    --use_count;
  }
  if (use_count < 2) {
    return false;
  }

  constexpr float k_origin_tol = 14.0f;
  constexpr float k_dir_dot_min = 0.985f;
  if (distance_3d(launch.origin, intercept.trace.steps[0].position) > k_origin_tol) {
    return false;
  }

  const Vec3 seg = intercept.trace.steps[1].position - intercept.trace.steps[0].position;
  const float seg_len = std::sqrt((seg.x * seg.x) + (seg.y * seg.y) + (seg.z * seg.z));
  if (seg_len <= 0.0001f) {
    return false;
  }
  const Vec3 trace_dir{seg.x / seg_len, seg.y / seg_len, seg.z / seg_len};
  const float dir_len = std::sqrt(
    (launch.direction.x * launch.direction.x) +
    (launch.direction.y * launch.direction.y) +
    (launch.direction.z * launch.direction.z));
  if (dir_len <= 0.0001f) {
    return false;
  }
  const Vec3 launch_dir{
    launch.direction.x / dir_len,
    launch.direction.y / dir_len,
    launch.direction.z / dir_len
  };
  const float align =
    (trace_dir.x * launch_dir.x) +
    (trace_dir.y * launch_dir.y) +
    (trace_dir.z * launch_dir.z);
  if (align < k_dir_dot_min) {
    return false;
  }

  *step_count_out = use_count;
  return true;
}

inline bool proj_aim_trace_path(Player* localplayer,
  Player* target,
  Weapon* weapon,
  const LocalPredictionInterceptResult& intercept) {
  if (localplayer == nullptr || target == nullptr || weapon == nullptr || engine_trace == nullptr || !intercept.valid || !intercept.trace.valid) {
    return false;
  }

  projectile_sim_profile sim_profile = projectile_sim_profile_for_weapon(localplayer, weapon);
  if (!sim_profile.valid) {
    return false;
  }

  sim_profile.params.max_time = std::min(
    sim_profile.params.max_time,
    std::max(intercept.intercept_time + sim_profile.params.time_step, sim_profile.params.time_step));
  sim_profile.lifetime = sim_profile.params.max_time;

  const projectile_sim_launch launch = proj_aim_launch_from_intercept(localplayer, weapon, intercept, sim_profile);
  if (!launch.valid) {
    return false;
  }

  size_t reuse_step_count = 0;
  if (proj_aim_intercept_trace_matches_launch(launch, intercept, sim_profile, &reuse_step_count)) {
    ++proj_aim_budget().reuse_trace_hits;
    return proj_aim_trace_path_segment_loop(target, weapon, sim_profile, intercept, intercept.trace.steps, reuse_step_count);
  }

  ++proj_aim_budget().fallback_sim_count;
  const projectile_sim_result sim_result = projectile_sim_run(
    launch,
    sim_profile,
    localplayer,
    target,
    projectile_sim_trace_mode::blocking_non_player);
  if (!sim_result.valid || sim_result.steps.size() < 2) {
    return false;
  }

  std::vector<LocalPredictionProjectileStep> fallback_steps{};
  fallback_steps.reserve(sim_result.steps.size());
  for (const projectile_sim_step& step : sim_result.steps) {
    fallback_steps.push_back({
      .time = step.time,
      .position = step.position,
      .velocity = step.velocity
    });
  }

  return proj_aim_trace_path_segment_loop(target, weapon, sim_profile, intercept, fallback_steps, fallback_steps.size());
}

inline bool proj_aim_trace_splash_path(Player* localplayer,
  Player* target,
  Weapon* weapon,
  const LocalPredictionInterceptResult& intercept,
  float splash_radius,
  uint32_t hitbox_mask,
  Vec3* explosion_origin_out = nullptr,
  const Vec3* predicted_target_origin = nullptr,
  bool validate_damage = true) {
  if (localplayer == nullptr || target == nullptr || weapon == nullptr || engine_trace == nullptr || !intercept.valid || !intercept.trace.valid || splash_radius <= 0.0f) {
    return false;
  }

  projectile_sim_profile sim_profile = projectile_sim_profile_for_weapon(localplayer, weapon);
  if (!sim_profile.valid) {
    return false;
  }

  sim_profile.params.max_time = std::min(
    sim_profile.params.max_time,
    std::max(intercept.intercept_time + sim_profile.params.time_step, sim_profile.params.time_step));
  sim_profile.lifetime = sim_profile.params.max_time;

  const projectile_sim_launch launch = proj_aim_launch_from_intercept(localplayer, weapon, intercept, sim_profile);
  const projectile_sim_result sim_result = projectile_sim_run(
    launch,
    sim_profile,
    localplayer,
    target,
    projectile_sim_trace_mode::blocking_non_player);
  if (!sim_result.hit || sim_result.hit_target) {
    return false;
  }

  Vec3 explosion_origin = sim_result.hit_position;
  if (distance_3d(explosion_origin, intercept.target_origin) > splash_radius * 1.35f) {
    return false;
  }

  if (!validate_damage) {
    if (explosion_origin_out != nullptr) {
      *explosion_origin_out = explosion_origin;
    }
    return true;
  }

  bool can_damage = false;
  if (predicted_target_origin != nullptr) {
    const std::vector<proj_aim_hitbox_sample> hitbox_samples = proj_aim_hitbox_samples(target, hitbox_mask);
    can_damage = proj_aim_predicted_explosion_can_damage(
      localplayer,
      target,
      explosion_origin,
      *predicted_target_origin,
      hitbox_samples,
      splash_radius);
  } else {
    can_damage = proj_aim_explosion_can_damage(localplayer, target, explosion_origin, splash_radius, hitbox_mask);
  }

  if (can_damage) {
    if (explosion_origin_out != nullptr) {
      *explosion_origin_out = explosion_origin;
    }
    return true;
  }

  return false;
}

#endif
