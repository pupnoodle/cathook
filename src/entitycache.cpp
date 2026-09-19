#include "common.hpp"
#include "soundcache.hpp"

EntityType ClassifyEntity(int classid)
{
    if (!classid)
        return ENTITY_GENERIC;
    if (classid == CL_CLASS(CTFPlayer))
        return ENTITY_PLAYER;
    if (class_is(classid, CL_CLASS(CTFGrenadePipebombProjectile), CL_CLASS(CTFProjectile_Cleaver), CL_CLASS(CTFProjectile_Jar), CL_CLASS(CTFProjectile_JarMilk), CL_CLASS(CTFProjectile_Arrow), CL_CLASS(CTFProjectile_EnergyBall), CL_CLASS(CTFProjectile_EnergyRing), CL_CLASS(CTFProjectile_GrapplingHook), CL_CLASS(CTFProjectile_HealingBolt), CL_CLASS(CTFProjectile_Rocket), CL_CLASS(CTFProjectile_SentryRocket), CL_CLASS(CTFProjectile_BallOfFire), CL_CLASS(CTFProjectile_Flare)))
        return ENTITY_PROJECTILE;
    if (class_is(classid, CL_CLASS(CObjectTeleporter), CL_CLASS(CObjectSentrygun), CL_CLASS(CObjectDispenser)))
        return ENTITY_BUILDING;
    if (class_is(classid, CL_CLASS(CTFTankBoss), CL_CLASS(CMerasmus), CL_CLASS(CMerasmusDancer), CL_CLASS(CZombie), CL_CLASS(CEyeballBoss), CL_CLASS(CHeadlessHatman)))
        return ENTITY_NPC;
    return ENTITY_GENERIC;
}

void CachedEntity::Update()
{
    m_lLastSeen         = 0;
    m_bVisCheckComplete = false;
    m_classid           = 0;
    m_type              = ENTITY_GENERIC;
    m_alive             = false;
    hitboxes.InvalidateCache();

    IClientEntity *e = InternalEntity();
    if (!e)
        return;
    ClientClass *cc = EntClientClass(e);
    if (!cc || !cc->m_ClassID)
        return;
    m_classid = cc->m_ClassID;
    m_type    = ClassifyEntity(m_classid);
    switch (m_type)
    {
    case ENTITY_PLAYER:
        m_alive = !NET_BYTE(e, netvar.iLifeState);
        break;
    case ENTITY_BUILDING:
        m_alive = NET_INT(e, netvar.iBuildingHealth) > 0;
        break;
    case ENTITY_NPC:
        m_alive = NET_INT(e, netvar.iHealth) > 0;
        break;
    default:
        m_alive = true;
        break;
    }
}

CachedEntity::CachedEntity(int idx) : m_IDX(idx), hitboxes(idx) {}

CachedEntity::~CachedEntity()
{
    delete player_info;
    player_info = nullptr;
}

bool CachedEntity::IsVisible()
{
    PROF_SECTION(CE_IsVisible);
    if (m_bVisCheckComplete)
        return m_bAnyHitboxVisible;
    auto hitbox = hitboxes.GetHitbox(std::max(0, (hitboxes.GetNumHitboxes() >> 1) - 1));
    Vector result;
    if (!hitbox)
        result = m_vecOrigin();
    else
        result = hitbox->center;

    if (IsEntityVectorVisible(this, result, true, MASK_SHOT_HULL, nullptr, true))
    {
        m_bAnyHitboxVisible = true;
        m_bVisCheckComplete = true;
        return true;
    }

    m_bAnyHitboxVisible = false;
    m_bVisCheckComplete = true;
    return false;
}

namespace entity_cache
{
static CachedEntity *ents[MAX_ENTITIES];
std::vector<CachedEntity *> valid_ents;
std::vector<CachedEntity *> player_cache;
int max = 1;

CachedEntity *Get(int idx)
{
    if (idx < 0 || idx >= MAX_ENTITIES)
        return nullptr;
    if (!ents[idx])
        ents[idx] = new CachedEntity(idx);
    return ents[idx];
}

void Update()
{
    int highest = g_IEntityList->GetHighestEntityIndex();
    if (highest >= MAX_ENTITIES)
        highest = MAX_ENTITIES - 1;
    if (highest < 0)
        highest = 0;
    max = highest;
    valid_ents.clear();
    player_cache.clear();
    if (g_Settings.bInvalid)
        return;

    for (int i = 0; i <= max; ++i)
    {
        IClientEntity *raw = g_IEntityList->GetClientEntity(i);
        ClientClass *cc    = EntClientClass(raw);
        if (!raw || !cc || !cc->m_ClassID)
            continue;
        if (!ents[i])
            ents[i] = new CachedEntity(i);
        CachedEntity &ent = *ents[i];
        ent.Update();
        if (!ent.Valid())
            continue;
        if (ent.m_Type() == ENTITY_PLAYER)
        {
            if (!ent.player_info)
                ent.player_info = new player_info_s{};
            GetPlayerInfo(ent.m_IDX, ent.player_info);
        }
        if (EntIsDormant(raw))
            continue;
        valid_ents.push_back(&ent);
        if ((ent.m_Type() == ENTITY_PLAYER || ent.m_Type() == ENTITY_BUILDING || ent.m_Type() == ENTITY_NPC) && ent.m_bAlivePlayer())
        {
            ent.hitboxes.UpdateBones();
            if (ent.m_Type() == ENTITY_PLAYER)
                player_cache.push_back(&ent);
        }
    }
}

void Invalidate()
{
    for (int i = 0; i < MAX_ENTITIES; ++i)
    {
        delete ents[i];
        ents[i] = nullptr;
    }
    valid_ents.clear();
    player_cache.clear();
}

void Shutdown()
{
    Invalidate();
    max = 0;
}
} // namespace entity_cache
