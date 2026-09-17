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
#include "core/player_resource.hpp"
#include "core/commands.hpp"
#include "core/detach.hpp"
#include "core/identify/identify.hpp"
#include "core/ipc/ipc_client.hpp"
#include "core/player_manager.hpp"
#include "features/menu/config.hpp"
#include "features/combat/aimbot/aimbot.hpp"
#include "features/combat/aimbot/resolver.hpp"
#include "features/combat/backtrack/backtrack.hpp"
#include "features/combat/animation/anim_driver.hpp"
#include "features/automation/cheat_detection/cheat_detection.hpp"
#include "features/automation/killstreak/killstreak.hpp"
#include "features/automation/navbot/navbot_controller.hpp"
#include "features/automation/spectate/spectate.hpp"
#include "features/automation/anti_cheat_compat/anti_cheat_compat.hpp"
#include "features/visuals/thirdperson.hpp"
#include "features/visuals/skybox_changer.hpp"
#include "features/visuals/skin_changer.hpp"
#include "features/visuals/world_visuals.hpp"
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

namespace
{

float saved_interpolation_amount = 0.0f;
bool interpolation_amount_overridden = false;
Convar* cl_interpolate = nullptr;
int saved_cl_interpolate = 1;
bool cl_interpolate_overridden = false;

void update_interpolation_removals()
{
  if (convar_system != nullptr && cl_interpolate == nullptr) {
    cl_interpolate = convar_system->find_var("cl_interpolate");
  }

  if (cl_interpolate != nullptr && config.visuals.removals.interpolation) {
    if (!cl_interpolate_overridden) {
      saved_cl_interpolate = cl_interpolate->get_int();
      cl_interpolate_overridden = true;
    }
    if (cl_interpolate->get_int() != 0) {
      cl_interpolate->set_int(0);
    }
  } else if (cl_interpolate != nullptr && cl_interpolate_overridden) {
    cl_interpolate->set_int(saved_cl_interpolate);
    cl_interpolate_overridden = false;
  }
}

void begin_lerp_removal()
{
  if (!config.visuals.removals.lerp || global_vars == nullptr || interpolation_amount_overridden) {
    return;
  }

  saved_interpolation_amount = global_vars->interpolation_amount;
  global_vars->interpolation_amount = 0.0f;
  interpolation_amount_overridden = true;
}

void end_lerp_removal()
{
  if (!interpolation_amount_overridden || global_vars == nullptr) {
    return;
  }

  global_vars->interpolation_amount = saved_interpolation_amount;
  interpolation_amount_overridden = false;
}

Convar* zoom_sensitivity_ratio = nullptr;
float user_zoom_sensitivity_ratio = 1.0f;
bool ratio_overridden = false;

void update_zoom_sensitivity()
{

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

void restore_frame_stage_state()
{
  thirdperson::end_render_angles();
  end_lerp_removal();
  if (cl_interpolate != nullptr && cl_interpolate_overridden) {
    cl_interpolate->set_int(saved_cl_interpolate);
  }
  if (zoom_sensitivity_ratio != nullptr && ratio_overridden) {
    zoom_sensitivity_ratio->set_float(user_zoom_sensitivity_ratio);
  }
  cl_interpolate_overridden = false;
  ratio_overridden = false;
  cl_interpolate = nullptr;
  zoom_sensitivity_ratio = nullptr;
}

void frame_stage_notify_hook(void* me, ClientFrameStage current_stage) {
  CATHOOK_HOOK_GUARD();
  if (cathook::core::is_detach_pending()) {
    thirdperson::end_render_angles();
    if (frame_stage_notify_original != nullptr) {
      frame_stage_notify_original(me, current_stage);
    }
    cathook::core::service_detach_request();
    return;
  }

  const bool runtime_ready = global_vars != nullptr && entity_list != nullptr && engine != nullptr &&
    engine->is_connected() && engine->is_in_game();

  if (runtime_ready && current_stage == FRAME_RENDER_START) {
    thirdperson::begin_render_angles();
    update_interpolation_removals();
    begin_lerp_removal();
    update_zoom_sensitivity();
  }

  if (runtime_ready && current_stage == FRAME_NET_UPDATE_POSTDATAUPDATE_END) {
    skin_changer::apply();
  }

  if (frame_stage_notify_original == nullptr) {
    restore_frame_stage_state();
    return;
  }
  frame_stage_notify_original(me, current_stage);

  if (!runtime_ready) {
    entity_cache_clear_lists();
    entity_cache_clear_snapshot();
    cathook::core::player_resource::invalidate_player_resource_cache();
    last_time = 0.0f;
    run_match_exec_on_level_change();
    run_skybox_changer();
    skin_changer::invalidate();
    killstreak::reset();
    spectate::reset();
    cheat_detection::reset();
    anti_cheat_compat::reset();
    restore_frame_stage_state();
    return;
  }
  if (cathook::core::is_detach_pending() || engine == nullptr || entity_list == nullptr || global_vars == nullptr ||
      !engine->is_connected() || !engine->is_in_game()) {
    entity_cache_clear_lists();
    entity_cache_clear_snapshot();
    cathook::core::player_resource::invalidate_player_resource_cache();
    restore_frame_stage_state();
    return;
  }

  // Inventory changer frame-stage handler temporarily disabled.

  if (current_stage == FRAME_RENDER_START) {
    aimbot_note_render_clock();
    world_visuals::on_render_start();
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
      spectate::on_net_update_start();

      break;
    }

  case FRAME_NET_UPDATE_END:
    {
      entity_cache_snapshot snapshot{};
      snapshot.players.reserve(32);
      const bool refresh_friend_cache = steam_friends != nullptr && global_vars->curtime - last_time >= 1;
      const bool cache_player_info = true;

      const int max_entities_value = entity_list->get_max_entities();
      if (max_entities_value <= 0 || max_entities_value > 8192) {
        entity_cache_clear_lists();
        entity_cache_clear_snapshot();
        break;
      }
      const unsigned int max_entities = static_cast<unsigned int>(max_entities_value);
      for (unsigned int i = 1; i < max_entities; ++i) {
	Entity* entity = entity_list->entity_from_index(i);
	if (entity == nullptr) continue;

        const auto* client_class = entity->get_typed_client_class();
        if (client_class == nullptr) {
          continue;
        }
        const class_id entity_class = static_cast<class_id>(client_class->class_id);
        if (static_cast<int>(entity_class) < 0 || static_cast<int>(entity_class) > 512) {
          continue;
        }
        const char* network_name = client_class->network_name;
        bool classified = false;

	switch (entity_class) {
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
                .friendly = player_info_valid && pinfo.friends_id != 0 && pinfo.fakeplayer != true &&
                    (cathook::core::players::is_friendly(static_cast<std::uint32_t>(pinfo.friends_id)) ||
                     friend_cache_lookup(pinfo.friends_id)),
                .ignored = player_info_valid && pinfo.friends_id != 0 && pinfo.fakeplayer != true &&
                    cathook::core::players::is_ignored(static_cast<std::uint32_t>(pinfo.friends_id)),
                .fakeplayer = player_info_valid && pinfo.fakeplayer,
                .player_info_valid = player_info_valid
              });
            } else {
              resolver::clear_player(player);
              animation::reset_player(player);
              aimbot::clear_network_pose(player);
            }

	    if (refresh_friend_cache) {
	      if (player_info_valid && pinfo.friends_id != 0) {
		friend_cache_store(pinfo.friends_id, steam_friends->is_friend(pinfo.friends_id));
	      }
	    }

            classified = true;
	    break;
	  }

	case class_id::PLAYER_RESOURCE:
	  cathook::core::player_resource::cache_player_resource_entity(entity, static_cast<int>(i));
	  classified = true;
	  break;

	case class_id::AMMO_OR_HEALTH_PACK:
	  {
	    const enum pickup_type pickup = entity->get_pickup_type();
	    if (pickup == pickup_type::AMMOPACK)
	      entity_cache[class_id::AMMO].push_back(entity);
	    else if (pickup == pickup_type::MEDKIT)
	      entity_cache[class_id::HEALTH_PACK].push_back(entity);

            classified = true;
	    break;
	  }

	case class_id::CAPTURE_FLAG:
	  entity_cache[class_id::CAPTURE_FLAG].push_back(entity);
	  classified = true;
	  break;

	case class_id::OBJECTIVE_RESOURCE:
	  entity_cache[class_id::OBJECTIVE_RESOURCE].push_back(entity);
	  classified = true;
	  break;

	case class_id::SENTRY:
	case class_id::OBJECT_CART_DISPENSER:
	case class_id::DISPENSER:
	case class_id::TELEPORTER:
	  entity_cache[entity_class].push_back(entity);
	  classified = true;
	  break;

	case class_id::SNIPER_DOT:
	  entity_cache[class_id::SNIPER_DOT].push_back(entity);
	  classified = true;
	  break;

        case class_id::ROCKET:
        case class_id::PILL_OR_STICKY:
        case class_id::FLARE:
        case class_id::ARROW:
        case class_id::CROSSBOW_BOLT:
        case class_id::SENTRY_ROCKET:
          entity_cache[entity_class].push_back(entity);
          classified = true;
          break;

        case class_id::WEARABLE:
        case class_id::WEARABLE_CAMPAIGN_ITEM:
        case class_id::WEARABLE_DEMO_SHIELD:
        case class_id::WEARABLE_ECON:
        case class_id::WEARABLE_ITEM:
        case class_id::WEARABLE_RAZORBACK:
        case class_id::WEARABLE_VM:
        case class_id::WEARABLE_LEVELABLE_ITEM:
        case class_id::WEARABLE_ROBOT_ARM:
          entity_cache[entity_class].push_back(entity);
          classified = true;
          break;
	}

        if (classified || network_name == nullptr || network_name[0] == '\0') {
          continue;
        }

        if (std::strstr(network_name, "Boss") != nullptr ||
            std::strstr(network_name, "Merasmus") != nullptr ||
            std::strstr(network_name, "Eyeball") != nullptr ||
            std::strstr(network_name, "Headless") != nullptr ||
            std::strstr(network_name, "Zombie") != nullptr ||
            std::strstr(network_name, "Tank") != nullptr) {
          g_entity_cache_npcs.push_back(entity);
        }

        if (std::strcmp(network_name, "CTFPumpkinBomb") == 0) {
          entity_cache[class_id::PUMPKIN].push_back(entity);
        } else if (std::strcmp(network_name, "CTFProjectile_SentryRocket") == 0) {
          entity_cache[class_id::SENTRY_ROCKET].push_back(entity);
        } else if (std::strcmp(network_name, "CCurrencyPack") == 0) {
          entity_cache[class_id::MVM_CURRENCY].push_back(entity);
        } else if (std::strcmp(network_name, "CFuncUpgrades") == 0
          || std::strcmp(network_name, "CUpgrades") == 0
          || std::strcmp(network_name, "CFuncUpgradeStation") == 0) {
          entity_cache[class_id::MVM_UPGRADE_STATION].push_back(entity);
        }
      }

      entity_cache_publish_snapshot(std::move(snapshot));

      animation::update_all();

      for (const entity_cache_player_entry& entry : entity_cache_current_snapshot().players) {
        if (entry.player == nullptr || entry.dormant || !entry.alive) {
          continue;
        }

        resolver::record_player(entry.player);
        backtrack::record_player(entry.player);
      }

      if (global_vars->curtime - last_time >= 1) {
	last_time = global_vars->curtime;
      }

      visual_groups::store(entity_list->get_localplayer());

      break;
    }
  }

  if (current_stage == FRAME_RENDER_END) {
    end_lerp_removal();
    entity_visuals::on_render_end();
    thirdperson::end_render_angles();
  }

  if (current_stage == FRAME_NET_UPDATE_END) {
    run_match_exec_on_level_change();
    run_skybox_changer();
    cat_ipc::client::tick();
    cathook::core::identify::tick();
    cathook::core::players::tick();
    killstreak::apply();
    spectate::on_net_update_end();
    cheat_detection::on_net_update_end();
    automation::controller().on_frame_stage_notify();
    navbot::controller().on_frame_stage_notify();
  }
}
