#pragma once

#include <cstddef>

// Linux64 TF2 client.so Itanium vtable indices. Object vptr = _ZTV+16
// (complete dtor at [0], deleting dtor at [1]). Recovered from the live
// client.so IDB; prefer these over raw numeric literals at call sites.
namespace vtables
{

namespace entity
{
constexpr std::size_t get_ref_ehandle         = 3;   // IHandleEntity, after Itanium dtors
constexpr std::size_t get_collideable         = 4;   // returns this+576 CCollisionProperty
constexpr std::size_t get_abs_origin          = 11;  // C_BaseEntity_GetAbsOrigin, this+808
constexpr std::size_t get_abs_angles          = 12;  // C_BaseEntity_GetAbsAngles, this+820
constexpr std::size_t should_draw             = 136; // C_TFPlayer_ShouldDraw (zoom / first-person)
constexpr std::size_t interpolate             = 143; // C_BaseAnimating_Interpolate
constexpr std::size_t is_base_combat_weapon   = 191; // entity returns 0 / C_TFWeaponBase returns 1
constexpr std::size_t eye_position            = 195; // C_TFPlayer vptr+195 -> EyePosition @ 0x14f8fa0
constexpr std::size_t eye_angles              = 196; // returns this+9196 / observer path
constexpr std::size_t should_collide          = 199;
constexpr std::size_t should_interpolate      = 210; // C_BasePlayer_ShouldInterpolate
constexpr std::size_t update_ik_locks         = 229;
constexpr std::size_t calculate_ik_locks      = 230;
constexpr std::size_t frame_advance           = 254; // C_BaseAnimating_FrameAdvance
}

// C_BaseEntity MI: this+0 unknown/entity, this+8 renderable, this+16 networkable.
namespace renderable
{
constexpr std::size_t vptr_offset        = 8;
constexpr std::size_t get_iclient_unknown = 0;
constexpr std::size_t should_draw        = 3;
constexpr std::size_t get_model          = 9;
constexpr std::size_t draw_model         = 10;
constexpr std::size_t setup_bones        = 16;
constexpr std::size_t get_render_bounds  = 20; // thunk this-8
constexpr std::size_t lookup_attachment  = 35;
constexpr std::size_t get_attachment     = 36; // origin + angles
}

namespace networkable
{
constexpr std::size_t vptr_offset      = 16;
constexpr std::size_t get_client_class = 2;
constexpr std::size_t is_dormant       = 8;  // C_BaseEntity_IsDormant
constexpr std::size_t entindex         = 9;  // C_BaseEntity_GetIndex
}

// CCollisionProperty, no extra dtor slots.
namespace collideable
{
constexpr std::size_t obb_mins              = 3;
constexpr std::size_t obb_maxs              = 4;
constexpr std::size_t get_collision_origin  = 10;
}

namespace weapon
{
// C_TFWeaponBase primary vptr 0x2b79cb8 (typeinfo 0x2b79ba0).
constexpr std::size_t get_slot                                = 395; // CEconItemDefinition+380
constexpr std::size_t get_print_name                          = 401; // CEconItemDefinition+10
constexpr std::size_t get_weapon_id                           = 451; // base returns 0, subclasses override
constexpr std::size_t calc_is_attack_critical_helper          = 468; // named CalcIsAttackCritical @ 0x1d60730
constexpr std::size_t calc_is_attack_critical_helper_no_crits = 469;
constexpr std::size_t apply_fire_delay                        = 481; // multiplies delay by mult_postfiredelay
constexpr std::size_t can_fire_critical_shot                  = 497; // base returns 1
}

namespace melee
{
// C_TFWeaponBaseMelee primary vptr 0x2b7e830.
constexpr std::size_t get_swing_range = 528; // 48 / 72 sword / 128
constexpr std::size_t do_swing_trace  = 530;
}

namespace inventory
{
constexpr std::size_t get_max_item_count      = 10; // 300, or backpack upgrade +50 cap 4000
constexpr std::size_t equip_item_in_loadout   = 20; // class, slot, itemid
constexpr std::size_t get_tf_player_inventory = 24; // return this+416
}

namespace entity_list
{
// IClientEntityList is a secondary vptr on CClientEntityList (this already
// adjusted by CreateInterface). No extra dtor slots. Matches 2013 header.
constexpr std::size_t get_client_networkable            = 0;
constexpr std::size_t get_client_networkable_from_handle = 1;
constexpr std::size_t get_client_unknown_from_handle    = 2;
constexpr std::size_t get_client_entity                 = 3;
constexpr std::size_t get_client_entity_from_handle     = 4;
constexpr std::size_t number_of_entities                = 5;
constexpr std::size_t get_highest_entity_index          = 6;
constexpr std::size_t set_max_entities                  = 7;
constexpr std::size_t get_max_entities                  = 8;
}

namespace client_dll
{
constexpr std::size_t get_all_classes         = 8;  // returns g_pClientClassHead
constexpr std::size_t hud_process_input       = 10; // lea g_pClientMode; jmp [rax+0x58]
constexpr std::size_t in_key_event            = 20; // CHLClient
constexpr std::size_t create_move             = 21; // IBaseClientDLL::CreateMove
constexpr std::size_t write_usercmd_delta     = 23;
constexpr std::size_t frame_stage_notify      = 35; // CHLClient::FrameStageNotify
constexpr std::size_t dispatch_user_message   = 36;
constexpr std::size_t get_player_view         = 59; // copies live CViewSetup, returns true
constexpr std::size_t invalidate_mdl_cache    = 64; // walks C_BaseAnimating
}

namespace client_mode
{
constexpr std::size_t override_view         = 17; // ClientModeShared_OverrideView @ 0x15afec0
constexpr std::size_t create_move           = 22; // ClientModeShared_CreateMove
constexpr std::size_t level_init            = 23; // TF wraps shared LevelInit @ 0x15b09e0
constexpr std::size_t level_shutdown        = 24; // shared sub_15B0560 on both TF/shared vtables
// IGameEventListener2 is the second vptr on ClientModeShared (this+8 on linux64).
// Slot 2 thunks to ClientModeShared_FireGameEvent with this-8.
constexpr std::size_t fire_game_event       = 2;
constexpr std::size_t listener_vptr_offset  = 8;
}

namespace model_render
{
constexpr std::size_t forced_material_override = 1;  // CModelRender, no extra dtors
constexpr std::size_t draw_model_execute       = 19;
}

// IVRenderView / CVRenderView: no extra dtor slots. SetBlend stores the
// engine color-modulation blend float; GetMatricesForView is live slot 50.
namespace render_view
{
constexpr std::size_t set_blend              = 4;
constexpr std::size_t get_blend              = 5;
constexpr std::size_t set_color_modulation   = 6;
constexpr std::size_t get_color_modulation   = 7;
constexpr std::size_t get_matrices_for_view  = 50;
}

// IInputSystem : IAppSystem, no extra dtors. Live CInputSystem.
namespace input_system
{
constexpr std::size_t is_button_down         = 11;
constexpr std::size_t button_code_to_string  = 27;
}

namespace vgui_panel
{
// VPanelWrapper: IBaseInterface complete+deleting dtors, then IPanel. Live
// GetName body 35 -> slot 37, PaintTraverse body 40 -> 42, SetTopmostPopup 58 -> 60.
constexpr std::size_t get_name          = 37;
constexpr std::size_t paint_traverse    = 42;
constexpr std::size_t set_topmost_popup = 60;
}

// ISurface : IAppSystem, extra Shutdown overrides slot 4 (no extra dtor).
// Live CMatSystemSurface; indices match 2013 ISurface body + IAppSystem 0-4.
namespace surface
{
constexpr std::size_t draw_set_color            = 10;
constexpr std::size_t draw_filled_rect          = 12;
constexpr std::size_t draw_outlined_rect        = 14;
constexpr std::size_t draw_line                 = 15;
constexpr std::size_t draw_set_text_font        = 17;
constexpr std::size_t draw_set_text_color       = 18;
constexpr std::size_t draw_set_text_pos         = 20;
constexpr std::size_t draw_print_text           = 22;
constexpr std::size_t draw_set_texture_rgba     = 31;
constexpr std::size_t draw_set_texture          = 32;
constexpr std::size_t is_texture_id_valid       = 35;
constexpr std::size_t delete_texture_by_id      = 36;
constexpr std::size_t create_new_texture_id     = 37;
constexpr std::size_t get_screen_size           = 38;
constexpr std::size_t set_cursor_always_visible = 52;
constexpr std::size_t unlock_cursor             = 61;
constexpr std::size_t lock_cursor               = 62;
constexpr std::size_t create_font               = 66;
constexpr std::size_t set_font_glyph_set        = 67;
constexpr std::size_t add_custom_font_file      = 68;
constexpr std::size_t get_text_size             = 75;
constexpr std::size_t play_sound                = 78;
constexpr std::size_t draw_outlined_circle      = 99;
constexpr std::size_t draw_textured_polygon     = 102;
constexpr std::size_t draw_set_texture_rgba_ex  = 119;
}

// IMaterial has no extra dtor slots. Live CMaterial.
namespace material
{
constexpr std::size_t get_name                   = 0;
constexpr std::size_t get_texture_group_name     = 1;
constexpr std::size_t find_var                   = 11;
constexpr std::size_t increment_reference_count  = 12;
constexpr std::size_t decrement_reference_count  = 13;
constexpr std::size_t is_translucent             = 17;
constexpr std::size_t alpha_modulate             = 27;
constexpr std::size_t color_modulate             = 28;
constexpr std::size_t set_material_var_flag      = 29;
constexpr std::size_t get_material_var_flag      = 30;
constexpr std::size_t refresh                    = 37;
constexpr std::size_t is_error_material          = 42;
constexpr std::size_t get_alpha_modulation       = 44;
constexpr std::size_t get_color_modulation       = 45;
constexpr std::size_t set_shader_and_params      = 48;
constexpr std::size_t is_sprite_card             = 51;
constexpr std::size_t is_precached               = 56;
}

// CCraftingPanel::Craft @ 0x1D8A230. Input UUID walk is [this+0x310, this+0x370).
namespace crafting_panel
{
constexpr std::size_t vpanel           = 0x60;
constexpr std::size_t input_items      = 0x310;
constexpr std::size_t input_items_end  = 0x370;
constexpr std::size_t selected_recipe  = 0x450;
}

// IMatchGroupDescription: vptr + ETFMatchGroup at +8. Force-client-settings
// bool is stored as [this+0x73] in the live ctor (Amalgam m_bForceClientSettings).
namespace match_group
{
constexpr std::size_t id                     = 8;
constexpr std::size_t force_client_settings  = 0x73;
constexpr int table_max                      = 8; // GetMatchGroupDescription rejects >8
}

namespace game_event
{
constexpr std::size_t fire_event             = 8;
constexpr std::size_t fire_event_client_side = 9;
}

namespace input
{
constexpr std::size_t create_move           = 3;  // CInput::CreateMove, MULTIPLAYER_BACKUP 90
constexpr std::size_t get_user_cmd          = 8;  // CTFInput::GetUserCmd; m_pCommands parsed from 48 8B 87 disp32
constexpr std::size_t activate_mouse        = 18;
constexpr std::size_t deactivate_mouse      = 19;
constexpr std::size_t cam_is_third_person   = 31; // *(this+161)
constexpr std::size_t cam_cap_yaw           = 51; // CTFInput_CAM_CapYaw (CInput is a ret)
}

namespace prediction
{
constexpr std::size_t update       = 4;  // IPrediction::Update (Itanium dtors 0-1)
constexpr std::size_t run_command  = 18; // CPrediction::RunCommand
constexpr std::size_t setup_move   = 19;
constexpr std::size_t finish_move  = 20;
}

namespace game_movement
{
constexpr std::size_t process_movement                 = 2; // CTFGameMovement, after dtors
constexpr std::size_t start_track_prediction_errors    = 3;
constexpr std::size_t finish_track_prediction_errors   = 4;
}

namespace engine_trace
{
constexpr std::size_t get_point_contents = 0;
constexpr std::size_t trace_ray          = 4; // CEngineTraceClient, no dtor slots
}

namespace model_info
{
// CModelInfoClient Itanium dtors occupy 0-1.
constexpr std::size_t get_model_name   = 4;
constexpr std::size_t get_vcollide     = 5;
constexpr std::size_t get_studiomodel  = 29;
}

namespace cvar
{
// CCvar / IAppSystem, no extra dtor slots. FindVar matches linx vtable[12].
constexpr std::size_t allocate_dll_identifier = 5;
constexpr std::size_t register_con_command    = 6;
constexpr std::size_t unregister_con_command  = 7;
constexpr std::size_t find_var                = 12;
constexpr std::size_t find_command            = 14;
constexpr std::size_t console_color_printf    = 23;
}

// CMaterialSystem VMaterialSystem082, no extra dtor slots (Connect is slot 0).
// Recovered from live materialsystem.so _ZTV / factory returning &unk_16D240.
namespace material_system
{
constexpr std::size_t set_in_stub_mode                        = 58;
constexpr std::size_t create_material                         = 70;
constexpr std::size_t find_material                           = 71;  // thunks to find_material_ex
constexpr std::size_t first_material                          = 73;
constexpr std::size_t next_material                           = 74;
constexpr std::size_t invalid_material                        = 75;
constexpr std::size_t get_material                            = 76;
constexpr std::size_t find_texture                            = 79;
constexpr std::size_t create_procedural_texture               = 81;
constexpr std::size_t begin_render_target_allocation          = 82;
constexpr std::size_t end_render_target_allocation            = 83;
constexpr std::size_t create_named_render_target_texture_ex   = 85;
constexpr std::size_t get_render_context                      = 98;
constexpr std::size_t find_procedural_material                = 110;
constexpr std::size_t find_material_ex                        = 121;
constexpr std::size_t override_render_target_allocation       = 127;
}

// ITexture / CTexture, no extra dtor slots.
namespace texture
{
constexpr std::size_t get_actual_width   = 3;
constexpr std::size_t get_actual_height  = 4;
constexpr std::size_t increment_ref      = 10;
constexpr std::size_t decrement_ref      = 11;
constexpr std::size_t is_error           = 15;
}

// IMatRenderContext : IRefCounted (AddRef=0, Release=1), no extra dtors.
// Live CMatRenderContext / CMatQueuedRenderContext share this layout.
namespace mat_render_context
{
constexpr std::size_t add_ref                           = 0;
constexpr std::size_t release                           = 1;
constexpr std::size_t begin_render                      = 2;
constexpr std::size_t end_render                        = 3;
constexpr std::size_t set_render_target                 = 6;
constexpr std::size_t get_render_target                 = 7;
constexpr std::size_t depth_range                       = 11;
constexpr std::size_t clear_buffers                     = 12;
constexpr std::size_t viewport                          = 38;
constexpr std::size_t clear_color_4ub                   = 73;
constexpr std::size_t draw_screen_space_rectangle       = 103;
constexpr std::size_t push_render_target_and_viewport   = 105;
constexpr std::size_t pop_render_target_and_viewport    = 109;
constexpr std::size_t set_stencil_enable                = 117;
constexpr std::size_t set_stencil_fail_operation        = 118;
constexpr std::size_t set_stencil_zfail_operation       = 119;
constexpr std::size_t set_stencil_pass_operation        = 120;
constexpr std::size_t set_stencil_compare_function      = 121;
constexpr std::size_t set_stencil_reference_value       = 122;
constexpr std::size_t set_stencil_test_mask             = 123;
constexpr std::size_t set_stencil_write_mask            = 124;
constexpr std::size_t clear_stencil_buffer_rectangle    = 125;
constexpr std::size_t override_alpha_write_enable       = 195;
}

namespace netchan
{
constexpr std::size_t get_name        = 0;  // lea rax, [rdi+0x2360]
constexpr std::size_t get_address     = 1;  // lea rax, [rdi+0xCC]
constexpr std::size_t get_latency     = 9;  // flow stride 3888, +1248
constexpr std::size_t get_avg_latency = 10; // flow stride 3888, +1244
constexpr std::size_t shutdown        = 35; // "shutdown netchan"
constexpr std::size_t send_net_msg    = 38; // "SendNetMsg %s: stream"
constexpr std::size_t send_datagram   = 44; // CNetChan::SendDatagram
constexpr std::size_t transmit        = 45;
constexpr std::size_t can_packet      = 54;
// Sequence prefix after vptr+connection_state. Parsed at runtime from
// unique GetSequenceData / SetChoked / GetReliable engine bytes.
constexpr std::size_t out_sequence_nr     = 0x0C;
constexpr std::size_t in_sequence_nr      = 0x10;
constexpr std::size_t out_sequence_nr_ack = 0x14;
constexpr std::size_t out_reliable_state  = 0x18;
constexpr std::size_t in_reliable_state   = 0x1C;
constexpr std::size_t choked_packets      = 0x20;
}

// IMaterialVar / CMaterialVar, no extra dtor slots.
namespace material_var
{
constexpr std::size_t set_int_value     = 4;  // stores dword at this+16, type 4
constexpr std::size_t set_vec_value_xyz = 11; // packs xyz, calls SetVecValue(*, 3)
}

namespace engine_client
{
// CEngineClient ZTV has no extra dtor slots (COM-style).
constexpr std::size_t get_screen_size           = 5;
constexpr std::size_t server_cmd                = 6;
constexpr std::size_t client_cmd                = 7;
constexpr std::size_t get_player_info           = 8;  // memcpy 0x84
constexpr std::size_t get_player_for_user_id    = 9;
constexpr std::size_t get_local_player          = 12;
constexpr std::size_t get_view_angles           = 19;
constexpr std::size_t set_view_angles           = 20;
constexpr std::size_t get_max_clients           = 21;
constexpr std::size_t is_in_game                = 26;
constexpr std::size_t get_level_name            = 51;
constexpr std::size_t get_net_channel_info      = 72;
constexpr std::size_t is_playing_time_demo      = 78;
constexpr std::size_t is_taking_screenshot      = 85;
constexpr std::size_t execute_client_cmd        = 102;
constexpr std::size_t get_app_id                = 104;
constexpr std::size_t client_cmd_unrestricted   = 106;
constexpr std::size_t get_achievement_mgr       = 114;
constexpr std::size_t server_cmd_key_values     = 127;
}

namespace engine_vgui
{
constexpr std::size_t paint = 15; // CEngineVGui::Paint
}

namespace engine_sound
{
constexpr std::size_t emit_sound_1 = 4; // CEngineSoundClient::EmitSound
constexpr std::size_t emit_sound_2 = 5;
constexpr std::size_t emit_sound_3 = 6;
}

namespace hud_chat
{
constexpr std::size_t chat_printf        = 22;
constexpr std::size_t start_message_mode = 23; // #chat_say
constexpr std::size_t stop_message_mode  = 24;
}

// Steamworks COM vtables have no Itanium dtor slots.
namespace steam_friends
{
constexpr std::size_t get_persona_name            = 0;
constexpr std::size_t get_friend_persona_name     = 7; // ISteamFriends::GetFriendPersonaName
constexpr std::size_t activate_overlay_to_user    = 29;
}

// IUniformRandomStream has four methods and no dtor slots:
// SetSeed, RandomFloat, RandomInt, RandomFloatExp. ZTV is 6 qwords.
namespace random_stream
{
constexpr std::size_t random_int = 2;
}

// CBaseFileSystem on filesystem_stdio.so. IBaseFileSystem is the this+8 vptr.
// IFileSystem primary vptr has no extra dtor slots (Connect is 0), matching linx.
namespace filesystem
{
constexpr std::size_t find_first              = 27;
constexpr std::size_t find_next               = 28;
constexpr std::size_t find_first_ex           = 31;
constexpr std::size_t async_read_multiple     = 37;
constexpr std::size_t open_ex                 = 69;
constexpr std::size_t read_file_ex            = 71;
constexpr std::size_t add_files_to_file_cache = 103;
constexpr std::size_t register_file_whitelist = 94; // CBaseFileSystem_RegisterFileWhitelist
constexpr std::size_t ibasefilesystem_vptr_offset = 8;
constexpr std::size_t open                    = 2;
constexpr std::size_t precache                = 9;
constexpr std::size_t read_file               = 14;
}

// IStudioRender::BeginFrame is the first method after IAppSystem
// (Connect/Disconnect/QueryInterface/Init/Shutdown) with no dtor slots.
namespace studio_render
{
constexpr std::size_t begin_frame = 5;
}

// engine.so CToolFrameworkInternal::Think(bool finalTick) iterates tools.
namespace tool_framework
{
constexpr std::size_t think = 27;
}

// server.so CServerTools, only present when a listen server is loaded.
namespace server_tools
{
constexpr std::size_t get_i_server_entity = 2; // CServerTools_GetIServerEntity
}

// engine.so CClientState fields. ForceFullUpdate cmp [rdi+0x1B8];
// client_state lea is followed by mov r8,[rax+0x20]; CL_Move RIP-stores
// lastoutgoingcommand/chokedcommands as a consecutive int pair.
namespace client_state
{
constexpr std::size_t net_channel          = 0x20;
constexpr std::size_t signon_state         = 0x14c;
constexpr std::size_t delta_tick           = 0x1b8;
constexpr std::size_t lastoutgoingcommand  = 0x8c74;
constexpr std::size_t chokedcommands       = 0x8c78;
constexpr std::size_t last_command_ack     = 0x8c7c;
}

// CHud mouse floats live on gHUD; CInput mouse path loads factor then stores FOV.
namespace hud
{
constexpr std::size_t mouse_sensitivity        = 4;    // m_flMouseSensitivity
constexpr std::size_t mouse_sensitivity_factor = 8;    // m_flMouseSensitivityFactor
constexpr std::size_t fov_sensitivity_adjust   = 0xC;  // written to 1.0 then FOV ratio
constexpr std::size_t element_array            = 0x20; // CUtlVector<CHudElement*> memory
constexpr std::size_t element_count            = 0x30; // CUtlVector size (FindElement)
}

// CHudElement::GetName is slot 9 (*vptr + 72) in CHud::FindElement.
namespace hud_element
{
constexpr std::size_t get_name = 9;
}

// IEconItemInterface on CEconItem. GetID returns *(this+72).
namespace econ
{
constexpr std::size_t get_item_id = 13;
}

// CTFGCClientSystem fields from BConnectedToMatchServer @ 0x1EA1AA0.
namespace gc
{
constexpr std::size_t party               = 0x30;  // CTFParty*
constexpr std::size_t assigned_match_id    = 0x7C8; // CSteamID
constexpr std::size_t assigned_match_ended = 0x7D1;
constexpr std::size_t force_ping_refresh   = 0x4CC; // movb $1, this+0x4CC in SDR ping force
}

} // namespace vtables
