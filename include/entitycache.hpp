#pragma once

#include "entityhitboxcache.hpp"
#include <mathlib/vector.h>
#include <icliententity.h>
#include <icliententitylist.h>
#include <cdll_int.h>
#include <enums.hpp>
#include <core/interfaces.hpp>
#include <itemtypes.hpp>
#include "localplayer.hpp"
#include <core/netvars.hpp>
#include "playerresource.h"
#include "globals.h"
#include "classinfo/classinfo.hpp"
#include "client_class.h"
#include "Constants.hpp"
#include "core/vfunc.hpp"
#include "core/vtables.hpp"
#include "sdk/client_entity.hpp"
#include <cfloat>
#include <optional>
#include <soundcache.hpp>

class IClientEntity;
struct player_info_s;

#define RAW_ENT(ce) (ce)->InternalEntity()

#define CE_VAR(entity, offset, type) NET_VAR(RAW_ENT(entity), offset, type)

#define CE_INT(entity, offset) CE_VAR(entity, offset, int)
#define CE_FLOAT(entity, offset) CE_VAR(entity, offset, float)
#define CE_BYTE(entity, offset) CE_VAR(entity, offset, unsigned char)
#define CE_VECTOR(entity, offset) CE_VAR(entity, offset, Vector)

#define CE_GOOD(entity) ((entity) && !g_Settings.bInvalid && (entity)->Good())
#define CE_BAD(entity) (!CE_GOOD(entity))
#define CE_VALID(entity) ((entity) && !g_Settings.bInvalid && (entity)->Valid())
#define CE_INVALID(entity) (!CE_VALID(entity))

#define IDX_GOOD(idx) ((idx) >= 0 && (idx) <= HIGHEST_ENTITY && (idx) < MAX_ENTITIES)
#define IDX_BAD(idx) !IDX_GOOD(idx)

#define HIGHEST_ENTITY (entity_cache::max)
#define ENTITY(idx) (entity_cache::Get(idx))

EntityType ClassifyEntity(int classid);

class CachedEntity
{
public:
    explicit CachedEntity(int idx);
    ~CachedEntity();

    CachedEntity(const CachedEntity &)            = delete;
    CachedEntity &operator=(const CachedEntity &) = delete;

    void Update();

    bool IsVisible();

    IClientEntity *InternalEntity() const
    {
        return g_IEntityList->GetClientEntity(m_IDX);
    }

    bool Good() const
    {
        IClientEntity *e = InternalEntity();
        if (!e || EntIsDormant(e))
            return false;
        ClientClass *cc = EntClientClass(e);
        return cc && cc->m_ClassID;
    }

    bool Valid() const
    {
        IClientEntity *e = InternalEntity();
        if (!e)
            return false;
        ClientClass *cc = EntClientClass(e);
        return cc && cc->m_ClassID;
    }

    template <typename T> T &var(uintptr_t offset) const
    {
        return *reinterpret_cast<T *>(uintptr_t(RAW_ENT(this)) + offset);
    }

    const int m_IDX;

    int m_iClassID() const
    {
        if (m_classid)
            return m_classid;
        IClientEntity *e = InternalEntity();
        if (!e)
            return 0;
        ClientClass *cc = EntClientClass(e);
        return cc ? cc->m_ClassID : 0;
    }

    Vector m_vecOrigin() const
    {
        IClientEntity *e = InternalEntity();
        if (!e || !netvar.m_vecOrigin)
            return {};
        return NET_VECTOR(e, netvar.m_vecOrigin);
    }

    std::optional<Vector> m_vecDormantOrigin() const
    {
        IClientEntity *e = InternalEntity();
        if (!e)
            return std::nullopt;
        if (!EntIsDormant(e))
            return m_vecOrigin();
        auto vec = soundcache::GetSoundLocation(m_IDX);
        if (vec)
            return *vec;
        return std::nullopt;
    }

    int m_iTeam() const
    {
        IClientEntity *e = InternalEntity();
        return e ? NET_INT(e, netvar.iTeamNum) : 0;
    }

    unsigned char m_MoveType() const
    {
        IClientEntity *e = InternalEntity();
        return (e && netvar.movetype) ? NET_BYTE(e, netvar.movetype) : 0;
    }

    bool m_bAlivePlayer() const
    {
        return m_alive;
    }

    bool m_bEnemy() const
    {
        if (CE_BAD(g_pLocalPlayer->entity))
            return true;
        return m_iTeam() != g_pLocalPlayer->team;
    }

    int m_iMaxHealth() const
    {
        if (m_Type() == ENTITY_PLAYER)
            return g_pPlayerResource->GetMaxHealth(const_cast<CachedEntity *>(this));
        if (m_Type() == ENTITY_BUILDING)
            return NET_INT(RAW_ENT(this), netvar.iBuildingMaxHealth);
        if (m_Type() == ENTITY_NPC)
            return NET_INT(RAW_ENT(this), netvar.iHealth);
        return 0;
    }

    int m_iHealth() const
    {
        if (m_Type() == ENTITY_PLAYER)
            return NET_INT(RAW_ENT(this), netvar.iHealth);
        if (m_Type() == ENTITY_BUILDING)
            return NET_INT(RAW_ENT(this), netvar.iBuildingHealth);
        if (m_Type() == ENTITY_NPC)
            return NET_INT(RAW_ENT(this), netvar.iHealth);
        return 0;
    }

    Vector &m_vecAngle()
    {
        return CE_VECTOR(this, netvar.m_angEyeAngles);
    }

    EntityType m_Type() const
    {
        return m_type;
    }

    float m_flDistance() const
    {
        if (CE_GOOD(g_pLocalPlayer->entity))
            return g_pLocalPlayer->v_Origin.DistTo(m_vecOrigin());
        return FLT_MAX;
    }

    bool m_bGrenadeProjectile() const
    {
        int id = m_iClassID();
        return class_is(id, CL_CLASS(CTFGrenadePipebombProjectile), CL_CLASS(CTFProjectile_Cleaver), CL_CLASS(CTFProjectile_Jar), CL_CLASS(CTFProjectile_JarMilk));
    }

    bool IsProjectileACrit() const
    {
        if (m_bGrenadeProjectile())
            return CE_BYTE(this, netvar.Grenade_bCritical);
        return CE_BYTE(this, netvar.Rocket_bCritical);
    }

    bool m_bCritProjectile() const
    {
        return m_Type() == ENTITY_PROJECTILE && IsProjectileACrit();
    }

    k_EItemType m_ItemType() const
    {
        if (m_Type() == ENTITY_GENERIC)
            return g_ItemManager.GetItemType(const_cast<CachedEntity *>(this));
        return ITEM_NONE;
    }

    void Reset()
    {
        m_bAnyHitboxVisible = false;
        m_bVisCheckComplete = false;
        m_lLastSeen         = 0;
        m_classid           = 0;
        m_type              = ENTITY_GENERIC;
        m_alive             = false;
        if (player_info)
            memset(player_info, 0, sizeof(player_info_s));
        m_vecAcceleration.Zero();
        m_vecVelocity.Zero();
    }

    bool was_dormant() const
    {
        IClientEntity *e = InternalEntity();
        return !e || EntIsDormant(e);
    }

    bool m_bAnyHitboxVisible{ false };
    bool m_bVisCheckComplete{ false };
    unsigned long m_lLastSeen{ 0 };
    Vector m_vecVelocity{ 0 };
    Vector m_vecAcceleration{ 0 };
    hitbox_cache::EntityHitboxCache hitboxes;
    player_info_s *player_info{ nullptr };
    bool velocity_is_valid{ false };

private:
    int m_classid{ 0 };
    EntityType m_type{ ENTITY_GENERIC };
    bool m_alive{ false };
};

namespace entity_cache
{

extern int max;
extern std::vector<CachedEntity *> valid_ents;
extern std::vector<CachedEntity *> player_cache;

CachedEntity *Get(int idx);
void Update();
void Invalidate();
void Shutdown();

} // namespace entity_cache
