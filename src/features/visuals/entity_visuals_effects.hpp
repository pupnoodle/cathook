namespace entity_visuals
{

using draw_model_execute_fn = void (*)(void*, const DrawModelState&, const ModelRenderInfo&, matrix_3x4*);

extern draw_model_execute_fn draw_model_execute_original;

void on_render_start();
void on_render_end();
void on_draw_model_execute(void* model_render_instance, const DrawModelState& state, const ModelRenderInfo& info, matrix_3x4* bones);
void on_shutdown(bool release_graphics_resources = true);

}
