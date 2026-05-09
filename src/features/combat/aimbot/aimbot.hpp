/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/combat/aimbot/aimbot.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef AIMBOT_HPP
#define AIMBOT_HPP

#include <algorithm>

#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"

inline static Player* target_player = nullptr;
inline static Entity* target_entity = nullptr;
inline static bool autoscope_cached_scoped = false;
inline static float autoscope_next_scope_check_time = 0.0f;
inline static float aimbot_scoped_begin_time = 0.0f;
inline static Vec3 aimbot_last_input_angles{};
inline static bool aimbot_last_input_angles_valid = false;

struct AimbotPreference {
  Player* preferred_target = nullptr;
  float until_time = 0.0f;
};

inline static AimbotPreference aimbot_preference;

inline void clear_aimbot_preference() {
  aimbot_preference.preferred_target = nullptr;
  aimbot_preference.until_time = 0.0f;
}

inline void set_aimbot_preference(Player* player, float hold_for_seconds = 0.2f) {
  aimbot_preference.preferred_target = player;
  aimbot_preference.until_time = global_vars != nullptr ? (global_vars->curtime + hold_for_seconds) : hold_for_seconds;
}

inline bool has_aimbot_preference(Player* player) {
  if (player == nullptr || aimbot_preference.preferred_target != player) return false;
  return global_vars == nullptr || aimbot_preference.until_time >= global_vars->curtime;
}

inline void reset_autoscope_scope_state() {
  autoscope_cached_scoped = false;
  autoscope_next_scope_check_time = 0.0f;
}

inline void reset_aimbot_scope_timing() {
  aimbot_scoped_begin_time = 0.0f;
}

inline void reset_aimbot_input_history() {
  aimbot_last_input_angles = {};
  aimbot_last_input_angles_valid = false;
}

inline void store_aimbot_input_angles(const Vec3& view_angles) {
  aimbot_last_input_angles = view_angles;
  aimbot_last_input_angles_valid = true;
}

inline bool aimbot_autoscope_scoped_state(Player* localplayer) {
  if (localplayer == nullptr) {
    reset_autoscope_scope_state();
    return false;
  }

  autoscope_cached_scoped = localplayer->is_scoped();
  autoscope_next_scope_check_time = global_vars != nullptr ? global_vars->curtime : 0.0f;
  return autoscope_cached_scoped;
}

inline void update_aimbot_scope_timing(Player* localplayer) {
  if (localplayer == nullptr || !localplayer->is_scoped()) {
    reset_aimbot_scope_timing();
    return;
  }

  if (aimbot_scoped_begin_time > 0.0f) {
    return;
  }

  aimbot_scoped_begin_time = global_vars != nullptr
    ? global_vars->curtime
    : localplayer->get_tickbase() * static_cast<float>(TICK_INTERVAL);
}

inline float aimbot_tracked_scoped_time(Player* localplayer) {
  if (localplayer == nullptr || !localplayer->is_scoped() || aimbot_scoped_begin_time <= 0.0f) {
    return 0.0f;
  }

  const float current_time = global_vars != nullptr
    ? global_vars->curtime
    : localplayer->get_tickbase() * static_cast<float>(TICK_INTERVAL);
  return std::max(current_time - aimbot_scoped_begin_time, 0.0f);
}

bool aimbot(user_cmd* user_cmd, Vec3 original_view_angles);
bool aimbot_requested_shot();
void aimbot_apply_walk_to_target(Player* localplayer, user_cmd* user_cmd);

#endif
