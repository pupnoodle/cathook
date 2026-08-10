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
#include "features/combat/tickbase/tickbase.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"

tickbase::cl_move_fn cl_move_original = nullptr;

using prediction_run_simulation_fn = void (*)(void*, int, user_cmd*, Player*, float);

prediction_run_simulation_fn prediction_run_simulation_original = nullptr;

namespace
{

auto resolve_rip_relative(std::uint8_t* instruction, int displacement_offset, int instruction_size) -> std::uint8_t*
{
  const auto displacement = *reinterpret_cast<std::int32_t*>(instruction + displacement_offset);
  return instruction + instruction_size + displacement;
}

auto resolve_cl_move_lea(void* cl_move, int offset) -> std::uint8_t*
{
  auto* instruction = reinterpret_cast<std::uint8_t*>(cl_move) + offset;
  if (instruction[0] != 0x48 || instruction[1] != 0x8D || instruction[2] != 0x05) {
    return nullptr;
  }

  return resolve_rip_relative(instruction, 3, 7);
}

}

void cl_move_hook(bool final_tick, float accumulated_extra_samples)
{
  CATHOOK_HOOK_GUARD();
  tickbase::move(final_tick, accumulated_extra_samples, cl_move_original);
}

void prediction_run_simulation_hook(void* prediction_instance, int current_command, user_cmd* cmd, Player* localplayer,
  float curtime)
{
  CATHOOK_HOOK_GUARD();
  tickbase::apply_prediction_fix(current_command, cmd, localplayer, &curtime);

  int original_tick_count = 0;
  bool restore_tick = false;

  if (cmd != nullptr && cmd->tick_count > 0) {
    const float interval = global_vars != nullptr && global_vars->interval_per_tick > 0.0f
      ? global_vars->interval_per_tick
      : static_cast<float>(TICK_INTERVAL);
    const int predicted_tick = static_cast<int>(0.5f + (curtime / std::max(interval, 0.0001f)));
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

  constexpr int net_time_lea_offset = 0x16f;
  constexpr int host_frametime_unbounded_lea_offset = 0x45d;
  constexpr int host_frametime_std_deviation_lea_offset = 0x48d;

  auto* net_time = reinterpret_cast<double*>(resolve_cl_move_lea(reinterpret_cast<void*>(cl_move_original), net_time_lea_offset));
  auto* host_frametime_unbounded =
    reinterpret_cast<float*>(resolve_cl_move_lea(reinterpret_cast<void*>(cl_move_original), host_frametime_unbounded_lea_offset));
  auto* host_frametime_std_deviation =
    reinterpret_cast<float*>(resolve_cl_move_lea(reinterpret_cast<void*>(cl_move_original), host_frametime_std_deviation_lea_offset));

  tickbase::initialize_engine_globals(net_time, host_frametime_unbounded, host_frametime_std_deviation, host_should_run);
}
