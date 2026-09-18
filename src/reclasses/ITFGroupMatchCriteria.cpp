/*
 * ITFGroupMatchCriteria.cpp
 *
 *  Created on: Dec 7, 2017
 *      Author: nullifiedcat
 */

#include "common.hpp"

int re::ITFGroupMatchCriteria::SetMatchGroup(re::ITFGroupMatchCriteria *this_, int group)
{
    if (!this_)
        return 0;
    *reinterpret_cast<int *>(uintptr_t(this_) + 0x30) = group;
    return group;
}
