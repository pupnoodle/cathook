/*
/^-----^\   data: 2026-05-06
V  o o  V  file: src/core/player_manager.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "core/player_manager.hpp"
#include "core/identify/identify.hpp"
#include "core/ipc/ipc_client.hpp"
#include "core/logger.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/netvars.hpp"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>

namespace cathook::core::players
{
namespace
{

struct stored_player
{
  player_state state = player_state::default_state;
  std::string name{};
  std::vector<role_id> roles{};
};

std::mutex player_mutex{};
std::unordered_map<std::uint32_t, stored_player> persistent_players{};
std::unordered_map<std::uint32_t, stored_player> runtime_players{};

[[nodiscard]] std::filesystem::path player_list_path()
{
  return config_directory() / "players.cat";
}

[[nodiscard]] std::string trim(std::string value)
{
  const auto is_space = [](const unsigned char character)
  {
    return std::isspace(character) != 0;
  };

  const auto start = std::find_if_not(value.begin(), value.end(), is_space);
  const auto finish = std::find_if_not(value.rbegin(), value.rend(), is_space).base();
  if (start >= finish)
  {
    return {};
  }

  return {start, finish};
}

[[nodiscard]] std::string sanitize_name(std::string_view name)
{
  std::string sanitized{name};
  sanitized.erase(std::remove_if(sanitized.begin(), sanitized.end(), [](const char character)
  {
    return character == '\n' || character == '\r' || character == '\t';
  }), sanitized.end());
  return sanitized;
}

[[nodiscard]] std::uint32_t read_account_id(std::string_view value)
{
  std::uint32_t account_id = 0;
  const auto result = std::from_chars(value.data(), value.data() + value.size(), account_id);
  return result.ec == std::errc{} && result.ptr == value.data() + value.size() ? account_id : 0;
}

[[nodiscard]] bool should_persist(player_state state)
{
  return state != player_state::default_state && state != player_state::ipc && state != player_state::textmode && state != player_state::identified;
}

const std::vector<role_definition> g_role_definitions{
  {default_role, "Default", false, false, false},
  {ignored_role, "Ignored", false, true, false},
  {cheater_role, "Cheater", false, true, false},
  {friend_role, "Friend", true, true, false},
  {party_role, "Party", true, true, false},
  {f2p_role, "F2P", true, true, false},
  {ipc_role, "IPC", true, false, true},
  {textmode_role, "Textmode", true, false, true},
  {identified_role, "CAT", true, true, true},
};

[[nodiscard]] std::optional<role_id> role_for_state(player_state state)
{
  switch (state)
  {
    case player_state::friend_state: return friend_role;
    case player_state::ignored: return ignored_role;
    case player_state::cheater: return cheater_role;
    case player_state::ipc: return ipc_role;
    case player_state::textmode: return textmode_role;
    case player_state::party: return party_role;
    case player_state::identified: return identified_role;
    case player_state::default_state: return std::nullopt;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<player_state> state_for_role(role_id role)
{
  switch (role)
  {
    case friend_role: return player_state::friend_state;
    case ignored_role: return player_state::ignored;
    case cheater_role: return player_state::cheater;
    case ipc_role: return player_state::ipc;
    case textmode_role: return player_state::textmode;
    case party_role: return player_state::party;
    case identified_role: return player_state::identified;
    default: return std::nullopt;
  }
}

[[nodiscard]] player_state state_from_roles(const std::vector<role_id>& roles)
{

  constexpr std::array<role_id, 7> priority{
    ignored_role, cheater_role, party_role, friend_role, identified_role, ipc_role, textmode_role};
  for (const role_id role : priority)
  {
    if (std::ranges::find(roles, role) != roles.end())
    {
      return *state_for_role(role);
    }
  }
  return player_state::default_state;
}

[[nodiscard]] bool contains_role(const std::vector<role_id>& roles, role_id role)
{
  return std::ranges::find(roles, role) != roles.end();
}

void normalize_roles(std::vector<role_id>& roles)
{
  std::ranges::sort(roles);
  roles.erase(std::ranges::unique(roles).begin(), roles.end());
  roles.erase(std::ranges::remove(roles, default_role).begin(), roles.end());
}

[[nodiscard]] std::string serialize_roles(const std::vector<role_id>& roles)
{
  std::string result{};
  for (const role_id role : roles)
  {
    if (!result.empty()) result += ',';
    result += role_name(role);
  }
  return result;
}

[[nodiscard]] std::vector<role_id> parse_roles(std::string_view value)
{
  std::vector<role_id> roles{};
  std::size_t start = 0;
  while (start <= value.size())
  {
    const std::size_t end = value.find_first_of(",|", start);
    const auto token = value.substr(start, end == std::string_view::npos ? value.size() - start : end - start);
    if (const auto parsed = parse_role(token); parsed && *parsed != default_role)
    {
      roles.push_back(*parsed);
    }
    if (end == std::string_view::npos) break;
    start = end + 1;
  }
  normalize_roles(roles);
  return roles;
}

void set_primary_role(stored_player& player)
{
  player.state = state_from_roles(player.roles);
}

}
namespace {

[[nodiscard]] bool state_is_friendly(player_state state)
{
  return state == player_state::friend_state ||
         state == player_state::party ||
         state_is_cat(state);
}

[[nodiscard]] bool state_is_ignored(player_state state)
{
  return state == player_state::ignored;
}

[[nodiscard]] bool state_is_prioritized(player_state state)
{
  return state == player_state::cheater;
}

void set_runtime_state(std::uint32_t account_id, player_state state, std::string_view name)
{
  if (account_id == 0)
  {
    return;
  }

  stored_player& player = runtime_players[account_id];
  player.name = sanitize_name(name);
  player.roles.clear();
  if (const auto role = role_for_state(state))
  {
    player.roles.push_back(*role);
  }
  player.state = state;
}

[[nodiscard]] Entity* get_player_resource_entity()
{
  return player_resource_entity();
}

template <typename value_type>
[[nodiscard]] value_type read_player_resource_value(Entity* player_resource, int array_offset, int player_index)
{
  if (player_resource == nullptr || player_index <= 0)
  {
    return {};
  }

  const auto base = reinterpret_cast<std::uintptr_t>(player_resource);
  const auto entry_offset = static_cast<std::uintptr_t>(array_offset) + (static_cast<std::uintptr_t>(player_index) * sizeof(value_type));
  return *reinterpret_cast<value_type*>(base + entry_offset);
}

}

bool state_is_cat(player_state state)
{
  return state == player_state::ipc ||
         state == player_state::textmode ||
         state == player_state::identified;
}

void initialize()
{
  load();
}

void shutdown()
{
  std::lock_guard lock{player_mutex};
  runtime_players.clear();
  persistent_players.clear();
}

void tick()
{
  if (engine == nullptr ||
      entity_list == nullptr ||
      global_vars == nullptr ||
      !engine->is_connected() ||
      !engine->is_in_game() ||
      engine->is_drawing_loading_image())
  {
    return;
  }

  auto* player_resource = get_player_resource_entity();
  if (player_resource == nullptr)
  {
    return;
  }

  static const int connected_offset = tf2_netvars::find_offset("DT_TFPlayerResource", { "baseclass", "m_bConnected" });
  static const int ping_offset = tf2_netvars::find_offset("DT_TFPlayerResource", { "baseclass", "m_iPing" });

  if (connected_offset <= 0)
  {
    return;
  }

  std::lock_guard lock{player_mutex};
  runtime_players.clear();

  const int max_clients = global_vars->max_clients;
  for (int index = 1; index <= max_clients; ++index)
  {
    const bool is_connected = read_player_resource_value<bool>(player_resource, connected_offset, index);
    if (!is_connected)
    {
      continue;
    }

    player_info info{};
    if (!engine->get_player_info(index, &info) || info.fakeplayer || info.friends_id == 0)
    {
      continue;
    }

    const char* name_ptr = nullptr;
    if (ping_offset > 816)
    {
      name_ptr = reinterpret_cast<const char* const*>(reinterpret_cast<uintptr_t>(player_resource) + ping_offset - 816)[index];
    }
    const std::string_view name = (name_ptr != nullptr && name_ptr[0] != '\0') ? name_ptr : info.name;

    const auto account_id = static_cast<std::uint32_t>(info.friends_id);
    if (cat_ipc::client::is_local_ipc_friend(account_id))
    {
      set_runtime_state(account_id, player_state::ipc, name);
    }
    else if (cathook::core::identify::is_peer(account_id, name))
    {
      set_runtime_state(account_id, player_state::identified, name);
    }
  }
}

bool load()
{
  std::lock_guard lock{player_mutex};
  persistent_players.clear();

  std::ifstream input{player_list_path()};
  if (!input.is_open())
  {
    return false;
  }

  std::string line{};
  while (std::getline(input, line))
  {
    line = trim(std::move(line));
    if (line.empty() || line.starts_with('#'))
    {
      continue;
    }

    const auto first_tab = line.find('\t');
    if (first_tab == std::string::npos)
    {
      continue;
    }

    const auto second_tab = line.find('\t', first_tab + 1);
    const auto account_id = read_account_id(std::string_view{line}.substr(0, first_tab));
    const auto roles_text = second_tab == std::string::npos
      ? std::string_view{line}.substr(first_tab + 1)
      : std::string_view{line}.substr(first_tab + 1, second_tab - first_tab - 1);
    auto roles = parse_roles(roles_text);

    if (account_id == 0 || roles.empty())
    {
      continue;
    }

    const auto name = second_tab == std::string::npos ? std::string{} : sanitize_name(std::string_view{line}.substr(second_tab + 1));
    stored_player player{};
    player.name = name;
    player.roles = std::move(roles);
    set_primary_role(player);
    if (should_persist(player.state) || !player.roles.empty())
    {
      persistent_players[account_id] = std::move(player);
    }
  }

  return true;
}

bool save()
{
  std::lock_guard lock{player_mutex};
  std::error_code error{};
  std::filesystem::create_directories(config_directory(), error);

  std::ofstream output{player_list_path(), std::ios::trunc};
  if (!output.is_open())
  {
    return false;
  }

  std::vector<std::pair<std::uint32_t, stored_player>> ordered_entries{};
  ordered_entries.reserve(persistent_players.size());
  for (const auto& entry : persistent_players)
  {
    if (!entry.second.roles.empty())
    {
      ordered_entries.emplace_back(entry.first, entry.second);
    }
  }

  std::ranges::sort(ordered_entries, [](const auto& left, const auto& right)
  {
    return left.first < right.first;
  });

  output << "# account_id\troles\tname\n";
  for (const auto& [account_id, player] : ordered_entries)
  {
    output << account_id << '\t' << serialize_roles(player.roles) << '\t' << player.name << '\n';
  }

  return output.good();
}

bool set_state(std::uint32_t account_id, player_state state, std::string_view name, bool save_changes)
{
  if (account_id == 0)
  {
    return false;
  }

  if (state == player_state::default_state)
  {
    return clear_state(account_id, save_changes);
  }

  const auto role = role_for_state(state);
  return role && set_role(account_id, *role, name, save_changes);
}

bool clear_state(std::uint32_t account_id, bool save_changes)
{
  if (account_id == 0)
  {
    return false;
  }

  {
    std::lock_guard lock{player_mutex};
    persistent_players.erase(account_id);
  }

  return !save_changes || save();
}

const std::vector<role_definition>& role_definitions()
{
  return g_role_definitions;
}

const char* role_name(role_id role)
{
  const auto found = std::ranges::find(g_role_definitions, role, &role_definition::id);
  return found != g_role_definitions.end() ? found->name : "custom";
}

std::optional<role_id> parse_role(std::string_view value)
{
  std::string normalized{value};
  normalized = trim(std::move(normalized));
  normalized.erase(std::remove_if(normalized.begin(), normalized.end(), [](const unsigned char character)
  {
    return std::isspace(character) != 0 || character == '_' || character == '-';
  }), normalized.end());
  std::ranges::transform(normalized, normalized.begin(), [](const unsigned char character)
  {
    return static_cast<char>(std::tolower(character));
  });

  if (normalized.empty() || normalized == "default") return default_role;
  if (normalized == "ignore" || normalized == "ignored") return ignored_role;
  if (normalized == "cheater" || normalized == "rage") return cheater_role;
  if (normalized == "friend" || normalized == "friends") return friend_role;
  if (normalized == "party") return party_role;
  if (normalized == "f2p" || normalized == "free2play") return f2p_role;
  if (normalized == "ipc") return ipc_role;
  if (normalized == "textmode") return textmode_role;
  if (normalized == "cat" || normalized == "identified" || normalized == "identify") return identified_role;
  return std::nullopt;
}

bool role_is_label(role_id role)
{
  const auto found = std::ranges::find(g_role_definitions, role, &role_definition::id);
  return found != g_role_definitions.end() && found->label;
}

std::vector<role_id> roles_for(std::uint32_t account_id)
{
  if (account_id == 0) return {};

  std::lock_guard lock{player_mutex};
  if (const auto found = persistent_players.find(account_id); found != persistent_players.end())
  {
    return found->second.roles;
  }
  if (const auto found = runtime_players.find(account_id); found != runtime_players.end())
  {
    return found->second.roles;
  }
  return {};
}

bool has_role(std::uint32_t account_id, role_id role)
{
  if (account_id == 0 || role == default_role) return false;
  if (contains_role(roles_for(account_id), role)) return true;

  return role == friend_role && steam_friends != nullptr &&
         steam_friends->is_friend(static_cast<int>(account_id));
}

bool role_matches(std::uint32_t account_id, role_id role)
{
  return has_role(account_id, role);
}

bool add_role(std::uint32_t account_id, role_id role, std::string_view name, bool save_changes)
{
  if (account_id == 0 || role == default_role || !std::ranges::any_of(g_role_definitions, [role](const auto& definition)
      { return definition.id == role && definition.assignable; }))
  {
    return false;
  }

  bool changed = false;
  {
    std::lock_guard lock{player_mutex};
    stored_player& player = persistent_players[account_id];
    if (!name.empty()) player.name = sanitize_name(name);
    if (!contains_role(player.roles, role))
    {
      player.roles.push_back(role);
      normalize_roles(player.roles);
      changed = true;
    }
    set_primary_role(player);
  }
  return (!save_changes || save()) && changed;
}

bool remove_role(std::uint32_t account_id, role_id role, bool save_changes)
{
  if (account_id == 0 || role == default_role) return false;

  bool changed = false;
  {
    std::lock_guard lock{player_mutex};
    const auto found = persistent_players.find(account_id);
    if (found == persistent_players.end()) return false;
    auto& roles = found->second.roles;
    const auto old_size = roles.size();
    roles.erase(std::remove(roles.begin(), roles.end(), role), roles.end());
    changed = roles.size() != old_size;
    set_primary_role(found->second);
    if (roles.empty()) persistent_players.erase(found);
  }
  return (!save_changes || save()) && changed;
}

bool set_role(std::uint32_t account_id, role_id role, std::string_view name, bool save_changes)
{
  if (account_id == 0 || role == default_role || !std::ranges::any_of(g_role_definitions, [role](const auto& definition)
      { return definition.id == role && definition.assignable; })) return false;
  bool changed = false;
  {
    std::lock_guard lock{player_mutex};
    stored_player& player = persistent_players[account_id];
    if (!name.empty()) player.name = sanitize_name(name);
    std::vector<role_id> next_roles{};
    for (const role_id existing_role : player.roles)
    {
      if (role_is_label(existing_role)) next_roles.push_back(existing_role);
    }
    next_roles.push_back(role);
    normalize_roles(next_roles);
    changed = player.roles != next_roles;
    player.roles = std::move(next_roles);
    set_primary_role(player);
  }
  return (!save_changes || save()) && changed;
}

player_state state_for(std::uint32_t account_id)
{
  if (account_id == 0)
  {
    return player_state::default_state;
  }

  std::lock_guard lock{player_mutex};
  if (const auto found = persistent_players.find(account_id); found != persistent_players.end())
  {
    return found->second.state;
  }

  if (const auto found = runtime_players.find(account_id); found != runtime_players.end())
  {
    return found->second.state;
  }

  return player_state::default_state;
}

bool is_friendly(std::uint32_t account_id)
{
  return has_role(account_id, friend_role) || has_role(account_id, party_role) || has_role(account_id, identified_role);
}

bool is_party(std::uint32_t account_id)
{
  return has_role(account_id, party_role);
}

bool is_ignored(std::uint32_t account_id)
{
  return has_role(account_id, ignored_role);
}

bool is_prioritized(std::uint32_t account_id)
{
  return has_role(account_id, cheater_role);
}

bool is_cat(std::uint32_t account_id)
{
  return has_role(account_id, ipc_role) || has_role(account_id, textmode_role) || has_role(account_id, identified_role);
}

const char* state_name(player_state state)
{
  switch (state)
  {
    case player_state::friend_state:
      return "friend";
    case player_state::ignored:
      return "ignored";
    case player_state::cheater:
      return "cheater";
    case player_state::ipc:
      return "ipc";
    case player_state::textmode:
      return "textmode";
    case player_state::party:
      return "party";
    case player_state::identified:
      return "identified";
    case player_state::default_state:
    default:
      return "default";
  }
}

std::optional<player_state> parse_state(std::string_view state_name)
{
  std::string normalized{state_name};
  normalized.erase(std::remove_if(normalized.begin(), normalized.end(), [](const unsigned char character)
  {
    return std::isspace(character) != 0 || character == '_' || character == '-';
  }), normalized.end());
  std::ranges::transform(normalized, normalized.begin(), [](const unsigned char character)
  {
    return static_cast<char>(std::tolower(character));
  });

  if (normalized == "default")
  {
    return player_state::default_state;
  }
  if (normalized == "friend")
  {
    return player_state::friend_state;
  }
  if (normalized == "ignore" || normalized == "ignored")
  {
    return player_state::ignored;
  }
  if (normalized == "cheater" || normalized == "rage")
  {
    return player_state::cheater;
  }
  if (normalized == "ipc")
  {
    return player_state::ipc;
  }
  if (normalized == "textmode")
  {
    return player_state::textmode;
  }
  if (normalized == "party")
  {
    return player_state::party;
  }
  if (normalized == "identified" || normalized == "identify")
  {
    return player_state::identified;
  }

  return std::nullopt;
}

std::vector<player_entry> entries(bool include_runtime)
{
  std::lock_guard lock{player_mutex};
  std::vector<player_entry> result{};
  result.reserve(persistent_players.size() + (include_runtime ? runtime_players.size() : 0));

  for (const auto& [account_id, player] : persistent_players)
  {
    result.emplace_back(account_id, player.state, player.name, false, player.roles);
  }

  if (include_runtime)
  {
    for (const auto& [account_id, player] : runtime_players)
    {
      if (persistent_players.contains(account_id))
      {
        continue;
      }

      result.emplace_back(account_id, player.state, player.name, true, player.roles);
    }
  }

  std::ranges::sort(result, [](const auto& left, const auto& right)
  {
    return left.account_id < right.account_id;
  });
  return result;
}

std::uint32_t account_id_for_player_index(int player_index)
{
  if (engine == nullptr || player_index <= 0)
  {
    return 0;
  }

  player_info info{};
  if (!engine->get_player_info(player_index, &info) || info.fakeplayer)
  {
    return 0;
  }

  return static_cast<std::uint32_t>(info.friends_id);
}

}
