/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/games/tf2/sdk/entities/capture_flag.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef CAPTURE_FLAG_HPP
#define CAPTURE_FLAG_HPP

#include "entity.hpp"

enum flag_status {
  HOME = 0,
  STOLEN,
  DROPPED
};

class CaptureFlag : public Entity {
public:
  enum flag_status get_status(void) {
    static tf2_netvars::lazy_offset netvar_offset{"DT_CaptureFlag", {"m_nFlagStatus"}};
    const int offset = netvar_offset;
    if (offset <= 0) return flag_status::HOME;
    return static_cast<flag_status>(*reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + static_cast<uintptr_t>(offset)));
  }

};

#endif
