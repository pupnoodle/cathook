/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/hooks/dispatch_user_message.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "games/tf2/sdk/bitbuf.hpp"
#include "features/automation/misc/misc.hpp"
#include "features/automation/nographics/nographics.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include <cstring>

bool (*dispatch_user_message_original)(void*, int, bf_read*);

namespace
{

constexpr int vgui_menu_user_message_type = 12;
constexpr int shake_user_message_type = 10;
constexpr int fade_user_message_type = 11;
constexpr int rumble_user_message_type = 13;
constexpr int spawn_flying_bird_user_message_type = 52;
constexpr int player_god_ray_effect_user_message_type = 53;
constexpr int player_taunt_sound_loop_start_user_message_type = 70;
constexpr int player_taunt_sound_loop_end_user_message_type = 71;
constexpr int force_player_view_angles_user_message_type = 72;

[[nodiscard]] bool is_vgui_info_menu(const bf_read* message_data)
{
  if (message_data == nullptr || !message_data->is_valid() || message_data->bytes_left() < 4) {
    return false;
  }

  const auto* data = message_data->current_data();
  return data != nullptr && std::memcmp(data, "info", 4) == 0;
}

void close_welcome_menu()
{
  if (engine == nullptr) {
    return;
  }

  engine->client_cmd_unrestricted("closedwelcomemenu");
}

}

bool dispatch_user_message_hook(void* me, int message_type, bf_read* message_data) {
  CATHOOK_HOOK_GUARD();
  automation::controller().on_dispatch_user_message(message_type, message_data);
  if (nographics::is_enabled() &&
      (message_type == shake_user_message_type || message_type == fade_user_message_type ||
       message_type == rumble_user_message_type || message_type == spawn_flying_bird_user_message_type ||
       message_type == player_god_ray_effect_user_message_type ||
       message_type == player_taunt_sound_loop_start_user_message_type ||
       message_type == player_taunt_sound_loop_end_user_message_type)) {
    return true;
  }
  const bool dont_close_motd =
      config.misc.automation.anti_motd_dont_close_during_warmup && automation::controller().is_setup_time();
  if (config.misc.automation.anti_motd && !dont_close_motd && message_type == vgui_menu_user_message_type && is_vgui_info_menu(message_data)) {
    close_welcome_menu();
    return true;
  }

  if (config.visuals.removals.angle_forcing && message_type == force_player_view_angles_user_message_type) {
    return true;
  }

  if (config.visuals.effects.remove_screen_effects &&
      (message_type == shake_user_message_type || message_type == fade_user_message_type ||
       message_type == rumble_user_message_type)) {
    return true;
  }

  if (config.visuals.removals.taunts &&
      (message_type == spawn_flying_bird_user_message_type || message_type == player_god_ray_effect_user_message_type ||
       message_type == player_taunt_sound_loop_start_user_message_type ||
       message_type == player_taunt_sound_loop_end_user_message_type)) {
    return true;
  }

  return dispatch_user_message_original(me, message_type, message_data);
}
