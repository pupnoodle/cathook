/*
/^-----^\   data: 2026-05-01
V  o o  V  file: src/features/combat/tickbase/tickbase.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef TICKBASE_HPP
#define TICKBASE_HPP
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/entities/player.hpp"

namespace tickbase
{

using cl_move_fn = void (*)(bool, float);
using host_should_run_fn = bool (*)();

struct indicator_state
{
  int processing_ticks = 0;
  int max_processing_ticks = 0;
  int available_shift_ticks = 0;
  int choked_commands = 0;
  bool recharging = false;
  bool shifting = false;
  bool doubletap = false;
  bool warp = false;
  bool fakelag = false;
};

void reset();
void initialize_engine_globals(double* net_time, float* host_frametime_unbounded, float* host_frametime_std_deviation,
  host_should_run_fn host_should_run);
void move(bool final_tick, float accumulated_extra_samples, cl_move_fn original);
void on_create_move(user_cmd* cmd);
void apply_prediction_fix(int command_number, user_cmd* cmd, Player* player, float* curtime);
auto should_rebuild_cl_move() -> bool;
auto should_send_packet() -> bool;
auto get_indicator_state() -> indicator_state;

}
#endif
