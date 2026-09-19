#pragma once

#include "reclasses.hpp"

namespace re
{

class C_BasePlayer : public C_BaseEntity
{
public:
    inline static CTFPlayerShared &shared_(IClientEntity *self)
    {
        return *((CTFPlayerShared *) (uintptr_t(self) + netvar.m_Shared));
    }
    inline static Vector GetEyePosition(IClientEntity *self)
    {
        typedef Vector &(*fn_t)(IClientEntity *);
        Vector origin = vfunc<fn_t>(self, vtables::entity::get_abs_origin, 0)(self);
        if (netvar.vViewOffset)
            origin += NET_VECTOR(self, netvar.vViewOffset);
        return origin;
    }
    inline static Vector &GetEyeAngles(IClientEntity *self)
    {
        return NET_VECTOR(self, netvar.m_angEyeAngles);
    }
    inline static Vector &GetLocalEyeAngles(IClientEntity *self)
    {
        return NET_VECTOR(self, netvar.m_angEyeAnglesLocal ? netvar.m_angEyeAnglesLocal : netvar.m_angEyeAngles);
    }
    inline static IClientEntity *GetEquippedDemoShield(IClientEntity *self)
    {
        if (!self || !g_IEntityList)
            return nullptr;
        const int owner = EntIndex(self);
        const int max_e = g_IEntityList->GetHighestEntityIndex();
        for (int i = 1; i <= max_e; ++i)
        {
            IClientEntity *ent = g_IEntityList->GetClientEntity(i);
            if (!ent)
                continue;
            auto *cc = EntClientClass(ent);
            if (!cc || cc->m_ClassID != CL_CLASS(CTFWearableDemoShield))
                continue;
            int handle = netvar.m_hOwnerEntity ? NET_INT(ent, netvar.m_hOwnerEntity) : (netvar.hOwner ? NET_INT(ent, netvar.hOwner) : 0);
            if (HandleToIDX(handle) == owner)
                return ent;
        }
        return nullptr;
    }
};
} // namespace re
