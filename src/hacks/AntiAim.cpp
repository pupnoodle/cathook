/*
 * AntiAim.cpp
 *
 *  Created on: Oct 26, 2016
 *      Author: nullifiedcat
 */

#include <hacks/hacklist.hpp>
#include <settings/Bool.hpp>
#include <hacks/AntiAim.hpp>

#include "common.hpp"
#include "MiscTemporary.hpp"

namespace hacks::shared::antiaim
{
bool force_fakelag = false;
float used_yaw     = 0.0f;
float used_fake_yaw = 0.0f;
static settings::Boolean enable{ "antiaim.enable", "0" };
static settings::Boolean draw_fakes{ "antiaim.draw-fakes", "0" };

static settings::Boolean no_clamping{ "antiaim.no-clamp", "0" };
static settings::Float roll{ "antiaim.roll", "0" };
static settings::Float spin{ "antiaim.spin-speed", "10" };
static settings::Float yaw_offset{ "antiaim.yaw.offset", "0" };
static settings::Float jitter_offset{ "antiaim.yaw.jitter-offset", "90" };
static settings::Int jitter_ticks{ "antiaim.yaw.jitter-ticks", "2" };
static settings::Float distort_range{ "antiaim.yaw.distort", "35" };
static settings::Float pitch_jitter{ "antiaim.pitch.jitter", "30" };
static settings::Float at_target_offset{ "antiaim.yaw.at-target-offset", "180" };

static settings::Int pitch_fake{ "antiaim.pitch.fake", "0" };
static settings::Int pitch_real{ "antiaim.pitch.real", "0" };
static settings::Float pitch_static{ "antiaim.pitch.static", "0" };

static settings::Int yaw_fake{ "antiaim.yaw.fake", "0" };
static settings::Float yaw_fake_static{ "antiaim.yaw.fake.static", "0" };
static settings::Int yaw_real{ "antiaim.yaw.real", "0" };
static settings::Float yaw_real_static{ "antiaim.yaw.real.static", "0" };

static settings::Boolean aaaa_enable{ "antiaim.aaaa.enable", "0" };
static settings::Float aaaa_interval{ "antiaim.aaaa.interval.seconds", "0" };
static settings::Float aaaa_interval_random_high{ "antiaim.aaaa.interval.random-high", "10" };
static settings::Float aaaa_interval_random_low{ "antiaim.aaaa.interval.random-low", "2" };
static settings::Int aaaa_mode{ "antiaim.aaaa.mode", "0" };
static settings::Button aaaa_flip_key{ "antiaim.aaaa.flip-key", "<null>" };
static settings::Button manual_left{ "antiaim.manual.left", "<null>" };
static settings::Button manual_right{ "antiaim.manual.right", "<null>" };
static settings::Button manual_back{ "antiaim.manual.back", "<null>" };
static settings::Button manual_forward{ "antiaim.manual.forward", "<null>" };

static settings::Int yaw_sideways_min{ "antiaim.yaw.sideways.min", "0" };
static settings::Int yaw_sideways_max{ "antiaim.yaw.sideways.max", "4" };

enum pitch_real_t
{
    PREAL_OFF = 0,
    PREAL_CUSTOM,
    PREAL_UP,
    PREAL_DOWN,
    PREAL_JITTER,
    PREAL_RANDOM,
    PREAL_FLIP,
    PREAL_HECK,
    PREAL_HALFUP,
    PREAL_ZERO
};

enum pitch_fake_t
{
    PFAKE_OFF = 0,
    PFAKE_UP,
    PFAKE_DOWN,
    PFAKE_INVERSE,
    PFAKE_CLASSIC_UP,
    PFAKE_CLASSIC_DOWN,
    PFAKE_ZERO,
    PFAKE_JITTER
};

enum yaw_mode_t
{
    YAW_OFF = 0,
    YAW_CUSTOM,
    YAW_CUSTOM_OFFSET,
    YAW_LEFT,
    YAW_RIGHT,
    YAW_BACK,
    YAW_SPIN,
    YAW_EDGE,
    YAW_SIDEWAYS,
    YAW_HECK,
    YAW_OMEGA,
    YAW_RANDOM,
    YAW_RANDOM_CLAMPED,
    YAW_AT_TARGET,
    YAW_JITTER,
    YAW_FREESTAND,
    YAW_DISTORT
};

float cur_yaw[2] = { 0.0f, 0.0f };

int safe_space = 0;

float aaaa_timer_start = 0.0f;
float aaaa_timer       = 0.0f;
int aaaa_stage         = 0;
bool aaaa_key_pressed  = false;

float GetAAAAPitch()
{
    switch ((int) aaaa_mode)
    {
    case 0:
        return aaaa_stage ? -271 : -89;
    case 1:
        return aaaa_stage ? 271 : 89;
    case 2:
        return aaaa_stage ? -180 : 180;
    default:
        break;
    }
    return 0;
}

float GetAAAATimerLength()
{
    if (aaaa_interval)
        return (float) aaaa_interval;
    return RandFloatRange((float) aaaa_interval_random_low, (float) aaaa_interval_random_high);
}

void NextAAAA()
{
    aaaa_stage++;
    if (aaaa_stage > 1)
        aaaa_stage = 0;
}

void UpdateAAAAKey()
{
    if (aaaa_flip_key.isKeyDown())
    {
        if (!aaaa_key_pressed)
        {
            aaaa_key_pressed = true;
            NextAAAA();
        }
    }
    else
        aaaa_key_pressed = false;
}

void UpdateAAAATimer()
{
    const float &curtime = g_GlobalVars->curtime;
    if (aaaa_timer_start > curtime)
        aaaa_timer_start = 0.0f;
    if (!aaaa_timer || !aaaa_timer_start)
    {
        aaaa_timer       = GetAAAATimerLength();
        aaaa_timer_start = curtime;
    }
    else if (curtime - aaaa_timer_start > aaaa_timer)
    {
        NextAAAA();
        aaaa_timer_start = curtime;
        aaaa_timer       = GetAAAATimerLength();
    }
}

enum k_EFuckMode
{
    FM_INCREMENT,
    FM_RANDOMVARS,
    FM_JITTER,
    FM_SIGNFLIP,
    FM_COUNT
};

void FuckPitch(float &io_pitch)
{
    constexpr float min_pitch = -149489.97f;
    constexpr float max_pitch = 149489.97f;
    static k_EFuckMode fuckmode = k_EFuckMode::FM_RANDOMVARS;

    switch (fuckmode)
    {
    case k_EFuckMode::FM_RANDOMVARS:
        io_pitch = RandFloatRange(min_pitch, max_pitch);
    default:
        break;
    }

    if (io_pitch < min_pitch)
        io_pitch = min_pitch;
    if (io_pitch > max_pitch)
        io_pitch = max_pitch;
}

void FuckYaw(float &io_yaw)
{
    constexpr float min_yaw = -359999.97f;
    constexpr float max_yaw = 359999.97f;
    static k_EFuckMode fuckmode = k_EFuckMode::FM_RANDOMVARS;

    switch (fuckmode)
    {
    case k_EFuckMode::FM_RANDOMVARS:
        io_yaw = RandFloatRange(min_yaw, max_yaw);
    default:
        break;
    }

    if (io_yaw < min_yaw)
        io_yaw = min_yaw;
    if (io_yaw > max_yaw)
        io_yaw = max_yaw;
}

void SetSafeSpace(int safespace)
{
    if (safespace > safe_space)
        safe_space = safespace;
}

void SendNetMessage(INetMessage &msg)
{
    if (!enable)
        return;

    if (!((KeyValues *) (((unsigned *) &msg)[4])))
        return;

    auto name = ((KeyValues *) (((unsigned *) &msg)[4]))->GetName();

    if (CE_BAD(LOCAL_E))
        return;

    if (!strcmp(name, "+use_action_slot_item_server") && HasWeapon(LOCAL_E, 1152))
        SetSafeSpace(2);
}

bool ShouldAA(CUserCmd *cmd)
{
    if (hacks::tf2::antibackstab::noaa)
        return false;
    if (cmd->buttons & IN_USE)
        return false;
    int classid = LOCAL_W->m_iClassID();
    auto mode   = GetWeaponMode();
    if ((cmd->buttons & IN_ATTACK) && !(classid == CL_CLASS(CTFCompoundBow) || mode == weapon_melee) && CanShoot())
        return false;
    if ((cmd->buttons & IN_ATTACK2) && classid == CL_CLASS(CTFLunchBox))
        return false;
    if ((cmd->buttons & IN_ATTACK) && classid == CL_CLASS(CTFGrapplingHook) && !g_pLocalPlayer->bAttackLastTick)
        SetSafeSpace(2);
    switch (mode)
    {
    case weapon_projectile:
        if (classid == CL_CLASS(CTFCompoundBow))
        {
            if (!(cmd->buttons & IN_ATTACK))
            {
                if (g_pLocalPlayer->bAttackLastTick)
                    SetSafeSpace(4);
            }
            break;
        }
        [[fallthrough]];
    case weapon_throwable:
        if ((cmd->buttons & (IN_ATTACK | IN_ATTACK2)) || g_pLocalPlayer->bAttackLastTick)
        {
            SetSafeSpace(8);
            return false;
        }
        break;
    case weapon_melee:
        if (g_pLocalPlayer->weapon_melee_damage_tick)
            return false;
        if (g_pLocalPlayer->clazz == tf_class::tf_spy && cmd->buttons & IN_ATTACK && CanShoot())
            return false;
    default:
        break;
    }
    if (safe_space)
    {
        safe_space--;
        if (safe_space < 0)
            safe_space = 0;
        return false;
    }
    return true;
}

float edgeYaw      = 0;
float edgeToEdgeOn = 0;

float edgeDistance(float edgeRayYaw)
{
    trace_t trace;
    Ray_t ray;
    Vector forward;
    float sp, sy, cp, cy;
    sy        = sinf(DEG2RAD(edgeRayYaw));
    cy        = cosf(DEG2RAD(edgeRayYaw));
    sp        = sinf(DEG2RAD(0));
    cp        = cosf(DEG2RAD(0));
    forward.x = cp * cy;
    forward.y = cp * sy;
    forward.z = -sp;
    forward   = forward * 300.0f + g_pLocalPlayer->v_Eye;
    ray.Init(g_pLocalPlayer->v_Eye, forward);
    g_ITrace->TraceRay(ray, 0x4200400B, &trace::filter_no_player, &trace);
    return sqrt(pow(trace.startpos.x - trace.endpos.x, 2) + pow(trace.startpos.y - trace.endpos.y, 2));
}

bool findEdge(float edgeOrigYaw)
{
    float edgeLeftDist  = edgeDistance(edgeOrigYaw - 21);
    edgeLeftDist        = edgeLeftDist + edgeDistance(edgeOrigYaw - 27);
    float edgeRightDist = edgeDistance(edgeOrigYaw + 21);
    edgeRightDist       = edgeRightDist + edgeDistance(edgeOrigYaw + 27);

    if (edgeLeftDist >= 260)
        edgeLeftDist = 999999999;
    if (edgeRightDist >= 260)
        edgeRightDist = 999999999;

    if (edgeLeftDist == edgeRightDist)
        return false;

    if (edgeRightDist < edgeLeftDist)
    {
        edgeToEdgeOn = 1;
        if ((((int) pitch_real == PREAL_UP) || ((int) pitch_real == PREAL_JITTER)) && !g_pLocalPlayer->isFakeAngleCM)
            edgeToEdgeOn = 2;
        return true;
    }
    edgeToEdgeOn = 2;
    if ((((int) pitch_real == PREAL_UP) || ((int) pitch_real == PREAL_JITTER)) && !g_pLocalPlayer->isFakeAngleCM)
        edgeToEdgeOn = 1;
    return true;
}

float useEdge(float edgeViewAngle)
{
    bool edgeTest = true;
    if (((edgeViewAngle < -135) || (edgeViewAngle > 135)) && edgeTest == true)
    {
        if (edgeToEdgeOn == 1)
            edgeYaw = (float) -90;
        if (edgeToEdgeOn == 2)
            edgeYaw = (float) 90;
        edgeTest = false;
    }
    if ((edgeViewAngle >= -135) && (edgeViewAngle < -45) && edgeTest == true)
    {
        if (edgeToEdgeOn == 1)
            edgeYaw = (float) 0;
        if (edgeToEdgeOn == 2)
            edgeYaw = (float) 179;
        edgeTest = false;
    }
    if ((edgeViewAngle >= -45) && (edgeViewAngle < 45) && edgeTest == true)
    {
        if (edgeToEdgeOn == 1)
            edgeYaw = (float) 90;
        if (edgeToEdgeOn == 2)
            edgeYaw = (float) -90;
        edgeTest = false;
    }
    if ((edgeViewAngle <= 135) && (edgeViewAngle >= 45) && edgeTest == true)
    {
        if (edgeToEdgeOn == 1)
            edgeYaw = (float) 179;
        if (edgeToEdgeOn == 2)
            edgeYaw = (float) 0;
        edgeTest = false;
    }
    return edgeYaw;
}

static CachedEntity *ClosestThreat()
{
    CachedEntity *best = nullptr;
    float best_d       = FLT_MAX;
    for (auto const &ent : entity_cache::player_cache)
    {
        if (CE_BAD(ent) || !ent->m_bAlivePlayer() || !ent->m_bEnemy() || ent == LOCAL_E)
            continue;
        float d = ent->m_flDistance();
        if (d < best_d)
        {
            best_d = d;
            best   = ent;
        }
    }
    return best;
}

static float YawToEntity(CachedEntity *ent)
{
    Vector delta = ent->m_vecOrigin() - g_pLocalPlayer->v_Eye;
    Vector ang;
    VectorAngles(delta, ang);
    return ang.y;
}

static bool applyFreestand(float &yaw)
{
    CachedEntity *threat = ClosestThreat();
    if (!threat)
        return false;

    float threat_yaw = YawToEntity(threat);
    float left_d     = edgeDistance(threat_yaw + 90.0f);
    float right_d    = edgeDistance(threat_yaw - 90.0f);
    if (left_d >= 260.0f && right_d >= 260.0f)
        return false;
    yaw = (left_d < right_d) ? threat_yaw + 90.0f : threat_yaw - 90.0f;
    return true;
}

static bool applyManualYaw(float view_yaw, float &yaw)
{
    if (manual_left && manual_left.isKeyDown())
    {
        yaw = view_yaw + 90.0f;
        return true;
    }
    if (manual_right && manual_right.isKeyDown())
    {
        yaw = view_yaw - 90.0f;
        return true;
    }
    if (manual_back && manual_back.isKeyDown())
    {
        yaw = view_yaw + 180.0f;
        return true;
    }
    if (manual_forward && manual_forward.isKeyDown())
    {
        yaw = view_yaw;
        return true;
    }
    return false;
}

static float randyaw = 0.0f;

static void applyYawMode(int mode, bool real_slot, float &y, bool &clamp, bool &swap)
{
    const float custom = real_slot ? float(yaw_real_static) : float(yaw_fake_static);
    switch (mode)
    {
    case YAW_CUSTOM:
        y = custom;
        break;
    case YAW_CUSTOM_OFFSET:
        y += custom;
        break;
    case YAW_LEFT:
        y -= 90.0f;
        break;
    case YAW_RIGHT:
        y += 90.0f;
        break;
    case YAW_BACK:
        y += 180.0f;
        break;
    case YAW_SPIN:
        cur_yaw[real_slot] += real_slot ? float(spin) : -float(spin);
        cur_yaw[real_slot] = AngleNormalizeTF(cur_yaw[real_slot]);
        y                  = cur_yaw[real_slot];
        break;
    case YAW_EDGE:
        if (findEdge(y))
            y = useEdge(y);
        break;
    case YAW_SIDEWAYS:
    {
        if (!real_slot)
            swap = !swap;
        int span = int(yaw_sideways_max) - int(yaw_sideways_min);
        if (span < 0)
            span = 0;
        int hold = int(yaw_sideways_min) + (span ? (tickcount % (span + 1)) : 0);
        if (hold <= 0 || (tickcount / std::max(1, hold)) % 2)
            y += swap ? 90.0f : -90.0f;
        else
            y += swap ? -90.0f : 90.0f;
        break;
    }
    case YAW_HECK:
        FuckYaw(y);
        clamp = false;
        break;
    case YAW_OMEGA:
        if (!real_slot)
        {
            randyaw += RandFloatRange(-30.0f, 30.0f);
            y = randyaw;
        }
        else
            y = randyaw - 180.0f + RandFloatRange(-40.0f, 40.0f);
        break;
    case YAW_RANDOM:
        y     = RandFloatRange(-65536.0f, 65536.0f);
        clamp = false;
        break;
    case YAW_RANDOM_CLAMPED:
        y = RandFloatRange(-180.0f, 180.0f);
        break;
    case YAW_AT_TARGET:
        if (auto *t = ClosestThreat())
            y = YawToEntity(t) + float(at_target_offset);
        else
            y += 180.0f;
        break;
    case YAW_JITTER:
    {
        int period = std::max(1, int(jitter_ticks));
        bool flip  = ((tickcount / period) % 2) != 0;
        if (!real_slot)
            flip = !flip;
        y += flip ? float(jitter_offset) : -float(jitter_offset);
        break;
    }
    case YAW_FREESTAND:
        if (!applyFreestand(y))
        {
            if (findEdge(y))
                y = useEdge(y);
            else
                y += 180.0f;
        }
        break;
    case YAW_DISTORT:
        y += 180.0f + RandFloatRange(-float(distort_range), float(distort_range));
        break;
    default:
        break;
    }
}

void ProcessUserCmd(CUserCmd *cmd)
{
    if (!enable)
        return;
    if (!ShouldAA(cmd))
        return;
    if (!pitch_fake && !pitch_real && !yaw_fake && !yaw_real)
        return;

    float &p         = cmd->viewangles.x;
    float &y         = cmd->viewangles.y;
    const float view = y;
    static bool flip = false;
    bool clamp       = !no_clamping;
    bool yaw_mode    = !g_pLocalPlayer->isFakeAngleCM;

    static int ticksUntilSwap = 0;
    static bool swap          = true;

    if (ticksUntilSwap > 0 && (*yaw_fake != YAW_SIDEWAYS || *yaw_real != YAW_SIDEWAYS))
    {
        swap           = true;
        ticksUntilSwap = 0;
    }

    if (!applyManualYaw(view, y))
        applyYawMode(yaw_mode ? int(yaw_real) : int(yaw_fake), yaw_mode, y, clamp, swap);

    if (yaw_offset)
        y += float(yaw_offset);

    switch (int(pitch_real))
    {
    case PREAL_CUSTOM:
        p = float(pitch_static);
        break;
    case PREAL_UP:
        p = -89.0f;
        break;
    case PREAL_DOWN:
        p = 89.0f;
        break;
    case PREAL_JITTER:
        p += flip ? float(pitch_jitter) : -float(pitch_jitter);
        break;
    case PREAL_RANDOM:
        p = RandFloatRange(-89.0f, 89.0f);
        break;
    case PREAL_FLIP:
        p = flip ? 89.0f : -89.0f;
        break;
    case PREAL_HECK:
        FuckPitch(p);
        clamp = false;
        break;
    case PREAL_HALFUP:
        p = -45.0f;
        break;
    case PREAL_ZERO:
        p = 0.0f;
        break;
    default:
        break;
    }

    switch (int(pitch_fake))
    {
    case PFAKE_UP:
        p -= 360.0f;
        break;
    case PFAKE_DOWN:
        p += 360.0f;
        break;
    case PFAKE_INVERSE:
        if (p <= -89.0f)
            p += 360.0f;
        else if (p >= 89.0f)
            p -= 360.0f;
        break;
    case PFAKE_CLASSIC_UP:
        p = -271.0f;
        clamp = false;
        break;
    case PFAKE_CLASSIC_DOWN:
        p = 271.0f;
        clamp = false;
        break;
    case PFAKE_ZERO:
        p     = flip ? 180.0f : -180.0f;
        clamp = false;
        break;
    case PFAKE_JITTER:
        p     = flip ? -271.0f : 271.0f;
        clamp = false;
        break;
    }

    flip = !flip;
    if (clamp)
        fClampAngle(cmd->viewangles);
    if (roll)
        cmd->viewangles.z = float(roll);
    if (aaaa_enable)
    {
        UpdateAAAAKey();
        UpdateAAAATimer();
        p = GetAAAAPitch();
    }

    if (g_pLocalPlayer->isFakeAngleCM)
        used_fake_yaw = y;
    else
        used_yaw = y;
    g_pLocalPlayer->bUseSilentAngles = true;
}

#if ENABLE_VISUALS
static void DrawFakes()
{
    if (!enable || !draw_fakes || CE_BAD(LOCAL_E) || !LOCAL_E->m_bAlivePlayer())
        return;

    Vector origin = g_pLocalPlayer->v_Eye;
    origin.z -= 8.0f;

    auto line = [&](float yaw, const rgba_t &clr, float length) {
        Vector fwd;
        AngleVectors2({ 0.0f, yaw, 0.0f }, &fwd);
        Vector end = origin + fwd * length;
        Vector a, b;
        if (draw::WorldToScreen(origin, a) && draw::WorldToScreen(end, b))
            draw::Line(a.x, a.y, b.x - a.x, b.y - a.y, clr, 2.0f);
    };

    line(used_yaw, colors::green, 48.0f);
    if (yaw_fake)
        line(used_fake_yaw, colors::red_s, 40.0f);
}
#endif

bool isEnabled()
{
    return *enable;
}

static InitRoutine fakelag_check(
    []()
    {
        yaw_fake.installChangeCallback([](settings::VariableBase<int> &, int after) { force_fakelag = after > 0; });
#if ENABLE_VISUALS
        EC::Register(EC::Draw, DrawFakes, "aa_draw_fakes");
#endif
    });
} // namespace hacks::shared::antiaim
