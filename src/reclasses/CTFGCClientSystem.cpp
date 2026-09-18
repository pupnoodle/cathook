/*
 * CTFGCClientSystem.cpp
 *
 *  Created on: Dec 7, 2017
 *      Author: nullifiedcat
 */

#include "common.hpp"
#include "core/e8call.hpp"

using namespace re;

CTFGCClientSystem *CTFGCClientSystem::GTFGCClientSystem()
{
    typedef CTFGCClientSystem *(*GTFGCClientSystem_t)();
    static uintptr_t addr1                          = SigAdd(gSignatures.GetClientSignature(sigs::get_matchmaking_client), 16);
    static GTFGCClientSystem_t GTFGCClientSystem_fn = GTFGCClientSystem_t(addr1);
    return GTFGCClientSystem_fn ? GTFGCClientSystem_fn() : nullptr;
}

void CTFGCClientSystem::AbandonCurrentMatch()
{
    typedef void *(*AbandonCurrentMatch_t)(CTFGCClientSystem *);
    static auto fn = AbandonCurrentMatch_t(gSignatures.GetClientSignature(sigs::abandon_current_match));
    if (!fn)
    {
        logging::Info("CTFGCClientSystem::AbandonCurrentMatch() calling NULL!");
        return;
    }
    fn(this);
}

bool CTFGCClientSystem::BConnectedToMatchServer(bool)
{
    return g_IEngine && g_IEngine->IsInGame();
}

bool CTFGCClientSystem::BHaveLiveMatch()
{
    return g_IEngine && g_IEngine->IsInGame();
}

CTFParty *CTFGCClientSystem::GetParty()
{
    // IDA: sub_1B9B2D0 tests *(this + 48) after GTFGCClientSystem()
    return *reinterpret_cast<CTFParty **>(uintptr_t(this) + 48);
}

int CTFGCClientSystem::JoinMMMatch()
{
    typedef int (*JoinMMMatch_t)(CTFGCClientSystem *);
    static auto fn = JoinMMMatch_t(gSignatures.GetClientSignature(sigs::tf_gc_client_system_join_mm_match));
    return fn ? fn(this) : 0;
}
