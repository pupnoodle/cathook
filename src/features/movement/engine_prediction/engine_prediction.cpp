/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/movement/engine_prediction/engine_prediction.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include <array>
#include <climits>
#include <cmath>
#include "MD5/MD5.hpp"
#include "engine_prediction.hpp"
#include "core/random_seed.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/game_movement.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/move_helper.hpp"
#include "games/tf2/sdk/interfaces/prediction.hpp"

namespace
{

struct engine_prediction_player_snapshot {
  Vec3 origin{};
  Vec3 abs_origin{};
  Vec3 velocity{};
  Vec3 base_velocity{};
  Vec3 view_offset{};
  int flags = 0;
  int ground_entity_handle = 0;
  int buttons = 0;
  int last_buttons = 0;
  int tickbase = 0;
  user_cmd* current_cmd = nullptr;
  bool ducked = false;
  bool ducking = false;
  bool in_duck_jump = false;
  float duck_time = 0.0f;
  float duck_jump_time = 0.0f;
  float fall_velocity = 0.0f;
};

struct engine_prediction_global_snapshot {
  float curtime = 0.0f;
  float frametime = 0.0f;
  int tickcount = 0;
  bool first_time_predicted = false;
  bool in_prediction = false;
  int random_seed_value = -1;
};

struct engine_prediction_weapon_snapshot {
  bool valid = false;
  float crit_token_bucket = 0.0f;
  int crit_checks = 0;
  int crit_seed_requests = 0;
  float crit_time = 0.0f;
  int current_seed = 0;
  int last_crit_check_frame = 0;
  float last_crit_check_time = 0.0f;
  float last_rapid_fire_crit_check_time = 0.0f;
  bool current_attack_is_crit = false;
  bool current_crit_is_random = false;
  bool current_attack_is_during_demo_charge = false;
};

struct engine_prediction_state {
  engine_prediction_player_snapshot player{};
  engine_prediction_global_snapshot globals{};
  std::array<engine_prediction_weapon_snapshot, Player::max_weapon_count> weapons{};
  bool active = false;
};

engine_prediction_state prediction_state{};

int engine_prediction_tickbase(user_cmd* current_user_cmd, Player* localplayer) {
  static int predicted_tickbase = 0;
  static int last_command_number = 0;
  static bool last_command_was_predicted = false;

  if (current_user_cmd == nullptr || localplayer == nullptr) {
    return predicted_tickbase;
  }

  if (last_command_number <= 0 || last_command_was_predicted) {
    predicted_tickbase = localplayer->get_tickbase();
  } else {
    ++predicted_tickbase;
  }

  last_command_number = current_user_cmd->command_number;
  last_command_was_predicted = current_user_cmd->has_been_predicted;
  return predicted_tickbase;
}

bool engine_prediction_capture(Player* localplayer) {
  if (localplayer == nullptr || global_vars == nullptr || prediction == nullptr) {
    return false;
  }

  prediction_state.player.origin = localplayer->get_origin();
  prediction_state.player.abs_origin = localplayer->get_abs_origin();
  prediction_state.player.velocity = localplayer->get_velocity();
  prediction_state.player.base_velocity = localplayer->get_base_velocity();
  prediction_state.player.view_offset = localplayer->get_view_offset();
  prediction_state.player.flags = localplayer->get_flags();
  prediction_state.player.ground_entity_handle = localplayer->get_ground_entity_handle();
  prediction_state.player.buttons = localplayer->get_buttons();
  prediction_state.player.last_buttons = localplayer->get_last_buttons();
  prediction_state.player.tickbase = localplayer->get_tickbase();
  prediction_state.player.current_cmd = localplayer->get_current_cmd();
  prediction_state.player.ducked = localplayer->get_ducked();
  prediction_state.player.ducking = localplayer->get_ducking_state();
  prediction_state.player.in_duck_jump = localplayer->get_in_duck_jump();
  prediction_state.player.duck_time = localplayer->get_duck_time();
  prediction_state.player.duck_jump_time = localplayer->get_duck_jump_time();
  prediction_state.player.fall_velocity = localplayer->get_fall_velocity();

  prediction_state.globals.curtime = global_vars->curtime;
  prediction_state.globals.frametime = global_vars->frametime;
  prediction_state.globals.tickcount = global_vars->tickcount;
  prediction_state.globals.first_time_predicted = prediction->first_time_predicted;
  prediction_state.globals.in_prediction = prediction->in_prediction;
  prediction_state.globals.random_seed_value = random_seed != nullptr ? static_cast<int>(*random_seed) : -1;

  for (int slot = 0; slot < Player::max_weapon_count; ++slot) {
    auto& state = prediction_state.weapons[slot];
    state = {};

    auto* weapon = localplayer->get_weapon_at(slot);
    if (weapon == nullptr) {
      continue;
    }

    state.valid = true;
    state.crit_token_bucket = weapon->crit_token_bucket();
    state.crit_checks = weapon->crit_checks();
    state.crit_seed_requests = weapon->crit_seed_requests();
    state.crit_time = weapon->crit_time();
    state.current_seed = weapon->current_seed();
    state.last_crit_check_frame = weapon->last_crit_check_frame();
    state.last_crit_check_time = weapon->last_crit_check_time();
    state.last_rapid_fire_crit_check_time = weapon->last_rapid_fire_crit_check_time();
    state.current_attack_is_crit = weapon->current_attack_is_crit();
    state.current_crit_is_random = weapon->current_crit_is_random();
    state.current_attack_is_during_demo_charge = weapon->current_attack_is_during_demo_charge();
  }

  return true;
}

void engine_prediction_restore(Player* localplayer) {
  if (localplayer == nullptr || global_vars == nullptr || prediction == nullptr) {
    prediction_state.active = false;
    return;
  }

  localplayer->set_origin(prediction_state.player.origin);
  localplayer->set_abs_origin(prediction_state.player.abs_origin);
  localplayer->set_velocity(prediction_state.player.velocity);
  localplayer->set_base_velocity(prediction_state.player.base_velocity);
  localplayer->set_view_offset(prediction_state.player.view_offset);
  localplayer->set_flags(prediction_state.player.flags);
  localplayer->set_ground_entity_handle(prediction_state.player.ground_entity_handle);
  localplayer->set_buttons(prediction_state.player.buttons);
  localplayer->set_last_buttons(prediction_state.player.last_buttons);
  localplayer->set_tickbase(prediction_state.player.tickbase);
  localplayer->set_current_cmd(prediction_state.player.current_cmd);
  localplayer->set_ducked(prediction_state.player.ducked);
  localplayer->set_ducking_state(prediction_state.player.ducking);
  localplayer->set_in_duck_jump(prediction_state.player.in_duck_jump);
  localplayer->set_duck_time(prediction_state.player.duck_time);
  localplayer->set_duck_jump_time(prediction_state.player.duck_jump_time);
  localplayer->set_fall_velocity(prediction_state.player.fall_velocity);

  global_vars->curtime = prediction_state.globals.curtime;
  global_vars->frametime = prediction_state.globals.frametime;
  global_vars->tickcount = prediction_state.globals.tickcount;
  prediction->first_time_predicted = prediction_state.globals.first_time_predicted;
  prediction->in_prediction = prediction_state.globals.in_prediction;
  if (random_seed != nullptr) {
    *random_seed = prediction_state.globals.random_seed_value;
  }

  for (int slot = 0; slot < Player::max_weapon_count; ++slot) {
    const auto& state = prediction_state.weapons[slot];
    if (!state.valid) {
      continue;
    }

    auto* weapon = localplayer->get_weapon_at(slot);
    if (weapon == nullptr) {
      continue;
    }

    weapon->crit_token_bucket() = state.crit_token_bucket;
    weapon->crit_checks() = state.crit_checks;
    weapon->crit_seed_requests() = state.crit_seed_requests;
    weapon->crit_time() = state.crit_time;
    weapon->current_seed() = state.current_seed;
    weapon->last_crit_check_frame() = state.last_crit_check_frame;
    weapon->last_crit_check_time() = state.last_crit_check_time;
    weapon->last_rapid_fire_crit_check_time() = state.last_rapid_fire_crit_check_time;
    weapon->current_attack_is_crit() = state.current_attack_is_crit;
    weapon->current_crit_is_random() = state.current_crit_is_random;
    weapon->current_attack_is_during_demo_charge() = state.current_attack_is_during_demo_charge;
  }

  prediction_state.active = false;
}

}

void start_engine_prediction(user_cmd* user_cmd) {
  if (prediction_state.active || user_cmd == nullptr || prediction == nullptr || game_movement == nullptr ||
      move_helper == nullptr || global_vars == nullptr) {
    return;
  }

  Player* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive()) {
    return;
  }

  if (!engine_prediction_capture(localplayer)) {
    return;
  }

  prediction_state.active = true;

  struct user_cmd predicted_command = *user_cmd;
  predicted_command.buttons &= ~(IN_ATTACK | IN_ATTACK2 | IN_ATTACK3);
  predicted_command.impulse = 0;
  predicted_command.weapon_select = 0;
  predicted_command.weapon_subtype = 0;
  predicted_command.has_been_predicted = false;

  int predicted_tickbase = engine_prediction_tickbase(&predicted_command, localplayer);
  localplayer->set_tickbase(predicted_tickbase);
  localplayer->set_current_cmd(&predicted_command);

  if (random_seed != nullptr) {
    *random_seed = MD5_PseudoRandom(static_cast<unsigned int>(predicted_command.command_number)) & INT_MAX;
  }

  float interval_per_tick = global_vars->interval_per_tick > 0.0f ? global_vars->interval_per_tick : TICK_INTERVAL;
  global_vars->curtime = static_cast<float>(predicted_tickbase) * interval_per_tick;
  global_vars->frametime = prediction->engine_paused ? 0.0f : interval_per_tick;
  global_vars->tickcount = predicted_tickbase;

  prediction->first_time_predicted = false;
  prediction->in_prediction = true;
  prediction->set_local_view_angles(predicted_command.view_angles);

  move_helper->set_host(localplayer);
  prediction->run_command(localplayer, &predicted_command, move_helper);
  move_helper->set_host(nullptr);
}

void end_engine_prediction() {
  if (!prediction_state.active) {
    return;
  }

  Player* localplayer = entity_list->get_localplayer();
  engine_prediction_restore(localplayer);
}
