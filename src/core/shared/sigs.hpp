/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/shared/sigs.hpp
 |  Y  |   autor: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef SHARED_SIGS_HPP
#define SHARED_SIGS_HPP

namespace sigs
{

constexpr const char* input =
  "48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 90 ? ? ? ? 48 8D 15 ? ? ? ? 84 C0";
constexpr const char* move_helper =
  "48 8D 05 ? ? ? ? 48 89 85 ? ? ? ? 74 ? 48 8B 38";
constexpr const char* client_state =
  "48 8D 05 ? ? ? ? 4C 8B 40";
constexpr const char* client_mode_shared =
  "48 8D 05 ? ? ? ? 40 0F B6 F6 48 8B 38 48 8B 07 FF 60 58";

constexpr const char* in_cond =
  "55 83 FE ? 48 89 E5 41 54 41 89 F4";
constexpr const char* load_white_list =
  "55 48 89 E5 41 55 41 54 49 89 FC 48 83 EC ? 48 8B 07 FF 50";
constexpr const char* cl_move =
  "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 83 EC 78 83 3D ? ? ? ? 01 F3 0F 11 85 ? ? ? ? 0F 8E ? ? ? ? 41 89 FE E8 ? ? ? ? 84 C0 89 C3 0F 84 ? ? ? ? 4C 8B 3D ? ? ? ? 31";
constexpr const char* cl_read_packets =
  "55 31 C0 48 89 E5 41 57 41 56 41 55 41 89 FD 41 54 53 48 83 EC ? 48 8B 1D ? ? ? ? 48 C7 45 ? ? ? ? ?";
constexpr const char* host_should_run =
  "48 8B 15 ? ? ? ? B8 01 00 00 00 8B 72 58 85 F6 74 ? 48 8B 0D ? ? ? ? 8B 15";
constexpr const char* prediction_run_simulation =
  "55 31 C0 48 89 E5 41 57 41 56 49 89 FE 41 55 41 89 F5 41 54 49 89 D4 53 48 89 CB 48 83 EC ? 4C 8B 3D ? ? ? ? F3 0F 11 45 ?";
constexpr const char* should_draw_local_player =
  "55 48 89 E5 41 54 48 83 EC ? 48 8D 05 ? ? ? ? 48 8B 38 48 85 FF 74 ? 48 8B 07 FF 50";
constexpr const char* should_draw_this_player =
  "55 48 89 E5 41 54 53 E8 ? ? ? ? 84 C0 75";
constexpr const char* draw_view_models =
  "55 31 C0 48 89 E5 41 57 41 56 41 55 41 89 D5 41 54 49 89 FC 53 48 89 F3 48 81 EC";
constexpr const char* attribute_hook_value_float =
  "55 31 C0 48 89 E5 41 57 41 56 41 55 49 89 F5 41 54 49 89 FC 53 89 CB";
constexpr const char* intro_menu_on_tick =
  "55 48 89 E5 41 55 41 54 49 89 FC 48 8B BF ? ? ? ? 48 8B 07 FF 90 ? ? ? ? 84 C0";
constexpr const char* class_menu_show_panel =
  "55 48 89 E5 41 55 41 54 49 89 FC 53 89 F3 40 0F B6 F6 48 83 EC ? E8 ? ? ? ? 84 DB 48 8D 1D";
constexpr const char* team_menu_show_panel =
  "55 48 89 E5 41 56 41 55 41 54 49 89 FC 53 48 83 EC ? 40 84 F6 0F 85";
constexpr const char* map_info_menu_show_panel =
  "55 48 8D 15 ? ? ? ? 48 89 E5 41 54 49 89 FC 53 48 8B 07 89 F3";
constexpr const char* text_window_show_panel =
  "55 48 8D 15 ? ? ? ? 48 89 E5 41 54 41 89 F4 53 48 8B 07";
constexpr const char* key_values_constructor =
  "55 31 C0 66 0F EF C0 48 89 E5 53";
constexpr const char* key_values_set_int =
  "55 48 89 E5 53 89 D3 BA ? ? ? ? 48 83 EC ? E8 ? ? ? ? 48 85 C0 74 ? 89 58";
constexpr const char* key_values_load_from_buffer =
  "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 81 EC ? ? ? ? 48 85 D2 48 89 BD";
constexpr const char* random_seed =
  "48 8D 05 ? ? ? ? BA ? ? ? ? 89 10";
constexpr const char* input_apply_mouse =
  "55 66 0F EF D2 48 89 E5 41 55 41 54 49 89 FC 53 48 83 EC 28";
constexpr const char* item_schema_lookup_map =
  "48 8B 05 ? ? ? ? 55 48 89 E5 41 55 41 54 48 85 C0 74 ? 4C 8D 60 ?";
constexpr const char* item_definition_lookup =
  "55 48 89 E5 41 55 41 54 49 89 FC 53 48 83 EC ? 8B 87 ? ? ? ? 85 C0 0F 84 ? ? ? ? 41 89 F1";
constexpr const char* setup_bones =
  "55 48 89 E5 41 57 41 56 41 55 41 54 41 89 CC 53 48 89 FB 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ?";
constexpr const char* inspect_target_check =
  "55 48 89 E5 41 55 41 54 53 48 81 EC ? ? ? ? 48 85 F6 74 ? 48 8D 05 ? ? ? ?";
constexpr const char* calc_is_attack_critical =
  "48 8B 05 ? ? ? ? 55 48 89 E5 41 54 49 89 FC 53 83 78 ? ? 7E ? 48 8B 07 48 8D 15 ? ? ? ?";
constexpr const char* tf_projectile_sticky_arm_time =
  "55 48 89 E5 41 54 49 89 FC 48 83 EC ? 48 8B 05 ? ? ? ? 8B 97 ? ? ? ? F3 0F 10 40 ? 48 8D 05 ? ? ? ?";

constexpr const char* tf_inventory_manager_initializer =
  "55 48 8D 3D ? ? ? ? 48 89 E5 E8 ? ? ? ? 48 8D 15 ? ? ? ? 48 8D 35 ? ? ? ? 48 8D 3D ? ? ? ? E8 ? ? ? ? 48 8D 15 ? ? ? ?";
constexpr const char* tf_inventory_get_first_item_of_item_def =
  "55 48 8D 0D ? ? ? ? 48 89 E5 41 57 41 56 41 55 41 54 53 48 83 EC ? 48 8B 07 48 8B 80 ? ? ? ?";
constexpr const char* tf_inventory_equip_item_in_loadout =
  "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 83 EC ? 48 8D 05 ? ? ? ? 89 55 ? 48 8B 00 48 85 C0 74 ? 48 83 78 ? ? 74 ? 48 83 F9 ? 49 89 FE";
constexpr const char* tf_inventory_do_preview_item =
  "55 31 C0 48 89 E5 41 56 41 55 41 54 4C 8D A5 ? ? ? ? 53 0F B7 DE 4C 89 E7 48 8D 35 ? ? ? ?";
constexpr const char* tf_inventory_craft_custom =
  "55 48 8D 87 ? ? ? ? 48 89 E5 41 57 49 89 FF 41 56 41 55 4C 8D 6D ? 41 54 53 48 8D 9F ? ? ? ?";

constexpr const char* get_party_client =
  "48 8D 05 ? ? ? ? C3 0F 1F 84 00 00 00 00 00 48 8B 05 ? ? ? ? C3";
constexpr int get_party_client_offset = 16;
constexpr const char* get_matchmaking_client =
  "48 8D 05 ? ? ? ? C3 0F 1F 84 00 00 00 00 00 48 8B 05 ? ? ? ? C3 0F 1F 84 00 00 00 00 00 48 8D 05 ? ? ? ? 48 8B 00 48 85 C0 74 ? 55";
constexpr const char* tf_gc_client_system_ping_think =
  "55 48 89 E5 41 57 41 56 41 55 49 89 FD 41 54 48 8D 3D ? ? ? ? 53 48 81 EC ? ? ? ?";
constexpr const char* tf_gc_client_system_so_event =
  "55 48 89 E5 41 57 41 56 49 89 FE 48 89 F7 41 55 41 89 D5 41 54 49 89 F4 53 48 83 EC ? 48 8B 06 FF 50 10 3D D4 07 00 00 0F 84 ? ? ? ? 49 8B 04 24 4C 89 E7 FF 50 10 83 F8 2A 74 ? 49 8B 04 24 4C 89 E7 FF 50 10 3D D8 07 00 00";
constexpr const char* tf_gc_client_system_join_mm_match =
  "55 48 89 E5 E8 ? ? ? ? 48 85 C0 74 ? 48 89 C7 48 8B 00 5D 48 8B 40 78 FF E0 0F 1F 44 00 00 5D 48 8D 3D ? ? ? ? 31 C0 E9 ? ? ? ?";
constexpr const char* tf_gc_client_system_request_accept_match_invite =
  "48 83 BF ? ? ? ? 00 74 ? C3 0F 1F 44 00 00 55 48 89 E5 41 57 49 89 F7 41 56 41 55 41 54 49 89 FC BF ? ? ? ? 53 48 83 EC ? E8 ? ? ? ? 49 89 C6 E8 ? ? ? ? 48 89 C7 E8 ? ? ? ? 48 8D 70 ?";
constexpr const char* load_saved_casual_criteria =
  "48 83 7F 30 00 C6 87 10 03 00 00 01 74 ? 80 7F 40 00 74 ? C6 87 30 03 00 00 01 48 8D 35 ? ? ? ? 48 81 C7 B0 01 00 00 E9 ? ? ? ?";
constexpr const char* is_in_queue_for_match_group =
  "55 48 89 E5 41 54 49 89 FC 89 F7 53 89 F3 E8 ? ? ? ? 83 FB FF 41 89 C0 0F 94 C0 41 83 F0 01 41 08 C0 75 ? 41 8B 54 24 58 85 D2";
constexpr const char* is_in_standby_queue =
  "0F B6 47 68 C3";
constexpr const char* abandon_current_match =
  "55 31 C0 48 89 E5 41 55 41 54 4C 8D 65 ? 53 48 89 FB 48 8D 3D ? ? ? ? 48 83 EC 38 E8 ? ? ? ? 4C 89 E7 BE 91 18 00 00 E8 ? ? ? ? 48 8B 45 ? 48 89 DF 83 48 10 01 C6 40 28 01 4C 8B 6D ? E8 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? 48 89 DF E8 ? ? ? ? 48 8D 0D ? ? ? ? 48 8B 38 48 8B 50 10 48 39 CA";
constexpr const char* request_queue_for_match =
  "55 48 89 E5 41 57 41 56 49 89 FE 89 F7 41 55 41 54 41 89 F4 53 48 81 EC 88 00 00 00 E8 ? ? ? ? 41 83 FC FF 0F 94 C3 3C 01 75 ? 84 DB 75 ? 49 63 C4 41 80 BC 06 1E 03 00 00 00 75 ?";
constexpr const char* request_leave_for_match =
  "55 48 89 E5 41 57 41 56 41 55 49 89 FD 41 54 53 48 63 DE 48 83 EC 48 89 DE E8 ? ? ? ? 84 C0 75 ? 89 DF E8 ? ? ? ? 83 FB FF 41 0F 94 C6";
constexpr const char* request_queue_for_standby =
  "48 83 7F 30 00 0F 84 ? ? ? ? 55 48 89 E5 41 57 41 56 41 55 41 54 49 89 FC 53 48 83 EC 38 80 BF 12 03 00 00 00 74 ?";
constexpr const char* request_leave_standby =
  "80 7F 68 00 0F 84 ? ? ? ? 55 48 89 E5 41 57 41 56 41 55 41 54 53 48 89 FB 48 83 EC 38 80 BF 13 03 00 00 00 74 ?";
constexpr const char* report_player_account =
  "55 48 89 F8 48 89 E5 48 C1 E8 ? 41 57 41 56 41 55 41 54 53 48 83 EC ?";
constexpr const char* allow_secure_servers_flag_ref =
  "48 8D 05 ? ? ? ? 4C 89 E7 C6 00 00 4C 8B 65 ? C9 E9 ? ? ? ?";
constexpr const char* launcher_source_lock =
  "55 48 89 E5 41 55 41 54 4C 8D AD ? ? ? ? 48 81 EC ? ? ? ? E8 ? ? ? ?";
constexpr const char* client_file_system =
  "31 F6 4C 89 EF FF 13 48 83 3D ? ? ? ? 00 48 89 05 ? ? ? ? 0F 85";
constexpr const char* base_animating_play_sequence =
  "48 85 F6 0F 84 ? ? ? ? 55 48 89 E5 41 57 41 56 41 55 41 54 49 89 FC";
constexpr const char* cl_decay_lights =
  "55 48 8D 3D ? ? ? ? 48 89 E5 41 57 41 56 41 55 41 54 53 48 83 EC ? E8 ? ? ? ? 66 0F EF C9";
constexpr const char* mod_load_lighting =
  "55 48 89 E5 41 54 4C 8B 25 ? ? ? ? 53 48 63 37 85 F6 75 ? 49 C7 84 24 ? ? ? ? ? ? ? ?";
constexpr const char* mod_load_worldlights =
  "48 8B 0D ? ? ? ? 48 C7 81 ? ? ? ? ? ? ? ? 48 63 07 85 C0 0F 84 ? ? ? ?";
constexpr const char* sprite_load_model =
  "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 89 F3 48 81 EC ? ? ? ? 4C 8D 35 ? ? ? ?";
constexpr const char* overlay_mgr_load_overlays =
  "55 BE ? ? ? ? 48 89 E5 41 57 41 56 4C 8D B5 ? ? ? ? 41 55 41 54 53 48 81 EC ? ? ? ? 48 89 BD ? ? ? ? 4C 89 F7";
constexpr const char* material_system_begin_frame =
  "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 89 FB 48 83 EC ? F3 0F 11 85 ? ? ? ?";
constexpr const char* particle_property_create =
  "55 48 89 E5 41 57 41 56 49 89 F6 41 55 41 54 53 48 89 FB 48 83 EC ? 48 8D 05 ? ? ? ?";
constexpr const char* particle_system_precache =
  "31 C0 48 85 FF 74 ? 55 48 89 E5 41 54 49 89 FC 48 83 EC ? 48 8B 3D ? ? ? ?";
constexpr const char* particle_effect_create_event =
  "55 41 89 C9 48 89 E5 41 57 41 56 41 55 4D 89 C5 41 54 49 89 FC 53 48 81 EC ? ? ? ?";
constexpr const char* view_render_render =
  "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 81 EC ? ? ? ? 48 83 3F ?";
constexpr const char* client_update_steam_rich_presence =
  "55 48 89 E5 41 57 41 56 41 55 49 89 FD 41 54 53 48 81 EC ? ? ? ? 48 8B 1D ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ? 48 8B 3B 48 85 FF";
constexpr const char* replay_ui_nullcheck_0 =
  "48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 50 38 84 C0 74 ? 48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 90 90 01 00 00";
constexpr const char* replay_ui_nullcheck_1 =
  "48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 50 38 84 C0 74 ? 45 84 E4 74 ?";
constexpr const char* replay_ui_nullcheck_2 =
  "4C 8D 35 ? ? ? ? 48 89 C3 49 8B 3E 48 8B 07 FF 50 38 48 85 DB";
constexpr const char* replay_ui_nullcheck_3 =
  "49 8B 3E 48 8B 07 FF 50 38 84 C0 0F 85 ? ? ? ? E8";
constexpr const char* replay_ui_nullcheck_4 =
  "4C 8D 35 ? ? ? ? 49 8B 3E 48 8B 07 FF 50 38 E9 ? ? ? ?";
constexpr const char* replay_ui_nullcheck_5 =
  "48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 50 30 84 C0 0F 85 ? ? ? ? 48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 90 D8 00 00 00";
constexpr const char* replay_ui_nullcheck_6 =
  "48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 90 D8 00 00 00 48 8D 05 ? ? ? ? 48 8D 35";
constexpr const char* replay_ui_nullcheck_7 =
  "48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 50 38 84 C0 0F 84 ? ? ? ? 4C 8D 3D";
constexpr const char* replay_ui_nullcheck_8 =
  "48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 90 D0 00 00 00 E9 ? ? ? ?";
constexpr const char* econ_item_view_get_item_definition_index =
  "48 8B 47 08 48 85 C0 74 ? 8B 40 20 C3 0F 1F 00 48 8B 07 8B 80 BC 00 00 00 C3";
constexpr const char* character_info_open =
  "55 31 FF 48 89 E5 E8 ? ? ? ? 48 8D 15 ? ? ? ? 48 89 C7 48 05 30 03 00 00 48 85 FF 48 0F 45 F8 48 8B 07 48 8B 00 48 39 D0 75 ? 5D 48 81 EF 30 03 00 00 31 D2 31 F6";
constexpr const char* character_info_open_direct =
  "55 48 89 E5 41 55 41 54 53 48 89 FB 48 83 EC 08 48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 90 D0 00 00 00 84 C0 75";
constexpr const char* character_info_open_backpack =
  "55 31 FF 48 89 E5 E8 ? ? ? ? 48 8D 15 ? ? ? ? 48 89 C7 48 05 30 03 00 00 48 85 FF 48 0F 45 F8 48 8B 07 48 8B 00 48 39 D0 75 ? 5D 48 81 EF 30 03 00 00 31 D2 BE 01 00 00 00";
constexpr const char* character_info_open_crafting =
  "55 31 FF 48 89 E5 E8 ? ? ? ? 48 8D 15 ? ? ? ? 48 89 C7 48 05 30 03 00 00 48 85 FF 48 0F 45 F8 48 8B 07 48 8B 00 48 39 D0 75 ? 5D 48 81 EF 30 03 00 00 31 D2 BE 02 00 00 00";
constexpr const char* character_info_open_armory =
  "55 31 FF 48 89 E5 E8 ? ? ? ? 48 8D 15 ? ? ? ? 48 89 C7 48 05 30 03 00 00 48 85 FF 48 0F 45 F8 48 8B 07 48 8B 00 48 39 D0 75 ? 5D 48 81 EF 30 03 00 00 31 D2 BE 03 00 00 00";

} // namespace sigs

#endif
