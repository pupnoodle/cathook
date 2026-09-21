/*
 * Paint.cpp
 *
 *  Created on: Dec 31, 2017
 *      Author: nullifiedcat
 */

#include <hacks/hacklist.hpp>
#include <settings/Bool.hpp>
#include "common.hpp"
#include "hitrate.hpp"
#include "hack.hpp"
#if ENABLE_VISUALS
#include "drawmgr.hpp"
#endif
extern settings::Boolean die_if_vac;
static Timer checkmmban{};
namespace hooked_methods
{

DEFINE_HOOKED_METHOD(Paint, void, IEngineVGui *this_, PaintMode_t mode)
{
    if (!isHackActive())
    {
        return original::Paint(this_, mode);
    }

    if (!g_IEngine->IsInGame())
        g_Settings.bInvalid = true;

    CNetChan *ch = g_IEngine->GetNetChannelInfo();
    if (ch && !hooks::netchannel.IsHooked((void *) ch))
    {
        hooks::netchannel.Set(ch);
        hooks::netchannel.HookMethod(HOOK_ARGS(SendDatagram));
        hooks::netchannel.HookMethod(HOOK_ARGS(CanPacket));
        hooks::netchannel.HookMethod(HOOK_ARGS(SendNetMsg));
        hooks::netchannel.HookMethod(HOOK_ARGS(Shutdown));
        hooks::netchannel.Apply();
#if ENABLE_IPC
        ipc::UpdateServerAddress();
#endif
    }

    hack::PumpEngine();

#if ENABLE_TEXTMODE
    (void) mode;
#endif
#if ENABLE_TEXTMODE
    if (true)
#else
    if (mode & PaintMode_t::PAINT_UIPANELS)
#endif
    {
        hitrate::Update();
#if !ENABLE_VISUALS
        if (*die_if_vac && checkmmban.test_and_set(1000))
        {
            if (tfmm::isMMBanned())
                *(int *) 0 = 0;
        }
#endif

#if ENABLE_TEXTMODE_STDIN == 1
        static auto last_stdin = std::chrono::system_clock::from_time_t(0);
        auto ms                = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - last_stdin).count();
        if (ms > 500)
        {
            UpdateInput();
            last_stdin = std::chrono::system_clock::now();
        }
#endif
#if ENABLE_GLEZ_DRAWING
        render_cheat_visuals();
#endif
        EC::run(EC::Paint);
    }

#if ENABLE_TEXTMODE
    return;
#else
    return original::Paint(this_, mode);
#endif
}
} // namespace hooked_methods
