/*
/^-----^\   data: 2026-05-06
V  o o  V  file: src/features/combat/anti_aim/anti_aim.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "anti_aim.hpp"
#include "core/entity_cache.hpp"
#include "core/math/math.hpp"
#include "features/movement/bhop/bhop.hpp"
#include "features/combat/aimbot/aimbot.hpp"
#include "features/combat/tickbase/tickbase.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace anti_aim
{

namespace
{

constexpr float overlap_epsilon = 45.0f;
constexpr int attack_buttons = IN_ATTACK | IN_ATTACK2 | IN_ATTACK3;

struct anti_aim_state
{
  Vec3 real_angles{};
  Vec3 fake_angles{};
  int previous_attack_buttons = 0;
  int previous_command_number = 0;
  bool active = false;
  bool visual_angles = false;
};

anti_aim_state g_state{};

using pitch_mode = Misc::Exploits::anti_aim_pitch_mode;
using yaw_base = Misc::Exploits::anti_aim_yaw_base;
using yaw_mode = Misc::Exploits::anti_aim_yaw_mode;

[[nodiscard]] auto normalize_angle(float angle) -> float
{
  if (!std::isfinite(angle)) {
    return 0.0f;
  }

  return std::remainder(angle, 360.0f);
}

[[nodiscard]] auto clamp_angles(Vec3 angles) -> Vec3
{
  angles.x = std::clamp(normalize_angle(angles.x), -89.0f, 89.0f);
  angles.y = normalize_angle(angles.y);
  angles.z = 0.0f;
  return angles;
}

[[nodiscard]] auto deterministic_float(std::uint32_t seed, float minimum, float maximum) -> float
{
  seed ^= seed >> 16U;
  seed *= 0x7feb352dU;
  seed ^= seed >> 15U;
  seed *= 0x846ca68bU;
  seed ^= seed >> 16U;

  constexpr float scale = 1.0f / static_cast<float>(0x00ffffffU);
  const float value = static_cast<float>(seed & 0x00ffffffU) * scale;
  return minimum + ((maximum - minimum) * value);
}

[[nodiscard]] auto yaw_mode_enabled(yaw_mode mode, float offset) -> bool
{
  return mode != yaw_mode::off || std::fabs(offset) > 0.01f;
}

[[nodiscard]] auto should_run_settings() -> bool
{
  return config.misc.exploits.anti_aim
      && (config.misc.exploits.anti_aim_real_pitch != pitch_mode::off
          || config.misc.exploits.anti_aim_fake_pitch != pitch_mode::off
          || yaw_mode_enabled(config.misc.exploits.anti_aim_real_yaw, config.misc.exploits.anti_aim_real_yaw_offset)
          || yaw_mode_enabled(config.misc.exploits.anti_aim_fake_yaw, config.misc.exploits.anti_aim_fake_yaw_offset));
}

[[nodiscard]] auto attack_button_state(user_cmd* cmd) -> int
{
  return cmd != nullptr ? cmd->buttons & attack_buttons : 0;
}

[[nodiscard]] auto released_attack_button(user_cmd* cmd) -> bool
{
  if (cmd == nullptr || g_state.previous_command_number <= 0) {
    return false;
  }

  if (cmd->command_number != g_state.previous_command_number + 1) {
    return false;
  }

  return (g_state.previous_attack_buttons & ~attack_button_state(cmd)) != 0;
}

void store_attack_button_state(user_cmd* cmd)
{
  if (cmd == nullptr || cmd->command_number <= 0) {
    return;
  }

  g_state.previous_attack_buttons = attack_button_state(cmd);
  g_state.previous_command_number = cmd->command_number;
}

[[nodiscard]] auto calculate_angles_to_position(const Vec3& start, const Vec3& target) -> Vec3
{
  const Vec3 diff = target - start;
  const float hypotenuse = std::sqrt((diff.x * diff.x) + (diff.y * diff.y));
  return {
    -std::atan2(diff.z, hypotenuse) * radpi,
    std::atan2(diff.y, diff.x) * radpi,
    0.0f
  };
}

[[nodiscard]] auto closest_target_yaw(Player* localplayer, const Vec3& fallback_angles) -> float
{
  if (localplayer == nullptr) {
    return fallback_angles.y;
  }

  float best_fov = 360.0f;
  float best_yaw = fallback_angles.y;
  const Vec3 local_origin = localplayer->get_origin();
  const tf_team local_team = localplayer->get_team();

  for (Entity* entity : entity_cache[class_id::PLAYER]) {
    auto* player = reinterpret_cast<Player*>(entity);
    if (player == nullptr
        || player == localplayer
        || player->is_dormant()
        || !player->is_alive()
        || player->get_team() == local_team) {
      continue;
    }

    const Vec3 angles_to_player = calculate_angles_to_position(local_origin, player->get_origin());
    const float fov = std::fabs(normalize_angle(angles_to_player.y - fallback_angles.y));
    if (fov < best_fov) {
      best_fov = fov;
      best_yaw = angles_to_player.y;
    }
  }

  return best_yaw;
}

[[nodiscard]] auto base_yaw(Player* localplayer, user_cmd* cmd, bool fake, const Vec3& source_angles) -> float
{
  const yaw_base mode = fake
    ? config.misc.exploits.anti_aim_fake_yaw_base
    : config.misc.exploits.anti_aim_real_yaw_base;

  switch (mode) {
  case yaw_base::target:
    return closest_target_yaw(localplayer, source_angles);
  case yaw_base::view:
  default:
    return cmd != nullptr ? cmd->view_angles.y : source_angles.y;
  }
}

[[nodiscard]] auto jitter_sign(user_cmd* cmd, std::uint32_t salt) -> float
{
  const int command_number = cmd != nullptr ? cmd->command_number : 0;
  return ((static_cast<std::uint32_t>(command_number) + salt) & 1U) != 0U ? 1.0f : -1.0f;
}

[[nodiscard]] auto yaw_offset(Player* localplayer, user_cmd* cmd, bool fake) -> float
{
  (void)localplayer;

  const yaw_mode mode = fake
    ? config.misc.exploits.anti_aim_fake_yaw
    : config.misc.exploits.anti_aim_real_yaw;
  const float value = fake
    ? config.misc.exploits.anti_aim_fake_yaw_offset
    : config.misc.exploits.anti_aim_real_yaw_offset;
  const int tick = global_vars != nullptr ? global_vars->tickcount : cmd != nullptr ? cmd->tick_count : 0;
  const int command_number = cmd != nullptr ? cmd->command_number : tick;

  switch (mode) {
  case yaw_mode::forward:
    return value;
  case yaw_mode::left:
    return 90.0f + value;
  case yaw_mode::right:
    return -90.0f + value;
  case yaw_mode::backwards:
    return 180.0f + value;
  case yaw_mode::jitter:
    return (90.0f * jitter_sign(cmd, fake ? 0x9e3779b9U : 0x85ebca6bU)) + value;
  case yaw_mode::spin:
    return normalize_angle((static_cast<float>(tick) * config.misc.exploits.anti_aim_spin_speed) + value);
  case yaw_mode::random:
    return deterministic_float(static_cast<std::uint32_t>(command_number) ^ (fake ? 0x5bd1e995U : 0x27d4eb2fU), -180.0f, 180.0f) + value;
  case yaw_mode::sideways:
    return (((command_number & 1) != 0) ? 90.0f : -90.0f) + value;
  case yaw_mode::off:
  default:
    return value;
  }
}

[[nodiscard]] auto build_yaw(Player* localplayer, user_cmd* cmd, bool fake, const Vec3& source_angles) -> float
{
  float yaw = base_yaw(localplayer, cmd, fake, source_angles) + yaw_offset(localplayer, cmd, fake);

  if (!fake
      && config.misc.exploits.anti_aim_anti_overlap
      && yaw_mode_enabled(config.misc.exploits.anti_aim_real_yaw, config.misc.exploits.anti_aim_real_yaw_offset)
      && yaw_mode_enabled(config.misc.exploits.anti_aim_fake_yaw, config.misc.exploits.anti_aim_fake_yaw_offset)) {
    const float fake_yaw = base_yaw(localplayer, cmd, true, source_angles) + yaw_offset(localplayer, cmd, true);
    const float yaw_delta = normalize_angle(yaw - fake_yaw);
    const float abs_delta = std::fabs(yaw_delta);
    if (abs_delta < overlap_epsilon) {
      yaw += yaw_delta >= 0.0f ? overlap_epsilon - abs_delta : -(overlap_epsilon - abs_delta);
    }
  }

  return normalize_angle(yaw);
}

[[nodiscard]] auto build_pitch(pitch_mode mode, user_cmd* cmd, float source_pitch) -> float
{
  const int tick = global_vars != nullptr ? global_vars->tickcount : cmd != nullptr ? cmd->tick_count : 0;
  const int command_number = cmd != nullptr ? cmd->command_number : tick;

  switch (mode) {
  case pitch_mode::up:
    return -89.0f;
  case pitch_mode::down:
    return 89.0f;
  case pitch_mode::zero:
    return 0.0f;
  case pitch_mode::half_up:
    return -45.0f;
  case pitch_mode::half_down:
    return 45.0f;
  case pitch_mode::jitter:
    return ((command_number & 1) != 0) ? -89.0f : 89.0f;
  case pitch_mode::random:
    return deterministic_float(static_cast<std::uint32_t>(command_number) ^ 0x165667b1U, -89.0f, 89.0f);
  case pitch_mode::off:
  default:
    return source_pitch;
  }
}

[[nodiscard]] auto build_angles(Player* localplayer, user_cmd* cmd, bool fake, const Vec3& source_angles) -> Vec3
{
  const pitch_mode selected_pitch = fake
    ? config.misc.exploits.anti_aim_fake_pitch
    : config.misc.exploits.anti_aim_real_pitch;

  Vec3 angles = source_angles;
  angles.x = build_pitch(selected_pitch, cmd, source_angles.x);
  angles.y = build_yaw(localplayer, cmd, fake, source_angles);
  return clamp_angles(angles);
}

void fix_movement(user_cmd* cmd, const Vec3& source_angles, float source_forward_move, float source_side_move)
{
  if (cmd == nullptr) {
    return;
  }

  float yaw_delta = cmd->view_angles.y - source_angles.y;
  float source_yaw_correction = source_angles.y < 0.0f ? 360.0f + source_angles.y : source_angles.y;
  float target_yaw_correction = cmd->view_angles.y < 0.0f ? 360.0f + cmd->view_angles.y : cmd->view_angles.y;

  if (target_yaw_correction < source_yaw_correction) {
    yaw_delta = std::fabs(target_yaw_correction - source_yaw_correction);
  } else {
    yaw_delta = 360.0f - std::fabs(source_yaw_correction - target_yaw_correction);
  }
  yaw_delta = 360.0f - yaw_delta;

  cmd->forwardmove = std::cos(yaw_delta * pideg) * source_forward_move
    + std::cos((yaw_delta + 90.0f) * pideg) * source_side_move;
  cmd->sidemove = std::sin(yaw_delta * pideg) * source_forward_move
    + std::sin((yaw_delta + 90.0f) * pideg) * source_side_move;
}

[[nodiscard]] auto should_run(user_cmd* cmd) -> bool
{
  if (cmd == nullptr
      || cmd->command_number == 0
      || !should_run_settings()
      || engine == nullptr
      || entity_list == nullptr
      || client_state == nullptr
      || client_state->m_NetChannel == nullptr
      || !engine->is_in_game()
      || !tickbase::should_rebuild_cl_move()) {
    return false;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive()) {
    return false;
  }

  if (should_preserve_shot(cmd)) {
    return false;
  }

  if (moonwalk_applied_to_command(cmd->command_number)) {
    return false;
  }

  if (aimbot::has_active_target()) {
    return false;
  }

  if (localplayer->in_cond(TF_COND_TAUNTING)
      || localplayer->in_cond(TF_COND_SHIELD_CHARGE)
      || localplayer->in_cond(TF_COND_HALLOWEEN_KART)) {
    return false;
  }

  return true;
}

}

void on_create_move(user_cmd* cmd)
{
  g_state.active = false;

  if (!should_run(cmd)) {

    g_state.visual_angles = false;
    if (!g_state.visual_angles && cmd != nullptr) {
      g_state.real_angles = cmd->view_angles;
      g_state.fake_angles = cmd->view_angles;
    }
    store_attack_button_state(cmd);
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  const Vec3 source_angles = cmd->view_angles;
  const float source_forward_move = cmd->forwardmove;
  const float source_side_move = cmd->sidemove;

  g_state.real_angles = build_angles(localplayer, cmd, false, source_angles);
  g_state.fake_angles = build_angles(localplayer, cmd, true, source_angles);
  g_state.visual_angles = true;

  cmd->view_angles = tickbase::should_send_packet() ? g_state.fake_angles : g_state.real_angles;
  fix_movement(cmd, source_angles, source_forward_move, source_side_move);
  g_state.active = true;
  store_attack_button_state(cmd);
}

bool should_preserve_shot(user_cmd* cmd)
{
  return attack_button_state(cmd) != 0 || released_attack_button(cmd);
}

bool is_active()
{
  return g_state.active;
}

bool settings_enabled()
{
  return should_run_settings();
}

bool has_visual_angles()
{
  return g_state.visual_angles && should_run_settings();
}

Vec3 real_angles()
{
  return g_state.real_angles;
}

Vec3 fake_angles()
{
  return g_state.fake_angles;
}

}
