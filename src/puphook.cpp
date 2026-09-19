#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/puphook.cpp
 |  Y  |   author: pupnoodle
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
#include <initializer_list>
#include <mutex>
#include <unistd.h>
#include <csignal>
#include "core/logger.hpp"
#include "core/config/config_store.hpp"
#include "core/commands.hpp"
#include "core/ipc/ipc_client.hpp"
#include "core/diagnostics/exception_handler.hpp"
#include "features/menu/binds.hpp"
#include "features/automation/followbot/followbot.hpp"
// #include "features/automation/inventory_changer/inventory_changer.hpp" // Temporarily disabled.
#include "core/print.hpp"
#include "core/assert.hpp"
#include "core/detach.hpp"
#include "core/types.hpp"
#include "core/memory/byte_patch.hpp"
#include "core/memory/code_scan.hpp"
#include "core/memory/maps.hpp"
#include "core/memory/resolve.hpp"
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
#include "games/tf2/sdk/interfaces/steam_runtime.hpp"
#include "games/tf2/sdk/interfaces/input.hpp"
#include "games/tf2/sdk/interfaces/attribute_manager.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/move_helper.hpp"
#include "games/tf2/sdk/interfaces/game_movement.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/interfaces/game_event_manager.hpp"
#include "games/tf2/sdk/interfaces/file_system.hpp"
#include "games/tf2/sdk/interfaces/mdl_cache.hpp"
#include "games/tf2/sdk/interfaces/vphysics.hpp"
#include "games/tf2/sdk/interfaces/world_collision.hpp"
#include "libsigscan/libsigscan.h"
#include "funchook/funchook.h"

#include "core/hooks/sdl.cpp"
#include "features/visuals/esp/esp.cpp"
#include "core/hooks/vulkan.cpp"
#include "core/overwrite_dlopen.cpp"
#include "core/hooks/hooks.cpp"
#include "core/ipc/ipc_client.cpp"
#include "core/player_manager.cpp"
#include "features/combat/backtrack/backtrack.cpp"
#include "features/combat/tickbase/tickbase.cpp"
#include "features/combat/anti_aim/anti_aim.cpp"
#include "features/combat/random_crits/crit_hack.cpp"
#include "features/combat/auto_detonate/auto_detonate.cpp"
#include "features/combat/auto_reflect/auto_reflect.cpp"
#include "core/hooks/ctf_weapon_base_calc_is_attack_critical.cpp"
#include "core/hooks/cl_process_packet_entities.cpp"
#include "core/hooks/cl_read_packets.cpp"
#include "core/hooks/cl_move.cpp"
#include "core/hooks/client_mode_create_move.cpp"
#include "core/hooks/client_mode_post_screen_space_effects.cpp"
#include "core/hooks/view_render_removals.cpp"
#include "core/hooks/client_create_move.cpp"
#include "features/visuals/groups/visual_groups.cpp"
#include "features/visuals/material_manager.cpp"
#include "features/visuals/entity_visuals_effects.hpp"
#include "features/visuals/entity_visuals_effects.cpp"
#include "core/hooks/model_render.cpp"
#include "core/hooks/casual_medal.cpp"
#include "core/hooks/equip_region_unlock.cpp"
// #include "features/automation/inventory_changer/inventory_changer.cpp" // Temporarily disabled.
#include "core/hooks/region_selector.cpp"
#include "core/hooks/tf_gc_client_system.cpp"
#include "core/hooks/inspect_target.cpp"
#include "core/hooks/paint_traverse.cpp"
#include "core/hooks/override_view.cpp"
#include "core/hooks/draw_view_model.cpp"
#include "core/hooks/in_cond.cpp"
#include "core/hooks/update_client_side_animation.cpp"
#include "core/hooks/host_is_secure_server_allowed.cpp"
#include "core/hooks/load_white_list.cpp"
#include "core/hooks/fire_event_client_side.cpp"
#include "core/hooks/frame_stage_notify.cpp"
#include "core/hooks/recv_proxy_simulation_time.cpp"
#include "core/hooks/dispatch_user_message.cpp"
#include "core/hooks/intro_menu_on_tick.cpp"
#include "core/hooks/class_menu_show_panel.cpp"
#include "core/hooks/team_menu_show_panel.cpp"
#include "core/random_seed.hpp"
#include "features/automation/navbot/navbot_mesh.cpp"
#include "features/automation/navbot/navbot_hazards.cpp"
#include "features/automation/navbot/navbot_path.cpp"
#include "features/automation/navbot/navbot_jobs.cpp"
#include "features/automation/followbot/followbot.cpp"
#include "features/automation/medic_automation/medic_automation.cpp"
#include "features/automation/navbot/navbot_goals.cpp"
#include "features/automation/navbot/navbot_follow.cpp"
#include "features/automation/navbot/navbot_debug.cpp"
#include "features/automation/nographics/nographics.cpp"
#include "features/automation/region_selector/region_selector.cpp"
#include "features/automation/autoitem/autoitem.cpp"
#include "features/automation/misc/misc.cpp"
#include "features/automation/killstreak/killstreak.cpp"
#include "features/automation/spectate/spectate.cpp"
#include "features/automation/cheat_detection/cheat_detection.cpp"
#include "features/automation/anti_cheat_compat/anti_cheat_compat.cpp"
#include "features/automation/mvm_queue/mvm_queue.cpp"
#include "features/automation/profile_stalker/profile_stalker.cpp"
#include "features/automation/navbot/navbot_controller.cpp"
#include "features/visuals/hitmarker.cpp"
#include "features/visuals/spectator_list.cpp"
#include "features/visuals/thirdperson.cpp"
#include "features/visuals/radar/radar.cpp"
#include "features/visuals/skybox_changer.cpp"
#include "features/visuals/skin_changer.cpp"
#include "features/visuals/world_visuals.cpp"
#include "features/combat/simulation/projsim.hpp"
#include "core/hooks/hook_registry.hpp"

void** client_mode_vtable;
void** model_render_vtable;
void** vgui_vtable;
void** client_vtable;
void** game_event_manager_vtable;
void** steam_networking_utils_vtable;

bool initialize_game_runtime();

using client_panel_image_paint_fn = void (*)(void*);
client_panel_image_paint_fn client_panel_image_paint_original = nullptr;

using should_transmit_fn = int (*)(void*, void*);
should_transmit_fn scene_entity_should_transmit_original = nullptr;
should_transmit_fn base_entity_should_transmit = nullptr;

namespace
{

std::filesystem::path exception_log_path()
{
  const char* const bot_id{ std::getenv("PUP_BOT_ID") };
  if (bot_id != nullptr && bot_id[0] != '\0')
  {
    const std::string_view id{ bot_id };
    const bool numeric_id{ std::all_of(
      id.begin(),
      id.end(),
      [](const char character)
      {
        return character >= '0' && character <= '9';
      }) };
    if (numeric_id)
    {
      return puphook::core::log_directory() /
        (std::string{ "b" } + bot_id + ".exception.log");
    }
  }

  return puphook::core::log_directory() / "exception.log";
}

}

void client_panel_image_paint_hook(void* panel)
{
  PUPHOOK_HOOK_GUARD();
  if (panel == nullptr || client_panel_image_paint_original == nullptr) {
    return;
  }

  static const int image_offset = [] {
    const auto* fn = static_cast<const std::uint8_t*>(
      sigscan_module("client.so", sigs::client_panel_image_paint));
    if (fn == nullptr) {
      return 0;
    }
    return puphook::core::memory::member_access_disp(fn, fn + 0x100, 0x8B, 1,
      INT32_MAX, 0x4);
  }();
  if (image_offset <= 0) {
    client_panel_image_paint_original(panel);
    return;
  }

  auto* bytes = static_cast<std::uint8_t*>(panel);
  auto* image = *reinterpret_cast<void**>(bytes + image_offset);
  if (image == nullptr || *reinterpret_cast<void**>(image) == nullptr) {
    return;
  }

  client_panel_image_paint_original(panel);
}

namespace {

bool has_live_choreo_scene_vtable(void* scene)
{
  if (scene == nullptr) {
    return false;
  }

  memory_page_permissions object_page{};
  if (!query_page_permissions(scene, object_page) || (object_page.protection & PROT_READ) == 0) {
    return false;
  }

  auto* const vtable = *reinterpret_cast<void***>(scene);
  if (vtable == nullptr) {
    return false;
  }

  memory_page_permissions slot_page{};
  return query_page_permissions(&vtable[4], slot_page) && (slot_page.protection & PROT_READ) != 0 &&
    is_executable_memory_address(vtable[4]);
}

}

int scene_entity_should_transmit_hook(void* scene_entity, void* check_transmit_info)
{
  PUPHOOK_HOOK_GUARD();
  if (scene_entity == nullptr || scene_entity_should_transmit_original == nullptr) {
    return 0;
  }

  constexpr std::uintptr_t choreo_scene_offset = 0x898;
  auto* const scene = *reinterpret_cast<void**>(
    reinterpret_cast<std::uintptr_t>(scene_entity) + choreo_scene_offset);
  if (scene != nullptr && !has_live_choreo_scene_vtable(scene)) {
    static std::atomic_bool warned_invalid_scene = false;
    if (!warned_invalid_scene.exchange(true)) {
      print("[crash-guard] CSceneEntity has a stale CChoreoScene during CheckTransmit; using base transmission\n");
    }
    return base_entity_should_transmit != nullptr
      ? base_entity_should_transmit(scene_entity, check_transmit_info)
      : 0;
  }

  return scene_entity_should_transmit_original(scene_entity, check_transmit_info);
}

namespace
{

constexpr int steam_networking_utils_get_ping_to_data_center_index = 8;
constexpr int steam_networking_utils_get_direct_ping_to_pop_index = 9;
constexpr int client_objective_flag_countdown_branch_offset = 19;

byte_patch client_objective_flag_countdown_patch{};

void initialize_client_crashfix_patches()
{
  auto* match = reinterpret_cast<std::uint8_t*>(
    sigscan_module("client.so", sigs::client_objective_flag_countdown_update));
  if (match == nullptr) {
    print("Failed to find client objective flag countdown crashfix patch site\n");
    return;
  }

  client_objective_flag_countdown_patch = byte_patch(
    match + client_objective_flag_countdown_branch_offset,
    { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 });

  if (!client_objective_flag_countdown_patch.apply()) {
    print("Failed to apply client objective flag countdown crashfix patch\n");
  }
}

void restore_client_crashfix_patches()
{
  if (!client_objective_flag_countdown_patch.restore()) {
    print("Failed to restore client objective flag countdown crashfix patch\n");
  }
}

}

namespace
{

bool steam_api_ready_for_interfaces()
{
  using steam_api_is_steam_running_fn = bool (*)();
  using steam_api_get_handle_fn = int (*)();

  steam_api_is_steam_running_fn is_steam_running =
    steam_runtime::resolve_loaded_symbol<steam_api_is_steam_running_fn>("SteamAPI_IsSteamRunning");
  if (is_steam_running != nullptr && !is_steam_running())
  {
    return false;
  }

  steam_api_get_handle_fn get_steam_user =
    steam_runtime::resolve_loaded_symbol<steam_api_get_handle_fn>("SteamAPI_GetHSteamUser");
  steam_api_get_handle_fn get_steam_pipe =
    steam_runtime::resolve_loaded_symbol<steam_api_get_handle_fn>("SteamAPI_GetHSteamPipe");
  if (get_steam_user != nullptr && get_steam_pipe != nullptr)
  {
    return get_steam_user() != 0 && get_steam_pipe() != 0;
  }

  return is_steam_running != nullptr;
}

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

}

void register_hook_entries()
{
  if (!hooks::all().empty()) {
    return;
  }

  hooks::add(hooks::vmt("ModelRender::ForcedMaterialOverride", &model_render_vtable, 1,
    (void**)&model_render_forced_material_override_original, (void*)model_render_forced_material_override_hook));
  hooks::add(hooks::vmt("ModelRender::DrawModelExecute", &model_render_vtable, 19,
    (void**)&model_render_draw_model_execute_original, (void*)model_render_draw_model_execute_hook));
  hooks::add(hooks::vmt("ClientModeShared::DoPostScreenSpaceEffects", &client_mode_vtable, 40,
    (void**)&client_mode_post_screen_space_effects_original, (void*)client_mode_post_screen_space_effects_hook));
  hooks::add(hooks::vmt("ClientModeShared::CreateMove", &client_mode_vtable, 22,
    (void**)&client_mode_create_move_original, (void*)client_mode_create_move_hook));
  hooks::add(hooks::vmt("Client::CreateMove", &client_vtable, 21,
    (void**)&client_create_move_original, (void*)client_create_move_hook));
#if !defined(PUPHOOK_TEXTMODE) || !PUPHOOK_TEXTMODE
  hooks::add(hooks::vmt("ClientModeShared::OverrideView", &client_mode_vtable, 17,
    (void**)&override_view_original, (void*)override_view_hook));
  hooks::add(hooks::vmt("ClientModeShared::ShouldDrawViewModel", &client_mode_vtable, 25,
    (void**)&draw_view_model_original, (void*)draw_view_model_hook));
  hooks::add(hooks::vmt("VGUI_Panel::PaintTraverse", &vgui_vtable, 42,
    (void**)&paint_traverse_original, (void*)paint_traverse_hook));
#endif
  hooks::add(hooks::vmt("GameEventManager::FireEventClientSide", &game_event_manager_vtable, 9,
    (void**)&fire_event_client_side_original, (void*)fire_event_client_side_hook));
  hooks::add(hooks::vmt("Client::FrameStageNotify", &client_vtable, 35,
    (void**)&frame_stage_notify_original, (void*)frame_stage_notify_hook));
  hooks::add(hooks::vmt("Client::DispatchUserMessage", &client_vtable, 36,
    (void**)&dispatch_user_message_original, (void*)dispatch_user_message_hook));

  hooks::add(hooks::vmt("SteamNetworkingUtils::GetPingToDataCenter", &steam_networking_utils_vtable,
    steam_networking_utils_get_ping_to_data_center_index,
    (void**)&steam_networking_utils_get_ping_to_data_center_original,
    (void*)steam_networking_utils_get_ping_to_data_center_hook, hooks::group::deferred));
  hooks::add(hooks::vmt("SteamNetworkingUtils::GetDirectPingToPOP", &steam_networking_utils_vtable,
    steam_networking_utils_get_direct_ping_to_pop_index,
    (void**)&steam_networking_utils_get_direct_ping_to_pop_original,
    (void*)steam_networking_utils_get_direct_ping_to_pop_hook, hooks::group::deferred));

  hooks::add(hooks::sig("InCond", "client.so", sigs::in_cond,
    (void**)&in_cond_original, (void*)in_cond_hook, true));
  hooks::add(hooks::sig("C_TFPlayer::UpdateClientSideAnimation", "client.so",
    sigs::tfplayer_update_client_side_animation,
    (void**)&update_client_side_animation_original, (void*)update_client_side_animation_hook, true));
#if defined(PUPHOOK_TEXTMODE) && PUPHOOK_TEXTMODE
  hooks::add(hooks::resolved("LoadWhiteList", "engine.so", sigs::load_white_list,
    (void**)&load_white_list_original));
  hooks::add(hooks::resolved("Host_IsSecureServerAllowed", "engine.so", sigs::host_is_secure_server_allowed,
    (void**)&host_is_secure_server_allowed_original));
#else
  hooks::add(hooks::sig("LoadWhiteList", "engine.so", sigs::load_white_list,
    (void**)&load_white_list_original, (void*)load_white_list_hook, true));
  hooks::add(hooks::sig("Host_IsSecureServerAllowed", "engine.so", sigs::host_is_secure_server_allowed,
    (void**)&host_is_secure_server_allowed_original, (void*)host_is_secure_server_allowed_hook, false, true));
#endif
  hooks::add(hooks::resolved("item schema lookup map", "client.so", sigs::item_schema_lookup_map,
    (void**)&item_schema_lookup_map_original, true));
  hooks::add(hooks::sig("item definition lookup", "client.so", sigs::item_definition_lookup,
    (void**)&item_definition_lookup_original, (void*)item_definition_lookup_hook, true));
  hooks::add(hooks::sig("inspect target check", "client.so", sigs::inspect_target_check,
    (void**)&inspect_target_check_original, (void*)inspect_target_check_hook, true));
  hooks::add(hooks::resolved("CAttributeManager::AttribHookValue", "client.so", sigs::attribute_hook_value_float,
    (void**)&attribute_hook_value_float_original));
  hooks::add(hooks::resolved("CEconItemSchema::GetAttributeDefinition", "client.so",
    sigs::attribute_definition_lookup,
    (void**)&skin_changer::attribute_definition_lookup));
  hooks::add(hooks::resolved("CAttributeList::SetRuntimeAttributeValue", "client.so",
    sigs::attribute_list_set_runtime_value,
    (void**)&skin_changer::attribute_list_set_runtime_value));
  hooks::add(hooks::resolved("CBaseClientState::ForceFullUpdate", "engine.so",
    sigs::client_state_force_full_update,
    (void**)&client_state_force_full_update));
  hooks::add(hooks::sig("CTFIntroMenu::OnTick", "client.so", sigs::intro_menu_on_tick,
    (void**)&intro_menu_on_tick_original, (void*)intro_menu_on_tick_hook, true));
  hooks::add(hooks::sig("CTFClassMenu::ShowPanel", "client.so", sigs::class_menu_show_panel,
    (void**)&class_menu_show_panel_original, (void*)class_menu_show_panel_hook, true));
  hooks::add(hooks::sig("CTFTeamMenu::ShowPanel", "client.so", sigs::team_menu_show_panel,
    (void**)&team_menu_show_panel_original, (void*)team_menu_show_panel_hook, true));
  hooks::add(hooks::sig("client panel image paint crash guard", "client.so", sigs::client_panel_image_paint,
    (void**)&client_panel_image_paint_original, (void*)client_panel_image_paint_hook, false, false));
  hooks::add(hooks::sig("CSceneEntity::CheckTransmit", "server.so", sigs::server_scene_entity_should_transmit,
    (void**)&scene_entity_should_transmit_original, (void*)scene_entity_should_transmit_hook, false, false));
  hooks::add(hooks::resolved("CBaseEntity::CheckTransmit", "server.so", sigs::server_base_entity_should_transmit,
    (void**)&base_entity_should_transmit));
  hooks::add(hooks::sig("CL_Move", "engine.so", sigs::cl_move,
    (void**)&cl_move_original, (void*)cl_move_hook, true));
  hooks::add(hooks::sig("CL_ReadPackets", "engine.so", sigs::cl_read_packets,
    (void**)&cl_read_packets_original, (void*)cl_read_packets_hook, true));
  hooks::add(hooks::sig("CL_ProcessPacketEntities", "engine.so", sigs::cl_process_packet_entities,
    (void**)&cl_process_packet_entities_original, (void*)cl_process_packet_entities_hook, false, true));
  hooks::add(hooks::resolved("CTFPartyClient::RequestQueueForMatch", "client.so", sigs::request_queue_for_match,
    (void**)&region_selector_request_queue_for_match_original));
  hooks::add(hooks::sig("CTFGCClientSystem SO event", "client.so", sigs::tf_gc_client_system_so_event,
    (void**)&tf_gc_client_system_so_event_original, (void*)tf_gc_client_system_so_event_hook, false, true));
  hooks::add(hooks::resolved("CTFGCClientSystem::RequestAcceptMatchInvite", "client.so",
    sigs::tf_gc_client_system_request_accept_match_invite,
    (void**)&tf_gc_client_system_request_accept_match_invite));
  hooks::add(hooks::resolved("CTFGCClientSystem::JoinMMMatch", "client.so", sigs::tf_gc_client_system_join_mm_match,
    (void**)&tf_gc_client_system_join_mm_match));
  hooks::add(hooks::sig("CPrediction::RunSimulation", "client.so", sigs::prediction_run_simulation,
    (void**)&prediction_run_simulation_original, (void*)prediction_run_simulation_hook, true));
  hooks::add(hooks::sig("CTFWeaponBase::CalcIsAttackCritical", "client.so", sigs::ctf_weapon_base_calc_is_attack_critical,
    (void**)&ctf_weapon_base_calc_is_attack_critical_original, (void*)ctf_weapon_base_calc_is_attack_critical_hook,
    false, true));
  hooks::add(hooks::sig("CTFWeaponBaseMelee::CalcIsAttackCritical", "client.so",
    sigs::ctf_weapon_base_melee_calc_is_attack_critical,
    (void**)&ctf_weapon_base_melee_calc_is_attack_critical_original,
    (void*)ctf_weapon_base_melee_calc_is_attack_critical_hook, false, true));
  hooks::add(hooks::sig("CPvPRankPanel rank record", "client.so", sigs::casual_rank_record,
    (void**)&casual_medal::rank_record_original, (void*)casual_medal::rank_record_hook, false, false));
  hooks::add(hooks::resolved("KeyValues() constructor", "client.so", sigs::key_values_constructor,
    (void**)&key_values_constructor_original, true));
  hooks::add(hooks::resolved("KeyValues::SetInt()", "client.so", sigs::key_values_set_int,
    (void**)&key_values_set_int_original, true));
  hooks::add(hooks::resolved("KeyValues::LoadFromBuffer()", "client.so", sigs::key_values_load_from_buffer,
    (void**)&key_values_load_from_buffer_original, true));
  hooks::add(hooks::resolved("KeyValues::deleteThis()", "client.so", sigs::key_values_delete_this,
    (void**)&key_values_delete_this_original, true));

  hooks::add(hooks::pre_resolved("CViewRender::PerformScreenSpaceEffects",
    (void**)&view_render_perform_screen_space_effects_original,
    (void*)view_render_perform_screen_space_effects_hook, false, false));
  hooks::add(hooks::pre_resolved("CViewRender::PerformScreenOverlay",
    (void**)&view_render_perform_screen_overlay_original,
    (void*)view_render_perform_screen_overlay_hook, false, false));
  hooks::add(hooks::pre_resolved("vstdlib RandomInt", (void**)&casual_medal::random_int_original,
    (void*)casual_medal::random_int_hook, false, false));

  hooks::add(hooks::sdl("SDL_PollEvent", (void*)poll_event_hook, (void**)&poll_event_original, &poll_event_target));
  hooks::add(hooks::sdl("SDL_GL_SwapWindow", (void*)swap_window_hook, (void**)&swap_window_original,
    &swap_window_target));
  hooks::add(hooks::sdl("SDL_GetWindowFlags", (void*)get_window_flags_hook, (void**)&get_window_flags_original,
    &get_window_flags_target));
  hooks::add(hooks::sdl("SDL_GetWindowWMInfo", (void*)get_window_WM_info_hook, (void**)&get_window_WM_info_original,
    &get_window_WM_info_target));
  hooks::add(hooks::sdl("SDL_GetWindowSize", (void*)get_window_size_hook, (void**)&get_window_size_original,
    &get_window_size_target));
}

bool install_steam_networking_utils_hooks()
{
  if (hooks::group_armed(hooks::group::deferred))
  {
    return true;
  }

  if (!steam_api_ready_for_interfaces())
  {
    static std::atomic_bool warned_not_ready = false;
    if (!warned_not_ready.exchange(true))
    {
      print("SteamAPI is not ready; deferring SteamNetworkingUtils hooks\n");
    }
    return false;
  }

  if (steam_networking_utils_interface == nullptr)
  {
    steam_networking_utils_interface = resolve_steam_networking_utils();
  }

  if (steam_networking_utils_interface == nullptr)
  {
    static std::atomic_bool warned_missing = false;
    if (!warned_missing.exchange(true))
    {
      print("SteamNetworkingUtils interface missing; region selector ping hooks disabled\n");
    }
    return false;
  }

  steam_networking_utils_vtable = *reinterpret_cast<void***>(steam_networking_utils_interface);
  if (steam_networking_utils_vtable == nullptr)
  {
    print("SteamNetworkingUtils vtable missing; region selector ping hooks disabled\n");
    return false;
  }

  hooks::install_group(hooks::group::deferred);
  return hooks::group_armed(hooks::group::deferred);
}

namespace puphook::core
{

std::atomic_bool runtime_initialized = false;
std::atomic_bool unload_started = false;
std::atomic_bool process_exiting = false;
std::atomic_bool attach_worker_started = false;
std::atomic_bool attach_worker_complete = false;
std::atomic_bool attach_worker_stop = false;
std::mutex attach_worker_mutex{};

constexpr auto attach_wait_step = std::chrono::milliseconds(100);
constexpr long attach_ready_delay_default_seconds = 0;
constexpr long attach_ready_delay_max_seconds = 300;
constexpr auto attach_module_wait_timeout = std::chrono::seconds(300);

constexpr std::string_view steamclient_module = "steamclient.so";

constexpr std::array<const char*, 29> game_events = {

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
  "teamplay_point_captured",
  "teamplay_point_unlocked",
  "teamplay_flag_event",
  "teamplay_setup_finished",
  "teamplay_waiting_begins",
  "teamplay_restart_round",
  "teamplay_round_win",
  "vote_maps_changed",
  "mvm_wave_spawn",
  "mvm_begin_wave",
  "mvm_wave_start",
  "mvm_wave_complete",
  "mvm_wave_failed",
};

bool is_module_loaded(const char* module_name)
{
  return puphook::core::memory::is_module_loaded(module_name);
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
    if (now - wait_start >= attach_module_wait_timeout) {
      print("puphook attach worker timed out waiting for %s\n", module_name);
      return false;
    }
    if (now >= next_missing_modules_log) {
      const auto waited_seconds = std::chrono::duration_cast<std::chrono::seconds>(now - wait_start).count();
      print("puphook attach worker waiting for %s waited_seconds=%lld",
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

bool stop_attach_worker();

  std::thread& attach_worker_thread()
{
  static std::thread thread{};
  return thread;
}

std::chrono::seconds attach_ready_delay()
{
  const char* value = std::getenv("PUPHOOK_ATTACH_DELAY_SECONDS");
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

bool require_signature(const char* const module, const char* const name, const char* const pattern,
  std::uintptr_t& instruction)
{
  instruction = reinterpret_cast<std::uintptr_t>(sigscan_module(module, pattern));
  if (instruction != 0) {
    return true;
  }

  print("Required signature missing: %s (%s); aborting puphook startup\n", name, module);
  return false;
}

void attach_worker_main()
{
  print("puphook attach worker initializing module stages\n");

  {
    const auto delay = attach_ready_delay();
    const auto delay_seconds = std::chrono::duration_cast<std::chrono::seconds>(delay).count();

    if (delay_seconds > 0) {
      print("puphook attach worker waiting %lld seconds for game runtime\n",
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
      attach_worker_started.store(false, std::memory_order_release);
      return;
    }

    print("puphook attach worker initializing runtime\n");
    nographics::prepare_startup_patches();
    const bool initialized = ::initialize_game_runtime();
    print("puphook attach worker initialize_game_runtime returned %d\n", initialized ? 1 : 0);
    if (!initialized && runtime_initialized.load(std::memory_order_acquire)) {
      puphook::core::request_detach();
      puphook::core::service_detach_request();
    }
  }

  attach_worker_complete.store(true, std::memory_order_release);
  attach_worker_started.store(false, std::memory_order_release);
}

bool start_attach_worker()
{
  std::scoped_lock lock{attach_worker_mutex};
  if (runtime_initialized.load(std::memory_order_acquire)) {
    return true;
  }

  if (attach_worker_started.load(std::memory_order_acquire)) {
    return true;
  }

  auto& thread = attach_worker_thread();
  if (thread.joinable()) thread.join();

  attach_worker_started.store(true, std::memory_order_release);
  attach_worker_stop.store(false, std::memory_order_release);
  attach_worker_complete.store(false, std::memory_order_release);
  attach_worker_thread() = std::thread{ attach_worker_main };
  return true;
}

bool stop_attach_worker()
{
  std::scoped_lock lock{attach_worker_mutex};
  attach_worker_stop.store(true, std::memory_order_release);

  auto& thread = attach_worker_thread();
  if (thread.joinable() && std::this_thread::get_id() != thread.get_id()) {
    thread.join();
  }

  attach_worker_started.store(false, std::memory_order_release);
  attach_worker_complete.store(true, std::memory_order_release);
  return true;
}

void shutdown_imgui_runtime(const bool release_graphics_resources)
{
  if (mono_ui_initialized()) {
    mono_ui_shutdown(release_graphics_resources);
  } else {
    mono_ui_lock();
    if (ImGui::GetCurrentContext() != nullptr) {
      ImGui::DestroyContext();
    }
    mono_ui_unlock();
    mono_ui_vulkan_resources_shutdown(release_graphics_resources);
  }
}

void shutdown_gl_runtime(bool release_graphics_resources)
{

  (void)release_graphics_resources;
}

bool wait_for_other_hook_calls()
{
  constexpr auto timeout = std::chrono::seconds(10);
  const auto deadline = std::chrono::steady_clock::now() + timeout;

  while (active_hook_calls.load(std::memory_order_acquire) > hook_call_depth) {
    if (std::chrono::steady_clock::now() >= deadline) {
      print("Timed out waiting for puphook hook callbacks to leave\n");
      return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  return true;
}

void clear_runtime_pointer_state()
{
  client_mode_vtable = nullptr;
  model_render_vtable = nullptr;
  vgui_vtable = nullptr;
  client_vtable = nullptr;
  game_event_manager_vtable = nullptr;
  steam_networking_utils_vtable = nullptr;

  engine = nullptr;
  overlay = nullptr;
  render_view = nullptr;
  engine_trace = nullptr;
  static_prop_mgr = nullptr;
  spatial_partition = nullptr;
  physics = nullptr;
  physics_collision = nullptr;
  client_state = nullptr;
  vgui = nullptr;
  surface = nullptr;
  material_system = nullptr;
  convar_system = nullptr;
  client = nullptr;
  input = nullptr;
  move_helper = nullptr;
  prediction = nullptr;
  game_movement = nullptr;
  entity_list = nullptr;
  mdl_cache = nullptr;
  game_file_system = nullptr;
  game_event_manager = nullptr;
  global_vars = nullptr;
  model_render = nullptr;
  model_info = nullptr;
  steam_client = nullptr;
  steam_friends = nullptr;
  steam_networking_utils_interface = nullptr;
  random_seed = nullptr;
  get_panel_name_original = nullptr;

  hooks::clear();
}

bool unload_module_runtime() {
  if (!stop_attach_worker()) {
    return false;
  }

  if (!wait_for_other_hook_calls()) {
    return false;
  }

  if (unload_started.exchange(true)) {
    return true;
  }

  print("Uninjecting...\n");
  const bool release_graphics_resources = process_exiting.load(std::memory_order_acquire)
      || is_environment_enabled("PUPHOOK_DETACH_RELEASE_GRAPHICS");

  print("Unhooking functions\n");
  bool hooks_restored = nographics::shutdown();
  simulation_time_proxy::restore();
  hooks_restored = backtrack::restore_net_channel_hook() && hooks_restored;
  hooks_restored = hooks::restore_all() && hooks_restored;

  print("Unhooking SDL functions\n");
  if (sdl_hooks_installed.load(std::memory_order_acquire)) {
    if (!begin_sdl_hook_uninstall()) {
      print("Timed out waiting for SDL hooks to drain; aborting unload\n");
      hooks_restored = false;
    } else {
      SDL_SetEventFilter(nullptr, nullptr);

      hooks_restored = hooks::restore_sdl() && hooks_restored;

      if (hooks_restored) {
        finish_sdl_hook_uninstall();
      }
    }
  }

  if (!hooks_restored || !wait_for_other_hook_calls()) {
    sdl_hooks_uninstalling.store(false, std::memory_order_release);
    unload_started.store(false, std::memory_order_release);
    detach_started.store(false, std::memory_order_release);
    detach_complete.store(false, std::memory_order_release);
    detach_requested.store(true, std::memory_order_release);
    return false;
  }

  restore_frame_stage_state();
  puphook::core::unregister_commands();
  pup_ipc::client::shutdown();
  puphook::core::players::shutdown();
  surface_runtime::reset_ready();
  restore_client_crashfix_patches();
  restore_launcher_source_lock();
  backtrack::clear();
  entity_visuals::on_shutdown(release_graphics_resources);
  world_visuals::on_shutdown();
  followbot::controller().shutdown();
  navbot::controller().shutdown();
  automation::shutdown();
  tickbase::reset();
  projsim::shutdown();

  if (!release_graphics_resources) {
    print("Skipping graphics resource release during detach\n");
  }

  shutdown_imgui_runtime(release_graphics_resources);
  shutdown_gl_runtime(release_graphics_resources);

  clear_runtime_pointer_state();

  menu_focused = false;
  sdl_window = nullptr;

  puphook::core::exception_handler::uninstall();
  puphook::core::shutdown_config_store();
  puphook::core::shutdown_logger();

  detach_requested.store(false, std::memory_order_release);
  detach_started.store(false, std::memory_order_release);
  detach_complete.store(true, std::memory_order_release);
  runtime_initialized.store(false, std::memory_order_release);
  game_hooks_installed.store(false, std::memory_order_release);
  unload_started.store(false, std::memory_order_release);
  return true;
}

bool is_runtime_detached()
{
  return detach_complete.load(std::memory_order_acquire)
      && active_hook_calls.load(std::memory_order_acquire) == 0;
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
  detach_complete.store(false, std::memory_order_release);
  game_hooks_installed.store(false, std::memory_order_release);

  print("initialize_module_runtime create directories\n");
  std::error_code error{};
  std::filesystem::create_directories(puphook::core::root_directory(), error);
  std::filesystem::create_directories(puphook::core::log_directory(), error);
  std::filesystem::create_directories(puphook::core::config_directory(), error);
  print("initialize_module_runtime initialize logger\n");
  puphook::core::initialize_logger(puphook::core::log_directory() / "puphook.log");
  print("initialize_module_runtime install exception handler\n");
  puphook::core::exception_handler::install(exception_log_path());
  print("initialize_module_runtime initialize config store\n");
  puphook::core::initialize_config_store(puphook::core::root_directory());
  print("initialize_module_runtime load default config\n");
  puphook::core::load_default_config(config);
  print("initialize_module_runtime load binds\n");
  pup_bind::load(puphook::core::get_config_store());
  print("puphook bootstrap started\n");
  return true;
}

void abort_module_runtime_init() {
  print("Aborting puphook startup; restoring hooked runtime\n");
  if (!unload_module_runtime()) {
    nographics::shutdown();
    backtrack::restore_net_channel_hook();
    hooks::restore_all();
    restore_client_crashfix_patches();
    restore_launcher_source_lock();
    pup_ipc::client::shutdown();
    runtime_initialized.store(false, std::memory_order_release);
    game_hooks_installed.store(false, std::memory_order_release);
  }
}

}

void** find_client_mode_storage_from_signature()
{
  static void** client_mode_storage = nullptr;
  static bool signature_scanned = false;

  if (signature_scanned) {
    return client_mode_storage;
  }

  void* instruction = sigscan_module("client.so", sigs::client_mode_shared);
  if (instruction == nullptr) {
    print("ClientModeShared storage signature missing\n");
    return nullptr;
  }
  signature_scanned = true;

  client_mode_storage = static_cast<void**>(puphook::core::memory::resolve_lea_rip(instruction));
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

  return static_cast<void**>(puphook::core::memory::resolve_lea_rip(instruction));
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
  const auto wait_start = next_log;

  while (!puphook::core::attach_worker_stop.load(std::memory_order_acquire)
      && !puphook::core::process_exiting.load(std::memory_order_acquire)) {
    if (void* client_mode_interface = read_client_mode_interface(); client_mode_interface != nullptr) {
      return client_mode_interface;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - wait_start >= puphook::core::attach_module_wait_timeout) {
      print("puphook attach worker timed out waiting for ClientModeShared\n");
      return nullptr;
    }
    if (now >= next_log) {
      print("Waiting for ClientModeShared\n");
      next_log = now + std::chrono::seconds(2);
    }

    std::this_thread::sleep_for(puphook::core::attach_wait_step);
  }

  return nullptr;
}

GlobalVars* read_global_vars(std::uintptr_t hud_update)
{
  const auto* const fn = reinterpret_cast<const std::uint8_t*>(hud_update);
  const auto* const end = fn + 0x60;
  for (const std::uint8_t* p = fn; p < end; p += puphook::core::memory::insn_length(p, end)) {
    puphook::core::memory::mem_insn insn{};
    if (!puphook::core::memory::decode_mem_insn(p, end, insn)) {
      continue;
    }
    if (insn.opcode != 0x8B || !insn.rex_w || !puphook::core::memory::is_rip_relative(insn)) {
      continue;
    }
    const int target_protection =
      puphook::core::memory::protection_at(reinterpret_cast<const void*>(insn.rip_target));
    if (target_protection < 0 || (target_protection & PROT_READ) == 0) {
      continue;
    }
    auto* candidate = *reinterpret_cast<GlobalVars**>(insn.rip_target);
    memory_page_permissions candidate_page{};
    if (candidate != nullptr && query_page_permissions(candidate, candidate_page) &&
        (candidate_page.protection & PROT_READ) != 0) {
      return candidate;
    }
  }

  return nullptr;
}

GlobalVars* wait_for_global_vars(std::uintptr_t hud_update)
{
  auto next_log = std::chrono::steady_clock::now();
  const auto wait_start = next_log;

  while (!puphook::core::attach_worker_stop.load(std::memory_order_acquire)
      && !puphook::core::process_exiting.load(std::memory_order_acquire)) {
    if (GlobalVars* candidate = read_global_vars(hud_update); candidate != nullptr) {
      return candidate;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - wait_start >= puphook::core::attach_module_wait_timeout) {
      print("puphook attach worker timed out waiting for CGlobalVars\n");
      return nullptr;
    }
    if (now >= next_log) {
      print("Waiting for CGlobalVars\n");
      next_log = now + std::chrono::seconds(2);
    }

    std::this_thread::sleep_for(puphook::core::attach_wait_step);
  }

  return nullptr;
}

bool install_sdl_hooks()
{
#if defined(PUPHOOK_TEXTMODE) && PUPHOOK_TEXTMODE

  print("Textmode build; skipping SDL hooks\n");
  return false;
#else

  if (sdl_hooks_installed.load(std::memory_order_acquire)) {
    return true;
  }

  if (puphook::core::is_environment_enabled("PUPHOOK_DISABLE_SDL_HOOKS")) {
    print("PUPHOOK_DISABLE_SDL_HOOKS set; skipping SDL hooks\n");
    return false;
  }

  if (nographics::is_noshaderapi()) {
    print("Empty shader API (-noshaderapi); skipping SDL/OpenGL overlay hooks\n");
    return false;
  }

  print("Installing SDL hooks\n");
  void* lib_sdl_handle = dlopen("libSDL2-2.0.so.0", RTLD_LAZY | RTLD_NOLOAD);
  if (lib_sdl_handle == nullptr) {
    const std::string sdl_path = puphook::core::memory::module_path("libSDL2-2.0.so.0", true);
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

  if (!hooks::install_sdl(lib_sdl_handle)) {
    hooks::restore_sdl();
    print("SDL hooks disabled after incomplete install\n");
    dlclose(lib_sdl_handle);
    return false;
  }

  SDL_SetEventFilter(event_filter, nullptr);

  sdl_hooks_installed.store(true, std::memory_order_release);
  print("SDL hooks installed\n");
  dlclose(lib_sdl_handle);
  return true;
#endif

}

bool initialize_game_runtime() {
  print("initialize_game_runtime enter\n");

  if (!puphook::core::initialize_module_runtime()) {
    print("initialize_game_runtime module runtime already initialized\n");
    return puphook::core::runtime_initialized.load(std::memory_order_acquire);
  }

  print("initialize_game_runtime module runtime initialized\n");
  register_hook_entries();

  if (!puphook::core::wait_for_module("engine.so")) {
    puphook::core::abort_module_runtime_init();
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

  static_prop_mgr = (IStaticPropMgrClient*)get_interface("./bin/linux64/engine.so", "StaticPropMgrClient004");
  if (static_prop_mgr == nullptr) {
    print("StaticPropMgrClient004 interface is missing; face splash will skip static props\n");
  }

  spatial_partition = (ISpatialPartition*)get_interface("./bin/linux64/engine.so", "SpatialPartition001");
  if (spatial_partition == nullptr) {
    print("SpatialPartition001 interface is missing; face splash will skip dynamic entities\n");
  }

  if (!puphook::core::wait_for_module("vphysics.so")) {
    puphook::core::abort_module_runtime_init();
    return false;
  }

  physics = (IPhysics*)get_interface("./bin/linux64/vphysics.so", "VPhysics031");
  error_assert(physics == nullptr, "VPhysics031 is missing");

  physics_collision = (IPhysicsCollision*)get_interface("./bin/linux64/vphysics.so", "VPhysicsCollision007");
  error_assert(physics_collision == nullptr, "VPhysicsCollision007 is missing");

  model_render = (ModelRender*)get_interface("./bin/linux64/engine.so", "VEngineModel016");
  error_assert(model_render == nullptr, "VEngineModel016 is missing");

  model_info = (ModelInfo*)get_interface("./bin/linux64/engine.so", "VModelInfoClient006");
  error_assert(model_info == nullptr, "VModelInfoClient006 is missing");

  std::uintptr_t rcon_addr_change_address = 0;
  if (!puphook::core::require_signature("engine.so", "CClientState", sigs::client_state, rcon_addr_change_address)) {
    puphook::core::abort_module_runtime_init();
    return false;
  }

  client_state = static_cast<ClientState*>(puphook::core::memory::resolve_lea_rip(reinterpret_cast<const void*>(rcon_addr_change_address)));
  error_assert(client_state == nullptr, "CClientState is missing");

  if (!puphook::core::wait_for_module("vgui2.so")) {
    puphook::core::abort_module_runtime_init();
    return false;
  }

  vgui = get_interface("./bin/linux64/vgui2.so", "VGUI_Panel009");
  error_assert(vgui == nullptr, "VGUI_Panel009 is missing");

  if (!puphook::core::wait_for_module("vguimatsurface.so")) {
    puphook::core::abort_module_runtime_init();
    return false;
  }

  surface = (Surface*)get_interface("./bin/linux64/vguimatsurface.so", "VGUI_Surface030");
  error_assert(surface == nullptr, "VGUI_Surface030 is missing");

  if (!puphook::core::wait_for_module("materialsystem.so")) {
    puphook::core::abort_module_runtime_init();
    return false;
  }

  material_system = (MaterialSystem*)get_interface("./bin/linux64/materialsystem.so", "VMaterialSystem082");
  error_assert(material_system == nullptr, "VMaterialSystem082 is missing");

  if (!puphook::core::wait_for_module("libvstdlib.so")) {
    puphook::core::abort_module_runtime_init();
    return false;
  }

  convar_system = (ConvarSystem*)get_interface("./bin/linux64/libvstdlib.so", "VEngineCvar004");
  error_assert(convar_system == nullptr, "VEngineCvar004 is missing");

  if (void* vstdlib_handle = open_loaded_library("libvstdlib.so")) {
    key_values_system_original = reinterpret_cast<key_values_system_interface* (*)()>(dlsym(vstdlib_handle, "KeyValuesSystem"));
    dlclose(vstdlib_handle);
  }
  error_assert(key_values_system_original == nullptr, "KeyValuesSystem is missing");

  if (!puphook::core::wait_for_module("steamclient.so")) {
    puphook::core::abort_module_runtime_init();
    return false;
  }

  if (!puphook::core::wait_for_module("client.so")) {
    puphook::core::abort_module_runtime_init();
    return false;
  }

  initialize_client_crashfix_patches();

  client = (Client*)get_interface("./tf/bin/linux64/client.so", "VClient017");
  error_assert(client == nullptr, "VClient017 is missing");

  std::uintptr_t func_address = 0;
  if (!puphook::core::require_signature("client.so", "CInput", sigs::input, func_address)) {
    puphook::core::abort_module_runtime_init();
    return false;
  }

  const auto input_storage = puphook::core::memory::resolve_lea_rip(reinterpret_cast<const void*>(func_address));
  input = input_storage != nullptr ? *static_cast<Input**>(input_storage) : nullptr;
  error_assert(input == nullptr, "CInput is missing");

  std::uintptr_t check_stuck_address = 0;
  if (!puphook::core::require_signature("client.so", "CMoveHelper", sigs::move_helper, check_stuck_address)) {
    puphook::core::abort_module_runtime_init();
    return false;
  }

  const auto move_helper_storage = puphook::core::memory::resolve_lea_rip(reinterpret_cast<const void*>(check_stuck_address));
  move_helper = move_helper_storage != nullptr ? *static_cast<MoveHelper**>(move_helper_storage) : nullptr;
  error_assert(move_helper == nullptr, "CMoveHelper is missing");

  prediction = (Prediction*)get_interface("./tf/bin/linux64/client.so", "VClientPrediction001");
  error_assert(prediction == nullptr, "VClientPrediction001 is missing");

  game_movement = (GameMovement*)get_interface("./tf/bin/linux64/client.so", "GameMovement001");
  error_assert(game_movement == nullptr, "GameMovement001 is missing");

  entity_list = (EntityList*)get_interface("./tf/bin/linux64/client.so", "VClientEntityList003");
  error_assert(entity_list == nullptr, "VClientEntityList003 is missing");

  mdl_cache = (mdl_cache_interface*)get_interface("./bin/linux64/datacache.so", "MDLCache004");
  if (mdl_cache == nullptr) {
    print("MDLCache004 interface is missing; CreateMove will run without MDL cache lock\n");
  }

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
  puphook::core::players::initialize();

  puphook::core::register_commands();
  puphook::core::execute_startup_autoexec();
  pup_ipc::client::start();

  game_event_manager = (GameEventManager*)get_interface("./bin/linux64/engine.so", "GAMEEVENTSMANAGER002");
  error_assert(game_event_manager == nullptr, "GAMEEVENTSMANAGER002 is missing");

  if (steam_api_ready_for_interfaces()) {
    steam_friends = steam_runtime::resolve_steam_friends();
    if (steam_friends == nullptr) {
      print("SteamFriends017 interface is unavailable\n");
    }
  } else {
    print("Steam runtime is not ready; SteamFriends017 is unavailable\n");
  }

  install_steam_networking_utils_hooks();

  client_vtable = *(void ***)client;
  void* client_mode_interface = wait_for_client_mode_interface();
  error_assert(client_mode_interface == nullptr, "ClientModeShared is missing");

  const auto hud_update = reinterpret_cast<std::uintptr_t>(client_vtable[11]);
  global_vars = wait_for_global_vars(hud_update);
  error_assert(global_vars == nullptr, "CGlobalVars is missing");

  client_mode_vtable = *(void***)client_mode_interface;
  model_render_vtable = *(void***)model_render;
  vgui_vtable = *(void ***)vgui;
  game_event_manager_vtable = *(void***)game_event_manager;

#if !defined(PUPHOOK_TEXTMODE) || !PUPHOOK_TEXTMODE

  skybox_changer::resolve_load_named_skys();
  world_visuals::resolve_particle_hook();

  get_panel_name_original = reinterpret_cast<const char* (*)(void*, void*)>(
    read_vtable_entry(vgui_vtable, 37, "VGUI_Panel::GetName"));
  if (get_panel_name_original == nullptr) {
    hooks::disable("VGUI_Panel::PaintTraverse");
  }
#endif

  resolve_view_render_removal_hooks();
  casual_medal::resolve_random_int();

  print("Renderer safety mode: engine-owned materials only; internal shader hooks disabled\n");
  if (nographics::is_noshaderapi()) {
    print("Empty shader API (-noshaderapi); skipping Vulkan present hooks\n");
  } else if (nographics::command_line_has_vulkan() ||
      puphook::core::memory::module_base("shaderapivk.so") != nullptr) {
    if (puphook::core::memory::module_base("shaderapivk.so") == nullptr &&
        !puphook::core::wait_for_module("shaderapivk.so")) {
      print("Vulkan renderer requested, but shaderapivk.so is not loaded; skipping Vulkan hooks\n");
    } else {
    void* lib_vulkan_handle = open_loaded_library("libvulkan.so.1");
    if (lib_vulkan_handle == nullptr) {
      lib_vulkan_handle = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    }
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

      VkInstanceCreateInfo create_info = {};
      constexpr const char* instance_extension = "VK_KHR_surface";

      create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
      create_info.enabledExtensionCount = 1;
      create_info.ppEnabledExtensionNames = &instance_extension;

      VkInstance vk_instance = VK_NULL_HANDLE;
      const auto instance_result = vkCreateInstance(&create_info, nullptr, &vk_instance);
      error_assert(instance_result != VK_SUCCESS || vk_instance == VK_NULL_HANDLE, "Failed to create Vulkan dummy instance\n");

      uint32_t gpu_count = 0;
      auto enumerate_result = vkEnumeratePhysicalDevices(vk_instance, &gpu_count, nullptr);
      error_assert(enumerate_result != VK_SUCCESS || gpu_count == 0, "Failed to enumerate Vulkan physical devices\n");

      auto gpus = std::vector<VkPhysicalDevice>(gpu_count);
      enumerate_result = vkEnumeratePhysicalDevices(vk_instance, &gpu_count, gpus.data());
      error_assert(enumerate_result != VK_SUCCESS, "Failed to read Vulkan physical devices\n");

      int use_gpu = 0;
      for (int i = 0; i < (int)gpu_count; ++i) {
	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(gpus[i], &properties);
	if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
	  use_gpu = i;
	  break;
	}
      }

      const VkPhysicalDevice vk_physical_device = gpus[use_gpu];

      uint32_t count = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(vk_physical_device, &count, nullptr);
      error_assert(count == 0, "Failed to enumerate Vulkan queue families\n");

      auto queue_families = std::make_unique<VkQueueFamilyProperties[]>(count);

      vkGetPhysicalDeviceQueueFamilyProperties(vk_physical_device, &count, queue_families.get());

      uint32_t queue_family = (uint32_t)-1;
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

      const auto device_result = vkCreateDevice(vk_physical_device, &create_info2, nullptr, &vk_fake_device);
      error_assert(device_result != VK_SUCCESS || vk_fake_device == VK_NULL_HANDLE, "Failed to create Vulkan dummy device\n");

      const auto vk_get_instance_proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(lib_vulkan_handle, "vkGetInstanceProcAddr"));

      create_device_original = vk_get_instance_proc != nullptr
        ? reinterpret_cast<PFN_vkCreateDevice>(
            vk_get_instance_proc(vk_instance, "vkCreateDevice"))
        : nullptr;
      queue_present_original = (VkResult (*)(VkQueue, const VkPresentInfoKHR*))vkGetDeviceProcAddr(vk_fake_device, "vkQueuePresentKHR");
      get_device_queue_original = reinterpret_cast<PFN_vkGetDeviceQueue>(
        vkGetDeviceProcAddr(vk_fake_device, "vkGetDeviceQueue"));
      get_device_queue2_original = reinterpret_cast<PFN_vkGetDeviceQueue2>(
        vkGetDeviceProcAddr(vk_fake_device, "vkGetDeviceQueue2"));
      create_swapchain_original = reinterpret_cast<PFN_vkCreateSwapchainKHR>(
        vkGetDeviceProcAddr(vk_fake_device, "vkCreateSwapchainKHR"));
      destroy_swapchain_original = reinterpret_cast<PFN_vkDestroySwapchainKHR>(
        vkGetDeviceProcAddr(vk_fake_device, "vkDestroySwapchainKHR"));
      acquire_next_image_original = reinterpret_cast<PFN_vkAcquireNextImageKHR>(
        vkGetDeviceProcAddr(vk_fake_device, "vkAcquireNextImageKHR"));
      acquire_next_image2_original = reinterpret_cast<PFN_vkAcquireNextImage2KHR>(
        vkGetDeviceProcAddr(vk_fake_device, "vkAcquireNextImage2KHR"));
      destroy_device_original = reinterpret_cast<PFN_vkDestroyDevice>(
        vkGetDeviceProcAddr(vk_fake_device, "vkDestroyDevice"));

      vkDestroyDevice(vk_fake_device, nullptr);

      vulkan_overlay_set_instance(vk_instance, vk_physical_device);

      hooks::add(hooks::pre_resolved("vkQueuePresentKHR", (void**)&queue_present_original,
        (void*)queue_present_hook, true));
      if (create_device_original != nullptr) {
        hooks::add(hooks::pre_resolved("vkCreateDevice", (void**)&create_device_original,
          (void*)create_device_hook, false));
      }
      if (get_device_queue_original != nullptr) {
        hooks::add(hooks::pre_resolved("vkGetDeviceQueue", (void**)&get_device_queue_original,
          (void*)get_device_queue_hook, false));
      }
      if (get_device_queue2_original != nullptr) {
        hooks::add(hooks::pre_resolved("vkGetDeviceQueue2", (void**)&get_device_queue2_original,
          (void*)get_device_queue2_hook, false));
      }
      if (create_swapchain_original != nullptr) {
        hooks::add(hooks::pre_resolved("vkCreateSwapchainKHR", (void**)&create_swapchain_original,
          (void*)create_swapchain_hook, false));
      }
      if (destroy_swapchain_original != nullptr) {
        hooks::add(hooks::pre_resolved("vkDestroySwapchainKHR", (void**)&destroy_swapchain_original,
          (void*)destroy_swapchain_hook, false));
      }
      if (acquire_next_image_original != nullptr) {
        hooks::add(hooks::pre_resolved("vkAcquireNextImageKHR", (void**)&acquire_next_image_original,
          (void*)acquire_next_image_hook, false));
      }
      if (acquire_next_image2_original != nullptr) {
        hooks::add(hooks::pre_resolved("vkAcquireNextImage2KHR", (void**)&acquire_next_image2_original,
          (void*)acquire_next_image2_hook, false));
      }
      if (destroy_device_original != nullptr) {
        hooks::add(hooks::pre_resolved("vkDestroyDevice", (void**)&destroy_device_original,
          (void*)destroy_device_hook, false));
      }
    } else {
      print("Vulkan mode detected, but libvulkan.so.1 is not loaded\n");
    }
    }
  }

  error_assert(!hooks::resolve_all(), "Required hook signatures are missing");

  auto host_should_run = (tickbase::host_should_run_fn)sigscan_module("engine.so", sigs::host_should_run);
  error_assert(host_should_run == nullptr, "Failed to find Host_ShouldRun");
  initialize_cl_move_globals(host_should_run);

  if (scene_entity_should_transmit_original == nullptr || base_entity_should_transmit == nullptr) {
    hooks::disable("CSceneEntity::CheckTransmit");
    scene_entity_should_transmit_original = nullptr;
    base_entity_should_transmit = nullptr;
  }

  if (!world_visuals::prepare_particle_hook(hooks::detail::ensure_funchook())) {
    print("Particle visual hook preparation failed; particle effect replacements disabled\n");
  }

  error_assert(!hooks::install_all(), "Failed to install hooks");
  if (!simulation_time_proxy::install()) {
    print("simulation time recv proxy not installed; movesim delta tracking will use stock decode\n");
  }

  entity_visuals::draw_model_execute_original = model_render_draw_model_execute_original;

  install_sdl_hooks();

  std::uintptr_t func_address_2 = 0;
  if (!puphook::core::require_signature("client.so", "random seed", sigs::random_seed, func_address_2)) {
    puphook::core::request_detach();
    puphook::core::service_detach_request();
    return false;
  }

  random_seed = static_cast<uint32_t*>(puphook::core::memory::resolve_lea_rip(reinterpret_cast<const void*>(func_address_2)));
  if (random_seed == nullptr) {
    puphook::core::request_detach();
    puphook::core::service_detach_request();
    return false;
  }

  puphook::core::game_hooks_installed.store(true, std::memory_order_release);
  return true;
}

__attribute__((constructor))
void entry()
{

  puphook::core::exception_handler::install(exception_log_path());

  const char* auto_attach = std::getenv("PUPHOOK_AUTO_ATTACH");
  if (auto_attach != nullptr && std::strcmp(auto_attach, "1") == 0) {
    puphook::core::start_attach_worker();
  }
}

extern "C" bool puphook_attach()
{
  print("puphook_attach export called\n");
  return puphook::core::start_attach_worker();
}

extern "C" bool puphook_detach()
{
  puphook::core::request_detach();
  puphook::core::service_detach_request();
  return puphook::core::is_runtime_detached();
}

extern "C" bool puphook_is_detached()
{
  return puphook::core::is_runtime_detached();
}

__attribute__((destructor))
void __exit() {
  puphook::core::process_exiting.store(true, std::memory_order_release);
  puphook::core::stop_attach_worker();
  if (puphook::core::runtime_initialized.load(std::memory_order_acquire)
      || puphook::core::game_hooks_installed.load(std::memory_order_acquire)
      || sdl_hooks_installed.load(std::memory_order_acquire)) {
    puphook::core::unload_module_runtime();
  }
  puphook::core::exception_handler::uninstall();
}
