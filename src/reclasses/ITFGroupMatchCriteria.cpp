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
    using Fn = int (*)(ITFGroupMatchCriteria *, int);
    static auto fn = Fn(SigAdd(gSignatures.GetClientSignature(sigs::group_criteria_set_match_group), sigs::group_criteria_set_match_group_offset));
    if (fn)
        return fn(this_, group);
    *reinterpret_cast<int *>(uintptr_t(this_) + 0x30) = group;
    return group;
}
