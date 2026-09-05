/*
/^-----^\   data: 2026-05-06
V  o o  V  file: src/core/overwrite_dlopen.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include <atomic>
#include <cstring>
#include <dlfcn.h>
#include <string_view>

#include "core/memory/byte_patch.hpp"
#include "core/print.hpp"
#include "core/shared/sigs.hpp"
#include "features/automation/nographics/nographics.hpp"
#include "libsigscan/libsigscan.h"

namespace
{

using dlopen_fn = void* (*)(const char*, int);

bool is_launcher_path(const char* file)
{
  if (file == nullptr || file[0] == '\0')
  {
    return false;
  }

  const std::string_view path{ file };
  const auto slash = path.find_last_of('/');
  const auto name = slash == std::string_view::npos ? path : path.substr(slash + 1);
  return name == "launcher.so";
}

bool is_gameoverlayrenderer_path(const char* file)
{
  if (file == nullptr || file[0] == '\0')
  {
    return false;
  }

  const std::string_view path{ file };
  const auto slash = path.find_last_of('/');
  const auto name = slash == std::string_view::npos ? path : path.substr(slash + 1);
  return name == "gameoverlayrenderer.so";
}

void patch_launcher_source_lock()
{
  static std::atomic_bool patch_attempted = false;
  static byte_patch source_lock_patch{};

  if (patch_attempted.exchange(true, std::memory_order_acq_rel))
  {
    return;
  }

  auto* source_lock_check = sigscan_module("launcher.so", sigs::launcher_source_lock);
  if (source_lock_check == nullptr)
  {
    print("[overwrite_dlopen] launcher source lock signature missing\n");
    return;
  }

  source_lock_patch = byte_patch(source_lock_check, { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 });
  if (!source_lock_patch.apply())
  {
    print("[overwrite_dlopen] failed to patch launcher source lock at %p\n", source_lock_check);
    return;
  }

  print("[overwrite_dlopen] patched launcher source lock at %p\n", source_lock_check);
}

}

extern "C" void* dlopen(const char* file, int mode) __THROWNL
{
  auto* real_dlopen = reinterpret_cast<dlopen_fn>(dlsym(RTLD_NEXT, "dlopen"));
  if (real_dlopen == nullptr)
  {
    return nullptr;
  }

  if (is_gameoverlayrenderer_path(file) && nographics::is_enabled())
  {
    return nullptr;
  }

  const char* redirected_file = nographics::redirect_shaderapi_path(file);
  void* handle = real_dlopen(redirected_file, mode);
  if (handle != nullptr && is_launcher_path(file))
  {
    patch_launcher_source_lock();
  }
  if (handle != nullptr)
  {
    nographics::on_library_loaded(file);
  }

  return handle;
}
