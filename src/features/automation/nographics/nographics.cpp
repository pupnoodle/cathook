/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/automation/nographics/nographics.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "features/automation/nographics/nographics.hpp"
#include "core/shared/modules.hpp"
#include "core/memory/maps.hpp"
#include "core/memory/resolve.hpp"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>
#include "core/memory/byte_patch.hpp"
#include "core/print.hpp"
#include "core/shared/sigs.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/file_system.hpp"
#include "games/tf2/sdk/interfaces/material_system.hpp"
#include "funchook/funchook.h"
#include "libsigscan/libsigscan.h"
#if defined(__linux__)
#include <dlfcn.h>
#endif

bool write_to_table(void** vtable, int index, void* func);
void* read_vtable_entry(void** vtable, int index, const char* hook_name);
void* get_interface(const char* lib_path, const char* version);

namespace nographics
{

namespace
{

constexpr int file_system_find_first_index = 27;
constexpr int file_system_find_next_index = 28;
constexpr int file_system_async_read_multiple_index = 37;
constexpr int file_system_open_ex_index = 69;
constexpr int file_system_read_file_ex_index = 71;
constexpr int file_system_add_files_to_cache_index = 103;
constexpr int base_file_system_open_index = 2;
constexpr int base_file_system_precache_index = 9;
constexpr int base_file_system_read_file_index = 14;
constexpr std::uintptr_t base_file_system_vptr_offset = sizeof(void*);
constexpr int fs_async_err_fileopen = -1;
constexpr int client_hud_update_index = 11;

using find_first_fn = const char* (*)(void*, const char*, file_find_handle_t*);
using find_next_fn = const char* (*)(void*, file_find_handle_t);
using open_ex_fn = file_handle_t (*)(void*, const char*, const char*, unsigned int, const char*, char**);
using read_file_ex_fn = int (*)(void*, const char*, const char*, void**, bool, bool, int, int, void*);
using add_files_to_cache_fn = void (*)(void*, file_cache_handle_t, const char**, int, const char*);
using open_fn = file_handle_t (*)(void*, const char*, const char*, const char*);
using precache_fn = bool (*)(void*, const char*, const char*);
using read_file_fn = bool (*)(void*, const char*, const char*, void*, int, int, void*);

struct file_async_request;
using fs_async_callback_fn = void (*)(const file_async_request&, int, int);

struct file_async_request
{
  const char* filename;
  void* data;
  int offset;
  int bytes;
  fs_async_callback_fn callback;
  void* context;
  int priority;
  unsigned int flags;
  const char* path_id;
  void* specific_async_file;
  void* alloc_fn;
};

using async_read_multiple_fn = int (*)(void*, const file_async_request*, int, void*);

find_first_fn find_first_original = nullptr;
find_next_fn find_next_original = nullptr;
async_read_multiple_fn async_read_multiple_original = nullptr;
open_ex_fn open_ex_original = nullptr;
read_file_ex_fn read_file_ex_original = nullptr;
add_files_to_cache_fn add_files_to_cache_original = nullptr;
open_fn open_original = nullptr;
precache_fn precache_original = nullptr;
read_file_fn read_file_original = nullptr;

void** file_system_vtable = nullptr;
void** base_file_system_vtable = nullptr;
bool file_system_hooked = false;
bool material_stub_enabled = false;
bool render_patches_applied = false;
bool initialized = false;
bool render_patches_initialized = false;
bool render_patches_ready = false;
std::atomic_bool startup_patch_running = false;
bool nographics_runtime_enabled = false;
std::chrono::steady_clock::time_point nographics_next_maintenance{};
constexpr auto nographics_maintenance_interval = std::chrono::seconds(2);
#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE

constexpr bool textmode_build = true;
#else

constexpr bool textmode_build = false;
#endif

bool module_is_loaded(const char* module_name)
{
  return cathook::core::memory::is_module_loaded(module_name);
}

bool command_line_has_flag(const char* flag)
{
  if (flag == nullptr)
  {
    return false;
  }

  std::ifstream cmdline{ "/proc/self/cmdline", std::ios::binary };
  if (!cmdline)
  {
    return false;
  }

  std::string argument{};
  for (char value{}; cmdline.get(value);)
  {
    if (value == '\0')
    {
      if (argument == flag)
      {
        return true;
      }
      argument.clear();
      continue;
    }
    argument.push_back(value);
  }

  return argument == flag;
}

bool command_line_has_noshaderapi()
{
  return command_line_has_flag("-noshaderapi");
}

bool empty_shader_api_is_active()
{
  if (!module_is_loaded("shaderapiempty.so"))
  {
    return false;
  }

  return !module_is_loaded("shaderapidx9.so") &&
         !module_is_loaded("shaderapivk.so") &&
         !module_is_loaded("togl.so");
}

std::string_view basename_of(std::string_view path)
{
  const auto slash = path.find_last_of('/');
  return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

bool is_shaderapivk_path(std::string_view library_path)
{
  const std::string_view name = basename_of(library_path);
  return name == "shaderapivk.so" || name == "shaderapivk";
}

std::string_view library_basename(const char* library_path)
{
  if (library_path == nullptr)
  {
    return {};
  }

  return basename_of(std::string_view{ library_path });
}

bool is_startup_patch_module(const char* library_path)
{
  const std::string_view name = library_basename(library_path);
  return name == "engine.so" ||
         name == "client.so" ||
         name == "materialsystem.so" ||
         name == "studiorender.so" ||
         name == "datacache.so" ||
         name == "filesystem_stdio.so" ||
         name == "filesystem_steam.so";
}

struct render_patch
{
  byte_patch patch{};
  const char* module;
  const char* signature;
  int offset;
  std::initializer_list<std::uint8_t> bytes;
  const char* name;
  bool textmode_only;
};

render_patch render_patches[] = {
  { {}, cathook::core::modules::tf_client, sigs::particle_property_create, 0, { 0x31, 0xC0, 0xC3 }, "particle_property_create", false },
  { {}, cathook::core::modules::tf_client, sigs::play_sequence, 0, { 0xC3 }, "play_sequence", false },
  { {}, cathook::core::modules::tf_client, sigs::particle_system_precache, 0, { 0x31, 0xC0, 0xC3 }, "particle_system_precache", false },
  { {}, cathook::core::modules::tf_client, sigs::particle_effect_create_event, 0, { 0x31, 0xC0, 0xC3 }, "particle_effect_create_event", false },
  { {}, cathook::core::modules::tf_client, sigs::view_render_render, 0, { 0x31, 0xC0, 0x40, 0xC3 }, "view_render_render", true },
  { {}, "engine.so", sigs::video_mode_setup_startup_graphic, 0, { 0xC3 }, "video_mode_setup_startup_graphic", true },
  { {}, "engine.so", sigs::v_render_view, 0, { 0xC3 }, "v_render_view", true },
  { {}, "materialsystem.so", sigs::material_system_swap_buffers, 0, { 0x31, 0xC0, 0x40, 0xC3 }, "material_system_swap_buffers", true },
};

char normalize_path_char(const char value)
{
  if (value == '\\')
  {
    return '/';
  }

  if (value >= 'A' && value <= 'Z')
  {
    return static_cast<char>(value - 'A' + 'a');
  }

  return value;
}

bool path_equals(const std::string_view path, const std::string_view expected)
{
  if (path.size() != expected.size())
  {
    return false;
  }

  for (std::size_t index = 0; index < expected.size(); ++index)
  {
    if (normalize_path_char(path[index]) != normalize_path_char(expected[index]))
    {
      return false;
    }
  }

  return true;
}

bool path_starts_with(const std::string_view path, const std::string_view prefix)
{
  if (path.size() < prefix.size())
  {
    return false;
  }

  return path_equals(path.substr(0, prefix.size()), prefix);
}

bool path_ends_with(const std::string_view path, const std::string_view suffix)
{
  if (path.size() < suffix.size())
  {
    return false;
  }

  return path_equals(path.substr(path.size() - suffix.size()), suffix);
}

bool path_contains(const std::string_view path, const std::string_view needle)
{
  if (needle.empty() || path.size() < needle.size())
  {
    return false;
  }

  for (std::size_t index = 0; index <= path.size() - needle.size(); ++index)
  {
    if (path_equals(path.substr(index, needle.size()), needle))
    {
      return true;
    }
  }

  return false;
}

std::string_view file_extension(const std::string_view filename)
{
  const auto slash = filename.find_last_of("/\\");
  const auto dot = filename.find_last_of('.');
  if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash))
  {
    return {};
  }

  return filename.substr(dot);
}

bool is_soundscape_script(const std::string_view filename)
{
  return path_equals(filename, "scripts/soundscapes_manifest.txt") ||
         (path_starts_with(filename, "scripts/soundscapes_") && path_ends_with(filename, ".txt"));
}

bool is_required_model_asset(const std::string_view extension)
{
  return path_equals(extension, ".mdl") ||
         path_equals(extension, ".phy") ||
         path_equals(extension, ".ani") ||
         path_equals(extension, ".vvd") ||
         path_equals(extension, ".vtx");
}

bool sound_family_file(const std::string_view filename)
{
  return path_contains(filename, "sound.cache") ||
         path_contains(filename, "tf2_sound") ||
         path_contains(filename, "game_sounds") ||
         path_starts_with(filename, "sound/player/footsteps");
}

bool extension_blocks_file(const std::string_view filename, const std::string_view extension)
{
  if (extension.size() < 2)
  {
    return false;
  }

  switch (extension[1])
  {
    case 'a':
    {
      return path_equals(extension, ".ani");
    }
    case 'b':
    {
      return path_equals(extension, ".bik");
    }
    case 'c':
    {
      if (path_equals(extension, ".cache"))
      {
        return !path_contains(filename, "sound.cache");
      }
      return path_equals(extension, ".cur");
    }
    case 'd':
    {
      return path_equals(extension, ".dds") || path_equals(extension, ".dem");
    }
    case 'i':
    {
      return path_equals(extension, ".ico");
    }
    case 'j':
    {
      return path_equals(extension, ".jpg");
    }
    case 'm':
    {
      return path_equals(extension, ".mp3");
    }
    case 'p':
    {
      return path_equals(extension, ".png") || path_equals(extension, ".pcf");
    }
    case 't':
    {
      return path_equals(extension, ".tga");
    }
    case 'v':
    {
      return path_equals(extension, ".vvd") || path_equals(extension, ".vtx") ||
             path_equals(extension, ".vtf") || path_equals(extension, ".vfe") ||
             path_equals(extension, ".vcd");
    }
    case 'w':
    {
      if (path_equals(extension, ".webm"))
      {
        return true;
      }
      return path_equals(extension, ".wav") && !sound_family_file(filename);
    }
    default:
    {
      break;
    }
  }

  return false;
}

bool should_block_file(const char* raw_filename)
{
  if (raw_filename == nullptr)
  {
    return false;
  }

  const std::string_view filename{ raw_filename };
  if (filename.size() <= 3)
  {
    return false;
  }

  const std::string_view extension = file_extension(filename);

  if (path_equals(extension, ".cat") || path_equals(extension, ".cfg") ||
      path_equals(extension, ".bsp") || path_equals(extension, ".nav") || is_required_model_asset(extension))
  {
    return false;
  }

  if (is_soundscape_script(filename) ||
      path_starts_with(filename, "materials/console/") ||
      path_starts_with(filename, "debug/"))
  {
    return false;
  }

  if (path_equals(extension, ".vmt"))
  {
    if (path_contains(filename, "corner") ||
        path_contains(filename, "hud") ||
        path_contains(filename, "vgui") ||
        path_contains(filename, "console"))
    {
      return false;
    }

    if constexpr (textmode_build)
    {
      if (aggressive_material_block && path_starts_with(filename, "materials/models/"))
      {
        return true;
      }
    }

    return true;
  }

  if (!extension.empty() && extension_blocks_file(filename, extension))
  {
    return true;
  }

  if (extension.empty())
  {
    return false;
  }

  if (path_starts_with(filename, "replay/") ||
      path_starts_with(filename, "resource/replay/") ||
      path_starts_with(filename, "materials/vgui/replay/") ||
      path_starts_with(filename, "media/") ||
      path_starts_with(filename, "videos/") ||
      path_starts_with(filename, "cursors/") ||
      path_starts_with(filename, "resource/cursors/") ||
      path_starts_with(filename, "materials/cursors/"))
  {
    return true;
  }

  if (sound_family_file(filename))
  {
    return false;
  }

  if (path_starts_with(filename, "/decal") ||
      path_starts_with(filename, "decal") ||
      path_starts_with(filename, "materials/decals/") ||
      path_starts_with(filename, "sprites/") ||
      path_contains(filename, "skybox") ||
      path_contains(filename, "detail") ||
      path_contains(filename, "ambient") ||
      (path_contains(filename, "soundscape") && !path_equals(extension, ".txt")))
  {
    return true;
  }

  return false;
}

bool hook_vtable(void** vtable, int index, void* hook, void** original)
{
  void* const entry = read_vtable_entry(vtable, index, "nographics vtable hook");
  if (entry == nullptr)
  {
    return false;
  }

  *original = entry;
  if (!write_to_table(vtable, index, hook))
  {
    *original = nullptr;
    return false;
  }

  return true;
}

file_handle_t open_hook(void* this_ptr, const char* filename, const char* options, const char* path_id)
{
  CATHOOK_HOOK_GUARD();
  if (should_block_file(filename))
  {
    return nullptr;
  }

  return open_original(this_ptr, filename, options, path_id);
}

bool precache_hook(void* this_ptr, const char* filename, const char* path_id)
{
  CATHOOK_HOOK_GUARD();

  (void)this_ptr;
  (void)filename;
  (void)path_id;
  return true;
}

bool read_file_hook(void* this_ptr, const char* filename, const char* path, void* buffer, int max_bytes, int starting_byte, void* alloc_fn)
{
  CATHOOK_HOOK_GUARD();
  if (should_block_file(filename))
  {
    return false;
  }

  return read_file_original(this_ptr, filename, path, buffer, max_bytes, starting_byte, alloc_fn);
}

const char* find_next_hook(void* this_ptr, file_find_handle_t handle)
{
  CATHOOK_HOOK_GUARD();
  const char* filename = nullptr;
  do
  {
    filename = find_next_original(this_ptr, handle);
  }
  while (filename != nullptr && should_block_file(filename));

  return filename;
}

const char* find_first_hook(void* this_ptr, const char* wildcard, file_find_handle_t* handle)
{
  CATHOOK_HOOK_GUARD();
  const char* filename = find_first_original(this_ptr, wildcard, handle);
  while (filename != nullptr && handle != nullptr && should_block_file(filename))
  {
    filename = find_next_original(this_ptr, *handle);
  }

  return filename;
}

int async_read_multiple_hook(void* this_ptr, const file_async_request* requests, int request_count, void* controls)
{
  CATHOOK_HOOK_GUARD();
  if (requests == nullptr || request_count <= 0)
  {
    return async_read_multiple_original(this_ptr, requests, request_count, controls);
  }

  bool has_blocked_request = false;
  bool has_allowed_request = false;
  for (int index = 0; index < request_count; ++index)
  {
    if (should_block_file(requests[index].filename))
    {
      has_blocked_request = true;
    }
    else
    {
      has_allowed_request = true;
    }
  }

  if (!has_blocked_request)
  {
    return async_read_multiple_original(this_ptr, requests, request_count, controls);
  }

  for (int index = 0; index < request_count; ++index)
  {
    if (should_block_file(requests[index].filename) && requests[index].callback != nullptr)
    {
      requests[index].callback(requests[index], 0, fs_async_err_fileopen);
    }
  }

  if (!has_allowed_request)
  {
    return fs_async_err_fileopen;
  }

  if (controls != nullptr)
  {
    return fs_async_err_fileopen;
  }

  std::vector<file_async_request> allowed_requests{};
  allowed_requests.reserve(static_cast<std::size_t>(request_count));
  for (int index = 0; index < request_count; ++index)
  {
    if (!should_block_file(requests[index].filename))
    {
      allowed_requests.emplace_back(requests[index]);
    }
  }

  return async_read_multiple_original(
    this_ptr,
    allowed_requests.data(),
    static_cast<int>(allowed_requests.size()),
    controls);
}

file_handle_t open_ex_hook(void* this_ptr, const char* filename, const char* options, unsigned int flags, const char* path_id, char** resolved_filename)
{
  CATHOOK_HOOK_GUARD();
  if (should_block_file(filename))
  {
    return nullptr;
  }

  return open_ex_original(this_ptr, filename, options, flags, path_id, resolved_filename);
}

int read_file_ex_hook(void* this_ptr, const char* filename, const char* path, void** buffer, bool null_terminate, bool optimal_alloc, int max_bytes, int starting_byte, void* alloc_fn)
{
  CATHOOK_HOOK_GUARD();
  if (should_block_file(filename))
  {
    return 0;
  }

  return read_file_ex_original(this_ptr, filename, path, buffer, null_terminate, optimal_alloc, max_bytes, starting_byte, alloc_fn);
}

void add_files_to_cache_hook(void* this_ptr, file_cache_handle_t cache_id, const char** filenames, int filename_count, const char* path_id)
{
  CATHOOK_HOOK_GUARD();

  (void)this_ptr;
  (void)cache_id;
  (void)filenames;
  (void)filename_count;
  (void)path_id;
}

struct file_system_hook
{
  void*** vtable;
  int index;
  void* hook;
  void** original;
  const char* name;
};

const file_system_hook file_system_hooks[] = {
  { &file_system_vtable, file_system_find_first_index, reinterpret_cast<void*>(find_first_hook), reinterpret_cast<void**>(&find_first_original), "IFileSystem::FindFirst" },
  { &file_system_vtable, file_system_find_next_index, reinterpret_cast<void*>(find_next_hook), reinterpret_cast<void**>(&find_next_original), "IFileSystem::FindNext" },
  { &file_system_vtable, file_system_async_read_multiple_index, reinterpret_cast<void*>(async_read_multiple_hook), reinterpret_cast<void**>(&async_read_multiple_original), "IFileSystem::AsyncReadMultiple" },
  { &file_system_vtable, file_system_open_ex_index, reinterpret_cast<void*>(open_ex_hook), reinterpret_cast<void**>(&open_ex_original), "IFileSystem::OpenEx" },
  { &file_system_vtable, file_system_read_file_ex_index, reinterpret_cast<void*>(read_file_ex_hook), reinterpret_cast<void**>(&read_file_ex_original), "IFileSystem::ReadFileEx" },
  { &file_system_vtable, file_system_add_files_to_cache_index, reinterpret_cast<void*>(add_files_to_cache_hook), reinterpret_cast<void**>(&add_files_to_cache_original), "IFileSystem::AddFilesToCache" },
  { &base_file_system_vtable, base_file_system_open_index, reinterpret_cast<void*>(open_hook), reinterpret_cast<void**>(&open_original), "CBaseFileSystem::Open" },
  { &base_file_system_vtable, base_file_system_precache_index, reinterpret_cast<void*>(precache_hook), reinterpret_cast<void**>(&precache_original), "CBaseFileSystem::Precache" },
  { &base_file_system_vtable, base_file_system_read_file_index, reinterpret_cast<void*>(read_file_hook), reinterpret_cast<void**>(&read_file_original), "CBaseFileSystem::ReadFile" },
};

std::uint8_t* scan_module_patch(const char* module_name, const char* signature, int offset)
{
  auto* match = reinterpret_cast<std::uint8_t*>(sigscan_module(module_name, signature));
  if (match == nullptr)
  {
    return nullptr;
  }

  return match + offset;
}



bool restore_render_patch_objects()
{
  bool ok = true;
  for (auto& desc : render_patches)
  {
    ok = desc.patch.restore() && ok;
  }
  return ok;
}

bool initialize_render_patches()
{
  if (render_patches_initialized)
  {
    return render_patches_ready;
  }

  if (!module_is_loaded(cathook::core::modules::tf_client))
  {
    return false;
  }

  render_patches_initialized = true;
  render_patches_ready = true;

  for (auto& desc : render_patches)
  {
    if ((desc.textmode_only && !textmode_build) || desc.patch.valid())
    {
      continue;
    }

    auto* patch_site = scan_module_patch(desc.module, desc.signature, desc.offset);
    if (patch_site == nullptr)
    {
      print("[nographics] core patch scan failed name=%s module=%s\n", desc.name, desc.module);
      render_patches_ready = false;
      continue;
    }

    desc.patch = byte_patch(patch_site, desc.bytes);
  }

  if (!render_patches_ready)
  {
    print("[nographics] no core render patches initialized\n");
    render_patches_initialized = false;
  }

  return render_patches_ready;
}

void apply_cathook2017_render_patches()
{
  initialize_render_patches();
  bool ok = true;
  bool any_patch = false;

  for (auto& desc : render_patches)
  {
    if (!desc.patch.valid())
    {
      continue;
    }

    any_patch = true;
    if (!desc.patch.apply())
    {
      print("[nographics] render patch apply failed name=%s\n", desc.name);
      ok = false;
    }
  }

  if (!ok)
  {
    restore_render_patch_objects();
    render_patches_applied = false;
    print("[nographics] Cathook2017 render patch apply failed\n");
    return;
  }

  render_patches_applied = render_patches_applied || any_patch;
}

bool restore_render_patches()
{
  if (!render_patches_applied)
  {
    return true;
  }

  if (!restore_render_patch_objects())
  {
    print("[nographics] render patch restore failed\n");
    return false;
  }

  render_patches_applied = false;
  return true;
}

bool disable_file_system_hooks();

void enable_file_system_hooks()
{
  if (file_system_hooked || game_file_system == nullptr)
  {
    return;
  }

  file_system_vtable = *reinterpret_cast<void***>(game_file_system);
  auto* base_subobject = reinterpret_cast<std::uint8_t*>(game_file_system) + base_file_system_vptr_offset;
  base_file_system_vtable = *reinterpret_cast<void***>(base_subobject);

  bool ok = true;
  for (const auto& desc : file_system_hooks)
  {
    ok &= hook_vtable(*desc.vtable, desc.index, desc.hook, desc.original);
  }

  if (!ok)
  {
    file_system_hooked = true;
    disable_file_system_hooks();
    print("[nographics] filesystem hook setup failed\n");
    return;
  }

  file_system_hooked = true;
}

bool disable_file_system_hooks()
{
  if (!file_system_hooked)
  {
    return true;
  }

  bool ok = true;
  for (const auto& desc : file_system_hooks)
  {
    void* const original = *desc.original;
    if (original == nullptr)
    {
      continue;
    }

    if (read_vtable_entry(*desc.vtable, desc.index, desc.name) != desc.hook)
    {
      print("[nographics] %s was re-hooked after us; leaving it alone\n", desc.name);
      *desc.original = nullptr;
      continue;
    }

    if (!write_to_table(*desc.vtable, desc.index, original))
    {
      print("[nographics] failed to restore %s\n", desc.name);
      ok = false;
      continue;
    }

    *desc.original = nullptr;
  }

  if (!ok)
  {
    return false;
  }

  file_system_hooked = false;

  if (client != nullptr)
  {
    using invalidate_mdl_cache_fn = void (*)(void*);
    void** client_vtable = *reinterpret_cast<void***>(client);
    void* const entry = read_vtable_entry(client_vtable, 65, "Client::InvalidateMdlCache");
    if (entry != nullptr)
    {
      invalidate_mdl_cache_fn invalidate_mdl_cache = reinterpret_cast<invalidate_mdl_cache_fn>(entry);
      invalidate_mdl_cache(client);
    }
    else
    {
      print("[nographics] client vtable[65] missing; skipped MDL cache invalidation\n");
    }
  }

  file_system_vtable = nullptr;
  base_file_system_vtable = nullptr;
  return true;
}

using hud_update_fn = void (*)(void*, bool);
hud_update_fn hud_update_original = nullptr;
funchook_t* hud_update_funchook = nullptr;
std::uint64_t hud_update_frame_counter = 0;

void hud_update_hook(void* this_ptr, bool active)
{
  CATHOOK_HOOK_GUARD();
  if (hud_update_original == nullptr || !is_enabled() || should_skip_rendering_hooks())
  {
    if (hud_update_original != nullptr)
    {
      hud_update_original(this_ptr, active);
    }
    return;
  }

  std::uint64_t interval = static_cast<std::uint64_t>(hud_throttle_frames);
  if (interval < 1)
  {
    interval = 1;
  }

  if (++hud_update_frame_counter < interval)
  {
    return;
  }

  hud_update_frame_counter = 0;
  hud_update_original(this_ptr, active);
}

bool disable_hud_update_hook()
{
  if (hud_update_funchook != nullptr)
  {
    const int result = funchook_uninstall(hud_update_funchook, 0);
    if (result != FUNCHOOK_ERROR_SUCCESS && result != FUNCHOOK_ERROR_NOT_INSTALLED)
    {
      print("[nographics] failed to restore HudUpdate hook: %d\n", result);
      return false;
    }

    funchook_destroy(hud_update_funchook);
    hud_update_funchook = nullptr;
  }

  hud_update_original = nullptr;
  hud_update_frame_counter = 0;
  return true;
}

void enable_hud_update_hook()
{
  if (hud_update_funchook != nullptr || client == nullptr)
  {
    return;
  }

  auto** client_vtable_local = *reinterpret_cast<void***>(client);
  if (client_vtable_local == nullptr)
  {
    return;
  }

  auto* target = read_vtable_entry(client_vtable_local, client_hud_update_index, "Client::HudUpdate");
  if (target == nullptr)
  {
    return;
  }

  auto* handle = funchook_create();
  if (handle == nullptr)
  {
    return;
  }

  hud_update_original = reinterpret_cast<hud_update_fn>(target);
  if (funchook_prepare(handle, reinterpret_cast<void**>(&hud_update_original), reinterpret_cast<void*>(hud_update_hook)) != FUNCHOOK_ERROR_SUCCESS ||
      funchook_install(handle, 0) != FUNCHOOK_ERROR_SUCCESS)
  {
    print("[nographics] HudUpdate throttle install failed: %s\n", funchook_error_message(handle));
    disable_hud_update_hook();
    return;
  }

  hud_update_funchook = handle;
  print("[nographics] HudUpdate throttle hooked every %d frames\n", hud_throttle_frames);
}

void update_hud_update_throttle()
{
  if (is_enabled() && !should_skip_rendering_hooks())
  {
    enable_hud_update_hook();
    return;
  }

  disable_hud_update_hook();
}

void update_material_stub(bool enabled)
{
  if (material_system == nullptr || material_stub_enabled == enabled)
  {
    return;
  }

  material_system->set_in_stub_mode(enabled);
  material_stub_enabled = enabled;
}

void resolve_game_file_system_interface()
{
  if (game_file_system != nullptr)
  {
    return;
  }

  if (module_is_loaded("filesystem_stdio.so"))
  {
    game_file_system = static_cast<file_system*>(get_interface("./bin/linux64/filesystem_stdio.so", "VFileSystem022"));
  }

  if (game_file_system == nullptr && module_is_loaded("filesystem_steam.so"))
  {
    game_file_system = static_cast<file_system*>(get_interface("./bin/linux64/filesystem_steam.so", "VFileSystem022"));
  }

  if (game_file_system != nullptr || !module_is_loaded(cathook::core::modules::tf_client))
  {
    return;
  }

  auto* match = reinterpret_cast<std::uint8_t*>(sigscan_module(cathook::core::modules::tf_client, sigs::client_file_system));
  if (match != nullptr)
  {
    game_file_system = *static_cast<file_system**>(cathook::core::memory::resolve_rip_relative(match + 15, 3, 7));
  }
}

void resolve_material_system_interface()
{
  if (material_system != nullptr || !module_is_loaded("materialsystem.so"))
  {
    return;
  }

  material_system = static_cast<MaterialSystem*>(get_interface("./bin/linux64/materialsystem.so", "VMaterialSystem082"));
}

}

void initialize()
{
  if constexpr (textmode_build)
  {
    config.misc.exploits.null_graphics = true;

    config.misc.exploits.no_engine_sleep = !textmode_allow_engine_sleep;
  }

  resolve_game_file_system_interface();

  if (game_file_system != nullptr)
  {
    initialized = true;
    return;
  }

  if (!initialized && module_is_loaded(cathook::core::modules::tf_client))
  {
    print("[nographics] VFileSystem022 is missing\n");
    initialized = true;
  }
}

void prepare_startup_patches()
{
  if constexpr (textmode_build)
  {
    if (startup_patch_running.exchange(true, std::memory_order_acq_rel))
    {
      return;
    }
    struct startup_patch_release
    {
      ~startup_patch_release()
      {
        startup_patch_running.store(false, std::memory_order_release);
      }
    } release;

    initialize();
    resolve_material_system_interface();
    enable_file_system_hooks();
    update_material_stub(true);
    apply_cathook2017_render_patches();
  }
}

void prepare_render_patches()
{
  if constexpr (textmode_build)
  {
    prepare_startup_patches();
  }
}

void on_library_loaded(const char* library_path)
{
  if constexpr (textmode_build)
  {
    if (is_startup_patch_module(library_path))
    {
      prepare_startup_patches();
    }
  }
}

void update()
{
  const bool enabled = textmode_build || config.misc.exploits.null_graphics;
  const auto now = std::chrono::steady_clock::now();

  if (!enabled)
  {
    if (nographics_runtime_enabled)
    {
      restore_render_patches();
      update_material_stub(false);
      disable_file_system_hooks();
    }
    disable_hud_update_hook();
    nographics_runtime_enabled = false;
    return;
  }

  const bool state_changed = !nographics_runtime_enabled;
  if (!state_changed && now < nographics_next_maintenance)
  {
    return;
  }

  initialize();
  resolve_material_system_interface();
  enable_file_system_hooks();
  update_hud_update_throttle();
  update_material_stub(textmode_build);
  apply_cathook2017_render_patches();

  nographics_runtime_enabled = true;
  nographics_next_maintenance = now + nographics_maintenance_interval;
}

bool shutdown()
{
  const bool render_patches_restored = restore_render_patches();
  update_material_stub(false);
  const bool file_system_hooks_disabled = disable_file_system_hooks();
  const bool hud_update_hook_disabled = disable_hud_update_hook();
  if (!render_patches_restored || !file_system_hooks_disabled || !hud_update_hook_disabled)
  {
    return false;
  }

  nographics_runtime_enabled = false;
  nographics_next_maintenance = {};
  initialized = false;
  render_patches_initialized = false;
  render_patches_ready = false;
  game_file_system = nullptr;
  return true;
}

bool is_enabled()
{
  return textmode_build || config.misc.exploits.null_graphics;
}

bool should_skip_rendering_hooks()
{
  return textmode_build || is_noshaderapi();
}

bool command_line_has_vulkan()
{
  static const bool from_command_line = command_line_has_flag("-vulkan");
  return from_command_line;
}

bool is_noshaderapi()
{
  static const bool from_command_line = command_line_has_noshaderapi();
  if (from_command_line)
  {
    return true;
  }

  static int from_modules = -1;
  if (from_modules < 0 && (material_system != nullptr || module_is_loaded("materialsystem.so")))
  {
    from_modules = empty_shader_api_is_active() ? 1 : 0;
  }

  return from_modules == 1;
}

const char* redirect_shaderapi_path(const char* library_path)
{
  if (library_path == nullptr || !is_noshaderapi())
  {
    return library_path;
  }

  const std::string_view path{ library_path };
  if (!is_shaderapivk_path(path))
  {
    return library_path;
  }

  thread_local std::string redirected_path{};
  const auto slash = path.find_last_of('/');
  if (slash == std::string_view::npos)
  {
    redirected_path = "shaderapiempty.so";
  }
  else
  {
    redirected_path.assign(path.substr(0, slash + 1));
    redirected_path += "shaderapiempty.so";
  }

  return redirected_path.c_str();
}

}
#if defined(__linux__)

struct SDL_Window;
extern SDL_Window* sdl_window;

extern "C" __attribute__((visibility("default"))) SDL_Window* SDL_CreateWindow(
  const char* title,
  int x,
  int y,
  int w,
  int h,
  unsigned int flags)
{
  using sdl_create_window_fn = SDL_Window* (*)(const char*, int, int, int, int, unsigned int);
  static sdl_create_window_fn sdl_create_window_original =
    reinterpret_cast<sdl_create_window_fn>(dlsym(RTLD_NEXT, "SDL_CreateWindow"));

  if (sdl_create_window_original == nullptr)
  {
    return nullptr;
  }

  constexpr unsigned int sdl_window_opengl = 0x00000002u;
  constexpr unsigned int sdl_window_hidden = 0x00000008u;
  constexpr unsigned int sdl_window_vulkan = 0x10000000u;

  unsigned int fixed_flags = flags;
  if (nographics::should_skip_rendering_hooks())
  {
    fixed_flags &= ~(sdl_window_opengl | sdl_window_vulkan);
    fixed_flags |= sdl_window_hidden;
    return sdl_create_window_original(title, -32000, -32000, 1, 1, fixed_flags);
  }

  SDL_Window* const window = sdl_create_window_original(title, x, y, w, h, fixed_flags);
  if (window != nullptr && (fixed_flags & sdl_window_vulkan) != 0)
  {
    sdl_window = window;
  }
  return window;
}
#endif
