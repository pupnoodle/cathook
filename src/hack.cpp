/*
 * hack.cpp
 *
 *  Created on: Oct 3, 2016
 *      Author: nullifiedcat
 */

#define __USE_GNU
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/uio.h>
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
#include <ucontext.h>

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

namespace hack
{
void PumpEngine()
{
    if (!initialized || !g_IEngine)
        return;

#if ENABLE_IPC
    CNetChan *ch = g_IEngine->GetNetChannelInfo();
    if (ch && !hooks::netchannel.IsHooked(reinterpret_cast<void *>(ch)))
    {
        hooks::netchannel.Set(ch);
        hooks::netchannel.HookMethod(HOOK_ARGS(SendDatagram));
        hooks::netchannel.HookMethod(HOOK_ARGS(CanPacket));
        hooks::netchannel.HookMethod(HOOK_ARGS(SendNetMsg));
        hooks::netchannel.HookMethod(HOOK_ARGS(Shutdown));
        hooks::netchannel.Apply();
        ipc::UpdateServerAddress();
    }
    static Timer nametimer{};
    if (nametimer.test_and_set(1000 * 10) && ipc::peer)
        ipc::StoreClientData();
    static Timer ipc_timer{};
    if (ipc_timer.test_and_set(1000) && ipc::peer)
    {
        if (ipc::peer->HasCommands())
            ipc::peer->ProcessCommands();
        ipc::Heartbeat();
        ipc::UpdateTemporaryData();
    }
#endif

    if (!command_stack().empty() && g_IEngine)
    {
        std::string cmd;
        {
            std::lock_guard<std::mutex> guard(command_stack_mutex);
            if (!command_stack().empty())
            {
                cmd = command_stack().top();
                command_stack().pop();
            }
        }
        if (!cmd.empty())
            g_IEngine->ClientCmd_Unrestricted(cmd.c_str());
    }

#if ENABLE_TEXTMODE
    static Timer paint_timer{};
    if (paint_timer.test_and_set(50))
        EC::run(EC::Paint);
#endif
}
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

void critical_error_handler(int signum, siginfo_t *sinfo, void *uctx)
{
    namespace st = boost::stacktrace;
    ::signal(SIGSEGV, SIG_DFL);
    ::signal(SIGABRT, SIG_DFL);
    passwd *pwd = getpwuid(getuid());
    char path[256];
    const char *home = getenv("HOME");
    if (home && home[0])
        snprintf(path, sizeof(path), "%s/cathook-segfault.log", home);
    else
        snprintf(path, sizeof(path), "/tmp/cathook-%s-%d-segfault.log", pwd ? pwd->pw_name : "unknown", getpid());
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        char hdr[64];
        int n = snprintf(hdr, sizeof(hdr), "signal %d\n", signum);
        if (n > 0)
            (void) write(fd, hdr, (size_t) n);
        if (sinfo || uctx)
        {
            char fl[160];
            int flen = snprintf(fl, sizeof(fl), "si_addr=%p", sinfo ? sinfo->si_addr : nullptr);
            if (uctx)
            {
                ucontext_t *uc = reinterpret_cast<ucontext_t *>(uctx);
                void *rip       = reinterpret_cast<void *>(uc->uc_mcontext.gregs[REG_RIP]);
                const auto *regs = uc->uc_mcontext.gregs;
                char regline[512];
                int reglen = snprintf(regline, sizeof(regline), "regs rsp=%p rbp=%p rax=%p rbx=%p rcx=%p rdx=%p rsi=%p rdi=%p r8=%p r9=%p r10=%p r11=%p r12=%p r13=%p r14=%p r15=%p\n",
                                      reinterpret_cast<void *>(regs[REG_RSP]), reinterpret_cast<void *>(regs[REG_RBP]),
                                      reinterpret_cast<void *>(regs[REG_RAX]), reinterpret_cast<void *>(regs[REG_RBX]),
                                      reinterpret_cast<void *>(regs[REG_RCX]), reinterpret_cast<void *>(regs[REG_RDX]),
                                      reinterpret_cast<void *>(regs[REG_RSI]), reinterpret_cast<void *>(regs[REG_RDI]),
                                      reinterpret_cast<void *>(regs[REG_R8]), reinterpret_cast<void *>(regs[REG_R9]),
                                      reinterpret_cast<void *>(regs[REG_R10]), reinterpret_cast<void *>(regs[REG_R11]),
                                      reinterpret_cast<void *>(regs[REG_R12]), reinterpret_cast<void *>(regs[REG_R13]),
                                      reinterpret_cast<void *>(regs[REG_R14]), reinterpret_cast<void *>(regs[REG_R15]));
                if (reglen > 0)
                    (void) write(fd, regline, std::min((size_t) reglen, sizeof(regline) - 1));

                uintptr_t stack_words[8]{};
                iovec local_iov{ stack_words, sizeof(stack_words) };
                iovec remote_iov{ reinterpret_cast<void *>(regs[REG_RSP]), sizeof(stack_words) };
                ssize_t stack_bytes = process_vm_readv(getpid(), &local_iov, 1, &remote_iov, 1, 0);
                for (size_t i = 0; stack_bytes > 0 && i < size_t(stack_bytes) / sizeof(uintptr_t); ++i)
                {
                    char stack_line[256];
                    Dl_info stack_info{};
                    int stack_len;
                    if (dladdr(reinterpret_cast<void *>(stack_words[i]), &stack_info) && stack_info.dli_fname)
                    {
                        const char *stack_base = strrchr(stack_info.dli_fname, '/');
                        stack_len = snprintf(stack_line, sizeof(stack_line), "stack[%zu]=%p %s+0x%lx\n", i,
                                             reinterpret_cast<void *>(stack_words[i]), stack_base ? stack_base + 1 : stack_info.dli_fname,
                                             (unsigned long) (stack_words[i] - uintptr_t(stack_info.dli_fbase)));
                    }
                    else
                        stack_len = snprintf(stack_line, sizeof(stack_line), "stack[%zu]=%p\n", i, reinterpret_cast<void *>(stack_words[i]));
                    if (stack_len > 0)
                        (void) write(fd, stack_line, std::min((size_t) stack_len, sizeof(stack_line) - 1));
                }

                Dl_info di{};
                if (dladdr(rip, &di) && di.dli_fname)
                {
                    const char *bas = strrchr(di.dli_fname, '/');
                    flen += snprintf(fl + flen, sizeof(fl) - flen, " rip=%s+0x%lx", bas ? bas + 1 : di.dli_fname, (unsigned long) (uintptr_t(rip) - uintptr_t(di.dli_fbase)));
                }
                else
                    flen += snprintf(fl + flen, sizeof(fl) - flen, " rip=%p", rip);
            }
            flen += snprintf(fl + flen, sizeof(fl) - flen, "\n");
            if (flen > 0)
                (void) write(fd, fl, (size_t) flen);
        }
        void *bt[32];
        int frames = backtrace(bt, 32);
        for (int i = 0; i < frames; ++i)
        {
            Dl_info di{};
            char line[256];
            int len = 0;
            if (dladdr(bt[i], &di) && di.dli_fname)
            {
                const char *bas = strrchr(di.dli_fname, '/');
                len = snprintf(line, sizeof(line), "%s\t0x%lx\n", bas ? bas + 1 : di.dli_fname, (unsigned long) (uintptr_t(bt[i]) - uintptr_t(di.dli_fbase)));
            }
            else
                len = snprintf(line, sizeof(line), "%p\n", bt[i]);
            if (len > 0)
                (void) write(fd, line, (size_t) len);
        }
        close(fd);
    }

    try
    {
        std::ofstream out(path, std::ios::app);
        out << std::unitbuf;

        Dl_info info;
        if (dladdr(reinterpret_cast<void *>(hack::ExecuteCommand), &info))
        {
            for (auto i : st::stacktrace())
            {
                Dl_info info2;
                if (dladdr(i.address(), &info2) && info2.dli_fname)
                {
                    uintptr_t offset = uintptr_t(i.address()) - uintptr_t(info2.dli_fbase);
                    out << (!strcmp(info2.dli_fname, info.dli_fname) ? "cathook" : info2.dli_fname) << '\t' << (void *) offset << std::endl;
                }
            }
        }
        out.close();
    }
    catch (...)
    {
    }

    ::raise(SIGABRT);
}
#endif

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
#if ENABLE_VISUALS || ENABLE_TEXTMODE
    hooks::client.HookMethod(HOOK_ARGS(FrameStageNotify));
#endif
#if ENABLE_VISUALS
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
    struct sigaction sa{};
    sa.sa_sigaction = &critical_error_handler;
    sa.sa_flags     = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    ::sigaction(SIGSEGV, &sa, nullptr);
    ::sigaction(SIGABRT, &sa, nullptr);
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
    logging::Info("Releasing detour hooks..");
    DetourHook::ShutdownAll();
    logging::Info("Releasing bytepatches..");
    BytePatch::ShutdownAll();
    logging::Info("Success..");
}
