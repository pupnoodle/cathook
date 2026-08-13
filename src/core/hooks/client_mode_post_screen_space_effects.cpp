#include "core/types.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "features/visuals/entity_visuals_effects.hpp"

bool (*client_mode_post_screen_space_effects_original)(void*, const view_setup*);

bool client_mode_post_screen_space_effects_hook(void* me, const view_setup* setup)
{
  CATHOOK_HOOK_GUARD();
  if (config.visuals.removals.post_processing &&
      (engine == nullptr || !engine->is_drawing_loading_image())) {
    entity_visuals::on_post_screen_space_effects();
    return true;
  }
  if (config.visuals.effects.remove_screen_effects &&
      (engine == nullptr || !engine->is_drawing_loading_image())) {
    entity_visuals::on_post_screen_space_effects();
    return true;
  }
  const bool result = client_mode_post_screen_space_effects_original(me, setup);
  if (result) entity_visuals::on_post_screen_space_effects();
  return result;
}
