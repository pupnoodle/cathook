/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/hooks/frame_stage_notify.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/steam_friends.hpp"
#include <string>
#include <cstring>
#include <cmath>
#include <utility>
#include "games/tf2/sdk/entities/player.hpp"
#include "core/entity_cache.hpp"
#include "core/commands.hpp"
#include "core/detach.hpp"
#include "core/identify/identify.hpp"
#include "core/ipc/ipc_client.hpp"
#include "core/player_manager.hpp"
#include "features/menu/config.hpp"
#include "features/combat/aimbot/aimbot.hpp"
#include "features/combat/aimbot/resolver.hpp"
#include "features/combat/backtrack/backtrack.hpp"
#include "features/automation/navbot/navbot_controller.hpp"
#include "features/visuals/thirdperson.hpp"
#include "features/visuals/skybox_changer.hpp"
#include "features/visuals/groups/visual_groups.hpp"
#include "core/print.hpp"

enum ClientFrameStage {
  FRAME_UNDEFINED = -1,
  FRAME_START,
  FRAME_NET_UPDATE_START,
  FRAME_NET_UPDATE_POSTDATAUPDATE_START,
  FRAME_NET_UPDATE_POSTDATAUPDATE_END,
  FRAME_NET_UPDATE_END,
  FRAME_RENDER_START,
  FRAME_RENDER_END
};

void (*frame_stage_notify_original)(void*, ClientFrameStage);

static float last_time = 0.0;
static unsigned int a = 0;

namespace
{

void update_zoom_sensitivity()
{
  static Convar* zoom_sensitivity_ratio = nullptr;
  static float user_zoom_sensitivity_ratio = 1.0f;
  static bool ratio_overridden = false;

  if (zoom_sensitivity_ratio == nullptr && convar_system != nullptr) {
    zoom_sensitivity_ratio = convar_system->find_var("zoom_sensitivity_ratio");
  }

  if (zoom_sensitivity_ratio == nullptr) {
    return;
  }

  Player* localplayer = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  const int player_fov = localplayer != nullptr ? localplayer->get_fov() : 0;
  const int default_fov = localplayer != nullptr ? localplayer->get_default_fov() : 0;
  const bool zoomed = player_fov > 1 && default_fov > 1 && player_fov < default_fov;
  const bool flat_sensitivity = zoomed &&
      (config.visuals.removals.scope || config.visuals.removals.zoom || config.visuals.flat_zoom_sensitivity);

  if (flat_sensitivity) {
    if (!ratio_overridden) {
      user_zoom_sensitivity_ratio = zoom_sensitivity_ratio->get_float();
      ratio_overridden = true;
    }

    const float flat_ratio = static_cast<float>(default_fov) / static_cast<float>(player_fov);
    if (std::isfinite(flat_ratio) && flat_ratio > 0.0f) {
      zoom_sensitivity_ratio->set_float(flat_ratio);
    }
  } else if (ratio_overridden) {
    zoom_sensitivity_ratio->set_float(user_zoom_sensitivity_ratio);
    ratio_overridden = false;
  }
}

std::string last_match_exec_level{};
void run_match_exec_on_level_change()
{
  if (engine == nullptr || !engine->is_in_game()) {
    last_match_exec_level.clear();
    return;
  }

  const char* level_name = engine->get_level_name();
  if (level_name == nullptr || level_name[0] == '\0') {
    return;
  }

  if (last_match_exec_level == level_name) {
    return;
  }

  last_match_exec_level = level_name;
  cathook::core::execute_cfg_file("cat_matchexec", "cat_matchexec");
}

void run_skybox_changer()
{
  static std::string last_level{};

  if (engine == nullptr || !engine->is_in_game()) {
    last_level.clear();
    skybox_changer::invalidate();
    return;
  }

  const char* level_name = engine->get_level_name();
  if (level_name == nullptr || level_name[0] == '\0') {
    return;
  }

  if (last_level != level_name) {
    last_level = level_name;
    skybox_changer::invalidate();
  }

  skybox_changer::update();
}

}

void frame_stage_notify_hook(void* me, ClientFrameStage current_stage) {
  CATHOOK_HOOK_GUARD();
  if (cathook::core::is_detach_pending()) {
    thirdperson::end_render_angles();
    cathook::core::service_detach_request();
    return;
  }

  if (current_stage == FRAME_RENDER_START) {
    thirdperson::begin_render_angles();
    update_zoom_sensitivity();
  }

  frame_stage_notify_original(me, current_stage);

  // Inventory changer frame-stage handler temporarily disabled.

  if (current_stage == FRAME_RENDER_START) {
    aimbot_note_render_clock();
    entity_visuals::on_render_start();
  }

  if (last_time == 0.0) {
    last_time = global_vars->curtime;
  }

  switch (current_stage) {
  case FRAME_NET_UPDATE_START:
    {

      if (global_vars->curtime - last_time >= 1) {
        friend_cache.clear();
      }

      entity_cache_clear_lists();
      entity_cache_clear_snapshot();
      resolver::update_pending_shots();
      aimbot::update_shot_diagnostics();

      break;
    }

  case FRAME_NET_UPDATE_END:
    {
      entity_cache_snapshot snapshot{};
      snapshot.players.reserve(32);
      const bool refresh_friend_cache = steam_friends != nullptr && global_vars->curtime - last_time >= 1;
      const bool cache_player_info = config.aimbot.master || refresh_friend_cache;

      for (unsigned int i = 1; i <= entity_list->get_max_entities(); ++i) {
	Entity* entity = entity_list->entity_from_index(i);
	if (entity == nullptr) continue;

	switch (entity->get_class_id()) {
	case class_id::PLAYER:
	  {
            auto* player = static_cast<Player*>(entity);
            player_info pinfo{};
            const bool player_info_valid = cache_player_info && engine != nullptr && engine->get_player_info(entity->get_index(), &pinfo);
            if (!player->is_dormant() && player->is_alive()) {
	      entity_cache[class_id::PLAYER].push_back(entity);
              snapshot.players.push_back({
                .player = player,
                .entity = entity,
                .index = player->get_index(),
                .simulation_time = player->get_simulation_time(),
                .origin = player->get_origin(),
                .velocity = player->get_velocity(),
                .team = player->get_team(),
                .player_class = static_cast<int>(player->get_tf_class()),
                .friends_id = player_info_valid ? pinfo.friends_id : 0,
                .alive = true,
                .dormant = false,
                .friendly = player->is_friend(),
                .ignored = player->is_ignored(),
                .fakeplayer = player_info_valid && pinfo.fakeplayer,
                .player_info_valid = player_info_valid
              });
              resolver::record_player(player);
              backtrack::record_player(player);
            } else {
              aimbot::clear_network_pose(player);
            }

	    if (refresh_friend_cache) {
	      if (player_info_valid && pinfo.friends_id != 0) {
		friend_cache_store(pinfo.friends_id, steam_friends->is_friend(pinfo.friends_id));
	      }
	    }

	    break;
	  }

	case class_id::AMMO_OR_HEALTH_PACK:
	  {
	    if (entity->get_pickup_type() == pickup_type::AMMOPACK)
	      entity_cache[class_id::AMMO].push_back(entity);
	    else if (entity->get_pickup_type() == pickup_type::MEDKIT)
	      entity_cache[class_id::HEALTH_PACK].push_back(entity);

	    break;
	  }

	case class_id::CAPTURE_FLAG:
	  entity_cache[class_id::CAPTURE_FLAG].push_back(entity); break;

	case class_id::OBJECTIVE_RESOURCE:
	  entity_cache[class_id::OBJECTIVE_RESOURCE].push_back(entity); break;

	case class_id::SENTRY:
	case class_id::OBJECT_CART_DISPENSER:
	case class_id::DISPENSER:
	case class_id::TELEPORTER:
	  entity_cache[entity->get_class_id()].push_back(entity); break;

	case class_id::SNIPER_DOT:
	  entity_cache[class_id::SNIPER_DOT].push_back(entity); break;

        case class_id::ROCKET:
        case class_id::PILL_OR_STICKY:
        case class_id::FLARE:
        case class_id::ARROW:
        case class_id::CROSSBOW_BOLT:
          entity_cache[entity->get_class_id()].push_back(entity);
          break;

        default:
          {
            const char* network_name = entity->get_network_name();
            if (network_name != nullptr &&
                (std::strstr(network_name, "Boss") != nullptr ||
                 std::strstr(network_name, "Merasmus") != nullptr ||
                 std::strstr(network_name, "Eyeball") != nullptr ||
                 std::strstr(network_name, "Headless") != nullptr ||
                 std::strstr(network_name, "Zombie") != nullptr ||
                 std::strstr(network_name, "Tank") != nullptr)) {
              g_entity_cache_npcs.push_back(entity);
            }

            if (entity->is_network_class("CTFPumpkinBomb")) {
              entity_cache[class_id::PUMPKIN].push_back(entity);
              break;
            }

            if (entity->is_network_class("CCurrencyPack")) {
              entity_cache[class_id::MVM_CURRENCY].push_back(entity);
              break;
            }

            if (entity->is_network_class("CFuncUpgrades")
              || entity->is_network_class("CUpgrades")
              || entity->is_network_class("CFuncUpgradeStation")) {
              entity_cache[class_id::MVM_UPGRADE_STATION].push_back(entity);
            }

            break;
          }
	}

      }

      entity_cache_publish_snapshot(std::move(snapshot));

      if (global_vars->curtime - last_time >= 1) {
	last_time = global_vars->curtime;
      }

      visual_groups::store(entity_list->get_localplayer());

      break;
    }
  }

  if (current_stage == FRAME_RENDER_END) {
    entity_visuals::on_render_end();
    thirdperson::end_render_angles();
  }

  if (current_stage == FRAME_NET_UPDATE_END) {
    run_match_exec_on_level_change();
    run_skybox_changer();
    cat_ipc::client::tick();
    cathook::core::identify::tick();
    cathook::core::players::tick();
    automation::controller().on_frame_stage_notify();
    navbot::controller().on_frame_stage_notify();
  }
}
