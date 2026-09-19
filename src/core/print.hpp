/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/core/print.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef PRINT_HPP
#define PRINT_HPP

#include "logger.hpp"

#include <cstdarg>

static void print(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  puphook::core::vlog_raw(fmt, args);
  va_end(args);
}

#endif
