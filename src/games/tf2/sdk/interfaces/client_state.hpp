/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/games/tf2/sdk/interfaces/client_state.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef CLIENT_STATE_HPP
#define CLIENT_STATE_HPP

#include <cstddef>
#include <cstdint>

#include "games/tf2/sdk/interfaces/net_channel.hpp"

struct ClockDriftMgr {
  float m_ClockOffsets[16];
  int m_iCurClockOffset;
  int m_nServerTick;
  int m_nClientTick;
};

class ClientState {
public:
  uint8_t pad0[24];
  int m_Socket;
  net_channel* m_NetChannel;
  unsigned int m_nChallengeNr;
  double m_flConnectTime;
  int m_nRetryNumber;
  char m_szRetryAddress[260];
  char* m_sRetrySourceTag;
  int m_retryChallenge;
  int m_nSignonState;
  double m_flNextCmdTime;
  int m_nServerCount;
  uint64_t m_ulGameServerSteamID;
  int m_nCurrentSequence;
  ClockDriftMgr m_ClockDriftMgr;
  int m_nDeltaTick;
  bool m_bPaused;
  float m_flPausedExpireTime;
  int m_nViewEntity;
  int m_nPlayerSlot;
  char m_szLevelFileName[128];
  uint8_t pad1[132];
  char m_szLevelBaseName[128];
  uint8_t pad2[132];
  int m_nMaxClients;
  void* m_pEntityBaselines[2][0x800];
  uint8_t pad3[2068];
  void* m_StringTableContainer;
  bool m_bRestrictServerCommands;
  bool m_bRestrictClientCommands;
  uint8_t pad4[106];
  bool insimulation;
  int oldtickcount;
  float m_tickRemainder;
  float m_frameTime;
  int lastoutgoingcommand;
  int chokedcommands;
  int last_command_ack;
  int command_ack;
  int m_nSoundSequence;
  bool ishltv;
  bool isreplay;
  uint8_t pad5[278];
  int demonum;
  char* demos[32];
  uint8_t pad6[344184];
  bool m_bMarkedCRCsUnverified;
};

static_assert(offsetof(ClientState, m_nDeltaTick) == 0x1B8);

inline static ClientState* client_state;
inline void (*client_state_force_full_update)(ClientState*) = nullptr;

#endif
