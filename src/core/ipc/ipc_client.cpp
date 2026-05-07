/*
/^-----^\   data: 2026-05-05
V  o o  V  file: src/core/ipc/ipc_client.cpp
 |  Y  |   autor: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "core/ipc/ipc_client.hpp"

#include "core/entity_cache.hpp"
#include "core/ipc/ipc_shared.hpp"
#include "core/print.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/steam_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <unistd.h>
#include <vector>

namespace cat_ipc::client
{
namespace
{

struct pending_command
{
  unsigned int type = 0;
  std::string data;
};

shared_memory ipc_memory{};
shared_state* ipc_state = nullptr;
int local_peer_id = -1;
unsigned long last_command = 0;
std::time_t injected_time = 0;
bool ipc_enabled = true;
bool auto_ignore_enabled = true;
bool was_connected_to_server = false;
std::uint32_t cached_local_account_id = 0;
std::chrono::steady_clock::time_point next_connect_attempt{};
std::unordered_set<std::uint32_t> local_ipc_friends{};

[[nodiscard]] auto textmode_build() -> bool
{
#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE
  return true;
#else
  return false;
#endif
}

[[nodiscard]] auto local_player_info() -> std::optional<player_info>
{
  if (engine == nullptr)
  {
    return std::nullopt;
  }

  player_info info{};
  if (!engine->get_player_info(engine->get_localplayer_index(), &info))
  {
    return std::nullopt;
  }

  return info;
}

[[nodiscard]] auto local_account_id_from_steam() -> std::uint32_t
{
  if (cached_local_account_id != 0)
  {
    return cached_local_account_id;
  }

  if (const auto* env_id = std::getenv("CAT_STEAMID32"); env_id != nullptr)
  {
    std::uint32_t parsed_id = 0;
    const auto* end = env_id + std::strlen(env_id);
    const auto result = std::from_chars(env_id, end, parsed_id);
    if (result.ec == std::errc{} && result.ptr == end && parsed_id != 0)
    {
      cached_local_account_id = parsed_id;
      return cached_local_account_id;
    }
  }

  if (textmode_build())
  {
    return 0;
  }

  auto* user = steam_runtime::resolve_steam_user();
  if (user == nullptr)
  {
    return 0;
  }

  const auto steam_id = user->get_steam_id();
  cached_local_account_id = static_cast<std::uint32_t>(steam_id & 0xffffffffull);
  return cached_local_account_id;
}

[[nodiscard]] auto bot_name_from_environment() -> std::string_view
{
  const auto* name = std::getenv("CAT_BOT_NAME");
  return name != nullptr ? std::string_view{name} : std::string_view{};
}

[[nodiscard]] auto sanitize_map_name(const char* raw_name) -> std::string_view
{
  return raw_name != nullptr ? std::string_view{raw_name} : std::string_view{};
}

[[nodiscard]] auto server_address() -> std::string_view
{
  if (client_state == nullptr || client_state->m_NetChannel == nullptr)
  {
    return {};
  }

  const char* address = client_state->m_NetChannel->get_address();
  return address != nullptr ? std::string_view{address} : std::string_view{};
}

[[nodiscard]] auto count_players() -> int
{
  auto count = 0;
  for (auto* entity : entity_cache[class_id::PLAYER])
  {
    auto* player = reinterpret_cast<Player*>(entity);
    if (player != nullptr && !player->is_dormant())
    {
      ++count;
    }
  }

  return count;
}

[[nodiscard]] auto count_local_ipc_bots_on_server() -> int
{
  auto count = 0;
  for (auto* entity : entity_cache[class_id::PLAYER])
  {
    auto* player = reinterpret_cast<Player*>(entity);
    if (player == nullptr || player->is_dormant() || engine == nullptr)
    {
      continue;
    }

    player_info info{};
    if (!engine->get_player_info(player->get_index(), &info))
    {
      continue;
    }

    if (info.friends_id != 0 && is_local_ipc_friend(static_cast<std::uint32_t>(info.friends_id)))
    {
      ++count;
    }
  }

  return count;
}

void refresh_local_ipc_friends_locked()
{
  local_ipc_friends.clear();
  if (ipc_state == nullptr)
  {
    return;
  }

  const auto now = now_seconds();
  for (auto index = 0u; index < max_peers; ++index)
  {
    if (!peer_alive(ipc_state->peer_data[index], now))
    {
      continue;
    }

    const auto friend_id = ipc_state->peer_user_data[index].friendid;
    if (friend_id != 0)
    {
      local_ipc_friends.insert(friend_id);
    }
  }
}

void mark_peer_free()
{
  if (ipc_state == nullptr || local_peer_id < 0 || local_peer_id >= static_cast<int>(max_peers))
  {
    return;
  }

  try_scoped_lock lock{ipc_state};
  if (!lock.locked())
  {
    return;
  }

  ipc_state->peer_data[local_peer_id].free = true;
}

[[nodiscard]] auto find_free_peer_slot_locked() -> int
{
  const auto now = now_seconds();
  for (auto index = 0u; index < max_peers; ++index)
  {
    auto& peer = ipc_state->peer_data[index];
    if (peer.free || !peer_alive(peer, now))
    {
      return static_cast<int>(index);
    }
  }

  return -1;
}

[[nodiscard]] bool valid_local_peer_id()
{
  return local_peer_id >= 0 && local_peer_id < static_cast<int>(max_peers);
}

[[nodiscard]] bool store_initial_peer_data_locked()
{
  if (ipc_state == nullptr || !valid_local_peer_id())
  {
    return false;
  }

  auto& peer = ipc_state->peer_data[local_peer_id];
  auto& data = ipc_state->peer_user_data[local_peer_id];
  const auto now = now_seconds();

  std::memset(&peer, 0, sizeof(peer));
  std::memset(&data, 0, sizeof(data));

  peer.free = false;
  peer.pid = getpid();
  peer.starttime = read_process_start_time(peer.pid);
  peer.heartbeat = now;

  data.textmode = textmode_build();
  data.heartbeat = now;
  data.ts_injected = now;
  data.friendid = local_account_id_from_steam();
  copy_cstr(data.name, sizeof(data.name), bot_name_from_environment());
  return true;
}

void try_connect()
{
  if (!ipc_enabled || ipc_state != nullptr)
  {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (now < next_connect_attempt)
  {
    return;
  }

  next_connect_attempt = now + std::chrono::seconds(2);

  try
  {
    ipc_memory = shared_memory::open_client();
    ipc_state = ipc_memory.state();
    if (ipc_state == nullptr)
    {
      ipc_memory.close();
      local_peer_id = -1;
      return;
    }

    auto no_available_slot = false;
    {
      try_scoped_lock lock{ipc_state};
      if (!lock.locked())
      {
        ipc_memory.close();
        ipc_state = nullptr;
        local_peer_id = -1;
        return;
      }

      local_peer_id = find_free_peer_slot_locked();
      if (local_peer_id < 0)
      {
        no_available_slot = true;
      }
      else
      {
        if (!store_initial_peer_data_locked())
        {
          ipc_memory.close();
          ipc_state = nullptr;
          local_peer_id = -1;
          return;
        }

        last_command = ipc_state->command_count;
        refresh_local_ipc_friends_locked();
      }
    }

    if (no_available_slot)
    {
      ipc_memory.close();
      ipc_state = nullptr;
      local_peer_id = -1;
      print("[ipc] no available catbot peer slots\n");
      return;
    }

    injected_time = now_seconds();
    print("[ipc] connected to catbot ipc as peer %d\n", local_peer_id);
  }
  catch (const std::exception& error)
  {
    print("[ipc] connect failed: %s\n", error.what());
    ipc_memory.close();
    ipc_state = nullptr;
    local_peer_id = -1;
  }
}

void update_peer_count_locked()
{
  if (ipc_state == nullptr)
  {
    return;
  }

  auto count = 0u;
  const auto now = now_seconds();
  for (auto& peer : ipc_state->peer_data)
  {
    if (!peer.free && !peer_alive(peer, now))
    {
      peer.free = true;
    }

    if (!peer.free)
    {
      ++count;
    }
  }

  ipc_state->peer_count = count;
}

void update_telemetry_locked()
{
  if (ipc_state == nullptr || !valid_local_peer_id())
  {
    return;
  }

  auto& peer = ipc_state->peer_data[local_peer_id];
  auto& data = ipc_state->peer_user_data[local_peer_id];
  const auto now = now_seconds();
  const auto in_game = engine != nullptr && engine->is_in_game();
  const auto connected_to_server = engine != nullptr && engine->is_connected();

  peer.heartbeat = now;
  data.heartbeat = now;
  data.textmode = textmode_build();
  data.connected = in_game;
  data.ts_injected = injected_time;

  if (connected_to_server && !was_connected_to_server)
  {
    data.ts_connected = now;
  }
  else if (!connected_to_server && was_connected_to_server)
  {
    data.ts_disconnected = now;
  }
  was_connected_to_server = connected_to_server;

  if (const auto info = local_player_info())
  {
    if (info->friends_id != 0)
    {
      cached_local_account_id = static_cast<std::uint32_t>(info->friends_id);
      data.friendid = static_cast<unsigned int>(cached_local_account_id);
    }
    copy_cstr(data.name, sizeof(data.name), info->name);
  }
  else
  {
    const auto account_id = local_account_id_from_steam();
    if (account_id != 0)
    {
      data.friendid = static_cast<unsigned int>(account_id);
    }

    if (data.name[0] == '\0')
    {
      copy_cstr(data.name, sizeof(data.name), bot_name_from_environment());
    }
  }

  copy_cstr(data.ingame.server, sizeof(data.ingame.server), server_address());
  copy_cstr(data.ingame.mapname, sizeof(data.ingame.mapname), sanitize_map_name(engine != nullptr ? engine->get_level_name() : nullptr));
  data.ingame.good = false;
  data.ingame.player_count = 0;
  data.ingame.bot_count = 0;

  if (!in_game || entity_list == nullptr)
  {
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr)
  {
    return;
  }

  data.ingame.good = true;
  data.ingame.player_count = count_players();
  data.ingame.bot_count = count_local_ipc_bots_on_server();
  data.ingame.team = static_cast<int>(localplayer->get_team());
  data.ingame.role = static_cast<int>(localplayer->get_tf_class());
  data.ingame.life_state = static_cast<char>(localplayer->get_lifestate());
  data.ingame.health = localplayer->get_health();
  data.ingame.health_max = localplayer->get_max_health();
  const auto origin = localplayer->get_origin();
  data.ingame.x = origin.x;
  data.ingame.y = origin.y;
  data.ingame.z = origin.z;
}

void collect_commands(std::vector<pending_command>& commands_out)
{
  commands_out.clear();
  if (ipc_state == nullptr || local_peer_id < 0)
  {
    return;
  }

  try_scoped_lock lock{ipc_state};
  if (!lock.locked())
  {
    return;
  }

  const auto newest_command = ipc_state->command_count;
  if (newest_command <= last_command)
  {
    return;
  }

  const auto first_command = std::max(last_command + 1, newest_command > command_ring_size ? newest_command - command_ring_size + 1 : 1ul);
  commands_out.reserve(static_cast<std::size_t>(newest_command - first_command + 1));

  for (auto command_number = first_command; command_number <= newest_command; ++command_number)
  {
    const auto& command = ipc_state->commands[command_number % command_ring_size];
    if (command.command_number != command_number)
    {
      continue;
    }

    if (command.target_peer >= 0 && command.target_peer != local_peer_id)
    {
      continue;
    }

    if (command.sender == local_peer_id)
    {
      continue;
    }

    if (command.cmd_type == commands::execute_client_cmd_long)
    {
      if (const auto* payload = command_payload(ipc_state, command); payload != nullptr)
      {
        commands_out.emplace_back(pending_command{command.cmd_type, payload});
      }

      continue;
    }

    const auto* command_text = reinterpret_cast<const char*>(command.cmd_data);
    if (command_text == nullptr || command_text[0] == '\0')
    {
      continue;
    }

    commands_out.emplace_back(pending_command{
      command.cmd_type,
      std::string{command_text, strnlen(command_text, command_data_size)}
    });
  }

  last_command = newest_command;
}

void process_collected_commands(const std::vector<pending_command>& commands_to_process)
{
  for (const auto& command : commands_to_process)
  {
    if ((command.type == commands::execute_client_cmd || command.type == commands::execute_client_cmd_long) &&
      engine != nullptr && !command.data.empty())
    {
      engine->client_cmd_unrestricted(command.data.c_str());
    }
  }
}

} // namespace

void set_enabled(bool enabled)
{
  ipc_enabled = enabled;
  if (!ipc_enabled)
  {
    shutdown();
  }
}

void set_auto_ignore_enabled(bool enabled)
{
  auto_ignore_enabled = enabled;
}

void start()
{
  try_connect();
}

void tick()
{
  const auto force_ipc = textmode_build();
  set_enabled(force_ipc || config.ipc.enabled);
  set_auto_ignore_enabled(config.ipc.auto_ignore_local_bots);
  if (ipc_state == nullptr && !force_ipc && !config.ipc.auto_connect)
  {
    return;
  }

  if (ipc_state == nullptr)
  {
    try_connect();
  }

  if (ipc_state == nullptr)
  {
    return;
  }

  std::vector<pending_command> commands_to_process{};
  collect_commands(commands_to_process);

  {
    try_scoped_lock lock{ipc_state};
    if (lock.locked())
    {
      update_peer_count_locked();
      refresh_local_ipc_friends_locked();
      update_telemetry_locked();
    }
  }

  process_collected_commands(commands_to_process);
}

void shutdown()
{
  mark_peer_free();
  ipc_memory.close();
  ipc_state = nullptr;
  local_peer_id = -1;
  last_command = 0;
  local_ipc_friends.clear();
}

bool connected()
{
  return ipc_state != nullptr && local_peer_id >= 0;
}

int peer_id()
{
  return local_peer_id;
}

bool is_local_ipc_friend(std::uint32_t friend_id)
{
  return auto_ignore_enabled && friend_id != 0 && local_ipc_friends.contains(friend_id);
}

} // namespace cat_ipc::client
