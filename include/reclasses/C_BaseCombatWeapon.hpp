/*
 * C_BaseCombatWeapon.hpp
 *
 *  Created on: Nov 23, 2017
 *      Author: nullifiedcat
 */

#pragma once

#include "reclasses.hpp"

namespace re
{

class C_BaseCombatWeapon : public C_BaseEntity
{
public:
    inline static bool IsBaseCombatWeapon(IClientEntity *self)
    {
        typedef bool (*fn_t)(IClientEntity *);
        return vfunc<fn_t>(self, 138, 0)(self);
    }
    inline static int GetSlot(IClientEntity *self)
    {
        typedef int (*fn_t)(IClientEntity *);
        return vfunc<fn_t>(self, 398, 0)(self);
    }
    inline static const char *GetPrintName(IClientEntity *self)
    {
        typedef const char *(*fn_t)(IClientEntity *);
        return vfunc<fn_t>(self, offsets::PlatformOffset(401, offsets::undefined, 401), 0)(self);
    }
};
} // namespace re
