/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/combat/aimbot/proj_aim.hpp
 |  Y  |   autor: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef PROJ_AIM_HPP
#define PROJ_AIM_HPP

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <climits>
#include <utility>
#include <vector>

#include "aim_utils.hpp"
#include "core/shared/sigs.hpp"
#include "libsigscan/libsigscan.h"

inline bool proj_aim_supports_splash(Weapon* weapon) {
  if (weapon == nullptr) {
    return false;
  }

  switch (weapon->get_def_id()) {
  case Soldier_m_RocketLauncher:
  case Soldier_m_RocketLauncherR:
  case Soldier_m_TheDirectHit:
  case Soldier_m_TheBlackBox:
  case Soldier_m_RocketJumper:
  case Soldier_m_TheLibertyLauncher:
  case Soldier_m_TheCowMangler5000:
  case Soldier_m_TheOriginal:
  case Soldier_m_FestiveRocketLauncher:
  case Soldier_m_TheBeggarsBazooka:
  case Soldier_m_FestiveBlackBox:
  case Soldier_m_TheAirStrike:
  case Demoman_m_GrenadeLauncher:
  case Demoman_m_GrenadeLauncherR:
  case Demoman_m_TheLochnLoad:
  case Demoman_m_TheLooseCannon:
  case Demoman_m_FestiveGrenadeLauncher:
  case Demoman_m_TheIronBomber:
  case Demoman_s_StickybombLauncher:
  case Demoman_s_StickybombLauncherR:
  case Demoman_s_FestiveStickybombLauncher:
  case Demoman_s_TheScottishResistance:
  case Demoman_s_TheQuickiebombLauncher:
  case Pyro_s_TheDetonator:
  case Pyro_s_TheScorchShot:
    return true;
  default:
    return false;
  }
}

struct proj_aim_hitbox_sample {
  int hitbox = -1;
  int bone = 0;
  int priority = INT_MAX;
  Vec3 offset{};
};

enum class proj_aim_point_kind {
  direct,
  splash
};

struct proj_aim_weapon_profile {
  LocalPredictionProjectileParameters params{};
  float splash_radius = 0.0f;
  float hull_radius = 2.0f;
  float arm_time = 0.0f;
  bool supports_direct = false;
  bool supports_splash = false;
  bool arcing = false;
};

struct proj_aim_direct_point {
  int hitbox = -1;
  int bone = 0;
  int priority = INT_MAX;
  Vec3 offset{};
};

struct proj_aim_solution {
  bool valid = false;
  proj_aim_point_kind kind = proj_aim_point_kind::direct;
  int hitbox = -1;
  int bone = 0;
  int priority = INT_MAX;
  Vec3 aim_position{};
  Vec3 aim_angles{};
  Vec3 explosion_origin{};
  float fov = FLT_MAX;
  float distance = FLT_MAX;
  float intercept_time = 0.0f;
  float splash_radius = 0.0f;
};

struct proj_aim_direct_history {
  Vec3 predicted_origin{};
  proj_aim_direct_point point{};
  LocalPredictionInterceptResult intercept{};
  float fov = FLT_MAX;
};

struct proj_aim_splash_history {
  Vec3 predicted_origin{};
  Vec3 splash_point{};
  Vec3 explosion_origin{};
  LocalPredictionInterceptResult intercept{};
  float time_error = FLT_MAX;
  float fov = FLT_MAX;
};

struct proj_aim_path_sample {
  float time = 0.0f;
  Vec3 position{};
};

struct projectile_timing_context {
  float interp_time = 0.0f;
  float outgoing_latency = 0.0f;
  float entity_staleness = 0.0f;
  float unclamped_lead_time = 0.0f;
  float clamped_lead_time = 0.0f;
  int lead_ticks = 0;
};

inline static std::vector<proj_aim_direct_history> proj_aim_last_direct_history{};
inline static std::vector<proj_aim_splash_history> proj_aim_last_splash_history{};

struct proj_aim_debug_path {
  bool valid = false;
  bool splash = false;
  float expire_time = 0.0f;
  Vec3 aim_position{};
  Vec3 explosion_origin{};
  std::vector<Vec3> target_path{};
  LocalPredictionProjectileTrace projectile_trace{};
};

inline static proj_aim_debug_path proj_aim_current_debug_path{};
inline static proj_aim_debug_path proj_aim_last_debug_path{};

inline bool proj_aim_is_huntsman_weapon(Weapon* weapon) {
  if (weapon == nullptr) {
    return false;
  }

  switch (weapon->get_def_id()) {
  case Sniper_m_TheHuntsman:
  case Sniper_m_FestiveHuntsman:
  case Sniper_m_TheFortifiedCompound:
    return true;
  default:
    return false;
  }
}

inline uint32_t proj_aim_auto_hitbox_mask(Player* localplayer, Weapon* weapon, Player* target) {
  (void)localplayer;
  if (weapon == nullptr) {
    return aim_hitbox_mask_default_hitscan;
  }

  if (proj_aim_is_huntsman_weapon(weapon)) {
    return aim_hitbox_mask_head;
  }

  if (proj_aim_supports_splash(weapon)) {
    if (target != nullptr && target->is_on_ground()) {
      return aim_hitbox_mask_legs | aim_hitbox_mask_pelvis;
    }

    return aim_hitbox_mask_body | aim_hitbox_mask_pelvis;
  }

  switch (weapon->get_def_id()) {
  case Medic_m_SyringeGun:
  case Medic_m_SyringeGunR:
  case Medic_m_TheBlutsauger:
  case Medic_m_TheOverdose:
  case Medic_m_CrusadersCrossbow:
  case Medic_m_FestiveCrusadersCrossbow:
  case Engi_m_TheRescueRanger:
    return aim_hitbox_mask_body | aim_hitbox_mask_pelvis;
  default:
    return aim_hitbox_mask_default_hitscan;
  }
}

inline uint32_t proj_aim_effective_hitbox_mask(Player* localplayer, Weapon* weapon, Player* target) {
  const uint32_t configured_mask = config.aimbot.projectile_hitboxes;
  if ((configured_mask & aim_hitbox_mask_auto) != 0) {
    return proj_aim_auto_hitbox_mask(localplayer, weapon, target);
  }

  const uint32_t manual_mask = configured_mask & aim_hitbox_mask_all;
  return manual_mask != 0 ? manual_mask : aim_hitbox_mask_default_hitscan;
}

struct proj_aim_debug_stats {
  bool valid = false;
  bool best_direct = false;
  bool best_splash = false;
  bool used_movement_sim = false;
  bool used_strafe_prediction = false;
  float expire_time = 0.0f;
  float frame_time = 0.0f;
  float path_start_time = 0.0f;
  float path_time_step = 0.0f;
  float average_yaw = 0.0f;
  float strafe_confidence = 0.0f;
  float best_time = 0.0f;
  float best_fov = FLT_MAX;
  float best_direct_miss = FLT_MAX;
  float best_splash_miss = FLT_MAX;
  float projectile_speed = 0.0f;
  float projectile_gravity = 0.0f;
  float projectile_max_time = 0.0f;
  float interp_time = 0.0f;
  float outgoing_latency = 0.0f;
  float entity_staleness = 0.0f;
  float clamped_lead_time = 0.0f;
  float target_speed = 0.0f;
  float target_vertical_speed = 0.0f;
  uint32_t configured_hitbox_mask = 0;
  uint32_t effective_hitbox_mask = 0;
  int weapon_def_id = 0;
  int target_index = 0;
  int path_positions = 0;
  int scan_targets = 0;
  int scan_attempts = 0;
  int scan_cap = 0;
  int direct_points = 0;
  int direct_solves = 0;
  int direct_intercepts = 0;
  int direct_trace_rejects = 0;
  int direct_candidates = 0;
  int splash_path_samples = 0;
  int splash_offsets = 0;
  int splash_solves = 0;
  int splash_intercepts = 0;
  int splash_trace_rejects = 0;
  int splash_damage_rejects = 0;
  int splash_candidates = 0;
  bool auto_hitbox = false;
};

inline static proj_aim_debug_stats proj_aim_current_debug_stats{};
inline static proj_aim_debug_stats proj_aim_last_debug_stats{};

inline projectile_timing_context proj_aim_build_timing_context(Entity* target) {
  projectile_timing_context context{};
  context.interp_time = local_prediction_interp_time();
  context.outgoing_latency = local_prediction_outgoing_latency();
  context.entity_staleness = local_prediction_estimate_entity_lag(target);
  context.unclamped_lead_time = context.outgoing_latency + context.interp_time + (context.entity_staleness * 0.5f);
  const int max_lead_ticks = std::clamp(config.aimbot.projectile_prediction_ticks, 8, 180) / 2;
  context.lead_ticks = std::clamp(local_prediction_time_to_ticks(context.unclamped_lead_time), 0, max_lead_ticks);
  context.clamped_lead_time = local_prediction_ticks_to_time(context.lead_ticks);
  return context;
}

inline void proj_aim_reset_debug_stats(Weapon* weapon,
  Player* target,
  const LocalPredictionEntityPath& target_path,
  uint32_t configured_hitbox_mask,
  uint32_t effective_hitbox_mask) {
  if (!config.aimbot.projectile_splash_debug) {
    return;
  }

  proj_aim_current_debug_stats = {};
  proj_aim_current_debug_stats.valid = true;
  proj_aim_current_debug_stats.expire_time = global_vars != nullptr ? global_vars->curtime + 0.35f : 0.35f;
  proj_aim_current_debug_stats.frame_time = global_vars != nullptr ? global_vars->frametime : 0.0f;
  proj_aim_current_debug_stats.weapon_def_id = weapon != nullptr ? weapon->get_def_id() : 0;
  proj_aim_current_debug_stats.target_index = target != nullptr ? target->get_index() : 0;
  proj_aim_current_debug_stats.path_positions = static_cast<int>(target_path.positions.size());
  proj_aim_current_debug_stats.path_start_time = target_path.start_time;
  proj_aim_current_debug_stats.path_time_step = target_path.time_step;
  proj_aim_current_debug_stats.average_yaw = target_path.average_yaw;
  proj_aim_current_debug_stats.strafe_confidence = target_path.strafe_confidence;
  proj_aim_current_debug_stats.used_movement_sim = target_path.used_movement_sim;
  proj_aim_current_debug_stats.used_strafe_prediction = target_path.used_strafe_prediction;
  const LocalPredictionProjectileParameters projectile_params = local_prediction_projectile_parameters_for_weapon(weapon);
  proj_aim_current_debug_stats.projectile_speed = projectile_params.speed;
  proj_aim_current_debug_stats.projectile_gravity = projectile_params.gravity;
  proj_aim_current_debug_stats.projectile_max_time = projectile_params.max_time;
  const projectile_timing_context timing_context = proj_aim_build_timing_context(target != nullptr ? target->to_entity() : nullptr);
  proj_aim_current_debug_stats.interp_time = timing_context.interp_time;
  proj_aim_current_debug_stats.outgoing_latency = timing_context.outgoing_latency;
  proj_aim_current_debug_stats.entity_staleness = timing_context.entity_staleness;
  proj_aim_current_debug_stats.clamped_lead_time = timing_context.clamped_lead_time;
  proj_aim_current_debug_stats.target_speed = local_prediction_velocity_2d_length(target_path.final_velocity);
  proj_aim_current_debug_stats.target_vertical_speed = target_path.final_velocity.z;
  proj_aim_current_debug_stats.configured_hitbox_mask = configured_hitbox_mask;
  proj_aim_current_debug_stats.effective_hitbox_mask = effective_hitbox_mask;
  proj_aim_current_debug_stats.auto_hitbox = (configured_hitbox_mask & aim_hitbox_mask_auto) != 0;
  proj_aim_current_debug_path = {};
}

inline void proj_aim_commit_debug_stats() {
  if (!config.aimbot.projectile_splash_debug || !proj_aim_current_debug_stats.valid) {
    return;
  }

  proj_aim_last_debug_stats = proj_aim_current_debug_stats;
  if (proj_aim_current_debug_path.valid) {
    proj_aim_last_debug_path = proj_aim_current_debug_path;
  }
}

inline void proj_aim_set_scan_debug_stats(int scan_targets, int scan_attempts, int scan_cap) {
  if (!config.aimbot.projectile_splash_debug) {
    return;
  }

  proj_aim_current_debug_stats.scan_targets = scan_targets;
  proj_aim_current_debug_stats.scan_attempts = scan_attempts;
  proj_aim_current_debug_stats.scan_cap = scan_cap;
  proj_aim_last_debug_stats.scan_targets = scan_targets;
  proj_aim_last_debug_stats.scan_attempts = scan_attempts;
  proj_aim_last_debug_stats.scan_cap = scan_cap;
}

inline void proj_aim_store_debug_path(const LocalPredictionEntityPath& target_path,
  const LocalPredictionInterceptResult& intercept,
  const aimbot_candidate& candidate) {
  if (!config.aimbot.projectile_splash_debug || global_vars == nullptr || !intercept.valid) {
    return;
  }

  proj_aim_current_debug_path = {};
  proj_aim_current_debug_path.valid = true;
  proj_aim_current_debug_path.splash = candidate.projectile_splash;
  proj_aim_current_debug_path.expire_time = global_vars->curtime + 0.25f;
  proj_aim_current_debug_path.aim_position = candidate.aim_position;
  proj_aim_current_debug_path.explosion_origin = candidate.projectile_splash ? candidate.aim_position : Vec3{};
  proj_aim_current_debug_path.projectile_trace = intercept.trace;
  proj_aim_current_debug_path.target_path = target_path.positions;
}

inline bool proj_aim_mode_allows_direct() {
  return config.aimbot.projectile_mode == Aim::ProjectileMode::DIRECT_ONLY ||
    config.aimbot.projectile_mode == Aim::ProjectileMode::DIRECT_THEN_SPLASH ||
    config.aimbot.projectile_mode == Aim::ProjectileMode::PREFER_SPLASH;
}

inline bool proj_aim_mode_allows_splash(Weapon* weapon) {
  if (!proj_aim_supports_splash(weapon)) {
    return false;
  }

  return config.aimbot.projectile_mode == Aim::ProjectileMode::DIRECT_THEN_SPLASH ||
    config.aimbot.projectile_mode == Aim::ProjectileMode::PREFER_SPLASH ||
    config.aimbot.projectile_mode == Aim::ProjectileMode::SPLASH_ONLY;
}

inline float proj_aim_splash_radius_for_weapon(Weapon* weapon) {
  if (weapon == nullptr) {
    return 0.0f;
  }

  float radius = 0.0f;
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
    radius = 170.0f;
    break;
  case Soldier_m_TheAirStrike:
    radius = 130.0f;
    break;
  case Soldier_m_TheDirectHit:
    radius = 70.0f;
    break;
  case Demoman_m_GrenadeLauncher:
  case Demoman_m_GrenadeLauncherR:
  case Demoman_m_TheLochnLoad:
  case Demoman_m_TheLooseCannon:
  case Demoman_m_FestiveGrenadeLauncher:
  case Demoman_m_TheIronBomber:
  case Demoman_s_StickybombLauncher:
  case Demoman_s_StickybombLauncherR:
  case Demoman_s_FestiveStickybombLauncher:
  case Demoman_s_TheScottishResistance:
  case Demoman_s_TheQuickiebombLauncher:
    radius = 170.0f;
    break;
  case Pyro_s_TheDetonator:
  case Pyro_s_TheScorchShot:
    radius = weapon->get_def_id() == Pyro_s_TheScorchShot ? 150.0f : 110.0f;
    break;
  default:
    break;
  }

  if (radius > 0.0f && attribute_manager != nullptr) {
    radius = attribute_manager->attrib_hook_value(radius, "mult_explosion_radius", weapon->to_entity());
  }

  radius *= std::clamp(config.aimbot.projectile_splash_radius_scale, 0.25f, 2.0f);
  return radius;
}

inline float proj_aim_hull_radius_for_weapon(Weapon* weapon) {
  if (weapon == nullptr) {
    return 2.0f;
  }

  switch (weapon->get_def_id()) {
  case Soldier_m_RocketLauncher:
  case Soldier_m_RocketLauncherR:
  case Soldier_m_TheDirectHit:
  case Soldier_m_TheBlackBox:
  case Soldier_m_RocketJumper:
  case Soldier_m_TheLibertyLauncher:
  case Soldier_m_TheCowMangler5000:
  case Soldier_m_TheOriginal:
  case Soldier_m_FestiveRocketLauncher:
  case Soldier_m_TheBeggarsBazooka:
  case Soldier_m_FestiveBlackBox:
  case Soldier_m_TheAirStrike:
  case Soldier_s_TheRighteousBison:
    return 4.0f;
  case Engi_m_ThePomson6000:
  case Pyro_m_DragonsFury:
    return 3.0f;
  case Demoman_m_GrenadeLauncher:
  case Demoman_m_GrenadeLauncherR:
  case Demoman_m_TheLochnLoad:
  case Demoman_m_TheLooseCannon:
  case Demoman_m_FestiveGrenadeLauncher:
  case Demoman_m_TheIronBomber:
  case Demoman_s_StickybombLauncher:
  case Demoman_s_StickybombLauncherR:
  case Demoman_s_FestiveStickybombLauncher:
  case Demoman_s_TheScottishResistance:
  case Demoman_s_TheQuickiebombLauncher:
    return 6.0f;
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
  case Pyro_s_GasPasser:
    return 4.0f;
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
    return 5.0f;
  case Medic_m_CrusadersCrossbow:
  case Medic_m_FestiveCrusadersCrossbow:
  case Engi_m_TheRescueRanger:
  case Sniper_m_TheHuntsman:
  case Sniper_m_FestiveHuntsman:
  case Sniper_m_TheFortifiedCompound:
    return 2.0f;
  case Medic_m_SyringeGun:
  case Medic_m_SyringeGunR:
  case Medic_m_TheBlutsauger:
  case Medic_m_TheOverdose:
    return 1.0f;
  case Pyro_s_TheFlareGun:
  case Pyro_s_TheDetonator:
  case Pyro_s_TheManmelter:
  case Pyro_s_TheScorchShot:
  case Pyro_s_FestiveFlareGun:
    return 3.0f;
  default:
    return 2.0f;
  }
}

inline bool proj_aim_is_stickybomb_launcher(Weapon* weapon) {
  if (weapon == nullptr) {
    return false;
  }

  switch (weapon->get_def_id()) {
  case Demoman_s_StickybombLauncher:
  case Demoman_s_StickybombLauncherR:
  case Demoman_s_FestiveStickybombLauncher:
  case Demoman_s_TheScottishResistance:
  case Demoman_s_TheQuickiebombLauncher:
    return true;
  default:
    return false;
  }
}

inline float proj_aim_arm_time_for_weapon(Weapon* weapon) {
  if (!proj_aim_is_stickybomb_launcher(weapon)) {
    return 0.0f;
  }

  using sticky_arm_time_fn = float (*)(Weapon*);
  static sticky_arm_time_fn sticky_arm_time = nullptr;
  static bool sticky_arm_time_initialized = false;
  if (!sticky_arm_time_initialized) {
    sticky_arm_time_initialized = true;
    sticky_arm_time = reinterpret_cast<sticky_arm_time_fn>(sigscan_module("client.so", sigs::tf_projectile_sticky_arm_time));
  }

  if (sticky_arm_time != nullptr) {
    const float arm_time = sticky_arm_time(weapon);
    if (arm_time >= 0.0f && arm_time <= 5.0f) {
      return arm_time;
    }
  }

  if (attribute_manager == nullptr) {
    return 0.8f;
  }

  return std::max(0.0f, attribute_manager->attrib_hook_value(0.8f, "sticky_arm_time", weapon->to_entity()));
}

inline proj_aim_weapon_profile proj_aim_profile_for_weapon(Weapon* weapon) {
  proj_aim_weapon_profile profile{};
  profile.params = local_prediction_projectile_parameters_for_weapon(weapon);
  profile.splash_radius = proj_aim_splash_radius_for_weapon(weapon);
  profile.hull_radius = proj_aim_hull_radius_for_weapon(weapon);
  profile.arm_time = proj_aim_arm_time_for_weapon(weapon);
  profile.supports_direct = profile.params.speed > 0.0f && proj_aim_mode_allows_direct();
  profile.supports_splash = profile.params.speed > 0.0f && proj_aim_mode_allows_splash(weapon) && profile.splash_radius > 0.0f;
  profile.arcing = profile.params.gravity > 0.0f;
  profile.params.max_time = std::min(
    profile.params.max_time,
    static_cast<float>(std::clamp(config.aimbot.projectile_prediction_ticks, 8, 180)) * static_cast<float>(TICK_INTERVAL));
  profile.params.time_step = std::max(profile.params.time_step, static_cast<float>(TICK_INTERVAL));
  return profile;
}

inline bool proj_aim_trace_between(const Vec3& start, const Vec3& end, Entity* skip_entity, Entity* target_entity) {
  if (engine_trace == nullptr) {
    return false;
  }

  ray_t ray = engine_trace->init_ray(const_cast<Vec3*>(&start), const_cast<Vec3*>(&end));
  trace_filter filter{};
  engine_trace->init_trace_filter(&filter, skip_entity);

  trace_t trace{};
  engine_trace->trace_ray(&ray, MASK_SOLID | CONTENTS_GRATE, &filter, &trace);
  return trace.entity == target_entity || trace.fraction > 0.97f;
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
  const bool prefer_low_direct = proj_aim_supports_splash(weapon) && target->is_on_ground() && allow_legs;
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
  const projectile_sim_result sim_result = projectile_sim_run(
    launch,
    sim_profile,
    localplayer,
    target,
    projectile_sim_trace_mode::world);
  if (sim_result.hit) {
    return false;
  }
  if (!sim_result.valid || sim_result.steps.size() < 2) {
    return false;
  }

  const float hull_radius = proj_aim_hull_radius_for_weapon(weapon);
  const Vec3 inflate{hull_radius, hull_radius, hull_radius};
  const Vec3 predicted_origin = intercept.has_target_base_origin
    ? intercept.target_base_origin
    : intercept.target_origin;
  const Vec3 target_mins = target->get_player_mins(target->is_ducking()) + predicted_origin - inflate;
  const Vec3 target_maxs = target->get_player_maxs(target->is_ducking()) + predicted_origin + inflate;

  const Vec3 hull_mins = sim_profile.hull * -1.0f;
  const Vec3 hull_maxs = sim_profile.hull;
  for (size_t index = 1; index < sim_result.steps.size(); ++index) {
    const Vec3 start = sim_result.steps[index - 1].position;
    const Vec3 end = sim_result.steps[index].position;

    ray_t ray = sim_profile.hull_trace
      ? engine_trace->init_ray(const_cast<Vec3*>(&start), const_cast<Vec3*>(&end), const_cast<Vec3*>(&hull_mins), const_cast<Vec3*>(&hull_maxs))
      : engine_trace->init_ray(const_cast<Vec3*>(&start), const_cast<Vec3*>(&end));
    trace_filter filter{};
    engine_trace->init_world_trace_filter(&filter);

    trace_t trace{};
    engine_trace->trace_ray(&ray, MASK_SOLID, &filter, &trace);
    if (trace.fraction < 0.97f) {
      return false;
    }

    if (aimbot_segment_intersects_aabb(start, end, target_mins, target_maxs)) {
      return true;
    }
  }

  return false;
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
    projectile_sim_trace_mode::world);
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

inline std::vector<proj_aim_path_sample> proj_aim_limited_path_samples(const LocalPredictionEntityPath& target_path) {
  std::vector<proj_aim_path_sample> samples{};
  if (!target_path.valid || target_path.positions.empty()) {
    return samples;
  }

  const size_t desired_steps = static_cast<size_t>(std::clamp(config.aimbot.projectile_path_steps, 2, 64));
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
  offsets->emplace_back(Vec3{0.0f, 0.0f, -outer});
  offsets->emplace_back(Vec3{outer * 0.38f, 0.0f, -outer * 0.72f});
  offsets->emplace_back(Vec3{-outer * 0.38f, 0.0f, -outer * 0.72f});
  offsets->emplace_back(Vec3{0.0f, 0.0f, outer});
  offsets->emplace_back(Vec3{to_local.x * outer, to_local.y * outer, 0.0f});
  offsets->emplace_back(Vec3{to_local.x * outer, to_local.y * outer, -outer * 0.42f});

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
  std::vector<Vec3> sample_offsets{};
  sample_offsets.reserve(static_cast<size_t>(splash_samples) + 6);
  std::vector<proj_aim_splash_history> splash_history{};
  if (config.aimbot.projectile_splash_debug) {
    splash_history.reserve(predicted_samples.size() * static_cast<size_t>(std::min(splash_samples, 12)));
    proj_aim_current_debug_stats.splash_path_samples = static_cast<int>(predicted_samples.size());
  }

  for (const proj_aim_path_sample& predicted_sample : predicted_samples) {
    const Vec3& predicted_origin = predicted_sample.position;
    const Vec3 predicted_angles = aimbot_calculate_angles_to_position(local_origin, predicted_origin);
    const float predicted_fov = aimbot_calculate_fov(predicted_angles, original_view_angles);
    if (!aimbot_fov_within_limit(predicted_fov, 1.35f, 3.0f)) {
      continue;
    }

    proj_aim_splash_sample_offsets(
      local_origin,
      predicted_origin,
      splash_radius,
      splash_samples,
      config.aimbot.projectile_wall_splash,
      config.aimbot.projectile_seam_shot,
      &sample_offsets);
    if (config.aimbot.projectile_splash_debug) {
      proj_aim_current_debug_stats.splash_offsets += static_cast<int>(sample_offsets.size());
    }

    for (const Vec3& offset : sample_offsets) {
      const Vec3 splash_point = predicted_origin + offset;
      if (config.aimbot.projectile_splash_debug) {
        ++proj_aim_current_debug_stats.splash_solves;
      }
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
  }

  if (config.aimbot.projectile_splash_debug) {
    proj_aim_last_splash_history = std::move(splash_history);
  }
  return best_candidate;
}

inline aimbot_candidate proj_aim_find_candidate(Player* localplayer, Weapon* weapon, Player* player, user_cmd* user_cmd, const Vec3& original_view_angles) {
  aimbot_candidate candidate{};
  if (localplayer == nullptr || weapon == nullptr || player == nullptr || user_cmd == nullptr) return candidate;

  const proj_aim_weapon_profile profile = proj_aim_profile_for_weapon(weapon);
  if (profile.params.speed <= 0.0f) {
    return candidate;
  }

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

  switch (config.aimbot.projectile_mode) {
  case Aim::ProjectileMode::DIRECT_ONLY:
    return direct_candidate;
  case Aim::ProjectileMode::DIRECT_THEN_SPLASH:
    if (direct_candidate.player != nullptr && (splash_candidate.player == nullptr || direct_confident)) {
      return direct_candidate;
    }
    return splash_candidate.player != nullptr ? splash_candidate : direct_candidate;
  case Aim::ProjectileMode::PREFER_SPLASH:
    return splash_candidate.player != nullptr ? splash_candidate : direct_candidate;
  case Aim::ProjectileMode::SPLASH_ONLY:
    return splash_candidate;
  }

  return direct_candidate;
}

#endif
