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
#include "features/menu/binds.hpp"
#include "features/automation/medic_automation/medic_automation.hpp"
#include "features/combat/anti_aim/anti_aim.hpp"
#include "features/combat/tickbase/tickbase.hpp"
#include "features/visuals/thirdperson.hpp"

void (*client_create_move_original)(void*, int, float, bool);

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

}

void client_create_move_hook(void* me, int sequence_number, float input_sample_frametime, bool active) {
  CATHOOK_HOOK_GUARD();
  g_client_mode_pipeline_ran = false;
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

  const bool client_mode_pipeline_ran = g_client_mode_pipeline_ran;
  g_client_mode_pipeline_ran = false;
  if (!client_mode_pipeline_ran) {
    if (tickbase::should_rebuild_cl_move()) {
      refresh_prediction_state();
    }
    cat_bind::run();
    automation::controller().on_create_move(user_cmd);
    thirdperson::update_taunt_camera();
    run_move_feature_pipeline(user_cmd, entity_list != nullptr ? entity_list->get_localplayer() : nullptr);
  }

  update_verified_user_cmd(sequence_number, user_cmd);

}
