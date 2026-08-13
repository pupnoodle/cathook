#include "features/menu/config.hpp"
#include "core/shared/sigs.hpp"
#include "core/detach.hpp"
#include "core/print.hpp"
#include "libsigscan/libsigscan.h"

using view_render_screen_space_effects_fn = void (*)(void*, int, int, int, int);
using view_render_screen_overlay_fn = void (*)(void*, int, int, int, int);

view_render_screen_space_effects_fn view_render_perform_screen_space_effects_original = nullptr;
view_render_screen_overlay_fn view_render_perform_screen_overlay_original = nullptr;

void view_render_perform_screen_space_effects_hook(void* me, int x, int y, int w, int h)
{
  CATHOOK_HOOK_GUARD();
  if (config.visuals.removals.post_processing || config.visuals.effects.remove_screen_effects) {
    return;
  }
  if (view_render_perform_screen_space_effects_original != nullptr) {
    view_render_perform_screen_space_effects_original(me, x, y, w, h);
  }
}

void view_render_perform_screen_overlay_hook(void* me, int x, int y, int w, int h)
{
  CATHOOK_HOOK_GUARD();
  if (config.visuals.effects.remove_screen_overlays) {
    return;
  }
  if (view_render_perform_screen_overlay_original != nullptr) {
    view_render_perform_screen_overlay_original(me, x, y, w, h);
  }
}

void resolve_view_render_removal_hooks()
{
  view_render_perform_screen_space_effects_original =
    reinterpret_cast<view_render_screen_space_effects_fn>(
      sigscan_module("client.so", sigs::view_render_perform_screen_space_effects));
  if (view_render_perform_screen_space_effects_original == nullptr) {
    print("CViewRender::PerformScreenSpaceEffects signature missing; post-processing removal uses ClientMode only\n");
  }

  view_render_perform_screen_overlay_original =
    reinterpret_cast<view_render_screen_overlay_fn>(
      sigscan_module("client.so", sigs::view_render_perform_screen_overlay));
  if (view_render_perform_screen_overlay_original == nullptr) {
    print("CViewRender::PerformScreenOverlay signature missing; panel overlay removal remains active\n");
  }
}

