/*
/^-----^\   data: 2026-05-16
V  o o  V  file: src/features/combat/aimbot/proj_aim/proj_aim_budget.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef PROJ_AIM_BUDGET_HPP
#define PROJ_AIM_BUDGET_HPP

#include <algorithm>
#include <cmath>

#include "features/menu/config.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"

struct proj_aim_budget_state {
  bool active = false;
  int intercept_path_stride_mul = 1;
  float intercept_error_cap_add = 0.0f;
  int splash_solve_budget_percent = 100;
  int path_steps_percent = 100;
  int frame_tick = -1;
  int trace_calls = 0;
  int sim_calls = 0;
  int splash_candidates = 0;
  int trace_call_cap = 360;
  int sim_call_cap = 72;
  int splash_candidate_cap = 96;
  int trace_budget_rejects = 0;
  int sim_budget_rejects = 0;
  int splash_budget_rejects = 0;
  int reuse_trace_hits = 0;
  int fallback_sim_count = 0;
};

inline proj_aim_budget_state& proj_aim_budget() {
  static thread_local proj_aim_budget_state state{};
  return state;
}

inline void proj_aim_budget_begin_frame();

inline void proj_aim_budget_reset() {
  proj_aim_budget_begin_frame();
  proj_aim_budget_state& b = proj_aim_budget();
  const int frame_tick = b.frame_tick;
  const int trace_calls = b.trace_calls;
  const int sim_calls = b.sim_calls;
  const int splash_candidates = b.splash_candidates;
  const int trace_budget_rejects = b.trace_budget_rejects;
  const int sim_budget_rejects = b.sim_budget_rejects;
  const int splash_budget_rejects = b.splash_budget_rejects;
  const int reuse_trace_hits = b.reuse_trace_hits;
  const int fallback_sim_count = b.fallback_sim_count;
  b = proj_aim_budget_state{};
  b.frame_tick = frame_tick;
  b.trace_calls = trace_calls;
  b.sim_calls = sim_calls;
  b.splash_candidates = splash_candidates;
  b.trace_budget_rejects = trace_budget_rejects;
  b.sim_budget_rejects = sim_budget_rejects;
  b.splash_budget_rejects = splash_budget_rejects;
  b.reuse_trace_hits = reuse_trace_hits;
  b.fallback_sim_count = fallback_sim_count;
}

inline void proj_aim_budget_begin_frame() {
  proj_aim_budget_state& b = proj_aim_budget();
  const int tick = global_vars != nullptr ? global_vars->tickcount : 0;
  if (b.frame_tick == tick) {
    return;
  }

  const bool was_active = b.active;
  const int stride_mul = b.intercept_path_stride_mul;
  const float error_cap_add = b.intercept_error_cap_add;
  const int splash_percent = b.splash_solve_budget_percent;
  const int path_percent = b.path_steps_percent;
  b = proj_aim_budget_state{};
  b.active = was_active;
  b.intercept_path_stride_mul = stride_mul;
  b.intercept_error_cap_add = error_cap_add;
  b.splash_solve_budget_percent = splash_percent;
  b.path_steps_percent = path_percent;
  b.frame_tick = tick;
}

inline bool proj_aim_budget_try_trace_call() {
  proj_aim_budget_begin_frame();
  proj_aim_budget_state& b = proj_aim_budget();
  if (b.trace_calls >= b.trace_call_cap) {
    ++b.trace_budget_rejects;
    return false;
  }
  ++b.trace_calls;
  return true;
}

inline bool proj_aim_budget_try_sim_call() {
  proj_aim_budget_begin_frame();
  proj_aim_budget_state& b = proj_aim_budget();
  if (b.sim_calls >= b.sim_call_cap) {
    ++b.sim_budget_rejects;
    return false;
  }
  ++b.sim_calls;
  return true;
}

inline bool proj_aim_budget_try_splash_candidate() {
  proj_aim_budget_begin_frame();
  proj_aim_budget_state& b = proj_aim_budget();
  if (b.splash_candidates >= b.splash_candidate_cap) {
    ++b.splash_budget_rejects;
    return false;
  }
  ++b.splash_candidates;
  return true;
}

inline int proj_aim_budget_remaining_splash_candidates() {
  proj_aim_budget_begin_frame();
  const proj_aim_budget_state& b = proj_aim_budget();
  return std::max(0, b.splash_candidate_cap - b.splash_candidates);
}

inline float proj_aim_budget_frame_time() {
  if (global_vars == nullptr) {
    return 0.0f;
  }

  float frame_time = 0.0f;
  if (std::isfinite(global_vars->frametime) && global_vars->frametime > 0.0f) {
    frame_time = std::max(frame_time, global_vars->frametime);
  }
  if (std::isfinite(global_vars->absolute_frametime) && global_vars->absolute_frametime > 0.0f) {
    frame_time = std::max(frame_time, global_vars->absolute_frametime);
  }
  return frame_time;
}

inline int proj_aim_budget_frame_percent() {
  const float frame_time = proj_aim_budget_frame_time();
  if (frame_time > (1.0f / 45.0f)) {
    return 55;
  }
  if (frame_time > (1.0f / 60.0f)) {
    return 70;
  }
  if (frame_time > (1.0f / 75.0f)) {
    return 85;
  }
  return 100;
}

struct proj_aim_budget_guard {
  proj_aim_budget_guard() = default;
  ~proj_aim_budget_guard() {
    proj_aim_budget_reset();
  }
  proj_aim_budget_guard(const proj_aim_budget_guard&) = delete;
  proj_aim_budget_guard& operator=(const proj_aim_budget_guard&) = delete;
};

inline void proj_aim_budget_begin_for_distance(float distance_units) {
  proj_aim_budget_begin_frame();
  proj_aim_budget_state& b = proj_aim_budget();
  const int frame_tick = b.frame_tick;
  const int trace_calls = b.trace_calls;
  const int sim_calls = b.sim_calls;
  const int splash_candidates = b.splash_candidates;
  const int trace_budget_rejects = b.trace_budget_rejects;
  const int sim_budget_rejects = b.sim_budget_rejects;
  const int splash_budget_rejects = b.splash_budget_rejects;
  const int reuse_trace_hits = b.reuse_trace_hits;
  const int fallback_sim_count = b.fallback_sim_count;
  b = proj_aim_budget_state{};
  b.active = true;
  b.frame_tick = frame_tick;
  b.trace_calls = trace_calls;
  b.sim_calls = sim_calls;
  b.splash_candidates = splash_candidates;
  b.trace_budget_rejects = trace_budget_rejects;
  b.sim_budget_rejects = sim_budget_rejects;
  b.splash_budget_rejects = splash_budget_rejects;
  b.reuse_trace_hits = reuse_trace_hits;
  b.fallback_sim_count = fallback_sim_count;

  const float d0 = std::max(1.0f, config.aimbot.projectile_far_distance_begin);
  const float d1 = std::max(d0 + 1.0f, config.aimbot.projectile_far_distance_full);
  float tier = 0.0f;
  if (distance_units >= d1) {
    tier = 1.0f;
  } else if (distance_units > d0) {
    tier = (distance_units - d0) / (d1 - d0);
  }

  b.intercept_path_stride_mul = tier >= 0.99f ? 3 : (tier > 0.35f ? 2 : 1);
  b.intercept_error_cap_add = tier * config.aimbot.projectile_far_error_cap_add;
  b.splash_solve_budget_percent = static_cast<int>(std::lerp(100.0f, static_cast<float>(config.aimbot.projectile_far_splash_budget_percent), tier));
  b.path_steps_percent = static_cast<int>(std::lerp(100.0f, static_cast<float>(config.aimbot.projectile_far_path_steps_percent), tier));
  b.trace_call_cap = tier >= 0.99f ? 240 : (tier > 0.35f ? 300 : 360);
  b.sim_call_cap = tier >= 0.99f ? 40 : (tier > 0.35f ? 56 : 72);
  b.splash_candidate_cap = tier >= 0.99f ? 48 : (tier > 0.35f ? 72 : 96);

  const int frame_percent = proj_aim_budget_frame_percent();
  b.splash_solve_budget_percent = std::max(20, (b.splash_solve_budget_percent * frame_percent) / 100);
  b.path_steps_percent = std::max(35, (b.path_steps_percent * frame_percent) / 100);
  b.trace_call_cap = std::max(96, (b.trace_call_cap * frame_percent) / 100);
  b.sim_call_cap = std::max(18, (b.sim_call_cap * frame_percent) / 100);
  b.splash_candidate_cap = std::max(24, (b.splash_candidate_cap * frame_percent) / 100);
  proj_aim_budget_begin_frame();
}

#endif
