/*
 * CTFGCClientSystem.cpp
 *
 *  Created on: Dec 7, 2017
 *      Author: nullifiedcat
 */

#include "common.hpp"
#include "core/e8call.hpp"

using namespace re;

struct gc_layout
{
    int party_off{ int(vtables::gc::party) };
    int match_id_off{ int(vtables::gc::assigned_match_id) };
    int match_ended_off{ int(vtables::gc::assigned_match_ended) };
    int force_ping_off{ int(vtables::gc::force_ping_refresh) };
};

static gc_layout live_gc_layout()
{
    static gc_layout layout = [] {
        gc_layout out;
        auto *code = reinterpret_cast<uint8_t *>(gSignatures.GetClientSignature(sigs::gc_connected_to_match_server));
        if (!code)
            return out;
        if (code[9] == 0x0F && code[10] == 0xB6 && code[11] == 0x97)
        {
            int universe_byte = *reinterpret_cast<int *>(code + 12);
            out.match_id_off  = universe_byte - 6;
        }
        auto *join       = reinterpret_cast<uint8_t *>(gSignatures.GetClientSignature(sigs::tf_gc_client_system_join_mm_match));
        bool found_ended = false;
        auto find_ended  = [&](uint8_t *fn) {
            if (!fn)
                return;
            for (int i = 0; i < 0x180; ++i)
            {
                int disp = 0;
                if (fn[i] == 0x41 && fn[i + 1] == 0x80 && fn[i + 2] == 0xBC && fn[i + 3] == 0x24 && fn[i + 8] == 0)
                    disp = *reinterpret_cast<int *>(fn + i + 4);
                else if (fn[i] == 0x80 && fn[i + 1] == 0xBF && fn[i + 6] == 0)
                    disp = *reinterpret_cast<int *>(fn + i + 2);
                else
                    continue;
                if (disp > out.match_id_off + 7 && disp <= out.match_id_off + 16)
                {
                    out.match_ended_off = disp;
                    found_ended         = true;
                    return;
                }
            }
        };
        find_ended(join);
        if (!found_ended)
            find_ended(code);
        logging::Info("CTFGCClientSystem layout match_id=%x ended=%x", out.match_id_off, out.match_ended_off);
        auto *force = reinterpret_cast<uint8_t *>(gSignatures.GetClientSignature(sigs::gc_force_ping_refresh));
        if (force)
        {
            for (int i = 0; i < 64; ++i)
            {
                if (force[i] == 0xC6 && force[i + 1] == 0x83 && force[i + 6] == 0x01)
                {
                    out.force_ping_off = *reinterpret_cast<int *>(force + i + 2);
                    break;
                }
            }
        }
        auto *party = reinterpret_cast<uint8_t *>(gSignatures.GetClientSignature(sigs::party_allowed_to_party_with));
        if (party)
        {
            for (int i = 0; i < 32; ++i)
            {
                if (party[i] == 0x48 && party[i + 1] == 0x8B && party[i + 2] == 0x7F)
                {
                    out.party_off = party[i + 3];
                    break;
                }
            }
        }
        return out;
    }();
    return layout;
}

CTFGCClientSystem *CTFGCClientSystem::GTFGCClientSystem()
{
    typedef CTFGCClientSystem *(*GTFGCClientSystem_t)();
    static auto fn = GTFGCClientSystem_t(gSignatures.GetClientSignature(sigs::get_matchmaking_client));
    return fn ? fn() : nullptr;
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

bool CTFGCClientSystem::BConnectedToMatchServer(bool flag)
{
    using Fn = char (*)(CTFGCClientSystem *, char);
    static auto fn = Fn(gSignatures.GetClientSignature(sigs::gc_connected_to_match_server));
    return fn ? fn(this, flag) : false;
}

bool CTFGCClientSystem::BHaveLiveMatch()
{
    if (!this)
        return false;
    const auto layout = live_gc_layout();
    auto *base        = reinterpret_cast<uint8_t *>(this);
    const uint32_t account = *reinterpret_cast<uint32_t *>(base + layout.match_id_off);
    return account != 0 && base[layout.match_ended_off] == 0;
}

CTFParty *CTFGCClientSystem::GetParty()
{
    return *reinterpret_cast<CTFParty **>(uintptr_t(this) + live_gc_layout().party_off);
}

int CTFGCClientSystem::JoinMMMatch()
{
    typedef int (*JoinMMMatch_t)(CTFGCClientSystem *);
    static auto fn = JoinMMMatch_t(gSignatures.GetClientSignature(sigs::tf_gc_client_system_join_mm_match));
    return fn ? fn(this) : 0;
}

void CTFGCClientSystem::RequestAcceptMatchInvite(uint64_t lobby_id)
{
    using Fn = void (*)(CTFGCClientSystem *, uint64_t);
    static auto fn = Fn(gSignatures.GetClientSignature(sigs::tf_gc_client_system_request_accept_match_invite));
    if (fn)
        fn(this, lobby_id);
}

void CTFGCClientSystem::ForcePingRefresh()
{
    if (!this)
        return;
    reinterpret_cast<uint8_t *>(this)[live_gc_layout().force_ping_off] = 1;
}
