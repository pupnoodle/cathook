/*
/^-----^\   data: 2026-05-06
V  o o  V  file: src/features/combat/anti_aim/anti_aim.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef ANTI_AIM_HPP
#define ANTI_AIM_HPP
#include "core/types.hpp"

struct user_cmd;

namespace anti_aim
{

void on_create_move(user_cmd* cmd);
[[nodiscard]] bool should_preserve_shot(user_cmd* cmd);
[[nodiscard]] bool is_active();
[[nodiscard]] bool settings_enabled();
[[nodiscard]] bool has_visual_angles();
[[nodiscard]] Vec3 real_angles();
[[nodiscard]] Vec3 fake_angles();

}
#endif
