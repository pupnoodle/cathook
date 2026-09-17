/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/games/tf2/sdk/interfaces/steam_friends.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef STEAM_FRIENDS_HPP
#define STEAM_FRIENDS_HPP

#include <cstdint>

enum EAccountType {
  k_EAccountTypeInvalid = 0,
  k_EAccountTypeIndividual = 1,
  k_EAccountTypeMultiseat = 2,
  k_EAccountTypeGameServer = 3,
  k_EAccountTypeAnonGameServer = 4,
  k_EAccountTypePending = 5,
  k_EAccountTypeContentServer = 6,
  k_EAccountTypeClan = 7,
  k_EAccountTypeChat = 8,
  k_EAccountTypeConsoleUser = 9,
  k_EAccountTypeAnonUser = 10,

  k_EAccountTypeMax
};

enum EFriendFlags {
  k_EFriendFlagNone			= 0x00,
  k_EFriendFlagBlocked		= 0x01,
  k_EFriendFlagFriendshipRequested	= 0x02,
  k_EFriendFlagImmediate		= 0x04,
  k_EFriendFlagClanMember		= 0x08,
  k_EFriendFlagOnGameServer	= 0x10,

  k_EFriendFlagRequestingFriendship = 0x80,
  k_EFriendFlagRequestingInfo = 0x100,
  k_EFriendFlagIgnored		= 0x200,
  k_EFriendFlagIgnoredFriend	= 0x400,

  k_EFriendFlagChatMember		= 0x1000,
  k_EFriendFlagAll			= 0xFFFF,
};

enum EUniverse {
  k_EUniverseInvalid = 0,
  k_EUniversePublic = 1,
  k_EUniverseBeta = 2,
  k_EUniverseInternal = 3,
  k_EUniverseDev = 4,

  k_EUniverseMax
};

enum EPersonaState {
  k_EPersonaStateOffline = 0,
  k_EPersonaStateOnline = 1,
  k_EPersonaStateBusy = 2,
  k_EPersonaStateAway = 3,
  k_EPersonaStateSnooze = 4,
  k_EPersonaStateLookingToTrade = 5,
  k_EPersonaStateLookingToPlay = 6,
  k_EPersonaStateInvisible = 7
};

class SteamID {
public:

  SteamID() {
    m_steamid.m_unAll64Bits = 0;
  }

  SteamID(int unAccountID, unsigned int unAccountInstance, EUniverse eUniverse, EAccountType eAccountType) {
    InstancedSet( unAccountID, unAccountInstance, eUniverse, eAccountType );
  }

  void InstancedSet(int unAccountID, int unInstance, EUniverse eUniverse, EAccountType eAccountType) {
    m_steamid.m_comp.m_unAccountID = unAccountID;
    m_steamid.m_comp.m_EUniverse = eUniverse;
    m_steamid.m_comp.m_EAccountType = eAccountType;
    m_steamid.m_comp.m_unAccountInstance = unInstance;
  }

  union SteamID_t {
    struct SteamIDComponent_t {
      int				m_unAccountID : 32;
      unsigned int		m_unAccountInstance : 20;
      unsigned int		m_EAccountType : 4;
      EUniverse			m_EUniverse : 8;
    } m_comp;

    unsigned long m_unAll64Bits;
  } m_steamid;
};

struct FriendGameInfo {
  std::uint64_t game_id = 0;
  std::uint32_t game_ip = 0;
  std::uint16_t game_port = 0;
  std::uint16_t query_port = 0;
  std::uint64_t lobby_id = 0;
};

class SteamFriends {
public:

  const char* get_friend_persona_name(SteamID steam_friend_id) {
    void** vtable = *(void***)this;
    auto get_friend_persona_name_fn =
      reinterpret_cast<const char* (*)(void*, SteamID)>(vtable[7]);
    return get_friend_persona_name_fn(this, steam_friend_id);
  }

  int get_friend_persona_state(SteamID steam_friend_id) {
    void** vtable = *(void***)this;
    auto get_friend_persona_state_fn =
      reinterpret_cast<int (*)(void*, SteamID)>(vtable[6]);
    return get_friend_persona_state_fn(this, steam_friend_id);
  }

  bool get_friend_game_played(SteamID steam_friend_id, FriendGameInfo* game_info) {
    void** vtable = *(void***)this;
    auto get_friend_game_played_fn =
      reinterpret_cast<bool (*)(void*, SteamID, FriendGameInfo*)>(vtable[8]);
    return get_friend_game_played_fn(this, steam_friend_id, game_info);
  }

  bool has_friend(SteamID steam_friend_id, int friend_flags) {
    void** vtable = *(void***)this;

    bool (*has_friend_fn)(void*, SteamID, int) = (bool (*)(void*, SteamID, int))vtable[17];

    return has_friend_fn(this, steam_friend_id, friend_flags);
  }

  bool is_friend(int friend_id) {
    return has_friend({friend_id, 1, k_EUniversePublic, k_EAccountTypeIndividual}, k_EFriendFlagImmediate);
  }

  bool request_user_information(SteamID steam_id, bool name_only) {
    void** vtable = *(void***)this;
    auto request_user_information_fn =
      reinterpret_cast<bool (*)(void*, SteamID, bool)>(vtable[37]);
    return request_user_information_fn(this, steam_id, name_only);
  }

  const char* get_friend_rich_presence(SteamID steam_friend_id, const char* key) {
    void** vtable = *(void***)this;
    auto get_friend_rich_presence_fn =
      reinterpret_cast<const char* (*)(void*, SteamID, const char*)>(vtable[45]);
    return get_friend_rich_presence_fn(this, steam_friend_id, key);
  }

  int get_friend_rich_presence_key_count(SteamID steam_friend_id) {
    void** vtable = *(void***)this;
    auto get_friend_rich_presence_key_count_fn =
      reinterpret_cast<int (*)(void*, SteamID)>(vtable[46]);
    return get_friend_rich_presence_key_count_fn(this, steam_friend_id);
  }

  const char* get_friend_rich_presence_key_by_index(SteamID steam_friend_id, int index) {
    void** vtable = *(void***)this;
    auto get_friend_rich_presence_key_by_index_fn =
      reinterpret_cast<const char* (*)(void*, SteamID, int)>(vtable[47]);
    return get_friend_rich_presence_key_by_index_fn(this, steam_friend_id, index);
  }

  void request_friend_rich_presence(SteamID steam_friend_id) {
    void** vtable = *(void***)this;
    auto request_friend_rich_presence_fn =
      reinterpret_cast<void (*)(void*, SteamID)>(vtable[48]);
    request_friend_rich_presence_fn(this, steam_friend_id);
  }
};

inline static SteamFriends* steam_friends;

#endif
