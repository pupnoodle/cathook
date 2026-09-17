/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/games/tf2/sdk/interfaces/global_vars.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef GLOBAL_VARS_HPP
#define GLOBAL_VARS_HPP

#include <cmath>

class GlobalVars {
public:
  float realtime;
  int framecount;
  float absolute_frametime;
  float curtime;
  float frametime;
  int max_clients;
  int tickcount;
  float interval_per_tick;
  float interpolation_amount;
  int sim_ticks_this_frame;
  int network_protocol;
  void* save_data;
  bool client;
  int nTimestampNetworkingBase;
  int nTimestampRandomizeWindow;
};

inline static GlobalVars* global_vars;

inline float tick_interval() {
  if (global_vars != nullptr && std::isfinite(global_vars->interval_per_tick) &&
      global_vars->interval_per_tick > 0.0001f) {
    return global_vars->interval_per_tick;
  }
  return 0.015f;
}

inline int time_to_ticks(float seconds) {
  return static_cast<int>(0.5f + seconds / tick_interval());
}

inline float ticks_to_time(int ticks) {
  return static_cast<float>(ticks) * tick_interval();
}

#endif
