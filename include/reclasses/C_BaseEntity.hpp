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
        auto *cc = EntClientClass(self);
        return cc && cc->m_ClassID == CL_CLASS(CTFPlayer);
    }
    inline static const Vector &GetAbsOrigin(IClientEntity *self)
    {
        typedef const Vector &(*fn_t)(IClientEntity *);
        return vfunc<fn_t>(self, vtables::entity::get_abs_origin, 0)(self);
    }
    inline static const QAngle &GetAbsAngles(IClientEntity *self)
    {
        typedef const QAngle &(*fn_t)(IClientEntity *);
        return vfunc<fn_t>(self, vtables::entity::get_abs_angles, 0)(self);
    }
    inline static bool ShouldCollide(IClientEntity *self, int collisionGroup, int contentsMask)
    {
        if (!self)
            return true;
        typedef bool (*fn_t)(IClientEntity *, int, int);
        return vfunc<fn_t>(self, vtables::entity::should_collide, 0)(self, collisionGroup, contentsMask);
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
        const_cast<Vector &>(GetAbsOrigin(self)) = origin;
        return 0;
    }
    inline static void SetAbsAngles(IClientEntity *self, QAngle const &angles)
    {
        const_cast<QAngle &>(GetAbsAngles(self)) = angles;
    }
    inline static IClientEntity *FromHandle(int handle)
    {
        if (!handle || handle == -1)
            return nullptr;
        return g_IEntityList->GetClientEntity(HandleToIDX(handle));
    }
    inline static IClientEntity *FirstMoveChild(IClientEntity *self)
    {
        const int off = netvar.moveparent;
        if (!self || off < 24)
            return nullptr;
        return FromHandle(*(int *) ((uintptr_t) self + off - 24));
    }
    inline static IClientEntity *NextMovePeer(IClientEntity *self)
    {
        const int off = netvar.moveparent;
        if (!self || off < 16)
            return nullptr;
        return FromHandle(*(int *) ((uintptr_t) self + off - 16));
    }
};
} // namespace re
