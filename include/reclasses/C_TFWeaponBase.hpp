/*
 * C_TFWeaponBase.hpp
 *
 *  Created on: Nov 23, 2017
 *      Author: nullifiedcat
 */

#pragma once

#include <common.hpp>

namespace re
{

class C_TFWeaponBase : public re::C_BaseCombatWeapon
{
public:
    struct SpreadPitchYaw
    {
        float pitch;
        float yaw;
    };

    inline static void GetProjectileFireSetup(IClientEntity *weapon, IClientEntity *pPlayer, Vector vecOffset, Vector *vecSrc, Vector *angForward, bool bHitTeammates, float flEndDist)
    {
        typedef void (*GetProjectileFireSetup_t)(IClientEntity * weapon, IClientEntity * pPlayer, Vector vecOffset, Vector * vecSrc, Vector * angForward, bool bHitTeammates, float flEndDist);
        if (!weapon || !pPlayer || !vecSrc || !angForward)
            return;
        static auto signature                                     = gSignatures.GetClientSignature(sigs::get_projectile_fire_setup);
        static GetProjectileFireSetup_t GetProjectileFireSetup_fn = (GetProjectileFireSetup_t) signature;
        if (GetProjectileFireSetup_fn)
            GetProjectileFireSetup_fn(weapon, pPlayer, vecOffset, vecSrc, angForward, bHitTeammates, flEndDist);
    }
    inline static void GetProjectileFireSetupHuntsman(IClientEntity *weapon, IClientEntity *pPlayer, Vector vecOffset, Vector *vecSrc, Vector *angForward, bool bHitTeammates, float flEndDist)
    {
        GetProjectileFireSetup(weapon, pPlayer, vecOffset, vecSrc, angForward, bHitTeammates, flEndDist);
    }
    inline static Vector GetSpreadAngles(IClientEntity *self)
    {
        if (!self)
            return {};
        typedef SpreadPitchYaw (*GetSpreadAngles_t)(IClientEntity *);
        static int slot = -1;
        if (slot < 0)
            slot = WeaponVtableSlot(self, sigs::get_spread_angles, -1);
        if (slot >= 0)
        {
            SpreadPitchYaw py = vfunc<GetSpreadAngles_t>(self, slot, 0)(self);
            return Vector{ py.pitch, py.yaw, 0.0f };
        }
        static auto signature                       = gSignatures.GetClientSignature(sigs::get_spread_angles);
        static GetSpreadAngles_t GetSpreadAngles_fn = (GetSpreadAngles_t) signature;
        if (!GetSpreadAngles_fn)
            return {};
        SpreadPitchYaw py = GetSpreadAngles_fn(self);
        return Vector{ py.pitch, py.yaw, 0.0f };
    }
    inline static int GetWeaponID(IClientEntity *self)
    {
        typedef int (*fn_t)(IClientEntity *);
        return vfunc<fn_t>(self, vtables::weapon::get_weapon_id, 0)(self);
    }
    inline static bool IsMeleeWeapon(IClientEntity *self)
    {
        if (!self)
            return false;
        if (re::C_BaseCombatWeapon::GetSlot(self) == 2)
            return true;
        switch (GetWeaponID(self))
        {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
        case 64:
        case 72:
        case 74:
        case 82:
        case 86:
        case 96:
        case 104:
        case 106:
            return true;
        default:
            break;
        }
        auto **vt = *reinterpret_cast<void ***>(self);
        if (!vt)
            return false;
        static void *melee_helper = reinterpret_cast<void *>(gSignatures.GetClientSignature(sigs::ctf_weapon_base_melee_calc_is_attack_critical_helper));
        return melee_helper && vt[vtables::weapon::calc_is_attack_critical_helper] == melee_helper;
    }
    inline static bool IsViewModelFlipped(IClientEntity *self)
    {
        static lazy_netvar flip{ "DT_TFWeaponBase", "m_bFlipViewModel" };
        return self && flip && NET_VAR(self, flip, bool);
    }
    inline static IClientEntity *GetOwnerViaInterface(IClientEntity *self)
    {
        return g_IEntityList->GetClientEntity(HandleToIDX(NET_INT(self, netvar.hOwner)));
    }
    inline static bool UsesPrimaryAmmo(IClientEntity *self)
    {
        if (!self || !netvar.m_iPrimaryAmmoType)
            return false;
        return NET_INT(self, netvar.m_iPrimaryAmmoType) >= 0;
    }
    inline static bool HasPrimaryAmmo(IClientEntity *self)
    {
        if (!UsesPrimaryAmmo(self))
            return false;
        if (netvar.m_iClip1)
        {
            int clip = NET_INT(self, netvar.m_iClip1);
            if (clip > 0)
                return true;
            if (clip < 0)
                return true;
        }
        IClientEntity *owner = GetOwnerViaInterface(self);
        if (!owner || !netvar.m_iAmmo || !netvar.m_iPrimaryAmmoType)
            return false;
        int ammo_type = NET_INT(self, netvar.m_iPrimaryAmmoType);
        if (ammo_type < 0)
            return false;
        return reinterpret_cast<int *>(uintptr_t(owner) + netvar.m_iAmmo)[ammo_type] > 0;
    }
    inline static bool AreRandomCritsEnabled(IClientEntity *self)
    {
        if (!g_ICvar)
            return false;
        static ConVar *tf_weapon_criticals       = g_ICvar->FindVar("tf_weapon_criticals");
        static ConVar *tf_weapon_criticals_melee = g_ICvar->FindVar("tf_weapon_criticals_melee");
        if (self && IsMeleeWeapon(self))
        {
            int melee = tf_weapon_criticals_melee ? tf_weapon_criticals_melee->GetInt() : 1;
            if (melee == 0)
                return false;
            return melee == 2 || (tf_weapon_criticals && tf_weapon_criticals->GetInt() != 0);
        }
        return tf_weapon_criticals && tf_weapon_criticals->GetInt() != 0;
    }
    inline static int WeaponVtableSlot(IClientEntity *self, const char *sig, int fallback)
    {
        if (!self)
            return fallback;
        auto *fn = reinterpret_cast<void *>(gSignatures.GetClientSignature(sig));
        auto **vt = *reinterpret_cast<void ***>(self);
        if (!fn || !vt)
            return fallback;
        for (int i = 2; i < 600; ++i)
            if (vt[i] == fn)
                return i;
        return fallback;
    }
    inline static bool CalcIsAttackCriticalHelper(IClientEntity *self)
    {
        typedef bool (*fn_t)(IClientEntity *);
        static int slot = -1;
        if (slot < 0)
            slot = WeaponVtableSlot(self, sigs::ctf_weapon_base_calc_is_attack_critical, int(vtables::weapon::calc_is_attack_critical_helper));
        return vfunc<fn_t>(self, slot, 0)(self);
    }
    inline static bool CalcIsAttackCriticalHelperNoCrits(IClientEntity *self)
    {
        typedef bool (*fn_t)(IClientEntity *);
        static int slot = -1;
        if (slot < 0)
            slot = WeaponVtableSlot(self, sigs::ctf_weapon_base_melee_calc_is_attack_critical, int(vtables::weapon::calc_is_attack_critical_helper_no_crits));
        return vfunc<fn_t>(self, slot, 0)(self);
    }
    inline static bool CanFireCriticalShot(IClientEntity *self, bool unknown1, IClientEntity *unknown2)
    {
        typedef bool (*fn_t)(IClientEntity *, bool, IClientEntity *);
        return vfunc<fn_t>(self, vtables::weapon::can_fire_critical_shot, 0)(self, unknown1, unknown2);
    }
    inline static float ApplyFireDelay(IClientEntity *self, float delay)
    {
        typedef float (*fn_t)(IClientEntity *, float);
        return vfunc<fn_t>(self, vtables::weapon::apply_fire_delay, 0)(self, delay);
    }
    inline static void AddToCritBucket(IClientEntity *self, float value)
    {
        constexpr float max_bucket_capacity = 1000.0f;
        crit_bucket_(self)                  = fminf(crit_bucket_(self) + value, max_bucket_capacity);
    }
    inline static int CalcIsAttackCritical(IClientEntity *self)
    {
        IClientEntity *owner = GetOwnerViaInterface(self);
        if (owner)
        {
            if (C_BaseEntity::IsPlayer(owner))
            {
                // Always run calculations
                // Never write anything into entity, at least from here.

                // if (g_GlobalVars->framecount != *(int *)(self + 2872))
                {
                    // *(int *)(self + 2872) = g_GlobalVars->framecount;
                    // *(char *)(self + 2839) = 0;

                    if (g_pGameRules->RoundMode() == 5 && g_pGameRules->WinningTeam() == NET_INT(owner, netvar.iTeamNum))
                    {
                        // *(char *)(self + 2838) = 1;
                        return 1;
                    }
                    else
                    {
                        if (AreRandomCritsEnabled(self))
                            return CalcIsAttackCriticalHelper(self);
                        else
                            return CalcIsAttackCriticalHelperNoCrits(self);
                    }
                }
            }
        }

        return 0;
    }
    inline static int &weapon_info_handle_(IClientEntity *self)
    {
        static int dummy;
        const int off = netvar.m_iWeaponMode ? int(netvar.m_iWeaponMode) : 3952;
        return self ? *(int *) (uintptr_t(self) + off) : dummy;
    }
    inline static float &crit_bucket_(IClientEntity *self)
    {
        static float dummy;
        const int off = netvar.m_flCritTokenBucket ? int(netvar.m_flCritTokenBucket) : 3716;
        return self ? *(float *) (uintptr_t(self) + off) : dummy;
    }
};
} // namespace re
