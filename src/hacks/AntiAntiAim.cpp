/*
 * Created on 29.07.18.
 */

#include "common.hpp"
#include "hacks/AntiAntiAim.hpp"
#include "sdk/dt_recv_redef.h"
#include "velocity.hpp"

namespace hacks::shared::anti_anti_aim
{
static settings::Boolean enable{ "anti-anti-aim.enable", "false" };
static settings::Boolean resolve_pitch{ "anti-anti-aim.pitch", "true" };
static settings::Boolean resolve_yaw{ "anti-anti-aim.yaw", "true" };
static settings::Boolean brute_on_miss{ "anti-anti-aim.brute-on-miss", "true" };
static settings::Boolean use_sniper_dot{ "anti-anti-aim.sniper-dot", "true" };
static settings::Boolean use_attacker_hint{ "anti-anti-aim.attacker-hint", "true" };
static settings::Boolean debug{ "anti-anti-aim.debug.enable", "false" };

boost::unordered_flat_map<unsigned, brutedata> resolver_map;
std::array<CachedEntity *, 32> sniperdot_array;

static void pushYawHistory(brutedata &data, float yaw)
{
    yaw                              = AngleNormalizeTF(yaw);
    data.yaw_history[data.yaw_hist_index] = yaw;
    data.yaw_hist_index              = (data.yaw_hist_index + 1) % 8;
    if (data.yaw_hist_count < 8)
        data.yaw_hist_count++;
}

static float yawDelta(float a, float b)
{
    return fabsf(AngleNormalizeTF(a - b));
}

enum yaw_class_t
{
    YCLASS_STATIC,
    YCLASS_JITTER,
    YCLASS_SPIN
};

static yaw_class_t classifyYaw(const brutedata &data)
{
    if (data.yaw_hist_count < 3)
        return YCLASS_STATIC;

    float max_step = 0.0f;
    int flips      = 0;
    for (int i = 0; i < data.yaw_hist_count - 1; ++i)
    {
        int newer = (data.yaw_hist_index - 1 - i + 16) % 8;
        int older = (data.yaw_hist_index - 2 - i + 16) % 8;
        float step = yawDelta(data.yaw_history[newer], data.yaw_history[older]);
        if (step > max_step)
            max_step = step;
        if (step > 60.0f)
            flips++;
    }

    if (flips >= 2 && max_step >= 70.0f)
        return YCLASS_JITTER;
    if (max_step >= 20.0f && flips >= 3)
        return YCLASS_SPIN;
    return YCLASS_STATIC;
}

static inline void modifyAngles()
{
    for (auto const &player : entity_cache::player_cache)
    {
        if (CE_BAD(player) || !player->player_info || !player->m_bAlivePlayer() || !player->m_bEnemy() || !player->player_info->friendsID)
            continue;
        auto &data  = resolver_map[player->player_info->friendsID];
        auto &angle = CE_VECTOR(player, netvar.m_angEyeAngles);
        angle.x     = data.new_angle.x;
        angle.y     = data.new_angle.y;
    }
}

static inline void CreateMove()
{
    sniperdot_array.fill(0);
    for (auto &dot_ent : entity_cache::valid_ents)
    {
        if (dot_ent->m_iClassID() != CL_CLASS(CSniperDot))
            continue;
        auto ent_idx = HandleToIDX(CE_INT(dot_ent, netvar.m_hOwnerEntity));
        if (IDX_BAD(ent_idx) || ent_idx <= 0 || ent_idx > (int) sniperdot_array.size())
            continue;
        sniperdot_array[ent_idx - 1] = dot_ent;
    }
}

void frameStageNotify(ClientFrameStage_t stage)
{
#if !ENABLE_TEXTMODE
    if (!enable || !g_IEngine->IsInGame())
        return;
    if (stage == FRAME_NET_UPDATE_POSTDATAUPDATE_START)
        modifyAngles();
#endif
}

static const float yaw_resolves[] = { 0.0f, 180.0f, 90.0f, -90.0f, 45.0f, -45.0f, 135.0f, -135.0f, 65.0f, -65.0f };

static float resolveAngleYaw(float angle, brutedata &brute, CachedEntity *ent)
{
    brute.original_angle.y = angle;
    angle                  = AngleNormalizeTF(angle);
    pushYawHistory(brute, angle);

    if (!resolve_yaw)
    {
        brute.new_angle.y = angle;
        return angle;
    }

    if (use_attacker_hint && brute.has_attacker_hint && brute.brutenum == 0)
    {
        brute.new_angle.y = AngleNormalizeTF(brute.attacker_hint_yaw);
        return brute.new_angle.y;
    }

    const yaw_class_t kind = classifyYaw(brute);
    float resolved         = angle;

    if (kind == YCLASS_SPIN)
    {
        int entry = brute.brutenum % 2;
        resolved  = AngleNormalizeTF(angle + (entry ? 0.0f : 180.0f));
    }
    else if (kind == YCLASS_JITTER)
    {
        float a = angle;
        float b = angle;
        if (brute.yaw_hist_count >= 2)
        {
            int last = (brute.yaw_hist_index + 7) % 8;
            int prev = (brute.yaw_hist_index + 6) % 8;
            a        = brute.yaw_history[last];
            b        = brute.yaw_history[prev];
        }
        resolved = (brute.brutenum % 2) ? a : b;
    }
    else
    {
        int extras = 0;
        float extra[2]{};
        if (ent && CE_GOOD(ent) && velocity::EstimateAbsVelocity)
        {
            Vector vel;
            velocity::EstimateAbsVelocity(RAW_ENT(ent), vel);
            if (vel.Length2D() > 50.0f)
            {
                Vector move_ang;
                Vector fwd = vel;
                VectorAngles(fwd, move_ang);
                extra[extras++] = AngleNormalizeTF(move_ang.y - angle);
                extra[extras++] = AngleNormalizeTF(move_ang.y + 180.0f - angle);
            }
        }

        const int table_n = int(sizeof(yaw_resolves) / sizeof(yaw_resolves[0]));
        int stage         = brute.brutenum;
        if (stage < extras)
            resolved = AngleNormalizeTF(angle + extra[stage]);
        else
        {
            int entry = (stage - extras) % table_n;
            resolved  = AngleNormalizeTF(angle + yaw_resolves[entry]);
        }
    }

    brute.new_angle.y = resolved;
    return resolved;
}

static CachedEntity *sniperDotFor(CachedEntity *ent)
{
    if (!use_sniper_dot || !ent)
        return nullptr;

    auto weapon_id = HandleToIDX(CE_INT(ent, netvar.hActiveWeapon));
    if (!IDX_GOOD(weapon_id))
        return nullptr;

    auto weapon_ent = ENTITY(weapon_id);
    if (CE_BAD(weapon_ent))
        return nullptr;
    if (weapon_ent->m_iClassID() != CL_CLASS(CTFSniperRifle) && weapon_ent->m_iClassID() != CL_CLASS(CTFSniperRifleDecap) && weapon_ent->m_iClassID() != CL_CLASS(CTFSniperRifleClassic))
        return nullptr;

    if (ent->m_IDX < 1 || ent->m_IDX > (int) sniperdot_array.size())
        return nullptr;

    CachedEntity *sniper_dot = sniperdot_array[ent->m_IDX - 1];
    if (CE_BAD(sniper_dot) || sniper_dot->m_iClassID() != CL_CLASS(CSniperDot))
        return nullptr;
    return sniper_dot;
}

static float resolveAnglePitch(float angle, brutedata &brute, CachedEntity *ent)
{
    brute.original_angle.x = angle;

    if (CachedEntity *sniper_dot = sniperDotFor(ent))
    {
        auto dot_origin = sniper_dot->m_vecOrigin();
        auto eye_origin = re::C_BasePlayer::GetEyePosition(RAW_ENT(ent));
        Vector diff     = dot_origin - eye_origin;
        Vector angles;
        VectorAngles(diff, angles);
        brute.new_angle.x = angles.x;
        return angles.x;
    }

    if (!resolve_pitch)
    {
        brute.new_angle.x = angle;
        return angle;
    }

    float resolved = AngleNormalizePitchVisual(angle);

    if (brute.brutenum > 0 && (brute.brutenum % 3) == 2)
        resolved = (resolved >= 0.0f) ? -89.0f : 89.0f;

    brute.new_angle.x = resolved;
    return resolved;
}

void increaseBruteNum(int idx)
{
    if (!brute_on_miss)
        return;
    auto ent = ENTITY(idx);
    if (CE_BAD(ent) || !ent->player_info || !ent->player_info->friendsID)
        return;
    auto &data = hacks::shared::anti_anti_aim::resolver_map[ent->player_info->friendsID];
    if (data.hits_in_a_row >= 4)
        data.hits_in_a_row = 2;
    else if (data.hits_in_a_row >= 2)
        data.hits_in_a_row = 0;
    else
    {
        data.brutenum++;
        if (debug)
            logging::Info("AAA: Brutenum for entity %i increased to %i", idx, data.brutenum);
        data.hits_in_a_row = 0;
        auto &angle        = CE_VECTOR(ent, netvar.m_angEyeAngles);
        angle.x            = resolveAnglePitch(data.original_angle.x, data, ent);
        angle.y            = resolveAngleYaw(data.original_angle.y, data, ent);
        data.new_angle.x   = angle.x;
        data.new_angle.y   = angle.y;
    }
}

static void pitchHook(const CRecvProxyData *pData, void *pStruct, void *pOut)
{
    float flPitch      = pData->m_Value.m_Float;
    float *flPitch_out = (float *) pOut;

    if (!enable)
    {
        *flPitch_out = flPitch;
        return;
    }

    auto client_ent   = (IClientEntity *) (pStruct);
    CachedEntity *ent = ENTITY(EntIndex(client_ent));
    if (CE_GOOD(ent) && ent->player_info)
        *flPitch_out = resolveAnglePitch(flPitch, resolver_map[ent->player_info->friendsID], ent);
    else
        *flPitch_out = flPitch;
}

static void yawHook(const CRecvProxyData *pData, void *pStruct, void *pOut)
{
    float flYaw      = pData->m_Value.m_Float;
    float *flYaw_out = (float *) pOut;

    if (!enable)
    {
        *flYaw_out = flYaw;
        return;
    }

    auto client_ent   = (IClientEntity *) (pStruct);
    CachedEntity *ent = ENTITY(EntIndex(client_ent));
    if (CE_GOOD(ent) && ent->player_info)
        *flYaw_out = resolveAngleYaw(flYaw, resolver_map[ent->player_info->friendsID], ent);
    else
        *flYaw_out = flYaw;
}

static RecvVarProxyFn *original_ptrX;
static RecvVarProxyFn original_ProxyFnX;
static RecvVarProxyFn *original_ptrY;
static RecvVarProxyFn original_ProxyFnY;

static void hook()
{
    auto pClass = g_IBaseClient->GetAllClasses();
    while (pClass)
    {
        const char *pszName = pClass->m_pRecvTable->m_pNetTableName;
        if (!strcmp(pszName, "DT_TFPlayer"))
        {
            for (int i = 0; i < pClass->m_pRecvTable->m_nProps; ++i)
            {
                RecvPropRedef *pProp1 = (RecvPropRedef *) &(pClass->m_pRecvTable->m_pProps[i]);
                if (!pProp1)
                    continue;
                const char *pszName2 = pProp1->m_pVarName;
                if (!strcmp(pszName2, "tfnonlocaldata"))
                    for (int j = 0; j < pProp1->m_pDataTable->m_nProps; j++)
                    {
                        RecvPropRedef *pProp2 = (RecvPropRedef *) &(pProp1->m_pDataTable->m_pProps[j]);
                        if (!pProp2)
                            continue;
                        const char *name = pProp2->m_pVarName;

                        if (!strcmp(name, "m_angEyeAngles[0]"))
                        {
                            original_ptrX     = &pProp2->m_ProxyFn;
                            original_ProxyFnX = pProp2->m_ProxyFn;
                            pProp2->m_ProxyFn = pitchHook;
                        }
                        if (!strcmp(name, "m_angEyeAngles[1]"))
                        {
                            original_ptrY     = &pProp2->m_ProxyFn;
                            original_ProxyFnY = pProp2->m_ProxyFn;
                            pProp2->m_ProxyFn = yawHook;
                        }
                    }
            }
        }
        pClass = pClass->m_pNext;
    }
}

static void shutdown()
{
    *original_ptrX = original_ProxyFnX;
    *original_ptrY = original_ProxyFnY;
}

class HurtListener : public IGameEventListener
{
public:
    void FireGameEvent(KeyValues *event) override
    {
        if (!enable || !use_attacker_hint || !event)
            return;
        const char *name = event->GetName();
        if (!name || strcmp("player_hurt", name))
            return;

        const int local = g_IEngine->GetLocalPlayer();
        const int victim  = GetPlayerForUserID(event->GetInt("userid"));
        const int attacker = GetPlayerForUserID(event->GetInt("attacker"));
        if (victim != local || attacker == local || attacker <= 0)
            return;

        auto ent = ENTITY(attacker);
        if (CE_BAD(ent) || !ent->player_info || !ent->player_info->friendsID)
            return;

        Vector to_us = g_pLocalPlayer->v_Eye - re::C_BasePlayer::GetEyePosition(RAW_ENT(ent));
        Vector ang;
        VectorAngles(to_us, ang);
        auto &data            = resolver_map[ent->player_info->friendsID];
        data.has_attacker_hint = true;
        data.attacker_hint_yaw = ang.y;
        data.brutenum          = 0;
        if (debug)
            logging::Info("AAA: attacker hint yaw %.1f for %i", ang.y, attacker);
    }
};

static HurtListener &hurtListener()
{
    static HurtListener l{};
    return l;
}

static InitRoutine init(
    []()
    {
        hook();
        g_IGameEventManager->AddListener(&hurtListener(), false);
        EC::Register(EC::Shutdown, shutdown, "antiantiaim_shutdown");
        EC::Register(
            EC::Shutdown, []() { g_IGameEventManager->RemoveListener(&hurtListener()); }, "antiantiaim_hurt_shutdown");
        EC::Register(EC::CreateMove, CreateMove, "cm_antiantiaim");
        EC::Register(EC::CreateMoveWarp, CreateMove, "cmw_antiantiaim");
#if ENABLE_TEXTMODE
        EC::Register(EC::CreateMove, modifyAngles, "cm_textmodeantiantiaim");
        EC::Register(EC::CreateMoveWarp, modifyAngles, "cmw_textmodeantiantiaim");
#endif
    });
} // namespace hacks::shared::anti_anti_aim
