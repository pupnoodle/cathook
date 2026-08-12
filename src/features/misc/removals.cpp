/*
data: 2026-08-12
file: src/features/misc/removals.cpp
author: HappyKuro
*/
#include "features/misc/removals.hpp"

#include <cstring>

#include "features/menu/config.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"

namespace removals
{

namespace
{

[[nodiscard]] bool any_condition_removal_enabled()
{
  const auto& settings = config.visuals.removals;
  return settings.cloak || settings.disguise || settings.taunts || settings.contracker;
}

void apply_to(Player* player)
{
  const auto& settings = config.visuals.removals;

  if (settings.cloak) {
    player->clear_condition(TF_COND_STEALTHED);
    player->clear_condition(TF_COND_STEALTHED_BLINK);
  }

  if (settings.disguise) {
    player->clear_condition(TF_COND_DISGUISED);
    player->clear_condition(TF_COND_DISGUISING);
  }

  if (settings.taunts) {
    player->clear_condition(TF_COND_TAUNTING);
  }

  if (settings.contracker) {
    if (bool* viewing = player->viewing_contracker(); viewing != nullptr) {
      *viewing = false;
    }
  }
}

}

void on_create_move()
{
  if (!any_condition_removal_enabled()) {
    return;
  }

  if (entity_list == nullptr || global_vars == nullptr) {
    return;
  }

  auto* localplayer = entity_list->get_localplayer();

  for (int index = 1; index <= global_vars->max_clients; ++index) {
    auto* player = entity_list->player_from_index(static_cast<unsigned int>(index));
    if (player == nullptr || player == localplayer) {
      continue;
    }

    if (player->is_dormant() || !player->is_alive()) {
      continue;
    }

    apply_to(player);
  }
}

bool should_skip_model(const char* model_name)
{
  if (model_name == nullptr) {
    return false;
  }

  const auto& settings = config.visuals.removals;

  if (settings.arms) {
    const bool is_arm_model =
      (std::strstr(model_name, "arms") != nullptr && std::strstr(model_name, "yeti") == nullptr)
      || std::strstr(model_name, "c_engineer_gunslinger") != nullptr;

    if (is_arm_model) {
      return true;
    }
  }

  if (settings.hats) {
    if (std::strstr(model_name, "player/items") != nullptr) {
      return true;
    }
  }

  return false;
}

}
