/*
 * AutoJoin.cpp
 *
 *  Created on: Jul 28, 2017
 *      Author: nullifiedcat
 */

#include <settings/Int.hpp>
#include "HookTools.hpp"
#include <hacks/AutoJoin.hpp>

#include "common.hpp"
#include "hack.hpp"
#include "MiscTemporary.hpp"
#include "DetourHook.hpp"

namespace hacks::shared::autojoin
{
static settings::Boolean autojoin_team{ "autojoin.team", "false" };
static settings::Int autojoin_class{ "autojoin.class", "0" };
static settings::Boolean auto_queue{ "autojoin.auto-queue", "false" };
static settings::Boolean auto_requeue{ "autojoin.auto-requeue", "false" };
static settings::Boolean partybypass{ "hack.party-bypass", "false" };

/*
 * Credits to Blackfire for helping me with auto-requeue!
 */

const std::string classnames[] = { "scout", "sniper", "soldier", "demoman", "medic", "heavyweapons", "pyro", "spy", "engineer" };

bool UnassignedTeam()
{
    return !g_pLocalPlayer->team or (g_pLocalPlayer->team == TEAM_SPEC);
}

bool UnassignedClass()
{
    return g_pLocalPlayer->clazz != *autojoin_class;
}

static Timer startqueue_timer{};
#if not ENABLE_VISUALS
Timer queue_time{};
#endif
void updateSearch()
{
    if (!auto_queue && !auto_requeue)
    {
#if not ENABLE_VISUALS
        queue_time.update();
#endif
        return;
    }
    if (g_IEngine->IsInGame())
    {
#if not ENABLE_VISUALS
        queue_time.update();
#endif
        return;
    }

    re::CTFGCClientSystem *gc = re::CTFGCClientSystem::GTFGCClientSystem();
    re::CTFPartyClient *pc    = re::CTFPartyClient::GTFPartyClient();
    int invites               = pc ? pc->GetPendingInvites() : 0;

    if (current_user_cmd && gc && gc->BConnectedToMatchServer(false) && gc->BHaveLiveMatch())
    {
#if not ENABLE_VISUALS
        queue_time.update();
#endif
        tfmm::leaveQueue();
    }
    //    if (gc && !gc->BConnectedToMatchServer(false) &&
    //            queuetime.test_and_set(10 * 1000 * 60) &&
    //            !gc->BHaveLiveMatch())
    //        tfmm::leaveQueue();

    if (auto_requeue)
    {
        if (startqueue_timer.check(5000) && gc && !gc->BConnectedToMatchServer(false) && !gc->BHaveLiveMatch() && !invites)
            if (pc && !(pc->BInQueueForMatchGroup(tfmm::getQueue()) || pc->BInQueueForStandby()))
            {
                logging::Info("Starting queue for standby, Invites %d", invites);
                tfmm::startQueueStandby();
            }
    }

    if (auto_queue)
    {
        if (startqueue_timer.check(5000) && gc && !gc->BConnectedToMatchServer(false) && !gc->BHaveLiveMatch() && !invites)
            if (pc && !(pc->BInQueueForMatchGroup(tfmm::getQueue()) || pc->BInQueueForStandby()))
            {
                logging::Info("Starting queue, Invites %d", invites);
                tfmm::startQueue();
            }
    }
    
    startqueue_timer.test_and_set(5000);
#if not ENABLE_VISUALS
    if (queue_time.test_and_set(1200000))
    {
        g_IEngine->ClientCmd_Unrestricted("quit"); // lol
    }
#endif
}
static void update()
{
    static Timer autoteam_timer{};
    if (autoteam_timer.test_and_set(750))
    {
        if (autojoin_team and UnassignedTeam())
        {
            hack::ExecuteCommand("autoteam");
        }
        else if (autojoin_class and UnassignedClass())
        {
            if (int(autojoin_class) < 10)
                g_IEngine->ExecuteClientCmd(format("join_class ", classnames[int(autojoin_class) - 1]).c_str());
        }
    }
}

void onShutdown()
{
    if (auto_queue)
        tfmm::startQueue();
}

static CatCommand get_steamid("print_steamid", "Prints your SteamID", []() { g_ICvar->ConsoleColorPrintf(MENU_COLOR, "%u\n", g_ISteamUser->GetSteamID().GetAccountID()); });

static DetourHook allowed_party_detour;
static DetourHook can_invite_detour;
static DetourHook class_menu_detour;
static DetourHook team_menu_detour;
static DetourHook intro_menu_detour;

static void class_menu_show_panel_hook(void *me, bool show)
{
    using Fn = void (*)(void *, bool);
    auto orig = Fn(class_menu_detour.GetOriginalFunc());
    if (!orig)
        return;
    if (*autojoin_class && UnassignedClass())
        orig(me, false);
    else
        orig(me, show);
}

static void team_menu_show_panel_hook(void *me, bool show)
{
    using Fn = void (*)(void *, bool);
    auto orig = Fn(team_menu_detour.GetOriginalFunc());
    if (!orig)
        return;
    if (*autojoin_team && UnassignedTeam())
        orig(me, false);
    else
        orig(me, show);
}

static void intro_menu_on_tick_hook(void *me)
{
    using Fn = void (*)(void *);
    auto orig = Fn(intro_menu_detour.GetOriginalFunc());
    if (orig)
        orig(me);
}

static bool allowed_to_party_with_hook(re::CTFPartyClient *this_, uint64_t steamid)
{
    if (*partybypass)
        return true;
    using Fn = bool (*)(re::CTFPartyClient *, uint64_t);
    auto orig = Fn(allowed_party_detour.GetOriginalFunc());
    return orig ? orig(this_, steamid) : false;
}

static uint64_t can_invite_hook(re::CTFPartyClient *this_, uint64_t steamid)
{
    if (*partybypass)
        return 1;
    using Fn = uint64_t (*)(re::CTFPartyClient *, uint64_t);
    auto orig = Fn(can_invite_detour.GetOriginalFunc());
    return orig ? orig(this_, steamid) : 0;
}

static InitRoutine init(
    []()
    {
        EC::Register(EC::CreateMove, update, "cm_autojoin", EC::average);
        EC::Register(EC::Paint, updateSearch, "paint_autojoin", EC::average);
        if (auto addr = gSignatures.GetClientSignature(sigs::party_allowed_to_party_with))
            allowed_party_detour.Init(addr, (void *) allowed_to_party_with_hook);
        if (auto addr = gSignatures.GetClientSignature(sigs::party_can_invite))
            can_invite_detour.Init(addr, (void *) can_invite_hook);
        if (auto addr = gSignatures.GetClientSignature(sigs::class_menu_show_panel))
            class_menu_detour.Init(addr, (void *) class_menu_show_panel_hook);
        if (auto addr = gSignatures.GetClientSignature(sigs::team_menu_show_panel))
            team_menu_detour.Init(addr, (void *) team_menu_show_panel_hook);
        if (auto addr = gSignatures.GetClientSignature(sigs::intro_menu_on_tick))
            intro_menu_detour.Init(addr, (void *) intro_menu_on_tick_hook);
        EC::Register(
            EC::Shutdown,
            []()
            {
                allowed_party_detour.Shutdown();
                can_invite_detour.Shutdown();
                class_menu_detour.Shutdown();
                team_menu_detour.Shutdown();
                intro_menu_detour.Shutdown();
            },
            "shutdown_autojoin");
    });
} // namespace hacks::shared::autojoin
