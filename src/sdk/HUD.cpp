/*
 * HUD.cpp
 *
 *  Created on: Jun 4, 2017
 *      Author: nullifiedcat
 */

#include <core/logging.hpp>
#include "sdk/HUD.h"

#include "copypasted/CSignature.h"
#include "core/sigs.hpp"

CHudElement *CHud::FindElement(const char *name)
{
    typedef CHudElement *(*FindElementFn)(CHud *, const char *);
    static auto findel = FindElementFn(gSignatures.GetClientSignature(sigs::hud_find_element));
    return findel ? findel(this, name) : nullptr;
}
float &CHud::GetSensitivityFactor()
{
    return *(float *) ((uintptr_t) this + 8);
}
