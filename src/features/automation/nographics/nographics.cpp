/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/automation/nographics/nographics.cpp
 |  Y  |   autor: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "features/automation/nographics/nographics.hpp"

#include <array>
#include <cstdint>
#include <cstring>

#include "core/memory/byte_patch.hpp"
#include "core/print.hpp"
#include "core/shared/sigs.hpp"

#include "features/menu/config.hpp"

#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/file_system.hpp"
#include "games/tf2/sdk/interfaces/material_system.hpp"

#include "libsigscan/libsigscan.h"

bool write_to_table(void** vtable, int index, void* func);

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

using find_first_fn = const char* (*)(void*, const char*, file_find_handle_t*);
using find_next_fn = const char* (*)(void*, file_find_handle_t);
using async_read_multiple_fn = int (*)(void*, const void*, int, void*);
using open_ex_fn = file_handle_t (*)(void*, const char*, const char*, unsigned int, const char*, char**);
using read_file_ex_fn = int (*)(void*, const char*, const char*, void**, bool, bool, int, int, void*);
using add_files_to_cache_fn = void (*)(void*, file_cache_handle_t, const char**, int, const char*);
using open_fn = file_handle_t (*)(void*, const char*, const char*, const char*);
using precache_fn = bool (*)(void*, const char*, const char*);
using read_file_fn = bool (*)(void*, const char*, const char*, void*, int, int, void*);

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

#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE
constexpr bool textmode_build = true;
#else
constexpr bool textmode_build = false;
#endif

byte_patch play_sequence_patch{};
byte_patch particle_create_patch{};
byte_patch particle_precache_patch{};
byte_patch particle_effect_create_patch{};
byte_patch view_render_patch{};
byte_patch steam_rich_presence_patch{};

bool has_extension(const char* filename, const char* extension)
{
  if (filename == nullptr || extension == nullptr)
  {
    return false;
  }

  const char* dot = std::strrchr(filename, '.');
  return dot != nullptr && std::strcmp(dot, extension) == 0;
}

bool blacklisted_file(const char* filename)
{
  constexpr std::array<const char*, 8> blacklist = {
    ".ani", ".wav", ".mp3", ".vvd", ".vtx", ".vtf", ".vfe", ".cache"
  };

  if (filename == nullptr || std::strncmp(filename, "materials/console/", 18) == 0)
  {
    return false;
  }

  const std::size_t length = std::strlen(filename);
  if (length <= 3)
  {
    return false;
  }

  const char* extension = std::strrchr(filename, '.');
  if (extension == nullptr)
  {
    return false;
  }

  if (std::strcmp(extension, ".vmt") == 0)
  {
    return std::strstr(filename, "corner") == nullptr &&
           std::strstr(filename, "hud") == nullptr &&
           std::strstr(filename, "vgui") == nullptr;
  }

  if (std::strstr(filename, "sound.cache") != nullptr ||
      std::strstr(filename, "tf2_sound") != nullptr ||
      std::strstr(filename, "game_sounds") != nullptr ||
      std::strncmp(filename, "sound/player/footsteps", 22) == 0)
  {
    return false;
  }

  if (has_extension(filename, ".mdl"))
  {
    return false;
  }

  if (std::strncmp(filename, "/decal", 6) == 0)
  {
    return true;
  }

  for (const char* blocked_extension : blacklist)
  {
    if (std::strcmp(extension, blocked_extension) == 0)
    {
      return true;
    }
  }

  return false;
}

template <typename function_type>
bool hook_vtable(void** vtable, int index, void* hook, function_type* original)
{
  if (vtable == nullptr || hook == nullptr || original == nullptr)
  {
    return false;
  }

  *original = reinterpret_cast<function_type>(vtable[index]);
  return write_to_table(vtable, index, hook);
}

file_handle_t open_hook(void* this_ptr, const char* filename, const char* options, const char* path_id)
{
  if (blacklisted_file(filename))
  {
    return nullptr;
  }

  return open_original(this_ptr, filename, options, path_id);
}

bool precache_hook(void*, const char*, const char*)
{
  return true;
}

bool read_file_hook(void* this_ptr, const char* filename, const char* path, void* buffer, int max_bytes, int starting_byte, void* alloc_fn)
{
  if (blacklisted_file(filename))
  {
    return false;
  }

  return read_file_original(this_ptr, filename, path, buffer, max_bytes, starting_byte, alloc_fn);
}

const char* find_next_hook(void* this_ptr, file_find_handle_t handle)
{
  const char* filename = nullptr;
  do
  {
    filename = find_next_original(this_ptr, handle);
  }
  while (filename != nullptr && blacklisted_file(filename));

  return filename;
}

const char* find_first_hook(void* this_ptr, const char* wildcard, file_find_handle_t* handle)
{
  const char* filename = find_first_original(this_ptr, wildcard, handle);
  while (filename != nullptr && handle != nullptr && blacklisted_file(filename))
  {
    filename = find_next_original(this_ptr, *handle);
  }

  return filename;
}

int async_read_multiple_hook(void* this_ptr, const void* requests, int request_count, void* controls)
{
  return async_read_multiple_original(this_ptr, requests, request_count, controls);
}

file_handle_t open_ex_hook(void* this_ptr, const char* filename, const char* options, unsigned int flags, const char* path_id, char** resolved_filename)
{
  if (blacklisted_file(filename))
  {
    return nullptr;
  }

  return open_ex_original(this_ptr, filename, options, flags, path_id, resolved_filename);
}

int read_file_ex_hook(void* this_ptr, const char* filename, const char* path, void** buffer, bool null_terminate, bool optimal_alloc, int max_bytes, int starting_byte, void* alloc_fn)
{
  if (blacklisted_file(filename))
  {
    return 0;
  }

  return read_file_ex_original(this_ptr, filename, path, buffer, null_terminate, optimal_alloc, max_bytes, starting_byte, alloc_fn);
}

void add_files_to_cache_hook(void*, file_cache_handle_t, const char**, int, const char*)
{
}

void* resolve_rip_target(std::uint8_t* instruction, int displacement_offset, int instruction_size)
{
  const auto displacement = *reinterpret_cast<std::int32_t*>(instruction + displacement_offset);
  return instruction + instruction_size + displacement;
}

bool initialize_render_patches()
{
  if (render_patches_initialized)
  {
    return render_patches_ready;
  }

  auto* play_sequence = sigscan_module("client.so", sigs::base_animating_play_sequence);
  auto* particle_create = sigscan_module("client.so", sigs::particle_property_create);
  auto* particle_precache = sigscan_module("client.so", sigs::particle_system_precache);
  auto* particle_effect_create = sigscan_module("client.so", sigs::particle_effect_create_event);
  auto* view_render = sigscan_module("client.so", sigs::view_render_render);
  auto* steam_rich_presence = sigscan_module("client.so", sigs::client_update_steam_rich_presence);

  render_patches_initialized = true;

  if (play_sequence == nullptr || particle_create == nullptr || particle_precache == nullptr ||
      particle_effect_create == nullptr || view_render == nullptr || steam_rich_presence == nullptr)
  {
    print("[nographics] render patch scan failed play_sequence=%p particle_create=%p particle_precache=%p particle_effect_create=%p view_render=%p steam_rich_presence=%p\n",
          play_sequence, particle_create, particle_precache, particle_effect_create, view_render, steam_rich_presence);
    render_patches_ready = false;
    return false;
  }

  play_sequence_patch = byte_patch(play_sequence, { 0xC3 });
  particle_create_patch = byte_patch(particle_create, { 0x31, 0xC0, 0xC3 });
  particle_precache_patch = byte_patch(particle_precache, { 0x31, 0xC0, 0xC3 });
  particle_effect_create_patch = byte_patch(particle_effect_create, { 0x31, 0xC0, 0xC3 });
  view_render_patch = byte_patch(view_render, { 0xB0, 0x01, 0xC3 });
  steam_rich_presence_patch = byte_patch(steam_rich_presence, { 0xC3 });

  render_patches_ready = true;
  return true;
}

void apply_render_patches()
{
  if (!initialize_render_patches())
  {
    return;
  }

  const bool ok = play_sequence_patch.apply() &&
                  particle_create_patch.apply() &&
                  particle_precache_patch.apply() &&
                  particle_effect_create_patch.apply() &&
                  view_render_patch.apply() &&
                  steam_rich_presence_patch.apply();
  if (!ok)
  {
    play_sequence_patch.restore();
    particle_create_patch.restore();
    particle_precache_patch.restore();
    particle_effect_create_patch.restore();
    view_render_patch.restore();
    steam_rich_presence_patch.restore();
    print("[nographics] render patch apply failed\n");
    return;
  }

  render_patches_applied = true;
}

void restore_render_patches()
{
  if (!render_patches_applied)
  {
    return;
  }

  play_sequence_patch.restore();
  particle_create_patch.restore();
  particle_precache_patch.restore();
  particle_effect_create_patch.restore();
  view_render_patch.restore();
  steam_rich_presence_patch.restore();
  render_patches_applied = false;
}

void disable_file_system_hooks();

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
  ok &= hook_vtable(file_system_vtable, file_system_find_first_index, reinterpret_cast<void*>(find_first_hook), &find_first_original);
  ok &= hook_vtable(file_system_vtable, file_system_find_next_index, reinterpret_cast<void*>(find_next_hook), &find_next_original);
  ok &= hook_vtable(file_system_vtable, file_system_async_read_multiple_index, reinterpret_cast<void*>(async_read_multiple_hook), &async_read_multiple_original);
  ok &= hook_vtable(file_system_vtable, file_system_open_ex_index, reinterpret_cast<void*>(open_ex_hook), &open_ex_original);
  ok &= hook_vtable(file_system_vtable, file_system_read_file_ex_index, reinterpret_cast<void*>(read_file_ex_hook), &read_file_ex_original);
  ok &= hook_vtable(file_system_vtable, file_system_add_files_to_cache_index, reinterpret_cast<void*>(add_files_to_cache_hook), &add_files_to_cache_original);
  ok &= hook_vtable(base_file_system_vtable, base_file_system_open_index, reinterpret_cast<void*>(open_hook), &open_original);
  ok &= hook_vtable(base_file_system_vtable, base_file_system_precache_index, reinterpret_cast<void*>(precache_hook), &precache_original);
  ok &= hook_vtable(base_file_system_vtable, base_file_system_read_file_index, reinterpret_cast<void*>(read_file_hook), &read_file_original);

  if (!ok)
  {
    file_system_hooked = true;
    disable_file_system_hooks();
    print("[nographics] filesystem hook setup failed\n");
    return;
  }

  file_system_hooked = true;
}

void disable_file_system_hooks()
{
  if (!file_system_hooked)
  {
    return;
  }

  if (find_first_original != nullptr) write_to_table(file_system_vtable, file_system_find_first_index, reinterpret_cast<void*>(find_first_original));
  if (find_next_original != nullptr) write_to_table(file_system_vtable, file_system_find_next_index, reinterpret_cast<void*>(find_next_original));
  if (async_read_multiple_original != nullptr) write_to_table(file_system_vtable, file_system_async_read_multiple_index, reinterpret_cast<void*>(async_read_multiple_original));
  if (open_ex_original != nullptr) write_to_table(file_system_vtable, file_system_open_ex_index, reinterpret_cast<void*>(open_ex_original));
  if (read_file_ex_original != nullptr) write_to_table(file_system_vtable, file_system_read_file_ex_index, reinterpret_cast<void*>(read_file_ex_original));
  if (add_files_to_cache_original != nullptr) write_to_table(file_system_vtable, file_system_add_files_to_cache_index, reinterpret_cast<void*>(add_files_to_cache_original));
  if (open_original != nullptr) write_to_table(base_file_system_vtable, base_file_system_open_index, reinterpret_cast<void*>(open_original));
  if (precache_original != nullptr) write_to_table(base_file_system_vtable, base_file_system_precache_index, reinterpret_cast<void*>(precache_original));
  if (read_file_original != nullptr) write_to_table(base_file_system_vtable, base_file_system_read_file_index, reinterpret_cast<void*>(read_file_original));

  file_system_hooked = false;

  if (client != nullptr)
  {
    using invalidate_mdl_cache_fn = void (*)(void*);
    auto** vtable = *reinterpret_cast<void***>(client);
    auto invalidate_mdl_cache = reinterpret_cast<invalidate_mdl_cache_fn>(vtable[65]);
    invalidate_mdl_cache(client);
  }
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

} // namespace

void initialize()
{
  if constexpr (textmode_build)
  {
    config.misc.exploits.null_graphics = true;
    config.misc.exploits.null_graphics_render_stubs = true;
  }

  if (initialized || game_file_system != nullptr)
  {
    initialized = true;
    return;
  }

  auto* match = reinterpret_cast<std::uint8_t*>(sigscan_module("client.so", sigs::client_file_system));
  if (match != nullptr)
  {
    game_file_system = *reinterpret_cast<file_system**>(resolve_rip_target(match + 15, 3, 7));
  }

  if (game_file_system == nullptr)
  {
    print("[nographics] VFileSystem022 is missing\n");
  }

  initialized = true;
}

void prepare_render_patches()
{
  if constexpr (textmode_build)
  {
    initialize();
    enable_file_system_hooks();
    update_material_stub(true);
    apply_render_patches();
  }
}

void update()
{
  initialize();

  const bool enabled = textmode_build || config.misc.exploits.null_graphics;
  if (enabled)
  {
    enable_file_system_hooks();
    update_material_stub(true);
    if (textmode_build || config.misc.exploits.null_graphics_render_stubs)
    {
      apply_render_patches();
    }
    else
    {
      restore_render_patches();
    }
    return;
  }

  restore_render_patches();
  update_material_stub(false);
  disable_file_system_hooks();
}

void shutdown()
{
  restore_render_patches();
  update_material_stub(false);
  disable_file_system_hooks();
}

bool is_enabled()
{
  return textmode_build || config.misc.exploits.null_graphics;
}

bool should_skip_rendering_hooks()
{
  return textmode_build || config.misc.exploits.null_graphics_render_stubs;
}

bool should_use_aimbot_trace_fallback()
{
  return is_enabled() && should_skip_rendering_hooks();
}

} // namespace nographics
