/*
/^-----^\   data: 2026-05-01
V  o o  V  file: src/core/hooks/cl_move.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include <cstdint>
#include <cstdlib>
#include <vector>
#include "core/memory/code_scan.hpp"
#include "core/memory/resolve.hpp"
#include "core/print.hpp"
#include "features/combat/tickbase/tickbase.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"

tickbase::cl_move_fn cl_move_original = nullptr;

using prediction_run_simulation_fn = void (*)(void*, int, user_cmd*, Player*, float);

prediction_run_simulation_fn prediction_run_simulation_original = nullptr;

namespace
{

bool lea_consumed_by(const std::uint8_t* p, const std::uint8_t* end, int reg,
                     std::uint8_t prefix, std::uint8_t opcode2, int span)
{
  for (const std::uint8_t* q = p; q < p + span && q < end; q += puphook::core::memory::insn_length(q, end)) {
    puphook::core::memory::mem_insn insn{};
    if (!puphook::core::memory::decode_mem_insn(q, end, insn)) {
      continue;
    }
    if (insn.opcode == 0x0F && insn.opcode2 == opcode2 && insn.prefix == prefix &&
        insn.base == reg) {
      return true;
    }
  }
  return false;
}

bool resolve_cl_move_globals(void* cl_move, double** net_time, float** unbounded, float** std_deviation)
{
  *net_time = nullptr;
  *unbounded = nullptr;
  *std_deviation = nullptr;
  const auto* begin = static_cast<const std::uint8_t*>(cl_move);
  const auto* end = begin + 0x600;

  struct movss_global
  {
    std::uintptr_t target;
    std::ptrdiff_t position;
  };
  std::vector<movss_global> movss_globals;

  for (const std::uint8_t* p = begin; p < end; p += puphook::core::memory::insn_length(p, end)) {
    puphook::core::memory::mem_insn insn{};
    if (!puphook::core::memory::decode_mem_insn(p, end, insn)) {
      continue;
    }
    if (insn.opcode == 0x8D && puphook::core::memory::is_rip_relative(insn)) {
      if (lea_consumed_by(p + insn.size, end, insn.reg, 0xF2, 0x58, 0x30)) {
        *net_time = reinterpret_cast<double*>(insn.rip_target);
      } else if (lea_consumed_by(p + insn.size, end, insn.reg, 0xF3, 0x10, 0x30)) {
        movss_globals.push_back({insn.rip_target, p - begin});
      }
    }
  }

  for (const auto& first : movss_globals) {
    for (const auto& second : movss_globals) {
      if (first.target + 4 != second.target ||
          std::abs(first.position - second.position) > 0x100) {
        continue;
      }
      *unbounded = reinterpret_cast<float*>(first.target);
      *std_deviation = reinterpret_cast<float*>(second.target);
      return *net_time != nullptr;
    }
  }
  return false;
}

}

void cl_move_hook(bool final_tick, float accumulated_extra_samples)
{
  PUPHOOK_HOOK_GUARD();
  tickbase::move(final_tick, accumulated_extra_samples, cl_move_original);
}

void prediction_run_simulation_hook(void* prediction_instance, int current_command, user_cmd* cmd, Player* localplayer,
  float curtime)
{
  PUPHOOK_HOOK_GUARD();
  if (prediction_run_simulation_original == nullptr) {
    return;
  }

  tickbase::apply_prediction_fix(current_command, localplayer, &curtime);

  int original_tick_count = 0;
  bool restore_tick = false;

  if (cmd != nullptr && cmd->tick_count > 0) {
    const int predicted_tick = static_cast<int>(0.5f + (curtime / tick_interval()));
    if (cmd->tick_count != predicted_tick) {
      original_tick_count = cmd->tick_count;
      cmd->tick_count = predicted_tick;
      restore_tick = true;
    }
  }

  prediction_run_simulation_original(prediction_instance, current_command, cmd, localplayer, curtime);

  if (restore_tick && cmd != nullptr) {
    cmd->tick_count = original_tick_count;
  }
}

void initialize_cl_move_globals(tickbase::host_should_run_fn host_should_run)
{
  if (cl_move_original == nullptr) {
    return;
  }

  double* net_time = nullptr;
  float* host_frametime_unbounded = nullptr;
  float* host_frametime_std_deviation = nullptr;

  if (!resolve_cl_move_globals(reinterpret_cast<void*>(cl_move_original),
                               &net_time, &host_frametime_unbounded, &host_frametime_std_deviation)) {
    print("[tickbase] CL_Move global resolution failed (net_time=%p unbounded=%p stddev=%p); tickbase shifting disabled\n",
      static_cast<void*>(net_time), static_cast<void*>(host_frametime_unbounded), static_cast<void*>(host_frametime_std_deviation));
    return;
  }
  tickbase::initialize_engine_globals(net_time, host_frametime_unbounded, host_frametime_std_deviation, host_should_run);
}
