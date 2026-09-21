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
#include "hacks/AutoParty.hpp"

namespace hacks::shared::autojoin
{
static settings::Boolean autojoin_team{ "autojoin.team", "false" };
static settings::Int autojoin_class{ "autojoin.class", "0" };
static settings::Boolean auto_queue{ "autojoin.auto-queue", "false" };
static settings::Boolean auto_requeue{ "autojoin.auto-requeue", "false" };
static settings::Boolean auto_accept_q{ "autojoin.auto-accept-q", "false" };
static settings::Boolean partybypass{ "hack.party-bypass", "false" };

// Protocol SO type IDs from EGCTFProtoObjectTypes (tf_gcmessages.h)
constexpr int k_SO_GameServerLobby = 2004;
constexpr int k_SO_LobbyInvite     = 2008;
constexpr int k_SOChange_Create    = 0;

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

    static Timer stuck_connect{};
    static bool tracking_stuck = false;
    re::CTFGCClientSystem *gc = re::CTFGCClientSystem::GTFGCClientSystem();
    re::CTFPartyClient *pc    = re::CTFPartyClient::GTFPartyClient();
    const bool live_match     = gc && gc->BHaveLiveMatch();
    if (g_IEngine->IsConnected() && !tfmm::isLoadingMap() && !live_match)
    {
        if (!tracking_stuck)
        {
            stuck_connect.update();
            tracking_stuck = true;
        }
        else if (stuck_connect.test_and_set(25000))
        {
            logging::Info("autojoin: stuck connecting (FakeIP/SDR), disconnecting");
            g_IEngine->ClientCmd_Unrestricted("disconnect");
            tracking_stuck = false;
        }
    }
    else
        tracking_stuck = false;

    if (tfmm::shouldHoldQueueForMapLoad())
    {
#if not ENABLE_VISUALS
        queue_time.update();
#endif
        return;
    }

    int invites = pc ? pc->GetPendingInvites() : 0;

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

    if (auto_requeue && !*auto_queue)
    {
        if (startqueue_timer.check(15000) && gc && !gc->BConnectedToMatchServer(false) && !gc->BHaveLiveMatch() && !invites)
            if (pc && !(pc->BInQueueForMatchGroup(tfmm::getQueue()) || pc->BInQueueForStandby()))
            {
                logging::Info("Starting queue for standby, Invites %d", invites);
                tfmm::startQueueStandby();
            }
    }

    if (auto_queue)
    {
        if (gc && gc->BHaveLiveMatch() && !g_IEngine->IsInGame() && !tfmm::isLoadingMap())
        {
            static Timer abandon_stuck{};
            static bool tracking_abandon = false;
            if (!tracking_abandon)
            {
                abandon_stuck.update();
                tracking_abandon = true;
            }
            else if (abandon_stuck.test_and_set(45000))
            {
                logging::Info("autojoin: abandoning stuck live match");
                tfmm::abandon();
                tracking_abandon = false;
            }
        }
        const bool in_queue = pc && (pc->BInQueueForMatchGroup(tfmm::getQueue()) || pc->BInQueueForStandby());
        static Timer last_queue_sent{};
        static Timer queue_status{};
        if (queue_status.test_and_set(15000))
            logging::Info("autojoin: inqueue=%d live=%d invites=%d", (int) in_queue, (int) live_match, invites);
        if (in_queue)
            last_queue_sent.update();
        else if (last_queue_sent.check(180000) && gc && !gc->BConnectedToMatchServer(false) && !gc->BHaveLiveMatch() && !invites)
        {
            logging::Info("Starting queue, Invites %d", invites);
            tfmm::startQueue();
            last_queue_sent.update();
        }
    }
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
    if (!autoteam_timer.test_and_set(750))
        return;
    if (!g_IEngine->IsInGame() || CE_BAD(LOCAL_E))
        return;

    const int want_class = int(autojoin_class);
    // Class select implies joining a team first; the team checkbox still works alone.
    if ((*autojoin_team || want_class) && UnassignedTeam())
    {
        g_IEngine->ClientCmd_Unrestricted("team_ui_setup");
        g_IEngine->ClientCmd_Unrestricted("menuopen");
        g_IEngine->ClientCmd_Unrestricted("autoteam");
        g_IEngine->ClientCmd_Unrestricted("menuclosed");
        return;
    }
    if (want_class && want_class < 10 && (UnassignedClass() || !LOCAL_E->m_bAlivePlayer()))
    {
        g_IEngine->ClientCmd_Unrestricted(format("joinclass ", classnames[want_class - 1]).c_str());
        g_IEngine->ClientCmd_Unrestricted("menuclosed");
    }
}

void onShutdown()
{
    if (auto_queue && !tfmm::shouldHoldQueueForMapLoad())
        tfmm::startQueue();
}

static CatCommand get_steamid("print_steamid", "Prints your SteamID", []() { g_ICvar->ConsoleColorPrintf(MENU_COLOR, "%u\n", g_ISteamUser->GetSteamID().GetAccountID()); });

static DetourHook allowed_party_detour;
static DetourHook can_invite_detour;
static DetourHook class_menu_detour;
static DetourHook team_menu_detour;
static DetourHook intro_menu_detour;
static DetourHook so_changed_detour;

static int shared_object_type(void *obj)
{
    static const int voff = []() -> int {
        auto *code = reinterpret_cast<uint8_t *>(gSignatures.GetClientSignature(sigs::tf_gc_client_system_so_event));
        if (code)
        {
            for (int i = 0; i < 64; ++i)
            {
                if (code[i] == 0xFF && code[i + 1] == 0x50 && code[i + 3] == 0x3D && code[i + 4] == 0xD4 && code[i + 5] == 0x07)
                    return code[i + 2];
            }
        }
        return -1;
    }();
    if (!obj || voff < 0)
        return 0;
    auto vtable = *reinterpret_cast<uintptr_t *>(obj);
    auto fn     = *reinterpret_cast<int (**)(void *)>(vtable + voff);
    return fn ? fn(obj) : 0;
}

static uint64_t lobby_invite_group_id(void *obj)
{
    static const int voff = []() -> int {
        auto *code = reinterpret_cast<uint8_t *>(gSignatures.GetClientSignature(sigs::tf_gc_client_system_get_match_invite));
        if (code)
        {
            for (int i = 0; i < 0x80; ++i)
            {
                if (code[i] == 0x48 && code[i + 1] == 0x8B && code[i + 2] == 0x03 && code[i + 3] == 0x48 && code[i + 4] == 0x8B && code[i + 5] == 0x40)
                    return code[i + 6];
            }
        }
        return -1;
    }();
    if (!obj || voff < 0)
        return 0;
    auto vtable = *reinterpret_cast<uintptr_t *>(obj);
    auto fn     = *reinterpret_cast<uint64_t (**)(void *)>(vtable + voff);
    return fn ? fn(obj) : 0;
}

static uintptr_t so_changed_hook(re::CTFGCClientSystem *this_, void *obj, int change)
{
    using Fn = uintptr_t (*)(re::CTFGCClientSystem *, void *, int);
    auto orig = Fn(so_changed_detour.GetOriginalFunc());
    if (!orig)
        return 0;
    if (!*auto_accept_q || !obj || change != k_SOChange_Create)
        return orig(this_, obj, change);

    const int type = shared_object_type(obj);
    if (type == k_SO_LobbyInvite)
    {
        if (uint64_t id = lobby_invite_group_id(obj))
            this_->RequestAcceptMatchInvite(id);
        return orig(this_, obj, change);
    }

    auto result = orig(this_, obj, change);
    if (type == k_SO_GameServerLobby)
        this_->JoinMMMatch();
    return result;
}

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
    if (*partybypass || hacks::tf2::autoparty::AllowPartyWithSteamID(steamid))
        return true;
    using Fn = bool (*)(re::CTFPartyClient *, uint64_t);
    auto orig = Fn(allowed_party_detour.GetOriginalFunc());
    return orig ? orig(this_, steamid) : false;
}

static uint64_t can_invite_hook(re::CTFPartyClient *this_, uint64_t steamid)
{
    if (*partybypass || hacks::tf2::autoparty::AllowPartyWithSteamID(steamid))
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
        if (auto addr = gSignatures.GetClientSignature(sigs::tf_gc_client_system_so_event))
            so_changed_detour.Init(addr, (void *) so_changed_hook);
        EC::Register(
            EC::Shutdown,
            []()
            {
                allowed_party_detour.Shutdown();
                can_invite_detour.Shutdown();
                class_menu_detour.Shutdown();
                team_menu_detour.Shutdown();
                intro_menu_detour.Shutdown();
                so_changed_detour.Shutdown();
            },
            "shutdown_autojoin");
    });
} // namespace hacks::shared::autojoin
