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
