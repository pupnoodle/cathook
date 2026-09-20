#pragma once

#include "core/netvars.hpp"

class CGameRules
{
public:
    enum gamerules_roundstate_t
    {
        GR_STATE_INIT = 0,
        GR_STATE_PREGAME,
        GR_STATE_STARTGAME,
        GR_STATE_PREROUND,
        GR_STATE_RND_RUNNING,
        GR_STATE_TEAM_WIN,
        GR_STATE_RESTART,
        GR_STATE_STALEMATE,
        GR_STATE_GAME_OVER,
        GR_STATE_BONUS,
        GR_STATE_BETWEEN_RNDS,
    };

    int RoundMode() const
    {
        if (!this || !netvar.m_iRoundState)
            return 0;
        return NET_INT(this, netvar.m_iRoundState);
    }
    bool RoundHasBeenWon() const
    {
        return RoundMode() == GR_STATE_TEAM_WIN;
    }
    bool InStalemate() const
    {
        return RoundMode() == GR_STATE_STALEMATE;
    }
    bool PointsMayBeCaptured() const
    {
        return (RoundMode() == GR_STATE_RND_RUNNING || InStalemate()) && !InWaitingForPlayers();
    }
    bool IsPlayingSpecialDeliveryMode() const
    {
        return this && netvar.m_bPlayingSpecialDeliveryMode && NET_VAR(this, netvar.m_bPlayingSpecialDeliveryMode, bool);
    }
    int WinningTeam() const
    {
        if (!this || !netvar.m_iWinningTeam)
            return 0;
        return NET_INT(this, netvar.m_iWinningTeam);
    }
    bool InSetup() const
    {
        return this && netvar.m_bInSetup && NET_VAR(this, netvar.m_bInSetup, bool);
    }
    bool InWaitingForPlayers() const
    {
        return this && netvar.m_bInWaitingForPlayers && NET_VAR(this, netvar.m_bInWaitingForPlayers, bool);
    }
    bool isPVEMode() const
    {
        return this && netvar.m_bPlayingMannVsMachine && NET_VAR(this, netvar.m_bPlayingMannVsMachine, bool);
    }
    int halloweenScenario() const
    {
        if (!this || !netvar.m_halloweenScenario)
            return 0;
        return NET_INT(this, netvar.m_halloweenScenario);
    }
    bool isUsingSpells() const
    {
        return this && netvar.m_bIsUsingSpells && NET_VAR(this, netvar.m_bIsUsingSpells, bool);
    }
    bool isUsingSpells_fn()
    {
        auto tf_spells_enabled = g_ICvar->FindVar("tf_spells_enabled");
        if (tf_spells_enabled && tf_spells_enabled->GetBool())
            return true;
        if (halloweenScenario() == 4)
            return true;
        return isUsingSpells();
    }
};
