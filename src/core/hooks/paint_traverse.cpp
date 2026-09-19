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
#include "features/menu/config.hpp"
#include "features/visuals/overlay_projection.hpp"
#include "core/detach.hpp"
#include "core/ipc/ipc_client.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/surface.hpp"
#include "features/automation/misc/misc.hpp"
#include <chrono>
#include <cstring>
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

void* top_panel = nullptr;
void* hud_scope_panel = nullptr;
void* hud_underwater_panel = nullptr;
void* hud_damage_panel = nullptr;
void* hud_zoom_panel = nullptr;

void reset_panel_cache() {
  top_panel = nullptr;
  hud_scope_panel = nullptr;
  hud_underwater_panel = nullptr;
  hud_damage_panel = nullptr;
  hud_zoom_panel = nullptr;
}

void remember_named_panel(void* panel, std::string_view name) {
  if (name == "HudScope") {
    hud_scope_panel = panel;
  } else if (name == "HudUnderwaterOverlay") {
    hud_underwater_panel = panel;
  } else if (name == "HudDamageIndicator") {
    hud_damage_panel = panel;
  } else if (name == "HudZoom") {
    hud_zoom_panel = panel;
  } else if (name == "MatSystemTopPanel") {
    if (top_panel != nullptr && top_panel != panel) {
      reset_panel_cache();
    }
    top_panel = panel;
  }
}

void run_top_panel_work() {
  surface_runtime::mark_ready();
  (void)overlay_projection::update_view_matrix();
  automation::controller().on_paint();
  mono_ui_build_frame();

  static auto last_ipc_tick = std::chrono::steady_clock::time_point{};
  const bool in_game = engine != nullptr && engine->is_in_game();
  if (in_game) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (last_ipc_tick.time_since_epoch().count() == 0 ||
      now - last_ipc_tick >= std::chrono::seconds(1)) {
    last_ipc_tick = now;
    pup_ipc::client::tick();
  }
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
  PUPHOOK_HOOK_GUARD();
  if (paint_traverse_original_active) {
    if (paint_traverse_original != nullptr) {
      paint_traverse_original(me, panel, force_repaint, allow_force);
    }
    return;
  }

  if (puphook::core::is_detach_pending()) {
    call_paint_traverse_original(me, panel, force_repaint, allow_force);
    puphook::core::service_detach_request();
    return;
  }

  if (engine == nullptr || !engine->is_in_game()) {
    reset_panel_cache();
  }

  const bool skip_scope = config.visuals.removals.scope;
  const bool skip_overlays = config.visuals.effects.remove_screen_overlays;

  if (panel == top_panel) {
    call_paint_traverse_original(me, panel, force_repaint, allow_force);
    run_top_panel_work();
    return;
  }

  if (skip_scope && panel == hud_scope_panel) {
    return;
  }
  if (skip_overlays && (panel == hud_underwater_panel || panel == hud_damage_panel || panel == hud_zoom_panel)) {
    return;
  }

  if (skip_scope || skip_overlays || top_panel == nullptr) {
    const std::string_view panel_name = get_panel_name(panel);
    remember_named_panel(panel, panel_name);
    if (skip_scope && panel_name == "HudScope") {
      return;
    }
    if (skip_overlays &&
        (panel_name == "HudUnderwaterOverlay" || panel_name == "HudDamageIndicator" ||
         panel_name == "HudZoom")) {
      return;
    }
    call_paint_traverse_original(me, panel, force_repaint, allow_force);
    if (panel == top_panel || panel_name == "MatSystemTopPanel") {
      run_top_panel_work();
    }
    return;
  }

  call_paint_traverse_original(me, panel, force_repaint, allow_force);
}
