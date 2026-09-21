/*
  Created by Jenny White on 29.04.18.
  Copyright (c) 2018 nullworks. All rights reserved.
*/

#include "MiscTemporary.hpp"
#include "Warp.hpp"
#include "nospread.hpp"
#include "sdk/CNetChan.hpp"

std::array<Timer, 32> timers{};
std::array<int, 32> bruteint{};

int spectator_target;
CLC_VoiceData *voicecrash{};
bool firstcm = false;
Timer DelayTimer{};
float prevflow            = 0.0f;
int prevflowticks         = 0;
int stored_buttons        = 0;
bool calculated_can_shoot = false;
bool ignoredc             = false;

bool *bSendPackets{ nullptr };
bool ignoreKeys{ false };
settings::Boolean clean_chat{ "chat.clean", "false" };

settings::Boolean crypt_chat{ "chat.crypto", "true" };
settings::Boolean clean_screenshots{ "visual.clean-screenshots", "false" };
#if ENABLE_TEXTMODE
settings::Boolean nolerp{ "misc.no-lerp", "true" };
#else
settings::Boolean nolerp{ "misc.no-lerp", "false" };
#endif
float backup_lerp = 0.0f;
settings::Int fakelag_amount{ "misc.fakelag", "0" };
settings::Boolean fakelag_midair{ "misc.fakelag-midair-only", "false" };
settings::Boolean no_zoom{ "remove.zoom", "false" };
settings::Boolean no_scope{ "remove.scope", "false" };
settings::Boolean disable_visuals{ "visual.disable", "false" };
settings::Int print_r{ "print.rgb.r", "183" };
settings::Int print_g{ "print.rgb.b", "27" };
settings::Int print_b{ "print.rgb.g", "139" };
Color menu_color{ *print_r, *print_g, *print_b, 255 };

void color_callback(settings::VariableBase<int> &, int)
{
    menu_color = Color(*print_r, *print_g, *print_b, 255);
}
DetourHook cl_sendmove_detour;
static bool send_packets{ true };

static void CL_SendMove_dispatch()
{
    hacks::tf2::nospread::CL_SendMove_hook();
}

static InitRoutine misc_init([]() {
    bSendPackets                 = &send_packets;
    static auto cl_sendmove_addr = gSignatures.GetEngineSignature(sigs::cl_sendmove);
    cl_sendmove_detour.Init(cl_sendmove_addr, (void *) CL_SendMove_dispatch);

    print_r.installChangeCallback(color_callback);
    print_g.installChangeCallback(color_callback);
    print_b.installChangeCallback(color_callback);
    nolerp.installChangeCallback([](settings::VariableBase<bool> &, bool after) {
        if (!cl_interp)
            return;
        if (!after)
        {
            if (backup_lerp)
            {
                cl_interp->SetValue(backup_lerp);
                backup_lerp = 0.0f;
            }
        }
        else
        {
            backup_lerp = cl_interp->GetFloat();
            if (cl_interp->GetFloat() > 0.152f)
                cl_interp->SetValue(0.152f);
        }
    });
    EC::Register(
        EC::Shutdown,
        []() {
            cl_sendmove_detour.Shutdown();
            if (backup_lerp && cl_interp)
            {
                cl_interp->SetValue(backup_lerp);
                backup_lerp = 0.0f;
            }
        },
        "misctemp_shutdown");
#if ENABLE_TEXTMODE
    // Ensure that we trigger the callback for textmode builds
    nolerp = false;
    nolerp = true;
#endif
});
