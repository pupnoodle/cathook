/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/games/tf2/sdk/materials/material.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef MATERIAL_HPP
#define MATERIAL_HPP

#include "core/types.hpp"

#include "material_var.hpp"

class Material {
public:

  const char* get_name() const {
    void** vtable = *(void***)const_cast<Material*>(this);
    return ((const char* (*)(const void*))vtable[0])(this);
  }

  const char* get_texture_group_name() const {
    void** vtable = *(void***)const_cast<Material*>(this);
    return ((const char* (*)(const void*))vtable[1])(this);
  }

  MaterialVar* find_var(const char* var_name, bool* found = nullptr, bool complain = false) {
    void** vtable = *(void***)this;

    MaterialVar* (*find_var_fn)(void*, const char*, bool*, bool) = (MaterialVar* (*)(void*, const char*, bool*, bool))vtable[11];

    return find_var_fn(this, var_name, found, complain);
  }

  void increment_reference_count(void) {
    void** vtable = *(void***)this;

    void (*increment_reference_count_fn)(void*) = (void (*)(void*))vtable[12];

    increment_reference_count_fn(this);
  }

  void decrement_reference_count(void) {
    void** vtable = *(void***)this;

    void (*decrement_reference_count_fn)(void*) = (void (*)(void*))vtable[13];

    decrement_reference_count_fn(this);
  }

  void alpha_modulate(float alpha) {
    void** vtable = *(void***)this;

    void (*alpha_modulate_fn)(void*, float) = (void (*)(void*, float))vtable[27];

    alpha_modulate_fn(this, alpha);
  }

  float get_alpha_modulation() const {
    void** vtable = *(void***)const_cast<Material*>(this);
    return ((float (*)(void*))vtable[43])(const_cast<Material*>(this));
  }

  void get_color_modulation(float* r, float* g, float* b) const {
    void** vtable = *(void***)const_cast<Material*>(this);
    ((void (*)(void*, float*, float*, float*))vtable[44])(const_cast<Material*>(this), r, g, b);
  }

  void color_modulate(RGBA_float color) {
    color = color.resolved();
    void** vtable = *(void***)this;

    void (*color_modulate_fn)(void*, float, float, float) = (void (*)(void*, float, float, float))vtable[28];

    color_modulate_fn(this, color.r, color.g, color.b);
  }

  void set_material_flag(int flag, bool on) {
    void** vtable = *(void***)this;

    void (*set_material_flag_fn)(void*, int, bool) = (void (*)(void*, int, bool))vtable[29];

    set_material_flag_fn(this, flag, on);
  }

  bool get_material_flag(int flag) {
    void** vtable = *(void***)this;

    bool (*get_material_flag_fn)(void*, int) = (bool (*)(void*, int))vtable[30];

    return get_material_flag_fn(this, flag);
  }

  bool is_error_material() const {
    void** vtable = *(void***)const_cast<Material*>(this);

    auto is_error_material_fn = (bool (*)(const void*))vtable[42];
    return is_error_material_fn(this);
  }

  void delete_if_unreferenced(void) {
    void** vtable = *(void***)this;

    void (*delete_if_unreferenced_fn)(void*) = (void (*)(void*))vtable[50];

    delete_if_unreferenced_fn(this);
  }
};

#endif
