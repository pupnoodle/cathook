/*
  Created by Jenny White on 29.04.18.
  Copyright (c) 2018 nullworks. All rights reserved.
*/

#include <hacks/hacklist.hpp>
#include <settings/Bool.hpp>
#if ENABLE_VISUALS
#include <menu/GuiInterface.hpp>
#endif
#include "HookedMethods.hpp"
#include "MiscTemporary.hpp"
#include "navparser.hpp"
#include "AntiAntiAim.hpp"
#if ENABLE_IMGUI_DRAWING
#include "visual/imgui/imrenderer.hpp"
#endif

static settings::Boolean halloween_mode{ "misc.force-halloween", "false" };
extern settings::Boolean random_name;
extern settings::String force_name;
extern std::string name_forced;

namespace hooked_methods
{

DEFINE_HOOKED_METHOD(LevelInit, void, void *this_, const char *name)
{
    firstcm = true;
    // nav::init = false;
    playerlist::Save();
#if ENABLE_VISUALS
#if ENABLE_GUI
    gui::onLevelLoad();
#endif
    ConVar *holiday = g_ICvar->FindVar("tf_forced_holiday");
    if (halloween_mode)
        holiday->SetValue(2);
    else if (holiday->m_nValue == 2)
        holiday->SetValue(0);
#endif
    hacks::shared::anti_anti_aim::resolver_map.clear();
    g_IEngine->ClientCmd_Unrestricted("exec cat_matchexec");
    chat_stack::Reset();
    original::LevelInit(this_, name);
    logging::Info("LevelInit map=%s", name ? name : "?");
#if ENABLE_IMGUI_DRAWING
    im_renderer::resetFrameLog();
#endif
    EC::run(EC::LevelInit);
#if ENABLE_IPC
    if (ipc::peer)
    {
        ipc::peer->memory->peer_user_data[ipc::peer->client_id].ts_connected = time(nullptr);
    }
#endif
    if (*random_name)
    {
        static TextFile file;
        if (file.TryLoad("names.txt"))
            name_forced = file.lines.at(rand() % file.lines.size());
    }
    else
        name_forced = "";
}
} // namespace hooked_methods
