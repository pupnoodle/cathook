/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/memory/memory.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef MEMORY_HPP
#define MEMORY_HPP

#include <string>
#include <fstream>
#include <sstream>

#include "../print.hpp"

static void* get_module_base_address(const std::string& module_name) {
  std::ifstream file{"/proc/self/maps"};
  std::string line;
  while (std::getline(file, line)) {
    if (line.find(module_name) == std::string::npos) {
      continue;
    }
    const auto dash = line.find('-');
    if (dash == std::string::npos || dash == 0) {
      continue;
    }
    unsigned long result = 0;
    std::stringstream ss;
    ss << std::hex << line.substr(0, dash);
    ss >> result;
    if (result != 0) {
      return (void*)result;
    }
    return nullptr;
  }
  return nullptr;
}

#endif
