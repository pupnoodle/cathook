/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/cathook.cpp
 |  Y  |   autor: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include <SDL2/SDL_events.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <unistd.h>
#include <csignal>

#include "core/logger.hpp"
#include "core/config/config_store.hpp"
#include "core/commands.hpp"
#include "core/ipc/ipc_client.hpp"
#include "core/diagnostics/exception_handler.hpp"
#include "features/menu/binds.hpp"

#include "core/print.hpp"
#include "core/assert.hpp"
#include "core/detach.hpp"

#include "core/types.hpp"
#include "core/memory/memory.hpp"
#include "core/shared/sigs.hpp"

#include "games/tf2/sdk/materials/keyvalues.hpp"

#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/debug_overlay.hpp"
#include "games/tf2/sdk/interfaces/surface.hpp"
#include "games/tf2/sdk/interfaces/engine_trace.hpp"
#include "games/tf2/sdk/interfaces/render_view.hpp"
#include "games/tf2/sdk/interfaces/material_system.hpp"
#include "games/tf2/sdk/interfaces/model_info.hpp"
#include "games/tf2/sdk/interfaces/model_render.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/prediction.hpp"
#include "games/tf2/sdk/interfaces/steam_client.hpp"
#include "games/tf2/sdk/interfaces/steam_friends.hpp"
#include "games/tf2/sdk/interfaces/steam_networking_utils.hpp"
#include "games/tf2/sdk/interfaces/input.hpp"
#include "games/tf2/sdk/interfaces/attribute_manager.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/move_helper.hpp"
#include "games/tf2/sdk/interfaces/game_movement.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/interfaces/game_event_manager.hpp"
#include "games/tf2/sdk/interfaces/file_system.hpp"

#include "libsigscan/libsigscan.h"
#include "funchook/funchook.h"

#include "core/hooks/sdl.cpp"
#include "core/hooks/vulkan.cpp"

#include "core/overwrite_dlopen.cpp"
#include "core/hooks/hooks.cpp"
#include "core/ipc/ipc_client.cpp"
#include "core/player_manager.cpp"
#include "features/combat/tickbase/tickbase.cpp"
#include "features/combat/anti_aim/anti_aim.cpp"
#include "core/hooks/cl_read_packets.cpp"
#include "core/hooks/cl_move.cpp"
#include "core/hooks/client_mode_create_move.cpp"
#include "core/hooks/client_create_move.cpp"
#include "core/hooks/calc_is_attack_critical.cpp"
#include "core/hooks/equip_region_unlock.cpp"
#include "core/hooks/region_selector.cpp"
#include "core/hooks/tf_gc_client_system.cpp"
#include "core/hooks/setup_bones.cpp"
#include "core/hooks/inspect_target.cpp"
#include "core/hooks/paint_traverse.cpp"
#include "core/hooks/override_view.cpp"
#include "core/hooks/draw_view_model.cpp"
#include "core/hooks/do_post_screen_space_effects.cpp"
#include "core/hooks/in_cond.cpp"
#include "core/hooks/forced_material_override.cpp"
#include "core/hooks/draw_model_execute.cpp"
#include "core/hooks/load_white_list.cpp"
#include "core/hooks/should_draw_local_player.cpp"
#include "core/hooks/should_draw_this_player.cpp"
#include "core/hooks/draw_view_models.cpp"
#include "core/hooks/fire_event_client_side.cpp"
#include "core/hooks/frame_stage_notify.cpp"
#include "core/hooks/dispatch_user_message.cpp"
#include "core/hooks/intro_menu_on_tick.cpp"
#include "core/hooks/class_menu_show_panel.cpp"
#include "core/hooks/team_menu_show_panel.cpp"
#include "core/hooks/map_info_menu_show_panel.cpp"
#include "core/hooks/text_window_show_panel.cpp"
#include "core/hooks/scene_end.cpp"

#include "core/random_seed.hpp"

#include "features/automation/navbot/navbot_mesh.cpp"
#include "features/automation/navbot/navbot_hazards.cpp"
#include "features/automation/navbot/navbot_path.cpp"
#include "features/automation/navbot/navbot_jobs.cpp"
#include "features/automation/medic_automation/medic_automation.cpp"
#include "features/automation/navbot/navbot_goals.cpp"
#include "features/automation/navbot/navbot_follow.cpp"
#include "features/automation/navbot/navbot_debug.cpp"
#include "features/automation/nographics/nographics.cpp"
#include "features/automation/region_selector/region_selector.cpp"
#include "features/automation/autoitem/autoitem.cpp"
#include "features/automation/misc/misc.cpp"
#include "features/automation/navbot/navbot_controller.cpp"
#include "features/visuals/hitmarker.cpp"
#include "features/visuals/spectator_list.cpp"
#include "features/visuals/thirdperson.cpp"
#include "features/visuals/glow/player_model_glow.cpp"

void** client_mode_vtable;
void** vgui_vtable;
void** client_vtable;
void** model_render_vtable;
void** game_event_manager_vtable;
void** render_view_vtable;
void** steam_networking_utils_vtable;

funchook_t* funchook;

bool initialize_game_runtime();

namespace
{

constexpr int steam_networking_utils_get_ping_to_data_center_index = 8;
constexpr int steam_networking_utils_get_direct_ping_to_pop_index = 9;

} // namespace

namespace
{

steam_networking_utils* resolve_steam_networking_utils()
{
  using steam_networking_utils_factory_fn = steam_networking_utils* (*)();

  if (auto* factory = reinterpret_cast<steam_networking_utils_factory_fn>(
        dlsym(RTLD_DEFAULT, "SteamAPI_SteamNetworkingUtils_SteamAPI_v004")))
  {
    if (auto* utils = factory())
    {
      return utils;
    }
  }

  constexpr const char* modules[] = {
    "./bin/linux64/steamclient.so",
    "steamclient.so",
    "./bin/linux64/libsteam_api.so",
    "libsteam_api.so",
  };

  constexpr const char* versions[] = {
    "SteamNetworkingUtils004",
    "SteamNetworkingUtils003",
  };

  for (const char* module : modules)
  {
    for (const char* version : versions)
    {
      if (auto* utils = static_cast<steam_networking_utils*>(get_interface(module, version)))
      {
        return utils;
      }
    }

    void* handle = dlopen(module, RTLD_NOW | RTLD_NOLOAD);
    if (handle == nullptr)
    {
      continue;
    }

    auto* factory = reinterpret_cast<steam_networking_utils_factory_fn>(
      dlsym(handle, "SteamAPI_SteamNetworkingUtils_SteamAPI_v004"));
    auto* utils = factory != nullptr ? factory() : nullptr;
    dlclose(handle);

    if (utils != nullptr)
    {
      return utils;
    }
  }

  return nullptr;
}

} // namespace

bool install_steam_networking_utils_hooks()
{
  if (steam_networking_utils_get_ping_to_data_center_original != nullptr &&
      steam_networking_utils_get_direct_ping_to_pop_original != nullptr)
  {
    return true;
  }

  if (steam_networking_utils_interface == nullptr)
  {
    steam_networking_utils_interface = resolve_steam_networking_utils();
  }

  if (steam_networking_utils_interface == nullptr)
  {
    static bool warned_missing = false;
    if (!warned_missing)
    {
      print("SteamNetworkingUtils interface missing; region selector ping hooks disabled\n");
      warned_missing = true;
    }
    return false;
  }

  steam_networking_utils_vtable = *reinterpret_cast<void***>(steam_networking_utils_interface);
  if (steam_networking_utils_vtable == nullptr)
  {
    print("SteamNetworkingUtils vtable missing; region selector ping hooks disabled\n");
    return false;
  }

  bool hooks_installed = true;
  if (steam_networking_utils_get_ping_to_data_center_original == nullptr)
  {
    steam_networking_utils_get_ping_to_data_center_original =
      reinterpret_cast<int (*)(void*, steam_networking_pop_id, steam_networking_pop_id*)>(
        steam_networking_utils_vtable[steam_networking_utils_get_ping_to_data_center_index]);

    if (!write_to_table(
          steam_networking_utils_vtable,
          steam_networking_utils_get_ping_to_data_center_index,
          reinterpret_cast<void*>(steam_networking_utils_get_ping_to_data_center_hook)))
    {
      steam_networking_utils_get_ping_to_data_center_original = nullptr;
      hooks_installed = false;
      print("ISteamNetworkingUtils::GetPingToDataCenter hook failed\n");
    }
    else
    {
      print("ISteamNetworkingUtils::GetPingToDataCenter hooked\n");
    }
  }

  if (steam_networking_utils_get_direct_ping_to_pop_original == nullptr)
  {
    steam_networking_utils_get_direct_ping_to_pop_original =
      reinterpret_cast<int (*)(void*, steam_networking_pop_id)>(
        steam_networking_utils_vtable[steam_networking_utils_get_direct_ping_to_pop_index]);

    if (!write_to_table(
          steam_networking_utils_vtable,
          steam_networking_utils_get_direct_ping_to_pop_index,
          reinterpret_cast<void*>(steam_networking_utils_get_direct_ping_to_pop_hook)))
    {
      steam_networking_utils_get_direct_ping_to_pop_original = nullptr;
      hooks_installed = false;
      print("ISteamNetworkingUtils::GetDirectPingToPOP hook failed\n");
    }
    else
    {
      print("ISteamNetworkingUtils::GetDirectPingToPOP hooked\n");
    }
  }

  return hooks_installed;
}

namespace cathook::core
{

std::atomic_bool runtime_initialized = false;
std::atomic_bool unload_started = false;
std::atomic_bool process_exiting = false;
std::atomic_bool attach_worker_started = false;
std::atomic_bool attach_worker_complete = false;
std::atomic_bool attach_worker_stop = false;

constexpr auto attach_wait_step = std::chrono::milliseconds(100);
constexpr long attach_ready_delay_default_seconds = 0;
constexpr long attach_ready_delay_max_seconds = 300;

constexpr std::string_view steamclient_module = "steamclient.so";

constexpr std::array<const char*, 16> game_events = {
  "client_beginconnect",
  "client_connected",
  "client_disconnect",
  "game_newmap",
  "teamplay_round_start",
  "scorestats_accumulated_update",
  "mvm_reset_stats",
  "player_connect_client",
  "player_spawn",
  "player_changeclass",
  "player_hurt",
  "player_death",
  "vote_cast",
  "item_pickup",
  "revive_player_notify",
  "localplayer_respawn",
};

std::string find_loaded_module_path(const char* module_name, bool allow_prefix_match = false)
{
  std::ifstream maps{ "/proc/self/maps" };
  std::string line{};

  while (std::getline(maps, line)) {
    const auto path_offset = line.find('/');
    if (path_offset == std::string::npos) {
      continue;
    }

    const auto path = std::string_view{ line }.substr(path_offset);
    const auto name_offset = path.rfind('/');
    const auto file_name = name_offset == std::string_view::npos ? path : path.substr(name_offset + 1);

    if (file_name == module_name || (allow_prefix_match && file_name.starts_with(module_name))) {
      return std::string{ path };
    }
  }

  return {};
}

bool is_module_loaded(const char* module_name)
{
  return !find_loaded_module_path(module_name).empty();
}

bool wait_for_module(const char* module_name)
{
  auto next_missing_modules_log = std::chrono::steady_clock::now();
  const auto wait_start = std::chrono::steady_clock::now();

  while (!attach_worker_stop.load(std::memory_order_acquire)
      && !process_exiting.load(std::memory_order_acquire)) {
    if (is_module_loaded(module_name)) {
      return true;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now >= next_missing_modules_log) {
      const auto waited_seconds = std::chrono::duration_cast<std::chrono::seconds>(now - wait_start).count();
      print("cathook attach worker waiting for %s waited_seconds=%lld",
        module_name,
        static_cast<long long>(waited_seconds));
      if (std::string_view{module_name} == steamclient_module) {
        print(" (check ~/.steam/sdk64/steamclient.so and Steam runtime symlinks)");
      }
      print("\n");
      next_missing_modules_log = now + std::chrono::seconds(2);
    }

    std::this_thread::sleep_for(attach_wait_step);
  }

  return false;
}

void stop_attach_worker();

std::thread& attach_worker_thread()
{
  static auto* thread = new std::thread{};
  return *thread;
}

std::chrono::seconds attach_ready_delay()
{
  const char* value = std::getenv("CATHOOK_ATTACH_DELAY_SECONDS");
  if (value == nullptr || *value == '\0') {
    return std::chrono::seconds(attach_ready_delay_default_seconds);
  }

  char* end = nullptr;
  const long seconds = std::strtol(value, &end, 10);
  if (end == value || seconds < 0) {
    return std::chrono::seconds(attach_ready_delay_default_seconds);
  }

  return std::chrono::seconds(std::min(seconds, attach_ready_delay_max_seconds));
}

bool is_environment_enabled(const char* name)
{
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    return false;
  }

  return std::strcmp(value, "0") != 0
      && std::strcmp(value, "false") != 0
      && std::strcmp(value, "FALSE") != 0
      && std::strcmp(value, "no") != 0
      && std::strcmp(value, "NO") != 0;
}

void attach_worker_main()
{
  print("cathook attach worker initializing module stages\n");

  {
    const auto delay = attach_ready_delay();
    const auto delay_seconds = std::chrono::duration_cast<std::chrono::seconds>(delay).count();

    if (delay_seconds > 0) {
      print("cathook attach worker waiting %lld seconds for game runtime\n",
        static_cast<long long>(delay_seconds));

      const auto wait_until = std::chrono::steady_clock::now() + delay;
      while (std::chrono::steady_clock::now() < wait_until
          && !attach_worker_stop.load(std::memory_order_acquire)
          && !process_exiting.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(attach_wait_step);
      }
    }

    if (attach_worker_stop.load(std::memory_order_acquire)
        || process_exiting.load(std::memory_order_acquire)) {
      attach_worker_complete.store(true, std::memory_order_release);
      return;
    }

    print("cathook attach worker initializing runtime\n");
    const bool initialized = ::initialize_game_runtime();
    print("cathook attach worker initialize_game_runtime returned %d\n", initialized ? 1 : 0);
  }

  attach_worker_complete.store(true, std::memory_order_release);
}

bool start_attach_worker()
{
  if (runtime_initialized.load(std::memory_order_acquire)) {
    return true;
  }

  if (attach_worker_started.load(std::memory_order_acquire)
      && attach_worker_complete.load(std::memory_order_acquire)) {
    stop_attach_worker();
  }

  if (attach_worker_started.exchange(true, std::memory_order_acq_rel)) {
    return true;
  }

  attach_worker_stop.store(false, std::memory_order_release);
  attach_worker_complete.store(false, std::memory_order_release);
  attach_worker_thread() = std::thread{ attach_worker_main };
  return true;
}

void stop_attach_worker()
{
  attach_worker_stop.store(true, std::memory_order_release);

  auto& thread = attach_worker_thread();
  if (thread.joinable()) {
    if (std::this_thread::get_id() == thread.get_id()) {
      thread.detach();
    } else {
      thread.join();
    }
  }

  attach_worker_started.store(false, std::memory_order_release);
  attach_worker_complete.store(true, std::memory_order_release);
}

void shutdown_imgui_runtime()
{
  if (ImGui::GetCurrentContext() == nullptr) {
    return;
  }

  ImGui::DestroyContext();
}

void shutdown_vulkan_runtime()
{
  shutdown_vulkan_runtime_state();
}

void shutdown_gl_runtime()
{
  if (menu_gl_context != nullptr) {
    SDL_GL_DeleteContext(menu_gl_context);
  }

  menu_gl_context = nullptr;
  original_gl_context = nullptr;
}

bool unload_module_runtime() {
  stop_attach_worker();

  if (process_exiting.load(std::memory_order_acquire)) {
    runtime_initialized.store(false, std::memory_order_release);
    unload_started.store(false, std::memory_order_release);
    return true;
  }

  if (unload_started.exchange(true)) {
    return true;
  }

  if (!runtime_initialized.load(std::memory_order_acquire)) {
    unload_started.store(false, std::memory_order_release);
    return true;
  }

  print("Uninjecting...\n");
  cathook::core::unregister_commands();
  cat_ipc::client::shutdown();
  cathook::core::players::shutdown();

  print("Unhooking VMT functions\n");
  if (client_mode_vtable != nullptr && client_mode_create_move_original != nullptr && !write_to_table(client_mode_vtable, 22, (void*)client_mode_create_move_original)) {
    print("ClientMode::CreateMove failed to restore hook\n");
  }

  if (client_vtable != nullptr && client_create_move_original != nullptr && !write_to_table(client_vtable, 21, (void*)client_create_move_original)) {
    print("Client::CreateMove failed to restore hook\n");
  }

  if (client_mode_vtable != nullptr && override_view_original != nullptr && !write_to_table(client_mode_vtable, 17, (void*)override_view_original)) {
    print("OverrideView failed to restore hook\n");
  }

  if (client_mode_vtable != nullptr && draw_view_model_original != nullptr && !write_to_table(client_mode_vtable, 25, (void*)draw_view_model_original)) {
    print("ShouldDrawViewModel failed to restore hook\n");
  }

  if (client_mode_vtable != nullptr && do_post_screen_space_effects_original != nullptr &&
      !write_to_table(client_mode_vtable, 39, (void*)do_post_screen_space_effects_original)) {
    print("DoPostScreenSpaceEffects failed to restore hook\n");
  }

  if (vgui_vtable != nullptr && paint_traverse_original != nullptr && !write_to_table(vgui_vtable, 42, (void*)paint_traverse_original)) {
    print("PaintTraverse failed to restore hook\n");
  }

  if (model_render_vtable != nullptr && draw_model_execute_original != nullptr && !write_to_table(model_render_vtable, 19, (void*)draw_model_execute_original)) {
    print("DrawModelExecute failed to restore hook\n");
  }

  if (model_render_vtable != nullptr && forced_material_override_original != nullptr &&
      !write_to_table(model_render_vtable, 1, (void*)forced_material_override_original)) {
    print("ForcedMaterialOverride failed to restore hook\n");
  }

  if (game_event_manager_vtable != nullptr && fire_event_client_side_original != nullptr && !write_to_table(game_event_manager_vtable, 9, (void*)fire_event_client_side_original)) {
    print("FireEventClientSide failed to restore hook\n");
  }

  if (client_vtable != nullptr && frame_stage_notify_original != nullptr && !write_to_table(client_vtable, 35, (void*)frame_stage_notify_original)) {
    print("FrameStageNotify failed to restore hook\n");
  }

  if (client_vtable != nullptr && dispatch_user_message_original != nullptr && !write_to_table(client_vtable, 36, (void*)dispatch_user_message_original)) {
    print("DispatchUserMessage failed to restore hook\n");
  }

  if (render_view_vtable != nullptr && scene_end_original != nullptr && !write_to_table(render_view_vtable, 9, (void*)scene_end_original)) {
    print("SceneEnd failed to restore hook\n");
  }

  if (steam_networking_utils_vtable != nullptr &&
      steam_networking_utils_get_ping_to_data_center_original != nullptr &&
      !write_to_table(
        steam_networking_utils_vtable,
        steam_networking_utils_get_ping_to_data_center_index,
        (void*)steam_networking_utils_get_ping_to_data_center_original)) {
    print("ISteamNetworkingUtils::GetPingToDataCenter failed to restore hook\n");
  }

  if (steam_networking_utils_vtable != nullptr &&
      steam_networking_utils_get_direct_ping_to_pop_original != nullptr &&
      !write_to_table(
        steam_networking_utils_vtable,
        steam_networking_utils_get_direct_ping_to_pop_index,
        (void*)steam_networking_utils_get_direct_ping_to_pop_original)) {
    print("ISteamNetworkingUtils::GetDirectPingToPOP failed to restore hook\n");
  }

  nographics::shutdown();
  player_model_glow::shutdown();

  print("Unhooking Non-VMT functions\n");
  if (funchook != nullptr) {
    funchook_uninstall(funchook, 0);
    funchook_destroy(funchook);
    funchook = nullptr;
  }

  tickbase::reset();

  print("Unhooking SDL functions\n");
  SDL_SetEventFilter(nullptr, nullptr);

  void* lib_sdl_handle = dlopen("libSDL2-2.0.so.0", RTLD_LAZY | RTLD_NOLOAD);
  if (lib_sdl_handle != nullptr) {
    if (swap_window_original != nullptr && !restore_sdl_hook(lib_sdl_handle, "SDL_GL_SwapWindow", (void*)swap_window_original)) {
      print("Failed to restore SDL_GL_SwapWindow\n");
    }

    if (poll_event_original != nullptr && !restore_sdl_hook(lib_sdl_handle, "SDL_PollEvent", (void*)poll_event_original)) {
      print("Failed to restore SDL_PollEvent\n");
    }

    if (get_window_flags_original != nullptr && !restore_sdl_hook(lib_sdl_handle, "SDL_GetWindowFlags", (void*)get_window_flags_original)) {
      print("Failed to restore SDL_GetWindowFlags\n");
    }

    if (get_window_WM_info_original != nullptr && !restore_sdl_hook(lib_sdl_handle, "SDL_GetWindowWMInfo", (void*)get_window_WM_info_original)) {
      print("Failed to restore SDL_GetWindowWMInfo\n");
    }

    if (get_window_size_original != nullptr && !restore_sdl_hook(lib_sdl_handle, "SDL_GetWindowSize", (void*)get_window_size_original)) {
      print("Failed to restore SDL_GetWindowSize\n");
    }

    dlclose(lib_sdl_handle);
  }

  if (game_event_manager != nullptr) {
    game_event_manager->remove_listener((IGameEventListener*)game_event_manager);
  }

  shutdown_vulkan_runtime();
  shutdown_gl_runtime();
  shutdown_imgui_runtime();

  menu_focused = false;
  sdl_window = nullptr;
  detach_requested.store(false, std::memory_order_release);
  detach_started.store(false, std::memory_order_release);
  runtime_initialized.store(false, std::memory_order_release);
  unload_started.store(false, std::memory_order_release);

  cathook::core::exception_handler::uninstall();
  cathook::core::shutdown_config_store();
  cathook::core::shutdown_logger();
  return true;
}

bool is_runtime_detached()
{
  return !runtime_initialized.load(std::memory_order_acquire)
      && !detach_requested.load(std::memory_order_acquire)
      && !detach_started.load(std::memory_order_acquire)
      && !unload_started.load(std::memory_order_acquire);
}

bool initialize_module_runtime() {
  print("initialize_module_runtime enter\n");

  if (runtime_initialized.exchange(true, std::memory_order_acq_rel)) {
    print("initialize_module_runtime already initialized\n");
    return false;
  }

  print("initialize_module_runtime reset state\n");
  unload_started.store(false, std::memory_order_release);
  detach_requested.store(false, std::memory_order_release);
  detach_started.store(false, std::memory_order_release);

  print("initialize_module_runtime create directories\n");
  std::error_code error{};
  std::filesystem::create_directories(cathook::core::root_directory(), error);
  std::filesystem::create_directories(cathook::core::log_directory(), error);
  std::filesystem::create_directories(cathook::core::config_directory(), error);
  print("initialize_module_runtime initialize logger\n");
  cathook::core::initialize_logger(cathook::core::log_directory() / "cathook.log");
  print("initialize_module_runtime install exception handler\n");
  cathook::core::exception_handler::install(cathook::core::log_directory() / "exception.log");
  print("initialize_module_runtime initialize config store\n");
  cathook::core::initialize_config_store(cathook::core::root_directory());
  print("initialize_module_runtime load default config\n");
  cathook::core::load_default_config(config);
  print("initialize_module_runtime load binds\n");
  cat_bind::load(cathook::core::get_config_store());
  print("cathook bootstrap started\n");
  return true;
}

}

/*
void signal_handler(int signal) {
  error_box((
	     "Signal recieved: " + std::to_string(signal)
	     + "\n" + std::format("{:p}", __builtin_return_address(0))
	     + "\n" + std::format("{:p}", __builtin_return_address(1))
	     + "\n" + std::format("{:p}", __builtin_return_address(2))
	     + "\n" + std::format("{:p}", __builtin_return_address(3))
	     + "\n" + std::format("{:p}", __builtin_return_address(4))
	     ).c_str());
}
*/

void* resolve_rip_relative_address(void* instruction, std::ptrdiff_t displacement_offset, std::ptrdiff_t instruction_size)
{
  if (instruction == nullptr) {
    return nullptr;
  }

  auto* bytes = static_cast<std::uint8_t*>(instruction);
  std::int32_t displacement = 0;
  std::memcpy(&displacement, bytes + displacement_offset, sizeof(displacement));
  return bytes + instruction_size + displacement;
}

void** find_client_mode_storage_from_signature()
{
  static void** client_mode_storage = nullptr;
  static bool signature_scanned = false;

  if (signature_scanned) {
    return client_mode_storage;
  }

  signature_scanned = true;
  void* instruction = sigscan_module("client.so", sigs::client_mode_shared);
  if (instruction == nullptr) {
    print("ClientModeShared storage signature missing\n");
    return nullptr;
  }

  client_mode_storage = static_cast<void**>(resolve_rip_relative_address(instruction, 3, 7));
  print("ClientModeShared storage found at %p\n", static_cast<void*>(client_mode_storage));
  return client_mode_storage;
}

void** find_client_mode_storage_from_client()
{
  if (client_vtable == nullptr || client_vtable[10] == nullptr) {
    return nullptr;
  }

  void* instruction = client_vtable[10];
  const auto* bytes = static_cast<const std::uint8_t*>(instruction);
  if (bytes[0] != 0x48 || bytes[1] != 0x8d || bytes[2] != 0x05) {
    return nullptr;
  }

  return static_cast<void**>(resolve_rip_relative_address(instruction, 3, 7));
}

void* read_client_mode_interface()
{
  if (void** client_mode_storage = find_client_mode_storage_from_client(); client_mode_storage != nullptr) {
    if (void* client_mode_interface = *client_mode_storage; client_mode_interface != nullptr) {
      return client_mode_interface;
    }
  }

  if (void** client_mode_storage = find_client_mode_storage_from_signature(); client_mode_storage != nullptr) {
    return *client_mode_storage;
  }

  return nullptr;
}

void* wait_for_client_mode_interface()
{
  auto next_log = std::chrono::steady_clock::now();

  while (!cathook::core::attach_worker_stop.load(std::memory_order_acquire)
      && !cathook::core::process_exiting.load(std::memory_order_acquire)) {
    if (void* client_mode_interface = read_client_mode_interface(); client_mode_interface != nullptr) {
      return client_mode_interface;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now >= next_log) {
      print("Waiting for ClientModeShared\n");
      next_log = now + std::chrono::seconds(2);
    }

    std::this_thread::sleep_for(cathook::core::attach_wait_step);
  }

  return nullptr;
}

bool install_sdl_hooks()
{
#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE
  print("Textmode build; skipping SDL hooks\n");
  return false;
#else
  static bool sdl_hooks_installed = false;

  if (sdl_hooks_installed) {
    return true;
  }

  if (cathook::core::is_environment_enabled("CATHOOK_DISABLE_SDL_HOOKS")) {
    print("CATHOOK_DISABLE_SDL_HOOKS set; skipping SDL hooks\n");
    return false;
  }

  print("Installing SDL hooks\n");
  void* lib_sdl_handle = dlopen("libSDL2-2.0.so.0", RTLD_LAZY | RTLD_NOLOAD);
  if (lib_sdl_handle == nullptr) {
    const std::string sdl_path = cathook::core::find_loaded_module_path("libSDL2-2.0.so.0", true);
    if (!sdl_path.empty()) {
      print("SDL2 mapped at %s\n", sdl_path.c_str());
      lib_sdl_handle = dlopen(sdl_path.c_str(), RTLD_LAZY | RTLD_NOLOAD);
    }
  }

  if (lib_sdl_handle == nullptr) {
    print("SDL2 not loaded; skipping SDL hooks\n");
    return false;
  }

  print("SDL2 loaded at %p\n", lib_sdl_handle);

  bool sdl_hooks_ready = true;
  if (!sdl_hook(lib_sdl_handle, "SDL_PollEvent", (void*)poll_event_hook, (void **)&poll_event_original)) {
    print("Failed to hook SDL_PollEvent\n");
    sdl_hooks_ready = false;
  }

  if (!sdl_hook(lib_sdl_handle, "SDL_GL_SwapWindow", (void*)swap_window_hook, (void **)&swap_window_original)) {
    print("Failed to hook SDL_GL_SwapWindow\n");
    sdl_hooks_ready = false;
  }

  if (!sdl_hook(lib_sdl_handle, "SDL_GetWindowFlags", (void*)get_window_flags_hook, (void **)&get_window_flags_original)) {
    print("Failed to hook SDL_GetWindowFlags\n");
    sdl_hooks_ready = false;
  }

  if (!sdl_hook(lib_sdl_handle, "SDL_GetWindowWMInfo", (void*)get_window_WM_info_hook, (void **)&get_window_WM_info_original)) {
    print("Failed to hook SDL_GetWindowWMInfo\n");
    sdl_hooks_ready = false;
  }

  if (!sdl_hook(lib_sdl_handle, "SDL_GetWindowSize", (void*)get_window_size_hook, (void **)&get_window_size_original)) {
    print("Failed to hook SDL_GetWindowSize\n");
    sdl_hooks_ready = false;
  }

  if (!sdl_hooks_ready) {
    if (poll_event_original != nullptr) {
      restore_sdl_hook(lib_sdl_handle, "SDL_PollEvent", (void*)poll_event_original);
      poll_event_original = nullptr;
    }
    if (swap_window_original != nullptr) {
      restore_sdl_hook(lib_sdl_handle, "SDL_GL_SwapWindow", (void*)swap_window_original);
      swap_window_original = nullptr;
    }
    if (get_window_flags_original != nullptr) {
      restore_sdl_hook(lib_sdl_handle, "SDL_GetWindowFlags", (void*)get_window_flags_original);
      get_window_flags_original = nullptr;
    }
    if (get_window_WM_info_original != nullptr) {
      restore_sdl_hook(lib_sdl_handle, "SDL_GetWindowWMInfo", (void*)get_window_WM_info_original);
      get_window_WM_info_original = nullptr;
    }
    if (get_window_size_original != nullptr) {
      restore_sdl_hook(lib_sdl_handle, "SDL_GetWindowSize", (void*)get_window_size_original);
      get_window_size_original = nullptr;
    }
    print("SDL hooks disabled after incomplete install\n");
    dlclose(lib_sdl_handle);
    return false;
  }

  SDL_SetEventFilter(event_filter, nullptr);
  //SDL_AddEventWatch(event_watch, nullptr);
  sdl_hooks_installed = true;
  print("SDL hooks installed\n");
  dlclose(lib_sdl_handle);
  return true;
#endif
}

bool initialize_game_runtime() {
  print("initialize_game_runtime enter\n");

  if (!cathook::core::initialize_module_runtime()) {
    print("initialize_game_runtime module runtime already initialized\n");
    return cathook::core::runtime_initialized.load(std::memory_order_acquire);
  }

  print("initialize_game_runtime module runtime initialized\n");

  //std::signal(SIGKILL, signal_handler);
  
  // Interfaces
  if (!cathook::core::wait_for_module("engine.so")) {
    return false;
  }

  engine = (Engine*)get_interface("./bin/linux64/engine.so", "VEngineClient014");
  error_assert(engine == nullptr, "VEngineClient014 is missing");

  overlay = (DebugOverlay*)get_interface("./bin/linux64/engine.so", "VDebugOverlay003");
  error_assert(overlay == nullptr, "VDebugOverlay003 is missing");

  render_view = (RenderView*)get_interface("./bin/linux64/engine.so", "VEngineRenderView014");
  error_assert(render_view == nullptr, "VEngineRenderView014 is missing");

  engine_trace = (EngineTrace*)get_interface("./bin/linux64/engine.so", "EngineTraceClient003");
  error_assert(engine_trace == nullptr, "EngineTraceClient003 is missing");

  model_render = (ModelRender*)get_interface("./bin/linux64/engine.so", "VEngineModel016");
  error_assert(model_render == nullptr, "VEngineModel016 is missing");

  model_info = (ModelInfo*)get_interface("./bin/linux64/engine.so", "VModelInfoClient006");
  error_assert(model_info == nullptr, "VModelInfoClient006 is missing");

  unsigned long rcon_addr_change_address = (unsigned long)sigscan_module("engine.so", sigs::client_state);
  unsigned int client_state_eaddr = *(unsigned int*)(rcon_addr_change_address + 0x3);
  unsigned long rcon_addr_change_next_instruction = (unsigned long)(rcon_addr_change_address + 0x7);
  client_state = (ClientState*)((void*)(rcon_addr_change_next_instruction + client_state_eaddr));
  error_assert(client_state == nullptr, "CClientState is missing");

  if (!cathook::core::wait_for_module("vgui2.so")) {
    return false;
  }
  
  vgui = get_interface("./bin/linux64/vgui2.so", "VGUI_Panel009");
  error_assert(vgui == nullptr, "VGUI_Panel009 is missing");

  if (!cathook::core::wait_for_module("vguimatsurface.so")) {
    return false;
  }
  
  surface = (Surface*)get_interface("./bin/linux64/vguimatsurface.so", "VGUI_Surface030");
  error_assert(surface == nullptr, "VGUI_Surface030 is missing");

  if (!cathook::core::wait_for_module("materialsystem.so")) {
    return false;
  }

  material_system = (MaterialSystem*)get_interface("./bin/linux64/materialsystem.so", "VMaterialSystem082");
  error_assert(material_system == nullptr, "VMaterialSystem082 is missing");

  if (!cathook::core::wait_for_module("libvstdlib.so")) {
    return false;
  }

  convar_system = (ConvarSystem*)get_interface("./bin/linux64/libvstdlib.so", "VEngineCvar004");
  error_assert(convar_system == nullptr, "VEngineCvar004 is missing");

  if (!cathook::core::wait_for_module("steamclient.so")) {
    return false;
  }

  if (!cathook::core::wait_for_module("client.so")) {
    return false;
  }

  client = (Client*)get_interface("./tf/bin/linux64/client.so", "VClient017");
  error_assert(client == nullptr, "VClient017 is missing");
  
  unsigned long func_address = (unsigned long)sigscan_module("client.so", sigs::input);
  unsigned int input_eaddr = *(unsigned int*)(func_address + 0x3);
  unsigned long next_instruction = (unsigned long)(func_address + 0x7);
  input = (Input*)(*(void**)(next_instruction + input_eaddr));
  error_assert(input == nullptr, "CInput is missing");

  unsigned long check_stuck_address = (unsigned long)sigscan_module("client.so", sigs::move_helper);
  unsigned int move_helper_eaddr = *(unsigned int*)(check_stuck_address + 0x3);
  unsigned long check_stuck_next_instruction = (unsigned long)(check_stuck_address + 0x7);
  move_helper = (MoveHelper*)(*(void**)(check_stuck_next_instruction + move_helper_eaddr));
  error_assert(move_helper == nullptr, "CMoveHelper is missing");

  prediction = (Prediction*)get_interface("./tf/bin/linux64/client.so", "VClientPrediction001");
  error_assert(prediction == nullptr, "VClientPrediction001 is missing");
 
  game_movement = (GameMovement*)get_interface("./tf/bin/linux64/client.so", "GameMovement001");
  error_assert(game_movement == nullptr, "GameMovement001 is missing");
 
  entity_list = (EntityList*)get_interface("./tf/bin/linux64/client.so", "VClientEntityList003");
  error_assert(entity_list == nullptr, "VClientEntityList003 is missing");

  game_file_system = (file_system*)get_interface("./bin/linux64/filesystem_stdio.so", "VFileSystem022");
  if (game_file_system == nullptr) {
    game_file_system = (file_system*)get_interface("./bin/linux64/filesystem_steam.so", "VFileSystem022");
  }
  if (game_file_system == nullptr) {
    print("VFileSystem022 interface is missing; nographics filesystem hooks will use signature fallback\n");
  }
  install_sdl_hooks();
  nographics::initialize();
  nographics::prepare_render_patches();
  autoitem::initialize();
  cathook::core::players::initialize();

  cathook::core::register_commands();
  cathook::core::execute_startup_autoexec();
  cat_ipc::client::start();
 
  game_event_manager = (GameEventManager*)get_interface("./bin/linux64/engine.so", "GAMEEVENTSMANAGER002");
  error_assert(game_event_manager == nullptr, "GAMEEVENTSMANAGER002 is missing");
  {
    for (const char* event : cathook::core::game_events) {
      game_event_manager->add_listener((IGameEventListener*)game_event_manager, event, false);
      
      if (!game_event_manager->find_listener((IGameEventListener*)game_event_manager, event)) {
	print("Failed to add event listener: %s\n", event);
      }
    }
  }
  
  steam_client = nullptr;
  steam_friends = nullptr;
  print("Steam interfaces disabled during startup\n");

  install_steam_networking_utils_hooks();

  client_vtable = *(void ***)client;
  void* client_mode_interface = wait_for_client_mode_interface();
  error_assert(client_mode_interface == nullptr, "ClientModeShared is missing");
  
  unsigned long hud_update = (unsigned long)client_vtable[11];
  unsigned int global_vars_eaddr = *(unsigned int *)(hud_update + 0x16);
  unsigned long global_vars_next_instruction = (unsigned long)(hud_update + 0x1A);
  global_vars = (GlobalVars*)(*(void **)(global_vars_next_instruction + global_vars_eaddr));
  error_assert(global_vars == nullptr, "CGlobalVars is missing");

  in_cond_original = (bool (*)(void*, int))sigscan_module("client.so", sigs::in_cond);
  error_assert(in_cond_original == nullptr, "Failed to find InCond");
 
  // VMT Function Hooks
  client_mode_vtable = *(void***)client_mode_interface;  
  client_mode_create_move_original = (bool (*)(void*, float, user_cmd*))client_mode_vtable[22];
  if (!write_to_table(client_mode_vtable, 22, (void*)client_mode_create_move_hook)) {
    print("ClientModeShared::CreateMove hook failed\n");
  } else {
    print("ClientModeShared::CreateMove hooked\n");
  }
  
  client_create_move_original = (void (*)(void*, int, float, bool))client_vtable[21];
  if (!write_to_table(client_vtable, 21, (void*)client_create_move_hook)) {
    print("Client::CreateMove hook failed\n");
  } else {
    print("Client::CreateMove hooked\n");
  }
  
  override_view_original = (void (*)(void*, view_setup*))client_mode_vtable[17];  
  if (!write_to_table(client_mode_vtable, 17, (void*)override_view_hook)) {
    print("OverrideView hook failed\n");
  } else {
    print("OverrideView hooked\n");
  }

  draw_view_model_original = (bool (*)(void*))client_mode_vtable[25];  
  if (!write_to_table(client_mode_vtable, 25, (void*)draw_view_model_hook)) {
    print("ShouldDrawViewModel hook failed\n");
  } else {
    print("ShouldDrawViewModel hooked\n");
  }

  do_post_screen_space_effects_original = (bool (*)(void*, view_setup*))client_mode_vtable[39];
  if (!write_to_table(client_mode_vtable, 39, (void*)do_post_screen_space_effects_hook)) {
    print("DoPostScreenSpaceEffects hook failed\n");
  } else {
    print("DoPostScreenSpaceEffects hooked\n");
  }
  
  vgui_vtable = *(void ***)vgui;
  paint_traverse_original = (void (*)(void*, void*, bool, bool))vgui_vtable[42];  
  if (!write_to_table(vgui_vtable, 42, (void*)paint_traverse_hook)) {
    print("PaintTraverse hook failed\n");
  } else {
    print("PaintTraverse hooked\n");
  }

  model_render_vtable = *(void ***)model_render;    
  forced_material_override_original = (void (*)(void*, Material*, OverrideType))model_render_vtable[1];
  if (!write_to_table(model_render_vtable, 1, (void*)forced_material_override_hook)) {
    print("ForcedMaterialOverride hook failed\n");
  } else {
    print("ForcedMaterialOverride hooked\n");
  }

  draw_model_execute_original = (void (*)(void*, void*, ModelRenderInfo*, VMatrix*))model_render_vtable[19];  
  if (!write_to_table(model_render_vtable, 19, (void*)draw_model_execute_hook)) {
    print("DrawModelExecute hook failed\n");
  } else {
    print("DrawModelExecute hooked\n");
  }
  
  game_event_manager_vtable = *(void***)game_event_manager;
  fire_event_client_side_original = (bool (*)(void*, GameEvent*))game_event_manager_vtable[9];
  if (!write_to_table(game_event_manager_vtable, 9, (void*)fire_event_client_side_hook)) {
    print("FireEventClientSide hook failed\n");
  } else {
    print("FireEventClientSide hooked\n");
  }  

  
  frame_stage_notify_original = (void (*)(void*, ClientFrameStage))client_vtable[35];
  if (!write_to_table(client_vtable, 35, (void*)frame_stage_notify_hook)) {
    print("FrameStageNotify hook failed\n");
  } else {
    print("FrameStageNotify hooked\n");
  }  

  dispatch_user_message_original = (bool (*)(void*, int, bf_read*))client_vtable[36];
  if (!write_to_table(client_vtable, 36, (void*)dispatch_user_message_hook)) {
    print("DispatchUserMessage hook failed\n");
  } else {
    print("DispatchUserMessage hooked\n");
  }

  render_view_vtable = *(void***)render_view;

  scene_end_original = (void (*)(void*, void*))render_view_vtable[9];
  if (!write_to_table(render_view_vtable, 9, (void*)scene_end_hook)) {
    print("SceneEnd hook failed\n");
  } else {
    print("SceneEnd hooked\n");
  }  
  
  
  // Non-VMT Function hooks
  funchook = funchook_create();
  
  load_white_list_original = (void* (*)(void*, const char*))sigscan_module("engine.so", sigs::load_white_list);

  item_schema_lookup_map_original = (std::uintptr_t (*)())sigscan_module("client.so", sigs::item_schema_lookup_map);
  error_assert(item_schema_lookup_map_original == nullptr, "Failed to find item schema lookup map");

  item_definition_lookup_original =
    (std::uintptr_t (*)(std::uintptr_t, unsigned int))sigscan_module("client.so", sigs::item_definition_lookup);
  error_assert(item_definition_lookup_original == nullptr, "Failed to find item definition lookup");

  setup_bones_original = (bool (*)(void*, void*, int, int, float))sigscan_module("client.so", sigs::setup_bones);
  error_assert(setup_bones_original == nullptr, "Failed to find SetupBones");

  inspect_target_check_original = (std::int64_t (*)(void*, void*))sigscan_module("client.so", sigs::inspect_target_check);
  error_assert(inspect_target_check_original == nullptr, "Failed to find inspect target check");

  should_draw_local_player_original = (bool (*)(void*))sigscan_module("client.so", sigs::should_draw_local_player);

  should_draw_this_player_original = (bool (*)(void*))sigscan_module("client.so", sigs::should_draw_this_player);

  draw_view_models_original = (void (*)(void*, view_setup*, bool))sigscan_module("client.so", sigs::draw_view_models);

  attribute_hook_value_float_original = (float (*)(float, const char*, Entity*, void*, bool))sigscan_module("client.so", sigs::attribute_hook_value_float);

  intro_menu_on_tick_original = (void (*)(void*))sigscan_module("client.so", sigs::intro_menu_on_tick);
  
  class_menu_show_panel_original = (void (*)(void*, bool))sigscan_module("client.so", sigs::class_menu_show_panel);

  team_menu_show_panel_original = (void (*)(void*, bool))sigscan_module("client.so", sigs::team_menu_show_panel);

  map_info_menu_show_panel_original = (void (*)(void*, bool))sigscan_module("client.so", sigs::map_info_menu_show_panel);

  text_window_show_panel_original = (void (*)(void*, bool))sigscan_module("client.so", sigs::text_window_show_panel);

  calc_is_attack_critical_original = (bool (*)(void*))sigscan_module("client.so", sigs::calc_is_attack_critical);
  error_assert(calc_is_attack_critical_original == nullptr, "Failed to find CalcIsAttackCritical");

  cl_move_original = (tickbase::cl_move_fn)sigscan_module("engine.so", sigs::cl_move);
  error_assert(cl_move_original == nullptr, "Failed to find CL_Move");

  cl_read_packets_original = (std::int64_t (*)(char))sigscan_module("engine.so", sigs::cl_read_packets);
  error_assert(cl_read_packets_original == nullptr, "Failed to find CL_ReadPackets");

  region_selector_request_queue_for_match_original =
    (void (*)(void*, unsigned int))sigscan_module("client.so", sigs::request_queue_for_match);
  if (region_selector_request_queue_for_match_original == nullptr) {
    print("Failed to find CTFPartyClient::RequestQueueForMatch; region selector queue refresh disabled\n");
  }

  tf_gc_client_system_so_event_original =
    (void (*)(void*, void*, int))sigscan_module("client.so", sigs::tf_gc_client_system_so_event);
  if (tf_gc_client_system_so_event_original == nullptr) {
    print("Failed to find CTFGCClientSystem SO event handler; auto casual join disabled\n");
  }

  tf_gc_client_system_join_mm_match =
    (void (*)(void*))sigscan_module("client.so", sigs::tf_gc_client_system_join_mm_match);
  if (tf_gc_client_system_join_mm_match == nullptr) {
    print("Failed to find CTFGCClientSystem::JoinMMMatch; auto casual match joining disabled\n");
  }

  tf_gc_client_system_request_accept_match_invite =
    (void (*)(void*, std::uint64_t))sigscan_module("client.so", sigs::tf_gc_client_system_request_accept_match_invite);
  if (tf_gc_client_system_request_accept_match_invite == nullptr) {
    print("Failed to find CTFGCClientSystem::RequestAcceptMatchInvite; auto casual invite accepting disabled\n");
  }

  auto host_should_run = (tickbase::host_should_run_fn)sigscan_module("engine.so", sigs::host_should_run);
  error_assert(host_should_run == nullptr, "Failed to find Host_ShouldRun");

  prediction_run_simulation_original =
    (prediction_run_simulation_fn)sigscan_module("client.so", sigs::prediction_run_simulation);
  error_assert(prediction_run_simulation_original == nullptr, "Failed to find CPrediction::RunSimulation");

  initialize_cl_move_globals(host_should_run);
  
  int rv;
  
  rv = funchook_prepare(funchook, (void**)&in_cond_original, (void*)in_cond_hook);
  error_assert(rv != 0, "Failed to prepare InCond hook\n");

  rv = funchook_prepare(funchook, (void**)&load_white_list_original, (void*)load_white_list_hook);
  error_assert(rv != 0, "Failed to prepare LoadWhiteList hook\n");

  rv = funchook_prepare(funchook, (void**)&item_definition_lookup_original, (void*)item_definition_lookup_hook);
  error_assert(rv != 0, "Failed to prepare item definition lookup hook\n");

  rv = funchook_prepare(funchook, (void**)&setup_bones_original, (void*)setup_bones_hook);
  error_assert(rv != 0, "Failed to prepare SetupBones hook\n");

  rv = funchook_prepare(funchook, (void**)&inspect_target_check_original, (void*)inspect_target_check_hook);
  error_assert(rv != 0, "Failed to prepare inspect target check hook\n");

  rv = funchook_prepare(funchook, (void**)&should_draw_local_player_original, (void*)should_draw_local_player_hook);
  error_assert(rv != 0, "Failed to prepare ShouldDrawLocalPlayer hook\n");

  rv = funchook_prepare(funchook, (void**)&should_draw_this_player_original, (void*)should_draw_this_player_hook);
  error_assert(rv != 0, "Failed to prepare ShouldDrawThisPlayer hook\n");

  rv = funchook_prepare(funchook, (void**)&draw_view_models_original, (void*)draw_view_models_hook);
  error_assert(rv != 0, "Failed to prepare DrawViewModels hook\n");

  rv = funchook_prepare(funchook, (void**)&intro_menu_on_tick_original, (void*)intro_menu_on_tick_hook);
  error_assert(rv != 0, "Failed to prepare CTFIntroMenu::OnTick hook\n");
  
  rv = funchook_prepare(funchook, (void**)&class_menu_show_panel_original, (void*)class_menu_show_panel_hook);
  error_assert(rv != 0, "Failed to prepare CTFClassMenu::ShowPanel hook\n");

  rv = funchook_prepare(funchook, (void**)&team_menu_show_panel_original, (void*)team_menu_show_panel_hook);
  error_assert(rv != 0, "Failed to prepare CTFTeamMenu::ShowPanel hook\n");
  
  rv = funchook_prepare(funchook, (void**)&map_info_menu_show_panel_original, (void*)map_info_menu_show_panel_hook);
  error_assert(rv != 0, "Failed to prepare CTFMapInfoMenu::ShowPanel hook\n");

  rv = funchook_prepare(funchook, (void**)&text_window_show_panel_original, (void*)text_window_show_panel_hook);
  error_assert(rv != 0, "Failed to prepare CTFTextWindow::ShowPanel hook\n");

  rv = funchook_prepare(funchook, (void**)&calc_is_attack_critical_original, (void*)calc_is_attack_critical_hook);
  error_assert(rv != 0, "Failed to prepare CalcIsAttackCritical hook\n");

  rv = funchook_prepare(funchook, (void**)&cl_move_original, (void*)cl_move_hook);
  error_assert(rv != 0, "Failed to prepare CL_Move hook\n");

  rv = funchook_prepare(funchook, (void**)&cl_read_packets_original, (void*)cl_read_packets_hook);
  error_assert(rv != 0, "Failed to prepare CL_ReadPackets hook\n");

  if (region_selector_request_queue_for_match_original != nullptr) {
    rv = funchook_prepare(
      funchook,
      (void**)&region_selector_request_queue_for_match_original,
      (void*)region_selector_request_queue_for_match_hook);
    error_assert(rv != 0, "Failed to prepare CTFPartyClient::RequestQueueForMatch hook\n");
  }

  if (tf_gc_client_system_so_event_original != nullptr) {
    rv = funchook_prepare(
      funchook,
      (void**)&tf_gc_client_system_so_event_original,
      (void*)tf_gc_client_system_so_event_hook);
    error_assert(rv != 0, "Failed to prepare CTFGCClientSystem SO event hook\n");
  }

  rv = funchook_prepare(funchook, (void**)&prediction_run_simulation_original, (void*)prediction_run_simulation_hook);
  error_assert(rv != 0, "Failed to prepare CPrediction::RunSimulation hook\n");

  key_values_constructor_original = (KeyValues* (*)(void*, const char*))sigscan_module("client.so", sigs::key_values_constructor);
  error_assert(key_values_constructor_original == nullptr, "Failed to find KeyValues() constructor");

  key_values_set_int_original = (void (*)(void*, const char*, int))sigscan_module("client.so", sigs::key_values_set_int);
  error_assert(key_values_set_int_original == nullptr, "Failed to find KeyValues::SetInt()");
  rv = funchook_prepare(funchook, (void**)&key_values_set_int_original, (void*)key_values_set_int_hook);
  error_assert(rv != 0, "Failed to prepare KeyValues::SetInt() hook\n");
  
  key_values_load_from_buffer_original = (bool (*)(void*, const char*, const char*, void*, const char*))sigscan_module("client.so", sigs::key_values_load_from_buffer);
  error_assert(key_values_load_from_buffer_original == nullptr, "Failed to find KeyValues::LoadFromBuffer()");  
  
  // Hook Vulkan present when TF2 is using the native Vulkan shader API.
  if (get_module_base_address("shaderapivk.so") != nullptr) {
    void* lib_vulkan_handle = open_loaded_library("libvulkan.so.1");
    if (lib_vulkan_handle == nullptr) {
      lib_vulkan_handle = dlopen("/usr/lib/libvulkan.so.1", RTLD_LAZY | RTLD_NOLOAD);
    }
    if (lib_vulkan_handle == nullptr) {
      lib_vulkan_handle = dlopen("/usr/lib64/libvulkan.so.1", RTLD_LAZY | RTLD_NOLOAD);
    }
    if (lib_vulkan_handle == nullptr) {
      lib_vulkan_handle = dlopen("/usr/lib/x86_64-linux-gnu/libvulkan.so.1", RTLD_LAZY | RTLD_NOLOAD);
    }
    if (lib_vulkan_handle == nullptr) {
      lib_vulkan_handle = dlopen("/run/host/usr/lib/libvulkan.so.1", RTLD_LAZY | RTLD_NOLOAD);
    }
    if (lib_vulkan_handle == nullptr) {
      lib_vulkan_handle = dlopen("/run/host/usr/lib64/libvulkan.so.1", RTLD_LAZY | RTLD_NOLOAD);
    }
    
    if (lib_vulkan_handle != nullptr) {
      print("Vulkan loaded at %p\n", lib_vulkan_handle);

      // https://github.com/bruhmoment21/UniversalHookX/blob/main/UniversalHookX/src/hooks/backend/vulkan/hook_vulkan.cpp#L47
      VkInstanceCreateInfo create_info = {};
      constexpr const char* instance_extension = "VK_KHR_surface";
      
      create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
      create_info.enabledExtensionCount = 1;
      create_info.ppEnabledExtensionNames = &instance_extension;
  
      // Create Vulkan Instance without any debug feature
      const auto instance_result = vkCreateInstance(&create_info, vk_allocator, &vk_instance);
      error_assert(instance_result != VK_SUCCESS || vk_instance == VK_NULL_HANDLE, "Failed to create Vulkan dummy instance\n");
  
      uint32_t gpu_count = 0;
      auto enumerate_result = vkEnumeratePhysicalDevices(vk_instance, &gpu_count, nullptr);
      error_assert(enumerate_result != VK_SUCCESS || gpu_count == 0, "Failed to enumerate Vulkan physical devices\n");

      auto gpus = std::vector<VkPhysicalDevice>(gpu_count);
      enumerate_result = vkEnumeratePhysicalDevices(vk_instance, &gpu_count, gpus.data());
      error_assert(enumerate_result != VK_SUCCESS, "Failed to read Vulkan physical devices\n");

      // If a number >1 of GPUs got reported, find discrete GPU if present, or use first one available. This covers
      // most common cases (multi-gpu/integrated+dedicated graphics). Handling more complicated setups (multiple
      // dedicated GPUs) is out of scope of this sample.
      int use_gpu = 0;
      for (int i = 0; i < (int)gpu_count; ++i) {
	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(gpus[i], &properties);
	if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
	  use_gpu = i;
	  break;
	}
      }
	
      vk_physical_device = gpus[use_gpu];

      count = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(vk_physical_device, &count, nullptr);
      error_assert(count == 0, "Failed to enumerate Vulkan queue families\n");

      queue_families = std::make_unique<VkQueueFamilyProperties[]>(count);

      vkGetPhysicalDeviceQueueFamilyProperties(vk_physical_device, &count, queue_families.get());

      for (uint32_t i = 0; i < count; ++i) {
	if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
	  queue_family = i;
	  break;
	}
      }
  
      error_assert(queue_family == (uint32_t)-1, "queue_family fail\n");
  
      constexpr const char* device_extension = "VK_KHR_swapchain";
      constexpr const float queue_priority = 1.0f;

      VkDeviceQueueCreateInfo queue_info = { };
      queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      queue_info.queueFamilyIndex = queue_family;
      queue_info.queueCount = 1;
      queue_info.pQueuePriorities = &queue_priority;

      VkDeviceCreateInfo create_info2 = { };
      create_info2.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
      create_info2.queueCreateInfoCount = 1;
      create_info2.pQueueCreateInfos = &queue_info;
      create_info2.enabledExtensionCount = 1;
      create_info2.ppEnabledExtensionNames = &device_extension;

      VkDevice vk_fake_device = VK_NULL_HANDLE;

      const auto device_result = vkCreateDevice(vk_physical_device, &create_info2, vk_allocator, &vk_fake_device);
      error_assert(device_result != VK_SUCCESS || vk_fake_device == VK_NULL_HANDLE, "Failed to create Vulkan dummy device\n");

      create_device_original = (VkResult (*)(VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice*))vkGetInstanceProcAddr(vk_instance, "vkCreateDevice");
      queue_present_original = (VkResult (*)(VkQueue, const VkPresentInfoKHR*))vkGetDeviceProcAddr(vk_fake_device, "vkQueuePresentKHR");
      acquire_next_image_original = (VkResult (*)(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t*))vkGetDeviceProcAddr(vk_fake_device, "vkAcquireNextImageKHR");
      acquire_next_image2_original = (VkResult (*)(VkDevice, const VkAcquireNextImageInfoKHR*,  uint32_t*))vkGetDeviceProcAddr(vk_fake_device, "vkAcquireNextImage2KHR");
      create_swapchain_original = (VkResult (*)(VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*, VkSwapchainKHR*))vkGetDeviceProcAddr(vk_fake_device, "vkCreateSwapchainKHR");
      destroy_swapchain_original = (void (*)(VkDevice, VkSwapchainKHR, const VkAllocationCallbacks*))vkGetDeviceProcAddr(vk_fake_device, "vkDestroySwapchainKHR");
      get_device_queue_original = (void (*)(VkDevice, uint32_t, uint32_t, VkQueue*))vkGetDeviceProcAddr(vk_fake_device, "vkGetDeviceQueue");
      get_device_queue2_original = (void (*)(VkDevice, const VkDeviceQueueInfo2*, VkQueue*))vkGetDeviceProcAddr(vk_fake_device, "vkGetDeviceQueue2");

      vkDestroyDevice(vk_fake_device, vk_allocator);

      // Hook the functions
      rv = funchook_prepare(funchook, (void**)&queue_present_original, (void*)queue_present_hook);
      error_assert(rv != 0, "Failed to prepare vkQueuePresentKHR hook\n");

      rv = funchook_prepare(funchook, (void**)&acquire_next_image_original, (void*)acquire_next_image_hook);
      error_assert(rv != 0, "Failed to prepare vkAcquireNextImageKHR hook\n");

      rv = funchook_prepare(funchook, (void**)&acquire_next_image2_original, (void*)acquire_next_image2_hook);
      error_assert(rv != 0, "Failed to prepare vkAcquireNextImage2KHR hook\n");

      rv = funchook_prepare(funchook, (void**)&create_swapchain_original, (void*)create_swapchain_hook);
      error_assert(rv != 0, "Failed to prepare vkCreateSwapchainKHR hook\n");

      rv = funchook_prepare(funchook, (void**)&destroy_swapchain_original, (void*)destroy_swapchain_hook);
      error_assert(rv != 0, "Failed to prepare vkDestroySwapchainKHR hook\n");

      if (create_device_original != nullptr) {
        rv = funchook_prepare(funchook, (void**)&create_device_original, (void*)create_device_hook);
        error_assert(rv != 0, "Failed to prepare vkCreateDevice hook\n");
      }

      if (get_device_queue_original != nullptr) {
        rv = funchook_prepare(funchook, (void**)&get_device_queue_original, (void*)get_device_queue_hook);
        error_assert(rv != 0, "Failed to prepare vkGetDeviceQueue hook\n");
      }

      if (get_device_queue2_original != nullptr) {
        rv = funchook_prepare(funchook, (void**)&get_device_queue2_original, (void*)get_device_queue2_hook);
        error_assert(rv != 0, "Failed to prepare vkGetDeviceQueue2 hook\n");
      }

      dlclose(lib_vulkan_handle);
    } else {
      print("Vulkan mode detected, but libvulkan.so.1 is not loaded\n");
    }
  }

  rv = funchook_install(funchook, 0);
  error_assert(rv != 0, "Non-VMT related hooks failed\n");

  install_sdl_hooks();
  
  // Misc static variables and hookable things
  unsigned long func_address_2 = (unsigned long)sigscan_module("client.so", sigs::random_seed);
  unsigned int random_seed_eaddr = *(unsigned int*)(func_address_2 + 0x3);
  unsigned long func_address_2_next_instruction = (unsigned long)(func_address_2 + 0x7);
  random_seed = (uint32_t*)((void*)(func_address_2_next_instruction + random_seed_eaddr));
  
  return true;
}

__attribute__((constructor))
void entry()
{
  const char* auto_attach = std::getenv("CATHOOK_AUTO_ATTACH");
  if (auto_attach != nullptr && std::strcmp(auto_attach, "1") == 0) {
    cathook::core::start_attach_worker();
  }
}

extern "C" bool cathook_attach()
{
  print("cathook_attach export called\n");
  return cathook::core::start_attach_worker();
}

extern "C" bool cathook_detach()
{
  cathook::core::request_detach();
  return true;
}

extern "C" bool cathook_is_detached()
{
  return cathook::core::is_runtime_detached();
}

__attribute__((destructor))
void __exit() {
  cathook::core::process_exiting.store(true, std::memory_order_release);
  cathook::core::stop_attach_worker();
  cathook::core::exception_handler::uninstall();
}
