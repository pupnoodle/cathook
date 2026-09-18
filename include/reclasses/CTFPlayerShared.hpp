#pragma once

#include "reclasses.hpp"
#include "e8call.hpp"

namespace re
{

class CTFPlayerShared
{
public:
    inline static CTFPlayerShared *GetPlayerShared(IClientEntity *ent)
    {
        return (CTFPlayerShared *) (((uintptr_t) ent) + netvar.m_Shared);
    }
    inline static bool IsDominatingPlayer(CTFPlayerShared *self, int ent_idx)
    {
        if (!self || !netvar.bPlayerDominated || !netvar.m_Shared)
            return false;
        if (ent_idx < 0 || ent_idx >= PLAYER_ARRAY_SIZE)
            return false;
        const int rel = int(netvar.bPlayerDominated) - int(netvar.m_Shared);
        if (rel < 0)
            return false;
        return *(((bool *) ((uintptr_t) self + rel)) + ent_idx);
    }
    inline static float GetCritMult(CTFPlayerShared *self)
    {
        int raw = netvar.m_iCritMult ? *(int *) ((uintptr_t) self + (netvar.m_iCritMult - netvar.m_Shared)) : 0;
        return RemapValClamped(raw, 0, 255, 1.0, 4.0);
    }
    inline static bool IsCritBoosted(CTFPlayerShared *self)
    {
        typedef bool (*InCond_t)(CTFPlayerShared *, int);
        static auto fn = (InCond_t) gSignatures.GetClientSignature(sigs::in_cond);
        return fn && fn(self, 11);
    }
    inline static float CalculateChargeCap(CTFPlayerShared *self)
    {
        typedef float (*CalculateChargeCap_t)(re::CTFPlayerShared *);
        static auto fn = CalculateChargeCap_t(gSignatures.GetClientSignature(sigs::calculate_charge_cap));
        return fn ? fn(self) : 0.0f;
    }
    inline static float GetChargeMeter(CTFPlayerShared *self)
    {
        if (!netvar.m_flChargeMeter || !netvar.m_Shared)
            return 0.0f;
        return *(float *) (((uintptr_t) self) + (netvar.m_flChargeMeter - netvar.m_Shared));
    }
};
} // namespace re
