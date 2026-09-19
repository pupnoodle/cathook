/*
 * CMannVsMachineStats.cpp
 *
 *  Created on: Apr 10, 2018
 *      Author: bencat07
 */
#include "common.hpp"
using namespace re;

C_MannVsMachineStats *C_MannVsMachineStats::G_MannVsMachineStats()
{
    typedef C_MannVsMachineStats *(*fn_t)();
    static auto fn = fn_t(SigAdd(gSignatures.GetClientSignature(sigs::mvm_stats_singleton), sigs::mvm_stats_singleton_offset));
    return fn ? fn() : nullptr;
}

int *C_MannVsMachineStats::AddLocalPlayerUpgrade(int id, int &item_def)
{
    typedef int *(*fn_t)(C_MannVsMachineStats *, int, int16_t);
    static auto fn = fn_t(gSignatures.GetClientSignature(sigs::mvm_add_local_player_upgrade));
    return fn ? fn(this, id, int16_t(item_def)) : nullptr;
}
