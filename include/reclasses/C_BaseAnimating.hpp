#pragma once

#include "reclasses.hpp"
#include "copypasted/CSignature.h"
#include "e8call.hpp"

namespace re
{

class C_BaseAnimating
{
public:
    inline static void InvalidateBoneCache(IClientEntity *self)
    {
        typedef int (*InvalidateBoneCache_t)(IClientEntity *);
        static uintptr_t addr                            = gSignatures.GetClientSignature(sigs::base_animating_invalidate_bone_cache);
        static InvalidateBoneCache_t InvalidateBoneCache = InvalidateBoneCache_t(addr);
        if (InvalidateBoneCache)
            InvalidateBoneCache(self);
    }
    // Currently unused, might be useful in the near future though
    inline static bool Interpolate(IClientEntity *self, float time)
    {
        typedef bool (*fn_t)(IClientEntity *, float);
        return vfunc<fn_t>(self, offsets::PlatformOffset(143, offsets::undefined, 143), 0)(self, time);
    }
};
} // namespace re
