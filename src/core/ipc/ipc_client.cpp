/*
/^-----^\   data: 2026-05-05
V  o o  V  file: src/core/ipc/ipc_client.cpp
 |  Y  |   author: pupnoodle
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
#include "games/tf2/sdk/interfaces/game_event_manager.hpp"
#include "games/tf2/sdk/interfaces/steam_runtime.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <unistd.h>
#include <vector>

namespace pup_ipc::client
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
std::uint32_t connected_state_version = 0;
pid_t connected_server_pid = 0;
unsigned long connected_server_starttime = 0;
unsigned long last_command = 0;
std::time_t injected_time = 0;
std::atomic_bool ipc_enabled = true;
std::atomic_bool auto_ignore_enabled = true;
std::atomic_bool in_casual_queue_state = false;
bool was_connected_to_server = false;
std::uint32_t cached_local_account_id = 0;
std::chrono::steady_clock::time_point next_connect_attempt{};
std::chrono::steady_clock::time_point game_telemetry_ready_since{};
std::mutex ipc_mutex{};
std::thread ipc_worker{};
std::atomic_bool ipc_worker_running = false;

struct ipc_worker_guard
{
  ~ipc_worker_guard()
  {
    ipc_worker_running.store(false, std::memory_order_release);
    if (ipc_worker.joinable() && ipc_worker.get_id() != std::this_thread::get_id()) {
      ipc_worker.join();
    }
  }
};
inline ipc_worker_guard ipc_worker_guard_instance{};
std::atomic_bool ipc_worker_sweep_active = false;
std::shared_mutex local_ipc_friends_mutex{};
constexpr std::size_t max_local_ipc_friends = static_cast<std::size_t>(max_peers);
std::array<std::uint32_t, max_local_ipc_friends> local_ipc_friends{};
std::size_t local_ipc_friend_count = 0;
std::mutex pending_commands_mutex{};
std::vector<pending_command> pending_commands{};
constexpr std::size_t max_pending_commands = command_ring_size;

[[nodiscard]] auto textmode_build() -> bool
{
#if defined(PUPHOOK_TEXTMODE) && PUPHOOK_TEXTMODE

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

  if (const auto* env_id = std::getenv("PUP_STEAMID32"); env_id != nullptr)
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
  const auto* name = std::getenv("PUP_BOT_NAME");
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

[[nodiscard]] auto game_state_ready_for_telemetry() -> bool
{
  return engine != nullptr &&
         entity_list != nullptr &&
         engine->is_connected() &&
         engine->is_in_game() &&
         !engine->is_drawing_loading_image();
}

[[nodiscard]] auto settled_game_state_ready_for_telemetry() -> bool
{
  constexpr auto settle_time = std::chrono::seconds(1);
  if (!game_state_ready_for_telemetry())
  {
    game_telemetry_ready_since = {};
    return false;
  }

  const auto now = std::chrono::steady_clock::now();
  if (game_telemetry_ready_since.time_since_epoch().count() == 0)
  {
    game_telemetry_ready_since = now;
    return false;
  }

  return now - game_telemetry_ready_since >= settle_time;
}

void reset_ingame_telemetry(user_data_s& data)
{
  std::memset(&data.ingame, 0, sizeof(data.ingame));
}

void reset_game_telemetry_locked(user_data_s& data)
{
  data.connected = false;
  reset_ingame_telemetry(data);
  game_telemetry_ready_since = {};
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

[[nodiscard]] bool valid_local_peer_id()
{
  return local_peer_id >= 0 && local_peer_id < static_cast<int>(max_peers);
}

void refresh_local_ipc_friends_locked()
{
  std::array<std::uint32_t, max_local_ipc_friends> refreshed_friends{};
  std::size_t refreshed_friend_count = 0;

  if (ipc_state != nullptr)
  {
    const auto now = now_seconds();
    const auto local_valid = valid_local_peer_id();
    const auto& local_peer = local_valid ? ipc_state->peer_data[local_peer_id] : ipc_state->peer_data[0];
    const auto& local_data = local_valid ? ipc_state->peer_user_data[local_peer_id] : ipc_state->peer_user_data[0];
    const bool local_ready = local_valid &&
      peer_alive(local_peer, now) &&
      local_data.connected &&
      local_data.ingame.good &&
      local_data.ingame.server[0] != '\0';
    for (auto index = 0u; index < max_peers; ++index)
    {
      if (!local_ready ||
          static_cast<int>(index) == local_peer_id ||
          !peer_alive(ipc_state->peer_data[index], now))
      {
        continue;
      }

      const auto& data = ipc_state->peer_user_data[index];
      if (!data.connected ||
          !data.ingame.good ||
          data.ingame.server[0] == '\0' ||
          std::strncmp(data.ingame.server, local_data.ingame.server, sizeof(data.ingame.server)) != 0)
      {
        continue;
      }

      const auto friend_id = ipc_state->peer_user_data[index].friendid;
      if (friend_id != 0 && refreshed_friend_count < refreshed_friends.size())
      {
        auto refreshed_friend_end = refreshed_friends.begin() + static_cast<std::ptrdiff_t>(refreshed_friend_count);
        if (std::find(refreshed_friends.begin(), refreshed_friend_end, friend_id) == refreshed_friend_end)
        {
          refreshed_friends[refreshed_friend_count] = friend_id;
          ++refreshed_friend_count;
        }
      }
    }
  }

  std::unique_lock lock{local_ipc_friends_mutex};
  local_ipc_friends = refreshed_friends;
  local_ipc_friend_count = refreshed_friend_count;
}

void clear_local_ipc_friends()
{
  std::unique_lock lock{local_ipc_friends_mutex};
  local_ipc_friends.fill(0);
  local_ipc_friend_count = 0;
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
  peer.pid = read_host_pid();

  peer.starttime = read_process_start_time(getpid());
  peer.heartbeat = now;

  data.textmode = textmode_build();
  data.heartbeat = now;
  data.ts_injected = now;
  copy_cstr(data.name, sizeof(data.name), bot_name_from_environment());
  return true;
}

void try_connect()
{
  if (!ipc_enabled.load(std::memory_order_acquire) || ipc_state != nullptr)
  {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (now < next_connect_attempt)
  {
    return;
  }

  next_connect_attempt = now + std::chrono::seconds(10);

  try
  {
    ipc_memory = shared_memory::open_client();
    ipc_state = ipc_memory.state();
    if (ipc_state == nullptr)
    {
      ipc_memory.close();
      ipc_state = nullptr;
      local_peer_id = -1;
      connected_state_version = 0;
      connected_server_pid = 0;
      connected_server_starttime = 0;
      return;
    }

    auto connection_failed = false;
    auto no_available_slot = false;
    {
      try_scoped_lock lock{ipc_state};
      if (!lock.locked())
      {
        connection_failed = true;
      }
      else
      {
        local_peer_id = find_free_peer_slot_locked();
        if (local_peer_id < 0)
        {
          no_available_slot = true;
        }
        else if (!store_initial_peer_data_locked())
        {
          connection_failed = true;
        }
        else
        {
          last_command = ipc_state->command_count;
          connected_state_version = ipc_state->global_data.state_version;
          connected_server_pid = ipc_state->global_data.pid;
          connected_server_starttime = ipc_state->global_data.starttime;
          refresh_local_ipc_friends_locked();
        }
      }
    }

    if (connection_failed || no_available_slot)
    {
      ipc_memory.close();
      ipc_state = nullptr;
      local_peer_id = -1;
      connected_state_version = 0;
      connected_server_pid = 0;
      connected_server_starttime = 0;
      if (no_available_slot)
      {
        print("[ipc] no available pupbot peer slots\n");
      }
      return;
    }

    injected_time = now_seconds();
    const auto connected_pid = ipc_state->peer_data[local_peer_id].pid;
    print("[ipc] connected to pupbot ipc as peer %d host_pid=%d ns_pid=%d\n",
      local_peer_id,
      static_cast<int>(connected_pid),
      static_cast<int>(getpid()));
  }
  catch (const std::exception& error)
  {
    print("[ipc] connect failed: %s\n", error.what());
    ipc_memory.close();
    ipc_state = nullptr;
    local_peer_id = -1;
    connected_state_version = 0;
    connected_server_pid = 0;
    connected_server_starttime = 0;
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

[[nodiscard]] auto local_peer_registered_locked() -> bool
{
  if (ipc_state == nullptr || !valid_local_peer_id())
  {
    return false;
  }

  const auto& peer = ipc_state->peer_data[local_peer_id];
  return peer_identity_valid(peer) && peer.pid == read_host_pid() &&
    peer.starttime == read_process_start_time(getpid());
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
  const auto connected_to_server = engine != nullptr && engine->is_connected();
  const auto ready_for_game_telemetry = settled_game_state_ready_for_telemetry();

  peer.heartbeat = now;
  data.heartbeat = now;
  data.textmode = textmode_build();
  data.connected = ready_for_game_telemetry;
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

  const bool in_queue = in_casual_queue_state.load(std::memory_order_acquire) && !connected_to_server;
  if (in_queue)
  {
    if (data.ts_queue_started == 0)
    {
      data.ts_queue_started = now;
    }
  }
  else
  {
    data.ts_queue_started = 0;
  }

  if (ready_for_game_telemetry)
  {
    if (const auto info = local_player_info())
    {
      if (info->friends_id != 0 && !info->fakeplayer && cached_local_account_id == 0)
      {
        cached_local_account_id = static_cast<std::uint32_t>(info->friends_id);
      }
      if (cached_local_account_id != 0)
      {
        data.friendid = static_cast<unsigned int>(cached_local_account_id);
      }
      copy_cstr(data.name, sizeof(data.name), info->name);
    }
  }

  if (data.friendid == 0)
  {
    const auto account_id = local_account_id_from_steam();
    if (account_id != 0)
    {
      data.friendid = static_cast<unsigned int>(account_id);
    }
  }

  if (data.name[0] == '\0')
  {
    copy_cstr(data.name, sizeof(data.name), bot_name_from_environment());
  }

  reset_ingame_telemetry(data);
  if (!ready_for_game_telemetry)
  {
    return;
  }

  copy_cstr(data.ingame.server, sizeof(data.ingame.server), server_address());
  copy_cstr(data.ingame.mapname, sizeof(data.ingame.mapname), sanitize_map_name(engine != nullptr ? engine->get_level_name() : nullptr));

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

void update_basic_telemetry_locked()
{
  if (ipc_state == nullptr || !valid_local_peer_id())
  {
    return;
  }

  auto& peer = ipc_state->peer_data[local_peer_id];
  auto& data = ipc_state->peer_user_data[local_peer_id];
  const auto now = now_seconds();

  peer.heartbeat = now;
  data.heartbeat = now;
  data.textmode = textmode_build();
  data.ts_injected = injected_time;

  const bool basic_in_queue = in_casual_queue_state.load(std::memory_order_acquire);
  if (basic_in_queue)
  {
    if (data.ts_queue_started == 0)
    {
      data.ts_queue_started = now;
    }
  }
  else
  {
    data.ts_queue_started = 0;
  }

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

    if (command.sender >= 0 && (command.sender >= static_cast<int>(max_peers) ||
        !peer_identity_valid(ipc_state->peer_data[command.sender])))
    {
      continue;
    }

    if (command.sender >= 0 &&
        (command.sender_pid != ipc_state->peer_data[command.sender].pid ||
         command.sender_starttime != ipc_state->peer_data[command.sender].starttime))
    {
      continue;
    }
    if (command.sender < 0 &&
        (command.sender_pid != ipc_state->global_data.pid ||
         command.sender_starttime != ipc_state->global_data.starttime))
    {
      continue;
    }

    if (command.cmd_type == commands::execute_client_cmd_long)
    {
      if (const auto payload = command_payload(ipc_state, command); payload.has_value())
      {
        commands_out.emplace_back(pending_command{command.cmd_type, payload->as_string()});
      }

      continue;
    }

    const auto command_length = strnlen(reinterpret_cast<const char*>(command.cmd_data), command_data_size);
    if (command_length == 0 || command_length >= command_data_size)
    {
      continue;
    }

    commands_out.emplace_back(pending_command{
      command.cmd_type,
      std::string{reinterpret_cast<const char*>(command.cmd_data), command_length}
    });
  }

  last_command = newest_command;
}

[[nodiscard]] auto executable_command(const pending_command& command) -> bool
{
  return (command.type == commands::execute_client_cmd || command.type == commands::execute_client_cmd_long) &&
         !command.data.empty();
}

void process_follow_target_command(const pending_command& command)
{
  if (command.type != commands::set_follow_steamid)
  {
    return;
  }

  std::uint64_t steam_id = 0;
  const auto* begin = command.data.data();
  const auto* end = begin + command.data.size();
  const auto result = std::from_chars(begin, end, steam_id);
  if (result.ec == std::errc{} && result.ptr == end)
  {

    followbot::controller().set_ipc_target(static_cast<std::uint32_t>(steam_id & 0xffffffffu));
  }
}

void queue_commands_for_game_thread(std::vector<pending_command>& commands_to_queue)
{
  if (commands_to_queue.empty())
  {
    return;
  }

  std::lock_guard lock{pending_commands_mutex};
  pending_commands.reserve(std::min(max_pending_commands, pending_commands.size() + commands_to_queue.size()));
  for (auto& command : commands_to_queue)
  {
    pending_commands.emplace_back(std::move(command));
  }

  if (pending_commands.size() > max_pending_commands)
  {
    pending_commands.erase(
      pending_commands.begin(),
      pending_commands.begin() + static_cast<std::ptrdiff_t>(pending_commands.size() - max_pending_commands));
  }
}

void clear_pending_commands()
{
  std::lock_guard lock{pending_commands_mutex};
  pending_commands.clear();
}

void process_pending_commands()
{
  if (engine == nullptr)
  {
    return;
  }

  std::vector<pending_command> commands_to_process{};
  {
    std::lock_guard lock{pending_commands_mutex};
    if (pending_commands.empty())
    {
      return;
    }

    commands_to_process = std::move(pending_commands);
    pending_commands.clear();
  }

  for (const auto& command : commands_to_process)
  {
    if (command.type == commands::set_follow_steamid)
    {
      process_follow_target_command(command);
      continue;
    }

    if (executable_command(command))
    {
      engine->client_cmd_unrestricted(command.data.c_str());
    }
  }
}

void service_ipc_locked(bool full_telemetry, bool process_commands)
{
  if (ipc_state == nullptr)
  {
    try_connect();
  }

  if (ipc_state == nullptr)
  {
    return;
  }

  auto needs_reconnect = !ipc_memory.maps_current_object();
  if (!needs_reconnect)
  {
    try_scoped_lock lock{ipc_state};
    if (lock.locked())
    {
      if (!ipc_memory.owns_valid_state() ||
          ipc_state->global_data.state_version != connected_state_version ||
          ipc_state->global_data.pid != connected_server_pid ||
          ipc_state->global_data.starttime != connected_server_starttime ||
          !local_peer_registered_locked())
      {
        needs_reconnect = true;
      }
      else
      {
        update_peer_count_locked();
        refresh_local_ipc_friends_locked();
        if (full_telemetry)
        {
          update_telemetry_locked();
        }
        else
        {
          update_basic_telemetry_locked();
        }
      }
    }
  }

  if (needs_reconnect)
  {
    print("[ipc] local peer slot was reset, reconnecting\n");
    ipc_memory.close();
    ipc_state = nullptr;
    local_peer_id = -1;
    connected_state_version = 0;
    connected_server_pid = 0;
    connected_server_starttime = 0;
    last_command = 0;
    clear_local_ipc_friends();
    clear_pending_commands();
    try_connect();
    return;
  }

  std::vector<pending_command> commands_to_process{};
  if (process_commands)
  {
    collect_commands(commands_to_process);
  }
  queue_commands_for_game_thread(commands_to_process);
}

void ipc_worker_main()
{
  while (ipc_worker_running.load())
  {
    ipc_worker_sweep_active.store(true, std::memory_order_release);
    {
      std::lock_guard lock{ipc_mutex};
      if (ipc_enabled.load(std::memory_order_acquire))
      {

        service_ipc_locked(false, true);
      }
    }
    ipc_worker_sweep_active.store(false, std::memory_order_release);

    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  ipc_worker_sweep_active.store(false, std::memory_order_release);
}

void start_ipc_worker()
{
  if (!textmode_build() || ipc_worker_running.exchange(true))
  {
    return;
  }

  ipc_worker = std::thread{ipc_worker_main};
}

void stop_ipc_worker()
{
  ipc_worker_running.store(false, std::memory_order_release);

  if (ipc_worker.joinable() && ipc_worker.get_id() != std::this_thread::get_id())
  {
    ipc_worker.join();
  }
}

}

void set_enabled(bool enabled)
{
  ipc_enabled.store(textmode_build() || enabled, std::memory_order_release);
  if (!ipc_enabled.load(std::memory_order_acquire))
  {
    shutdown();
  }
}

void set_auto_ignore_enabled(bool enabled)
{
  auto_ignore_enabled.store(textmode_build() || enabled, std::memory_order_release);
}

void set_in_casual_queue(bool in_queue)
{
  in_casual_queue_state.store(in_queue, std::memory_order_release);
}

void start()
{
  {
    std::lock_guard lock{ipc_mutex};
    try_connect();
  }
  start_ipc_worker();
}

void tick()
{
  const auto force_ipc = textmode_build();
  if (force_ipc)
  {
    config.ipc.enabled = true;
    config.ipc.auto_connect = true;
    config.ipc.auto_ignore_local_bots = true;
  }

  set_enabled(config.ipc.enabled);
  set_auto_ignore_enabled(config.ipc.auto_ignore_local_bots);

  if (ipc_worker_sweep_active.load(std::memory_order_acquire))
  {
    return;
  }

  std::unique_lock lock{ipc_mutex, std::try_to_lock};
  if (!lock.owns_lock())
  {
    return;
  }

  if (ipc_state == nullptr && !config.ipc.auto_connect)
  {
    return;
  }

  static std::chrono::steady_clock::time_point last_full_telemetry{};
  const auto now = std::chrono::steady_clock::now();
  const bool full_telemetry = last_full_telemetry.time_since_epoch().count() == 0
      || now - last_full_telemetry >= std::chrono::seconds(1);
  if (full_telemetry)
  {
    last_full_telemetry = now;
  }
  service_ipc_locked(full_telemetry, true);
  process_pending_commands();
}

[[nodiscard]] bool event_user_is_local(GameEvent* event, const char* key)
{
  if (event == nullptr || engine == nullptr)
  {
    return false;
  }

  const int user_id = event->get_int(key);
  if (user_id <= 0)
  {
    return false;
  }

  const int player_index = engine->get_player_index_from_id(user_id);
  return player_index > 0 && player_index == engine->get_localplayer_index();
}

void update_stats_for_event_locked(GameEvent* event, const char* event_name)
{
  if (!valid_local_peer_id())
  {
    return;
  }

  auto& data = ipc_state->peer_user_data[local_peer_id];

  if (std::strcmp(event_name, "player_death") == 0)
  {
    const bool victim_is_local = event_user_is_local(event, "userid");
    const bool attacker_is_local = event_user_is_local(event, "attacker");
    const bool suicide = victim_is_local && attacker_is_local;

    if (victim_is_local)
    {
      ++data.accumulated.deaths;
      ++data.ingame.deaths;
    }
    if (attacker_is_local && !suicide)
    {
      ++data.accumulated.kills;
      ++data.ingame.kills;
      ++data.accumulated.score;
      ++data.ingame.score;
      if (event->get_int("customkill") == 1)
      {
        ++data.accumulated.headshots;
        ++data.ingame.headshots;
      }
    }
    return;
  }

  if (std::strcmp(event_name, "player_hurt") == 0)
  {
    const bool attacker_is_local = event_user_is_local(event, "attacker");
    const bool victim_is_local = event_user_is_local(event, "userid");
    if (attacker_is_local && !victim_is_local)
    {
      ++data.accumulated.hits;
      ++data.ingame.hits;
    }
    return;
  }

  if (std::strcmp(event_name, "player_shoot") == 0 || std::strcmp(event_name, "weapon_fire") == 0)
  {
    if (event_user_is_local(event, "userid"))
    {
      ++data.accumulated.shots;
      ++data.ingame.shots;
    }
    return;
  }
}

void on_game_event(GameEvent* event)
{
  if (event == nullptr)
  {
    return;
  }

  std::unique_lock ipc_lock{ipc_mutex, std::try_to_lock};
  if (!ipc_lock.owns_lock())
  {
    return;
  }
  if (ipc_state == nullptr || !valid_local_peer_id())
  {
    return;
  }

  const char* event_name = event->get_name();
  if (event_name == nullptr)
  {
    return;
  }

  const auto begins_connection = std::strcmp(event_name, "client_beginconnect") == 0;
  const auto connected_to_map = std::strcmp(event_name, "client_connected") == 0 ||
                                std::strcmp(event_name, "game_newmap") == 0;
  const auto disconnected_from_map = std::strcmp(event_name, "client_disconnect") == 0;
  const auto is_stat_event =
    std::strcmp(event_name, "player_death") == 0 ||
    std::strcmp(event_name, "player_hurt") == 0 ||
    std::strcmp(event_name, "player_shoot") == 0 ||
    std::strcmp(event_name, "weapon_fire") == 0;

  if (!begins_connection && !connected_to_map && !disconnected_from_map && !is_stat_event)
  {
    return;
  }

  try_scoped_lock lock{ipc_state};
  if (!lock.locked() || !valid_local_peer_id())
  {
    return;
  }

  if (is_stat_event)
  {
    update_stats_for_event_locked(event, event_name);
    return;
  }

  auto& data = ipc_state->peer_user_data[local_peer_id];
  const auto now = now_seconds();
  reset_game_telemetry_locked(data);

  if (connected_to_map)
  {
    data.ts_connected = now;
    was_connected_to_server = true;
    in_casual_queue_state.store(false, std::memory_order_release);
    data.ts_queue_started = 0;
  }
  else if (disconnected_from_map)
  {
    data.ts_disconnected = now;
    was_connected_to_server = false;
  }
}

void shutdown()
{
  stop_ipc_worker();
  std::lock_guard lock{ipc_mutex};
  mark_peer_free();
  ipc_memory.close();
  ipc_state = nullptr;
  local_peer_id = -1;
  connected_state_version = 0;
  connected_server_pid = 0;
  connected_server_starttime = 0;
  last_command = 0;
  game_telemetry_ready_since = {};
  clear_local_ipc_friends();
  clear_pending_commands();
}

bool connected()
{
  std::lock_guard lock{ipc_mutex};
  return ipc_state != nullptr && local_peer_id >= 0;
}

int peer_id()
{
  std::lock_guard lock{ipc_mutex};
  return local_peer_id;
}

int local_ipc_peer_count_on_current_server()
{
  std::lock_guard ipc_lock{ipc_mutex};
  if (ipc_state == nullptr || !valid_local_peer_id())
  {
    return 0;
  }

  try_scoped_lock lock{ipc_state};
  if (!lock.locked() || !valid_local_peer_id())
  {
    return 0;
  }

  const auto now = now_seconds();
  const auto& local_peer = ipc_state->peer_data[local_peer_id];
  const auto& local_data = ipc_state->peer_user_data[local_peer_id];
  if (!peer_alive(local_peer, now) ||
      !local_data.connected ||
      !local_data.ingame.good ||
      local_data.ingame.server[0] == '\0')
  {
    return 0;
  }

  int same_server_count = 0;
  for (auto index = 0u; index < max_peers; ++index)
  {
    const auto& peer = ipc_state->peer_data[index];
    const auto& data = ipc_state->peer_user_data[index];
    if (!peer_alive(peer, now) ||
        !data.connected ||
        !data.ingame.good ||
        data.ingame.server[0] == '\0' ||
        std::strncmp(data.ingame.server, local_data.ingame.server, sizeof(data.ingame.server)) != 0)
    {
      continue;
    }

    ++same_server_count;
  }

  return same_server_count;
}

bool is_first_local_ipc_peer_on_current_server()
{
  std::lock_guard ipc_lock{ipc_mutex};
  if (ipc_state == nullptr || !valid_local_peer_id())
  {
    return false;
  }

  try_scoped_lock lock{ipc_state};
  if (!lock.locked() || !valid_local_peer_id())
  {
    return false;
  }

  const auto now = now_seconds();
  const auto& local_peer = ipc_state->peer_data[local_peer_id];
  const auto& local_data = ipc_state->peer_user_data[local_peer_id];
  if (!peer_alive(local_peer, now) ||
      !local_data.connected ||
      !local_data.ingame.good ||
      local_data.ingame.server[0] == '\0')
  {
    return false;
  }

  const auto local_connected_time = local_data.ts_connected != 0 ? local_data.ts_connected : local_peer.starttime;
  for (auto index = 0u; index < max_peers; ++index)
  {
    if (static_cast<int>(index) == local_peer_id)
    {
      continue;
    }

    const auto& peer = ipc_state->peer_data[index];
    const auto& data = ipc_state->peer_user_data[index];
    if (!peer_alive(peer, now) ||
        !data.connected ||
        !data.ingame.good ||
        data.ingame.server[0] == '\0' ||
        std::strncmp(data.ingame.server, local_data.ingame.server, sizeof(data.ingame.server)) != 0)
    {
      continue;
    }

    const auto connected_time = data.ts_connected != 0 ? data.ts_connected : peer.starttime;
    if (connected_time < local_connected_time ||
        (connected_time == local_connected_time && index < static_cast<unsigned int>(local_peer_id)))
    {
      return false;
    }
  }

  return true;
}

std::vector<std::uint32_t> ipc_peer_friend_ids_by_injection_time(int max_count)
{
  std::vector<std::uint32_t> ids;
  if (max_count <= 0)
  {
    return ids;
  }

  std::vector<std::pair<std::time_t, std::uint32_t>> peers;
  {
    std::lock_guard ipc_lock{ipc_mutex};
    if (ipc_state == nullptr || !valid_local_peer_id())
    {
      return ids;
    }

    try_scoped_lock lock{ipc_state};
    if (!lock.locked())
    {
      return ids;
    }

    const auto now = now_seconds();
    for (auto index = 0u; index < max_peers; ++index)
    {
      const auto& peer = ipc_state->peer_data[index];
      const auto& data = ipc_state->peer_user_data[index];
      if (peer.free || !peer_alive(peer, now) || !data.connected || data.friendid == 0)
      {
        continue;
      }
      peers.emplace_back(data.ts_injected, data.friendid);
    }
  }

  std::sort(peers.begin(), peers.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });
  for (const auto& peer : peers)
  {
    if (static_cast<int>(ids.size()) >= max_count)
    {
      break;
    }
    ids.push_back(peer.second);
  }
  return ids;
}

bool is_known_local_ipc_friend(std::uint32_t friend_id)
{
  if (friend_id == 0)
  {
    return false;
  }

  std::shared_lock lock{local_ipc_friends_mutex};
  auto local_ipc_friend_end = local_ipc_friends.begin() + static_cast<std::ptrdiff_t>(local_ipc_friend_count);
  return std::find(local_ipc_friends.begin(), local_ipc_friend_end, friend_id) != local_ipc_friend_end;
}

bool is_local_ipc_friend(std::uint32_t friend_id)
{
  return auto_ignore_enabled.load(std::memory_order_acquire) && is_known_local_ipc_friend(friend_id);
}

bool is_excess_ipc_bot_on_current_server(int max_bots)
{
  std::lock_guard ipc_lock{ipc_mutex};
  if (max_bots <= 0 || ipc_state == nullptr || !valid_local_peer_id())
  {
    return false;
  }

  try_scoped_lock lock{ipc_state};
  if (!lock.locked() || !valid_local_peer_id())
  {
    return false;
  }

  const auto now = now_seconds();
  const auto& local_peer = ipc_state->peer_data[local_peer_id];
  const auto& local_data = ipc_state->peer_user_data[local_peer_id];
  if (!peer_alive(local_peer, now) ||
      !local_data.connected ||
      !local_data.ingame.good ||
      local_data.ingame.server[0] == '\0')
  {
    return false;
  }

  const auto local_connected_time = local_data.ts_connected != 0 ? local_data.ts_connected : local_peer.starttime;
  int same_server_count = 0;
  int earlier_peer_count = 0;

  for (auto index = 0u; index < max_peers; ++index)
  {
    const auto& peer = ipc_state->peer_data[index];
    const auto& data = ipc_state->peer_user_data[index];
    if (!peer_alive(peer, now) ||
        !data.connected ||
        !data.ingame.good ||
        data.ingame.server[0] == '\0' ||
        std::strncmp(data.ingame.server, local_data.ingame.server, sizeof(data.ingame.server)) != 0)
    {
      continue;
    }

    ++same_server_count;

    const auto connected_time = data.ts_connected != 0 ? data.ts_connected : peer.starttime;
    if (connected_time < local_connected_time ||
        (connected_time == local_connected_time && index < static_cast<unsigned int>(local_peer_id)))
    {
      ++earlier_peer_count;
    }
  }

  return same_server_count > max_bots && earlier_peer_count >= max_bots;
}

}
