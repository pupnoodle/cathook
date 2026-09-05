/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/hooks/fire_event_client_side.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "games/tf2/sdk/interfaces/game_event_manager.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "core/identify/identify.hpp"
#include "core/ipc/ipc_client.hpp"
#include "core/math/math.hpp"
#include "features/automation/medic_automation/medic_automation.hpp"
#include "features/automation/misc/misc.hpp"
#include "features/automation/navbot/navbot_controller.hpp"
#include "features/combat/aimbot/aimbot.hpp"
#include "features/combat/aimbot/resolver.hpp"
#include "features/visuals/hitmarker.hpp"
#include "core/detach.hpp"

namespace crit_hack { void on_game_event(GameEvent* event); }
#include <cfloat>
#define TF_DEATH_DOMINATION				0x0001
#define TF_DEATH_ASSISTER_DOMINATION	0x0002
#define TF_DEATH_REVENGE				0x0004
#define TF_DEATH_ASSISTER_REVENGE		0x0008
#define TF_DEATH_FIRST_BLOOD			0x0010
#define TF_DEATH_FEIGN_DEATH			0x0020
#define TF_DEATH_INTERRUPTED			0x0040
#define TF_DEATH_GIBBED					0x0080
#define TF_DEATH_PURGATORY				0x0100
#define TF_DEATH_MINIBOSS				0x0200
#define TF_DEATH_AUSTRALIUM				0x0400

bool (*fire_event_client_side_original)(void*, GameEvent*) = NULL;

bool fire_event_client_side_hook(void* me, GameEvent* event) {
  CATHOOK_HOOK_GUARD();
  if (event == nullptr) {
    return fire_event_client_side_original(me, event);
  }

  if (cathook::core::is_detach_pending()) {
    return fire_event_client_side_original(me, event);
  }

  crit_hack::on_game_event(event);

  cat_ipc::client::on_game_event(event);

  navbot::controller().on_game_event(event);
  medic_automation::controller().on_game_event(event);
  automation::controller().on_game_event(event);

  const char* raw_event_name = event->get_name();
  if (raw_event_name == nullptr) {
    return fire_event_client_side_original(me, event);
  }

  std::string event_name = std::string(raw_event_name);

  if (event_name == "weapon_fire" || event_name == "player_shoot") {
    Player* shooter = entity_list->get_player_from_id(event->get_int("userid"));
    backtrack::report_shot(shooter);
    resolver::on_local_weapon_fire(shooter);
    aimbot::on_weapon_fire(shooter);
  }

  if (event_name == "item_pickup") {
    Player* obtainer = entity_list->get_player_from_id(event->get_int("userid"));
    if (obtainer != nullptr && !obtainer->is_dormant()) {
      const char* item_name = event->get_string("item");
      if (strstr(item_name, "medkit") || strstr(item_name, "ammopack")) {
	float previous = FLT_MAX;
	Entity* obtained_entity = nullptr;
	if (strstr(item_name, "medkit")) {
	  for (Entity* pickup : entity_cache[class_id::HEALTH_PACK]) {
	    float distance = distance_3d(obtainer->get_origin(), pickup->get_origin());
	    if (distance < previous) {
	      previous = distance;
	      obtained_entity = pickup;
	    }
	  }
	} else if (strstr(item_name, "ammopack")) {
	  for (Entity* pickup : entity_cache[class_id::AMMO]) {
	    float distance = distance_3d(obtainer->get_origin(), pickup->get_origin());
	    if (distance < previous) {
	      previous = distance;
	      obtained_entity = pickup;
	    }
	  }
	}

	if (obtained_entity != nullptr)
	  pickup_item_cache.push_back(PickupItem{obtained_entity->get_origin(), global_vars->curtime + 10});
      }
    }
  }

  if (event_name == "player_hurt") {
    Player* victim = entity_list->get_player_from_id(event->get_int("userid"));
    Player* attacker = entity_list->get_player_from_id(event->get_int("attacker"));
    resolver::note_player_hurt(attacker, victim);
    aimbot::on_player_hurt(attacker, victim, event->get_int("damageamount"));
    hitmarker::on_player_hurt(attacker, victim, event->get_int("damageamount"), event->get_bool("crit"), event->get_int("custom") == 1);
  }

  if (event_name == "player_death") {
	if (event->get_int("death_flags") & TF_DEATH_FEIGN_DEATH) {

	} else {
	    Player* victim = entity_list->get_player_from_id(event->get_int("userid"));
	    if (victim != nullptr && victim == entity_list->get_localplayer()) {
	      cathook::core::identify::on_player_death(event->get_int("attacker"));
	    }
	}
  }

  return fire_event_client_side_original(me, event);
}
