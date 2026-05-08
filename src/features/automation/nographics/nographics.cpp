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

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string_view>
#include <vector>

#include "core/memory/byte_patch.hpp"
#include "core/print.hpp"
#include "core/shared/sigs.hpp"

#include "features/menu/config.hpp"

#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/file_system.hpp"
#include "games/tf2/sdk/interfaces/material_system.hpp"

#include "libsigscan/libsigscan.h"

bool write_to_table(void** vtable, int index, void* func);
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
constexpr int base_file_system_file_exists_index = 10;
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
using file_exists_fn = bool (*)(void*, const char*, const char*);
using read_file_fn = bool (*)(void*, const char*, const char*, void*, int, int, void*);

find_first_fn find_first_original = nullptr;
find_next_fn find_next_original = nullptr;
async_read_multiple_fn async_read_multiple_original = nullptr;
open_ex_fn open_ex_original = nullptr;
read_file_ex_fn read_file_ex_original = nullptr;
add_files_to_cache_fn add_files_to_cache_original = nullptr;
open_fn open_original = nullptr;
precache_fn precache_original = nullptr;
file_exists_fn file_exists_original = nullptr;
read_file_fn read_file_original = nullptr;

void** file_system_vtable = nullptr;
void** base_file_system_vtable = nullptr;
bool file_system_hooked = false;
bool material_stub_enabled = false;
bool render_patches_applied = false;
bool optional_render_patches_applied = false;
bool initialized = false;
bool render_patches_initialized = false;
bool render_patches_ready = false;
bool engine_render_patches_initialized = false;
bool materialsystem_render_patches_initialized = false;
std::atomic_bool startup_patch_running = false;

#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE
constexpr bool textmode_build = true;
#else
constexpr bool textmode_build = false;
#endif

bool module_is_loaded(const char* module_name)
{
  auto* bounds = sigscan_get_module_bounds(SIGSCAN_PID_SELF, module_name);
  if (bounds == nullptr)
  {
    return false;
  }

  sigscan_free_module_bounds(bounds);
  return true;
}

std::string_view library_basename(const char* library_path)
{
  if (library_path == nullptr)
  {
    return {};
  }

  const std::string_view path{ library_path };
  const auto slash = path.find_last_of('/');
  if (slash == std::string_view::npos)
  {
    return path;
  }

  return path.substr(slash + 1);
}

bool is_startup_patch_module(const char* library_path)
{
  const std::string_view name = library_basename(library_path);
  return name == "engine.so" ||
         name == "client.so" ||
         name == "materialsystem.so" ||
         name == "filesystem_stdio.so" ||
         name == "filesystem_steam.so";
}

bool should_apply_extra_render_patches()
{
  return textmode_build || config.misc.exploits.experimental_nographic_hooks;
}

byte_patch dispatch_anim_events_patch{};
byte_patch particle_create_patch{};
byte_patch particle_precache_patch{};
byte_patch particle_effect_create_patch{};
byte_patch view_render_patch{};
byte_patch replay_screenshot_patch{};
byte_patch steam_rich_presence_patch{};
byte_patch cl_decay_lights_patch{};
byte_patch mod_load_lighting_patch{};
byte_patch mod_load_worldlights_patch{};
byte_patch sprite_load_model_patch{};
byte_patch overlay_mgr_load_overlays_patch{};
byte_patch material_system_begin_frame_patch{};
byte_patch video_mode_setup_startup_graphic_patch{};
constexpr std::size_t replay_ui_nullcheck_patch_count = 9;
constexpr std::size_t character_info_command_patch_count = 5;
std::array<byte_patch, replay_ui_nullcheck_patch_count> replay_ui_nullcheck_patches{};
std::array<byte_patch, character_info_command_patch_count> character_info_command_patches{};
byte_patch econ_item_definition_index_patch{};

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

bool path_contains_any(const std::string_view path, std::initializer_list<std::string_view> needles)
{
  for (const std::string_view needle : needles)
  {
    if (path_contains(path, needle))
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

bool is_required_model_asset(const std::string_view filename, const std::string_view extension)
{
  if (path_equals(extension, ".mdl") || path_equals(extension, ".phy"))
  {
    return true;
  }

  if (!path_equals(extension, ".ani") && !path_equals(extension, ".vvd") && !path_equals(extension, ".vtx"))
  {
    return false;
  }

  // Player/building/projectile model companions are not just graphics for us:
  // model setup feeds hitboxes, bones, and some collideable bounds used by aim traces.
  return path_contains_any(filename, {
    "models/player/",
    "models/bots/",
    "models/buildables/",
    "models/weapons/",
    "models/items/",
    "models/pickups/",
    "models/props_halloween/",
    "models/props_medieval/",
    "models/flag/",
    "models/custom/",
    "player/",
    "bots/",
    "buildables/",
    "weapons/",
    "empty.mdl",
    "error.mdl",
  });
}

bool should_block_file(const char* raw_filename)
{
  constexpr std::array<std::string_view, 16> blocked_extensions = {
    ".ani", ".wav", ".mp3", ".vvd", ".vtx", ".vtf", ".vfe", ".cache",
    ".jpg", ".png", ".tga", ".dds", ".bik", ".webm", ".vcd", ".pcf",
  };

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
  if (path_equals(extension, ".cat") || path_equals(extension, ".cfg"))
  {
    return false;
  }

  if (path_equals(extension, ".bsp") || path_equals(extension, ".nav") || is_required_model_asset(filename, extension))
  {
    return false;
  }

  if (is_soundscape_script(filename) ||
      path_starts_with(filename, "materials/console/") ||
      path_starts_with(filename, "debug/"))
  {
    return false;
  }

  if (path_starts_with(filename, "replay/") ||
      path_starts_with(filename, "resource/replay/") ||
      path_starts_with(filename, "materials/vgui/replay/") ||
      path_starts_with(filename, "media/") ||
      path_starts_with(filename, "videos/"))
  {
    return true;
  }

  if (extension.empty())
  {
    return false;
  }

  if (path_equals(extension, ".vmt"))
  {
    return !path_contains(filename, "corner") &&
           !path_contains(filename, "hud") &&
           !path_contains(filename, "vgui") &&
           !path_contains(filename, "console");
  }

  if (path_contains(filename, "sound.cache") ||
      path_contains(filename, "tf2_sound") ||
      path_contains(filename, "game_sounds") ||
      path_starts_with(filename, "sound/player/footsteps"))
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

  for (const std::string_view blocked_extension : blocked_extensions)
  {
    if (path_equals(extension, blocked_extension))
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
  if (should_block_file(filename))
  {
    return nullptr;
  }

  return open_original(this_ptr, filename, options, path_id);
}

bool precache_hook(void* this_ptr, const char* filename, const char* path_id)
{
  if (should_block_file(filename))
  {
    return false;
  }

  return precache_original(this_ptr, filename, path_id);
}

bool file_exists_hook(void* this_ptr, const char* filename, const char* path_id)
{
  if (should_block_file(filename))
  {
    return false;
  }

  return file_exists_original(this_ptr, filename, path_id);
}

bool read_file_hook(void* this_ptr, const char* filename, const char* path, void* buffer, int max_bytes, int starting_byte, void* alloc_fn)
{
  if (should_block_file(filename))
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
  while (filename != nullptr && should_block_file(filename));

  return filename;
}

const char* find_first_hook(void* this_ptr, const char* wildcard, file_find_handle_t* handle)
{
  const char* filename = find_first_original(this_ptr, wildcard, handle);
  while (filename != nullptr && handle != nullptr && should_block_file(filename))
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
  if (should_block_file(filename))
  {
    return nullptr;
  }

  return open_ex_original(this_ptr, filename, options, flags, path_id, resolved_filename);
}

int read_file_ex_hook(void* this_ptr, const char* filename, const char* path, void** buffer, bool null_terminate, bool optimal_alloc, int max_bytes, int starting_byte, void* alloc_fn)
{
  if (should_block_file(filename))
  {
    return 0;
  }

  return read_file_ex_original(this_ptr, filename, path, buffer, null_terminate, optimal_alloc, max_bytes, starting_byte, alloc_fn);
}

void add_files_to_cache_hook(void* this_ptr, file_cache_handle_t cache_id, const char** filenames, int filename_count, const char* path_id)
{
  if (filenames == nullptr || filename_count <= 0)
  {
    return;
  }

  std::vector<const char*> allowed_filenames{};
  allowed_filenames.reserve(static_cast<std::size_t>(filename_count));

  for (int index = 0; index < filename_count; ++index)
  {
    const char* filename = filenames[index];
    if (filename != nullptr && !should_block_file(filename))
    {
      allowed_filenames.emplace_back(filename);
    }
  }

  if (allowed_filenames.empty())
  {
    return;
  }

  add_files_to_cache_original(
    this_ptr,
    cache_id,
    allowed_filenames.data(),
    static_cast<int>(allowed_filenames.size()),
    path_id);
}

void* resolve_rip_target(std::uint8_t* instruction, int displacement_offset, int instruction_size)
{
  const auto displacement = *reinterpret_cast<std::int32_t*>(instruction + displacement_offset);
  return instruction + instruction_size + displacement;
}

std::uint8_t* scan_module_patch(const char* module_name, const char* signature, int offset)
{
  auto* match = reinterpret_cast<std::uint8_t*>(sigscan_module(module_name, signature));
  if (match == nullptr)
  {
    return nullptr;
  }

  return match + offset;
}

std::uint8_t* scan_client_patch(const char* signature, int offset)
{
  return scan_module_patch("client.so", signature, offset);
}

bool initialize_optional_patch(byte_patch& patch,
                               const char* module_name,
                               const char* signature,
                               int offset,
                               std::initializer_list<std::uint8_t> patch_bytes,
                               const char* patch_name)
{
  auto* patch_site = scan_module_patch(module_name, signature, offset);
  if (patch_site == nullptr)
  {
    print("[nographics] optional patch scan failed name=%s module=%s\n", patch_name, module_name);
    return false;
  }

  patch = byte_patch(patch_site, patch_bytes);
  return true;
}

bool initialize_core_render_patch(byte_patch& patch,
                                  const char* module_name,
                                  const char* signature,
                                  int offset,
                                  std::initializer_list<std::uint8_t> patch_bytes,
                                  const char* patch_name)
{
  auto* patch_site = scan_module_patch(module_name, signature, offset);
  if (patch_site == nullptr)
  {
    print("[nographics] core patch scan failed name=%s module=%s\n", patch_name, module_name);
    return false;
  }

  patch = byte_patch(patch_site, patch_bytes);
  return true;
}

void initialize_engine_render_patches()
{
  if (engine_render_patches_initialized || !module_is_loaded("engine.so"))
  {
    return;
  }

  engine_render_patches_initialized = true;
  initialize_optional_patch(video_mode_setup_startup_graphic_patch, "engine.so", sigs::video_mode_setup_startup_graphic, 0, { 0xC3 }, "video_mode_setup_startup_graphic");
  initialize_optional_patch(cl_decay_lights_patch, "engine.so", sigs::cl_decay_lights, 0, { 0xC3 }, "cl_decay_lights");
  initialize_optional_patch(mod_load_lighting_patch, "engine.so", sigs::mod_load_lighting, 0, { 0x31, 0xC0, 0xC3 }, "mod_load_lighting");
  initialize_optional_patch(mod_load_worldlights_patch, "engine.so", sigs::mod_load_worldlights, 0, { 0x31, 0xC0, 0xC3 }, "mod_load_worldlights");
  initialize_optional_patch(sprite_load_model_patch, "engine.so", sigs::sprite_load_model, 0, { 0xC3 }, "sprite_load_model");
  initialize_optional_patch(overlay_mgr_load_overlays_patch, "engine.so", sigs::overlay_mgr_load_overlays, 0, { 0xB0, 0x01, 0xC3 }, "overlay_mgr_load_overlays");
}

void initialize_materialsystem_render_patches()
{
  if (materialsystem_render_patches_initialized || !module_is_loaded("materialsystem.so"))
  {
    return;
  }

  materialsystem_render_patches_initialized = true;
  initialize_optional_patch(material_system_begin_frame_patch, "materialsystem.so", sigs::material_system_begin_frame, 0, { 0xC3 }, "material_system_begin_frame");
}

void initialize_optional_render_patches()
{
  initialize_engine_render_patches();
  initialize_materialsystem_render_patches();
}

void restore_optional_render_patches()
{
  video_mode_setup_startup_graphic_patch.restore();
  cl_decay_lights_patch.restore();
  mod_load_lighting_patch.restore();
  mod_load_worldlights_patch.restore();
  sprite_load_model_patch.restore();
  overlay_mgr_load_overlays_patch.restore();
  material_system_begin_frame_patch.restore();
  optional_render_patches_applied = false;
}

bool apply_optional_patch(byte_patch& patch, const char* patch_name)
{
  if (!patch.valid())
  {
    return false;
  }

  if (!patch.apply())
  {
    print("[nographics] optional patch apply failed name=%s\n", patch_name);
    return false;
  }

  return true;
}

bool apply_optional_render_patches()
{
  initialize_optional_render_patches();

  bool applied_any_patch = false;
  applied_any_patch = apply_optional_patch(video_mode_setup_startup_graphic_patch, "video_mode_setup_startup_graphic") || applied_any_patch;
  applied_any_patch = apply_optional_patch(cl_decay_lights_patch, "cl_decay_lights") || applied_any_patch;
  applied_any_patch = apply_optional_patch(mod_load_lighting_patch, "mod_load_lighting") || applied_any_patch;
  applied_any_patch = apply_optional_patch(mod_load_worldlights_patch, "mod_load_worldlights") || applied_any_patch;
  applied_any_patch = apply_optional_patch(sprite_load_model_patch, "sprite_load_model") || applied_any_patch;
  applied_any_patch = apply_optional_patch(overlay_mgr_load_overlays_patch, "overlay_mgr_load_overlays") || applied_any_patch;
  applied_any_patch = apply_optional_patch(material_system_begin_frame_patch, "material_system_begin_frame") || applied_any_patch;
  optional_render_patches_applied = optional_render_patches_applied || applied_any_patch;
  return applied_any_patch;
}

bool apply_render_patch_if_valid(byte_patch& patch, const char* patch_name)
{
  if (!patch.valid())
  {
    return true;
  }

  if (!patch.apply())
  {
    print("[nographics] render patch apply failed name=%s\n", patch_name);
    return false;
  }

  return true;
}

bool initialize_replay_ui_nullcheck_patches()
{
  constexpr int call_offset_after_global_load = 13;
  constexpr int call_offset_after_saved_return = 16;
  constexpr int call_offset_at_start = 6;

  std::array<std::uint8_t*, replay_ui_nullcheck_patch_count> patch_sites = {
    scan_client_patch(sigs::replay_ui_nullcheck_0, call_offset_after_global_load),
    scan_client_patch(sigs::replay_ui_nullcheck_1, call_offset_after_global_load),
    scan_client_patch(sigs::replay_ui_nullcheck_2, call_offset_after_saved_return),
    scan_client_patch(sigs::replay_ui_nullcheck_3, call_offset_at_start),
    scan_client_patch(sigs::replay_ui_nullcheck_4, call_offset_after_global_load),
    scan_client_patch(sigs::replay_ui_nullcheck_5, call_offset_after_global_load),
    scan_client_patch(sigs::replay_ui_nullcheck_6, call_offset_after_global_load),
    scan_client_patch(sigs::replay_ui_nullcheck_7, call_offset_after_global_load),
    scan_client_patch(sigs::replay_ui_nullcheck_8, call_offset_after_global_load),
  };

  bool initialized_any_patch = false;
  for (std::size_t index = 0; index < patch_sites.size(); ++index)
  {
    if (patch_sites[index] == nullptr)
    {
      print("[nographics] optional replay ui nullcheck patch scan failed index=%zu\n", index);
      continue;
    }

    if (index == 6 || index == 8)
    {
      replay_ui_nullcheck_patches[index] = byte_patch(patch_sites[index], { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 });
    }
    else
    {
      replay_ui_nullcheck_patches[index] = byte_patch(patch_sites[index], { 0x30, 0xC0, 0x90 });
    }

    initialized_any_patch = true;
  }

  return initialized_any_patch;
}

bool initialize_extra_crashfix_patches()
{
  constexpr std::array<const char*, character_info_command_patch_count> character_info_command_sigs = {
    sigs::character_info_open,
    sigs::character_info_open_direct,
    sigs::character_info_open_backpack,
    sigs::character_info_open_crafting,
    sigs::character_info_open_armory,
  };

  auto* item_definition_index = scan_client_patch(sigs::econ_item_view_get_item_definition_index, 0);
  if (item_definition_index == nullptr)
  {
    print("[nographics] optional econ item definition index patch scan failed\n");
  }

  bool initialized_any_patch = false;
  for (std::size_t index = 0; index < character_info_command_sigs.size(); ++index)
  {
    auto* patch_site = scan_client_patch(character_info_command_sigs[index], 0);
    if (patch_site == nullptr)
    {
      print("[nographics] optional character info command patch scan failed index=%zu\n", index);
      continue;
    }

    character_info_command_patches[index] = byte_patch(patch_site, { 0x31, 0xC0, 0xC3 });
    initialized_any_patch = true;
  }

  if (item_definition_index != nullptr)
  {
    econ_item_definition_index_patch = byte_patch(item_definition_index, {
      0x48, 0x8B, 0x47, 0x08,
      0x48, 0x85, 0xC0,
      0x75, 0x0F,
      0x48, 0x8B, 0x07,
      0x48, 0x85, 0xC0,
      0x74, 0x0B,
      0x8B, 0x80, 0xBC, 0x00, 0x00, 0x00,
      0xC3,
      0x8B, 0x40, 0x20,
      0xC3,
      0x31, 0xC0,
      0xC3,
      0x90,
    });
    initialized_any_patch = true;
  }

  return initialized_any_patch;
}

void restore_render_patch_objects()
{
  dispatch_anim_events_patch.restore();
  particle_create_patch.restore();
  particle_precache_patch.restore();
  particle_effect_create_patch.restore();
  view_render_patch.restore();
  replay_screenshot_patch.restore();
  steam_rich_presence_patch.restore();
  restore_optional_render_patches();

  for (auto& patch : replay_ui_nullcheck_patches)
  {
    patch.restore();
  }

  for (auto& patch : character_info_command_patches)
  {
    patch.restore();
  }

  econ_item_definition_index_patch.restore();
}

bool initialize_render_patches()
{
  if (render_patches_initialized)
  {
    return render_patches_ready;
  }

  if (!module_is_loaded("client.so"))
  {
    return false;
  }

  render_patches_initialized = true;

  std::size_t core_patch_count = 0;
  // Keep animation events alive in textmode. Stubbing this can leave client animation
  // state stale enough to break hitscan hitbox alignment.
  if (initialize_core_render_patch(particle_create_patch, "client.so", sigs::particle_property_create, 0, { 0x31, 0xC0, 0xC3 }, "particle_property_create"))
  {
    ++core_patch_count;
  }
  if (initialize_core_render_patch(particle_precache_patch, "client.so", sigs::particle_system_precache, 0, { 0x31, 0xC0, 0xC3 }, "particle_system_precache"))
  {
    ++core_patch_count;
  }
  if (initialize_core_render_patch(particle_effect_create_patch, "client.so", sigs::particle_effect_create_event, 0, { 0x31, 0xC0, 0xC3 }, "particle_effect_create_event"))
  {
    ++core_patch_count;
  }
  if (initialize_core_render_patch(view_render_patch, "client.so", sigs::view_render_render, 0, { 0xC3 }, "view_render_render"))
  {
    ++core_patch_count;
  }

  initialize_optional_patch(replay_screenshot_patch, "client.so", sigs::replay_screenshot_render, 0, { 0xB0, 0x01, 0xC3 }, "replay_screenshot_render");
  initialize_optional_patch(steam_rich_presence_patch, "client.so", sigs::client_update_steam_rich_presence, 0, { 0xC3 }, "client_update_steam_rich_presence");
  initialize_replay_ui_nullcheck_patches();
  initialize_extra_crashfix_patches();

  render_patches_ready = core_patch_count != 0;
  if (!render_patches_ready)
  {
    print("[nographics] no core render patches initialized\n");
  }

  return render_patches_ready;
}

void apply_render_patches()
{
  const bool core_patches_ready = initialize_render_patches();
  bool ok = true;

  if (core_patches_ready)
  {
    ok = apply_render_patch_if_valid(particle_create_patch, "particle_property_create") && ok;
    ok = apply_render_patch_if_valid(particle_precache_patch, "particle_system_precache") && ok;
    ok = apply_render_patch_if_valid(particle_effect_create_patch, "particle_effect_create_event") && ok;
    ok = apply_render_patch_if_valid(view_render_patch, "view_render_render") && ok;
    ok = apply_render_patch_if_valid(replay_screenshot_patch, "replay_screenshot_render") && ok;
    ok = apply_render_patch_if_valid(steam_rich_presence_patch, "client_update_steam_rich_presence") && ok;
    for (auto& patch : replay_ui_nullcheck_patches)
    {
      ok = apply_render_patch_if_valid(patch, "replay_ui_nullcheck") && ok;
    }
    for (auto& patch : character_info_command_patches)
    {
      ok = apply_render_patch_if_valid(patch, "character_info_command") && ok;
    }
    ok = apply_render_patch_if_valid(econ_item_definition_index_patch, "econ_item_definition_index") && ok;
  }

  if (!ok)
  {
    restore_render_patch_objects();
    render_patches_applied = false;
    optional_render_patches_applied = false;
    print("[nographics] render patch apply failed\n");
    return;
  }

  if (should_apply_extra_render_patches())
  {
    apply_optional_render_patches();
  }
  else
  {
    restore_optional_render_patches();
  }

  render_patches_applied = render_patches_applied || core_patches_ready;
}

void restore_render_patches()
{
  if (!render_patches_applied && !optional_render_patches_applied)
  {
    return;
  }

  restore_render_patch_objects();
  render_patches_applied = false;
  optional_render_patches_applied = false;
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
  ok &= hook_vtable(base_file_system_vtable, base_file_system_file_exists_index, reinterpret_cast<void*>(file_exists_hook), &file_exists_original);
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
  if (file_exists_original != nullptr) write_to_table(base_file_system_vtable, base_file_system_file_exists_index, reinterpret_cast<void*>(file_exists_original));
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

  if (game_file_system != nullptr || !module_is_loaded("client.so"))
  {
    return;
  }

  auto* match = reinterpret_cast<std::uint8_t*>(sigscan_module("client.so", sigs::client_file_system));
  if (match != nullptr)
  {
    game_file_system = *reinterpret_cast<file_system**>(resolve_rip_target(match + 15, 3, 7));
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

} // namespace

void initialize()
{
  if constexpr (textmode_build)
  {
    config.misc.exploits.null_graphics = true;
    config.misc.exploits.null_graphics_render_stubs = true;
  }

  resolve_game_file_system_interface();

  if (game_file_system != nullptr)
  {
    initialized = true;
    return;
  }

  if (!initialized && module_is_loaded("client.so"))
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
    apply_render_patches();
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
  else
  {
    (void)library_path;
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
  return textmode_build || (config.misc.exploits.null_graphics && config.misc.exploits.null_graphics_render_stubs);
}

bool should_use_aimbot_trace_fallback()
{
  return is_enabled() && should_skip_rendering_hooks();
}

} // namespace nographics
