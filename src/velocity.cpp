/*
 * velocity.cpp
 *
 *  Created on: May 27, 2017
 *      Author: nullifiedcat
 */

#include "velocity.hpp"
#include "common.hpp"
#include "copypasted/CSignature.h"

namespace velocity
{

EstimateAbsVelocity_t EstimateAbsVelocity{};

void Init()
{
    EstimateAbsVelocity = [](IClientEntity *ent, Vector &vel)
    {
        if (!ent)
        {
            vel.Init();
            return;
        }
        vel = NET_VECTOR(ent, netvar.vVelocity);
    };
}
} // namespace velocity
