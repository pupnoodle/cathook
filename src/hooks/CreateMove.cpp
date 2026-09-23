/*
 * CreateMove.cpp
 *
 *  Created on: Jan 8, 2017
 *      Author: nullifiedcat
 */

#include "common.hpp"
#include "hack.hpp"
#include "MiscTemporary.hpp"
#include <link.h>
#include <hacks/hacklist.hpp>
#include <settings/Bool.hpp>
#include <hacks/AntiAntiAim.hpp>
#include "NavBot.hpp"
#include "HookTools.hpp"
#include "teamroundtimer.hpp"

#include "HookedMethods.hpp"
#include "nospread.hpp"
#include "Warp.hpp"
#include "Tickbase.hpp"
#include <game/shared/imovehelper.h>

settings::Boolean roll_speedhack{ "misc.roll-speedhack", "false" };
settings::Boolean roll_speedhack_navbot{ "misc.roll-speedhack.navbot", "false" };
static settings::Boolean forward_speedhack{ "misc.roll-speedhack.forward", "false" };
settings::Boolean engine_pred{ "misc.engine-prediction", "true" };
static settings::Boolean debug_projectiles{ "debug.projectiles", "false" };
static settings::Boolean fuckmode{ "misc.fuckmode", "false" };

class CMoveData;
namespace engine_prediction
{
static Vector original_origin;
static Vector original_net_origin;
static Vector original_velocity;
static int original_tickbase;
static int original_flags;
static bool original_ducked;
static bool have_flags;
static bool have_ducked;

void RunEnginePrediction(IClientEntity *ent, CUserCmd *ucmd)
{
    if (!ent || !g_IMoveHelperServer)
        return;

    CMoveData movedata{};
    CMoveData *pMoveData = &movedata;

    // Backup
    float frameTime = g_GlobalVars->frametime;
    float curTime   = g_GlobalVars->curtime;
    int tickcount   = g_GlobalVars->tickcount;
    original_origin     = re::C_BaseEntity::GetAbsOrigin(ent);
    original_net_origin = netvar.m_vecOrigin ? NET_VECTOR(ent, netvar.m_vecOrigin) : original_origin;
    original_velocity   = NET_VECTOR(ent, netvar.vVelocity);
    original_tickbase   = NET_INT(ent, netvar.nTickBase);
    have_flags = netvar.iFlags;
    if (have_flags)
        original_flags = NET_INT(ent, netvar.iFlags);
    have_ducked = netvar.m_bDucked;
    if (have_ducked)
        original_ducked = NET_BYTE(ent, netvar.m_bDucked);

    CUserCmd defaultCmd{};
    if (ucmd == nullptr)
    {
        ucmd = &defaultCmd;
    }

    // Set Usercmd for prediction
    NET_VAR(ent, netvar.m_pCurrentCommand, CUserCmd *) = ucmd;

    // Set correct CURTIME
    g_GlobalVars->curtime   = g_GlobalVars->interval_per_tick * NET_INT(ent, netvar.nTickBase);
    g_GlobalVars->frametime = g_GlobalVars->interval_per_tick;

    SetPredictionRandomSeed(MD5_PseudoRandom(current_user_cmd->command_number) & 0x7FFFFFFF);

    // Run The Prediction
    auto *player = reinterpret_cast<CBasePlayer *>(ent);
    g_IMoveHelperServer->SetHost(player);
    g_IGameMovement->StartTrackPredictionErrors(player);
    g_IPrediction->SetupMove(ent, ucmd, g_IMoveHelperServer, pMoveData);
    g_IGameMovement->ProcessMovement(player, pMoveData);
    g_IPrediction->FinishMove(ent, ucmd, pMoveData);
    g_IGameMovement->FinishTrackPredictionErrors(player);
    g_IMoveHelperServer->SetHost(nullptr);

    // Reset User CMD
    NET_VAR(ent, netvar.m_pCurrentCommand, CUserCmd *) = nullptr;

    g_GlobalVars->frametime = frameTime;
    g_GlobalVars->curtime   = curTime;
    g_GlobalVars->tickcount = tickcount;

    // Adjust tickbase
    NET_INT(ent, netvar.nTickBase)++;

    return;
}
void FinishEnginePrediction(IClientEntity *ent, CUserCmd *ucmd)
{
    if (netvar.vVelocity)
        NET_VECTOR(ent, netvar.vVelocity) = original_velocity;
    if (netvar.m_vecOrigin)
        NET_VECTOR(ent, netvar.m_vecOrigin) = original_net_origin;
    re::C_BaseEntity::SetAbsOrigin(ent, original_origin);
    NET_INT(ent, netvar.nTickBase) = original_tickbase;
    if (have_flags)
        NET_INT(ent, netvar.iFlags) = original_flags;
    if (have_ducked)
        NET_BYTE(ent, netvar.m_bDucked) = original_ducked;
    original_origin.Invalidate();
}
} // namespace engine_prediction

void PrecalculateCanShoot()
{
    auto weapon = g_pLocalPlayer->weapon();
    // Check if player and weapon are good
    if (CE_BAD(g_pLocalPlayer->entity) || CE_BAD(weapon))
    {
        calculated_can_shoot = false;
        return;
    }

    // flNextPrimaryAttack without reload
    static float next_attack = 0.0f;
    // Last shot fired using weapon
    static float last_attack = 0.0f;
    // Last weapon used
    static CachedEntity *last_weapon = nullptr;
    float server_time                = (float) (CE_INT(g_pLocalPlayer->entity, netvar.nTickBase)) * g_GlobalVars->interval_per_tick;
    float new_next_attack            = CE_FLOAT(weapon, netvar.flNextPrimaryAttack);
    float new_last_attack            = CE_FLOAT(weapon, netvar.flLastFireTime);

    // Reset everything if using a new weapon/shot fired
    if (new_last_attack != last_attack || last_weapon != weapon)
    {
        next_attack = new_next_attack;
        last_attack = new_last_attack;
        last_weapon = weapon;
    }
    // Check if can shoot
    calculated_can_shoot = next_attack <= server_time;
}

namespace hooked_methods
{
bool speedHack(CUserCmd *cmd, bool &ret)
{
    int flags = CE_INT(g_pLocalPlayer->entity, netvar.iFlags);

    if (!(flags & FL_DUCKING) || !(flags & FL_ONGROUND) || (cmd->buttons & IN_ATTACK) || HasCondition<TFCond_Charging>(LOCAL_E))
        return false;

    float maxspeed        = CE_FLOAT(g_pLocalPlayer->entity, netvar.m_flMaxspeed);
    float speed_threshold = fminf(maxspeed * 0.9f, 520.0f) - 10.0f;
    if (CE_VECTOR(g_pLocalPlayer->entity, netvar.vVelocity).Length2D() >= speed_threshold)
        return false;

    float move_length = hypotf(cmd->forwardmove, cmd->sidemove);
    if (move_length <= 0.0f)
        return false;

    if (!(cmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT)) && !roll_speedhack_navbot)
        return false;

    if (g_IBaseClientState && g_IBaseClientState->chokedcommands() != 0)
        return false;

    if (forward_speedhack)
    {
        cmd->forwardmove *= -1.0f;
        cmd->sidemove *= -1.0f;
        cmd->viewangles.x = 91.0f;
    }
    float reverse_yaw = RAD2DEG(atan2f(-cmd->sidemove, -cmd->forwardmove));
    float boost       = maxspeed > 1.0f ? fmaxf(move_length, maxspeed) : move_length;

    cmd->forwardmove = -boost;
    cmd->sidemove    = 0.0f;
    float res        = g_pLocalPlayer->v_OrigViewangles.y - reverse_yaw;
    while (res > 180)
        res -= 360;
    while (res < -180)
        res += 360;
    cmd->viewangles.y                = res;
    cmd->viewangles.z                = 270.0f;
    g_pLocalPlayer->bUseSilentAngles = true;
    ret                              = false;
    return true;
}
DEFINE_HOOKED_METHOD(CreateMove, bool, void *this_, float input_sample_time, CUserCmd *cmd)
{
    g_Settings.is_create_move = true;
    bool time_replaced, ret;
    float curtime_old, servertime;

    current_user_cmd = cmd;
    EC::run(EC::CreateMoveEarly);
    ret = original::CreateMove(this_, input_sample_time, cmd);

    if (!cmd)
    {
        g_Settings.is_create_move = false;
        return ret;
    }

#if ENABLE_VISUALS
    // Fix nolerp camera jitter
    if (nolerp)
    {
        QAngle viewangles = { cmd->viewangles.x, cmd->viewangles.y, cmd->viewangles.z };
        g_IEngine->SetViewAngles(viewangles);
    }
#endif

    // Disabled because this causes EXTREME aimbot inaccuracy
    // Actually dont disable it. It causes even more inaccuracy
    if (!cmd->command_number)
    {
        g_Settings.is_create_move = false;
        return ret;
    }

    tickcount++;

    if (!isHackActive())
    {
        g_Settings.is_create_move = false;
        return ret;
    }

    if (!g_IEngine->IsInGame())
    {
        g_Settings.bInvalid       = true;
        g_Settings.is_create_move = false;
#if ENABLE_TEXTMODE
        hack::PumpEngine();
#endif
        return true;
    }

    PROF_SECTION(CreateMove);
#if ENABLE_VISUALS
    stored_buttons = current_user_cmd->buttons;
    if (freecam_is_toggled)
    {
        current_user_cmd->sidemove    = 0.0f;
        current_user_cmd->forwardmove = 0.0f;
    }
#endif
    if (current_user_cmd && current_user_cmd->command_number)
        last_cmd_number = current_user_cmd->command_number;

    /**bSendPackets = true;
    if (hacks::shared::lagexploit::ExploitActive()) {
        *bSendPackets = ((current_user_cmd->command_number % 4) == 0);
        //logging::Info("%d", *bSendPackets);
    }*/

    // logging::Info("canpacket: %i", ch->CanPacket());
    // if (!cmd) return ret;

    time_replaced = false;
    curtime_old   = g_GlobalVars->curtime;
    
    if (!g_Settings.bInvalid && CE_GOOD(g_pLocalPlayer->entity))
    {
        servertime            = (float) CE_INT(g_pLocalPlayer->entity, netvar.nTickBase) * g_GlobalVars->interval_per_tick;
        g_GlobalVars->curtime = servertime;
        time_replaced         = true;
    }
    if (g_Settings.bInvalid)
        entity_cache::Invalidate();
    //	PROF_BEGIN();
    // Do not update if in warp, since the entities will stay identical either way
    if (!hacks::tf2::warp::in_warp && !hacks::tf2::tickbase::shifting)
    {
        PROF_SECTION(EntityCache);
        entity_cache::Update();
    }
    //	PROF_END("Entity Cache updating");
    {
        PROF_SECTION(CM_PlayerResource);
        g_pPlayerResource->Update();
    }
    {
        PROF_SECTION(CM_LocalPlayer);
        g_pLocalPlayer->Update();
    }
    PrecalculateCanShoot();
    if (firstcm)
    {
        DelayTimer.update();
        EC::run(EC::FirstCM);
        firstcm = false;
    }
    g_Settings.bInvalid = false;

    if (CE_GOOD(g_pLocalPlayer->entity))
    {
        if (!g_pLocalPlayer->life_state && CE_GOOD(g_pLocalPlayer->weapon()))
        {
            // Walkbot can leave game.
            if (!g_IEngine->IsInGame())
            {
                g_Settings.is_create_move = false;
                return ret;
            }
            g_pLocalPlayer->isFakeAngleCM = false;
            static int fakelag_queue      = 0;
            if (CE_GOOD(LOCAL_E))
                if (!hacks::tf2::nospread::is_syncing && (fakelag_amount || (hacks::shared::antiaim::force_fakelag && hacks::shared::antiaim::isEnabled())))
                {
                    // Do not fakelag when trying to attack
                    bool do_fakelag = true;
                    switch (g_pLocalPlayer->weapon_mode)
                    {
                    case weapon_melee:
                    {
                        if (g_pLocalPlayer->weapon_melee_damage_tick)
                            do_fakelag = false;
                        break;
                    }
                    case weapon_hitscan:
                    {
                        if ((CanShoot() || isRapidFire(RAW_ENT(LOCAL_W))) && current_user_cmd->buttons & IN_ATTACK)
                            do_fakelag = false;
                        break;
                    }
                    default:
                        break;
                    }

                    if (fakelag_midair && CE_INT(LOCAL_E, netvar.iFlags) & FL_ONGROUND)
                        do_fakelag = false;

                    if (do_fakelag)
                    {
                        int fakelag_amnt = (*fakelag_amount > 1) ? *fakelag_amount : 1;
                        *bSendPackets    = fakelag_amnt == fakelag_queue;
                        if (*bSendPackets)
                            g_pLocalPlayer->isFakeAngleCM = true;
                        fakelag_queue++;
                        if (fakelag_queue > fakelag_amnt)
                            fakelag_queue = 0;
                    }
                }
            {
                PROF_SECTION(CM_antiaim);
                hacks::shared::antiaim::ProcessUserCmd(cmd);
            }
            if (debug_projectiles)
                projectile_logging::Update();
        }
    }
    else
        return false;
    {
        PROF_SECTION(CM_WRAPPER);
        EC::run(EC::CreateMove_NoEnginePred);

        if (engine_pred && (g_pLocalPlayer->weapon_mode == weapon_projectile || g_pLocalPlayer->weapon_mode == weapon_hitscan))
        {
            engine_prediction::RunEnginePrediction(RAW_ENT(LOCAL_E), current_user_cmd);
            g_pLocalPlayer->UpdateEye();
        }

        if (hacks::tf2::warp::in_warp || hacks::tf2::tickbase::shifting)
            EC::run(EC::CreateMoveWarp);
        else
            EC::run(EC::CreateMove);
    }
    if (time_replaced)
        g_GlobalVars->curtime = curtime_old;
    g_Settings.bInvalid = false;
    {
        PROF_SECTION(CM_chat_stack);
        chat_stack::OnCreateMove();
    }

    // TODO Auto Steam Friend

#if ENABLE_TEXTMODE
    hack::PumpEngine();
#endif
#if ENABLE_IPC
    {
        PROF_SECTION(CM_playerlist);
        static Timer ipc_update_timer{};
        //	playerlist::DoNotKillMe();
        if (ipc_update_timer.test_and_set(1000 * 10))
        {
            ipc::UpdatePlayerlist();
        }
    }
#endif
    if (CE_GOOD(g_pLocalPlayer->entity))
    {
        if (!(roll_speedhack && speedHack(cmd, ret)) && g_pLocalPlayer->bUseSilentAngles)
        {
            float speed, yaw;
            Vector ang, vsilent;
            vsilent.x = cmd->forwardmove;
            vsilent.y = cmd->sidemove;
            vsilent.z = cmd->upmove;
            speed     = sqrt(vsilent.x * vsilent.x + vsilent.y * vsilent.y);
            VectorAngles(vsilent, ang);
            yaw                 = DEG2RAD(ang.y - g_pLocalPlayer->v_OrigViewangles.y + cmd->viewangles.y);
            cmd->forwardmove    = cos(yaw) * speed;
            cmd->sidemove       = sin(yaw) * speed;
            float clamped_pitch = fabsf(fmodf(cmd->viewangles.x, 360.0f));
            if (clamped_pitch >= 90 && clamped_pitch <= 270)
                cmd->forwardmove = -cmd->forwardmove;

            ret = false;
        }
        g_pLocalPlayer->UpdateEnd();
    }

    g_Settings.is_create_move = false;
    if (nolerp && g_ICvar && cl_interp && cl_interp_ratio)
    {
        static const ConVar *pUpdateRate = g_ICvar->FindVar("cl_updaterate");
        if (pUpdateRate && pUpdateRate->GetFloat() > 0.0f)
        {
            float interp = MAX(cl_interp->GetFloat(), cl_interp_ratio->GetFloat() / pUpdateRate->GetFloat());
            cmd->tick_count += TIME_TO_TICKS(interp);
        }
    }
    return ret;
}

void WriteCmd(IInput *input, CUserCmd *cmd, int sequence_nr)
{
    auto *verified = GetVerifiedCmds(input);
    auto *cmds     = GetCmds(input);
    if (!verified || !cmds || !cmd)
        return;
    verified[sequence_nr % VERIFIED_CMD_SIZE].m_cmd = *cmd;
    verified[sequence_nr % VERIFIED_CMD_SIZE].m_crc = GetChecksum(cmd);
    cmds[sequence_nr % VERIFIED_CMD_SIZE]           = *cmd;
}

// This gets called before the other CreateMove, but since we run original first in here all the stuff gets called after normal CreateMove is done
DEFINE_HOOKED_METHOD(CreateMoveInput, void, IInput *this_, int sequence_nr, float input_sample_time, bool arg3)
{
    if (!bSendPackets)
    {
        static bool fallback = true;
        bSendPackets         = &fallback;
    }
    *bSendPackets = true;
    original::CreateMoveInput(this_, sequence_nr, input_sample_time, arg3);

    CUserCmd *cmd = nullptr;
    if (this_ && GetCmds(this_) && sequence_nr > 0)
        cmd = this_->GetUserCmd(sequence_nr);

    if (!cmd)
        return;

    current_late_user_cmd = cmd;

    if (!isHackActive())
    {
        WriteCmd(this_, current_late_user_cmd, sequence_nr);
        return;
    }

    if (!g_IEngine->IsInGame())
    {
        WriteCmd(this_, current_late_user_cmd, sequence_nr);
        return;
    }

    PROF_SECTION(CreateMoveInput);

    // Run EC
    EC::run(EC::CreateMoveLate);

    if (CE_GOOD(LOCAL_E))
    {
        // Restore prediction
        if (engine_prediction::original_origin.IsValid())
            engine_prediction::FinishEnginePrediction(RAW_ENT(LOCAL_E), current_late_user_cmd);
    }
    if (bSendPackets && *bSendPackets)
        g_pLocalPlayer->fakeAngles = current_late_user_cmd->viewangles;
    // Write the usercmd
    WriteCmd(this_, current_late_user_cmd, sequence_nr);
}
} // namespace hooked_methods
