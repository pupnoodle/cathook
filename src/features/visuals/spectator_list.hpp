/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/visuals/spectator_list.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef SPECTATOR_LIST_HPP
#define SPECTATOR_LIST_HPP
#include <string>
#include <vector>

class Player;

namespace spectator_list
{

struct spectator_entry
{
  std::string name{};
  bool firstperson = false;
};

std::vector<spectator_entry> collect_spectators(Player** target_player_out = nullptr);

}
#endif
