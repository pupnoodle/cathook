#pragma once

#include "core/vfunc.hpp"
#include "core/vtables.hpp"

class ClientClass;
class CViewSetup;

// Live linux64 CHLClient. Indices from _ZTV9CHLClient (no extra dtor slots).
class CHLClient
{
public:
    ClientClass *GetAllClasses()
    {
        return vfunc<ClientClass *(*)(CHLClient *)>(this, vtables::client_dll::get_all_classes)(this);
    }
    bool GetPlayerView(CViewSetup &playerView)
    {
        return vfunc<bool (*)(CHLClient *, CViewSetup &)>(this, vtables::client_dll::get_player_view)(this, playerView);
    }
    void InvalidateMdlCache()
    {
        vfunc<void (*)(CHLClient *)>(this, vtables::client_dll::invalidate_mdl_cache)(this);
    }
};
