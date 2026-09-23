/*
 * Created on 16.4.2020
 * Author: BenCat07
 *
 * Copyright Nullworks 2020
 */

#include "common.hpp"
#if ENABLE_VISUALS
    #include "drawing.hpp"
#endif
#include "MiscAimbot.hpp"
#include "NavBot.hpp"
#include "PlayerTools.hpp"
#include "WeaponData.hpp"
#include "MiscTemporary.hpp"
#include "Think.hpp"
#include "Aimbot.hpp"
#include "Tickbase.hpp"
#include "Warp.hpp"
#include <Misc.hpp>
#include <limits>

namespace hacks::tf2::warp
{
settings::Boolean enabled{ "warp.enabled", "false" };
static settings::Float speed{ "warp.speed", "22" };
static settings::Boolean draw{ "warp.draw", "false" };
static settings::Boolean draw_bar{ "warp.draw-bar", "false" };
static settings::Button warp_key{ "warp.key", "<null>" };
static settings::Button charge_key{ "warp.charge-key", "<null>" };
static settings::Boolean charge_passively{ "warp.charge-passively", "true" };
static settings::Boolean charge_in_jump{ "warp.charge-passively.jump", "true" };
static settings::Boolean charge_no_input{ "warp.charge-passively.no-inputs", "false" };
static settings::Int warp_movement_ratio{ "warp.movement-ratio", "6" };
settings::Boolean dodge_projectile{ "warp.dodge_proj", "true" };
static settings::Boolean warp_demoknight{ "warp.demoknight", "false" };
static settings::Boolean warp_peek{ "warp.peek", "false" };
static settings::Boolean warp_on_damage{ "warp.on-hit", "false" };
static settings::Boolean warp_forward{ "warp.on-hit.forward", "false" };
static settings::Boolean warp_melee{ "warp.to.enemy", "true" };
static settings::Boolean warp_backwards{ "warp.on-hit.backwards", "false" };
static settings::Boolean warp_left{ "warp.on-hit.left", "true" };
static settings::Boolean warp_right{ "warp.on-hit.right", "true" };

static settings::Boolean debug_seqout{ "debug.warp_seqout", "false" };
static boost::unordered_flat_map<CachedEntity *, Vector> proj_map;

bool in_warp = false;

int GetMaxWarpTicks()
{
    return hacks::tf2::tickbase::MaxTicks();
}

static int WarpTicks()
{
    return hacks::tf2::tickbase::Ticks();
}

// Draw control
static settings::Int size{ "warp.bar-size", "100" };
static settings::Int bar_x{ "warp.bar-x", "50" };
static settings::Int bar_y{ "warp.bar-y", "200" };
static settings::Int draw_string_x{ "warp.draw-info.x", "8" };
static settings::Int draw_string_y{ "warp.draw-info.y", "800" };

static std::array<std::string, 32> warp_strings;
#if ENABLE_VISUALS
static size_t warp_strings_count{ 0 };
static std::array<rgba_t, 32> warp_strings_colors{ colors::empty };

void AddWarpString(const std::string &string, const rgba_t &color)
{
    warp_strings[warp_strings_count]        = string;
    warp_strings_colors[warp_strings_count] = color;
    ++warp_strings_count;
}

void DrawWarpStrings()
{
    float x = *draw_string_x;
    float y = *draw_string_y;
    for (size_t i = 0; i < warp_strings_count; ++i)
    {
        float sx, sy;
        fonts::menu->stringSize(warp_strings[i], &sx, &sy);
        draw::String(x, y, warp_strings_colors[i], warp_strings[i].c_str(), *fonts::center_screen);
        y += fonts::center_screen->size + 1;
    }
    warp_strings_count = 0;
}
#endif

static bool should_melee  = false;
static bool was_hurt      = false;
static bool should_warp   = true;
static bool warp_dodge    = false;
static float yaw_amount   = 90.0f;
static int warp_amount_override = 0;

void dodgeProj(CachedEntity *proj_ptr)
{

    Vector eav;
    const Vector player_origin = re::C_BaseEntity::GetAbsOrigin(RAW_ENT(LOCAL_E));
    velocity::EstimateAbsVelocity(RAW_ENT(proj_ptr), eav);
    if (1 < eav.Length())
    {
        Vector proj_pos         = re::C_BaseEntity::GetAbsOrigin(RAW_ENT(proj_ptr));
        float multipler         = 2.0f;
        bool add_grav           = false;
        float high_time         = 10;
        float high_displacement = std::numeric_limits<float>::max();
        float curr_grav         = g_ICvar->FindVar("sv_gravity")->GetFloat();
        if (proj_ptr->m_Type() == ENTITY_PROJECTILE && ProjGravMult(proj_ptr->m_iClassID(), eav.Length()) > 0.001f)
            add_grav = true;
        curr_grav = curr_grav * ProjGravMult(proj_ptr->m_iClassID(), eav.Length());

        if (!add_grav)
        {
            float c_1 = ((float) (player_origin - proj_pos).Dot(eav)) / ((float) eav.Dot(eav));
            if (c_1 > 0)
            {
                float dist = (eav * c_1 + proj_pos).DistToSqr(player_origin);
                if (dist > 40000)
                    proj_map.insert({ proj_ptr, Vector(0, 0, 0) });
                else
                    proj_map.insert({ proj_ptr, eav });
            }
            return;
        }
        float last_displacement = high_displacement;
        int sign                = 1;
        int repeats             = 0;
        while (high_time > 0.01f)
        {

            Vector temp_pos = (eav * high_time) + proj_pos;
            temp_pos.z      = temp_pos.z - 0.5 * curr_grav * high_time * high_time;
            float curr_disp = temp_pos.DistToSqr(player_origin);
            if (curr_disp < high_displacement)
            {
                repeats           = 0;
                high_displacement = curr_disp;
                high_time -= multipler * sign;
                if (multipler > 0.05f)
                    multipler /= 2.0f;
            }
            else if (last_displacement < curr_disp)
            {
                ++repeats;
                sign *= -1;
            }
            else
            {
                repeats = 0;
            }
            if (repeats > 2)
            {
                proj_map.insert({ proj_ptr, Vector(0, 0, 0) });
                return;
            }
            last_displacement = curr_disp;
            if (high_displacement < 40000)
            {
                proj_map.insert({ proj_ptr, eav });
                return;
            }
            else
            {
                proj_map.insert({ proj_ptr, Vector(0, 0, 0) });
                return;
            }
        }
    }
}
float hitbox_size_proj(int class_id)
{
    if (class_id == CL_CLASS(CTFProjectile_Rocket))
        return 4.0f;
    if (class_id == CL_CLASS(CTFGrenadePipebombProjectile))
        return 4.0f;
    if (class_id == CL_CLASS(CTFProjectile_Flare))
        return 3.0f;
    if (class_id == CL_CLASS(CTFProjectile_EnergyBall))
        return 8.0f;
    if (class_is(class_id, CL_CLASS(CTFProjectile_GrapplingHook), CL_CLASS(CTFProjectile_HealingBolt), CL_CLASS(CTFProjectile_Arrow)))
        return 1.0f;
    if (class_id == CL_CLASS(CTFProjectile_SentryRocket))
        return 2.0f;
    if (class_id == CL_CLASS(CTFProjectile_Throwable))
        return 4.0f;
    return 1.0f;
}
static void dodgeProj_cm()
{
    if (!LOCAL_E->m_bAlivePlayer() || proj_map.empty() || !dodge_projectile)
        return;
    Vector player_pos = re::C_BaseEntity::GetAbsOrigin(RAW_ENT(LOCAL_E));
    for (const auto &[proj_ptr, proj_vec] : proj_map)
    {
        if (proj_vec.Length() < 0.1f)
        {
            if (CE_GOOD(proj_ptr))
                continue;
            else
                proj_map.erase(proj_ptr);
            continue;
        }
        if (CE_GOOD(proj_ptr))
        {
            float c_1   = ((float) (g_pLocalPlayer->v_Origin - re::C_BaseEntity::GetAbsOrigin(RAW_ENT(proj_ptr))).Dot(proj_vec)) / ((float) proj_vec.Dot(proj_vec));
            float ticks = TIME_TO_TICKS(c_1);
            if (ticks > 30)
                continue;
            Vector dist       = re::C_BaseEntity::GetAbsOrigin(RAW_ENT(proj_ptr));
            float proj_hitbox = hitbox_size_proj(proj_ptr->m_iClassID());
            trace_t trace;
            Ray_t ray;
            ray.Init(dist, player_pos, Vector(-proj_hitbox, -proj_hitbox, -proj_hitbox), Vector(proj_hitbox, proj_hitbox, proj_hitbox));
            g_ITrace->TraceRay(ray, MASK_SHOT_HULL, NULL, &trace);
            if (((IClientEntity *) trace.m_pEnt) == RAW_ENT(LOCAL_E) || trace.DidHit() || trace.endpos.DistToSqr(player_pos) < 1600)
            {
                Vector result = GetAimAtAngles(g_pLocalPlayer->v_Eye, re::C_BaseEntity::GetAbsOrigin(RAW_ENT(proj_ptr)), LOCAL_E) - g_pLocalPlayer->v_OrigViewangles;

                if (0 <= result.y)
                    yaw_amount = -90.0f;
                else
                    yaw_amount = 90.0f;
                was_hurt   = true;
                warp_dodge = true;
                proj_map.erase(proj_ptr);
            }
        }
        else
            proj_map.erase(proj_ptr);
    }
}

bool shouldWarp(bool check_amount)
{
    if (!g_IEngine->IsInGame())
        return false;
    auto nearest = hacks::tf2::NavBot::getNearestPlayerDistance();
    return ((warp_key && warp_key.isKeyDown()) || was_hurt || warp_dodge || (*warp_melee && nearest.second < 175.0f && hacks::tf2::NavBot::isVisible)) && (!check_amount || WarpTicks());
}

static int GetWarpUse()
{
    int max_extra = GetMaxWarpTicks();
    float pre = std::max(*speed, 0.05f);
    if (warp_dodge)
        pre = (float) max_extra;
    int use = (int) std::floor(pre);
    if (warp_amount_override)
        use = warp_amount_override;
    return std::clamp(use, 1, max_extra);
}

void PrepareShift()
{
    if (!hacks::tf2::tickbase::Active())
        return;
    hacks::tf2::tickbase::PollDoubletapKey();
    if (charge_key && charge_key.isKeyDown())
        hacks::tf2::tickbase::QueueRecharge();
    if (!enabled || !should_warp)
        return;
    if (hacks::tf2::tickbase::DoubletapEnabled() && hacks::tf2::tickbase::DoubletapKeyBound() && hacks::tf2::tickbase::DoubletapHeld())
        return;
    if (shouldWarp(true))
        hacks::tf2::tickbase::RequestWarp(GetWarpUse());
}

float approximateSpeedAtTick(int ticks_since_start, float initial_speed, float max_speed)
{
    float speed = ticks_since_start >= 20 ? max_speed : (ticks_since_start * (113.8f - 2.8f * ticks_since_start) + 1.0f);
    return std::min(max_speed * g_GlobalVars->interval_per_tick, initial_speed * g_GlobalVars->interval_per_tick + speed * g_GlobalVars->interval_per_tick);
}

int approximateTicksForDist(float distance, float initial_speed, int max_ticks)
{
    bool is_skullcutter = false;
    bool has_booties    = false;
    if (CE_GOOD(LOCAL_E) && LOCAL_E->m_bAlivePlayer() && CE_GOOD(LOCAL_W))
    {
        if (CE_INT(LOCAL_W, netvar.iItemDefinitionIndex) == 172)
            is_skullcutter = true;
        if (HasWeapon(LOCAL_E, 405) || HasWeapon(LOCAL_E, 608))
            has_booties = true;
    }
    float travelled_dist = 0.0f;
    for (int i = 0; i <= max_ticks; ++i)
    {
        travelled_dist += approximateSpeedAtTick(i, initial_speed, is_skullcutter ? (has_booties ? 701.0f : 637.0f) : 750.0f);
        if (travelled_dist >= distance)
            return i;
    }
    return -1;
}

static bool move_last_tick     = true;
static bool was_hurt_last_tick = false;
static int ground_ticks        = 0;
static std::vector<float> yaw_selections{ 90.0f, -90.0f };

enum charge_state
{
    ATTACK = 0,
    CHARGE,
    WARP,
    DONE
};

enum peek_state
{
    IDLE = 0,
    MOVE_TOWARDS,
    MOVE_BACK,
    STOP
};

charge_state current_state    = ATTACK;
peek_state current_peek_state = IDLE;
static int charge_at_start = 0;
static bool was_overridden = false;

static void CreateMove()
{
    const bool dt_on = hacks::tf2::tickbase::DoubletapEnabled();
    if (!enabled && !dt_on)
        return;
    if (CE_BAD(LOCAL_E) || !LOCAL_E->m_bAlivePlayer())
        return;
    if (CE_BAD(LOCAL_W))
        return;

    if (enabled && (bool) dodge_projectile && CE_GOOD(g_pLocalPlayer->entity))
        for (auto const &ent : entity_cache::valid_ents)
            if (ent->m_Type() == ENTITY_PROJECTILE && ent->m_bEnemy() && proj_map.find(ent) == proj_map.end())
                dodgeProj(ent);

    if (!shouldWarp(false) && !hacks::tf2::tickbase::shifting)
    {
        current_state      = ATTACK;
        current_peek_state = IDLE;

        if (charge_key && charge_key.isKeyDown())
        {
            hacks::tf2::tickbase::QueueRecharge();
            return;
        }

        Vector velocity{};
        velocity::EstimateAbsVelocity(RAW_ENT(LOCAL_E), velocity);

        if (!charge_in_jump)
        {
            if (CE_INT(LOCAL_E, netvar.iFlags) & FL_ONGROUND)
                ground_ticks++;
            else
                ground_ticks = 0;
        }

        bool button_block = (current_user_cmd->buttons & (IN_ATTACK | IN_ATTACK2));
        if (LOCAL_E->m_bAlivePlayer() && CE_GOOD(LOCAL_W) && LOCAL_W->m_iClassID() == CL_CLASS(CTFMinigun))
            button_block = current_user_cmd->buttons & IN_ATTACK;

        if ((ground_ticks > 1 || charge_in_jump) && (charge_no_input || velocity.IsZero()) && !HasCondition<TFCond_Charging>(LOCAL_E) && !current_user_cmd->forwardmove && !current_user_cmd->sidemove && !current_user_cmd->upmove && !(current_user_cmd->buttons & IN_JUMP) && !button_block)
        {
            if (!move_last_tick)
                hacks::tf2::tickbase::QueueRecharge();
            move_last_tick = false;
            return;
        }
        else if (charge_passively && (charge_in_jump || ground_ticks > 1))
        {
            bool passive_block = (current_user_cmd->buttons & (IN_ATTACK | IN_ATTACK2));
            if (LOCAL_W->m_iClassID() == CL_CLASS(CTFMinigun))
                passive_block = current_user_cmd->buttons & IN_ATTACK;

            if (!passive_block)
            {
                if (*warp_movement_ratio > 0 && !(tickcount % *warp_movement_ratio))
                    hacks::tf2::tickbase::QueueRecharge();
                move_last_tick = true;
            }
        }
    }
    else if (hacks::tf2::tickbase::in_doubletap || !enabled)
        return;
    else if (was_hurt)
    {
        static float yaw = 0.0f;
        if (!was_hurt_last_tick)
        {
            yaw = 0.0f;
            if (yaw_selections.empty())
                return;
            if (warp_dodge)
                yaw = yaw_amount;
            else
                yaw = yaw_selections[UniformRandomInt(0, yaw_selections.size() - 1)];
        }
        float actual_yaw = DEG2RAD(yaw);
        current_user_cmd->forwardmove = cos(actual_yaw) * 450.0f;
        current_user_cmd->sidemove    = -sin(actual_yaw) * 450.0f;
    }
    else if (warp_demoknight)
    {
        switch (current_state)
        {
        case ATTACK:
        {
            if (WarpTicks() < floor(GetMaxWarpTicks() / 2.0f))
                break;
            float charge_meter = re::CTFPlayerShared::GetChargeMeter(re::CTFPlayerShared::GetPlayerShared(RAW_ENT(LOCAL_E)));

            if (charge_meter == 100.0f)
            {
                std::pair<CachedEntity *, float> result{ nullptr, FLT_MAX };

                for (auto const &ent : entity_cache::player_cache)
                {

                    if (CE_BAD(ent) || !ent->m_bAlivePlayer() || !ent->m_bEnemy() || !player_tools::shouldTarget(ent))
                        continue;
                    if (!ent->hitboxes.GetHitbox(2))
                        continue;
                    float FOVScore = GetFov(g_pLocalPlayer->v_OrigViewangles, g_pLocalPlayer->v_Eye, ent->hitboxes.GetHitbox(spine_1)->center);
                    if (FOVScore < result.second)
                    {
                        result.second = FOVScore;
                        result.first  = ent;
                    }
                }
                if (result.first)
                {
                    float distance = LOCAL_E->m_vecOrigin().DistTo(result.first->m_vecOrigin());

                    Vector vel;
                    velocity::EstimateAbsVelocity(RAW_ENT(LOCAL_E), vel);
                    int charge_ticks = approximateTicksForDist(distance - 40.0f, vel.Length(), WarpTicks() + 11);

                    if (charge_ticks <= 0)
                        charge_ticks = approximateTicksForDist(distance - 128.0f, vel.Length(), WarpTicks() + 11);
                    if (charge_ticks <= 0)
                    {
                        charge_ticks = WarpTicks();
                        should_melee = false;
                    }
                    else
                    {
                        charge_ticks = std::clamp(charge_ticks, 0, WarpTicks());
                        charge_ticks -= std::ceil(charge_ticks / *speed);
                        should_melee = true;
                    }
                    warp_amount_override = charge_ticks;
                    was_overridden       = true;
                }
                else
                {
                    should_melee   = false;
                    was_overridden = false;
                }

                criticals::force_crit_this_tick = true;
                if (should_melee)
                    current_user_cmd->buttons |= IN_ATTACK;
                current_state = CHARGE;
            }
            else
            {
                was_overridden = false;
                current_state  = WARP;
            }

            should_warp = false;
            break;
        }
        case CHARGE:
        {
            current_user_cmd->buttons |= IN_ATTACK2;
            current_state = WARP;
            should_warp   = false;
            break;
        }
        case WARP:
        {
            should_warp = true;
            if ((was_overridden && !warp_amount_override) || !WarpTicks())
            {
                should_warp   = false;
                current_state = DONE;
            }
            break;
        }
        case DONE:
        {
            should_warp = false;
            break;
        }
        default:
            break;
        }
    }
    else if (warp_peek)
    {
        switch (current_peek_state)
        {
        case IDLE:
        {
            charge_at_start = WarpTicks();

            Vector vel;
            velocity::EstimateAbsVelocity(RAW_ENT(LOCAL_E), vel);

            if (CE_INT(LOCAL_E, netvar.iFlags) & FL_ONGROUND && !vel.IsZero(1.0f) && current_user_cmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT))
                current_peek_state = MOVE_TOWARDS;
            else
            {
                should_warp = false;
                break;
            }

            [[fallthrough]];
        }
        case MOVE_TOWARDS:
        {
            if (WarpTicks() <= charge_at_start * (2.0f / 3.0f))
                current_peek_state = MOVE_BACK;
            break;
        }
        case MOVE_BACK:
        {
            if (WarpTicks())
            {
                current_user_cmd->forwardmove *= -1.0f;
                current_user_cmd->sidemove *= -1.0f;
                break;
            }
            else
                current_peek_state = STOP;

            [[fallthrough]];
        }
        case STOP:
        {
            current_user_cmd->forwardmove = 0.0f;
            current_user_cmd->sidemove    = 0.0f;
            break;
        }
        default:
            break;
        }
    }
    was_hurt_last_tick = was_hurt;
    if (!WarpTicks())
    {
        was_hurt   = false;
        warp_dodge = false;
        warp_amount_override = 0;
    }
}

void CL_SendMove_hook()
{
    if (hacks::tf2::tickbase::WriteShiftMove())
        return;
    auto orig = CL_SendMove_t(cl_sendmove_detour.GetOriginalFunc());
    if (orig)
        orig();
}

void SendNetMessage(INetMessage &)
{
}

#if ENABLE_VISUALS
void Draw()
{
    if (!enabled && !hacks::tf2::tickbase::Active())
        return;
    if (!draw && !draw_bar)
        return;
    if (!g_IEngine->IsInGame())
        return;
    if (CE_BAD(LOCAL_E))
        return;
    if (!LOCAL_E->m_bAlivePlayer())
        return;

    int amount = WarpTicks();
    int max_t  = GetMaxWarpTicks();

    if (draw)
    {
        rgba_t color = colors::orange;
        if (amount == 0)
            color = colors::FromRGBA8(128.0f, 128.0f, 128.0f, 255.0f);
        else if (max_t == amount)
            color = colors::green;
        AddWarpString("Shiftable ticks: " + std::to_string(amount), color);
    }

    if (draw_bar)
    {
        float charge_percent = max_t ? (float) amount / (float) max_t : 0.0f;
        static rgba_t background_color = colors::FromRGBA8(96, 96, 96, 150);
        float bar_bg_x_size            = *size * 2.0f;
        float bar_bg_y_size            = *size / 5.0f;
        draw::Rectangle(*bar_x - 5.0f, *bar_y - 5.0f, bar_bg_x_size + 10.0f, bar_bg_y_size + 10.0f, background_color);
        rgba_t color_bar = colors::orange;
        if (max_t == amount)
            color_bar = colors::green;
        color_bar.a = 100 / 255.0f;
        draw::Rectangle(*bar_x, *bar_y, *size * 2.0f * charge_percent, *size / 5.0f, color_bar);
    }

    DrawWarpStrings();
}
#endif

void LevelShutdown()
{
    hacks::tf2::tickbase::Reset();
}

class WarpHurtListener : public IGameEventListener2
{
public:
    virtual void FireGameEvent(IGameEvent *event)
    {
        if (!isHackActive() || !enabled || !warp_on_damage)
            return;
        if (!WarpTicks())
            return;
        int victim       = event->GetInt("userid");
        int attacker     = event->GetInt("attacker");
        int attacker_idx = GetPlayerForUserID(attacker);
        int victim_idx   = GetPlayerForUserID(victim);
        player_info_s kinfo{};
        player_info_s vinfo{};

        if (IDX_BAD(attacker_idx) || IDX_BAD(victim_idx) || !GetPlayerInfo(attacker_idx, &vinfo) || !GetPlayerInfo(victim_idx, &kinfo))
            return;
        if (victim_idx != g_pLocalPlayer->entity_idx)
            return;

        CachedEntity *att = ENTITY(attacker_idx);

        if (CE_BAD(LOCAL_E) || !LOCAL_E->m_bAlivePlayer())
            return;
        if (CE_VALID(att) && att->m_bAlivePlayer() && GetWeaponMode(att) == weapon_projectile)
            return;

        was_hurt = true;
    }
};

static WarpHurtListener listener;

void rvarCallback(settings::VariableBase<bool> &, bool)
{
    yaw_selections.clear();
    if (warp_forward)
        yaw_selections.push_back(0.0f);
    if (warp_backwards)
        yaw_selections.push_back(-180.0f);
    if (warp_left)
        yaw_selections.push_back(-90.0f);
    if (warp_right)
        yaw_selections.push_back(90.0f);
}

static InitRoutine init(
    []()
    {
        EC::Register(EC::LevelShutdown, LevelShutdown, "warp_levelshutdown");
        EC::Register(EC::CreateMove, CreateMove, "warp_createmove", EC::very_late);
        EC::Register(EC::CreateMoveWarp, CreateMove, "warp_createmovew", EC::very_late);
        EC::Register(EC::CreateMove, dodgeProj_cm, "warp_dodgeproj", EC::average);
        g_IEventManager2->AddListener(&listener, "player_hurt", false);
        EC::Register(
            EC::Shutdown,
            []()
            {
                g_IEventManager2->RemoveListener(&listener);
            },
            "warp_shutdown");
        warp_forward.installChangeCallback(rvarCallback);
        warp_backwards.installChangeCallback(rvarCallback);
        warp_left.installChangeCallback(rvarCallback);
        warp_right.installChangeCallback(rvarCallback);

#if ENABLE_VISUALS
        EC::Register(EC::Draw, Draw, "warp_draw");
#endif
    });
} // namespace hacks::tf2::warp
