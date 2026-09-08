/*
/^-----^\   data: 2026-05-06
V  o o  V  file: src/games/tf2/sdk/materials/texture.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef TEXTURE_HPP
#define TEXTURE_HPP

class Texture {
public:
  void increment_reference_count(void) {
    void** vtable = *(void***)this;

    auto increment_reference_count_fn = (void (*)(void*))vtable[10];
    increment_reference_count_fn(this);
  }

  void decrement_reference_count(void) {
    void** vtable = *(void***)this;

    auto decrement_reference_count_fn = (void (*)(void*))vtable[11];
    decrement_reference_count_fn(this);
  }
};

#endif
