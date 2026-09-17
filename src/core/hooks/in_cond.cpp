/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/core/hooks/in_cond.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "features/menu/config.hpp"
#include "core/detach.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/entities/player.hpp"

bool (*in_cond_original)(void* me, int mask) = nullptr;

bool in_cond_hook(void* me, int mask) {
  CATHOOK_HOOK_GUARD();

  auto call_original = [&]() {
    if (in_cond_original != nullptr) {
      return in_cond_original(me, mask);
    }
    return tf_player_shared_in_cond(me, mask);
  };

  if (cathook::core::is_detach_pending()) {
    return call_original();
  }

  const bool spoof_scope = mask == TF_COND_ZOOMED && config.visuals.removals.scope;
  const bool spoof_disguise = config.visuals.removals.disguises &&
    (mask == TF_COND_DISGUISED || mask == TF_COND_DISGUISED_AS_DISPENSER);
  const bool spoof_taunt = config.visuals.removals.taunts && mask == TF_COND_TAUNTING;
  if (!spoof_scope && !spoof_disguise && !spoof_taunt) {
    return call_original();
  }

  if (spoof_scope) {
    return false;
  }

  Player* localplayer = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  if (localplayer != nullptr && me == localplayer->get_shared()) {
    return false;
  }

  return call_original();
}
