/*
/^-----^\   data: 2026-05-01
V  o o  V  file: src/features/combat/tickbase/tickbase.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "tickbase.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

#include "core/hooks/cl_read_packets.hpp"
#include "features/combat/anti_aim/anti_aim.hpp"
#include "features/combat/aimbot/aimbot.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/input.hpp"
#include "games/tf2/sdk/interfaces/net_channel.hpp"
#include "games/tf2/sdk/net_messages.hpp"

namespace tickbase
{

namespace
{

constexpr int signon_state_connected = 2;
constexpr int signon_state_full = 6;
constexpr int max_shift_history = 64;
constexpr int max_choked_commands = 21;
constexpr int fallback_processing_ticks = 21;
constexpr int move_buffer_bytes = 4000;

enum class shift_mode
{
  none,
  doubletap,
  warp,
};

struct prediction_fix
{
  int command_number = 0;
  int tickbase = 0;
  bool valid = false;
};

struct state
{
  double* net_time = nullptr;
  float* host_frametime_unbounded = nullptr;
  float* host_frametime_std_deviation = nullptr;
  host_should_run_fn host_should_run = nullptr;

  std::array<prediction_fix, max_shift_history> prediction_fixes{};
  int prediction_fix_cursor = 0;

  int processing_ticks = 0;
  int shift_goal = 0;
  int shift_start_command = 0;
  int shift_start_tickbase = 0;
  Vec3 shift_velocity{};
  Vec3 shift_start_origin{};
  bool send_packet = true;
  bool final_tick = false;
  bool recharging = false;
  bool in_shift_rebuild = false;
  bool should_antiwarp = false;
  shift_mode mode = shift_mode::none;
};

state g_state{};

auto interval_per_tick() -> float
{
  if (global_vars != nullptr && global_vars->interval_per_tick > 0.0f) {
    return global_vars->interval_per_tick;
  }

  return TICK_INTERVAL;
}

auto get_cl_cmdrate() -> float
{
  static Convar* cl_cmdrate = nullptr;
  if (cl_cmdrate == nullptr && convar_system != nullptr) {
    cl_cmdrate = convar_system->find_var("cl_cmdrate");
  }

  const float value = cl_cmdrate != nullptr ? cl_cmdrate->get_float() : 66.0f;
  return std::max(value, 1.0f);
}

auto max_processing_ticks() -> int
{
  static Convar* sv_maxusrcmdprocessticks = nullptr;
  if (sv_maxusrcmdprocessticks == nullptr && convar_system != nullptr) {
    sv_maxusrcmdprocessticks = convar_system->find_var("sv_maxusrcmdprocessticks");
  }

  const int server_limit = sv_maxusrcmdprocessticks != nullptr
    ? sv_maxusrcmdprocessticks->get_int()
    : fallback_processing_ticks;

  return std::clamp(server_limit, 1, 24);
}

auto packet_rebuild_enabled() -> bool
{
  return config.misc.exploits.tickbase
      || config.misc.exploits.fakelag
      || config.misc.exploits.anti_aim;
}

auto rebuild_dependencies_ready() -> bool
{
  return client != nullptr
      && client_state != nullptr
      && client_state->m_NetChannel != nullptr
      && global_vars != nullptr
      && engine != nullptr
      && input != nullptr
      && g_state.net_time != nullptr
      && g_state.host_frametime_unbounded != nullptr
      && g_state.host_frametime_std_deviation != nullptr
      && g_state.host_should_run != nullptr;
}

auto can_rebuild_packets() -> bool
{
  return packet_rebuild_enabled() && rebuild_dependencies_ready();
}

auto latest_command_number() -> int
{
  if (client_state == nullptr) {
    return 0;
  }

  return client_state->lastoutgoingcommand + client_state->chokedcommands + 1;
}

auto available_shift_ticks(bool warp) -> int
{
  if (!config.misc.exploits.tickbase || client_state == nullptr) {
    return 0;
  }

  const int available_ticks = std::min(
    std::max(0, g_state.processing_ticks - client_state->chokedcommands),
    max_processing_ticks());

  const int requested_ticks = warp
    ? config.misc.exploits.warp_ticks
    : config.misc.exploits.doubletap_ticks;

  return std::clamp(std::min(available_ticks, requested_ticks), 0, max_processing_ticks());
}

auto is_button_ready(button& bind) -> bool
{
  if (bind.button == SDLK_UNKNOWN) {
    return true;
  }

  return is_button_active(bind);
}

auto is_weapon_supported_for_shift(Weapon* weapon) -> bool
{
  if (weapon == nullptr) {
    return false;
  }

  switch (weapon->get_weapon_id()) {
  case TF_WEAPON_MEDIGUN:
  case TF_WEAPON_BUILDER:
  case TF_WEAPON_PDA:
  case TF_WEAPON_PDA_ENGINEER_BUILD:
  case TF_WEAPON_PDA_ENGINEER_DESTROY:
  case TF_WEAPON_PDA_SPY:
  case TF_WEAPON_PDA_SPY_BUILD:
  case TF_WEAPON_INVIS:
  case TF_WEAPON_JAR:
  case TF_WEAPON_JAR_MILK:
  case TF_WEAPON_LUNCHBOX:
  case TF_WEAPON_BUFF_ITEM:
  case TF_WEAPON_GRAPPLINGHOOK:
    return false;
  default:
    return true;
  }
}

auto is_attack_command(user_cmd* cmd) -> bool
{
  return cmd != nullptr && (cmd->buttons & IN_ATTACK) != 0;
}

auto can_attack_at(Player* localplayer, Weapon* weapon, float time) -> bool
{
  if (localplayer == nullptr || weapon == nullptr) {
    return false;
  }

  return localplayer->get_next_attack() <= time && weapon->get_next_primary_attack() <= time;
}

void add_prediction_fix(int command_number, int tickbase)
{
  if (command_number <= 0 || tickbase <= 0) {
    return;
  }

  for (auto& fix : g_state.prediction_fixes) {
    if (fix.valid && fix.command_number == command_number) {
      fix.tickbase = tickbase;
      return;
    }
  }

  auto& fix = g_state.prediction_fixes[static_cast<std::size_t>(g_state.prediction_fix_cursor)];
  fix.command_number = command_number;
  fix.tickbase = tickbase;
  fix.valid = true;
  g_state.prediction_fix_cursor = (g_state.prediction_fix_cursor + 1) % max_shift_history;
}

void prune_prediction_fixes()
{
  if (client_state == nullptr) {
    return;
  }

  for (auto& fix : g_state.prediction_fixes) {
    if (fix.valid && client_state->command_ack >= fix.command_number) {
      fix.valid = false;
    }
  }
}

void spend_shift_tick()
{
  g_state.processing_ticks = std::max(0, g_state.processing_ticks - 1);
}

void recharge_shift_tick()
{
  g_state.processing_ticks = std::min(max_processing_ticks(), g_state.processing_ticks + 1);
}

auto should_recharge() -> bool
{
  if (!config.misc.exploits.tickbase
      || !config.misc.exploits.tickbase_recharge
      || g_state.in_shift_rebuild
      || g_state.mode != shift_mode::none) {
    return false;
  }

  return g_state.processing_ticks < max_processing_ticks();
}

void set_choked_command()
{
  auto* channel = client_state != nullptr ? client_state->m_NetChannel : nullptr;
  if (channel == nullptr) {
    return;
  }

  channel->set_choked();
  ++client_state->chokedcommands;
}

auto send_move() -> bool
{
  auto* channel = client_state != nullptr ? client_state->m_NetChannel : nullptr;
  if (channel == nullptr || client == nullptr) {
    return false;
  }

  alignas(4) std::array<std::uint8_t, move_buffer_bytes> data{};
  clc_move_message message{};

  const int command_count = 1 + client_state->chokedcommands;
  message.data_out.start_writing(data.data(), static_cast<int>(data.size()));
  message.new_commands = std::clamp(command_count, 0, max_new_commands);

  const int extra_commands = std::max(0, command_count - message.new_commands);
  message.backup_commands = std::clamp(extra_commands, 2, max_backup_commands);

  const int command_total = message.new_commands + message.backup_commands;
  const int next_command = client_state->lastoutgoingcommand + command_count;
  const int first_command = next_command - command_total + 1;
  const int first_new_command = next_command - message.new_commands + 1;

  auto from = -1;
  auto ok = true;
  for (int to = first_command; to <= next_command; ++to) {
    const bool is_new_command = to >= first_new_command;
    ok = ok && client->write_user_cmd_delta_to_buffer(&message.data_out, from, to, is_new_command);
    from = to;
  }

  if (!ok || message.data_out.is_overflowed()) {
    return false;
  }

  if (extra_commands > 0) {
    auto* channel_storage = reinterpret_cast<net_channel_storage*>(channel);
    channel_storage->choked_packets = std::max(0, channel_storage->choked_packets - extra_commands);
  }

  if (!channel->send_net_msg(message, false, false)) {
    return false;
  }

  return true;
}

void send_tick()
{
  auto* channel = client_state != nullptr ? client_state->m_NetChannel : nullptr;
  if (channel == nullptr) {
    return;
  }

  net_tick_message tick_message(
    client_state->m_nDeltaTick,
    *g_state.host_frametime_unbounded,
    *g_state.host_frametime_std_deviation);

  channel->send_net_msg(tick_message, false, false);
}

void update_next_command_time()
{
  if (client_state == nullptr || g_state.net_time == nullptr) {
    return;
  }

  if (client_state->m_nSignonState == signon_state_full) {
    const float command_interval = 1.0f / get_cl_cmdrate();
    const float max_delta = std::min(interval_per_tick(), command_interval);
    const float delta = std::clamp(static_cast<float>(*g_state.net_time - client_state->m_flNextCmdTime), 0.0f, max_delta);
    client_state->m_flNextCmdTime = *g_state.net_time + command_interval - delta;
    return;
  }

  client_state->m_flNextCmdTime = *g_state.net_time + 0.2;
}

void apply_fakelag(user_cmd* cmd)
{
  if (!config.misc.exploits.fakelag
      || g_state.in_shift_rebuild
      || g_state.mode != shift_mode::none
      || client_state == nullptr) {
    return;
  }

  if (is_attack_command(cmd) || aimbot_has_active_target()) {
    g_state.send_packet = true;
    return;
  }

  const int wanted_choke = std::clamp(config.misc.exploits.fakelag_ticks, 1, max_choked_commands);
  if (client_state->chokedcommands < wanted_choke) {
    g_state.send_packet = false;
  }
}

auto anti_aim_has_active_settings() -> bool
{
  using pitch_mode = Misc::Exploits::anti_aim_pitch_mode;
  using yaw_mode = Misc::Exploits::anti_aim_yaw_mode;

  return config.misc.exploits.anti_aim
      && (config.misc.exploits.anti_aim_real_pitch != pitch_mode::off
          || config.misc.exploits.anti_aim_fake_pitch != pitch_mode::off
          || config.misc.exploits.anti_aim_real_yaw != yaw_mode::off
          || config.misc.exploits.anti_aim_fake_yaw != yaw_mode::off
          || config.misc.exploits.anti_aim_real_yaw_offset != 0.0f
          || config.misc.exploits.anti_aim_fake_yaw_offset != 0.0f);
}

void apply_anti_aim_choke(user_cmd* cmd)
{
  if (!anti_aim_has_active_settings()
      || g_state.in_shift_rebuild
      || g_state.mode != shift_mode::none
      || client_state == nullptr
      || cmd == nullptr) {
    return;
  }

  if (anti_aim::should_preserve_shot(cmd) || aimbot_has_active_target()) {
    g_state.send_packet = true;
    return;
  }

  auto* localplayer = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  if (localplayer == nullptr
      || !localplayer->is_alive()
      || localplayer->in_cond(TF_COND_SHIELD_CHARGE)
      || localplayer->in_cond(TF_COND_HALLOWEEN_KART)) {
    return;
  }

  if (g_state.send_packet && client_state->chokedcommands == 0) {
    g_state.send_packet = false;
  }
}

void apply_antiwarp(Player* localplayer, user_cmd* cmd)
{
  if (!config.misc.exploits.antiwarp
      || !g_state.should_antiwarp
      || localplayer == nullptr
      || cmd == nullptr
      || !localplayer->is_alive()
      || (localplayer->get_flags() & FL_ONGROUND) == 0) {
    return;
  }

  cmd->forwardmove = 0.0f;
  cmd->sidemove = 0.0f;
}

void start_shift(Player* localplayer, int command_number, int ticks_to_shift, shift_mode mode)
{
  if (localplayer == nullptr || command_number <= 0 || ticks_to_shift <= 0) {
    return;
  }

  g_state.mode = mode;
  g_state.shift_goal = std::max(0, g_state.processing_ticks - ticks_to_shift);
  g_state.shift_start_command = command_number;
  g_state.shift_start_tickbase = localplayer->get_tickbase();
  g_state.shift_start_origin = localplayer->get_origin();
  g_state.shift_velocity = localplayer->get_velocity();
  g_state.should_antiwarp = config.misc.exploits.antiwarp && mode == shift_mode::doubletap;
  g_state.send_packet = true;

  add_prediction_fix(command_number, g_state.shift_start_tickbase);
}

void update_shift_state(user_cmd* cmd)
{
  if (!config.misc.exploits.tickbase || g_state.in_shift_rebuild || g_state.mode != shift_mode::none) {
    return;
  }

  auto* localplayer = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  if (localplayer == nullptr || !localplayer->is_alive()) {
    return;
  }

  auto* weapon = localplayer->get_weapon();
  if (!is_weapon_supported_for_shift(weapon)) {
    return;
  }

  if (config.misc.exploits.warp && is_button_ready(config.misc.exploits.warp_key)) {
    const int ticks_to_shift = available_shift_ticks(true);
    if (ticks_to_shift > 0) {
      start_shift(localplayer, cmd->command_number, ticks_to_shift, shift_mode::warp);
    }
    return;
  }

  if (!config.misc.exploits.doubletap
      || !is_button_ready(config.misc.exploits.doubletap_key)
      || !is_attack_command(cmd)) {
    return;
  }

  const int ticks_to_shift = available_shift_ticks(false);
  const float shifted_time = static_cast<float>(localplayer->get_tickbase() + ticks_to_shift) * interval_per_tick();
  if (ticks_to_shift > 0 && can_attack_at(localplayer, weapon, shifted_time)) {
    start_shift(localplayer, cmd->command_number, ticks_to_shift, shift_mode::doubletap);
  }
}

auto run_rebuilt_move(float accumulated_extra_samples, bool final_tick, bool force_send) -> bool
{
  if (!can_rebuild_packets()) {
    return false;
  }

  if (client_state->m_nSignonState < signon_state_connected || !g_state.host_should_run()) {
    return true;
  }

  prune_prediction_fixes();
  run_network_fix_before_move(final_tick);

  g_state.send_packet = true;
  g_state.final_tick = final_tick;

  auto* channel = client_state != nullptr ? client_state->m_NetChannel : nullptr;
  if (channel == nullptr) {
    return false;
  }

  const bool should_gate_packet = !force_send
      && (!channel->is_loopback())
      && ((*g_state.net_time < client_state->m_flNextCmdTime) || !channel->can_packet() || !final_tick);
  const bool packet_gate_open = !should_gate_packet;

  if (should_gate_packet) {
    g_state.send_packet = false;
  }

  if (client_state->m_nSignonState == signon_state_full) {
    if (force_send) {
      spend_shift_tick();
    }

    if (!force_send && should_recharge() && client_state->chokedcommands == 0) {
      recharge_shift_tick();
      g_state.recharging = true;
      g_state.send_packet = false;
      return true;
    }

    g_state.recharging = false;

    const int next_command = latest_command_number();
    client->create_move(next_command, interval_per_tick() - accumulated_extra_samples, true);

    if (!packet_gate_open) {
      g_state.send_packet = false;
    }

    if (force_send) {
      g_state.send_packet = final_tick;
    }

    if (client_state->chokedcommands >= max_choked_commands) {
      g_state.send_packet = true;
    }

    if (g_state.send_packet) {
      send_move();
    } else {
      set_choked_command();
    }
  }

  if (!g_state.send_packet) {
    return true;
  }

  channel = client_state != nullptr ? client_state->m_NetChannel : nullptr;
  if (channel == nullptr) {
    return false;
  }

  if (client_state->m_nSignonState == signon_state_full) {
    if (channel->is_timing_out()) {
      client_state->m_nDeltaTick = -1;
    }

    send_tick();
  }

  client_state->lastoutgoingcommand = channel->send_datagram(nullptr);
  client_state->chokedcommands = 0;
  update_next_command_time();
  return true;
}

void finish_shift(float accumulated_extra_samples)
{
  if (g_state.mode == shift_mode::none || g_state.in_shift_rebuild) {
    return;
  }

  g_state.in_shift_rebuild = true;

  while (g_state.processing_ticks > g_state.shift_goal) {
    const int previous_ticks = g_state.processing_ticks;
    const int next_command = latest_command_number();
    const int command_offset = std::max(0, next_command - g_state.shift_start_command);
    const bool final_shift_tick = g_state.processing_ticks - 1 <= g_state.shift_goal;

    add_prediction_fix(next_command, g_state.shift_start_tickbase + command_offset);
    run_rebuilt_move(0.0f, final_shift_tick, true);

    if (g_state.processing_ticks >= previous_ticks) {
      break;
    }
  }

  g_state.in_shift_rebuild = false;
  g_state.recharging = false;
  g_state.should_antiwarp = false;
  g_state.shift_goal = 0;
  g_state.shift_start_command = 0;
  g_state.shift_start_tickbase = 0;
  g_state.mode = shift_mode::none;

  (void)accumulated_extra_samples;
}

} // namespace

void reset()
{
  const auto net_time = g_state.net_time;
  const auto host_frametime_unbounded = g_state.host_frametime_unbounded;
  const auto host_frametime_std_deviation = g_state.host_frametime_std_deviation;
  const auto host_should_run = g_state.host_should_run;

  g_state = {};
  g_state.net_time = net_time;
  g_state.host_frametime_unbounded = host_frametime_unbounded;
  g_state.host_frametime_std_deviation = host_frametime_std_deviation;
  g_state.host_should_run = host_should_run;
}

void initialize_engine_globals(double* net_time, float* host_frametime_unbounded, float* host_frametime_std_deviation,
  host_should_run_fn host_should_run)
{
  g_state.net_time = net_time;
  g_state.host_frametime_unbounded = host_frametime_unbounded;
  g_state.host_frametime_std_deviation = host_frametime_std_deviation;
  g_state.host_should_run = host_should_run;
}

void move(float accumulated_extra_samples, bool final_tick, cl_move_fn original)
{
  if (!should_rebuild_cl_move()) {
    if (original != nullptr) {
      run_network_fix_before_move(final_tick);
      original(accumulated_extra_samples, final_tick);
    }
    return;
  }

  if (!run_rebuilt_move(accumulated_extra_samples, final_tick, false)) {
    if (original != nullptr) {
      original(accumulated_extra_samples, final_tick);
    }
    return;
  }

  finish_shift(accumulated_extra_samples);
}

void on_create_move(user_cmd* cmd)
{
  if (cmd == nullptr || !can_rebuild_packets()) {
    return;
  }

  auto* localplayer = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  apply_antiwarp(localplayer, cmd);
  update_shift_state(cmd);
  apply_fakelag(cmd);
  apply_anti_aim_choke(cmd);
}

void apply_prediction_fix(int command_number, user_cmd* cmd, Player* player, float* curtime)
{
  (void)cmd;

  if (player == nullptr || curtime == nullptr) {
    return;
  }

  for (auto& fix : g_state.prediction_fixes) {
    if (!fix.valid || fix.command_number != command_number) {
      continue;
    }

    player->set_tickbase(fix.tickbase);
    *curtime = static_cast<float>(fix.tickbase) * interval_per_tick();
    return;
  }
}

auto should_rebuild_cl_move() -> bool
{
  return can_rebuild_packets();
}

auto should_send_packet() -> bool
{
  return g_state.send_packet;
}

auto get_indicator_state() -> indicator_state
{
  return {
    .processing_ticks = g_state.processing_ticks,
    .max_processing_ticks = max_processing_ticks(),
    .available_shift_ticks = available_shift_ticks(false),
    .choked_commands = client_state != nullptr ? client_state->chokedcommands : 0,
    .recharging = g_state.recharging,
    .shifting = g_state.mode != shift_mode::none || g_state.in_shift_rebuild,
    .doubletap = g_state.mode == shift_mode::doubletap,
    .warp = g_state.mode == shift_mode::warp,
    .fakelag = config.misc.exploits.fakelag,
  };
}

} // namespace tickbase
