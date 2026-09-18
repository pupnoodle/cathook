/*
 * C_BaseEntity.hpp
 *
 *  Created on: Nov 23, 2017
 *      Author: nullifiedcat
 */

#pragma once

#include "reclasses.hpp"
#include "copypasted/CSignature.h"
#include "e8call.hpp"

namespace re
{

class C_BaseEntity
{
public:
    inline static bool IsPlayer(IClientEntity *self)
    {
        if (!self)
            return false;
        auto *cc = self->GetClientClass();
        return cc && cc->m_ClassID == CL_CLASS(CTFPlayer);
    }
    inline static bool ShouldCollide(IClientEntity *self, int collisionGroup, int contentsMask)
    {
        if (!self)
            return true;
        typedef bool (*fn_t)(IClientEntity *, int, int);
        return vfunc<fn_t>(self, 199, 0)(self, collisionGroup, contentsMask);
    }
    inline static int &m_nPredictionRandomSeed()
    {
        if (g_PredictionRandomSeed)
            return *g_PredictionRandomSeed;
        static int placeholder = 0;
        return placeholder;
    }
    inline static int SetAbsOrigin(IClientEntity *self, Vector const &origin)
    {
        typedef Vector &(*fn_t)(IClientEntity *);
        vfunc<fn_t>(self, 11, 0)(self) = origin;
        return 0;
    }
};
} // namespace re
