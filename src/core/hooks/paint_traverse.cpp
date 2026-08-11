/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/hooks/paint_traverse.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "features/visuals/esp/esp.hpp"
#include <string>
#include "features/menu/config.hpp"
#include "features/visuals/overlay_projection.hpp"
#include "core/detach.hpp"
#include "core/ipc/ipc_client.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/surface.hpp"
#include "features/automation/misc/misc.hpp"
#include "features/automation/telemetry_blocker/telemetry_blocker.hpp"
#include <chrono>

void (*paint_traverse_original)(void*, void*, bool, bool) = NULL;
const char* (*get_panel_name_original)(void*, void*) = NULL;

void* vgui;
const char* get_panel_name(void* panel) {
    if (vgui == nullptr || panel == nullptr || get_panel_name_original == nullptr) {
      return "";
    }

    return get_panel_name_original(vgui, panel);
}

void paint_traverse_hook(void* me, void* panel, bool force_repaint, bool allow_force) {
  CATHOOK_HOOK_GUARD();
  if (cathook::core::is_detach_pending()) {
    paint_traverse_original(me, panel, force_repaint, allow_force);
    cathook::core::service_detach_request();
    return;
  }

  std::string panel_name = get_panel_name(panel);

  if (config.visuals.removals.scope == true && panel_name == "HudScope") {
    return;
  }

  paint_traverse_original(me, panel, force_repaint, allow_force);

  if (panel_name != "MatSystemTopPanel") {
    return;
  }

  surface_runtime::mark_ready();
  telemetry_blocker::update();

  const bool view_matrix_updated = overlay_projection::update_view_matrix();
  (void)view_matrix_updated;
  automation::controller().on_paint();
  mono_ui_build_opengl_frame();

  static auto last_ipc_tick = std::chrono::steady_clock::time_point{};
  const auto now = std::chrono::steady_clock::now();
  if (last_ipc_tick.time_since_epoch().count() == 0 ||
      now - last_ipc_tick >= std::chrono::seconds(1)) {
    last_ipc_tick = now;
    cat_ipc::client::tick();
  }
}
