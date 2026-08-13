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
#include <chrono>
#include <string_view>

void (*paint_traverse_original)(void*, void*, bool, bool) = NULL;
const char* (*get_panel_name_original)(void*, void*) = NULL;

void paint_traverse_hook(void* me, void* panel, bool force_repaint, bool allow_force);

namespace {

thread_local bool paint_traverse_original_active = false;

class paint_traverse_original_scope final {
public:
  paint_traverse_original_scope()
    : previous_(paint_traverse_original_active) {
    paint_traverse_original_active = true;
  }

  paint_traverse_original_scope(const paint_traverse_original_scope&) = delete;
  paint_traverse_original_scope& operator=(const paint_traverse_original_scope&) = delete;

  ~paint_traverse_original_scope() {
    paint_traverse_original_active = previous_;
  }

private:
  bool previous_ = false;
};

void call_paint_traverse_original(void* me, void* panel, const bool force_repaint, const bool allow_force) {
  if (paint_traverse_original == nullptr) return;
  paint_traverse_original_scope scope{};
  paint_traverse_original(me, panel, force_repaint, allow_force);
}

}

void* vgui;
const char* get_panel_name(void* panel) {
    if (vgui == nullptr || panel == nullptr || get_panel_name_original == nullptr) {
      return "";
    }

    return get_panel_name_original(vgui, panel);
}

void paint_traverse_hook(void* me, void* panel, bool force_repaint, bool allow_force) {
  CATHOOK_HOOK_GUARD();
  if (paint_traverse_original_active) {
    if (paint_traverse_original != nullptr) {
      paint_traverse_original(me, panel, force_repaint, allow_force);
    }
    return;
  }

  if (cathook::core::is_detach_pending()) {
    call_paint_traverse_original(me, panel, force_repaint, allow_force);
    cathook::core::service_detach_request();
    return;
  }

  const std::string_view panel_name = get_panel_name(panel);

  if (config.visuals.removals.scope == true && panel_name == "HudScope") {
    return;
  }

  if (config.visuals.effects.remove_screen_overlays &&
      (panel_name == "HudUnderwaterOverlay" || panel_name == "HudDamageIndicator" ||
       panel_name == "HudZoom")) {
    return;
  }

  call_paint_traverse_original(me, panel, force_repaint, allow_force);

  if (panel_name != "MatSystemTopPanel") {
    return;
  }

  surface_runtime::mark_ready();

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
