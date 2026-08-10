namespace entity_visuals
{

draw_model_execute_fn draw_model_execute_original = nullptr;

namespace
{

struct glow_entity {
  Entity* entity = nullptr;
  RGBA_float color{};
  DrawModelState state{};
  ModelRenderInfo info{};
  matrix_3x4* bones = nullptr;
};

struct glow_batch {
  glow_settings settings{};
  std::vector<glow_entity> entities{};
};

std::vector<glow_batch> glow_batches{};
thread_local bool rendering_effect = false;
Material* mat_glow_color = nullptr;
Material* mat_halo = nullptr;
Material* mat_blur_x = nullptr;
Material* mat_blur_y = nullptr;
MaterialVar* bloom_amount = nullptr;
Texture* render_buffer_0 = nullptr;
Texture* render_buffer_1 = nullptr;
std::vector<Material*> retired_materials{};
std::vector<Texture*> retired_textures{};
int resource_width = 0;
int resource_height = 0;
bool resources_ready = false;

class render_context_scope final {
public:
  explicit render_context_scope(MaterialSystem* material_system)
    : context_(material_system != nullptr ? material_system->get_render_context() : nullptr) {
    if (context_ != nullptr) context_->begin_render();
  }

  render_context_scope(const render_context_scope&) = delete;
  render_context_scope& operator=(const render_context_scope&) = delete;

  ~render_context_scope() {
    if (context_ != nullptr) {
      context_->end_render();
      context_->release();
    }
  }

  [[nodiscard]] RenderContext* get() const { return context_; }

private:
  RenderContext* context_ = nullptr;
};

[[nodiscard]] bool apply_distance_alpha(const float distance, const float start, const float end, const bool smooth, RGBA_float& color)
{
  if (distance < start || distance > end) return false;
  if (!smooth) return color.a > 0.0f;
  if (distance > end - 256.0f) color.a *= std::clamp((end - distance) / 256.0f, 0.0f, 1.0f);
  if (start > 0.0f && distance < start + 256.0f) color.a *= std::clamp((distance - start) / 256.0f, 0.0f, 1.0f);
  return color.a > 0.0f;
}

[[nodiscard]] float distance_for(Entity* entity)
{
  if (entity == nullptr || entity_list == nullptr) return 0.0f;
  Entity* local = entity_list->get_localplayer();
  if (local == nullptr) return 0.0f;
  const Vec3 delta = entity->get_render_origin() - local->get_render_origin();
  return std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
}

void call_original(void* instance, const DrawModelState& state, const ModelRenderInfo& info, matrix_3x4* bones)
{
  if (draw_model_execute_original != nullptr) draw_model_execute_original(instance, state, info, bones);
}

void set_stencil(RenderContext* context, const int reference, const int write_mask, const int test_mask,
  const StencilComparisonMode compare, const StencilOperation pass, const StencilOperation fail, const StencilOperation zfail)
{
  if (context == nullptr) return;
  context->set_stencil_enable(true);
  context->set_stencil_reference_count(reference);
  context->set_stencil_write_mask(write_mask);
  context->set_stencil_test_mask(test_mask);
  context->set_stencil_compare_mode(compare);
  context->set_stencil_pass_mode(pass);
  context->set_stencil_fail_mode(fail);
  context->set_stencil_zfail_mode(zfail);
}

Material* find_engine_material(const char* name)
{
  if (material_system == nullptr) return nullptr;
  Material* material = material_system->find_material(name, "Other", false, nullptr);
  if (material != nullptr) material->increment_reference_count();
  return material;
}

bool bind_base_texture(Material* material, Texture* texture)
{
  if (material == nullptr || texture == nullptr) return false;
  bool found = false;
  MaterialVar* base_texture = material->find_var("$basetexture", &found, false);
  if (!found || base_texture == nullptr) return false;
  base_texture->set_texture_value(texture);
  return true;
}

void reset_resource_handles()
{
  mat_glow_color = nullptr;
  mat_halo = nullptr;
  mat_blur_x = nullptr;
  mat_blur_y = nullptr;
  bloom_amount = nullptr;
  render_buffer_0 = nullptr;
  render_buffer_1 = nullptr;
  resource_width = 0;
  resource_height = 0;
  resources_ready = false;
}

void release_resources()
{
  if (mat_glow_color != nullptr) mat_glow_color->decrement_reference_count();
  if (mat_halo != nullptr) mat_halo->decrement_reference_count();
  if (mat_blur_x != nullptr) mat_blur_x->decrement_reference_count();
  if (mat_blur_y != nullptr) mat_blur_y->decrement_reference_count();
  if (render_buffer_0 != nullptr) render_buffer_0->decrement_reference_count();
  if (render_buffer_1 != nullptr) render_buffer_1->decrement_reference_count();
  for (Material* material : retired_materials) {
    if (material != nullptr) material->decrement_reference_count();
  }
  for (Texture* texture : retired_textures) {
    if (texture != nullptr) texture->decrement_reference_count();
  }
  retired_materials.clear();
  retired_textures.clear();
  reset_resource_handles();
}

void retire_resources()
{
  if (mat_glow_color != nullptr) retired_materials.emplace_back(mat_glow_color);
  if (mat_halo != nullptr) retired_materials.emplace_back(mat_halo);
  if (mat_blur_x != nullptr) retired_materials.emplace_back(mat_blur_x);
  if (mat_blur_y != nullptr) retired_materials.emplace_back(mat_blur_y);
  if (render_buffer_0 != nullptr) retired_textures.emplace_back(render_buffer_0);
  if (render_buffer_1 != nullptr) retired_textures.emplace_back(render_buffer_1);
  reset_resource_handles();
}

bool ensure_resources()
{
  if (engine == nullptr || material_system == nullptr || !engine->is_in_game() || engine->is_drawing_loading_image()) return false;
  const Vec2 screen = engine->get_screen_size();
  if (screen.x <= 0 || screen.y <= 0) {
    if (resources_ready) retire_resources();
    return false;
  }
  if (resources_ready && (resource_width != static_cast<int>(screen.x) || resource_height != static_cast<int>(screen.y))) {
    retire_resources();
  }
  if (resources_ready) {
    if (!materials.loaded()) materials.load();
    return materials.loaded();
  }
  if (!materials.load()) return false;

  mat_glow_color = material_system->find_material("dev/glow_color", "Other", false, nullptr);
  if (mat_glow_color != nullptr) mat_glow_color->increment_reference_count();

  render_buffer_0 = material_system->create_named_render_target_texture_ex(
    "monolilth_glow_buffer_0", screen.x, screen.y, rt_size_literal, image_format_rgba8888,
    material_rt_depth_separate, texture_flags_clamps | texture_flags_clampt | texture_flags_eight_bit_alpha, create_render_target_flags_hdr);
  render_buffer_1 = material_system->create_named_render_target_texture_ex(
    "monolilth_glow_buffer_1", screen.x, screen.y, rt_size_literal, image_format_rgba8888,
    material_rt_depth_separate, texture_flags_clamps | texture_flags_clampt | texture_flags_eight_bit_alpha, create_render_target_flags_hdr);

  mat_halo = find_engine_material("dev/halo_add_to_screen");
  mat_blur_x = find_engine_material("dev/blurfilterx");
  mat_blur_y = find_engine_material("dev/blurfiltery");
  const bool textures_bound = bind_base_texture(mat_halo, render_buffer_0) &&
    bind_base_texture(mat_blur_x, render_buffer_0) && bind_base_texture(mat_blur_y, render_buffer_1);
  if (mat_blur_y != nullptr) {
    bool found = false;
    bloom_amount = mat_blur_y->find_var("$bloomamount", &found, false);
    if (!found) bloom_amount = nullptr;
  }
  resources_ready = mat_glow_color != nullptr && mat_halo != nullptr && mat_blur_x != nullptr &&
    mat_blur_y != nullptr && render_buffer_0 != nullptr && render_buffer_1 != nullptr && textures_bound;
  if (resources_ready) {
    resource_width = static_cast<int>(screen.x);
    resource_height = static_cast<int>(screen.y);
  } else {
    retire_resources();
  }
  return resources_ready;
}

void draw_layer(const std::vector<chams_layer>& layers, const float distance, const bool visible_pass,
  const bool two_models, RenderContext* context, void* instance, const DrawModelState& state, const ModelRenderInfo& info, matrix_3x4* bones)
{
  for (const chams_layer& layer : layers) {
    RGBA_float color = layer.color;
    if (!apply_distance_alpha(distance, layer.start, layer.end, layer.smooth_alpha, color)) continue;
    if (layer.material.empty() || layer.material.data() == nullptr) {
      continue;
    }
    const auto definition = materials.find(layer.material);
    materials.set_color(definition ? &*definition : nullptr, color);
    model_render->forced_material_override(definition ? definition->material : nullptr);
    if (definition && definition->invert_cull) context->set_cull_mode(MATERIAL_CULLMODE_CW);
    if (visible_pass && two_models && definition && definition->block_occluded) context->set_stencil_zfail_mode(STENCILOPERATION_REPLACE);
    rendering_effect = true;
    call_original(instance, state, info, bones);
    rendering_effect = false;
    if (definition && definition->invert_cull) context->set_cull_mode(MATERIAL_CULLMODE_CCW);
    if (visible_pass && two_models && definition && definition->block_occluded) context->set_stencil_zfail_mode(STENCILOPERATION_KEEP);
  }
}

void draw_chams(void* instance, const DrawModelState& state, const ModelRenderInfo& info, matrix_3x4* bones,
  const chams_settings& settings, const float distance, const bool ignore_z = false)
{
  render_context_scope context_scope{material_system};
  RenderContext* context = context_scope.get();
  if (context == nullptr || model_render == nullptr || render_view == nullptr) return;

  const bool has_occluded = !settings.occluded.empty();
  const bool separate_layers = !settings.visible.empty() && has_occluded && settings.visible != settings.occluded;
  RGBA_float original_color{};
  render_view->get_color_modulation(&original_color);
  const float original_blend = render_view->get_blend();
  Material* original_material = nullptr;
  OverrideType original_override = OVERRIDE_NORMAL;
  model_render->get_material_override(&original_material, &original_override);

  if (!settings.visible.empty()) {
    if (separate_layers) set_stencil(context, 1, 0xFF, 0x0, STENCILCOMPARISONFUNCTION_ALWAYS, STENCILOPERATION_REPLACE, STENCILOPERATION_KEEP, STENCILOPERATION_KEEP);
    if (ignore_z) context->set_depth_range(0.0f, 0.2f);
    draw_layer(settings.visible, distance, true, separate_layers, context, instance, state, info, bones);
    if (ignore_z) context->set_depth_range(0.0f, 1.0f);
    if (separate_layers) context->set_stencil_enable(false);
  }
  if (has_occluded) {
    if (separate_layers) set_stencil(context, 0, 0x0, 0xFF, STENCILCOMPARISONFUNCTION_EQUAL, STENCILOPERATION_KEEP, STENCILOPERATION_KEEP, STENCILOPERATION_KEEP);
    context->set_depth_range(0.0f, 0.2f);
    draw_layer(settings.occluded, distance, false, separate_layers, context, instance, state, info, bones);
    context->set_depth_range(0.0f, 1.0f);
    if (separate_layers) context->set_stencil_enable(false);
  }

  context->set_cull_mode(MATERIAL_CULLMODE_CCW);
  render_view->set_color_modulation(&original_color);
  render_view->set_blend(original_blend);
  model_render->forced_material_override(original_material, original_override);
}

void store_glow(Entity* entity, const glow_settings& settings, const float distance,
  const DrawModelState* state = nullptr, const ModelRenderInfo* info = nullptr, matrix_3x4* bones = nullptr)
{
  if (entity == nullptr || state == nullptr || info == nullptr) return;
  RGBA_float color = settings.color;
  if (!apply_distance_alpha(distance, settings.start, settings.end, settings.smooth_alpha, color)) return;
  auto iterator = std::ranges::find_if(glow_batches, [&settings](const glow_batch& batch) { return batch.settings == settings; });
  if (iterator == glow_batches.end()) {
    glow_batches.push_back({settings, {}});
    iterator = std::prev(glow_batches.end());
  }
  if (std::ranges::find_if(iterator->entities, [entity, info, bones](const glow_entity& item) {
    return item.entity == entity && item.info.model == info->model && item.bones == bones;
  }) == iterator->entities.end()) {
    glow_entity item{entity, color, *state, *info, bones};
    iterator->entities.push_back(item);
  }
}

void draw_glow_entities(const glow_batch& batch, const int width, const int height, RenderContext* context)
{
  context->push_render_target_and_viewport();
  context->set_render_target(render_buffer_0);
  context->viewport(0, 0, width, height);
  context->clear_color4ub(0, 0, 0, 0);
  context->set_stencil_enable(false);
  context->clear_buffers(true, true, true);
  model_render->forced_material_override(mat_glow_color);
  for (const glow_entity& item : batch.entities) {
    if (item.entity == nullptr) continue;
    materials.set_color(nullptr, item.color);
    rendering_effect = true;
    call_original(model_render, item.state, item.info, item.bones);
    rendering_effect = false;
  }
  context->pop_render_target_and_viewport();

  if (batch.settings.blur > 0.0f) {
    if (bloom_amount != nullptr) bloom_amount->set_float_value(batch.settings.blur);
    context->push_render_target_and_viewport();
    context->viewport(0, 0, width, height);
    context->set_render_target(render_buffer_1);
    context->set_stencil_enable(false);
    context->clear_color4ub(0, 0, 0, 0);
    context->clear_buffers(true, false, false);
    context->draw_screen_space_rectangle(mat_blur_x, 0, 0, width, height, 0.0f, 0.0f, width - 1, height - 1, width, height);
    context->set_render_target(render_buffer_0);
    context->draw_screen_space_rectangle(mat_blur_y, 0, 0, width, height, 0.0f, 0.0f, width - 1, height - 1, width, height);
    context->pop_render_target_and_viewport();
  }

  if (batch.settings.stencil > 0) {
    const int side = (batch.settings.stencil + 1) / 2;
    context->draw_screen_space_rectangle(mat_halo, -side, 0, width, height, 0.0f, 0.0f, width - 1, height - 1, width, height);
    context->draw_screen_space_rectangle(mat_halo, side, 0, width, height, 0.0f, 0.0f, width - 1, height - 1, width, height);
    context->draw_screen_space_rectangle(mat_halo, 0, -side, width, height, 0.0f, 0.0f, width - 1, height - 1, width, height);
    context->draw_screen_space_rectangle(mat_halo, 0, side, width, height, 0.0f, 0.0f, width - 1, height - 1, width, height);
    const int corner = batch.settings.stencil / 2;
    if (corner > 0) {
      context->draw_screen_space_rectangle(mat_halo, -corner, -corner, width, height, 0.0f, 0.0f, width - 1, height - 1, width, height);
      context->draw_screen_space_rectangle(mat_halo, corner, corner, width, height, 0.0f, 0.0f, width - 1, height - 1, width, height);
      context->draw_screen_space_rectangle(mat_halo, corner, -corner, width, height, 0.0f, 0.0f, width - 1, height - 1, width, height);
      context->draw_screen_space_rectangle(mat_halo, -corner, corner, width, height, 0.0f, 0.0f, width - 1, height - 1, width, height);
    }
  }
  if (batch.settings.blur > 0.0f) context->draw_screen_space_rectangle(mat_halo, 0, 0, width, height, 0.0f, 0.0f, width - 1, height - 1, width, height);
  context->set_stencil_enable(false);
}

void draw_backtrack_effects(void* instance, const DrawModelState& state, const ModelRenderInfo& info,
  Player* player, const visual_group_backtrack_settings& settings, const float distance)
{
  if (player == nullptr || !settings.active() || !backtrack::is_enabled()) return;
  const backtrack::backtrack_record_view records = backtrack::valid_records(player);
  if (records.count <= 0) return;

  int first = 0;
  int last = 1;
  if (settings.record_mode == 1) {
    first = records.count - 1;
    last = records.count;
  } else if (settings.record_mode == 2) {
    last = records.count;
  }

  for (int index = first; index < last; ++index) {
    const backtrack::backtrack_record* record = records.records[static_cast<std::size_t>(index)];
    if (record == nullptr || record->bone_count <= 0) continue;
    ModelRenderInfo record_info = info;
    record_info.origin = record->origin;
    auto* record_bones = const_cast<matrix_3x4*>(record->bones.data());
    if (settings.chams.active()) {
      draw_chams(instance, state, record_info, record_bones, settings.chams, distance, settings.ignore_z);
    }
    if (settings.glow.active()) {
      store_glow(player, settings.glow, distance, &state, &record_info, record_bones);
    }
  }
}

}

void on_render_start()
{
  glow_batches.clear();
  if (visual_groups::groups_need_model_effects()) ensure_resources();
}

void on_render_end()
{
  if (glow_batches.empty() || !ensure_resources() || engine == nullptr || model_render == nullptr || render_view == nullptr) {
    glow_batches.clear();
    return;
  }
  render_context_scope context_scope{material_system};
  RenderContext* context = context_scope.get();
  const Vec2 screen = engine->get_screen_size();
  if (context == nullptr || screen.x <= 0 || screen.y <= 0) {
    glow_batches.clear();
    return;
  }

  RGBA_float original_color{};
  render_view->get_color_modulation(&original_color);
  const float original_blend = render_view->get_blend();
  Material* original_material = nullptr;
  OverrideType original_override = OVERRIDE_NORMAL;
  model_render->get_material_override(&original_material, &original_override);
  for (const glow_batch& batch : glow_batches) draw_glow_entities(batch, screen.x, screen.y, context);
  model_render->forced_material_override(original_material, original_override);
  render_view->set_color_modulation(&original_color);
  render_view->set_blend(original_blend);
  glow_batches.clear();
}

void on_draw_model_execute(void* instance, const DrawModelState& state, const ModelRenderInfo& info, matrix_3x4* bones)
{
  if (draw_model_execute_original == nullptr) return;
  if (rendering_effect || model_render == nullptr || !materials.loaded() || entity_list == nullptr ||
      engine == nullptr || engine->is_drawing_loading_image()) {
    call_original(instance, state, info, bones);
    return;
  }
  Entity* entity = info.entity_index > 0 ? entity_list->entity_from_index(static_cast<unsigned int>(info.entity_index)) : nullptr;
  const auto match = visual_groups::group_for_entity(entity, true);
  if (!match) {
    call_original(instance, state, info, bones);
    return;
  }

  const float distance = distance_for(entity);
  if (match->chams.active()) draw_chams(instance, state, info, bones, match->chams, distance);
  else call_original(instance, state, info, bones);
  if (match->glow.active() && resources_ready) store_glow(entity, match->glow, distance, &state, &info, bones);
  if (match->backtrack_visuals.active() && entity != nullptr && entity->get_class_id() == class_id::PLAYER) {
    draw_backtrack_effects(instance, state, info, reinterpret_cast<Player*>(entity), match->backtrack_visuals, distance);
  }
}

void on_shutdown(const bool release_graphics_resources)
{
  glow_batches.clear();
  if (release_graphics_resources) {
    release_resources();
    materials.shutdown();
  } else {
    reset_resource_handles();
    materials.abandon();
  }
}

}
