#include <cstring>

#include "features/menu/config.hpp"
#include "games/tf2/sdk/entities/entity.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"

void (*model_render_draw_model_execute_original)(void*, const DrawModelState&, const ModelRenderInfo&, matrix_3x4*) = nullptr;
void (*model_render_forced_material_override_original)(void*, Material*, OverrideType) = nullptr;

void model_render_draw_model_execute_hook(void* me, const DrawModelState& state, const ModelRenderInfo& info, matrix_3x4* bones)
{
  CATHOOK_HOOK_GUARD();

  Entity* entity = entity_list != nullptr && info.entity_index > 0
    ? entity_list->entity_from_index(static_cast<unsigned int>(info.entity_index))
    : nullptr;
  const char* network_name = entity != nullptr ? entity->get_network_name() : nullptr;
  const char* model_name = info.model != nullptr ? info.model->name : nullptr;
  const bool ragdoll = network_name != nullptr &&
    (std::strstr(network_name, "Ragdoll") != nullptr || std::strstr(network_name, "ragdoll") != nullptr);
  const bool gib = (network_name != nullptr &&
    (std::strstr(network_name, "Gib") != nullptr || std::strstr(network_name, "gib") != nullptr)) ||
    (model_name != nullptr && (std::strstr(model_name, "/gibs/") != nullptr || std::strstr(model_name, "gib") != nullptr));
  if ((config.visuals.removals.ragdolls && ragdoll) || (config.visuals.removals.gibs && gib)) {
    return;
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
