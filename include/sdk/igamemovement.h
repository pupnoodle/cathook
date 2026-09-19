//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//

#pragma once

#ifdef _WIN32
#pragma once
#endif

class CBasePlayer;

#include "mathlib/vector.h"
#include "interface.h"
#include <cstddef>
//#include "imovehelper.h"
#include "const.h"

//-----------------------------------------------------------------------------
// Name of the class implementing the game movement.
//-----------------------------------------------------------------------------

#define INTERFACENAME_GAMEMOVEMENT "GameMovement001"

//-----------------------------------------------------------------------------
// Forward declarations.
//-----------------------------------------------------------------------------

class IMoveHelper;

//-----------------------------------------------------------------------------
// Purpose: Encapsulated input parameters to player movement.
//-----------------------------------------------------------------------------

class CMoveData
{
public:
    bool m_bFirstRunOfFunctions : 1;
    bool m_bGameCodeMovedPlayer : 1;

    CBaseHandle m_nPlayerHandle; // edict index on server, client entity handle
                                 // on client

    int m_nImpulseCommand;     // Impulse command issued.
    QAngle m_vecViewAngles;    // Command view angles (local space)
    QAngle m_vecAbsViewAngles; // Command view angles (world space)
    int m_nButtons;            // Attack buttons.
    int m_nOldButtons;         // From host_client->oldbuttons;
    float m_flForwardMove;
    float m_flOldForwardMove;
    float m_flSideMove;
    float m_flUpMove;

    float m_flMaxSpeed;
    float m_flClientMaxSpeed;

    // Variables from the player edict (sv_player) or entvars on the client.
    // These are copied in here before calling and copied out after calling.
    Vector m_vecVelocity; // edict::velocity		// Current movement
                          // direction.
    QAngle m_vecAngles;   // edict::angles
    QAngle m_vecOldAngles;

    // Output only
    float m_outStepHeight; // how much you climbed this move
    Vector m_outWishVel;   // This is where you tried
    Vector m_outJumpVel;   // This is your jump velocity

    // Movement constraints	(radius 0 means no constraint)
    Vector m_vecConstraintCenter;
    float m_flConstraintRadius;
    float m_flConstraintWidth;
    float m_flConstraintSpeedFactor;

    void SetAbsOrigin(const Vector &vec);
    const Vector &GetAbsOrigin() const;

private:
    Vector m_vecAbsOrigin; // edict::origin
};

static_assert(offsetof(CMoveData, m_vecAbsOrigin) == 0x9c, "CMoveData m_vecAbsOrigin must match TF2 linux64 SetupMove");
static_assert(sizeof(CMoveData) >= 0xA8, "CMoveData must cover SetupMove writes through +0xA4");

inline const Vector &CMoveData::GetAbsOrigin() const
{
    return m_vecAbsOrigin;
}

#if !defined(CLIENT_DLL) && defined(_DEBUG)
// We only ever want this code path on the server side in a debug build
//  and you have to uncomment the code below and rebuild to have the test
//  operate.
//#define PLAYER_GETTING_STUCK_TESTING

#endif

#if !defined(PLAYER_GETTING_STUCK_TESTING)

// This is implemented with a more exhaustive test in gamemovement.cpp.  We
// check if the origin being requested is
//  inside solid, which it never should be
inline void CMoveData::SetAbsOrigin(const Vector &vec)
{
    m_vecAbsOrigin = vec;
}

#endif

//-----------------------------------------------------------------------------
// Purpose: The basic player movement interface
//-----------------------------------------------------------------------------

#include "core/vfunc.hpp"
#include "core/vtables.hpp"

class IGameMovement
{
public:
    void ProcessMovement(CBasePlayer *pPlayer, CMoveData *pMove)
    {
        vfunc<void (*)(IGameMovement *, CBasePlayer *, CMoveData *)>(this, vtables::game_movement::process_movement)(this, pPlayer, pMove);
    }
    void StartTrackPredictionErrors(CBasePlayer *pPlayer)
    {
        vfunc<void (*)(IGameMovement *, CBasePlayer *)>(this, vtables::game_movement::start_track_prediction_errors)(this, pPlayer);
    }
    void FinishTrackPredictionErrors(CBasePlayer *pPlayer)
    {
        vfunc<void (*)(IGameMovement *, CBasePlayer *)>(this, vtables::game_movement::finish_track_prediction_errors)(this, pPlayer);
    }
};
