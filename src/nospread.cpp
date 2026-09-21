// To anyone reading this and planning to add it to their own cheat,
// It would be nice if you could credit us, the Nullworks/Cathook team.
// Thanks :)

/*
* Cathook
* Copyright (C) 2020  nullworks

* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.

* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.

* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "DetourHook.hpp"
#include <regex>
#include <boost/algorithm/string.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <vector>
#include "usercmd.hpp"
#include "MiscTemporary.hpp"
#include "AntiAim.hpp"
#include "WeaponData.hpp"
#include "Warp.hpp"
#include "sdk/CNetChan.hpp"

namespace hacks::tf2::nospread
{
static settings::Boolean projectile("nospread.projectile", "false");
/*
 * 0 Always on
 * 1 Disable if being spectated in first person
 * 2 Disable if being spectated
 */
static settings::Int specmode("nospread.spectator-mode", "1");
settings::Boolean bullet("nospread.bullet", "false");
settings::Int debug_nospread("nospread.debug", "0");
settings::Boolean center_cone{ "nospread.center-cone", "true" };
static settings::Boolean tightest_pellet{ "nospread.tightest-pellet", "true" };
settings::Boolean draw{ "nospread.draw-info", "true" };
settings::Boolean draw_mantissa{ "nospread.draw-info.mantissa", "false" };
settings::Boolean correct_ping{ "nospread.correct-ping", "true" };
settings::Boolean use_avg_latency{ "nospread.use-average-latency", "false" };
settings::Boolean extreme_accuracy{ "nospread.use-extreme-accuracy", "false" };
static settings::Int sync_samples{ "nospread.sync-samples", "12" };
static settings::Float resync_interval{ "nospread.resync-interval", "2" };
bool is_syncing = false;

static constexpr std::size_t k_delta_history_max = 24;

bool shouldNoSpread(bool _projectile)
{
    switch (*specmode)
    {
    // Always on
    default:
    case 0:
        break;
    // Disable if being spectated in first person
    case 1:
        if (g_pLocalPlayer->spectator_state == g_pLocalPlayer->FIRSTPERSON)
            return false;
        break;
    // Disable if being spectated
    case 2:
        if (g_pLocalPlayer->spectator_state != g_pLocalPlayer->NONE)
            return false;
    };
    return _projectile ? *projectile : *bullet;
}

static bool IsLoopbackNet()
{
    auto *ch = g_IEngine ? g_IEngine->GetNetChannelInfo() : nullptr;
    return ch && NetChan(ch)->IsLoopback();
}

static float AimFireDistance(const Vector &view)
{
    Vector eye = g_pLocalPlayer->v_Eye;
    Vector fwd;
    AngleVectors2(VectorToQAngle(view), &fwd);
    Vector end = eye + fwd * 8192.0f;

    trace_t tr{};
    Ray_t ray;
    ray.Init(eye, end);
    if (CE_GOOD(LOCAL_E))
        trace::filter_no_player.SetSelf(RAW_ENT(LOCAL_E));
    g_ITrace->TraceRay(ray, MASK_SOLID, &trace::filter_no_player, &tr);

    float dist = (tr.endpos - eye).Length();
    if (!std::isfinite(dist) || dist < 32.0f)
        return 8192.0f;
    return dist;
}

static Vector ProjectileMuzzleOffset(int class_id)
{
    if (class_id == CL_CLASS(CTFCompoundBow) || class_id == CL_CLASS(CTFCrossbow) || class_id == CL_CLASS(CTFShotgunBuildingRescue))
        return Vector(23.5f, 8.0f, -3.0f);
    return Vector(23.5f, 12.0f, -3.0f);
}

static void ApplyGrenadeVelocitySpread(Vector &angles, IClientEntity *)
{
    float speed = 0.0f, grav = 0.0f, start_vel = 0.0f;
    if (!GetProjectileData(LOCAL_W, speed, grav, start_vel) || speed <= 0.0f)
        return;
    if (GetPowerupOnPlayer(LOCAL_E) == precision)
        speed = 3000.0f;

    Vector fwd, right, up;
    AngleVectors3(VectorToQAngle(angles), &fwd, &right, &up);
    Vector velocity = fwd * speed + up * 200.0f;
    Vector adjusted = velocity + up * RandomFloat(-10.0f, 10.0f) + right * RandomFloat(-10.0f, 10.0f);
    Vector vel_ang, adj_ang;
    VectorAngles(velocity, vel_ang);
    VectorAngles(adjusted, adj_ang);
    angles -= (adj_ang - vel_ang);
}

static void CreateMove()
{
    if (CE_BAD(LOCAL_E) || CE_BAD(LOCAL_W))
        return;

    if (!shouldNoSpread(true))
        return;

    int cmd_num = current_late_user_cmd->command_number;
    RandomSeed(MD5_PseudoRandom(cmd_num) & 0x7FFFFFFF);
    SharedRandomInt(MD5_PseudoRandom(cmd_num) & 0x7FFFFFFF, "SelectWeightedSequence", 0, 0, 0);
    for (int i = 0; i < 6; ++i)
        RandomFloat();

    // Projectile/Huntsman check
    if (g_pLocalPlayer->weapon_mode != weapon_projectile && LOCAL_W->m_iClassID() != CL_CLASS(CTFCompoundBow))
        return;

    // Beggars check
    if (CE_INT(LOCAL_W, netvar.iItemDefinitionIndex) == 730)
    {
        bool no_loaded_rockets = CE_INT(LOCAL_W, netvar.m_iClip1) == 0 && CE_INT(LOCAL_W, netvar.iReloadMode) != 2;
        bool loading_rockets   = current_late_user_cmd->buttons & IN_ATTACK && CE_INT(LOCAL_W, netvar.iReloadMode) != 0;
        if (no_loaded_rockets || loading_rockets)
            return;
    }
    else if (LOCAL_W->m_iClassID() == CL_CLASS(CTFCompoundBow))
    {
        if (current_late_user_cmd->buttons & IN_ATTACK || CE_FLOAT(LOCAL_W, netvar.flChargeBeginTime) == 0)
            return;
    }
    else if (!(current_late_user_cmd->buttons & IN_ATTACK))
        return;

    if (g_pLocalPlayer->v_OrigViewangles == current_late_user_cmd->viewangles)
        g_pLocalPlayer->bUseSilentAngles = true;

    IClientEntity *weapon = RAW_ENT(LOCAL_W);
    IClientEntity *player = RAW_ENT(LOCAL_E);
    const int class_id    = LOCAL_W->m_iClassID();
    Vector view           = re::C_BasePlayer::GetLocalEyeAngles(player);
    Vector &out           = current_late_user_cmd->viewangles;

    if (class_id == CL_CLASS(CTFCompoundBow) || class_id == CL_CLASS(CTFCrossbow) || class_id == CL_CLASS(CTFShotgunBuildingRescue))
    {
        Vector src, fired;
        re::C_TFWeaponBase::GetProjectileFireSetup(weapon, player, ProjectileMuzzleOffset(class_id), &src, &fired, false, AimFireDistance(out));
        out -= (fired - view);
    }
    else
    {
        Vector spread = re::C_TFWeaponBase::GetSpreadAngles(weapon);
        out -= (spread - view);
        if (class_id == CL_CLASS(CTFGrenadeLauncher) || class_id == CL_CLASS(CTFCannon) || class_id == CL_CLASS(CTFPipebombLauncher))
            ApplyGrenadeVelocitySpread(out, weapon);
    }

    fClampAngle(out);
}

static InitRoutine init([]() { EC::Register(EC::CreateMoveLate, CreateMove, "nospread_cm", EC::very_late); });

enum nospread_sync_state
{
    NOT_SYNCED = 0,
    CORRECTING,
    SYNCED,
    DEAD_SYNC,
};

// TODO: Attempt to send clc_move over unreliable channel

static bool waiting_perf_data                = false;
static bool should_update_time               = false;
static bool waiting_for_post_SNM             = false;
static bool resync_needed                    = false;
static bool last_was_player_perf             = false;
static bool resynced_this_death              = false;
static nospread_sync_state no_spread_synced  = NOT_SYNCED; // this will be set to 0 each level init / level shutdown
static bool bad_mantissa                     = false;      // Also reset every levelinit/shutdown
static double float_time_delta               = 0.0;
static double ping_at_send                   = 0.0;
static double last_ping_at_send              = 0.0;
static double sent_client_floattime          = 0.0;
static double last_correction                = 0.0;
static double write_usercmd_correction       = 0.0;
static double last_sync_delta_time           = 0.0;
static std::deque<double> time_deltas{};
static float prediction_seed                 = 0.0;
static bool use_usercmd_seed                 = false;
static float current_weapon_spread           = 0.0;
static bool first_usercmd                    = false;
static bool called_from_sendmove             = false;
static bool should_update_usercmd_correction = false;
static CUserCmd user_cmd_backup;

static float CalculateMantissaStep(float flValue)
{
    if (!std::isfinite(flValue))
        return 0.0f;
    const float next = std::nextafter(flValue, std::numeric_limits<float>::infinity());
    const float step = (next - flValue) * 1000.0f;
    if (!std::isfinite(step) || step <= 0.0f)
        return 0.0f;
    return powf(2.0f, ceilf(logf(step) / logf(2.0f)));
}

static double MedianDelta()
{
    if (time_deltas.empty())
        return float_time_delta;
    std::vector<double> sorted(time_deltas.begin(), time_deltas.end());
    std::sort(sorted.begin(), sorted.end());
    const std::size_t n = sorted.size();
    if (n % 2u)
        return sorted[n / 2];
    return 0.5 * (sorted[n / 2 - 1] + sorted[n / 2]);
}

static void PushTimeDelta(double sample)
{
    if (!std::isfinite(sample))
        return;
    time_deltas.push_back(sample);
    const std::size_t cap = std::clamp(std::size_t(std::max(1, int(*sync_samples))), std::size_t(1), k_delta_history_max);
    while (time_deltas.size() > cap)
        time_deltas.pop_front();
    float_time_delta = MedianDelta();
}

float GetServerCurTime()
{
    if (g_GlobalVars && g_GlobalVars->curtime > 0.0f)
        return g_GlobalVars->curtime;
    return g_GlobalVars->interval_per_tick * CE_INT(LOCAL_E, netvar.nTickBase);
}

static int BulletsPerShot(IClientEntity *weapon)
{
    int n = GetWeaponData(weapon)->m_nBulletsPerShot;
    if (n >= 1)
        n = int(ATTRIB_HOOK_FLOAT(float(n), "mult_bullets_per_shot", weapon, nullptr, true));
    return n >= 1 ? n : 1;
}

bool IsPerfectShot(IClientEntity *weapon, float provided_time = 0.0)
{
    float server_time       = provided_time == 0.0 ? GetServerCurTime() : provided_time;
    float time_since_attack = server_time - NET_FLOAT(weapon, netvar.flLastFireTime);
    const int n             = BulletsPerShot(weapon);
    if (!((n == 1 && time_since_attack > 1.25f) || (n > 1 && time_since_attack > 0.25f)))
        return false;
    return ATTRIB_HOOK_FLOAT(0.0f, "mult_spread_scale_first_shot", weapon, nullptr, true) == 0.0f;
}

void ApplySpreadCorrection(Vector &angles, int seed, float spread)
{
    if (CE_BAD(LOCAL_E) || !LOCAL_E->m_bAlivePlayer() || CE_BAD(LOCAL_W))
        return;
    IClientEntity *weapon = RAW_ENT(LOCAL_W);

    const bool is_first_shot_perfect = IsPerfectShot(weapon);
    int nBulletsPerShot              = BulletsPerShot(weapon);
    const float first_shot_scale     = ATTRIB_HOOK_FLOAT(0.0f, "mult_spread_scale_first_shot", weapon, nullptr, true);

    if ((nBulletsPerShot == 1 || (!center_cone && !tightest_pellet)) && is_first_shot_perfect)
        return;

    if (!center_cone && !tightest_pellet)
        nBulletsPerShot = 1;

    struct Pellet
    {
        Vector dir;
        float radial;
    };
    std::vector<Pellet> pellets;
    pellets.reserve(nBulletsPerShot);
    Vector average_spread(0.0f);
    fClampAngle(angles);

    Vector vShootForward, vShootRight, vShootUp;
    AngleVectors3(VectorToQAngle(angles), &vShootForward, &vShootRight, &vShootUp);

    for (int i = 0; i < nBulletsPerShot; i++)
    {
        RandomSeed(seed + i);
        float flX, flY;
        if (is_first_shot_perfect && !i)
        {
            flX = 0.0f;
            flY = 0.0f;
        }
        else if (!i && first_shot_scale != 0.0f)
        {
            flX = RandomFloat(-first_shot_scale, first_shot_scale) + RandomFloat(-first_shot_scale, first_shot_scale);
            flY = RandomFloat(-first_shot_scale, first_shot_scale) + RandomFloat(-first_shot_scale, first_shot_scale);
        }
        else
        {
            flX = RandomFloat(-0.5f, 0.5f) + RandomFloat(-0.5f, 0.5f);
            flY = RandomFloat(-0.5f, 0.5f) + RandomFloat(-0.5f, 0.5f);
        }

        Vector fixed_spread = vShootForward + (vShootRight * flX * spread) + (vShootUp * flY * spread);
        fixed_spread.NormalizeInPlace();
        average_spread += fixed_spread;
        pellets.push_back({ fixed_spread, flX * flX + flY * flY });
    }
    if (pellets.empty())
        return;
    average_spread /= float(nBulletsPerShot);

    Vector chosen = pellets.front().dir;
    if (tightest_pellet)
    {
        float best = pellets.front().radial;
        for (const auto &p : pellets)
        {
            if (p.radial < best)
            {
                best   = p.radial;
                chosen = p.dir;
            }
        }
    }
    else
    {
        float best = chosen.DistToSqr(average_spread);
        for (const auto &p : pellets)
        {
            const float d = p.dir.DistToSqr(average_spread);
            if (d < best)
            {
                best   = d;
                chosen = p.dir;
            }
        }
    }

    Vector fixed_angles;
    VectorAngles(chosen, fixed_angles);
    angles += (angles - fixed_angles);
    fClampAngle(angles);
}

CatCommand debug_mantissa("test_mantissa", "For debug purposes",
                          [](const CCommand &rCmd)
                          {
                              if (rCmd.ArgC() < 2)
                              {
                                  g_ICvar->ConsoleColorPrintf(MENU_COLOR, "You must provide float to test.\n");
                                  return;
                              }

                              try
                              {
                                  float float_value   = atof(rCmd.Arg(1));
                                  float mantissa_step = CalculateMantissaStep(float_value);

                                  g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Mantissa step for %.3f: %.10f\n", float_value, mantissa_step);
                              }
                              catch (const std::invalid_argument &)
                              {
                                  g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Invalid float.\n");
                              }
                              return;
                          });

static CatCommand nospread_sync("nospread_sync", "Try to sync client and server time",
                                []()
                                {
                                    if (!bullet)
                                    {
                                        g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Set nospread.enable to true first.\n");
                                        return;
                                    }
                                    should_update_time = true;
                                    no_spread_synced   = NOT_SYNCED;
                                    g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Trying to sync Seed...\n");
                                });

static CatCommand nospread_resync("nospread_resync", "Try to sync client and server time",
                                  []()
                                  {
                                      if (!bullet)
                                      {
                                          g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Set nospread.enable to true first.\n");
                                          return;
                                      }
                                      if (no_spread_synced == CORRECTING)
                                      {
                                          g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Already syncing.");
                                          return;
                                      }
                                      if (no_spread_synced != SYNCED)
                                      {
                                          g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Can't resync when not previously synced! Use cat_nospread_sync\n");
                                          return;
                                      }
                                      no_spread_synced = CORRECTING;
                                      g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Trying to resync Seed...\n");
                                  });

// Our Detour hooks
DetourHook cl_writeusercmd_detour;
typedef void (*WriteUserCmd_t)(bf_write *, CUserCmd *, CUserCmd *);
DetourHook fx_firebullets_detour;
typedef void (*FX_FireBullets_t)(IClientEntity *, int, Vector *, Vector *, int, int, int, float, float, bool);
// DetourHook net_sendpacket_detour;
// typedef int (*NET_SendPacket_t)(INetChannel *, int, const netadr_t &, const unsigned char *, int, bf_write *, bool);

// false == unchanged, true == Force reliable
// We force the clc_move as a reliable message here to ensure its arrival before we call the original
bool SendNetMessage(INetMessage *data)
{
    if (!bullet)
        return false;

    // if we send clc_move with playerperf command or corrected angles, we must ensure it will be sent via reliable stream
    if (should_update_time)
    {
        if (data->GetType() != clc_Move)
            return false;

        // and wait for post call
        waiting_for_post_SNM = true;

        // Force reliable
        return true;
    }
    else if (no_spread_synced)
    {
        if (data->GetType() != clc_Move)
            return false;
    }
    return false;
}

static Timer wait_perf{};

static void SendPlayerPerf(bool force_reliable)
{
    auto *ch = g_IEngine ? g_IEngine->GetNetChannelInfo() : nullptr;
    if (!ch)
        return;
    NET_StringCmd sCmd("playerperf");
    ch->SendNetMsg(sCmd, force_reliable);
    if (force_reliable)
        ch->Transmit();
    sent_client_floattime = Plat_FloatTime();
    if (use_avg_latency)
        ping_at_send = ch->GetAvgLatency(FLOW_OUTGOING);
    else
        ping_at_send = ch->GetLatency(FLOW_OUTGOING);
    waiting_perf_data = true;
    wait_perf.update();
}

void SendNetMessagePost()
{
    if (!waiting_for_post_SNM || !bullet || (waiting_perf_data && !wait_perf.test_and_set(1000)))
        return;

    waiting_for_post_SNM = false;
    should_update_time   = false;
    SendPlayerPerf(true);
}

CatCommand debug_flows("debug_flows", "debug", []() {
    auto *ch = g_IEngine ? g_IEngine->GetNetChannelInfo() : nullptr;
    if (!ch)
    {
        logging::Info("debug_flows: no netchannel");
        return;
    }
    logging::Info("Incoming: %f\n Outgoing: %f", ch->GetLatency(FLOW_INCOMING), ch->GetLatency(FLOW_OUTGOING));
});

// false == don't call original, true == call original
// This function is used to parse the playerperf data
bool DispatchUserMessage(bf_read *buf, int type)
{
    bool should_call_original = true;
    if ((!waiting_perf_data && !last_was_player_perf) || !bullet)
        return should_call_original;

    // We are looking for TextMsg
    if (type != 5)
        return should_call_original;

    int message_dest = buf->ReadByte();

    // Not send to us
    if (message_dest != 2)
    {
        buf->Seek(0);
        return should_call_original;
    }

    char msg_str[256];
    buf->ReadString(msg_str, sizeof(msg_str));
    buf->Seek(0);

    std::vector<std::string> lines;
    boost::split(lines, msg_str, boost::is_any_of("\n"), boost::token_compress_on);

    // Regex to find the playerperf data we want/need
    static std::regex primary_regex("^(([0-9]+\\.[0-9]+) ([0-9]{1,2}) ([0-9]{1,2}))$");

    std::vector<double> vData;
    static const std::regex long_regex(R"((\d+\.\d+)\s+\d+\s+\d+\s+\d+\.\d+\s+\d+\.\d+\s+vel\s+\d+\.\d+)");
    static const std::regex short_regex(R"(^(\d+\.\d+)\s+\d{1,2}\s+\d{1,2}$)");

    for (auto sStr : lines)
    {
        std::smatch sMatch;
        if (std::regex_search(sStr, sMatch, long_regex) && sMatch.size() >= 2)
        {
            last_was_player_perf = true;
            should_call_original = false;
            char *tmp            = nullptr;
            vData.push_back(std::strtod(sMatch[1].str().c_str(), &tmp));
            continue;
        }
        if (std::regex_match(sStr, sMatch, primary_regex) && sMatch.size() >= 3)
        {
            char *tmp = nullptr;
            vData.push_back(std::strtod(sMatch[2].str().c_str(), &tmp));
            continue;
        }
        if (std::regex_match(sStr, sMatch, short_regex) && sMatch.size() >= 2)
        {
            char *tmp = nullptr;
            vData.push_back(std::strtod(sMatch[1].str().c_str(), &tmp));
            continue;
        }
    }

    if (vData.empty())
    {
        if (!last_was_player_perf)
            return should_call_original;
        return false;
    }

    // Less than 1 in step size is literally impossible to predict, although 1 is already pushing it
    if (CalculateMantissaStep(vData[0] * 1000.0) < 1.0)
    {
        if (waiting_perf_data)
            g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Couldn't sync nospread: server uptime too low.\n");
        waiting_perf_data    = false;
        last_was_player_perf = true;
        should_call_original = false;
        bad_mantissa         = true;
        no_spread_synced     = NOT_SYNCED;
        return should_call_original;
    }

    bad_mantissa         = false;
    last_was_player_perf = true;
    should_call_original = false;

    // Process time!

    // ...or not
    if (!waiting_perf_data)
        return should_call_original;

    if (IsLoopbackNet())
    {
        time_deltas.clear();
        float_time_delta     = 0.0;
        last_correction      = 0.0;
        waiting_perf_data    = false;
        resync_needed        = false;
        no_spread_synced     = SYNCED;
        is_syncing           = false;
        if (debug_nospread)
            g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Nospread loopback: seed is local Plat_FloatTime.\n");
        return should_call_original;
    }

    const double tick = (g_GlobalVars && g_GlobalVars->interval_per_tick > 0.0f) ? double(g_GlobalVars->interval_per_tick) : (1.0 / 66.0);
    PushTimeDelta(vData[0] - sent_client_floattime + tick);

    const double mantissa_step = CalculateMantissaStep(float(vData[0] * 1000.0));
    last_correction            = (Plat_FloatTime() + float_time_delta) - vData[0];
    waiting_perf_data          = false;

    if (debug_nospread)
        g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Nospread sample: server=%.6f delta=%.10f residual=%.10f samples=%zu\n", vData[0], float_time_delta, last_correction, time_deltas.size());

    if (no_spread_synced != SYNCED)
    {
        const bool tight_enough = extreme_accuracy ? (fabs(last_correction) <= 0.001) : true;
        if (tight_enough || time_deltas.size() >= 2)
        {
            g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Nospread successfully synced. Mantissa step: %.2f\n", mantissa_step);
            no_spread_synced = SYNCED;
            is_syncing       = false;
            resync_needed    = false;
        }
        else
        {
            no_spread_synced = CORRECTING;
            is_syncing       = true;
            resync_needed    = true;
        }
    }
    else
    {
        resync_needed = false;
        is_syncing    = false;
    }
    return should_call_original;
};

void CL_SendMove_hook()
{
    first_usercmd        = true;
    called_from_sendmove = false;

    if (!no_spread_synced || !shouldNoSpread(false))
    {
        hacks::tf2::warp::CL_SendMove_hook();
        return;
    }
    // Here we need to calculate process average process time, predict spread and get weapon spread

    current_weapon_spread = 0.0;

    // first try to get the player and check if he is valid
    if (!RAW_ENT(LOCAL_E) || HasCondition<TFCond_HalloweenGhostMode>(LOCAL_E))
    {
        // don't set called_from_sendmove here cuz we don't care
        hacks::tf2::warp::CL_SendMove_hook();
        return;
    }

    int *choked_packets = nullptr;
    if (!choked_packets)
        choked_packets = &g_IBaseClientState->chokedcommands();

    int new_packets = 1 + *choked_packets;

    auto RecheckIfresync_needed = [&new_packets]() -> void
    {
        static Timer s_NextCheck;
        const int interval_ms = std::max(250, int(float(*resync_interval) * 1000.0f));
        if (s_NextCheck.check(interval_ms) && new_packets == 1 && !waiting_perf_data)
        {
            s_NextCheck.update();
            if (no_spread_synced == SYNCED && LOCAL_E->m_bAlivePlayer())
            {
                SendPlayerPerf(false);
                return;
            }
            should_update_time = true;
            if (!LOCAL_E->m_bAlivePlayer() && no_spread_synced != CORRECTING)
            {
                last_sync_delta_time = float_time_delta;
                last_ping_at_send    = ping_at_send;
                no_spread_synced     = DEAD_SYNC;
            }
            resynced_this_death = true;
        }
    };

    // try to predict server's float time
    // please notice that time delta calculated relative to this code possition
    // this means we don't care about procession time of the code below
    // but we care about procession time of dynamic code in WriteUserCmd hook
    double asumed_real_time = Plat_FloatTime() + float_time_delta;
    double predicted_time   = asumed_real_time;

    predicted_time += write_usercmd_correction * new_packets;
    double ping = 0.0;
    if (auto *ch = g_IEngine->GetNetChannelInfo())
        ping = use_avg_latency ? ch->GetAvgLatency(FLOW_OUTGOING) : ch->GetLatency(FLOW_OUTGOING);

    if (correct_ping)
        // Ping changed, adjust (Provided we are not fakelagging)
        if (!(fakelag_amount || (hacks::shared::antiaim::isEnabled() && hacks::shared::antiaim::force_fakelag)) && (int) (ping * 1000.0) != (int) (ping_at_send * 1000.0))
            predicted_time += ping - ping_at_send;

    // Check if we need to sync
    RecheckIfresync_needed();

    // If we're dead just return original
    if (!LOCAL_E->m_bAlivePlayer())
    {
        hacks::tf2::warp::CL_SendMove_hook();
        return;
    }
    else
    {
        // We are alive
        resynced_this_death = false;

        // We should cancel the dead sync process
        if (no_spread_synced == DEAD_SYNC)
        {
            // Restore delta time
            float_time_delta = last_sync_delta_time;
            ping_at_send     = last_ping_at_send;
            // Mark as synced
            no_spread_synced   = SYNCED;
            should_update_time = false;
        }
    }

    // Bad weapon
    if (CE_BAD(LOCAL_W) || (g_pLocalPlayer->weapon_mode != weapon_hitscan && LOCAL_W->m_iClassID() != CL_CLASS(CTFCompoundBow)))
    {
        hacks::tf2::warp::CL_SendMove_hook();
        return;
    }

    float current_time = GetServerCurTime();

    // Check if we are attacking, if not then no point in adjusting
    if (!current_user_cmd || !(current_user_cmd->buttons & IN_ATTACK))
    {
        hacks::tf2::warp::CL_SendMove_hook();
        return;
    }

    // If we have a perfect shot and we don#t want to center the whole cone, returne too
    if (IsPerfectShot(RAW_ENT(LOCAL_W), current_time) && !center_cone)
    {
        hacks::tf2::warp::CL_SendMove_hook();
        return;
    }

    current_weapon_spread = re::C_TFWeaponBaseGun::GetWeaponSpread(RAW_ENT(LOCAL_W));

    // Bad spread
    if (!IsFinite(current_weapon_spread))
    {
        hacks::tf2::warp::CL_SendMove_hook();
        return;
    }

    if (*debug_nospread >= 2)
        g_ICvar->ConsoleColorPrintf(MENU_COLOR, "Predicted: %.6f Assumed: %.6f Correction: %.6f Commands: %i\n", predicted_time, asumed_real_time, write_usercmd_correction, new_packets);
    // Try to predict seed now
    // The important thing to understand this: server_random_seed set as soon as ProcessUsercmds called. And it's called in clc_move process function, so as soon as server accepts our packet, not when it actually processed.
    // This means every usercmd in 1 clc_move will have almost same random seed - if process time less than mantissa step for random seed number, then same "random" number will be set for each usercmd in this packet

    // Only adjust if not using usercmd seed
    if (!use_usercmd_seed)
        prediction_seed = (float) (predicted_time * 1000.0);

    // Call original
    called_from_sendmove = true;
    double time_start    = Plat_FloatTime();

    hacks::tf2::warp::CL_SendMove_hook();

    double time_end      = Plat_FloatTime();
    called_from_sendmove = false;

    // Update the processing time if we actually processed stuff
    if (should_update_usercmd_correction)
    {
        // How long it took for us to process each cmd? We will add this number next time we will process usercommands
        write_usercmd_correction         = (time_end - time_start) / new_packets;
        should_update_usercmd_correction = false;
    }
}

void WriteUserCmd_hook(bf_write *buf, CUserCmd *to, CUserCmd *from)
{
    // Called by a demo recorder or we shouldn't compensate it.
    if ((no_spread_synced != SYNCED && !resync_needed) || !shouldNoSpread(false) || current_weapon_spread == 0.0)
    {
        WriteUserCmd_t original = (WriteUserCmd_t) cl_writeusercmd_detour.GetOriginalFunc();
        original(buf, to, from);
        cl_writeusercmd_detour.RestorePatch();
        return;
    }

    // Save the original from
    CUserCmd backup_from = *from;

    if (!(to->buttons & IN_ATTACK))
    {
        user_cmd_backup         = *to;
        first_usercmd           = false;
        WriteUserCmd_t original = (WriteUserCmd_t) cl_writeusercmd_detour.GetOriginalFunc();
        original(buf, to, from);
        cl_writeusercmd_detour.RestorePatch();
        return;
    }

    // Save the original to
    CUserCmd backup_to = *to;

    // If nospread fully synced, the only thing we need to do is to use already known float time with our random seed

    ApplySpreadCorrection(to->viewangles, reinterpret_cast<int &>(prediction_seed) & 0xFF, current_weapon_spread);

    // We are processing, so we should update it
    should_update_usercmd_correction = true;

    user_cmd_backup = *to;
    first_usercmd   = false;

    WriteUserCmd_t original = (WriteUserCmd_t) cl_writeusercmd_detour.GetOriginalFunc();
    original(buf, to, from);
    cl_writeusercmd_detour.RestorePatch();

    // Restore the original from
    *from = backup_from;
    // Restore the original to
    *to = backup_to;
}

void FX_FireBullets_hook(IClientEntity *weapon, int player, Vector *origin, Vector *angles, int weapon_idx, int bullet_mode, int seed, float spread, float damage, bool is_critical)
{
    // Not synced/weapon bad
    if (!weapon || (no_spread_synced != SYNCED && !resync_needed) || !bullet || (IsPerfectShot(weapon) && !center_cone && !tightest_pellet))
    {
        FX_FireBullets_t original = (FX_FireBullets_t) fx_firebullets_detour.GetOriginalFunc();
        original(weapon, player, origin, angles, weapon_idx, bullet_mode, seed, spread, damage, is_critical);
        fx_firebullets_detour.RestorePatch();
        return;
    }

    Vector corrected_angles = *angles;
    ApplySpreadCorrection(corrected_angles, seed, spread);

    FX_FireBullets_t original = (FX_FireBullets_t) fx_firebullets_detour.GetOriginalFunc();
    original(weapon, player, origin, &corrected_angles, weapon_idx, bullet_mode, seed, spread, damage, is_critical);
    fx_firebullets_detour.RestorePatch();
}

/*int NET_SendPacket_hook(INetChannel *chan, int sock, const netadr_t &to, const unsigned char *data, int length, bf_write *pVoicePayload, bool bUseCompression)
{
    logging::Info("Packet size: %d", length);
    NET_SendPacket_t original = (NET_SendPacket_t) net_sendpacket_detour.GetOriginalFunc();
    auto ret_val              = original(chan, sock, to, data, length, pVoicePayload, bUseCompression);
    net_sendpacket_detour.RestorePatch();
    return ret_val;
}*/

static Timer update_nospread_timer{};
static void CreateMove2()
{
    if (bullet)
    {
        static auto sv_usercmd_custom_random_seed = g_ICvar->FindVar("sv_usercmd_custom_random_seed");
        if (!sv_usercmd_custom_random_seed)
            sv_usercmd_custom_random_seed = g_ICvar->FindVar("sv_usercmd_custom_random_seed");

        // Server owner decided it would be a great idea to give the user control over the random seed
        else if (!sv_usercmd_custom_random_seed->GetBool())
        {
            auto seed        = MD5_PseudoRandom(current_user_cmd->command_number) & 0x7FFFFFFF;
            prediction_seed  = *reinterpret_cast<float *>(&seed);
            use_usercmd_seed = true;
        }
        // Normal server
        else
            use_usercmd_seed = false;

        if (IsLoopbackNet() || use_usercmd_seed)
        {
            no_spread_synced = SYNCED;
            is_syncing       = false;
            bad_mantissa     = false;
        }

        // Synced, mark as such to other modules
        if (no_spread_synced == SYNCED)
            is_syncing = false;
        // Not synced currently, try to sync
        if (no_spread_synced == NOT_SYNCED && !bad_mantissa)
        {
            is_syncing         = true;
            should_update_time = true;
            update_nospread_timer.update();
        }
        // Else if mantissa bad, update every 10 mins
        else if (no_spread_synced == NOT_SYNCED && update_nospread_timer.test_and_set(10 * 60 * 1000))
            no_spread_synced = CORRECTING;
    }
}

static InitRoutine init_bulletnospread(
    []()
    {
        // Get our detour hooks running
        static auto writeusercmd_addr = gSignatures.GetClientSignature(sigs::write_usercmd);
        cl_writeusercmd_detour.Init(writeusercmd_addr, (void *) WriteUserCmd_hook);
        static auto fx_firebullets_addr = gSignatures.GetClientSignature(sigs::fx_fire_bullets);
        fx_firebullets_detour.Init(fx_firebullets_addr, (void *) FX_FireBullets_hook);

        // Register Event callbacks
        EC::Register(EC::CreateMove, CreateMove2, "nospread_createmove2");
        EC::Register(EC::CreateMoveWarp, CreateMove2, "nospread_createmove2w");

        bullet.installChangeCallback(
            [](settings::VariableBase<bool> &, bool after)
            {
                if (!after)
                {
                    is_syncing       = false;
                    no_spread_synced = NOT_SYNCED;
                }
            });
#if ENABLE_VISUALS
        EC::Register(
            EC::Draw,
            []()
            {
                if (bullet && (draw || draw_mantissa) && CE_GOOD(LOCAL_E) && LOCAL_E->m_bAlivePlayer())
                {
                    std::string draw_string = "";
                    rgba_t draw_color       = colors::white;
                    switch (no_spread_synced)
                    {
                    case NOT_SYNCED:
                    {
                        if (bad_mantissa)
                        {
                            draw_color  = colors::red_s;
                            draw_string = "Server uptime too Low!";
                        }
                        else
                        {
                            draw_color  = colors::orange;
                            draw_string = "Not Syncing";
                        }
                        break;
                    }
                    case CORRECTING:
                    case DEAD_SYNC:
                    {
                        draw_color  = colors::yellow;
                        draw_string = "Syncing...";
                        break;
                    }
                    case SYNCED:
                    {
                        draw_color  = colors::green;
                        draw_string = "Synced.";
                        break;
                    }
                    default:
                        break;
                    }
                    if (draw)
                        AddCenterString(draw_string, draw_color);
                    if (draw_mantissa && no_spread_synced != NOT_SYNCED)
                        AddCenterString("Mantissa step size: " + std::to_string((int) CalculateMantissaStep(1000.0 * (Plat_FloatTime() + float_time_delta))), draw_color);
                }
            },
            "nospread_draw");
#endif
        EC::Register(
            EC::LevelInit,
            []()
            {
                no_spread_synced     = NOT_SYNCED;
                last_was_player_perf = false;
                bad_mantissa         = false;
                waiting_perf_data    = false;
                time_deltas.clear();
                float_time_delta = 0.0;
            },
            "nospread_levelinit");

        EC::Register(
            EC::LevelShutdown,
            []()
            {
                no_spread_synced     = NOT_SYNCED;
                last_was_player_perf = false;
                bad_mantissa         = false;
                waiting_perf_data    = false;
                time_deltas.clear();
                float_time_delta = 0.0;
            },
            "nospread_levelshutdown");
        EC::Register(
            EC::Shutdown,
            []()
            {
                cl_writeusercmd_detour.Shutdown();
                fx_firebullets_detour.Shutdown();
                // net_sendpacket_detour.Shutdown();
            },
            "nospread_shutdown");
    });

} // namespace hacks::tf2::nospread
