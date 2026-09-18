#pragma once

#include <inetchannel.h>

// Live TF2 linux64 CNetChan layout after the INetChannel vtable.
struct CNetChan
{
    void *vtable;
    int connection_state;
    int m_nOutSequenceNr;
    int m_nInSequenceNr;
    int m_nOutSequenceNrAck;
    int m_nOutReliableState;
    int m_nInReliableState;
    int m_nChokedPackets;
};

inline CNetChan *NetChan(INetChannel *ch)
{
    return reinterpret_cast<CNetChan *>(ch);
}
