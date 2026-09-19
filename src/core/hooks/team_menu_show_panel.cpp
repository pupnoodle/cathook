/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/core/hooks/team_menu_show_panel.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "features/menu/config.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"

void (*team_menu_show_panel_original)(void*, bool) = NULL;

void team_menu_show_panel_hook(void* me, bool show) {
  PUPHOOK_HOOK_GUARD();
  Player* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr) {
    team_menu_show_panel_original(me, show);
    return;
  }

  if (config.misc.automation.auto_class_select == true && localplayer->get_team() == tf_team::UNKNOWN) {
    team_menu_show_panel_original(me, false);
  } else {
    team_menu_show_panel_original(me, show);
  }
}
