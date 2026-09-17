/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/games/tf2/sdk/entities/sniper_dot.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef SNIPER_DOT_HPP
#define SNIPER_DOT_HPP

#include "entity.hpp"

class SniperDot {
public:
  float get_charge_start_time(void) {
    static tf2_netvars::lazy_offset offset{"DT_SniperDot", {"m_flChargeStartTime"}};
    return offset > 0 ? *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(this) + static_cast<uintptr_t>(offset.operator int())) : 0.0f;
  }
};

#endif
