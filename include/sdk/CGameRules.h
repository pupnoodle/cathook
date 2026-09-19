#pragma once

#include "core/netvars.hpp"

class CGameRules
{
public:
    int RoundMode() const
    {
        if (!this || !netvar.m_iRoundState)
            return 0;
        return NET_INT(this, netvar.m_iRoundState);
    }
    int WinningTeam() const
    {
        if (!this || !netvar.m_iWinningTeam)
            return 0;
        return NET_INT(this, netvar.m_iWinningTeam);
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
