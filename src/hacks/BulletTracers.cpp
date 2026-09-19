/*
 * Credits  To Unknown For most of this
 */
#include "common.hpp"
#include "DetourHook.hpp"
#include "boost/unordered/unordered_flat_map.hpp"
namespace hacks::tf2::bullettracers
{

static settings::Boolean enable{ "visual.bullet-tracers.enable", "false" };
static settings::Boolean sniper_only{ "visual.bullet-tracers.sniper-only", "false" };
static settings::Boolean local_only{ "visual.bullet-tracers.local-only", "false" };
static settings::Boolean draw_local{ "visual.bullet-tracers.draw-local", "true" };
static settings::Int team_mode{ "visual.bullet-tracers.teammode", "0" };
static settings::Boolean one_trace_only{ "visual.bullet-tracers.one-trace-only", "false" };
static settings::Boolean sentry_tracers{ "visual.bullet-tracers.sentry", "true" };
static settings::Int tracer_type{ "visual.bullet-tracers.type", "0" };

class CEffectData
{
public:
    Vector m_vStart{ 0 };
    Vector m_vEnd{ 0 };
    Vector m_vNormal{ 0 };
    QAngle m_vAngles{ 0, 0, 0 };
    int m_fFlags{ 0 };
    CBaseHandle m_hEntity{ -1, 0 };
    char pad0[28]{ 0 };
    int m_iEffectId{ 0 };
    char pad1[44]{ 0 };
};

typedef const char *(*GetParticleSystemNameFromIndex_t)(int);
typedef void (*ParticleEffectCallback_t)(CEffectData *);
typedef void (*DispatchEffect_t)(const char *, const CEffectData &);
typedef void (*FX_Tracer_t)(Vector &, Vector &, int, bool);

static DispatchEffect_t DispatchEffect_fn;
static DetourHook particle_name_detour;
static DetourHook particle_cb_detour;
static DetourHook fx_tracer_detour;
static thread_local CEffectData *tls_effect;

const char *AppropiateBeam(int team)
{
    switch (*tracer_type)
    {
    case 0:
        if (team == TEAM_RED)
            return "dxhr_sniper_rail_red";
        else
            return "dxhr_sniper_rail_blue";
    case 1:
        if (re::CTFPlayerShared::IsCritBoosted(re::CTFPlayerShared::GetPlayerShared(RAW_ENT(LOCAL_E))))
        {
            if (team == TEAM_RED)
                return "bullet_tracer_raygun_red_crit";
            else
                return "bullet_tracer_raygun_blue_crit";
        }
        else
        {
            if (team == TEAM_RED)
                return "bullet_tracer_raygun_red";
            else
                return "bullet_tracer_raygun_blue";
        }
    case 2:
        if (team == TEAM_RED)
            return "dxhr_lightningball_hit_zap_red";
        else
            return "dxhr_lightningball_hit_zap_blue";
    case 3:
        return "merasmus_zap_beam02";
    case 4:
        return "merasmus_zap";
    }
    return "dxhr_sniper_rail_red";
}

const char *ReplaceParticleName(CEffectData &data, const char *wantedEffect)
{
    auto player = g_IEntityList->GetClientEntityFromHandle(data.m_hEntity);
    if (!enable || !player || EntIndex(player) == -1)
        return wantedEffect;

    if (!strstr(wantedEffect, "bullet_") && data.m_iEffectId != 0xDEADCA7)
        return wantedEffect;

    ClientClass *player_cc = EntClientClass(player);
    if (*sentry_tracers && player_cc && player_cc->m_ClassID == CL_CLASS(CObjectSentrygun))
        return AppropiateBeam(NET_INT(player, netvar.iTeamNum));

    auto weapon = ENTITY(HandleToIDX(NET_INT(player, netvar.hActiveWeapon)));

    if (!weapon || weapon->m_IDX == 0)
        return wantedEffect;

    bool isLocal = EntIndex(player) == g_pLocalPlayer->entity_idx;

    if (sniper_only)
        if (weapon->m_iClassID() == CL_CLASS(CTFSniperRifle) || weapon->m_iClassID() == CL_CLASS(CTFSniperRifleDecap))
            return wantedEffect;
    if (!draw_local)
        if (isLocal)
            return wantedEffect;
    if (local_only)
        if (!isLocal)
            return wantedEffect;
    if (one_trace_only && data.m_iEffectId != 0xDEADCA7)
    {
        data.m_fFlags  = 1;
        data.m_hEntity = -1;
        return wantedEffect;
    }

    int team = NET_INT(player, netvar.iTeamNum);

    if (!isLocal)
        switch (*team_mode)
        {
        case 1:
            if (team == LOCAL_E->m_iTeam())
                return wantedEffect;
            break;
        case 2:
            if (team != LOCAL_E->m_iTeam())
                return wantedEffect;
            break;
        }
    return AppropiateBeam(team);
}

const char *GetParticleSystemNameFromIndex_hook(int idx)
{
    auto orig = (GetParticleSystemNameFromIndex_t) particle_name_detour.GetOriginalFunc();
    if (!orig)
        return "error";
    const char *wanted = orig(idx);
    if (!tls_effect)
        return wanted;
    return ReplaceParticleName(*tls_effect, wanted);
}

void ParticleEffectCallback_hook(CEffectData *data)
{
    auto orig = (ParticleEffectCallback_t) particle_cb_detour.GetOriginalFunc();
    if (!orig)
        return;
    tls_effect = data;
    orig(data);
    tls_effect = nullptr;
}

void TryDispatchPlayerTracer(IClientEntity *this_)
{
    if (!this_ || !enable || !DispatchEffect_fn)
        return;
    if (CE_BAD(LOCAL_E) || IDX_BAD(EntIndex(this_)) || CE_BAD(ENTITY(EntIndex(this_))))
        return;

    bool isLocal = EntIndex(this_) == g_pLocalPlayer->entity_idx;
    auto weapon  = ENTITY(HandleToIDX(NET_INT(this_, netvar.hActiveWeapon)));
    if (CE_BAD(weapon))
        return;

    if (sniper_only)
        if (weapon->m_iClassID() == CL_CLASS(CTFSniperRifle) || weapon->m_iClassID() == CL_CLASS(CTFSniperRifleDecap))
            return;
    if (local_only)
        if (!isLocal)
            return;
    if (!draw_local)
        if (isLocal)
            return;

    int team = NET_INT(this_, netvar.iTeamNum);

    if (!isLocal)
        switch (*team_mode)
        {
        case 1:
            if (team == LOCAL_E->m_iTeam())
                return;
            break;
        case 2:
            if (team != LOCAL_E->m_iTeam())
                return;
            break;
        }

    CEffectData data;
    data.m_hEntity = EntRefEHandle(this_);

    int attachment = EntLookupAttachment(RAW_ENT(weapon), "muzzle");
    QAngle muzzle_ang;
    EntGetAttachment(RAW_ENT(weapon), attachment, data.m_vStart, muzzle_ang);

    if (isLocal && g_pLocalPlayer->bZoomed)
        data.m_vStart = g_pLocalPlayer->v_Eye;

    {
        auto cent = ENTITY(EntIndex(this_));
        if (CE_BAD(cent) || !cent->hitboxes.GetHitbox(0))
            return;
        Vector eyePos = cent->hitboxes.GetHitbox(0)->center;
        trace::filter_default.SetSelf(this_);
        trace_t trace;
        Ray_t ray;
        QAngle angle = *(QAngle *) &NET_VECTOR(this_, netvar.angEyeAngles);
        if (isLocal)
        {
            angle  = VectorToQAngle(current_user_cmd->viewangles);
            eyePos = g_pLocalPlayer->v_Eye;
        }
        Vector forward;
        AngleVectors2(angle, &forward);
        forward *= 8192.0f;
        forward += eyePos;
        ray.Init(eyePos, forward);
        g_ITrace->TraceRay(ray, MASK_SHOT, &trace::filter_default, &trace);
        data.m_vEnd = trace.endpos;
    }
    data.m_iEffectId = 0xDEADCA7;
    DispatchEffect_fn("ParticleEffect", data);
}

boost::unordered_flat_map<u_int16_t, char> SentryTracerParity;
void FX_Tracer_hook(Vector &start, Vector &end, int velocity, bool makeWhiz)
{
    auto orig = (FX_Tracer_t) fx_tracer_detour.GetOriginalFunc();
    CEffectData &data = *reinterpret_cast<CEffectData *>(&end);
    if (!sentry_tracers || !enable)
    {
        if (orig)
            orig(start, data.m_vStart, velocity, makeWhiz);
        return;
    }
    auto sentry            = g_IEntityList->GetClientEntityFromHandle(data.m_hEntity);
    ClientClass *sentry_cc = sentry ? EntClientClass(sentry) : nullptr;
    if (!sentry || EntIndex(sentry) == -1 || !sentry_cc || sentry_cc->m_ClassID != CL_CLASS(CObjectSentrygun))
        return;
    if (!DispatchEffect_fn)
        return;
    int muzzle   = 4;
    int muzzle_l = 1;
    int muzzle_r = 2;
    CEffectData dataTracer;
    if (NET_INT(sentry, netvar.iUpgradeLevel) > 1)
    {
        u_int16_t index = EntIndex(sentry);
        auto &parity    = SentryTracerParity[index];
        parity          = !parity;
        QAngle muzzle_ang;
        EntGetAttachment(sentry, parity ? muzzle_l : muzzle_r, dataTracer.m_vStart, muzzle_ang);
    }
    else
    {
        QAngle muzzle_ang;
        EntGetAttachment(sentry, muzzle, dataTracer.m_vStart, muzzle_ang);
    }

    dataTracer.m_hEntity   = EntRefEHandle(sentry);
    dataTracer.m_vEnd      = data.m_vStart;
    dataTracer.m_iEffectId = 0xDEADCA7;
    DispatchEffect_fn("ParticleEffect", dataTracer);
}

class BulletImpactListener : public IGameEventListener2
{
    void FireGameEvent(IGameEvent *event) override
    {
        if (!enable || !event)
            return;
        int idx = GetPlayerForUserID(event->GetInt("userid"));
        auto *player = g_IEntityList->GetClientEntity(idx);
        if (player)
            TryDispatchPlayerTracer(player);
    }
};

static BulletImpactListener impact_listener;
static bool listening;

static void set_listening(bool on)
{
    if (!g_IEventManager2)
        return;
    if (on && !listening)
    {
        g_IEventManager2->AddListener(&impact_listener, "bullet_impact", false);
        listening = true;
    }
    else if (!on && listening)
    {
        g_IEventManager2->RemoveListener(&impact_listener);
        listening = false;
    }
}

static InitRoutine init(
    []()
    {
        auto name_addr = gSignatures.GetClientSignature(sigs::get_particle_system_name_from_index);
        auto cb_addr   = gSignatures.GetClientSignature(sigs::particle_effect_callback);
        auto fx_addr   = gSignatures.GetClientSignature(sigs::fx_tracer);
        auto disp_addr = gSignatures.GetClientSignature(sigs::dispatch_effect);
        DispatchEffect_fn = (DispatchEffect_t) disp_addr;
        if (name_addr)
            particle_name_detour.Init(name_addr, (void *) GetParticleSystemNameFromIndex_hook);
        if (cb_addr)
            particle_cb_detour.Init(cb_addr, (void *) ParticleEffectCallback_hook);
        if (fx_addr)
            fx_tracer_detour.Init(fx_addr, (void *) FX_Tracer_hook);

        set_listening(*enable);
        enable.installChangeCallback([](settings::VariableBase<bool> &, bool after) { set_listening(after); });
        EC::Register(
            EC::Shutdown,
            []()
            {
                set_listening(false);
                particle_name_detour.Shutdown();
                particle_cb_detour.Shutdown();
                fx_tracer_detour.Shutdown();
            },
            "shutdown_bullettrace");
    });
} // namespace hacks::tf2::bullettracers
