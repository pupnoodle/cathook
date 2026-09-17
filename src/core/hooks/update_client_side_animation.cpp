/*
/^-----^\   data: 2026-08-10
V  o o  V  file: src/core/hooks/update_client_side_animation.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "core/detach.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "features/combat/aimbot/aim_utils.hpp"

void (*update_client_side_animation_original)(void* me) = nullptr;

void update_client_side_animation_hook(void* me) {
  CATHOOK_HOOK_GUARD();
  if (cathook::core::is_detach_pending() ||
      aimbot_anim_detail::manual_update_active ||
      !aimbot_suppress_engine_anim_update(static_cast<Player*>(me))) {
    if (update_client_side_animation_original != nullptr) {
      update_client_side_animation_original(me);
    }
  }
}
