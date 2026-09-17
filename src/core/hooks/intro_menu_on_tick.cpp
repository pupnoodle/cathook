/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/core/hooks/intro_menu_on_tick.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "features/automation/misc/misc.hpp"

void (*intro_menu_on_tick_original)(void*) = NULL;

void intro_menu_on_tick_hook(void* me) {
  CATHOOK_HOOK_GUARD();
  intro_menu_on_tick_original(me);
  automation::controller().on_menu_tick();
}
