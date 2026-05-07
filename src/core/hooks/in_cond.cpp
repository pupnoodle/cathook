/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/core/hooks/in_cond.cpp
 |  Y  |   autor: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "features/menu/config.hpp"

bool in_cond_hook(void* me, int mask) {

  if (mask == TF_COND_ZOOMED && config.visuals.removals.scope == true) { //if the player is scoped
    return false;
  }

  if (in_cond_original == nullptr) {
    return false;
  }

  bool re = in_cond_original(me, mask);
  
  return re;
}
