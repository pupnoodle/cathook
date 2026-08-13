/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/games/tf2/sdk/interfaces/render_context.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef RENDER_CONTEXT_HPP
#define RENDER_CONTEXT_HPP

class Material;
class Texture;

enum MaterialCullMode {
  MATERIAL_CULLMODE_CCW,
  MATERIAL_CULLMODE_CW
};

enum StencilComparisonMode {
  STENCILCOMPARISONFUNCTION_NEVER = 1,
  STENCILCOMPARISONFUNCTION_LESS = 2,
  STENCILCOMPARISONFUNCTION_EQUAL = 3,
  STENCILCOMPARISONFUNCTION_LESSEQUAL = 4,
  STENCILCOMPARISONFUNCTION_GREATER = 5,
  STENCILCOMPARISONFUNCTION_NOTEQUAL = 6,
  STENCILCOMPARISONFUNCTION_GREATEREQUAL = 7,
  STENCILCOMPARISONFUNCTION_ALWAYS = 8,
  STENCILCOMPARISONFUNCTION_FORCE_DWORD = 0x7fffffff
};

enum StencilOperation {
  STENCILOPERATION_KEEP = 1,
  STENCILOPERATION_ZERO = 2,
  STENCILOPERATION_REPLACE = 3,
  STENCILOPERATION_INCRSAT = 4,
  STENCILOPERATION_DECRSAT = 5,
  STENCILOPERATION_INVERT = 6,
  STENCILOPERATION_INCR = 7,
  STENCILOPERATION_DECR = 8,
  STENCILOPERATION_FORCE_DWORD = 0x7fffffff
};

class RenderContext {
public:
  void release() {
    void** vtable = *(void***)this;
    auto release_fn = (int (*)(void*))vtable[1];
    release_fn(this);
  }

  void begin_render() {
    void** vtable = *(void***)this;
    auto begin_render_fn = (void (*)(void*))vtable[2];
    begin_render_fn(this);
  }

  void end_render() {
    void** vtable = *(void***)this;
    auto end_render_fn = (void (*)(void*))vtable[3];
    end_render_fn(this);
  }

  void set_render_target(Texture* texture) {
    void** vtable = *(void***)this;

    void (*set_render_target_fn)(void*, Texture*) = (void (*)(void*, Texture*))vtable[6];

    set_render_target_fn(this, texture);
  }

  void set_depth_range(float near, float far) {
    void** vtable = *(void***)this;

    void (*set_depth_range_fn)(void*, float, float) = (void (*)(void*, float, float))vtable[11];

    set_depth_range_fn(this, near, far);
  }

  void get_fog_distances(float* start, float* end) {
    void** vtable = *(void***)this;
    ((void (*)(void*, float*, float*, float*))vtable[137])(this, start, end, nullptr);
  }

  void fog_start(float value) {
    void** vtable = *(void***)this;
    ((void (*)(void*, float))vtable[42])(this, value);
  }

  void fog_end(float value) {
    void** vtable = *(void***)this;
    ((void (*)(void*, float))vtable[43])(this, value);
  }

  void fog_color3ub(unsigned char r, unsigned char g, unsigned char b) {
    void** vtable = *(void***)this;
    ((void (*)(void*, unsigned char, unsigned char, unsigned char))vtable[48])(this, r, g, b);
  }

  void set_stencil_test_mask(int mask) {
    void** vtable = *(void***)this;

    void (*set_stencil_test_fn)(void*, int) = (void (*)(void*, int))vtable[123];

    set_stencil_test_fn(this, mask);
  }

  void set_stencil_write_mask(int mask) {
    void** vtable = *(void***)this;

    void (*set_stencil_write_fn)(void*, int) = (void (*)(void*, int))vtable[124];

    set_stencil_write_fn(this, mask);
  }

  void set_stencil_reference_count(int reference_count) {
    void** vtable = *(void***)this;

    void (*set_stencil_reference_count_fn)(void*, int) = (void (*)(void*, int))vtable[122];

    set_stencil_reference_count_fn(this, reference_count);
  }

  void set_stencil_fail_mode(StencilOperation stencil_operation) {
    void** vtable = *(void***)this;

    void (*set_stencil_fail_mode_fn)(void*, StencilOperation) = (void (*)(void*, StencilOperation))vtable[118];

    set_stencil_fail_mode_fn(this, stencil_operation);
  }

  void set_stencil_zfail_mode(StencilOperation stencil_operation) {
    void** vtable = *(void***)this;

    void (*set_stencil_zfail_mode_fn)(void*, StencilOperation) = (void (*)(void*, StencilOperation))vtable[119];

    set_stencil_zfail_mode_fn(this, stencil_operation);
  }

  void set_stencil_pass_mode(StencilOperation stencil_operation) {
    void** vtable = *(void***)this;

    void (*set_stencil_pass_mode_fn)(void*, StencilOperation) = (void (*)(void*, StencilOperation))vtable[120];

    set_stencil_pass_mode_fn(this, stencil_operation);
  }

  void set_stencil_compare_mode(StencilComparisonMode stencil_compare_mode) {
    void** vtable = *(void***)this;

    void (*set_stencil_compare_mode_fn)(void*, StencilComparisonMode) = (void (*)(void*, StencilComparisonMode))vtable[121];

    set_stencil_compare_mode_fn(this, stencil_compare_mode);
  }

  void clear_buffers(bool clear_color, bool clear_depth, bool clear_stencil) {
    void** vtable = *(void***)this;

    void (*clear_buffers_fn)(void*, bool, bool, bool) = (void (*)(void*, bool, bool, bool))vtable[12];

    clear_buffers_fn(this, clear_color, clear_depth, clear_stencil);
  }

  void viewport(int x, int y, int width, int height) {
    void** vtable = *(void***)this;

    void (*viewport_fn)(void*, int, int, int, int) = (void (*)(void*, int, int, int, int))vtable[38];

    viewport_fn(this, x, y, width, height);
  }

  void clear_color4ub(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    void** vtable = *(void***)this;

    void (*clear_color4ub_fn)(void*, unsigned char, unsigned char, unsigned char, unsigned char) =
      (void (*)(void*, unsigned char, unsigned char, unsigned char, unsigned char))vtable[73];

    clear_color4ub_fn(this, r, g, b, a);
  }

  void draw_screen_space_rectangle(
    Material* material,
    int dest_x,
    int dest_y,
    int width,
    int height,
    float src_texture_x0,
    float src_texture_y0,
    float src_texture_x1,
    float src_texture_y1,
    int src_texture_width,
    int src_texture_height,
    void* client_renderable = nullptr,
    int x_dice = 1,
    int y_dice = 1) {
    void** vtable = *(void***)this;

    auto draw_screen_space_rectangle_fn =
      (void (*)(
        void*,
        Material*,
        int,
        int,
        int,
        int,
        float,
        float,
        float,
        float,
        int,
        int,
        void*,
        int,
        int))vtable[103];

    draw_screen_space_rectangle_fn(
      this,
      material,
      dest_x,
      dest_y,
      width,
      height,
      src_texture_x0,
      src_texture_y0,
      src_texture_x1,
      src_texture_y1,
      src_texture_width,
      src_texture_height,
      client_renderable,
      x_dice,
      y_dice);
  }

  void push_render_target_and_viewport(void) {
    void** vtable = *(void***)this;

    void (*push_render_target_and_viewport_fn)(void*) = (void (*)(void*))vtable[105];

    push_render_target_and_viewport_fn(this);
  }

  void pop_render_target_and_viewport(void) {
    void** vtable = *(void***)this;

    void (*pop_render_target_and_viewport_fn)(void*) = (void (*)(void*))vtable[109];

    pop_render_target_and_viewport_fn(this);
  }

  void set_stencil_enable(bool value) {
    void** vtable = *(void***)this;

    void (*set_stencil_enable_fn)(void*, bool) = (void (*)(void*, bool))vtable[117];

    set_stencil_enable_fn(this, value);
  }

  void set_cull_mode(MaterialCullMode cull_mode) {
    void** vtable = *(void***)this;

    void (*set_cull_mode_fn)(void*, MaterialCullMode) = (void (*)(void*, MaterialCullMode))vtable[40];

    set_cull_mode_fn(this, cull_mode);
  }

};
#endif
