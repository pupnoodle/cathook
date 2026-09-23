/*
 * offsets.hpp
 *
 *  Created on: May 4, 2017
 *      Author: nullifiedcat
 */

#pragma once

#include <stdint.h>
#include <cstddef>
#include "sdk/client_state.hpp"
#include "core/vtables.hpp"

struct offsets
{
    static constexpr uint32_t GetUserCmd()
    {
        return vtables::input::get_user_cmd;
    }
    static constexpr uint32_t ShouldDraw()
    {
        return vtables::entity::should_draw;
    }
    static constexpr uint32_t ShouldInterpolate()
    {
        return vtables::entity::should_interpolate;
    }
    static constexpr uint32_t FrameAdvance()
    {
        return vtables::entity::frame_advance;
    }
    static constexpr uint32_t DrawModelExecute()
    {
        return vtables::model_render::draw_model_execute;
    }
    static constexpr uint32_t GetFriendPersonaName()
    {
        return vtables::steam_friends::get_friend_persona_name;
    }
    static constexpr uint32_t CreateMoveInput()
    {
        return vtables::input::create_move;
    }

    static constexpr uint32_t CreateMove()
    {
        return vtables::client_mode::create_move;
    }
    static constexpr uint32_t PaintTraverse()
    {
        return vtables::vgui_panel::paint_traverse;
    }
    static constexpr uint32_t OverrideView()
    {
        return vtables::client_mode::override_view;
    }
    static constexpr uint32_t FrameStageNotify()
    {
        return vtables::client_dll::frame_stage_notify;
    }
    static constexpr uint32_t DispatchUserMessage()
    {
        return vtables::client_dll::dispatch_user_message;
    }
    static constexpr uint32_t CanPacket()
    {
        return vtables::netchan::can_packet;
    }
    static constexpr uint32_t SendNetMsg()
    {
        return vtables::netchan::send_net_msg;
    }
    static constexpr uint32_t Shutdown()
    {
        return vtables::netchan::shutdown;
    }
    static constexpr uint32_t IN_KeyEvent()
    {
        return vtables::client_dll::in_key_event;
    }
    static constexpr uint32_t LevelInit()
    {
        return vtables::client_mode::level_init;
    }
    static constexpr uint32_t LevelShutdown()
    {
        return vtables::client_mode::level_shutdown;
    }
    static constexpr uint32_t BeginFrame()
    {
        return vtables::studio_render::begin_frame;
    }
    static constexpr uint32_t FireGameEvent()
    {
        return vtables::client_mode::fire_game_event;
    }
    static constexpr uint32_t FireEvent()
    {
        return vtables::game_event::fire_event;
    }
    static constexpr uint32_t FireEventClientSide()
    {
        return vtables::game_event::fire_event_client_side;
    }
    static constexpr uint32_t lastoutgoingcommand()
    {
        return uint32_t(vtables::client_state::lastoutgoingcommand);
    }
    static constexpr uint32_t m_nSignonState()
    {
        return uint32_t(vtables::client_state::signon_state);
    }
    static constexpr uint32_t m_nDeltaTick()
    {
        return uint32_t(vtables::client_state::delta_tick);
    }
    static constexpr uint32_t m_nOutSequenceNr()
    {
        return uint32_t(vtables::netchan::out_sequence_nr);
    }
    static constexpr uint32_t m_NetChannel()
    {
        return uint32_t(vtables::client_state::net_channel);
    }
    static constexpr uint32_t RandomInt()
    {
        return vtables::random_stream::random_int;
    }
    static constexpr uint32_t Paint()
    {
        return vtables::engine_vgui::paint;
    }
    static constexpr uint32_t SendDatagram()
    {
        return vtables::netchan::send_datagram;
    }
    static constexpr uint32_t IsPlayingTimeDemo()
    {
        return vtables::engine_client::is_playing_time_demo;
    }
    static constexpr uint32_t RegisterFileWhitelist()
    {
        return vtables::filesystem::register_file_whitelist;
    }
    static constexpr uint32_t StartMessageMode()
    {
        return vtables::hud_chat::start_message_mode;
    }
    static constexpr uint32_t StopMessageMode()
    {
        return vtables::hud_chat::stop_message_mode;
    }
    static constexpr uint32_t ChatPrintf()
    {
        return vtables::hud_chat::chat_printf;
    }
    static constexpr uint32_t ServerCmdKeyValues()
    {
        return vtables::engine_client::server_cmd_key_values;
    }
    static constexpr uint32_t EmitSound1()
    {
        return vtables::engine_sound::emit_sound_1;
    }
    static constexpr uint32_t EmitSound2()
    {
        return vtables::engine_sound::emit_sound_2;
    }
    static constexpr uint32_t EmitSound3()
    {
        return vtables::engine_sound::emit_sound_3;
    }
    static constexpr uint32_t GetMaxItemCount()
    {
        return vtables::inventory::get_max_item_count;
    }
    static constexpr uint32_t RunCommand()
    {
        return vtables::prediction::run_command;
    }
    static constexpr uint32_t Think()
    {
        return vtables::tool_framework::think;
    }
    static constexpr uint32_t CalcIsAttackCriticalHelper_brokenweps()
    {
        return vtables::weapon::calc_is_attack_critical_helper;
    }
};
