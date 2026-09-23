/*
 * interfaces.cpp
 *
 *  Created on: Oct 3, 2016
 *      Author: nullifiedcat
 */

#include "common.hpp"
#include "core/sharedobj.hpp"
#include "core/code_scan.hpp"

#include <thread>
#include <unistd.h>
#include <dlfcn.h>
#include <cstdlib>
#include <string>
#include <sstream>

#include <steam/isteamclient.h>
#if ENABLE_VISUALS
#include "sdk/ScreenSpaceEffects.h"
#endif

CModelRender *g_IVModelRender                     = nullptr;
ISteamClient *g_ISteamClient                       = nullptr;
ISteamFriends *g_ISteamFriends                     = nullptr;
ISteamNetworkingSockets *g_ISteamNetworkingSockets = nullptr;
CEngineClient *g_IEngine                       = nullptr;
void *demoplayer                                   = nullptr;
IEngineSound *g_ISoundEngine                       = nullptr;
CMatSystemSurface *g_ISurface                         = nullptr;
CPanel *g_IPanel                             = nullptr;
vgui::ILocalize *g_ILocalize                       = nullptr;
IClientEntityList *g_IEntityList                   = nullptr;
CCvar *g_ICvar                                     = nullptr;
IGameEventManager2 *g_IEventManager2               = nullptr;
CHLClient *g_IBaseClient                      = nullptr;
CEngineTrace *g_ITrace                             = nullptr;
CModelInfoClient *g_IModelInfo                    = nullptr;
CInputSystem *g_IInputSystem                       = nullptr;
CGlobalVarsBase **rg_GlobalVars                    = nullptr;
CPrediction *g_IPrediction                         = nullptr;
IGameMovement *g_IGameMovement                     = nullptr;
IInput *g_IInput                                   = nullptr;
ISteamUser *g_ISteamUser                           = nullptr;
IAchievementMgr *g_IAchievementMgr                 = nullptr;
ISteamUserStats *g_ISteamUserStats                 = nullptr;
IStudioRender *g_IStudioRender                     = nullptr;
IVDebugOverlay *g_IVDebugOverlay                   = nullptr;
IMaterialSystemFixed *g_IMaterialSystem            = nullptr;
CRenderView *g_IVRenderView                       = nullptr;
IMoveHelper *g_IMoveHelperServer                   = nullptr;
CBaseClientState *g_IBaseClientState               = nullptr;
IGameEventManager *g_IGameEventManager             = nullptr;
TFGCClientSystem *g_TFGCClientSystem               = nullptr;
CHud *g_CHUD                                       = nullptr;
CGameRules **rg_pGameRules                         = nullptr;
IEngineVGui *g_IEngineVGui                         = nullptr;
IUniformRandomStream *g_pUniformStream             = nullptr;
int *g_PredictionRandomSeed                        = nullptr;
IFileSystem *g_IFileSystem                         = nullptr;
IMDLCache *g_IMDLCache                             = nullptr;
IToolFrameworkInternal *g_IToolFramework           = nullptr;
CServerTools *g_IServerTools                       = nullptr;

template <typename T> T *MustInterface(const char *version, sharedobj::SharedObject &object)
{
    T *result = reinterpret_cast<T *>(object.CreateInterface(version));
    if (!result)
    {
        logging::Info("FATAL: CreateInterface(%s) failed", version);
        std::exit(1);
    }
    logging::Info("%s = %p", version, result);
    return result;
}

template <typename T> T *TryInterface(const char *version, sharedobj::SharedObject &object)
{
    return reinterpret_cast<T *>(object.CreateInterface(version));
}

static uintptr_t MustClientSig(const char *pat, const char *name)
{
    uintptr_t p = gSignatures.GetClientSignature(pat);
    if (!p)
    {
        logging::Info("FATAL: missing client signature %s", name);
        std::exit(1);
    }
    return p;
}

static uintptr_t MustEngineSig(const char *pat, const char *name)
{
    uintptr_t p = gSignatures.GetEngineSignature(pat);
    if (!p)
    {
        logging::Info("FATAL: missing engine signature %s", name);
        std::exit(1);
    }
    return p;
}

static CGlobalVarsBase **ReadGlobalVarsSlot(void *hud_update)
{
    using namespace cathook::core::memory;
    auto *fn  = static_cast<const uint8_t *>(hud_update);
    auto *end = fn + 0x80;
    for (auto *p = fn; p < end;)
    {
        mem_insn insn{};
        if (!decode_mem_insn(p, end, insn) || insn.size == 0)
        {
            ++p;
            continue;
        }
        if (insn.opcode == 0x8B && insn.rex_w && is_rip_relative(insn) && insn.rip_target)
            return reinterpret_cast<CGlobalVarsBase **>(insn.rip_target);
        p += insn.size;
    }
    return nullptr;
}

static IUniformRandomStream *ResolveUniformStream()
{
    using namespace cathook::core::memory;
    void *fn = dlsym(sharedobj::vstdlib().lmap, "RandomInt");
    if (!fn)
        fn = dlsym(RTLD_DEFAULT, "RandomInt");
    if (!fn)
        return nullptr;
    auto *p = static_cast<uint8_t *>(fn);
    if (p[0] == 0xF3 && p[1] == 0x0F && p[2] == 0x1E && (p[3] == 0xFA || p[3] == 0xFB))
        p += 4;
    auto *end = p + 0x80;
    for (auto *q = p; q < end;)
    {
        mem_insn insn{};
        if (!decode_mem_insn(q, end, insn) || insn.size == 0)
        {
            ++q;
            continue;
        }
        if (insn.rex_w && is_rip_relative(insn) && insn.rip_target)
        {
            if (insn.opcode == 0x8D)
                return reinterpret_cast<IUniformRandomStream *>(insn.rip_target);
            if (insn.opcode == 0x8B)
            {
                auto **slot = reinterpret_cast<IUniformRandomStream **>(insn.rip_target);
                if (slot && *slot)
                    return *slot;
            }
        }
        q += insn.size;
    }
    return nullptr;
}

void ClientState::InitLayout(void *self, const std::uint8_t *lea, const std::uint8_t *force_full, const std::uint8_t *cl_move)
{
    Layout live;
    if (lea && lea[7] == 0x4C && lea[8] == 0x8B && lea[9] == 0x40)
        live.m_NetChannel = lea[10];
    if (force_full && force_full[0] == 0x83 && force_full[1] == 0xBF)
        live.m_nDeltaTick = uint32_t(*reinterpret_cast<const int *>(force_full + 2));
    if (cl_move && self)
    {
        auto *base = reinterpret_cast<char *>(self);
        for (int i = 0; i < 0x400; ++i)
        {
            if (cl_move[i] == 0x89 && cl_move[i + 1] == 0x05 && cl_move[i + 6] == 0xC7 && cl_move[i + 7] == 0x05 && cl_move[i + 12] == 0 && cl_move[i + 13] == 0 && cl_move[i + 14] == 0 && cl_move[i + 15] == 0)
            {
                int rel           = *reinterpret_cast<const int *>(cl_move + i + 2);
                auto *target      = reinterpret_cast<char *>(uintptr_t(cl_move + i + 6) + rel);
                intptr_t off      = target - base;
                if (off > 0x1000 && off < 0x20000)
                {
                    live.lastoutgoingcommand = uint32_t(off);
                    live.chokedcommands      = uint32_t(off + 4);
                    live.last_command_ack    = uint32_t(off + 8);
                    break;
                }
            }
        }
        for (int i = 0; i < 0x400; ++i)
        {
            if (cl_move[i] == 0x83 && cl_move[i + 1] == 0x3D && cl_move[i + 6] == 0x06)
            {
                int rel      = *reinterpret_cast<const int *>(cl_move + i + 2);
                auto *target = reinterpret_cast<char *>(uintptr_t(cl_move + i + 7) + rel);
                intptr_t off = target - base;
                if (off > 0 && off < 0x400)
                {
                    live.m_nSignonState = uint32_t(off);
                    live.m_flNextCmdTime = uint32_t(off + 4);
                    break;
                }
            }
        }
    }
    layout() = live;
    logging::Info("CClientState layout net=%x signon=%x delta=%x lastout=%x choke=%x ack=%x", live.m_NetChannel, live.m_nSignonState, live.m_nDeltaTick, live.lastoutgoingcommand, live.chokedcommands, live.last_command_ack);
}

void CNetChan::InitLayout(const std::uint8_t *get_seq, const std::uint8_t *set_choked, const std::uint8_t *get_rel)
{
    Layout live;
    if (get_seq && get_seq[0] == 0x8B && get_seq[1] == 0x47 && get_seq[3] == 0x89 && get_seq[4] == 0x06 && get_seq[5] == 0x8B && get_seq[6] == 0x47 && get_seq[8] == 0x89 && get_seq[9] == 0x02 && get_seq[10] == 0x8B && get_seq[11] == 0x47)
    {
        live.m_nOutSequenceNr    = get_seq[2];
        live.m_nInSequenceNr     = get_seq[7];
        live.m_nOutSequenceNrAck = get_seq[12];
        live.m_nOutReliableState = live.m_nOutSequenceNrAck + 4;
        live.m_nInReliableState  = live.m_nOutSequenceNrAck + 8;
    }
    if (set_choked && set_choked[0] == 0x83 && set_choked[1] == 0x47 && set_choked[3] == 0x01 && set_choked[4] == 0x83 && set_choked[5] == 0x47 && set_choked[7] == 0x01)
    {
        live.m_nChokedPackets = set_choked[6];
        if (set_choked[2] <= 0x20)
            live.m_nOutSequenceNr = set_choked[2];
    }
    if (get_rel && get_rel[0] == 0x48 && get_rel[1] == 0x85 && get_rel[2] == 0xF6 && get_rel[5] == 0x8B && get_rel[6] == 0x47 && get_rel[15] == 0x8B && get_rel[16] == 0x47 && get_rel[25] == 0x8B && get_rel[26] == 0x47)
    {
        live.m_nOutReliableState = get_rel[7];
        live.m_nInReliableState  = get_rel[17];
        live.m_nChokedPackets    = get_rel[27];
    }
    layout() = live;
    logging::Info("CNetChan layout out=%x in=%x ack=%x outrel=%x inrel=%x choke=%x", live.m_nOutSequenceNr, live.m_nInSequenceNr, live.m_nOutSequenceNrAck, live.m_nOutReliableState, live.m_nInReliableState, live.m_nChokedPackets);
}

extern "C" typedef HSteamPipe (*GetHSteamPipe_t)();
extern "C" typedef HSteamUser (*GetHSteamUser_t)();
extern "C" typedef void *(*SteamInternal_FindOrCreateUserInterface_t)(HSteamUser, const char *);

void CreateEarlyInterfaces()
{
    g_IFileSystem = MustInterface<IFileSystem>("VFileSystem022", sharedobj::filesystem_stdio());
}

void CreateInterfaces()
{
    using cathook::core::memory::resolve_lea_rip;
    using cathook::core::memory::resolve_rip_relative;

    g_ICvar       = MustInterface<CCvar>("VEngineCvar004", sharedobj::vstdlib());
    g_IEngine     = MustInterface<CEngineClient>("VEngineClient014", sharedobj::engine());
    g_ISoundEngine = MustInterface<IEngineSound>("IEngineSoundClient003", sharedobj::engine());
    g_IEntityList = MustInterface<IClientEntityList>("VClientEntityList003", sharedobj::client());
    g_ISteamClient = TryInterface<ISteamClient>("SteamClient017", sharedobj::steamclient());
    if (!g_ISteamClient)
        g_ISteamClient = TryInterface<ISteamClient>("SteamClient019", sharedobj::steamclient());
    if (!g_ISteamClient)
        g_ISteamClient = MustInterface<ISteamClient>("SteamClient018", sharedobj::steamclient());
    g_IEventManager2    = MustInterface<IGameEventManager2>("GAMEEVENTSMANAGER002", sharedobj::engine());
    g_IGameEventManager = TryInterface<IGameEventManager>("GAMEEVENTSMANAGER001", sharedobj::engine());
    g_IBaseClient       = MustInterface<CHLClient>("VClient017", sharedobj::client());
    g_ITrace            = MustInterface<CEngineTrace>("EngineTraceClient003", sharedobj::engine());
    g_IInputSystem      = MustInterface<CInputSystem>("InputSystemVersion001", sharedobj::inputsystem());
    g_IEngineVGui       = MustInterface<IEngineVGui>("VEngineVGui002", sharedobj::engine());

    logging::Info("Initing SteamAPI");
    GetHSteamPipe_t GetHSteamPipe = reinterpret_cast<GetHSteamPipe_t>(dlsym(sharedobj::steamapi().lmap, "SteamAPI_GetHSteamPipe"));
    HSteamPipe sp                 = GetHSteamPipe ? GetHSteamPipe() : 0;
    if (!sp)
        sp = g_ISteamClient->CreateSteamPipe();
    GetHSteamUser_t GetHSteamUser = reinterpret_cast<GetHSteamUser_t>(dlsym(sharedobj::steamapi().lmap, "SteamAPI_GetHSteamUser"));
    HSteamUser su                 = GetHSteamUser ? GetHSteamUser() : 0;
    if (!su)
        su = g_ISteamClient->ConnectToGlobalUser(sp);
    logging::Info("Inited SteamAPI pipe=%d user=%d", int(sp), int(su));

    auto find_or_create = reinterpret_cast<SteamInternal_FindOrCreateUserInterface_t>(dlsym(sharedobj::steamapi().lmap, "SteamInternal_FindOrCreateUserInterface"));
    if (find_or_create)
        g_ISteamNetworkingSockets = (ISteamNetworkingSockets *) find_or_create(su, "SteamNetworkingSockets009");

    g_IVModelRender = MustInterface<CModelRender>("VEngineModel016", sharedobj::engine());
    g_ISteamFriends = g_ISteamClient->GetISteamFriends(su, sp, "SteamFriends017");
    if (!g_ISteamFriends)
        g_ISteamFriends = g_ISteamClient->GetISteamFriends(su, sp, "SteamFriends015");
    if (!g_ISteamFriends)
        g_ISteamFriends = g_ISteamClient->GetISteamFriends(su, sp, "SteamFriends002");

    auto **client_vt = *reinterpret_cast<void ***>(g_IBaseClient);
    rg_GlobalVars    = ReadGlobalVarsSlot(client_vt[11]);
    if (!rg_GlobalVars)
    {
        logging::Info("FATAL: CGlobalVars slot missing");
        std::exit(1);
    }

    g_IPrediction   = MustInterface<CPrediction>("VClientPrediction001", sharedobj::client());
    g_IGameMovement = MustInterface<IGameMovement>("GameMovement001", sharedobj::client());

    {
        auto *storage = reinterpret_cast<IInput **>(resolve_lea_rip(reinterpret_cast<void *>(MustClientSig(sigs::input, "CInput"))));
        g_IInput      = storage ? *storage : nullptr;
        if (!g_IInput)
        {
            logging::Info("FATAL: CInput missing");
            std::exit(1);
        }
        auto cmd_off = InputCommandsOffset(g_IInput);
        if (cmd_off <= 0)
        {
            logging::Info("FATAL: CInput m_pCommands missing");
            std::exit(1);
        }
        logging::Info("CInput %p commands=%tx", g_IInput, cmd_off);
    }

    g_ISteamUser = g_ISteamClient->GetISteamUser(su, sp, "SteamUser021");
    if (!g_ISteamUser)
        g_ISteamUser = g_ISteamClient->GetISteamUser(su, sp, "SteamUser018");
    g_IModelInfo = MustInterface<CModelInfoClient>("VModelInfoClient006", sharedobj::engine());

    g_IBaseClientState = reinterpret_cast<CBaseClientState *>(resolve_lea_rip(reinterpret_cast<void *>(MustEngineSig(sigs::client_state, "CClientState"))));
    ClientState::InitLayout(g_IBaseClientState, reinterpret_cast<uint8_t *>(gSignatures.GetEngineSignature(sigs::client_state)), reinterpret_cast<uint8_t *>(gSignatures.GetEngineSignature(sigs::client_state_force_full_update)), reinterpret_cast<uint8_t *>(gSignatures.GetEngineSignature(sigs::cl_move)));
    CNetChan::InitLayout(reinterpret_cast<uint8_t *>(gSignatures.GetEngineSignature(sigs::netchan_get_sequence_data)), reinterpret_cast<uint8_t *>(gSignatures.GetEngineSignature(sigs::netchan_set_choked)), reinterpret_cast<uint8_t *>(gSignatures.GetEngineSignature(sigs::netchan_get_reliable)));
    logging::Info("CClientState %p", g_IBaseClientState);

    {
        auto *insn = reinterpret_cast<uint8_t *>(MustEngineSig(sigs::demo_player, "demoplayer"));
        auto **slot = reinterpret_cast<void **>(resolve_rip_relative(insn + 38, 3, 7));
        demoplayer  = slot ? *slot : nullptr;
        logging::Info("demoplayer %p", demoplayer);
    }

    g_IAchievementMgr = g_IEngine->GetAchievementMgr();
    g_ISteamUserStats = g_ISteamClient->GetISteamUserStats(su, sp, "STEAMUSERSTATS_INTERFACE_VERSION012");
    if (!g_ISteamUserStats)
        g_ISteamUserStats = g_ISteamClient->GetISteamUserStats(su, sp, "STEAMUSERSTATS_INTERFACE_VERSION011");

    g_PredictionRandomSeed = reinterpret_cast<int *>(resolve_lea_rip(reinterpret_cast<void *>(MustClientSig(sigs::random_seed, "g_PredictionRandomSeed"))));

    rg_pGameRules = reinterpret_cast<CGameRules **>(resolve_lea_rip(reinterpret_cast<void *>(MustClientSig(sigs::gamerules_recvproxy, "g_pGameRules"))));

    {
        auto *storage          = reinterpret_cast<IMoveHelper **>(resolve_lea_rip(reinterpret_cast<void *>(MustClientSig(sigs::move_helper, "CMoveHelper"))));
        g_IMoveHelperServer    = storage ? *storage : nullptr;
        logging::Info("CMoveHelper %p", g_IMoveHelperServer);
    }

    g_IMaterialSystem = MustInterface<IMaterialSystemFixed>("VMaterialSystem082", sharedobj::materialsystem());
    g_IMDLCache       = TryInterface<IMDLCache>("MDLCache004", sharedobj::datacache());
    g_IPanel          = MustInterface<CPanel>("VGUI_Panel009", sharedobj::vgui2());
    g_ILocalize       = TryInterface<vgui::ILocalize>("VGUI_Localize005", sharedobj::vgui2());
    g_pUniformStream  = ResolveUniformStream();
    logging::Info("IUniformRandomStream %p", g_pUniformStream);
    g_IToolFramework = TryInterface<IToolFrameworkInternal>("VTOOLFRAMEWORKVERSION002", sharedobj::engine());

#if ENABLE_VISUALS
    g_IVDebugOverlay    = MustInterface<IVDebugOverlay>("VDebugOverlay003", sharedobj::engine());
    g_ISurface          = MustInterface<CMatSystemSurface>("VGUI_Surface030", sharedobj::vguimatsurface());
    g_IStudioRender     = TryInterface<IStudioRender>("VStudioRender025", sharedobj::studiorender());
    g_IVRenderView      = MustInterface<CRenderView>("VEngineRenderView014", sharedobj::engine());

    {
        auto *lea = reinterpret_cast<void *>(MustClientSig(sigs::screenspace_effects_manager, "g_pScreenSpaceEffects"));
        auto **slot = reinterpret_cast<IScreenSpaceEffectManager **>(resolve_lea_rip(lea));
        g_pScreenSpaceEffects = slot ? *slot : nullptr;
        auto *head_insn = reinterpret_cast<uint8_t *>(MustClientSig(sigs::screenspace_registration_head, "screenspace head"));
        g_ppScreenSpaceRegistrationHead = reinterpret_cast<CScreenSpaceEffectRegistration **>(resolve_rip_relative(head_insn + 10, 3, 7));
        logging::Info("ScreenSpaceEffects %p head %p", g_pScreenSpaceEffects, g_ppScreenSpaceRegistrationHead);
    }
#endif

    {
        auto *insn = reinterpret_cast<uint8_t *>(MustClientSig(sigs::hud_instance_from_chat, "CHud"));
        g_CHUD     = reinterpret_cast<CHud *>(resolve_rip_relative(insn + 18, 3, 7));
        logging::Info("CHud %p", g_CHUD);
    }

    static_assert(sizeof(player_info_s) == 0x84, "player_info_s must match CEngineClient memcpy 0x84");
}
