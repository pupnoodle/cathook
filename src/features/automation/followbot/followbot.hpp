/*
/^-----^\   data: 2026-08-10
V  o o  V  file: src/features/automation/followbot/followbot.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#pragma once

struct user_cmd;
class Player;

namespace followbot
{

class controller_t
{
public:
  void on_create_move(user_cmd* user_cmd);
  void shutdown();

  [[nodiscard]] bool is_active() const;
  [[nodiscard]] bool wants_nav() const;
  [[nodiscard]] bool get_nav_target(Vec3* origin, int* entity_index) const;
  [[nodiscard]] std::uint32_t target_account_id() const;

  void set_ipc_target(std::uint32_t account_id);

private:
  struct impl;
  impl* state_ = nullptr;
};

controller_t& controller();

}
