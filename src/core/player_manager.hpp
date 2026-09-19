/*
/^-----^\   data: 2026-05-06
V  o o  V  file: src/core/player_manager.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef PLAYER_MANAGER_HPP
#define PLAYER_MANAGER_HPP
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace puphook::core::players
{

enum class player_state
{
  default_state = 0,
  friend_state,
  ignored,
  cheater,
  ipc,
  textmode,
  party,
  identified,
};

using role_id = int;

inline constexpr role_id default_role = 0;
inline constexpr role_id ignored_role = -1;
inline constexpr role_id cheater_role = -2;
inline constexpr role_id friend_role = -3;
inline constexpr role_id party_role = -4;
inline constexpr role_id f2p_role = -5;
inline constexpr role_id ipc_role = -6;
inline constexpr role_id textmode_role = -7;
inline constexpr role_id identified_role = -8;

struct role_definition
{
  role_id id = default_role;
  const char* name = "Default";
  bool label = false;
  bool assignable = true;
  bool runtime = false;
};

struct player_entry
{
  std::uint32_t account_id = 0;
  player_state state = player_state::default_state;
  std::string name{};
  bool runtime = false;
  std::vector<role_id> roles{};
};

void initialize();
void shutdown();
void tick();

bool load();
bool save();
bool set_state(std::uint32_t account_id, player_state state, std::string_view name = {}, bool save_changes = true);
bool clear_state(std::uint32_t account_id, bool save_changes = true);

[[nodiscard]] const std::vector<role_definition>& role_definitions();
[[nodiscard]] const char* role_name(role_id role);
[[nodiscard]] std::optional<role_id> parse_role(std::string_view name);
[[nodiscard]] bool role_is_label(role_id role);
[[nodiscard]] bool has_role(std::uint32_t account_id, role_id role);
[[nodiscard]] bool add_role(std::uint32_t account_id, role_id role, std::string_view name = {}, bool save_changes = true);
[[nodiscard]] bool remove_role(std::uint32_t account_id, role_id role, bool save_changes = true);
[[nodiscard]] bool set_role(std::uint32_t account_id, role_id role, std::string_view name = {}, bool save_changes = true);
[[nodiscard]] std::vector<role_id> roles_for(std::uint32_t account_id);
[[nodiscard]] bool role_matches(std::uint32_t account_id, role_id role);

[[nodiscard]] player_state state_for(std::uint32_t account_id);
[[nodiscard]] bool is_friendly(std::uint32_t account_id);
[[nodiscard]] bool is_party(std::uint32_t account_id);
[[nodiscard]] bool is_ignored(std::uint32_t account_id);
[[nodiscard]] bool is_prioritized(std::uint32_t account_id);
[[nodiscard]] bool is_pup(std::uint32_t account_id);
[[nodiscard]] bool state_is_pup(player_state state);
[[nodiscard]] const char* state_name(player_state state);
[[nodiscard]] std::optional<player_state> parse_state(std::string_view state_name);
[[nodiscard]] std::vector<player_entry> entries(bool include_runtime);
[[nodiscard]] std::uint32_t account_id_for_player_index(int player_index);

}
#endif
