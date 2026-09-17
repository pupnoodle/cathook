#include "spectate.hpp"

#include "features/menu/config.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/input.hpp"

namespace spectate {

namespace {

int intended_userid = -1;
int active_userid = -1;
int original_target_handle = 0;
observer_mode original_mode = observer_mode::none;
Vec3 saved_view_angles{};
bool holding_view = false;

Player* local_player() {
  if (engine == nullptr || entity_list == nullptr || !engine->is_in_game()) {
    return nullptr;
  }
  return entity_list->get_localplayer();
}

Entity* target_entity() {
  if (active_userid < 0 || engine == nullptr || entity_list == nullptr) {
    return nullptr;
  }
  const int index = engine->get_player_index_from_id(active_userid);
  if (index <= 0) {
    return nullptr;
  }
  Entity* entity = entity_list->entity_from_index(static_cast<unsigned int>(index));
  return entity != nullptr && entity->get_class_id() == class_id::PLAYER
      ? static_cast<Entity*>(entity)
      : nullptr;
}

}

void reset() {
  intended_userid = -1;
  active_userid = -1;
  holding_view = false;
}

void on_net_update_end() {
  if (intended_userid < 0 && active_userid < 0) {
    return;
  }

  Player* localplayer = local_player();
  if (localplayer == nullptr) {
    active_userid = intended_userid = -1;
    return;
  }

  active_userid = intended_userid;
  Entity* target = target_entity();
  if (target != nullptr && target == static_cast<Entity*>(localplayer)) {
    target = nullptr;
    active_userid = intended_userid = -1;
  }

  if (active_userid < 0 || target == nullptr) {
    if (localplayer->is_alive() && localplayer->get_observer_target_handle() != 0) {
      localplayer->set_observer_mode(observer_mode::none);
      localplayer->set_observer_target_handle(0);
    }
    return;
  }

  original_target_handle = localplayer->get_observer_target_handle();
  original_mode = localplayer->get_observer_mode();

  auto* target_player = static_cast<Player*>(target);
  const observer_mode target_mode =
      target_player->get_observer_target_handle() != 0 ? target_player->get_observer_mode() : observer_mode::none;
  if (target_mode == observer_mode::in_eye || target_mode == observer_mode::chase) {
    localplayer->set_observer_target_handle(target_player->get_observer_target_handle());
  } else {
    localplayer->set_observer_target_handle(target_player->get_ref_handle());
  }

  const bool thirdperson = config.visuals.thirdperson.enabled;
  localplayer->set_observer_mode(thirdperson ? observer_mode::chase : observer_mode::in_eye);
  if (input != nullptr) {
    if (thirdperson) {
      input->to_thirdperson();
    } else {
      input->to_firstperson();
    }
  }
}

void on_net_update_start() {
  Player* localplayer = local_player();
  if (localplayer == nullptr || active_userid < 0) {
    return;
  }

  localplayer->set_observer_target_handle(original_target_handle);
  localplayer->set_observer_mode(original_mode);
}

void on_create_move(user_cmd* cmd) {
  if (cmd == nullptr || (intended_userid < 0 && active_userid < 0 && !holding_view)) {
    return;
  }

  if ((cmd->buttons & ~IN_SCORE) != 0) {
    intended_userid = -1;
  }

  const bool has_target = active_userid >= 0;
  if (!has_target && !holding_view) {
    saved_view_angles = cmd->view_angles;
    return;
  }

  if (!has_target && holding_view && engine != nullptr) {
    engine->set_view_angles(saved_view_angles);
  }
  holding_view = has_target;
  cmd->view_angles = saved_view_angles;
}

void set_target_userid(int userid) {
  intended_userid = intended_userid == userid ? -1 : userid;
}

int target_userid() {
  return intended_userid;
}

int target_index() {
  if (intended_userid < 0 || engine == nullptr) {
    return -1;
  }
  return engine->get_player_index_from_id(intended_userid);
}

}
