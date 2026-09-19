/*
 * sdk.h
 *
 *  Created on: Dec 5, 2016
 *      Author: nullifiedcat
 *
 * 2013 headers remain for types (Vector, ClientClass, CViewSetup, ...).
 * Live linux64 calls go through include/sdk wrappers + vtables.hpp, not
 * leftover 2013 multiple-inheritance vtables.
 */

#pragma once

#define private public
#define protected public

#include <client_class.h>
#include <icliententity.h>
#include <icliententitylist.h>
#include <cdll_int.h>
#include <inetchannelinfo.h>
#include <gametrace.h>
#include <engine/IEngineTrace.h>
#include <materialsystem/imaterialvar.h>
#include <toolframework/itoolframework.h>
#include <globalvars_base.h>
#include <materialsystem/itexture.h>
#include <engine/ivmodelinfo.h>
#include <inputsystem/iinputsystem.h>
#include <mathlib/vector.h>
#include <icvar.h>
#include <Color.h>
#include <cmodel.h>
#include <igameevents.h>
#include <iclient.h>
#include <inetchannel.h>
#include "sdk/CNetChan.hpp"
#include "sdk/engine_client.hpp"
#include "sdk/client_dll.hpp"
#include "sdk/cvar.hpp"
#include "sdk/engine_trace.hpp"
#include "sdk/model_info.hpp"
#include "sdk/prediction.hpp"
#include "sdk/client_entity.hpp"
#include <ivrenderview.h>
#include <iconvar.h>
#include <studio.h>
#include <vgui/ISurface.h>
#include <vgui/IPanel.h>
#include <vgui/ILocalize.h>
#include <mathlib/vmatrix.h>
#include <inetmessage.h>
#include <iclient.h>
#include <iserver.h>
#include <view_shared.h>
#include <KeyValues.h>
#include <checksum_md5.h>
#include <basehandle.h>
#include <iachievementmgr.h>
#include <VGuiMatSurface/IMatSystemSurface.h>
#include <steam/isteamuser.h>
#include <steam/steam_api.h>
#include <vgui/Cursor.h>
#include <engine/ivdebugoverlay.h>
#include <iprediction.h>
#include <engine/ICollideable.h>
#include <icommandline.h>
#include <sdk/netmessage.hpp>
#include <dbg.h>
#include <ienginevgui.h>

#include "sdk/c_basetempentity.h"
#include "sdk/in_buttons.h"
#include "sdk/imaterialsystemfixed.h"
#include "sdk/ScreenSpaceEffects.h"
#include "sdk/iinput.h"
#include "sdk/input_system.hpp"
#include "sdk/vgui_surface.hpp"
#include "sdk/vgui_panel.hpp"
#include "sdk/material.hpp"
#include "sdk/texture.hpp"
#include "sdk/render_view.hpp"
#include "sdk/model_render.hpp"
#include "sdk/igamemovement.h"
#include "sdk/HUD.h"
#include "sdk/CGameRules.h"

#undef private
#undef protected
