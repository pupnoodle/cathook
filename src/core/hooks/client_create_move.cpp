/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/core/hooks/client_create_move.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/input.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/prediction.hpp"
#include "games/tf2/sdk/interfaces/steam_friends.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "core/entity_cache.hpp"
#include "core/detach.hpp"
#include "features/misc/removals.hpp"
#include "features/automation/medic_automation/medic_automation.hpp"
#include "features/combat/anti_aim/anti_aim.hpp"
#include "features/combat/tickbase/tickbase.hpp"

void (*client_create_move_original)(void*, int, float, bool);

namespace
{
struct scoped_client_create_move_features {
  scoped_client_create_move_features() {
    g_client_create_move_owns_features = true;
  }

  ~scoped_client_create_move_features() {
    g_client_create_move_owns_features = false;
  }
};

void refresh_prediction_state()
{
  if (prediction == nullptr || client_state == nullptr) {
    return;
  }

  prediction->update(
    client_state->m_nDeltaTick,
    client_state->m_nDeltaTick > 0,
    client_state->last_command_ack,
    client_state->lastoutgoingcommand + client_state->chokedcommands);
}

unsigned int crc32_process_byte(unsigned int crc, unsigned char value)
{
  crc ^= value;
  for (int bit = 0; bit < 8; ++bit) {
    const unsigned int mask = 0U - (crc & 1U);
    crc = (crc >> 1) ^ (0xEDB88320U & mask);
  }

  return crc;
}

unsigned int crc32_process_buffer(unsigned int crc, const void* data, int size)
{
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (int i = 0; i < size; ++i) {
    crc = crc32_process_byte(crc, bytes[i]);
  }

  return crc;
}

unsigned int user_cmd_checksum(const user_cmd& cmd)
{
  unsigned int crc = 0xFFFFFFFFU;
  crc = crc32_process_buffer(crc, &cmd.command_number, sizeof(cmd.command_number));
  crc = crc32_process_buffer(crc, &cmd.tick_count, sizeof(cmd.tick_count));
  crc = crc32_process_buffer(crc, &cmd.view_angles, sizeof(cmd.view_angles));
  crc = crc32_process_buffer(crc, &cmd.forwardmove, sizeof(cmd.forwardmove));
  crc = crc32_process_buffer(crc, &cmd.sidemove, sizeof(cmd.sidemove));
  crc = crc32_process_buffer(crc, &cmd.upmove, sizeof(cmd.upmove));
  crc = crc32_process_buffer(crc, &cmd.buttons, sizeof(cmd.buttons));
  crc = crc32_process_buffer(crc, &cmd.impulse, sizeof(cmd.impulse));
  crc = crc32_process_buffer(crc, &cmd.weapon_select, sizeof(cmd.weapon_select));
  crc = crc32_process_buffer(crc, &cmd.weapon_subtype, sizeof(cmd.weapon_subtype));
  crc = crc32_process_buffer(crc, &cmd.random_seed, sizeof(cmd.random_seed));
  crc = crc32_process_buffer(crc, &cmd.mouse_dx, sizeof(cmd.mouse_dx));
  crc = crc32_process_buffer(crc, &cmd.mouse_dy, sizeof(cmd.mouse_dy));
  return crc ^ 0xFFFFFFFFU;
}

void update_verified_user_cmd(int sequence_number, user_cmd* cmd)
{
  if (input == nullptr || cmd == nullptr) {
    return;
  }

  auto* verified_cmd = input->get_verified_user_cmd(sequence_number);
  if (verified_cmd == nullptr) {
    return;
  }

  verified_cmd->cmd = *cmd;
  verified_cmd->crc = user_cmd_checksum(*cmd);
}

}

void client_create_move_hook(void* me, int sequence_number, float input_sample_frametime, bool active) {
  CATHOOK_HOOK_GUARD();
  {
    scoped_client_create_move_features feature_owner{};
    client_create_move_original(me, sequence_number, input_sample_frametime, active);
  }

  if (cathook::core::is_detach_pending()) {
    cathook::core::service_detach_request();
    return;
  }

  if (input == nullptr) {
    return;
  }

  auto* user_cmd = input->get_user_cmd(sequence_number);
  if (user_cmd == nullptr) {
    return;
  }

  refresh_prediction_state();
  cat_bind::run();
  automation::controller().on_create_move(user_cmd);
  removals::on_create_move();

  move_features_result move_result{};
  bool taunt_slide = false;
  if (can_run_move_features(user_cmd)) {
    Player* localplayer = entity_list->get_localplayer();
    taunt_slide = should_run_taunt_slide(localplayer);
    if (taunt_slide) {
      user_cmd->buttons &= ~(IN_ATTACK | IN_ATTACK2 | IN_ATTACK3);
      apply_taunt_slide(localplayer, user_cmd);
    } else {
      update_player_head_emoji_cache();
      move_result = run_move_features(user_cmd);
    }
  }

  if (!taunt_slide) {
    tickbase::on_create_move(user_cmd);
    anti_aim::on_create_move(user_cmd);
    if (!move_result.use_psilent) {
      navbot::controller().apply_post_anti_aim(user_cmd);
    }
  }
  aimbot::update_local_client_side_animation();
  update_verified_user_cmd(sequence_number, user_cmd);

}
