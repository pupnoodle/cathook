/*
/^-----^\   data: 2026-05-06
V  o o  V  file: src/games/tf2/sdk/interfaces/steam_runtime.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef STEAM_RUNTIME_HPP
#define STEAM_RUNTIME_HPP

#include <array>
#include <dlfcn.h>

#include "games/tf2/sdk/interfaces/steam_client.hpp"
#include "games/tf2/sdk/interfaces/steam_friends.hpp"
#include "games/tf2/sdk/interfaces/steam_game_coordinator.hpp"
#include "games/tf2/sdk/interfaces/steam_user.hpp"
#include "games/tf2/sdk/interfaces/steam_user_stats.hpp"

void* get_interface(const char* lib_path, const char* version);
void* open_loaded_library(const char* lib_path);

namespace steam_runtime
{

template <typename function_type>
function_type resolve_loaded_symbol(const char* symbol_name)
{
  if (auto* symbol = dlsym(RTLD_DEFAULT, symbol_name))
  {
    return reinterpret_cast<function_type>(symbol);
  }

  constexpr std::array steam_api_modules = {
    "./bin/linux64/libsteam_api.so",
    "libsteam_api.so",
  };

  for (const char* module_name : steam_api_modules)
  {
    void* handle = open_loaded_library(module_name);
    if (handle == nullptr)
    {
      continue;
    }

    auto* symbol = dlsym(handle, symbol_name);
    dlclose(handle);
    if (symbol != nullptr)
    {
      return reinterpret_cast<function_type>(symbol);
    }
  }

  return nullptr;
}

inline SteamClient* resolve_steam_client_from_api()
{
  using steam_client_factory_fn = SteamClient* (*)();

  auto* factory = resolve_loaded_symbol<steam_client_factory_fn>("SteamClient");
  return factory != nullptr ? factory() : nullptr;
}

inline SteamClient* resolve_steam_client()
{
  if (steam_client != nullptr)
  {
    return steam_client;
  }

  if (auto* client = resolve_steam_client_from_api())
  {
    steam_client = client;
    return steam_client;
  }

  constexpr std::array steam_client_modules = {
    "./bin/linux64/steamclient.so",
    "steamclient.so",
    "./bin/linux64/libsteam_api.so",
    "libsteam_api.so",
  };

  constexpr std::array steam_client_versions = {
    "SteamClient020",
    "SteamClient017",
  };

  for (const char* module_name : steam_client_modules)
  {
    for (const char* version : steam_client_versions)
    {
      auto* candidate = static_cast<SteamClient*>(get_interface(module_name, version));
      if (candidate != nullptr)
      {
        steam_client = candidate;
        return steam_client;
      }
    }
  }

  return nullptr;
}

inline auto steam_pipe_handle() -> int
{
  static int steam_pipe = 0;

  if (steam_pipe == 0)
  {
    auto* steam_client_interface = resolve_steam_client();
    if (steam_client_interface != nullptr)
    {
      steam_pipe = steam_client_interface->create_steam_pipe();
    }
  }

  return steam_pipe;
}

inline auto steam_user_handle() -> int
{
  static int steam_user = 0;

  if (steam_user == 0)
  {
    auto* steam_client_interface = resolve_steam_client();
    const auto steam_pipe = steam_pipe_handle();
    if (steam_client_interface != nullptr && steam_pipe != 0)
    {
      steam_user = steam_client_interface->connect_to_global_user(steam_pipe);
    }
  }

  return steam_user;
}

inline SteamFriends* resolve_steam_friends()
{
  if (steam_friends != nullptr)
  {
    return steam_friends;
  }

  using steam_user_handle_fn = int (*)();
  using find_interface_fn = void* (*)(int, const char*);
  const auto get_steam_user = resolve_loaded_symbol<steam_user_handle_fn>("SteamAPI_GetHSteamUser");
  const auto find_interface = resolve_loaded_symbol<find_interface_fn>("SteamInternal_FindOrCreateUserInterface");
  if (get_steam_user == nullptr || find_interface == nullptr)
  {
    return nullptr;
  }

  steam_friends = static_cast<SteamFriends*>(find_interface(get_steam_user(), "SteamFriends017"));
  return steam_friends;
}

inline steam_user* resolve_steam_user_from_api()
{
  using steam_user_factory_fn = steam_user* (*)();

  constexpr std::array factory_names = {
    "SteamAPI_SteamUser_v023",
    "SteamAPI_SteamUser_v022",
    "SteamAPI_SteamUser_v021",
    "SteamAPI_SteamUser_v020",
  };

  for (const char* factory_name : factory_names)
  {
    auto* factory = resolve_loaded_symbol<steam_user_factory_fn>(factory_name);
    if (factory == nullptr)
    {
      continue;
    }

    if (auto* user = factory())
    {
      return user;
    }
  }

  return nullptr;
}

inline steam_user* resolve_steam_user()
{
  if (steam_user_interface != nullptr)
  {
    return steam_user_interface;
  }

  if (auto* user = resolve_steam_user_from_api())
  {
    steam_user_interface = user;
    return steam_user_interface;
  }

  auto* steam_client_interface = resolve_steam_client();
  const auto steam_pipe = steam_pipe_handle();
  const auto steam_user_handle_value = steam_user_handle();
  if (steam_client_interface == nullptr || steam_pipe == 0 || steam_user_handle_value == 0)
  {
    return nullptr;
  }

  constexpr std::array user_versions = {
    "SteamUser023",
    "SteamUser022",
    "SteamUser021",
    "SteamUser020",
  };

  for (const char* version : user_versions)
  {
    steam_user_interface = steam_client_interface->get_steam_user_interface(steam_user_handle_value, steam_pipe, version);
    if (steam_user_interface != nullptr)
    {
      return steam_user_interface;
    }
  }

  return nullptr;
}

inline steam_user_stats* resolve_steam_user_stats_from_api()
{
  using steam_user_stats_factory_fn = steam_user_stats* (*)();

  constexpr std::array factory_names = {
    "SteamAPI_SteamUserStats_v012",
    "SteamAPI_SteamUserStats_v011",
  };

  for (const char* factory_name : factory_names)
  {
    auto* factory = resolve_loaded_symbol<steam_user_stats_factory_fn>(factory_name);
    if (factory == nullptr)
    {
      continue;
    }

    if (auto* stats = factory())
    {
      return stats;
    }
  }

  return nullptr;
}

inline steam_user_stats* resolve_steam_user_stats()
{
  if (steam_user_stats_interface != nullptr)
  {
    return steam_user_stats_interface;
  }

  if (auto* stats = resolve_steam_user_stats_from_api())
  {
    steam_user_stats_interface = stats;
    return steam_user_stats_interface;
  }

  auto* steam_client_interface = resolve_steam_client();
  if (steam_client_interface == nullptr)
  {
    return nullptr;
  }

  const auto steam_pipe = steam_pipe_handle();
  const auto steam_user_handle_value = steam_user_handle();
  if (steam_pipe == 0 || steam_user_handle_value == 0)
  {
    return nullptr;
  }

  constexpr std::array user_stats_versions = {
    "STEAMUSERSTATS_INTERFACE_VERSION012",
    "STEAMUSERSTATS_INTERFACE_VERSION011",
  };

  for (const char* version : user_stats_versions)
  {
    steam_user_stats_interface = steam_client_interface->get_steam_user_stats_interface(steam_user_handle_value, steam_pipe, version);
    if (steam_user_stats_interface != nullptr)
    {
      return steam_user_stats_interface;
    }
  }

  return nullptr;
}

inline int steam_api_steam_user_handle()
{
  using steam_api_handle_fn = int (*)();

  steam_api_handle_fn get_steam_user = resolve_loaded_symbol<steam_api_handle_fn>("SteamAPI_GetHSteamUser");
  return get_steam_user != nullptr ? get_steam_user() : 0;
}

inline int steam_api_steam_pipe_handle()
{
  using steam_api_handle_fn = int (*)();

  steam_api_handle_fn get_steam_pipe = resolve_loaded_symbol<steam_api_handle_fn>("SteamAPI_GetHSteamPipe");
  return get_steam_pipe != nullptr ? get_steam_pipe() : 0;
}

inline steam_game_coordinator* resolve_steam_game_coordinator()
{
  if (steam_game_coordinator_interface != nullptr)
  {
    return steam_game_coordinator_interface;
  }

  auto* steam_client_interface = resolve_steam_client();
  if (steam_client_interface == nullptr)
  {
    return nullptr;
  }

  int steam_pipe = steam_api_steam_pipe_handle();
  int steam_user_handle_value = steam_api_steam_user_handle();
  if (steam_pipe == 0 || steam_user_handle_value == 0)
  {
    steam_pipe = steam_pipe_handle();
    steam_user_handle_value = steam_user_handle();
  }

  if (steam_pipe == 0 || steam_user_handle_value == 0)
  {
    return nullptr;
  }

  steam_game_coordinator_interface = steam_client_interface->get_steam_generic_interface(
    steam_user_handle_value,
    steam_pipe,
    "SteamGameCoordinator001");

  return steam_game_coordinator_interface;
}

}

#endif
