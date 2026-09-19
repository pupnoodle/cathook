/*
/^-----^\   data: 2026-04-02
V  o o  V  file: src/core/diagnostics/exception_handler.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#pragma once

#include <filesystem>

namespace puphook::core
{

class exception_handler
{
public:
    static void install(const std::filesystem::path& log_file_path);
    static void uninstall();
};

}
