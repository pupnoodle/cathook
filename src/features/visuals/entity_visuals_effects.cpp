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
thread_local unsigned int rendering_effect_depth = 0;

class rendering_effect_scope final {
public:
  rendering_effect_scope() { ++rendering_effect_depth; }
  rendering_effect_scope(const rendering_effect_scope&) = delete;
  rendering_effect_scope& operator=(const rendering_effect_scope&) = delete;
  ~rendering_effect_scope() {
    if (rendering_effect_depth > 0) --rendering_effect_depth;
  }
};
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
bool bloom_amount_initialized = false;

constexpr float glow_scale_max = 10.0f;
constexpr float glow_blur_max = 100.0f;

[[nodiscard]] float normalized_glow_scale(const float scale)
{
  const float normalized = std::clamp(scale / glow_scale_max, 0.0f, 1.0f);
  return normalized * normalized;
}

[[nodiscard]] float blur_amount(const float scale)
{
  return std::clamp(scale, 0.0f, glow_blur_max);
}

[[nodiscard]] int stencil_radius(const float scale)
{
  const float normalized = normalized_glow_scale(scale);
  return std::clamp(static_cast<int>(std::ceil(normalized * glow_scale_max)), 0, static_cast<int>(glow_scale_max));
}

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

[[nodiscard]] bool is_entity_model(Entity* entity, const ModelRenderInfo& info)
{
  if (entity == nullptr || info.renderable == nullptr || info.model == nullptr) return false;
  return info.renderable == entity->get_renderable();
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

void reset_resource_handles()
{
  mat_glow_color = nullptr;
  mat_halo = nullptr;
  mat_blur_x = nullptr;
  mat_blur_y = nullptr;
  bloom_amount = nullptr;
  bloom_amount_initialized = false;
  render_buffer_0 = nullptr;
  render_buffer_1 = nullptr;
  resource_width = 0;
  resource_height = 0;
  resources_ready = false;
}

void release_resources()
{
  auto release_material = [](Material* material) {
    if (material == nullptr) return;
    material->decrement_reference_count();
  };
  auto release_texture = [](Texture* texture) {
    if (texture == nullptr) return;
    texture->decrement_reference_count();
  };
  release_material(mat_glow_color);
  release_material(mat_halo);
  release_material(mat_blur_x);
  release_material(mat_blur_y);
  release_texture(render_buffer_0);
  release_texture(render_buffer_1);
  for (Material* material : retired_materials) {
    release_material(material);
  }
  for (Texture* texture : retired_textures) {
    release_texture(texture);
  }
  retired_materials.clear();
  retired_textures.clear();
  reset_resource_handles();
}

Material* find_engine_material(const char* name)
{
  if (material_system == nullptr || name == nullptr) return nullptr;
  Material* material = material_system->find_material(name, "Other", false, nullptr);
  if (material == nullptr || material->is_error_material()) return nullptr;
  material->increment_reference_count();
  return material;
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
  if (nographics::is_enabled()) return false;
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

  mat_glow_color = find_engine_material("dev/glow_color");

  render_buffer_0 = material_system->create_named_render_target_texture_ex(
    "monolilth_glow_buffer_0", screen.x, screen.y, rt_size_literal, image_format_rgb888,
    material_rt_depth_shared, texture_flags_clamps | texture_flags_clampt | texture_flags_eight_bit_alpha, create_render_target_flags_hdr);
  render_buffer_1 = material_system->create_named_render_target_texture_ex(
    "monolilth_glow_buffer_1", screen.x, screen.y, rt_size_literal, image_format_rgb888,
    material_rt_depth_shared, texture_flags_clamps | texture_flags_clampt | texture_flags_eight_bit_alpha, create_render_target_flags_hdr);
  if (render_buffer_0 != nullptr) render_buffer_0->increment_reference_count();
  if (render_buffer_1 != nullptr) render_buffer_1->increment_reference_count();

  mat_halo = materials.create_runtime_material(
    "monolilth_glow_halo",
    "\"UnlitGeneric\"\n{\n\t$basetexture \"monolilth_glow_buffer_0\"\n\t$additive \"1\"\n}");
  mat_blur_x = materials.create_runtime_material(
    "monolilth_glow_blur_x",
    "\"BlurFilterX\"\n{\n\t$basetexture \"monolilth_glow_buffer_0\"\n}");
  mat_blur_y = materials.create_runtime_material(
    "monolilth_glow_blur_y",
    "\"BlurFilterY\"\n{\n\t$basetexture \"monolilth_glow_buffer_1\"\n}");
  const bool textures_bound = mat_halo != nullptr && mat_blur_x != nullptr && mat_blur_y != nullptr;
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
    auto definition = materials.find(layer.material);
    if (!definition || definition->material == nullptr) continue;
    materials.set_color(&*definition, color);
    model_render->forced_material_override(definition->material);
    if (definition->invert_cull) context->set_cull_mode(MATERIAL_CULLMODE_CW);
    if (visible_pass && two_models && definition->block_occluded) context->set_stencil_zfail_mode(STENCILOPERATION_REPLACE);
    rendering_effect_scope rendering_scope{};
    call_original(instance, state, info, bones);
    if (definition->invert_cull) context->set_cull_mode(MATERIAL_CULLMODE_CCW);
    if (visible_pass && two_models && definition->block_occluded) context->set_stencil_zfail_mode(STENCILOPERATION_KEEP);
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

bool glow_entity_valid(const glow_entity& item)
{
  if (item.entity == nullptr || entity_list == nullptr || item.info.entity_index <= 0) return false;
  Entity* current = entity_list->entity_from_index(static_cast<unsigned int>(item.info.entity_index));
  if (current != item.entity || current->is_dormant() || !current->should_draw()) return false;
  if (item.info.renderable != current->get_renderable() || item.info.model != current->get_model()) return false;
  if (current->get_class_id() == class_id::PLAYER && !reinterpret_cast<Player*>(current)->is_alive()) return false;
  return true;
}

void draw_glow_entities(const glow_batch& batch, const int width, const int height, RenderContext* context,
  Material* original_material, const OverrideType original_override)
{
  std::vector<const glow_entity*> valid_entities{};
  valid_entities.reserve(batch.entities.size());
  for (const glow_entity& item : batch.entities) {
    if (glow_entity_valid(item)) valid_entities.emplace_back(&item);
  }
  if (valid_entities.empty()) {
    context->push_render_target_and_viewport();
    context->set_render_target(render_buffer_0);
    context->viewport(0, 0, width, height);
    context->clear_color4ub(0, 0, 0, 0);
    context->clear_buffers(true, true, false);
    context->pop_render_target_and_viewport();
    return;
  }

  RGBA_float original_color{};
  render_view->get_color_modulation(&original_color);
  const float original_blend = render_view->get_blend();
  const RGBA_float white{.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f};

  context->set_stencil_enable(false);
  context->set_stencil_write_mask(0xFF);
  context->set_stencil_test_mask(0xFF);
  context->clear_buffers(false, false, true);
  model_render->forced_material_override(mat_glow_color);
  context->set_stencil_enable(true);
  context->set_stencil_reference_count(1);
  context->set_stencil_write_mask(0xFF);
  context->set_stencil_test_mask(0x0);
  context->set_stencil_compare_mode(STENCILCOMPARISONFUNCTION_ALWAYS);
  context->set_stencil_pass_mode(STENCILOPERATION_REPLACE);
  context->set_stencil_fail_mode(STENCILOPERATION_KEEP);
  context->set_stencil_zfail_mode(STENCILOPERATION_REPLACE);
  render_view->set_color_modulation(&white);
  render_view->set_blend(0.0f);
  for (const glow_entity* item : valid_entities) {
    rendering_effect_scope rendering_scope{};
    call_original(model_render, item->state, item->info, item->bones);
  }

  context->push_render_target_and_viewport();
  context->set_render_target(render_buffer_0);
  context->viewport(0, 0, width, height);
  context->clear_color4ub(0, 0, 0, 0);
  context->set_stencil_enable(false);
  context->clear_buffers(true, true, false);
  model_render->forced_material_override(mat_glow_color);
  for (const glow_entity* item : valid_entities) {
    materials.set_color(nullptr, item->color);
    rendering_effect_scope rendering_scope{};
    call_original(model_render, item->state, item->info, item->bones);
  }
  context->pop_render_target_and_viewport();

  if (batch.settings.blur > 0.0f) {
    if (!bloom_amount_initialized && mat_blur_y != nullptr) {
      bool found = false;
      bloom_amount = mat_blur_y->find_var("$bloomamount", &found, false);
      if (!found) bloom_amount = nullptr;
      bloom_amount_initialized = true;
    }
    if (bloom_amount != nullptr) bloom_amount->set_float_value(blur_amount(batch.settings.blur));
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

  // The render target already contains the entity colors. Composite it without
  // inheriting the last entity's modulation or the caller's blend state.
  render_view->set_color_modulation(&white);
  render_view->set_blend(1.0f);
  context->set_stencil_enable(true);
  context->set_stencil_reference_count(0);
  context->set_stencil_write_mask(0x0);
  context->set_stencil_test_mask(0xFF);
  context->set_stencil_compare_mode(STENCILCOMPARISONFUNCTION_EQUAL);
  context->set_stencil_pass_mode(STENCILOPERATION_KEEP);
  context->set_stencil_fail_mode(STENCILOPERATION_KEEP);
  context->set_stencil_zfail_mode(STENCILOPERATION_KEEP);
  auto draw_halo = [context, width, height](const int x, const int y) {
    context->draw_screen_space_rectangle(mat_halo, x, y, width, height, 0.0f, 0.0f, width - 1, height - 1, width, height);
  };
  if (batch.settings.stencil > 0) {
    const int side = (stencil_radius(batch.settings.stencil) + 1) / 2;
    draw_halo(-side, 0);
    draw_halo(side, 0);
    draw_halo(0, -side);
    draw_halo(0, side);
    const int corner = stencil_radius(batch.settings.stencil) / 2;
    if (corner > 0) {
      draw_halo(-corner, -corner);
      draw_halo(corner, corner);
      draw_halo(corner, -corner);
      draw_halo(-corner, corner);
    }
  }
  if (batch.settings.blur > 0.0f) draw_halo(0, 0);
  context->set_stencil_enable(false);
  model_render->forced_material_override(original_material, original_override);
  render_view->set_color_modulation(&original_color);
  render_view->set_blend(original_blend);
}

void draw_backtrack_effects(void* instance, const DrawModelState& state, const ModelRenderInfo& info,
  Player* player, const visual_group_backtrack_settings& settings, const float distance)
{
  if (player == nullptr || !settings.active() || !backtrack::is_enabled()) return;
  const backtrack::backtrack_record_view records = backtrack::visual_records(player);
  if (records.count <= 0) return;

  int first = 0;
  int last = 1;
  if (settings.record_mode == 0) {
    first = records.count - 1;
    last = records.count;
  } else if (settings.record_mode == 1) {
    first = 0;
    last = 1;
  } else if (settings.record_mode == 2) {
    first = 0;
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
  if (nographics::is_enabled()) return;
  if (visual_groups::groups_need_model_effects()) ensure_resources();
}

void on_post_screen_space_effects()
{
  if (nographics::is_enabled()) {
    glow_batches.clear();
    return;
  }
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
  for (const glow_batch& batch : glow_batches) {
    draw_glow_entities(batch, screen.x, screen.y, context, original_material, original_override);
  }
  model_render->forced_material_override(original_material, original_override);
  render_view->set_color_modulation(&original_color);
  render_view->set_blend(original_blend);
  glow_batches.clear();
}

void on_render_end()
{
  glow_batches.clear();
}

bool is_rendering_effect()
{
  return rendering_effect_depth != 0;
}

void on_draw_model_execute(void* instance, const DrawModelState& state, const ModelRenderInfo& info, matrix_3x4* bones)
{
  if (draw_model_execute_original == nullptr) return;
  if (nographics::is_enabled()) {
    call_original(instance, state, info, bones);
    return;
  }
  if (is_rendering_effect() || model_render == nullptr || !materials.loaded() || entity_list == nullptr ||
      engine == nullptr || engine->is_drawing_loading_image()) {
    call_original(instance, state, info, bones);
    return;
  }
  Entity* entity = info.entity_index > 0 ? entity_list->entity_from_index(static_cast<unsigned int>(info.entity_index)) : nullptr;
  if (!is_entity_model(entity, info)) {
    call_original(instance, state, info, bones);
    return;
  }
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
