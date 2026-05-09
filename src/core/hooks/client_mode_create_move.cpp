/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/hooks/client_mode_create_move.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/prediction.hpp"

#include "features/menu/binds.hpp"

#include "games/tf2/sdk/entities/player.hpp"

#include "imgui/dearimgui.hpp"

#include "features/menu/config.hpp"

#include "features/combat/aimbot/aimbot.cpp"
#include "features/combat/random_crits/random_crits.cpp"
#include "features/movement/bhop/bhop.cpp"
#include "features/movement/engine_prediction/engine_prediction.cpp"
#include "features/automation/medic_automation/medic_automation.hpp"
#include "features/automation/misc/misc.hpp"
#include "features/automation/navbot/navbot_controller.hpp"
#include "features/visuals/esp/esp.hpp"
#include "core/detach.hpp"

bool (*client_mode_create_move_original)(void*, float, user_cmd*);
bool g_client_create_move_owns_features = false;

static void movement_fix(user_cmd* user_cmd, Vec3 original_view_angle, float original_forward_move, float original_side_move) {
  float yaw_delta = user_cmd->view_angles.y - original_view_angle.y;
  float original_yaw_correction = 0;
  float current_yaw_correction = 0;

  if (original_view_angle.y < 0.0f) {
    original_yaw_correction = 360.0f + original_view_angle.y;
  } else {
    original_yaw_correction = original_view_angle.y;
  }
    
  if (user_cmd->view_angles.y < 0.0f) {
    current_yaw_correction = 360.0f + user_cmd->view_angles.y;
  } else {
    current_yaw_correction = user_cmd->view_angles.y;
  }

  if (current_yaw_correction < original_yaw_correction) {
    yaw_delta = abs(current_yaw_correction - original_yaw_correction);
  } else {
    yaw_delta = 360.0f - abs(original_yaw_correction - current_yaw_correction);
  }
  yaw_delta = 360.0f - yaw_delta;

  user_cmd->forwardmove = cos((yaw_delta) * (M_PI/180)) * original_forward_move + cos((yaw_delta + 90.f) * (M_PI/180)) * original_side_move;
  user_cmd->sidemove = sin((yaw_delta) * (M_PI/180)) * original_forward_move + sin((yaw_delta + 90.f) * (M_PI/180)) * original_side_move;
}

static bool should_block_menu_movement() {
  return ImGui::IsImGuiFullyInitialized() &&
         ImGui::IsAnyItemActive() &&
         !ImGui::IsMouseDown(ImGuiMouseButton_Left);
}

static bool can_run_move_features(user_cmd* user_cmd) {
  if (user_cmd == nullptr || user_cmd->command_number == 0 || user_cmd->tick_count <= 0) {
    return false;
  }

  if (!engine->is_in_game()) {
    return false;
  }

  return entity_list->get_localplayer() != nullptr;
}

static bool aimbot_should_clear_autoreload() {
  if (!config.aimbot.master) {
    return false;
  }

  if (config.aimbot.key.button != SDLK_UNKNOWN && !is_button_active(config.aimbot.key)) {
    return false;
  }

  return target_entity != nullptr || has_aimbot_preference(aimbot_preference.preferred_target);
}

// really stupid but i guess it works :thumbsup: - pupnoodle
static void force_aimbot_autoreload_convar() {
  if (!config.aimbot.master || convar_system == nullptr) {
    return;
  }

  static Convar* cl_autoreload = nullptr;
  if (cl_autoreload == nullptr) {
    cl_autoreload = convar_system->find_var("cl_autoreload");
  }

  if (cl_autoreload != nullptr && cl_autoreload->get_int() != 0) {
    cl_autoreload->set_int(0);
  }
}

static bool should_run_taunt_slide(Player* localplayer) {
  if (!config.misc.movement.taunt_slide || localplayer == nullptr || !localplayer->is_alive()) {
    return false;
  }

  return localplayer->in_cond(TF_COND_TAUNTING) && localplayer->allow_move_during_taunt();
}

static bool run_move_features(user_cmd* user_cmd) {
  const Vec3 original_view_angles = user_cmd->view_angles;

  force_aimbot_autoreload_convar();

  medic_automation::controller().on_pre_navbot_create_move(user_cmd);
  bhop(user_cmd);
  navbot::controller().on_create_move(user_cmd);
  medic_automation::controller().on_post_navbot_create_move(user_cmd);
  const bool suppress_aimbot_for_reload = navbot::controller().should_suppress_aimbot();
  const bool suppress_aimbot_for_medic = medic_automation::controller().should_suppress_aimbot();
  const bool suppress_aimbot = suppress_aimbot_for_reload || suppress_aimbot_for_medic;

  const bool menu_movement_blocked = should_block_menu_movement();
  if (menu_movement_blocked) {
    user_cmd->sidemove = 0.0f;
    user_cmd->forwardmove = 0.0f;
  }

  const float corrected_side_move = user_cmd->sidemove;
  const float corrected_forward_move = user_cmd->forwardmove;

  if (suppress_aimbot_for_reload) {
    target_player = nullptr;
    target_entity = nullptr;
    clear_aimbot_preference();
    reset_autoscope_scope_state();
    reset_aimbot_input_history();
    user_cmd->buttons &= ~(IN_ATTACK | IN_ATTACK2 | IN_ATTACK3);
    user_cmd->buttons |= IN_RELOAD;
  } else if (suppress_aimbot_for_medic) {
    target_player = nullptr;
    target_entity = nullptr;
    clear_aimbot_preference();
    reset_autoscope_scope_state();
    reset_aimbot_input_history();
  } else if (aimbot_should_clear_autoreload()) {
    user_cmd->buttons &= ~IN_RELOAD;
  }

  start_engine_prediction(user_cmd);
  const bool use_psilent = suppress_aimbot ? false : aimbot(user_cmd, original_view_angles);
  movement_fix(user_cmd, original_view_angles, corrected_forward_move, corrected_side_move);
  if (!menu_movement_blocked && !suppress_aimbot && !navbot::controller().should_prioritize_danger_movement()) {
    aimbot_apply_walk_to_target(entity_list->get_localplayer(), user_cmd);
  }

  end_engine_prediction();

  return use_psilent;
}

// Called approx every frame.
// Only valid user commands are sent approx 66 times a second
bool client_mode_create_move_hook(void* me, float sample_time, user_cmd* user_cmd) {
  if (cathook::core::is_detach_pending()) {
    const bool rc = client_mode_create_move_original(me, sample_time, user_cmd);
    cathook::core::service_detach_request();
    return rc;
  }

  if (g_client_create_move_owns_features) {
    if (can_run_move_features(user_cmd)) {
      Player* localplayer = entity_list->get_localplayer();
      if (should_run_taunt_slide(localplayer)) {
        return false;
      }
    }

    return client_mode_create_move_original(me, sample_time, user_cmd);
  }

  cat_bind::run();
  automation::controller().on_create_move(user_cmd);

  const bool can_run_features = can_run_move_features(user_cmd);
  Player* localplayer = can_run_features ? entity_list->get_localplayer() : nullptr;
  if (should_run_taunt_slide(localplayer)) {
    return false;
  }

  const bool rc = client_mode_create_move_original(me, sample_time, user_cmd);

  if (!can_run_features) {
    return rc;
  }

  update_player_head_emoji_cache();

  const bool use_psilent = run_move_features(user_cmd);
  if (use_psilent) {
    return false;
  }

  return rc;
}
