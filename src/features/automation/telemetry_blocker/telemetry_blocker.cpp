/*
data: 2026-08-11
file: src/features/automation/telemetry_blocker/telemetry_blocker.cpp
author: HappyKuro
*/
#include "telemetry_blocker.hpp"

#include "core/print.hpp"
#include "core/shared/sigs.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "libsigscan/libsigscan.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace telemetry_blocker
{

namespace
{

enum class convar_kind
{
  integer,
  floating
};

struct blocked_convar
{
  const char* name;
  convar_kind kind;
};

constexpr std::array blocked_convars{
  blocked_convar{ "tf_stats_track", convar_kind::integer },
  blocked_convar{ "steamworks_sessionid_client", convar_kind::integer },
  blocked_convar{ "steamworks_sessionid_server", convar_kind::integer },
  blocked_convar{ "cl_savescreenshotstosteam", convar_kind::integer },
  blocked_convar{ "cl_steamscreenshots", convar_kind::integer },
  blocked_convar{ "replay_enable", convar_kind::integer },
  blocked_convar{ "telemetry_level", convar_kind::integer },
  blocked_convar{ "tf_matchmaking_ogs_odds", convar_kind::floating },
};

struct saved_value
{
  bool found = false;
  int integer = 0;
  float floating = 0.0f;
};

std::array<saved_value, blocked_convars.size()> g_saved{};
bool g_applied = false;

void apply()
{
  if (g_applied || convar_system == nullptr)
  {
    return;
  }

  int applied_count = 0;
  for (std::size_t index = 0; index < blocked_convars.size(); ++index)
  {
    const blocked_convar& entry = blocked_convars[index];
    Convar* convar = convar_system->find_var(entry.name);
    if (convar == nullptr)
    {
      g_saved[index] = {};
      continue;
    }

    saved_value& saved = g_saved[index];
    saved.found = true;
    if (entry.kind == convar_kind::floating)
    {
      saved.floating = convar->get_float();
      convar->set_float(0.0f);
    }
    else
    {
      saved.integer = convar->get_int();
      convar->set_int(0);
    }
    ++applied_count;
  }

  g_applied = true;
  print("[telemetry] blocked %d/%d convars\n", applied_count, static_cast<int>(blocked_convars.size()));
}

void undo()
{
  if (!g_applied)
  {
    return;
  }

  // Cleared even when the convar system has gone away, so a later re-apply captures fresh
  // originals rather than restoring values saved against a dead interface.
  g_applied = false;

  if (convar_system == nullptr)
  {
    g_saved = {};
    return;
  }

  for (std::size_t index = 0; index < blocked_convars.size(); ++index)
  {
    const saved_value& saved = g_saved[index];
    if (!saved.found)
    {
      continue;
    }

    Convar* convar = convar_system->find_var(blocked_convars[index].name);
    if (convar == nullptr)
    {
      continue;
    }

    if (blocked_convars[index].kind == convar_kind::floating)
    {
      convar->set_float(saved.floating);
    }
    else
    {
      convar->set_int(saved.integer);
    }
  }

  g_saved = {};
  print("[telemetry] restored blocked convars\n");
}

} // namespace

constexpr std::ptrdiff_t session_id_offset = 952;
constexpr std::ptrdiff_t upload_gate_offset = 962;

[[nodiscard]] bool blocking()
{
  return config.misc.telemetry_blocker;
}

[[nodiscard]] bool blocking_aggressively()
{
  return config.misc.telemetry_blocker && config.misc.telemetry_blocker_aggressive;
}

void update()
{
  if (config.misc.telemetry_blocker)
  {
    apply();
    return;
  }

  undo();
}

void restore()
{
  undo();
}

void resolve_hooks()
{
  steamworks_gamestats_get_interface_original = reinterpret_cast<void* (*)(void*)>(
    sigscan_module("client.so", sigs::steamworks_gamestats_get_interface));
  steamworks_gamestats_write_perf_data_original = reinterpret_cast<long (*)(void*, void*)>(
    sigscan_module("client.so", sigs::steamworks_gamestats_write_perf_data));
  steamworks_gamestats_submit_row_original = reinterpret_cast<long (*)(void*, void*, char)>(
    sigscan_module("client.so", sigs::steamworks_gamestats_submit_row));
  steamworks_gamestats_drain_rows_original = reinterpret_cast<void (*)(void*)>(
    sigscan_module("client.so", sigs::steamworks_gamestats_drain_rows));
  steamworks_gamestats_end_session_original = reinterpret_cast<long (*)(void*)>(
    sigscan_module("client.so", sigs::steamworks_gamestats_end_session));
  steamworks_gamestats_reset_session_original = reinterpret_cast<void (*)(void*)>(
    sigscan_module("client.so", sigs::steamworks_gamestats_reset_session));

  const int resolved =
    (steamworks_gamestats_get_interface_original != nullptr ? 1 : 0) +
    (steamworks_gamestats_write_perf_data_original != nullptr ? 1 : 0) +
    (steamworks_gamestats_submit_row_original != nullptr ? 1 : 0) +
    (steamworks_gamestats_drain_rows_original != nullptr ? 1 : 0) +
    (steamworks_gamestats_end_session_original != nullptr ? 1 : 0) +
    (steamworks_gamestats_reset_session_original != nullptr ? 1 : 0);

  print("[telemetry] resolved %d/6 gamestats entry points\n", resolved);
}

void clear_hook_pointers()
{
  steamworks_gamestats_get_interface_original = nullptr;
  steamworks_gamestats_write_perf_data_original = nullptr;
  steamworks_gamestats_submit_row_original = nullptr;
  steamworks_gamestats_drain_rows_original = nullptr;
  steamworks_gamestats_end_session_original = nullptr;
  steamworks_gamestats_reset_session_original = nullptr;
}

} // namespace telemetry_blocker

void* (*steamworks_gamestats_get_interface_original)(void*) = nullptr;
long (*steamworks_gamestats_write_perf_data_original)(void*, void*) = nullptr;
long (*steamworks_gamestats_submit_row_original)(void*, void*, char) = nullptr;
void (*steamworks_gamestats_drain_rows_original)(void*) = nullptr;
long (*steamworks_gamestats_end_session_original)(void*) = nullptr;
void (*steamworks_gamestats_reset_session_original)(void*) = nullptr;

// Aggressive only. Everything downstream null-checks the interface and bails, which takes
// out paths that have no hook of their own - but it also silences the DevMsg-worthy
// "interface was not available" case, so it is not the default.
void* steamworks_gamestats_get_interface_hook(void* self)
{
  if (telemetry_blocker::blocking_aggressively())
  {
    return nullptr;
  }

  return steamworks_gamestats_get_interface_original(self);
}

// TF2ClientPerfData - the row carrying CPUID, CPU model and features, core count, GPU
// vendor/device/driver and DxLvl. Refusing to build it is what keeps the hardware
// fingerprint out of OGS.
long steamworks_gamestats_write_perf_data_hook(void* self, void* data)
{
  if (telemetry_blocker::blocking())
  {
    return 0;
  }

  return steamworks_gamestats_write_perf_data_original(self, data);
}

// 2 is the code the original returns for a row it did not accept, so callers already
// handle it.
long steamworks_gamestats_submit_row_hook(void* self, void* row, char send_now)
{
  if (telemetry_blocker::blocking())
  {
    return 2;
  }

  return steamworks_gamestats_submit_row_original(self, row, send_now);
}

// The original walks the queued rows and uploads each one whose gate byte is set, then
// frees them and zeroes the count. Clearing the gate for the duration means the queue is
// still drained and freed - no leak - but nothing goes out.
void steamworks_gamestats_drain_rows_hook(void* self)
{
  if (!telemetry_blocker::blocking() || self == nullptr)
  {
    steamworks_gamestats_drain_rows_original(self);
    return;
  }

  auto* gate = static_cast<unsigned char*>(self) + telemetry_blocker::upload_gate_offset;
  const unsigned char previous = *gate;
  *gate = 0;
  steamworks_gamestats_drain_rows_original(self);
  *gate = previous;
}

// Zeroing the session id first makes the original take its "no session" path, so the
// closing summary row is never built. ResetSession afterwards puts the object back into
// the state the original would have left it in had it run the full path.
long steamworks_gamestats_end_session_hook(void* self)
{
  if (!telemetry_blocker::blocking() || self == nullptr)
  {
    return steamworks_gamestats_end_session_original(self);
  }

  *reinterpret_cast<std::uint64_t*>(static_cast<unsigned char*>(self) + telemetry_blocker::session_id_offset) = 0;
  const long result = steamworks_gamestats_end_session_original(self);
  if (steamworks_gamestats_reset_session_original != nullptr)
  {
    steamworks_gamestats_reset_session_original(self);
  }

  return result;
}
