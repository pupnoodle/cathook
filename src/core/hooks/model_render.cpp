#include "features/misc/removals.hpp"

void (*model_render_draw_model_execute_original)(void*, const DrawModelState&, const ModelRenderInfo&, matrix_3x4*) = nullptr;
void (*model_render_forced_material_override_original)(void*, Material*, OverrideType) = nullptr;

void model_render_draw_model_execute_hook(void* me, const DrawModelState& state, const ModelRenderInfo& info, matrix_3x4* bones)
{
  CATHOOK_HOOK_GUARD();
  if (config.visuals.removals.arms || config.visuals.removals.hats) {
    if (model_info != nullptr) {
      if (auto* studio = model_info->get_studio_model(info.model); studio != nullptr) {
        if (removals::should_skip_model(studio->name)) {
          return;
        }
      }
    }
  }

  entity_visuals::on_draw_model_execute(me, state, info, bones);
}

void model_render_forced_material_override_hook(void* me, Material* material, OverrideType override_type)
{
  CATHOOK_HOOK_GUARD();
  if (entity_visuals::is_rendering_effect()) return;
  if (model_render_forced_material_override_original != nullptr) {
    model_render_forced_material_override_original(me, material, override_type);
  }
}
