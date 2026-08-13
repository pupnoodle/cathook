/*
/^-----^\   data: 2026-05-09
V  o o  V  file: src/features/combat/aimbot/aimbot_debug.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef AIMBOT_DEBUG_HPP
#define AIMBOT_DEBUG_HPP
#include <cfloat>
#include <cstdint>

enum class aimbot_debug_reason {
  none,
  disabled,
  no_localplayer,
  no_weapon,
  no_target,
  auto_scope,
  auto_unscope,
  auto_rev,
  attack_not_ready,
  charge_wait,
  scoped_only,
  headshot_wait,
  settle_wait,
  primary_wait,
  hitbox_miss,
  final_trace_miss,
  spread_seed_missing,
  attack_ready
};

enum class aimbot_reject_reason {
  none,
  invalid,
  local,
  dormant,
  dead,
  invulnerable,
  ignored,
  friend_state,
  ipc_bot,
  cloaked,
  team,
  type,
  no_candidate,
  not_visible,
  fov,
  no_model,
  no_studio_model,
  no_hitbox_set,
  no_hitbox,
  bone_cache,
  bone_invalidate,
  bone_access,
  bone_studio_header,
  bone_sequence,
  bone_animation,
  bone_reconstruction,
  bone_attachments,
  bone_setup,
  bone_matrices,
  pose_timing,
  trace_blocked,
  wrong_hitbox,
  no_point
};

struct aimbot_reject_debug {
  aimbot_reject_reason reason = aimbot_reject_reason::none;
  int entity_index = -1;
  int team = -1;
  int health = 0;
  int hitbox = -1;
  int trace_entity_index = -1;
  int trace_hitbox = -1;
  int trace_contents = 0;
  float fov = FLT_MAX;
  float fov_limit = FLT_MAX;
  float distance = FLT_MAX;
  float trace_fraction = 1.0f;
  Vec3 trace_start{};
  Vec3 trace_point{};
  Vec3 trace_end{};
  bool visible = false;
  bool preferred = false;
  bool current = false;
  bool backtrack = false;
};

struct aimbot_pose_debug_info {
  bool valid = false;
  bool getter_ready = false;
  bool updater_ready = false;
  bool cache_updated = false;
  bool signature_reused = false;
  int target_index = -1;
  int target_handle = 0;
  std::uintptr_t cache_handle = 0;
  int bone_count = 0;
  int generation = 0;
  int pose_frame = 0;
  int current_frame = 0;
  float simulation_time = NAN;
  float setup_time = NAN;
  float cache_time = NAN;
  float simulation_age = NAN;
  aimbot_reject_reason failure = aimbot_reject_reason::none;
};

struct aimbot_debug_state {
  bool active = false;
  bool requested_shot = false;
  bool attack_ready = false;
  bool attack_gate_ready = false;
  bool charge_ready = true;
  bool trace_ready = true;
  bool settled = true;
  bool primary_ready = true;
  bool scoped = false;
  bool scoped_ready = false;
  bool headshot_ready = false;
  bool final_trace_hit = false;
  bool spread_compensated = false;
  bool spread_signature = false;
  bool spread_fixed = false;
  int aim_mode = 0;
  int weapon_def_id = 0;
  int selected_entity_index = -1;
  int selected_hitbox = -1;
  int trace_entity_index = -1;
  int trace_hitbox = -1;
  int pellet_count = 0;
  int pellet_index = -1;
  int tick_count = 0;
  int candidates_total = 0;
  int candidates_visible = 0;
  int candidates_rejected = 0;
  int skipped_ignored = 0;
  int skipped_friends = 0;
  int skipped_ipc = 0;
  int skipped_cloaked = 0;
  int skipped_team = 0;
  int skipped_invulnerable = 0;
  int skipped_dead = 0;
  int skipped_type = 0;
  int resolver_mode = 0;
  int resolver_candidates = 0;
  int resolver_misses = 0;
  int resolver_hits = 0;
  float fov = FLT_MAX;
  float distance = FLT_MAX;
  float spread = 0.0f;
  float backtrack_timing_error = 0.0f;
  float backtrack_capture_gap = 0.0f;
  float resolver_yaw = 0.0f;
  Vec3 final_command_angles{};
  float resolver_pitch = 0.0f;
  bool resolver_active = false;
  int selected_team = -1;
  int selected_health = 0;
  int selected_handle = 0;
  bool selected_backtrack = false;
  Vec3 selected_aim_position{};
  float selected_simulation_time = NAN;
  bool pose_timing_valid = false;
  bool compensation_applied = false;
  int pose_target_tick = 0;
  int pose_command_tick = 0;
  float pose_lead_seconds = 0.0f;
  float pose_lead_distance = 0.0f;
  float target_speed = 0.0f;
  Vec3 target_velocity{};
  Vec3 pose_offset{};
  float final_trace_fraction = 1.0f;
  int final_trace_contents = 0;
  Vec3 final_trace_end{};
  float outgoing_latency = 0.0f;
  float incoming_latency = 0.0f;
  float interpolation = 0.0f;
  float fake_interpolation = 0.0f;
  float timing_correct = 0.0f;
  int lerp_ticks = 0;
  aimbot_pose_debug_info pose{};
  aimbot_reject_debug last_reject{};
  aimbot_reject_debug last_skip{};
  aimbot_reject_debug best_reject{};
  aimbot_debug_reason reason = aimbot_debug_reason::none;
};

inline static aimbot_debug_state aimbot_debug_current_state{};

inline void aimbot_debug_set_state(const aimbot_debug_state& state) {
  aimbot_debug_current_state = state;
}

inline const aimbot_debug_state& aimbot_debug_get_state() {
  return aimbot_debug_current_state;
}

inline const char* aimbot_debug_reason_name(aimbot_debug_reason reason) {
  switch (reason) {
  case aimbot_debug_reason::none:
    return "none";
  case aimbot_debug_reason::disabled:
    return "disabled";
  case aimbot_debug_reason::no_localplayer:
    return "no local";
  case aimbot_debug_reason::no_weapon:
    return "no weapon";
  case aimbot_debug_reason::no_target:
    return "no target";
  case aimbot_debug_reason::auto_scope:
    return "auto scope";
  case aimbot_debug_reason::auto_unscope:
    return "auto unscope";
  case aimbot_debug_reason::auto_rev:
    return "auto rev";
  case aimbot_debug_reason::attack_not_ready:
    return "attack wait";
  case aimbot_debug_reason::charge_wait:
    return "charge wait";
  case aimbot_debug_reason::scoped_only:
    return "scope wait";
  case aimbot_debug_reason::headshot_wait:
    return "head wait";
  case aimbot_debug_reason::settle_wait:
    return "settle wait";
  case aimbot_debug_reason::primary_wait:
    return "primary wait";
  case aimbot_debug_reason::hitbox_miss:
    return "body hit";
  case aimbot_debug_reason::final_trace_miss:
    return "trace miss";
  case aimbot_debug_reason::spread_seed_missing:
    return "spread seed";
  case aimbot_debug_reason::attack_ready:
    return "ready";
  }

  return "unknown";
}

inline const char* aimbot_debug_resolver_mode_name(int mode) {
  switch (mode) {
  case 1:
    return "moving";
  case 2:
    return "standing";
  case 3:
    return "jitter";
  case 4:
    return "spin";
  case 5:
    return "fakewalk";
  case 6:
    return "sideways";
  default:
    break;
  }

  return "unknown";
}

inline const char* aimbot_debug_reject_reason_name(aimbot_reject_reason reason) {
  switch (reason) {
  case aimbot_reject_reason::none:
    return "none";
  case aimbot_reject_reason::invalid:
    return "invalid";
  case aimbot_reject_reason::local:
    return "local";
  case aimbot_reject_reason::dormant:
    return "dormant";
  case aimbot_reject_reason::dead:
    return "dead";
  case aimbot_reject_reason::invulnerable:
    return "invuln";
  case aimbot_reject_reason::ignored:
    return "ignored";
  case aimbot_reject_reason::friend_state:
    return "friend";
  case aimbot_reject_reason::ipc_bot:
    return "ipc";
  case aimbot_reject_reason::cloaked:
    return "cloaked";
  case aimbot_reject_reason::team:
    return "team";
  case aimbot_reject_reason::type:
    return "type";
  case aimbot_reject_reason::no_candidate:
    return "no candidate";
  case aimbot_reject_reason::not_visible:
    return "not visible";
  case aimbot_reject_reason::fov:
    return "fov";
  case aimbot_reject_reason::no_model:
    return "no model";
  case aimbot_reject_reason::no_studio_model:
    return "no studio";
  case aimbot_reject_reason::no_hitbox_set:
    return "no hb set";
  case aimbot_reject_reason::no_hitbox:
    return "no hitbox";
  case aimbot_reject_reason::bone_cache:
    return "bone cache";
  case aimbot_reject_reason::bone_invalidate:
    return "bone invalidate";
  case aimbot_reject_reason::bone_access:
    return "bone access";
  case aimbot_reject_reason::bone_studio_header:
    return "bone studio header";
  case aimbot_reject_reason::bone_sequence:
    return "bone sequence";
  case aimbot_reject_reason::bone_animation:
    return "bone animation";
  case aimbot_reject_reason::bone_reconstruction:
    return "bone reconstruction";
  case aimbot_reject_reason::bone_attachments:
    return "bone attachments";
  case aimbot_reject_reason::bone_setup:
    return "bone setup";
  case aimbot_reject_reason::bone_matrices:
    return "bone matrices";
  case aimbot_reject_reason::pose_timing:
    return "pose timing";
  case aimbot_reject_reason::trace_blocked:
    return "blocked";
  case aimbot_reject_reason::wrong_hitbox:
    return "wrong hb";
  case aimbot_reject_reason::no_point:
    return "no point";
  }

  return "unknown";
}
#endif
