/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/hooks/draw_view_model.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "features/menu/config.hpp"
#include "features/visuals/thirdperson.hpp"

bool (*draw_view_model_original)(void*);

bool draw_view_model_hook(void* me) {
  PUPHOOK_HOOK_GUARD();
  if (thirdperson::should_draw_local_player()) {
    return false;
  }

  if (config.visuals.removals.scope == true)
    return true;
  else
    return draw_view_model_original(me);
}
