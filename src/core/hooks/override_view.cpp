/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/core/hooks/override_view.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "core/types.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "features/menu/config.hpp"
#include "features/visuals/overlay_projection.hpp"
#include "features/visuals/thirdperson.hpp"
#include "games/tf2/sdk/entities/player.hpp"

void (*override_view_original)(void*, view_setup*);

void override_view_hook(void* me, view_setup* setup) {
  CATHOOK_HOOK_GUARD();
  thirdperson::update_taunt_camera();

  if (setup == nullptr) {
    if (override_view_original != nullptr) {
      override_view_original(me, setup);
    }
    return;
  }

  Player* localplayer = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  Vec3 original_punch{};
  Vec3* punch = nullptr;
  static tf2_netvars::lazy_offset punch_offset{"DT_BasePlayer", {"localdata", "m_Local", "m_vecPunchAngle"}};
  if (config.visuals.removals.view_punch && localplayer != nullptr && punch_offset > 0) {
    punch = reinterpret_cast<Vec3*>(reinterpret_cast<std::uintptr_t>(localplayer) + static_cast<std::uintptr_t>(punch_offset.operator int()));
    original_punch = *punch;
    *punch = {};
  }

  if (override_view_original != nullptr) {
    override_view_original(me, setup);
  }

  if (punch != nullptr) {
    *punch = original_punch;
  }

  if (localplayer == nullptr) return;

  const int player_fov = localplayer->get_fov();
  const int default_fov = localplayer->get_default_fov();
  const bool zoomed = player_fov > 1 && default_fov > 1 && player_fov < default_fov;

  if (config.visuals.removals.zoom == true) {
    setup->fov = config.visuals.override_fov ? config.visuals.custom_fov : default_fov;
  } else {
    if (zoomed && config.visuals.override_zoom_fov) {
      setup->fov = config.visuals.custom_zoom_fov;
    } else if (config.visuals.override_fov && !zoomed) {
      setup->fov = config.visuals.custom_fov;
    }
  }

  static Convar* viewmodel_fov = convar_system != nullptr ? convar_system->find_var("viewmodel_fov") : nullptr;
  if (viewmodel_fov != nullptr && config.visuals.override_viewmodel_fov == true) {
    viewmodel_fov->set_float(config.visuals.custom_viewmodel_fov);
  }

  thirdperson::update_camera(setup);
  overlay_projection::set_view_fov(setup->fov);
}
