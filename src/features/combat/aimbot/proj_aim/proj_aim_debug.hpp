/*
/^-----^\   data: 2026-05-15
V  o o  V  file: src/features/combat/aimbot/proj_aim/proj_aim_debug.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef PROJ_AIM_DEBUG_HPP
#define PROJ_AIM_DEBUG_HPP

#include "proj_aim_types.hpp"

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

inline bool proj_aim_is_direct_hit_weapon(Weapon* weapon) {
  return weapon != nullptr && weapon->get_def_id() == Soldier_m_TheDirectHit;
}

inline uint32_t proj_aim_auto_hitbox_mask(Player* localplayer, Weapon* weapon, Player* target) {
  (void)localplayer;
  if (weapon == nullptr) {
    return aim_hitbox_mask_default_hitscan;
  }

  if (proj_aim_is_huntsman_weapon(weapon)) {
    return aim_hitbox_mask_head;
  }

  if (proj_aim_is_direct_hit_weapon(weapon)) {
    return target != nullptr && target->is_on_ground()
      ? aim_hitbox_mask_legs
      : aim_hitbox_mask_body;
  }

  if (proj_aim_supports_splash(weapon)) {
    if (target != nullptr && target->is_on_ground()) {
      return aim_hitbox_mask_body | aim_hitbox_mask_pelvis | aim_hitbox_mask_legs;
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
  bool used_game_engine_movement = false;
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
  int trace_calls_per_frame = 0;
  int sim_calls_per_frame = 0;
  int splash_candidates_per_frame = 0;
  int trace_budget_rejects = 0;
  int sim_budget_rejects = 0;
  int splash_budget_rejects = 0;
  int reuse_trace_hits = 0;
  int fallback_sim_count = 0;
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
  const int max_lead_ticks = std::clamp(config.aimbot.projectile_prediction_ticks, 8, 420) / 2;
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
  proj_aim_current_debug_stats.used_game_engine_movement = target_path.used_game_engine_movement;
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

  const proj_aim_budget_state& budget = proj_aim_budget();
  proj_aim_current_debug_stats.trace_calls_per_frame = budget.trace_calls;
  proj_aim_current_debug_stats.sim_calls_per_frame = budget.sim_calls;
  proj_aim_current_debug_stats.splash_candidates_per_frame = budget.splash_candidates;
  proj_aim_current_debug_stats.trace_budget_rejects = budget.trace_budget_rejects;
  proj_aim_current_debug_stats.sim_budget_rejects = budget.sim_budget_rejects;
  proj_aim_current_debug_stats.splash_budget_rejects = budget.splash_budget_rejects;
  proj_aim_current_debug_stats.reuse_trace_hits = budget.reuse_trace_hits;
  proj_aim_current_debug_stats.fallback_sim_count = budget.fallback_sim_count;
  proj_aim_last_debug_stats = proj_aim_current_debug_stats;
  if (proj_aim_current_debug_path.valid) {
    proj_aim_last_debug_path = proj_aim_current_debug_path;
  }
}

inline void proj_aim_set_scan_debug_stats(int scan_targets, int scan_attempts, int scan_cap) {
  if (!config.aimbot.projectile_splash_debug) {
    return;
  }

  if (!proj_aim_current_debug_stats.valid) {
    proj_aim_current_debug_stats = {};
    proj_aim_current_debug_stats.valid = true;
    proj_aim_current_debug_stats.expire_time = global_vars != nullptr ? global_vars->curtime + 0.35f : 0.35f;
    proj_aim_current_debug_stats.frame_time = global_vars != nullptr ? global_vars->frametime : 0.0f;
  }
  proj_aim_current_debug_stats.scan_targets = scan_targets;
  proj_aim_current_debug_stats.scan_attempts = scan_attempts;
  proj_aim_current_debug_stats.scan_cap = scan_cap;
  proj_aim_commit_debug_stats();
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

#endif
