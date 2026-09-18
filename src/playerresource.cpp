/*
 * playerresource.cpp
 *
 *  Created on: Nov 13, 2016
 *      Author: nullifiedcat
 */

#include <playerresource.h>

#include "common.hpp"

namespace
{
IClientEntity *ResourceEntity(int idx)
{
    IClientEntity *ent = g_IEntityList->GetClientEntity(idx);
    ClientClass *cc    = EntClientClass(ent);
    if (!cc || cc->m_ClassID != RCC_PLAYERRESOURCE)
        return nullptr;
    return ent;
}

bool ResourceIndexOk(int idx, int min_idx)
{
    return idx >= min_idx && idx < MAX_PLAYERS;
}
} // namespace

void TFPlayerResource::Update()
{
    entity = 0;
    for (auto const &ent_not_raw : entity_cache::valid_ents)
    {
        ClientClass *cc = EntClientClass(RAW_ENT(ent_not_raw));
        if (cc && cc->m_ClassID == RCC_PLAYERRESOURCE)
        {
            entity = ent_not_raw->m_IDX;
            return;
        }
    }
}

int TFPlayerResource::GetHealth(CachedEntity *player)
{
    IF_GAME(!IsTF())
        return 100;
    IClientEntity *ent = ResourceEntity(entity);
    if (!ent || !player)
        return 0;
    int idx = player->m_IDX;
    if (!ResourceIndexOk(idx, 0))
        return 0;
    return *(int *) ((uintptr_t) ent + netvar.m_iHealth_Resource + 4 * idx);
}

int TFPlayerResource::GetMaxHealth(CachedEntity *player)
{
    IF_GAME(!IsTF())
        return 100;
    IClientEntity *ent = ResourceEntity(entity);
    if (!ent || !player)
        return 0;
    int idx = player->m_IDX;
    if (!ResourceIndexOk(idx, 0))
        return 0;
    return *(int *) ((uintptr_t) ent + netvar.res_iMaxHealth + 4 * idx);
}

int TFPlayerResource::GetMaxBuffedHealth(CachedEntity *player)
{
    IF_GAME(!IsTF())
        return GetMaxHealth(player);
    IClientEntity *ent = ResourceEntity(entity);
    if (!ent || !player)
        return 0;
    int idx = player->m_IDX;
    if (!ResourceIndexOk(idx, 0))
        return 0;
    return *(int *) ((uintptr_t) ent + netvar.res_iMaxBuffedHealth + 4 * idx);
}

int TFPlayerResource::GetTeam(int idx)
{
    if (!ResourceIndexOk(idx, 0))
        return 0;
    IClientEntity *ent = ResourceEntity(entity);
    if (!ent)
        return 0;
    return *(int *) ((uintptr_t) ent + netvar.res_iTeam + 4 * idx);
}

int TFPlayerResource::GetScore(int idx)
{
    if (!ResourceIndexOk(idx, 1))
        return 0;
    IClientEntity *ent = ResourceEntity(entity);
    if (!ent)
        return 0;
    return *(int *) ((uintptr_t) ent + netvar.m_iTotalScore_Resource + 4 * idx);
}

int TFPlayerResource::GetKills(int idx)
{
    if (!ResourceIndexOk(idx, 1))
        return 0;
    IClientEntity *ent = ResourceEntity(entity);
    if (!ent)
        return 0;
    return *(int *) ((uintptr_t) ent + netvar.m_iKills_Resource + 4 * idx);
}

int TFPlayerResource::GetDeaths(int idx)
{
    if (!ResourceIndexOk(idx, 1))
        return 0;
    IClientEntity *ent = ResourceEntity(entity);
    if (!ent)
        return 0;
    return *(int *) ((uintptr_t) ent + netvar.m_iDeaths_Resource + 4 * idx);
}

int TFPlayerResource::GetLevel(int idx)
{
    if (!ResourceIndexOk(idx, 1))
        return 0;
    IClientEntity *ent = ResourceEntity(entity);
    if (!ent)
        return 0;
    return *(int *) ((uintptr_t) ent + netvar.m_iPlayerLevel_Resource + 4 * idx);
}

int TFPlayerResource::GetDamage(int idx)
{
    if (!ResourceIndexOk(idx, 1))
        return 0;
    IClientEntity *ent = ResourceEntity(entity);
    if (!ent)
        return 0;
    return *(int *) ((uintptr_t) ent + netvar.m_iDamage_Resource + 4 * idx);
}

unsigned TFPlayerResource::GetAccountID(int idx)
{
    if (!ResourceIndexOk(idx, 1))
        return 0;
    IClientEntity *ent = ResourceEntity(entity);
    if (!ent)
        return 0;
    return *(unsigned *) ((uintptr_t) ent + netvar.m_iAccountID_Resource + 4 * idx);
}

int TFPlayerResource::GetPing(int idx)
{
    if (!ResourceIndexOk(idx, 1))
        return 0;
    IClientEntity *ent = ResourceEntity(entity);
    if (!ent)
        return 0;
    return *(int *) ((uintptr_t) ent + netvar.m_iPing_Resource + 4 * idx);
}

int TFPlayerResource::GetClass(CachedEntity *player)
{
    IClientEntity *ent = ResourceEntity(entity);
    if (!ent || !player)
        return 0;
    int idx = player->m_IDX;
    if (!ResourceIndexOk(idx, 0))
        return 0;
    return *(int *) ((uintptr_t) ent + netvar.res_iPlayerClass + 4 * idx);
}

bool TFPlayerResource::isAlive(int idx)
{
    IClientEntity *ent = ResourceEntity(entity);
    if (!ent || !ResourceIndexOk(idx, 0))
        return false;
    return *(bool *) ((uintptr_t) ent + netvar.res_bAlive + idx);
}

bool TFPlayerResource::isValid(int idx)
{
    IClientEntity *ent = ResourceEntity(entity);
    if (!ent || !ResourceIndexOk(idx, 0))
        return false;
    return *(bool *) ((uintptr_t) ent + netvar.res_bValid + idx);
}

int TFPlayerResource::getClass(int idx)
{
    IClientEntity *ent = ResourceEntity(entity);
    if (!ent || !ResourceIndexOk(idx, 0))
        return 0;
    return *(int *) ((uintptr_t) ent + netvar.res_iPlayerClass + 4 * idx);
}

int TFPlayerResource::getTeam(int idx)
{
    IClientEntity *ent = ResourceEntity(entity);
    if (!ent || !ResourceIndexOk(idx, 0))
        return 0;
    return *(int *) ((uintptr_t) ent + netvar.res_iTeam + 4 * idx);
}

TFPlayerResource *g_pPlayerResource{ nullptr };
