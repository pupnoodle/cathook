/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/core/hooks/class_menu_show_panel.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "features/menu/config.hpp"
#include "features/automation/misc/misc.hpp"

void (*class_menu_show_panel_original)(void*, bool) = NULL;

void class_menu_show_panel_hook(void* me, bool show) {
  CATHOOK_HOOK_GUARD();
  Player* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr) {
    class_menu_show_panel_original(me, show);
    return;
  }

  const bool dont_join_during_warmup =
      config.misc.automation.auto_class_dont_join_during_warmup && automation::controller().is_setup_time();

  if (config.misc.automation.auto_class_select == true && !dont_join_during_warmup && localplayer->get_tf_class() == tf_class::UNDEFINED) {
    class_menu_show_panel_original(me, false);
  } else {
    class_menu_show_panel_original(me, show);
  }
}
