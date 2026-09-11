/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/visuals/spectator_list.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "features/visuals/spectator_list.hpp"
#include <algorithm>
#include <string>
#include <vector>
#include "imgui/imgui.h"
#include "features/menu/config.hpp"
#include "features/menu/menu.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"

namespace spectator_list
{

namespace
{

Player* get_observed_target(Player* localplayer)
{
  if (localplayer == nullptr) {
    return nullptr;
  }

  const observer_mode mode = localplayer->get_observer_mode();
  if (mode == observer_mode::in_eye || mode == observer_mode::chase) {
    auto* target = reinterpret_cast<Player*>(localplayer->get_observer_target());
    if (target != nullptr && target->get_class_id() == class_id::PLAYER) {
      return target;
    }
  }

  return localplayer;
}

}

std::vector<spectator_entry> collect_spectators(Player** target_player_out)
{
  std::vector<spectator_entry> spectators{};
  if (target_player_out != nullptr) {
    *target_player_out = nullptr;
  }

  if (entity_list == nullptr || engine == nullptr) {
    return spectators;
  }

  auto* localplayer = entity_list->get_localplayer();
  auto* target_player = get_observed_target(localplayer);
  if (target_player_out != nullptr) {
    *target_player_out = target_player;
  }
  if (target_player == nullptr) {
    return spectators;
  }

  spectators.reserve(8);
  const int max_entities = entity_list->get_max_entities();
  for (int index = 1; index < max_entities; ++index) {
    auto* player = entity_list->player_from_index(index);
    if (player == nullptr || player == localplayer || player->get_class_id() != class_id::PLAYER) {
      continue;
    }

    const observer_mode mode = player->get_observer_mode();
    if (mode != observer_mode::in_eye && mode != observer_mode::chase) {
      continue;
    }

    if (player->get_observer_target() != target_player) {
      continue;
    }

    player_info info{};
    if (!engine->get_player_info(index, &info) || info.fakeplayer) {
      continue;
    }

    spectators.push_back(spectator_entry{
      .name = info.name,
      .firstperson = mode == observer_mode::in_eye,
    });
  }

  std::ranges::sort(spectators, [](const spectator_entry& left, const spectator_entry& right) {
    if (left.firstperson != right.firstperson) {
      return left.firstperson > right.firstperson;
    }

    return left.name < right.name;
  });
  return spectators;
}

}
