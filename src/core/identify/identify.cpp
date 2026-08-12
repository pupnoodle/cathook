#include "core/identify/identify.hpp"
#include "core/identify/identify_client.hpp"
#include "core/commands.hpp"
#include "core/player_manager.hpp"
#include "core/print.hpp"
#include "external/MD5/MD5.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/net_channel.hpp"
#include "games/tf2/sdk/netvars.hpp"
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cathook::core::identify
{
namespace
{

constexpr const char* server_host      = "identify.cathook.org";
constexpr int         server_port      = 3000;
constexpr int         betrayal_limit   = 2;
constexpr int         tick_interval_ms = 1000;

std::unique_ptr<identify_client> client = nullptr;

std::mutex                      peer_mutex{};
std::unordered_set<std::string> peer_hashes{};

std::mutex                             betrayals_mutex{};
std::unordered_map<std::uint32_t, int> betrayals{};

std::mutex                                       chat_mutex{};
std::vector<std::pair<std::string, std::string>> chat_queue{};

std::string current_server_id()
{
  if (engine == nullptr || !engine->is_in_game())
  {
    return {};
  }
  if (client_state == nullptr || client_state->m_NetChannel == nullptr)
  {
    return {};
  }
  const char* addr = client_state->m_NetChannel->get_address();
  if (addr == nullptr || *addr == '\0')
  {
    return {};
  }
  return std::string{addr};
}

std::string local_player_hash()
{
  if (engine == nullptr)
  {
    return {};
  }
  int local_idx = engine->get_localplayer_index();
  if (local_idx <= 0)
  {
    return {};
  }
  player_info info{};
  if (!engine->get_player_info(local_idx, &info) || info.friends_id == 0)
  {
    return {};
  }
  return player_hash(static_cast<std::uint32_t>(info.friends_id), info.name);
}

void on_peers(const std::vector<std::string>& hashes)
{
  std::lock_guard lock{peer_mutex};
  peer_hashes.clear();
  for (const std::string& h : hashes)
  {
    peer_hashes.insert(h);
  }
}

void on_chat(const std::string& from, const std::string& msg)
{
  std::lock_guard lock{chat_mutex};
  chat_queue.emplace_back(from, msg);
}

void drain_chat()
{
  std::vector<std::pair<std::string, std::string>> drained;
  {
    std::lock_guard lock{chat_mutex};
    drained.swap(chat_queue);
  }
  for (const auto& [from, msg] : drained)
  {
    print("[identify %.6s] %s\n", from.c_str(), msg.c_str());
  }
}

void command_identify_send_callback(const cathook::core::command_args& args)
{
  if (client == nullptr)
  {
    return;
  }
  if (args.argc() < 2)
  {
    print("usage: cat_identify_send <message>\n");
    return;
  }
  std::string msg;
  for (int i = 1; i < args.argc(); ++i)
  {
    if (i > 1)
    {
      msg += ' ';
    }
    msg += args.argv(i);
  }
  client->send_chat(msg);
}

void command_clear_betrayals_callback(const cathook::core::command_args&)
{
  clear_betrayals();
  print("[identify] betrayals cleared\n");
}

std::string compute_signature(std::string_view server_id, std::string_view player_hash)
{
  if (server_id.empty() || player_hash.empty())
  {
    return {};
  }
#ifndef IDENTIFY_SECRET
#define IDENTIFY_SECRET "catokxd13941"
#endif
  std::string blob = std::string{server_id} + std::string{player_hash} + IDENTIFY_SECRET;
  MD5Value_t result{};
  MD5_ProcessSingleBuffer(blob.data(), static_cast<int>(blob.size()), result);

  std::string hex_str;
  hex_str.reserve(32);
  constexpr char hex_chars[] = "0123456789abcdef";
  for (const unsigned char byte : result.bits)
  {
    hex_str.push_back(hex_chars[(byte >> 4) & 0x0f]);
    hex_str.push_back(hex_chars[byte & 0x0f]);
  }
  return hex_str;
}

}

std::string player_hash(std::uint32_t friends_id, std::string_view name)
{
  if (friends_id == 0 || name.empty())
  {
    return {};
  }

  std::string blob = std::to_string(friends_id) + std::string{name};
  MD5Value_t result{};
  MD5_ProcessSingleBuffer(blob.data(), static_cast<int>(blob.size()), result);

  std::string hex_str;
  hex_str.reserve(32);
  constexpr char hex_chars[] = "0123456789abcdef";
  for (const unsigned char byte : result.bits)
  {
    hex_str.push_back(hex_chars[(byte >> 4) & 0x0f]);
    hex_str.push_back(hex_chars[byte & 0x0f]);
  }
  return hex_str;
}

bool is_peer_hash(std::string_view hash)
{
  if (hash.empty())
  {
    return false;
  }
  std::lock_guard lock{peer_mutex};
  return peer_hashes.contains(std::string{hash});
}

bool is_betrayed(std::uint32_t account_id)
{
  if (account_id == 0)
  {
    return false;
  }
  std::lock_guard lock{betrayals_mutex};
  const auto found = betrayals.find(account_id);
  return found != betrayals.end() && found->second >= betrayal_limit;
}

void start()
{
  {
    std::lock_guard lock{betrayals_mutex};
    betrayals.clear();
  }

  client = std::make_unique<identify_client>();
  client->set_peers_handler(on_peers);
  client->set_chat_handler(on_chat);
  client->connect(server_host, server_port);

  cathook::core::add_command(
      "cat_identify_send",
      command_identify_send_callback,
      "Send a chat message to others on the same server");
  cathook::core::add_command(
      "cat_clear_betrayals",
      command_clear_betrayals_callback,
      "Clear the identify betrayal log");

  print("[identify] init done, connecting to %s:%d\n", server_host, server_port);
}

void stop()
{
  if (client != nullptr)
  {
    client->stop();
    client.reset();
  }
  {
    std::lock_guard lock{peer_mutex};
    peer_hashes.clear();
  }
}

void tick()
{
  if (client == nullptr)
  {
    return;
  }

  using clock = std::chrono::steady_clock;
  static clock::time_point last_run{};
  clock::time_point now = clock::now();
  long dt = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_run).count();
  if (dt < tick_interval_ms)
  {
    return;
  }
  last_run = now;

  const std::string server_id = current_server_id();
  const std::string player_hash_val = local_player_hash();

  int head_scale = 12;
  if (!server_id.empty() && !player_hash_val.empty())
  {
    double temp = 0.0;
    for (char c : player_hash_val)
    {
      temp += static_cast<double>(c);
    }
    head_scale = static_cast<int>(temp) % 10 + 12 - (static_cast<int>(temp) % 10);
  }

  client->update_identity(server_id, player_hash_val, compute_signature(server_id, player_hash_val), head_scale);
  drain_chat();
}

bool is_peer(std::uint32_t account_id, std::string_view name)
{
  if (account_id == 0)
  {
    return false;
  }

  if (is_betrayed(account_id))
  {
    return false;
  }

  const std::string hash = player_hash(account_id, name);
  return is_peer_hash(hash);
}

void on_player_death(int attacker_user_id)
{
  if (engine == nullptr || entity_list == nullptr)
  {
    return;
  }
  int attacker_index = engine->get_player_index_from_id(attacker_user_id);
  if (attacker_index <= 0)
  {
    return;
  }

  player_info info{};
  if (!engine->get_player_info(attacker_index, &info) || info.friends_id == 0)
  {
    return;
  }
  std::uint32_t account_id = static_cast<std::uint32_t>(info.friends_id);

  Player* p = entity_list->player_from_index(static_cast<unsigned int>(attacker_index));
  if (p == nullptr || p->get_class_id() != class_id::PLAYER)
  {
    return;
  }

  Entity* player_resource = player_resource_entity();

  static const int ping_offset = tf2_netvars::find_offset("DT_TFPlayerResource", { "baseclass", "m_iPing" });
  const char* name_ptr = nullptr;
  if (ping_offset > 816 && player_resource != nullptr)
  {
    name_ptr = reinterpret_cast<const char* const*>(reinterpret_cast<uintptr_t>(player_resource) + ping_offset - 816)[attacker_index];
  }
  const std::string_view name = (name_ptr != nullptr && name_ptr[0] != '\0') ? name_ptr : info.name;

  if (!is_peer(account_id, name))
  {
    return;
  }

  int count = 0;
  {
    std::lock_guard lock{betrayals_mutex};
    count = ++betrayals[account_id];
  }
  if (count >= betrayal_limit)
  {
    print("[identify] %u betrayed %d times; no longer identifying\n", account_id, count);
  }
}

void clear_betrayals()
{
  std::lock_guard lock{betrayals_mutex};
  betrayals.clear();
}

}
