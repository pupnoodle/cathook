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
#include <cmath>
#include <climits>
#include <cstdint>
#include "core/hooks/cl_read_packets.hpp"
#include "features/combat/anti_aim/anti_aim.hpp"
#include "features/combat/aimbot/aimbot.hpp"
#include "features/combat/random_crits/crit_hack.hpp"
#include "features/movement/bhop/bhop.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/input.hpp"
#include "games/tf2/sdk/interfaces/net_channel.hpp"
#include "games/tf2/sdk/net_messages.hpp"
#include "external/MD5/MD5.hpp"

namespace tickbase
{

namespace
{

constexpr int signon_state_full = 6;
constexpr int max_shift_history = 64;
constexpr int max_choked_commands = max_new_commands + max_backup_commands - 1;
constexpr int max_processing_ticks_fallback = 24;
constexpr int move_buffer_bytes = 4000;
constexpr int attack_buttons = IN_ATTACK | IN_ATTACK2 | IN_ATTACK3;

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
  int next_shift_command = 0;
  user_cmd first_shot_command{};
  Vec3 shift_velocity{};
  Vec3 shift_start_origin{};
  bool send_packet = true;
  bool final_tick = false;
  bool recharging = false;
  bool in_shift_rebuild = false;
  bool should_antiwarp = false;
  net_channel* session_channel = nullptr;
  int session_signon_state = 0;
  int session_server_count = 0;
  shift_mode mode = shift_mode::none;
};

state g_state{};

void reset_runtime_state()
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

void synchronize_session()
{
  if (client_state == nullptr) {
    return;
  }

  auto* channel = client_state->m_NetChannel;
  const int signon_state = client_state->m_nSignonState;
  const int server_count = client_state->m_nServerCount;
  if (g_state.session_channel != channel
      || (g_state.session_signon_state == signon_state_full && signon_state != signon_state_full)
      || (g_state.session_server_count != 0 && server_count != 0 && g_state.session_server_count != server_count)) {
    reset_runtime_state();
  }

  g_state.session_channel = channel;
  g_state.session_signon_state = signon_state;
  g_state.session_server_count = server_count;
}

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
    : max_processing_ticks_fallback;

  return std::clamp(server_limit, 1, 24);
}

auto host_frame_ticks() -> int
{
  if (global_vars == nullptr) {
    return 1;
  }

  return std::clamp(std::max(global_vars->sim_ticks_this_frame, 1), 1, max_choked_commands);
}

auto time_to_ticks(float time) -> int
{
  const float interval = interval_per_tick();
  if (interval <= 0.0f || !std::isfinite(time)) {
    return 0;
  }

  return static_cast<int>(0.5f + time / interval);
}

auto ticks_to_time(int ticks) -> float
{
  return static_cast<float>(ticks) * interval_per_tick();
}

auto server_tick() -> int
{
  if (client_state == nullptr || client_state->m_NetChannel == nullptr) {
    return global_vars != nullptr ? global_vars->tickcount : 0;
  }

  const float latency = client_state->m_NetChannel->get_latency(0);
  const float server_time = ticks_to_time(client_state->m_ClockDriftMgr.m_nServerTick + 1);
  return time_to_ticks(server_time + (std::isfinite(latency) ? std::max(latency, 0.0f) : 0.0f)) - 1;
}

auto adjusted_tick(int sim_ticks, int tickbase, int current_server_tick) -> int
{
  if (sim_ticks < 0) {
    return tickbase;
  }

  if (global_vars != nullptr && global_vars->max_clients == 1) {
    return global_vars->tickcount - sim_ticks + 1;
  }

  static Convar* sv_clockcorrection_msecs = nullptr;
  if (sv_clockcorrection_msecs == nullptr && convar_system != nullptr) {
    sv_clockcorrection_msecs = convar_system->find_var("sv_clockcorrection_msecs");
  }

  const float correction_seconds = sv_clockcorrection_msecs != nullptr
    ? std::clamp(sv_clockcorrection_msecs->get_float() / 1000.0f, 0.0f, 1.0f)
    : 0.06f;
  const int correction_ticks = time_to_ticks(correction_seconds);
  const int ideal_final_tick = current_server_tick + correction_ticks;
  const int estimated_final_tick = tickbase + sim_ticks;
  const int fast_limit = ideal_final_tick + correction_ticks;

  if (estimated_final_tick > fast_limit || estimated_final_tick < current_server_tick) {
    return ideal_final_tick - sim_ticks + 1;
  }

  return tickbase;
}

auto packet_rebuild_enabled() -> bool
{
  return config.misc.exploits.tickbase
      || config.misc.exploits.fakelag
      || config.misc.exploits.anti_aim
      || config.misc.exploits.ping_reducer;
}

void apply_ping_reducer_alignment()
{
  if (!config.misc.exploits.ping_reducer
      || client_state == nullptr
      || client_state->m_NetChannel == nullptr
      || client_state->m_nSignonState != signon_state_full
      || g_state.net_time == nullptr
      || g_state.in_shift_rebuild
      || g_state.mode != shift_mode::none) {
    return;
  }

  auto* channel = client_state->m_NetChannel;
  if (channel->is_loopback()) {
    return;
  }

  const double tick_interval = static_cast<double>(interval_per_tick());
  if (tick_interval <= 0.0) {
    return;
  }

  const double now = *g_state.net_time;
  const double natural_next = client_state->m_flNextCmdTime;
  if (natural_next <= now) {
    return;
  }

  const float rtt = channel->get_avg_latency(0);
  const double one_way = std::isfinite(rtt) && rtt > 0.0f
    ? std::clamp(static_cast<double>(rtt) * 0.5, 0.0, 0.250)
    : 0.0;

  const double expected_arrival = natural_next + one_way;
  const double aligned_arrival = std::ceil(expected_arrival / tick_interval) * tick_interval;
  constexpr double safety_margin = 0.0015;
  const double aligned_send = aligned_arrival - one_way - safety_margin;

  const double extra_wait = aligned_send - natural_next;
  if (extra_wait > 0.0 && extra_wait < tick_interval) {
    client_state->m_flNextCmdTime = aligned_send;
  }
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

  const int choked_commands = std::clamp(client_state->chokedcommands, 0, max_choked_commands + 1);
  const int shift_limit = std::min(g_state.processing_ticks - choked_commands, max_choked_commands)
    - (host_frame_ticks() - 1);
  const int requested_ticks = warp
    ? std::clamp(config.misc.exploits.warp_ticks, 1, max_choked_commands)
    : std::clamp(config.misc.exploits.doubletap_ticks, 1, max_choked_commands);

  const int packet_capacity = std::max(0, max_choked_commands - choked_commands);
  return std::clamp(std::min(std::min(shift_limit, requested_ticks), packet_capacity), 0, max_choked_commands);
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
  return cmd != nullptr && (cmd->buttons & attack_buttons) != 0;
}

auto can_attack_at(Player* localplayer, Weapon* weapon, float time) -> bool
{
  if (localplayer == nullptr || weapon == nullptr) {
    return false;
  }

  return localplayer->get_next_attack() <= time && weapon->get_next_primary_attack() <= time;
}

auto is_attacking_for_shift(user_cmd* cmd, Player* localplayer, Weapon* weapon, float time) -> bool
{
  if (cmd == nullptr || localplayer == nullptr || weapon == nullptr || !can_attack_at(localplayer, weapon, time)) {
    return false;
  }

  const int weapon_id = weapon->get_weapon_id();
  if (weapon_id == TF_WEAPON_PIPEBOMBLAUNCHER) {
    const float charge_begin = weapon->get_charge_begin_time();
    if (charge_begin <= 0.0f) {
      return (cmd->buttons & IN_ATTACK) != 0;
    }

    return ((cmd->buttons & IN_ATTACK) != 0 && time - charge_begin > 4.0f)
      || (cmd->buttons & IN_ATTACK) == 0;
  }

  if (weapon_id == TF_WEAPON_COMPOUND_BOW
      || weapon_id == TF_WEAPON_CANNON
      || weapon_id == TF_WEAPON_SNIPERRIFLE_CLASSIC) {
    return (cmd->buttons & IN_ATTACK) == 0 && weapon->get_charge_begin_time() > 0.0f;
  }

  return (cmd->buttons & attack_buttons) != 0;
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

auto should_recharge() -> bool
{
  if (!config.misc.exploits.tickbase
      || !config.misc.exploits.tickbase_recharge
      || g_state.in_shift_rebuild
      || g_state.mode != shift_mode::none) {
    return false;
  }

  const int choked_commands = client_state != nullptr ? client_state->chokedcommands : 0;
  return g_state.processing_ticks - choked_commands < max_processing_ticks();
}

void set_choked_command()
{
  auto* channel = client_state != nullptr ? client_state->m_NetChannel : nullptr;
  if (channel == nullptr || client_state->chokedcommands >= max_choked_commands) {
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
  message.backup_commands = std::clamp(extra_commands, 0, max_backup_commands);

  const int command_total = message.new_commands + message.backup_commands;
  const int next_command = client_state->lastoutgoingcommand + command_count;
  const int first_command = next_command - command_total + 1;

  auto from = -1;
  auto ok = true;
  for (int to = first_command; to <= next_command; ++to) {

    ok = ok && client->write_user_cmd_delta_to_buffer(&message.data_out, from, to, true);
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

unsigned int crc32_process_byte(unsigned int crc, unsigned char value)
{
  crc ^= value;
  for (int bit = 0; bit < 8; ++bit) {
    const unsigned int mask = 0U - (crc & 1U);
    crc = (crc >> 1) ^ (0xEDB88320U & mask);
  }

  return crc;
}

unsigned int crc32_process_buffer(unsigned int crc, const void* data, int size)
{
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (int i = 0; i < size; ++i) {
    crc = crc32_process_byte(crc, bytes[i]);
  }

  return crc;
}

unsigned int user_cmd_checksum(const user_cmd& cmd)
{
  unsigned int crc = 0xFFFFFFFFU;
  crc = crc32_process_buffer(crc, &cmd.command_number, sizeof(cmd.command_number));
  crc = crc32_process_buffer(crc, &cmd.tick_count, sizeof(cmd.tick_count));
  crc = crc32_process_buffer(crc, &cmd.view_angles, sizeof(cmd.view_angles));
  crc = crc32_process_buffer(crc, &cmd.forwardmove, sizeof(cmd.forwardmove));
  crc = crc32_process_buffer(crc, &cmd.sidemove, sizeof(cmd.sidemove));
  crc = crc32_process_buffer(crc, &cmd.upmove, sizeof(cmd.upmove));
  crc = crc32_process_buffer(crc, &cmd.buttons, sizeof(cmd.buttons));
  crc = crc32_process_buffer(crc, &cmd.impulse, sizeof(cmd.impulse));
  crc = crc32_process_buffer(crc, &cmd.weapon_select, sizeof(cmd.weapon_select));
  crc = crc32_process_buffer(crc, &cmd.weapon_subtype, sizeof(cmd.weapon_subtype));
  crc = crc32_process_buffer(crc, &cmd.random_seed, sizeof(cmd.random_seed));
  crc = crc32_process_buffer(crc, &cmd.mouse_dx, sizeof(cmd.mouse_dx));
  crc = crc32_process_buffer(crc, &cmd.mouse_dy, sizeof(cmd.mouse_dy));
  return crc ^ 0xFFFFFFFFU;
}

void update_verified_user_cmd(int sequence_number, user_cmd* cmd)
{
  if (input == nullptr || cmd == nullptr) {
    return;
  }

  auto* verified_cmd = input->get_verified_user_cmd(sequence_number);
  if (verified_cmd == nullptr) {
    return;
  }

  verified_cmd->cmd = *cmd;
  verified_cmd->crc = user_cmd_checksum(*cmd);
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
    apply_ping_reducer_alignment();
    return;
  }

  client_state->m_flNextCmdTime = *g_state.net_time + 0.2;
}

auto flush_packet() -> bool
{
  auto* channel = client_state != nullptr ? client_state->m_NetChannel : nullptr;
  if (channel == nullptr) {
    return false;
  }

  if (client_state->m_nSignonState == signon_state_full) {
    if (channel->is_timing_out()) {
      client_state->m_nDeltaTick = -1;
    }

    send_tick();
  }

  const int outgoing_command = channel->send_datagram(nullptr);
  if (outgoing_command < 0) {
    return false;
  }

  client_state->lastoutgoingcommand = outgoing_command;
  client_state->chokedcommands = 0;
  update_next_command_time();
  return true;
}

void apply_fakelag(user_cmd* cmd)
{
  if (!config.misc.exploits.fakelag
      || g_state.in_shift_rebuild
      || g_state.mode != shift_mode::none
      || client_state == nullptr) {
    return;
  }

  if (is_attack_command(cmd) || aimbot::has_active_target()) {
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

  if (anti_aim::should_preserve_shot(cmd) || aimbot::has_active_target()) {
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
      || !localplayer->is_alive()) {
    return;
  }

  cmd->forwardmove = 0.0f;
  cmd->sidemove = 0.0f;
}

void start_shift(Player* localplayer, Weapon* weapon, user_cmd* cmd, int ticks_to_shift, shift_mode mode)
{
  if (localplayer == nullptr || cmd == nullptr || cmd->command_number <= 0 || ticks_to_shift <= 0) {
    return;
  }

  const int corrected_tickbase = adjusted_tick(
    ticks_to_shift + 1,
    localplayer->get_tickbase(),
    server_tick());
  localplayer->set_tickbase(corrected_tickbase);

  g_state.mode = mode;
  g_state.shift_goal = std::max(0, g_state.processing_ticks - ticks_to_shift - 1);
  g_state.shift_start_command = cmd->command_number;
  g_state.shift_start_tickbase = corrected_tickbase;
  g_state.next_shift_command = cmd->command_number + 1;
  g_state.shift_start_origin = localplayer->get_origin();
  g_state.shift_velocity = localplayer->get_velocity();
  g_state.should_antiwarp = config.misc.exploits.antiwarp
    && mode == shift_mode::doubletap
    && !weapon->is_melee()
    && weapon->get_projectile_type() == 0;
  if (g_state.should_antiwarp) {
    apply_antiwarp(localplayer, cmd);
  }
  g_state.first_shot_command = *cmd;
  g_state.send_packet = true;

  add_prediction_fix(cmd->command_number, g_state.shift_start_tickbase);
}

auto create_shifted_command(int command_number) -> user_cmd*
{
  if (input == nullptr || input->commands() == nullptr || command_number <= 0) {
    return nullptr;
  }

  auto* cmd = &input->commands()[command_number % Input::command_buffer_size];
  *cmd = g_state.first_shot_command;
  cmd->command_number = command_number;
  cmd->random_seed = static_cast<int>(
    MD5_PseudoRandom(static_cast<unsigned int>(command_number)) & INT_MAX);
  cmd->has_been_predicted = false;

  auto* localplayer = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  if (g_state.should_antiwarp) {
    apply_antiwarp(localplayer, cmd);
  } else {
    bhop(cmd);
  }

  anti_aim::on_create_move(cmd);
  return cmd;
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

  if (crit_hack::should_hold_attack(cmd)) {
    return;
  }

  if (config.misc.exploits.warp) {
    const int ticks_to_shift = available_shift_ticks(true);
    if (ticks_to_shift > 0) {
      start_shift(localplayer, weapon, cmd, ticks_to_shift, shift_mode::warp);
    }
    return;
  }

  if (!config.misc.exploits.doubletap
      || !is_attack_command(cmd)
      || g_state.processing_ticks < 8) {
    return;
  }

  const int ticks_to_shift = available_shift_ticks(false);
  const int adjusted_tickbase = adjusted_tick(
    ticks_to_shift + 1,
    localplayer->get_tickbase(),
    server_tick());
  const float adjusted_time = ticks_to_time(adjusted_tickbase);
  if (ticks_to_shift > 0 && is_attacking_for_shift(cmd, localplayer, weapon, adjusted_time)) {
    start_shift(localplayer, weapon, cmd, ticks_to_shift, shift_mode::doubletap);
  }
}

auto run_rebuilt_move(float accumulated_extra_samples, bool final_tick, bool force_send) -> bool
{
  if (!can_rebuild_packets()) {
    return false;
  }

  if (client_state->m_nSignonState != signon_state_full || !g_state.host_should_run()) {
    return false;
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

  if (force_send) {
    g_state.send_packet = final_tick;
  }

  if (client_state->m_nSignonState == signon_state_full) {
    if (force_send) {
      spend_shift_tick();
    } else {
      g_state.processing_ticks = std::min(max_processing_ticks(), g_state.processing_ticks + 1);
    }

    if (!force_send && should_recharge() && client_state->chokedcommands == 0) {
      const int ticks_recharged = max_processing_ticks() - g_state.processing_ticks;
      const int recharge_tickbase = adjusted_tick(
        1,
        entity_list != nullptr && entity_list->get_localplayer() != nullptr
          ? entity_list->get_localplayer()->get_tickbase()
          : 0,
        server_tick() + ticks_recharged);
      add_prediction_fix(latest_command_number() + ticks_recharged, recharge_tickbase);
      g_state.recharging = true;
      g_state.send_packet = false;
      return true;
    }

    g_state.recharging = false;

    const int next_command = latest_command_number();
    const shift_mode previous_mode = g_state.mode;
    if (g_state.in_shift_rebuild && force_send) {
      create_shifted_command(next_command);
    } else {
      client->create_move(next_command, interval_per_tick() - accumulated_extra_samples, true);
    }
    const bool started_shift = previous_mode == shift_mode::none && g_state.mode != shift_mode::none;
    auto* created_cmd = input->get_user_cmd(next_command);
    const auto crit_stats = crit_hack::get_stats();

    if (g_state.in_shift_rebuild) {
      update_verified_user_cmd(next_command, created_cmd);
    }

    if (!packet_gate_open) {
      g_state.send_packet = false;
    }

    if (started_shift) {
      spend_shift_tick();
      g_state.send_packet = g_state.processing_ticks <= g_state.shift_goal;
    }

    if (!g_state.in_shift_rebuild
        && !started_shift
        && crit_stats.queue == crit_hack::queue_state::waiting_for_seed
        && crit_hack::wants_queued_force(created_cmd)
        && crit_hack::should_hold_attack(created_cmd)) {
      g_state.send_packet = false;
    } else if (!g_state.in_shift_rebuild
        && crit_stats.queue == crit_hack::queue_state::releasing
        && created_cmd != nullptr
        && is_attack_command(created_cmd)) {
      crit_hack::notify_queued_release(created_cmd->command_number);
      g_state.send_packet = true;
    }

    if (client_state->chokedcommands >= max_choked_commands) {
      g_state.send_packet = true;
    }

    if (g_state.in_shift_rebuild && force_send) {

      set_choked_command();
    } else if (started_shift) {
      set_choked_command();
    } else if (g_state.send_packet) {
      const int command_count = 1 + client_state->chokedcommands;
      if (send_move()) {
        if (!g_state.in_shift_rebuild && g_state.mode == shift_mode::none) {
          g_state.processing_ticks = std::max(0, g_state.processing_ticks - command_count);
        }
      } else {
        g_state.send_packet = false;
        set_choked_command();
      }
    } else {
      set_choked_command();
    }
  }

  if (g_state.in_shift_rebuild && force_send) {
    return true;
  }

  if (!g_state.send_packet) {
    return true;
  }

  channel = client_state != nullptr ? client_state->m_NetChannel : nullptr;
  if (channel == nullptr) {
    return false;
  }

  return flush_packet();
}

void finish_shift(float accumulated_extra_samples)
{
  if (g_state.mode == shift_mode::none || g_state.in_shift_rebuild) {
    return;
  }

  g_state.in_shift_rebuild = true;

  while (g_state.processing_ticks > g_state.shift_goal) {
    const int previous_ticks = g_state.processing_ticks;
    const int next_command = g_state.next_shift_command;
    const int command_offset = std::max(0, next_command - g_state.shift_start_command);
    const bool final_shift_tick = g_state.processing_ticks - 1 <= g_state.shift_goal;

    add_prediction_fix(next_command, g_state.shift_start_tickbase + command_offset);
    run_rebuilt_move(0.0f, final_shift_tick, true);
    ++g_state.next_shift_command;

    if (g_state.processing_ticks >= previous_ticks) {
      break;
    }
  }

  if (g_state.send_packet && client_state != nullptr && client_state->chokedcommands > 0) {
    if (send_move()) {
      flush_packet();
    }
  }

  g_state.in_shift_rebuild = false;
  g_state.recharging = false;
  g_state.should_antiwarp = false;
  g_state.shift_goal = 0;
  g_state.shift_start_command = 0;
  g_state.shift_start_tickbase = 0;
  g_state.next_shift_command = 0;
  g_state.first_shot_command = {};
  g_state.mode = shift_mode::none;

  (void)accumulated_extra_samples;
}

}

void reset()
{
  reset_runtime_state();
}

void initialize_engine_globals(double* net_time, float* host_frametime_unbounded, float* host_frametime_std_deviation,
  host_should_run_fn host_should_run)
{
  g_state.net_time = net_time;
  g_state.host_frametime_unbounded = host_frametime_unbounded;
  g_state.host_frametime_std_deviation = host_frametime_std_deviation;
  g_state.host_should_run = host_should_run;
}

void move(bool final_tick, float accumulated_extra_samples, cl_move_fn original)
{
  synchronize_session();

  if (!should_rebuild_cl_move()) {
    if (original != nullptr) {
      run_network_fix_before_move(final_tick);
      original(final_tick, accumulated_extra_samples);
    }
    return;
  }

  if (!run_rebuilt_move(accumulated_extra_samples, final_tick, false)) {
    if (original != nullptr) {
      original(final_tick, accumulated_extra_samples);
    }
    return;
  }

  finish_shift(accumulated_extra_samples);
}

void on_create_move(user_cmd* cmd)
{
  synchronize_session();

  if (cmd == nullptr || !can_rebuild_packets()) {
    return;
  }

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

}
