/*
 * interfaces.h
 *
 *  Created on: Oct 3, 2016
 *      Author: nullifiedcat
 */

#pragma once

#include <core/sharedobj.hpp>
#include <string>
#include <engine/IEngineSound.h>

namespace vgui
{
class ISurface;
class IPanel;
class ILocalize;
} // namespace vgui

class IToolFrameworkInternal;
class ISteamClient;
class ISteamNetworkingSockets;
class ISteamFriends;
class CEngineClient;
class IClientEntityList;
class ICenterPrint;
class CCvar;
class IGameEventManager2;
class CHLClient;
class ClientModeShared;
class CEngineTrace;
class CModelInfoClient;
class CInputSystem;
class IClient;
class CGlobalVarsBase;
class CPrediction;
class IGameMovement;
class IInput;
class IMatSystemSurface;
class ISteamUser;
class IAchievementMgr;
class ISteamUserStats;
class IStudioRender;
class IVDebugOverlay;
class CModelRender;
class CRenderView;
class CMatSystemSurface;
class CPanel;
class IMaterialSystemFixed;
class IMaterialSystem;
class IMoveHelperServer;
#include "sdk/client_state.hpp"
class CHud;
class IGameEventManager;
class TFGCClientSystem;
class CGameRules;
class IEngineVGui;
class IUniformRandomStream;
class IFileSystem;
class IMDLCache;
class CServerTools;

extern TFGCClientSystem *g_TFGCClientSystem;
extern CHud *g_CHUD;
extern ISteamClient *g_ISteamClient;
extern ISteamFriends *g_ISteamFriends;
extern ISteamNetworkingSockets *g_ISteamNetworkingSockets;
extern CEngineClient *g_IEngine;
extern void *demoplayer;
extern IEngineSound *g_ISoundEngine;
extern CMatSystemSurface *g_ISurface;
extern CPanel *g_IPanel;
extern vgui::ILocalize *g_ILocalize;
extern IClientEntityList *g_IEntityList;
extern CCvar *g_ICvar;
extern IGameEventManager2 *g_IEventManager2;
extern CHLClient *g_IBaseClient;
extern CEngineTrace *g_ITrace;
extern CModelInfoClient *g_IModelInfo;
extern CInputSystem *g_IInputSystem;
extern CGlobalVarsBase **rg_GlobalVars;
#define g_GlobalVars (*rg_GlobalVars)
extern CPrediction *g_IPrediction;
extern IGameMovement *g_IGameMovement;
extern IInput *g_IInput;
extern ISteamUser *g_ISteamUser;
extern IAchievementMgr *g_IAchievementMgr;
extern ISteamUserStats *g_ISteamUserStats;
extern IStudioRender *g_IStudioRender;
extern IVDebugOverlay *g_IVDebugOverlay;
extern IMaterialSystemFixed *g_IMaterialSystem;
extern CModelRender *g_IVModelRender;
extern CRenderView *g_IVRenderView;
extern IMoveHelperServer *g_IMoveHelperServer;
extern CBaseClientState *g_IBaseClientState;
extern IGameEventManager *g_IGameEventManager;
extern CGameRules **rg_pGameRules;
#define g_pGameRules (*rg_pGameRules)
extern IEngineVGui *g_IEngineVGui;
extern IUniformRandomStream *g_pUniformStream;
extern int *g_PredictionRandomSeed;

inline void SetPredictionRandomSeed(int seed)
{
    if (g_PredictionRandomSeed)
        *g_PredictionRandomSeed = seed;
}
extern IFileSystem *g_IFileSystem;
extern IMDLCache *g_IMDLCache;
extern IToolFrameworkInternal *g_IToolFramework;
extern CServerTools *g_IServerTools;

void CreateInterfaces();
void CreateEarlyInterfaces();
