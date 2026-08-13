/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/games/tf2/sdk/interfaces/material_system.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef MATERIAL_SYSTEM_HPP
#define MATERIAL_SYSTEM_HPP

#include "render_context.hpp"

#include "games/tf2/sdk/materials/material.hpp"
#include "games/tf2/sdk/materials/keyvalues.hpp"
#include "games/tf2/sdk/materials/texture.hpp"

enum image_format {
  image_format_unknown = -1,
  image_format_rgba8888 = 0,
  image_format_abgr8888,
  image_format_rgb888,
};

enum material_render_target_depth {
  material_rt_depth_shared = 0,
  material_rt_depth_separate,
  material_rt_depth_none,
  material_rt_depth_only,
};

enum render_target_size_mode {
  rt_size_no_change = 0,
  rt_size_default,
  rt_size_picmip,
  rt_size_hdr,
  rt_size_full_frame_buffer,
  rt_size_offscreen,
  rt_size_full_frame_buffer_rounded_up,
  rt_size_replay_screenshot,
  rt_size_literal,
  rt_size_literal_picmip,
};

constexpr unsigned int texture_flags_clamps = 0x00000004;
constexpr unsigned int texture_flags_clampt = 0x00000008;
constexpr unsigned int texture_flags_eight_bit_alpha = 0x00002000;
constexpr unsigned int create_render_target_flags_hdr = 0x00000001;

class MaterialSystem {
public:

  using material_handle = unsigned short;
  static constexpr material_handle invalid_material_handle = static_cast<material_handle>(0xFFFF);

  static constexpr unsigned int first_material_vtable_index = 73;
  static constexpr unsigned int next_material_vtable_index = 74;
  static constexpr unsigned int invalid_material_vtable_index = 75;
  static constexpr unsigned int get_material_vtable_index = 76;
  static constexpr unsigned int set_in_stub_mode_vtable_index = 58;
  static constexpr unsigned int create_named_render_target_texture_ex_vtable_index = 85;
  static constexpr unsigned int override_render_target_allocation_vtable_index = 127;
  static constexpr unsigned int get_render_context_vtable_index = 98;

  Material* find_material(char const* material_name, const char* texture_group_name, bool complain, const char* complain_prefix) {
    void** vtable = *(void ***)this;

    Material* (*find_material_fn)(void*, const char*, const char*, bool, const char*) =
      (Material* (*)(void*, const char*, const char*, bool, const char*))vtable[71];

    return find_material_fn(this, material_name, texture_group_name, complain, complain_prefix);
  }

  Material* create_material(const char* name, KeyValues* key_value) {
    void** vtable = *(void ***)this;

    Material* (*create_material_fn)(void*, const char*, void*) = (Material* (*)(void*, const char*, void*))vtable[70];

    return create_material_fn(this, name, key_value);
  }

  material_handle first_material() const {
    void** vtable = *(void***)this;
    return ((material_handle (*)(const void*))vtable[first_material_vtable_index])(this);
  }

  material_handle next_material(material_handle handle) const {
    void** vtable = *(void***)this;
    return ((material_handle (*)(const void*, material_handle))vtable[next_material_vtable_index])(this, handle);
  }

  material_handle invalid_material() const {
    void** vtable = *(void***)this;
    return ((material_handle (*)(const void*))vtable[invalid_material_vtable_index])(this);
  }

  Material* get_material(material_handle handle) const {
    void** vtable = *(void***)this;
    return ((Material* (*)(const void*, material_handle))vtable[get_material_vtable_index])(this, handle);
  }

  void set_in_stub_mode(bool enabled) {
    void** vtable = *(void***)this;
    auto set_in_stub_mode_fn = (void (*)(void*, bool))vtable[set_in_stub_mode_vtable_index];
    set_in_stub_mode_fn(this, enabled);
  }

  Texture* create_named_render_target_texture_ex(
    const char* render_target_name,
    int width,
    int height,
    render_target_size_mode size_mode,
    image_format format,
    material_render_target_depth depth,
    unsigned int texture_flags,
    unsigned int render_target_flags) {
    void** vtable = *(void***)this;

    auto create_named_render_target_texture_ex_fn =
      (Texture* (*)(
        void*,
        const char*,
        int,
        int,
        render_target_size_mode,
        image_format,
        material_render_target_depth,
        unsigned int,
        unsigned int))vtable[create_named_render_target_texture_ex_vtable_index];

    return create_named_render_target_texture_ex_fn(
      this,
      render_target_name,
      width,
      height,
      size_mode,
      format,
      depth,
      texture_flags,
      render_target_flags);
  }

  void override_render_target_allocation(bool enabled) {
    void** vtable = *(void***)this;

    void (*override_render_target_allocation_fn)(void*, bool) =
      (void (*)(void*, bool))vtable[override_render_target_allocation_vtable_index];

    override_render_target_allocation_fn(this, enabled);
  }

  RenderContext* get_render_context(void) {
    void** vtable = *(void***)this;

    RenderContext* (*get_render_context_fn)(void*) = (RenderContext* (*)(void*))vtable[get_render_context_vtable_index];

    return get_render_context_fn(this);
  }

};

inline static MaterialSystem* material_system;

#endif
