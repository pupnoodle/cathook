/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/games/tf2/sdk/materials/material_var.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef MATERIAL_VAR_HPP
#define MATERIAL_VAR_HPP

#include "core/types.hpp"

class Texture;

class MaterialVar {
public:
  void set_float_value(float value) {
    void** vtable = *(void***)this;

    void (*set_float_value_fn)(void*, float) = (void (*)(void*, float))vtable[3];

    set_float_value_fn(this, value);
  }

  void set_vec_value(RGBA_float color) {
    color = color.resolved();
    void** vtable = *(void***)this;

    void (*set_vec_value_fn)(void*, float, float, float) = (void (*)(void*, float, float, float))vtable[11];

    set_vec_value_fn(this, color.r, color.g, color.b);
  }
};

#endif
