/*
/^-----^\   data: 2026-05-07
V  o o  V  file: src/features/automation/medic_automation/medic_automation.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef MEDIC_AUTOMATION_HPP
#define MEDIC_AUTOMATION_HPP
#include "core/types.hpp"

struct user_cmd;
class GameEvent;
class Player;

namespace medic_automation
{

class medic_controller
{
public:
  void on_pre_navbot_create_move();
  void on_post_navbot_create_move(user_cmd* user_cmd);
  void on_game_event(GameEvent* event);

  [[nodiscard]] Player* heal_target() const;
  [[nodiscard]] int heal_target_index() const;
  [[nodiscard]] Vec3 heal_target_position() const;
  [[nodiscard]] bool should_suppress_aimbot() const;
  [[nodiscard]] bool recent_danger_matches(Player* localplayer, Player* patient, int* damage_type_out) const;
  [[nodiscard]] bool can_cycle_resist(float current_time) const;
  [[nodiscard]] bool can_press_uber(float current_time) const;
  void delay_resist_cycle(float next_time);
  void delay_uber(float next_time);

private:
  void clear_runtime_state();

  int heal_target_index_ = 0;
  Vec3 heal_target_position_{};
  bool suppress_aimbot_ = false;
  int recent_damage_target_index_ = 0;
  int recent_damage_type_ = 0;
  float recent_damage_time_ = 0.0f;
  float next_resist_cycle_time_ = 0.0f;
  float next_uber_time_ = 0.0f;
};

medic_controller& controller();

}
#endif
