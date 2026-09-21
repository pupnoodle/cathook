/*
 * C_TFWeaponBaseGun.hpp
 *
 *  Created on: Nov 23, 2017
 *      Author: nullifiedcat
 */

#pragma once

#include "reclasses.hpp"
#include "WeaponData.hpp"

namespace re
{

class C_TFWeaponBaseGun : public C_TFWeaponBase
{
public:
    inline static float GetProjectileSpeed(IClientEntity *self)
    {
        return GetWeaponData(self)->m_flProjectileSpeed;
    }
    inline static float GetWeaponSpread(IClientEntity *self)
    {
        if (!self)
            return 0.0f;
        const auto info_off = weapon_layout::weapon_info();
        if (!info_off || !*reinterpret_cast<void **>(uintptr_t(self) + info_off))
            return 0.0f;
        const auto mode_off = weapon_layout::weapon_mode();
        if (mode_off)
        {
            const int mode = *reinterpret_cast<int *>(uintptr_t(self) + mode_off);
            if (mode < 0 || mode > 2)
                return 0.0f;
        }
        typedef float (*fn_t)(IClientEntity *);
        static auto fn = (fn_t) gSignatures.GetClientSignature(sigs::tf_weapon_base_gun_get_bullet_spread);
        return fn ? fn(self) : 0.0f;
    }
    inline static float GetProjectileGravity(IClientEntity *self)
    {
        (void) self;
        return 0.0f;
    }
    inline static int LaunchGrenade(IClientEntity *self)
    {
        (void) self;
        return 0;
    }
};
} // namespace re
