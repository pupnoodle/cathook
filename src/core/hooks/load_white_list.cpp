/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/hooks/load_white_list.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "features/menu/config.hpp"

void* (*load_white_list_original)(void*);

void* load_white_list_hook(void* me) {
  PUPHOOK_HOOK_GUARD();
  if (config.misc.exploits.bypasspure || config.misc.exploits.pure_bypass) {
    return nullptr;
  }

  return load_white_list_original(me);
}
