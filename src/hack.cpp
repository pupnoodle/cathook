/*
 * hack.cpp
 *
 *  Created on: Oct 3, 2016
 *      Author: nullifiedcat
 */

#define __USE_GNU
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>
#include <atomic>
#include <thread>
#include <boost/stacktrace.hpp>
#include <cxxabi.h>
#include <visual/SDLHooks.hpp>
#include "hack.hpp"
#include "common.hpp"
#include "MiscTemporary.hpp"
#include "DetourHook.hpp"
#if ENABLE_GUI
#include "menu/GuiInterface.hpp"
#endif
#include <link.h>
#include <pwd.h>

#include <hacks/hacklist.hpp>
#include "teamroundtimer.hpp"
#if EXTERNAL_DRAWING
#include "xoverlay.h"
#endif
#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)

#include "version.h"
#include <cxxabi.h>

/*
 *  Credits to josh33901 aka F1ssi0N for butifel F1Public and Darkstorm 2015
 * Linux
 */

// game_shutdown = Is full game shutdown or just detach
bool hack::game_shutdown = true;
bool hack::shutdown      = false;
bool hack::initialized   = false;

const std::string &hack::GetVersion()
{
    static std::string version("Unknown Version");
    static bool version_set = false;
    if (version_set)
        return version;
#if defined(GIT_COMMIT_HASH) && defined(GIT_COMMITTER_DATE)
    version = "Version: #" GIT_COMMIT_HASH " " GIT_COMMITTER_DATE;
#endif
    version_set = true;
    return version;
}

const std::string &hack::GetType()
{
    static std::string version("Unknown Type");
    static bool version_set = false;
    if (version_set)
        return version;
    version = "";
#if not ENABLE_IPC
    version += " NOIPC";
#endif
#if not ENABLE_GUI
    version += " NOGUI";
#else
    version += " GUI";
#endif

#ifndef DYNAMIC_CLASSES

#ifdef GAME_SPECIFIC
    version += " GAME " TO_STRING(GAME);
#else
    version += " UNIVERSAL";
#endif

#else
    version += " DYNAMIC";
#endif

#if not ENABLE_VISUALS
    version += " NOVISUALS";
#endif

    version     = version.substr(1);
    version_set = true;
    return version;
}

std::mutex hack::command_stack_mutex;
std::stack<std::string> &hack::command_stack()
{
    static std::stack<std::string> stack;
    return stack;
}

void hack::ExecuteCommand(const std::string &command)
{
    std::lock_guard<std::mutex> guard(hack::command_stack_mutex);
    hack::command_stack().push(command);
}

extern "C" __attribute__((visibility("default"))) void ch_exec(const char *cmd)
{
    if (cmd && *cmd)
        hack::ExecuteCommand(cmd);
}

#if ENABLE_LOGGING

std::string getFileName(std::string filePath)
{
    // Get last dot position
    std::size_t dotPos = filePath.rfind('.');
    std::size_t sepPos = filePath.rfind('/');

    if (sepPos != std::string::npos)
    {
        return filePath.substr(sepPos + 1, filePath.size() - (dotPos != std::string::npos ? 1 : dotPos));
    }
    return filePath;
}

static pthread_t g_prof_main_thread;
static void prof_dump_handler(int)
{
    void *bt[32];
    int n = backtrace(bt, 32);
    char hdr[64];
    int hl = snprintf(hdr, sizeof(hdr), "===STACK===\n");
    write(2, hdr, hl);
    for (int i = 0; i < n; ++i)
    {
        Dl_info di;
        char buf[256];
        if (dladdr(bt[i], &di) && di.dli_fname)
        {
            uintptr_t off   = uintptr_t(bt[i]) - uintptr_t(di.dli_fbase);
            const char *bas = strrchr(di.dli_fname, '/');
            int len         = snprintf(buf, sizeof(buf), "%s+0x%lx\n", bas ? bas + 1 : di.dli_fname, (unsigned long) off);
            write(2, buf, len);
        }
    }
}

void prof_arm_main_thread()
{
    static std::atomic<bool> armed{ false };
    if (armed.exchange(true))
        return;
    g_prof_main_thread = pthread_self();
    struct sigaction sa
    {
    };
    sa.sa_handler = prof_dump_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR2, &sa, nullptr);
    std::thread(
        []
        {
            while (true)
            {
                usleep(300000);
                pthread_kill(g_prof_main_thread, SIGUSR2);
            }
        })
        .detach();
}

void critical_error_handler(int signum)
{
    namespace st = boost::stacktrace;
    ::signal(SIGSEGV, SIG_DFL);
    ::signal(SIGABRT, SIG_DFL);
    passwd *pwd = getpwuid(getuid());
    std::ofstream out(strfmt("/tmp/cathook-%s-%d-segfault.log", pwd->pw_name, getpid()).get());

    Dl_info info;
    if (!dladdr(reinterpret_cast<void *>(hack::ExecuteCommand), &info))
        return;

    for (auto i : st::stacktrace())
    {
        Dl_info info2;
        if (dladdr(i.address(), &info2))
        {
            uintptr_t offset = uintptr_t(i.address()) - uintptr_t(info2.dli_fbase);
            out << (!strcmp(info2.dli_fname, info.dli_fname) ? "cathook" : info2.dli_fname) << '\t' << (void *) offset << std::endl;
        }
    }

    out.close();
    ::raise(SIGABRT);
}
#endif

namespace
{
constexpr const char *server_nav_collect_sig =
    "55 48 89 E5 41 56 49 89 F6 41 55 41 54 53 48 8D 9F 60 0C 00 00 48 83 EC 10 83 FA 02 74 ? 83 FA 03 74 ? 48 83 C4 10 5B 41 5C 41 5D 41 5E 5D C3 48 8D 9F 80 0C 00 00";

using NavVectorInsert_t = void (*)(uintptr_t vec, unsigned int index, uintptr_t *value);

DetourHook server_nav_collect_detour;
NavVectorInsert_t server_nav_insert = nullptr;
uintptr_t server_nav_areas          = 0;
bool server_nav_collect_hooked      = false;

void NavCollectConnectedAreas(uintptr_t navmesh, uintptr_t out, int team)
{
    uintptr_t list;
    if (team == 2)
        list = navmesh + 0xC60;
    else if (team == 3)
        list = navmesh + 0xC80;
    else
        return;

    auto areas = *reinterpret_cast<uintptr_t **>(server_nav_areas);
    for (int i = 0; i < *reinterpret_cast<int *>(list + 0x10); ++i)
    {
        uintptr_t area  = areas[i];
        uintptr_t best  = 0;
        float best_score = 0.0f;
        if (!area)
            continue;
        for (auto slot = reinterpret_cast<uintptr_t *>(area + 0x68); slot != reinterpret_cast<uintptr_t *>(area + 0x88); ++slot)
        {
            auto header = reinterpret_cast<int *>(*slot);
            if (!header)
                continue;
            for (int j = 0; j < *header; ++j)
            {
                uintptr_t conn = *reinterpret_cast<uintptr_t *>(&header[4 * j + 2]);
                if (conn < (uintptr_t(1) << 40))
                    continue;
                if (*reinterpret_cast<uint8_t *>(conn + 0x29C) & 0xE)
                    continue;
                float score = (*reinterpret_cast<float *>(conn + 0x18) - *reinterpret_cast<float *>(conn + 0xC)) *
                              (*reinterpret_cast<float *>(conn + 0x14) - *reinterpret_cast<float *>(conn + 0x8));
                if (score > best_score)
                {
                    best       = conn;
                    best_score = score;
                }
            }
        }
        if (best)
            server_nav_insert(out, *reinterpret_cast<unsigned int *>(out + 0x10), &best);
    }
}

void InstallServerNavCrashFix()
{
    if (server_nav_collect_hooked)
        return;
    if (!sharedobj::server().Load(false))
        return;
    uintptr_t fn = gSignatures.GetServerSignature(server_nav_collect_sig);
    if (!fn)
        return;
    server_nav_areas  = fn + 0x45 + *reinterpret_cast<int32_t *>(fn + 0x41);
    server_nav_insert = reinterpret_cast<NavVectorInsert_t>(fn + 0xE1 + *reinterpret_cast<int32_t *>(fn + 0xDD));
    server_nav_collect_detour.Init(fn, reinterpret_cast<void *>(&NavCollectConnectedAreas));
    server_nav_collect_hooked = server_nav_collect_detour.GetOriginalFunc() != nullptr;
    if (server_nav_collect_hooked)
        logging::Info("Installed server nav crash fix at %p", reinterpret_cast<void *>(fn));
}
}

static void InitRandom()
{
    int rand_seed;
    FILE *rnd = fopen("/dev/urandom", "rb");
    if (!rnd || fread(&rand_seed, sizeof(rand_seed), 1, rnd) < 1)
    {
        logging::Info("Warning!!! Failed read from /dev/urandom (%s). Randomness is going to be weak", strerror(errno));
        timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        rand_seed = t.tv_nsec ^ (t.tv_sec & getpid());
    }
    srand(rand_seed);
    if (rnd)
        fclose(rnd);
}

void hack::Hook()
{
    uintptr_t *clientMode = nullptr;
    void **storage        = nullptr;
    auto *hud_input       = static_cast<const uint8_t *>((*(void ***) g_IBaseClient)[vtables::client_dll::hud_process_input]);
    if (hud_input && hud_input[0] == 0x48 && hud_input[1] == 0x8d && hud_input[2] == 0x05)
        storage = static_cast<void **>(cathook::core::memory::resolve_lea_rip(hud_input));
    if (!storage)
    {
        auto sig = gSignatures.GetClientSignature(sigs::client_mode_shared);
        storage  = static_cast<void **>(cathook::core::memory::resolve_lea_rip(reinterpret_cast<void *>(sig)));
    }
    while (!storage || !(clientMode = static_cast<uintptr_t *>(*storage)))
        usleep(10000);
    logging::Info("ClientModeShared %p", clientMode);
    hooks::clientmode.Set((void *) clientMode);
    hooks::clientmode.HookMethod(HOOK_ARGS(CreateMove));
#if ENABLE_VISUALS
    hooks::clientmode.HookMethod(HOOK_ARGS(OverrideView));
#endif
    hooks::clientmode.HookMethod(HOOK_ARGS(LevelInit));
    hooks::clientmode.HookMethod(HOOK_ARGS(LevelShutdown));
    hooks::clientmode.Apply();

    hooks::clientmode4.Set((void *) (clientMode), vtables::client_mode::listener_vptr_offset);
    hooks::clientmode4.HookMethod(HOOK_ARGS(FireGameEvent));
    hooks::clientmode4.Apply();

    hooks::client.Set(g_IBaseClient);
    hooks::client.HookMethod(HOOK_ARGS(DispatchUserMessage));
#if ENABLE_VISUALS
    hooks::client.HookMethod(HOOK_ARGS(FrameStageNotify));
    hooks::client.HookMethod(HOOK_ARGS(IN_KeyEvent));
#endif
    hooks::client.Apply();

#if ENABLE_VISUALS || ENABLE_TEXTMODE
    hooks::panel.Set(g_IPanel);
    hooks::panel.HookMethod(hooked_methods::methods::PaintTraverse, offsets::PaintTraverse(), &hooked_methods::original::PaintTraverse);
    hooks::panel.Apply();
#endif

    if (g_pUniformStream)
    {
        hooks::vstd.Set((void *) g_pUniformStream);
        hooks::vstd.HookMethod(HOOK_ARGS(RandomInt));
        hooks::vstd.Apply();
    }
#if ENABLE_VISUALS

    CHudElement *chat_hud = nullptr;
    if (g_CHUD)
    {
        for (int i = 0; i < 10000 && !(chat_hud = g_CHUD->FindElement("CHudChat")); ++i)
            usleep(1000);
    }
    if (chat_hud)
    {
        hooks::chathud.Set(chat_hud);
        hooks::chathud.HookMethod(HOOK_ARGS(StartMessageMode));
        hooks::chathud.HookMethod(HOOK_ARGS(StopMessageMode));
        hooks::chathud.HookMethod(HOOK_ARGS(ChatPrintf));
        hooks::chathud.Apply();
    }
    else
        logging::Info("CHudChat missing, skipping chat hooks");
#endif

    hooks::input.Set(g_IInput);
    hooks::input.HookMethod(HOOK_ARGS(GetUserCmd));
    hooks::input.HookMethod(HOOK_ARGS(CreateMoveInput));
    hooks::input.Apply();

#if ENABLE_VISUALS || ENABLE_TEXTMODE
    hooks::modelrender.Set(g_IVModelRender);
    hooks::modelrender.HookMethod(HOOK_ARGS(DrawModelExecute));
    hooks::modelrender.Apply();
#endif
    hooks::enginevgui.Set(g_IEngineVGui);
    hooks::enginevgui.HookMethod(HOOK_ARGS(Paint));
    hooks::enginevgui.Apply();

    hooks::engine.Set(g_IEngine);
    hooks::engine.HookMethod(HOOK_ARGS(ServerCmdKeyValues));
    hooks::engine.HookMethod(HOOK_ARGS(IsPlayingTimeDemo));
    hooks::engine.Apply();

    hooks::eventmanager2.Set(g_IEventManager2);
    hooks::eventmanager2.HookMethod(HOOK_ARGS(FireEvent));
    hooks::eventmanager2.HookMethod(HOOK_ARGS(FireEventClientSide));
    hooks::eventmanager2.Apply();

    if (g_ISteamFriends)
    {
        hooks::steamfriends.Set(g_ISteamFriends);
        hooks::steamfriends.HookMethod(HOOK_ARGS(GetFriendPersonaName));
        hooks::steamfriends.Apply();
    }

    hooks::soundclient.Set(g_ISoundEngine);
    hooks::soundclient.HookMethod(HOOK_ARGS(EmitSound1));
    hooks::soundclient.HookMethod(HOOK_ARGS(EmitSound2));
    hooks::soundclient.HookMethod(HOOK_ARGS(EmitSound3));
    hooks::soundclient.Apply();

    hooks::prediction.Set(g_IPrediction);
    hooks::prediction.HookMethod(HOOK_ARGS(RunCommand));
    hooks::prediction.Apply();

    if (g_IToolFramework)
    {
        hooks::toolbox.Set(g_IToolFramework);
        hooks::toolbox.HookMethod(HOOK_ARGS(Think));
        hooks::toolbox.Apply();
    }

#if ENABLE_VISUALS
    sdl_hooks::applySdlHooks();
#endif

    // FIXME [MP]
    logging::Info("Hooked!");
}

void hack::Initialize()
{
#if ENABLE_LOGGING
    ::signal(SIGSEGV, &critical_error_handler);
    ::signal(SIGABRT, &critical_error_handler);
#endif
    time_injected = time(nullptr);
/*passwd *pwd   = getpwuid(getuid());
char *logname = strfmt("/tmp/cathook-game-stdout-%s-%u.log", pwd->pw_name,
time_injected);
freopen(logname, "w", stdout);
free(logname);
logname = strfmt("/tmp/cathook-game-stderr-%s-%u.log", pwd->pw_name,
time_injected);
freopen(logname, "w", stderr);
free(logname);*/
// Essential files must always exist, except when the game is running in text
// mode.
#if ENABLE_VISUALS

    {
        std::vector<std::string> essential = { "fonts/tf2build.ttf" };
        for (const auto &s : essential)
        {
            std::ifstream exists(paths::getDataPath("/" + s), std::ios::in);
            if (not exists)
            {
                Error(("Missing essential file: " + s +
                       "/%s\nYou MUST run install-data script to finish "
                       "installation")
                          .c_str(),
                      s.c_str());
            }
        }
    }

#endif /* TEXTMODE */
    logging::Info("Initializing...");
    InitRandom();
    sharedobj::LoadLauncher();

    // remove epic source lock (needed for non-preload tf2)
    std::remove("/tmp/source_engine_2925226592.lock");

    sharedobj::LoadEarlyObjects();

    CreateEarlyInterfaces();

    // Applying the defaults needs to be delayed, because preloaded Cathook can not properly convert SDL codes to names before TF2 init
    settings::Manager::instance().applyDefaults();

    logging::Info("Clearing Early initializer stack");
    while (!init_stack_early().empty())
    {
        init_stack_early().top()();
        init_stack_early().pop();
    }
    logging::Info("Early Initializer stack done");
    sharedobj::LoadAllSharedObjects();
    CreateInterfaces();
    InitClassTable();
    EC::Register(EC::LevelInit, InitClassTable, "classinfo_levelinit", EC::very_early);
    EC::Register(EC::FirstCM, InitClassTable, "classinfo_firstcm", EC::very_early);
    EC::Register(EC::LevelInit, InstallServerNavCrashFix, "server_nav_crashfix");
    InstallServerNavCrashFix();

    BeginConVars();
    g_Settings.Init();
    EndConVars();

#if ENABLE_VISUALS
    draw::Initialize();
#if ENABLE_GUI
// FIXME put gui here
#endif

#endif /* TEXTMODE */

    gNetvars.init();
    InitNetVars();
    g_pLocalPlayer    = new LocalPlayer();
    g_pPlayerResource = new TFPlayerResource();
    g_pTeamRoundTimer = new CTeamRoundTimer();

    velocity::Init();
    playerlist::Load();

#if ENABLE_VISUALS
    InitStrings();
#endif /* TEXTMODE */
    logging::Info("Clearing initializer stack");
    while (!init_stack().empty())
    {
        init_stack().top()();
        init_stack().pop();
    }
    logging::Info("Initializer stack done");
#if ENABLE_TEXTMODE
    hack::command_stack().push("exec cat_autoexec_textmode");
#else
    hack::command_stack().push("exec cat_autoexec");
#endif
    auto extra_exec = std::getenv("CH_EXEC");
    if (extra_exec)
        hack::command_stack().push(extra_exec);

    hack::initialized = true;
    for (int i = 0; i <= re::ITFMatchGroupDescription::layout().table_max; i++)
    {
        re::ITFMatchGroupDescription *desc = re::GetMatchGroupDescription(i);
        if (!desc || desc->m_iID() > 9) // ID's over 9 are invalid
            continue;
        if (desc->m_bForceCompetitiveSettings())
        {
            desc->m_bForceCompetitiveSettings() = false;
            logging::Info("Bypassed force competitive cvars!");
        }
    }
    hack::Hook();
}

void hack::Think()
{
    usleep(250000);
}

void hack::Shutdown()
{
    if (hack::shutdown)
        return;
    hack::shutdown = true;
    // Stop cathook stuff
    settings::cathook_disabled.store(true);
    playerlist::Save();
#if ENABLE_VISUALS
    sdl_hooks::cleanSdlHooks();
#endif
    logging::Info("Unregistering convars..");
    ConVar_Unregister();
    logging::Info("Unloading sharedobjects..");
    sharedobj::UnloadAllSharedObjects();
    logging::Info("Deleting global interfaces...");
    delete g_pLocalPlayer;
    delete g_pTeamRoundTimer;
    delete g_pPlayerResource;
#if ENABLE_GUI
    logging::Info("Shutting down GUI");
    gui::shutdown();
#endif
    if (!hack::game_shutdown)
    {
        logging::Info("Running shutdown handlers");
        EC::run(EC::Shutdown);
#if ENABLE_VISUALS
        g_pScreenSpaceEffects->DisableScreenSpaceEffect("_cathook_glow");
#if EXTERNAL_DRAWING
        xoverlay_destroy();
#endif
#endif
    }
    logging::Info("Releasing VMT hooks..");
    hooks::ReleaseAllHooks();
    logging::Info("Success..");
}
