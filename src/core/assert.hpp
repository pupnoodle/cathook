/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/assert.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include <SDL2/SDL.h>

#include "core/logger.hpp"

#define error_box(message) \
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,			\
			   "Fatal Error",				\
			   message,					\
			   nullptr);

#define error_assert(expression, message)				\
  if (expression) {							\
    puphook::core::log_raw("fatal error: ");				\
    puphook::core::log_raw(message);					\
    error_box(message);							\
    if (puphook::core::game_hooks_installed.load(std::memory_order_acquire)) { \
      puphook::core::request_detach();					\
    } else {								\
      puphook::core::abort_module_runtime_init();			\
    }									\
    return false;							\
  }
