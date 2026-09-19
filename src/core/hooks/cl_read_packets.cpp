/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/hooks/cl_read_packets.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "cl_read_packets.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"

std::int64_t (*cl_read_packets_original)(char) = nullptr;

namespace
{

struct read_packet_state
{
  float client_state_frame_time = 0.0f;
  float frame_time = 0.0f;
  float current_time = 0.0f;
  int tick_count = 0;
  int delta_tick = 0;
  int last_command_ack = 0;
  int old_tick_count = 0;

  void store()
  {
    if (client_state == nullptr || global_vars == nullptr)
    {
      return;
    }

    client_state_frame_time = client_state->m_frameTime;
    frame_time = global_vars->frametime;
    current_time = global_vars->curtime;
    tick_count = global_vars->tickcount;
    delta_tick = client_state->m_nDeltaTick;
    last_command_ack = client_state->last_command_ack;
    old_tick_count = client_state->oldtickcount;
  }

  void restore() const
  {
    if (client_state == nullptr || global_vars == nullptr)
    {
      return;
    }

    client_state->m_frameTime = client_state_frame_time;
    global_vars->frametime = frame_time;
    global_vars->curtime = current_time;
    global_vars->tickcount = tick_count;
    client_state->m_nDeltaTick = delta_tick;
    client_state->last_command_ack = last_command_ack;
    client_state->oldtickcount = old_tick_count;
  }
};

read_packet_state g_read_packets_state{};
std::int64_t g_read_packets_result = 0;
bool g_has_read_packets_state = false;
constexpr int signon_state_full = 6;
#if defined(PUPHOOK_TEXTMODE) && PUPHOOK_TEXTMODE

constexpr bool textmode_build = true;
#else

constexpr bool textmode_build = false;
#endif

bool should_run_network_fix()
{
  if constexpr (textmode_build)
  {
    return false;
  }

  return config.misc.exploits.network_fix &&
         engine != nullptr &&
         client_state != nullptr &&
         global_vars != nullptr &&
         engine->is_in_game() &&
         client_state->m_nSignonState == signon_state_full &&
         client_state->m_NetChannel != nullptr &&
         !client_state->m_NetChannel->is_loopback();
}

}

void run_network_fix_before_move(bool final_tick)
{
  if (!should_run_network_fix() || cl_read_packets_original == nullptr)
  {
    g_has_read_packets_state = false;
    return;
  }

  read_packet_state backup_state{};
  backup_state.store();
  const std::int64_t result = cl_read_packets_original(final_tick ? 1 : 0);
  if (!should_run_network_fix())
  {
    g_has_read_packets_state = false;
    return;
  }

  g_read_packets_state.store();
  g_read_packets_result = result;
  backup_state.restore();
  g_has_read_packets_state = true;
}

std::int64_t cl_read_packets_hook(char final_tick)
{
  PUPHOOK_HOOK_GUARD();
  if (should_run_network_fix() && g_has_read_packets_state)
  {
    g_read_packets_state.restore();
    g_has_read_packets_state = false;
    return g_read_packets_result;
  }

  return cl_read_packets_original(final_tick);
}

namespace network_fix
{

bool is_active()
{
  return should_run_network_fix() && g_has_read_packets_state;
}

int adjusted_tick_count(int default_tick)
{
  return is_active() ? g_read_packets_state.tick_count : default_tick;
}

float adjusted_curtime(float default_curtime)
{
  return is_active() ? g_read_packets_state.current_time : default_curtime;
}

}
