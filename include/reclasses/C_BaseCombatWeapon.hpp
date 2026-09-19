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
        return vfunc<fn_t>(self, vtables::entity::is_base_combat_weapon, 0)(self);
    }
    inline static int GetSlot(IClientEntity *self)
    {
        typedef int (*fn_t)(IClientEntity *);
        return vfunc<fn_t>(self, vtables::weapon::get_slot, 0)(self);
    }
    inline static const char *GetPrintName(IClientEntity *self)
    {
        typedef const char *(*fn_t)(IClientEntity *);
        return vfunc<fn_t>(self, vtables::weapon::get_print_name, 0)(self);
    }
};
} // namespace re
