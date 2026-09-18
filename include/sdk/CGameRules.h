#pragma once

#include "core/netvars.hpp"

class CGameRules
{
public:
    int RoundMode() const
    {
        return netvar.m_iRoundState ? NET_INT(this, netvar.m_iRoundState) : 0;
    }
    int WinningTeam() const
    {
        return netvar.m_iWinningTeam ? NET_INT(this, netvar.m_iWinningTeam) : 0;
    }
    bool isPVEMode() const
    {
        return netvar.m_bPlayingMannVsMachine && NET_VAR(this, netvar.m_bPlayingMannVsMachine, bool);
    }
    int halloweenScenario() const
    {
        return netvar.m_halloweenScenario ? NET_INT(this, netvar.m_halloweenScenario) : 0;
    }
    bool isUsingSpells() const
    {
        return netvar.m_bIsUsingSpells && NET_VAR(this, netvar.m_bIsUsingSpells, bool);
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
