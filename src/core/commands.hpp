/*
/^-----^\   data: 2026-04-06
V  o o  V  file: src/core/commands.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include <cctype>
#include <charconv>
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/config/config_store.hpp"
#include "core/detach.hpp"
#include "core/player_manager.hpp"
#include "core/print.hpp"
#include "features/automation/autoitem/autoitem.hpp"
#include "features/automation/misc/misc.hpp"
#include "features/menu/binds.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/steam_runtime.hpp"

void* get_interface(const char* lib_path, const char* version);

namespace cathook::core
{

class command_args
{
public:
  [[nodiscard]] int argc() const { return argc_; }

  [[nodiscard]] const char* argv(const int index) const
  {
    if (index < 0 || index >= argc_ || index >= max_args || argv_[index] == nullptr)
    {
      return "";
    }

    return argv_[index];
  }

private:
  static constexpr int max_args = 64;
  static constexpr int max_length = 512;

  int argc_ = 0;
  int argv0_size_ = 0;
  char args_buffer_[max_length]{};
  char argv_buffer_[max_length]{};
  const char* argv_[max_args]{};
};

using command_callback_t = void (*)(const command_args&);

inline const char* command_invocation_name(const command_args& args, const char* fallback);

class command_base
{
public:
  virtual ~command_base() = default;
  virtual bool is_command() const { return false; }
  virtual bool is_flag_set(int flag) const { return (flags_ & flag) != 0; }
  virtual void add_flags(int flag) { flags_ |= flag; }
  virtual const char* get_name() const { return name_; }
  virtual const char* get_help_text() const { return help_; }
  virtual bool is_registered() const { return registered_; }
  virtual int get_dll_identifier() const { return 0; }
  virtual void create_base(const char* name, const char* help = nullptr, int flags = 0) {
    name_ = name;
    help_ = help != nullptr ? help : "";
    flags_ = flags;
  }
  virtual void init() {}
  int get_flags() const { return flags_; }
  void remove_flags(int flag) { flags_ &= ~flag; }

  command_base* next_ = nullptr;
  bool registered_ = false;
  const char* name_ = "";
  const char* help_ = "";
  int flags_ = 0;
};

class command final : public command_base
{
public:
  command(const char* name, command_callback_t callback, const char* help) : callback_(callback) {
    create_base(name, help, 0);
  }

  ~command() override = default;

  bool is_command() const override { return true; }
  virtual int auto_complete_suggest(const char*, void*) { return 0; }
  virtual bool can_auto_complete() { return false; }
  virtual void dispatch(const command_args& args) {
    if (callback_ != nullptr) {
      callback_(args);
    }
  }

private:
  command_callback_t callback_ = nullptr;
};

inline std::vector<std::unique_ptr<command>>& registered_commands() {
  static std::vector<std::unique_ptr<command>> value{};
  return value;
}

inline bool& commands_registered() {
  static bool value = false;
  return value;
}

inline void command_detach_callback(const command_args&) {
  request_detach();
}

inline std::filesystem::path game_cfg_directory()
{
  return std::filesystem::path{ "tf" } / "cfg";
}

inline bool write_cfg_file_if_missing(const char* file_name, const char* default_text)
{
  const auto cfg_path = game_cfg_directory() / file_name;
  std::error_code error{};

  if (std::filesystem::exists(cfg_path, error))
  {
    return true;
  }

  std::filesystem::create_directories(cfg_path.parent_path(), error);
  if (error)
  {
    print("[cat_exec] failed to create cfg directory '%s': %s\n",
      cfg_path.parent_path().string().c_str(),
      error.message().c_str());
    return false;
  }

  std::ofstream output{ cfg_path, std::ios::out | std::ios::trunc };
  if (!output.is_open())
  {
    print("[cat_exec] failed to create cfg '%s'\n", cfg_path.string().c_str());
    return false;
  }

  output << default_text;
  return output.good();
}

inline void ensure_cathook_cfg_files()
{
  constexpr const char* cat_autoexec =
    "// Put your custom cathook settings in this file\n"
    "// This script will be executed each time you inject cathook\n";

  constexpr const char* cat_autoexec_textmode =
    "// Put your custom cathook settings in this file\n"
    "// This script will be executed each time you inject textmode cathook\n"
    "sv_cheats 1\n"
    "engine_no_focus_sleep 0\n"
    "mat_queue_mode 0\n"
    "hud_fastswitch 1\n"
    "tf_medigun_autoheal 1\n"
    "fps_max 30\n";

  constexpr const char* cat_matchexec =
    "// Put your custom cathook settings in this file\n"
    "// This script will be executed each time you join a match\n";

  write_cfg_file_if_missing("cat_autoexec.cfg", cat_autoexec);
  write_cfg_file_if_missing("cat_autoexec_textmode.cfg", cat_autoexec_textmode);
  write_cfg_file_if_missing("cat_matchexec.cfg", cat_matchexec);
}

inline void execute_client_command(const char* command_name, const char* command_text)
{
  if (engine == nullptr)
  {
    print("[%s] engine unavailable\n", command_name);
    return;
  }

  if (command_text == nullptr || command_text[0] == '\0')
  {
    print("[%s] empty command\n", command_name);
    return;
  }

  engine->client_cmd_unrestricted(command_text);
}

inline void execute_cfg_file(const char* command_name, const char* cfg_name)
{
  ensure_cathook_cfg_files();

  std::string command_text{ "exec " };
  command_text += cfg_name;
  execute_client_command(command_name, command_text.c_str());
}

inline void command_cat_exec_callback(const command_args& args)
{
  execute_cfg_file(command_invocation_name(args, "cat_exec"), "cat_autoexec");
}

inline void command_cat_exec_textmode_callback(const command_args& args)
{
  execute_cfg_file(command_invocation_name(args, "cat_exec_textmode"), "cat_autoexec_textmode");
}

inline void execute_startup_autoexec()
{
#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE
  execute_cfg_file("cat_exec_textmode", "cat_autoexec_textmode");
#else
  execute_cfg_file("cat_exec", "cat_autoexec");
#endif

  if (const char* extra_exec = std::getenv("CH_EXEC"); extra_exec != nullptr && extra_exec[0] != '\0')
  {
    execute_client_command("CH_EXEC", extra_exec);
  }
}

inline bool is_valid_config_name(const std::string_view name)
{
  if (name.empty() || name.size() > 64)
  {
    return false;
  }

  for (const char character : name)
  {
    const auto byte = static_cast<unsigned char>(character);
    if (std::isalnum(byte) == 0 && character != '_' && character != '-')
    {
      return false;
    }
  }

  return true;
}

inline std::string_view read_config_name_arg(const command_args& args, const char* command_name)
{
  if (args.argc() < 2)
  {
    print("[%s] usage: %s <name>\n", command_name, command_name);
    return {};
  }

  const std::string_view name{args.argv(1)};
  if (!is_valid_config_name(name))
  {
    print("[%s] invalid config name '%s' (use letters, numbers, '_' or '-')\n", command_name, args.argv(1));
    return {};
  }

  return name;
}

inline void command_load_callback(const command_args& args)
{
  const auto name = read_config_name_arg(args, "cat_load");
  if (name.empty())
  {
    return;
  }

  auto* store = get_config_store();
  if (store == nullptr)
  {
    print("[cat_load] config store unavailable\n");
    return;
  }

  if (!store->load_file(name))
  {
    print("[cat_load] failed to load config '%s'\n", args.argv(1));
    return;
  }

  store->export_config(::config);
  cat_bind::load(store);
  print("[cat_load] loaded config '%s'\n", args.argv(1));
}

inline void command_save_callback(const command_args& args)
{
  const auto name = read_config_name_arg(args, "cat_save");
  if (name.empty())
  {
    return;
  }

  auto* store = get_config_store();
  if (store == nullptr)
  {
    print("[cat_save] config store unavailable\n");
    return;
  }

  store->import_config(::config);
  if (!store->save_file(name))
  {
    print("[cat_save] failed to save config '%s'\n", args.argv(1));
    return;
  }

  if (!cat_bind::save(store, name))
  {
    print("[cat_save] saved config '%s' but failed to save binds\n", args.argv(1));
    return;
  }

  print("[cat_save] saved config '%s'\n", args.argv(1));
}

enum class achievement_change
{
  unlock,
  lock,
};

inline SteamClient* resolve_steam_client()
{
  return steam_runtime::resolve_steam_client();
}

inline steam_user_stats* resolve_steam_user_stats()
{
  return steam_runtime::resolve_steam_user_stats();
}

inline achievement_manager* resolve_achievement_manager()
{
  if (engine == nullptr)
  {
    return nullptr;
  }

  return engine->get_achievement_manager();
}

inline void change_all_achievements(const achievement_change change, const char* command_name)
{
  auto* manager = resolve_achievement_manager();
  if (manager == nullptr)
  {
    print("[%s] achievement manager unavailable\n", command_name);
    return;
  }

  auto* stats = resolve_steam_user_stats();
  if (stats == nullptr)
  {
    print("[%s] Steam user stats unavailable\n", command_name);
    return;
  }

  if (!stats->request_current_stats())
  {
    print("[%s] RequestCurrentStats failed; trying achievement write anyway\n", command_name);
  }

  const auto achievement_count = manager->get_achievement_count();
  if (achievement_count <= 0)
  {
    print("[%s] no achievements found\n", command_name);
    return;
  }

  auto processed_count = 0;
  auto failed_count = 0;

  for (auto index = 0; index < achievement_count; ++index)
  {
    auto* entry = manager->get_achievement_by_index(index);
    if (entry == nullptr)
    {
      ++failed_count;
      continue;
    }

    if (change == achievement_change::unlock)
    {
      manager->award_achievement(entry->get_achievement_id());
      ++processed_count;
      continue;
    }

    const auto* achievement_name = entry->get_name();
    if (achievement_name == nullptr || achievement_name[0] == '\0')
    {
      ++failed_count;
      continue;
    }

    if (stats->clear_achievement(achievement_name))
    {
      ++processed_count;
    }
    else
    {
      ++failed_count;
    }
  }

  const auto stored = stats->store_stats();
  stats->request_current_stats();

  const auto* action_name = change == achievement_change::unlock ? "unlocked" : "locked";
  print("[%s] %s %d/%d achievements", command_name, action_name, processed_count, achievement_count);
  if (failed_count > 0)
  {
    print(" (%d failed)", failed_count);
  }
  if (!stored)
  {
    print(" (StoreStats failed)");
  }
  print("\n");
}

inline void command_unlock_achievements_callback(const command_args&)
{
  change_all_achievements(achievement_change::unlock, "cat_unlock_achievements");
}

inline void command_lock_achievements_callback(const command_args&)
{
  change_all_achievements(achievement_change::lock, "cat_lock_achievements");
}

inline std::optional<int> read_int_arg(const command_args& args, const char* command_name, const int index, const char* label)
{
  if (args.argc() <= index)
  {
    print("[%s] usage: %s <%s>\n", command_name, command_name, label);
    return std::nullopt;
  }

  const std::string_view value{ args.argv(index) };
  int parsed = 0;
  const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
  {
    print("[%s] invalid %s '%s'\n", command_name, label, args.argv(index));
    return std::nullopt;
  }

  return parsed;
}

inline const char* command_invocation_name(const command_args& args, const char* fallback)
{
  const char* name = args.argv(0);
  return name != nullptr && name[0] != '\0' ? name : fallback;
}

inline void command_unlock_achievement_callback(const command_args& args)
{
  const char* command_name = command_invocation_name(args, "cat_unlock_achievement");
  const auto achievement_id = read_int_arg(args, command_name, 1, "achievement_id");
  if (!achievement_id)
  {
    return;
  }

  if (!autoitem::unlock_achievement_by_id(*achievement_id))
  {
    print("[%s] failed to unlock achievement %d\n", command_name, *achievement_id);
    return;
  }

  print("[%s] unlocked achievement %d\n", command_name, *achievement_id);
}

inline void command_lock_achievement_callback(const command_args& args)
{
  const char* command_name = command_invocation_name(args, "cat_lock_achievement");
  const auto achievement_id = read_int_arg(args, command_name, 1, "achievement_id");
  if (!achievement_id)
  {
    return;
  }

  if (!autoitem::lock_achievement_by_id(*achievement_id))
  {
    print("[%s] failed to lock achievement %d\n", command_name, *achievement_id);
    return;
  }

  print("[%s] locked achievement %d\n", command_name, *achievement_id);
}

inline void command_autoitem_rent_callback(const command_args& args)
{
  const char* command_name = command_invocation_name(args, "cat_autoitem_rent");
  const auto item_def_id = read_int_arg(args, command_name, 1, "item_def_id");
  if (!item_def_id)
  {
    return;
  }

  if (!autoitem::rent_item(*item_def_id))
  {
    print("[%s] failed to request item preview for item def %d\n", command_name, *item_def_id);
    return;
  }

  print("[%s] requested item preview for item def %d\n", command_name, *item_def_id);
}

inline void command_autoitem_craft_callback(const command_args& args)
{
  const char* command_name = command_invocation_name(args, "cat_autoitem_craft");
  if (args.argc() < 2)
  {
    print("[%s] usage: %s <item_def_id> [item_def_id...]\n", command_name, command_name);
    return;
  }

  std::vector<int> item_def_ids{};
  item_def_ids.reserve(static_cast<std::size_t>(args.argc() - 1));
  for (int index = 1; index < args.argc(); ++index)
  {
    const auto item_def_id = read_int_arg(args, command_name, index, "item_def_id");
    if (!item_def_id)
    {
      return;
    }
    item_def_ids.emplace_back(*item_def_id);
  }

  if (!autoitem::craft_items(item_def_ids))
  {
    print("[%s] failed to craft with %d item defs\n", command_name, static_cast<int>(item_def_ids.size()));
    return;
  }

  print("[%s] sent craft request with %d item defs\n", command_name, static_cast<int>(item_def_ids.size()));
}

inline void command_dump_achievements_callback(const command_args& args)
{
  const char* command_name = command_invocation_name(args, "cat_dump_achievements");
  const char* path = args.argc() >= 2 ? args.argv(1) : "/tmp/cathook_achievements.txt";
  if (!autoitem::dump_achievements(path))
  {
    print("[%s] failed to dump achievements to %s\n", command_name, path);
    return;
  }

  print("[%s] dumped achievements to %s\n", command_name, path);
}

inline void command_queue_callback(const command_args&)
{
  if (!automation::request_casual_queue())
  {
    print("[cat_queue] matchmaking API unavailable\n");
    return;
  }

  print("[cat_queue] requested casual matchmaking queue\n");
}

inline void command_cancel_queue_callback(const command_args&)
{
  if (!automation::cancel_casual_queue())
  {
    print("[cat_cancelqueue] matchmaking API unavailable\n");
    return;
  }

  print("[cat_cancelqueue] requested casual matchmaking queue cancel\n");
}

inline void command_criteria_callback(const command_args&)
{
  if (!automation::reload_casual_criteria())
  {
    print("[cat_criteria] casual criteria API unavailable\n");
    return;
  }

  print("[cat_criteria] reloaded casual criteria\n");
}

inline void command_commands_callback(const command_args&)
{
  print("[cat_commands] %d registered commands\n", static_cast<int>(registered_commands().size()));
  for (const auto& command_entry : registered_commands())
  {
    if (command_entry != nullptr)
    {
      print("  %s%s\n", command_entry->get_name(), command_entry->is_registered() ? "" : " (not registered)");
    }
  }
}

struct resolved_player_arg
{
  std::uint32_t account_id = 0;
  std::string name{};
};

inline std::string lowercase_copy(std::string_view value)
{
  std::string result{value};
  std::ranges::transform(result, result.begin(), [](const unsigned char character)
  {
    return static_cast<char>(std::tolower(character));
  });
  return result;
}

inline std::optional<std::uint32_t> parse_account_id_arg(std::string_view value)
{
  if (value.empty())
  {
    return std::nullopt;
  }

  std::uint64_t parsed = 0;
  const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed == 0)
  {
    return std::nullopt;
  }

  return static_cast<std::uint32_t>(parsed & 0xffffffffu);
}

inline std::optional<resolved_player_arg> resolve_player_arg(const command_args& args, const char* command_name, const int index)
{
  if (args.argc() <= index)
  {
    print("[%s] usage: %s <steamid32|steamid64|#userid|name>\n", command_name, command_name);
    return std::nullopt;
  }

  const std::string_view value{args.argv(index)};
  if (const auto account_id = parse_account_id_arg(value))
  {
    return resolved_player_arg{*account_id, {}};
  }

  if (value.size() > 1 && value.front() == '#' && engine != nullptr)
  {
    const auto user_id = parse_account_id_arg(value.substr(1));
    if (user_id)
    {
      const int player_index = engine->get_player_index_from_id(static_cast<int>(*user_id));
      const auto account_id = players::account_id_for_player_index(player_index);
      if (account_id != 0)
      {
        player_info info{};
        std::string name{};
        if (engine->get_player_info(player_index, &info))
        {
          name = info.name;
        }
        return resolved_player_arg{account_id, std::move(name)};
      }
    }
  }

  if (engine == nullptr)
  {
    print("[%s] engine unavailable; use a steamid32/steamid64\n", command_name);
    return std::nullopt;
  }

  const auto wanted_name = lowercase_copy(value);
  std::optional<resolved_player_arg> match{};
  for (int player_index = 1; player_index <= 64; ++player_index)
  {
    player_info info{};
    if (!engine->get_player_info(player_index, &info) || info.fakeplayer || info.friends_id == 0)
    {
      continue;
    }

    const auto current_name = lowercase_copy(info.name);
    if (current_name.find(wanted_name) == std::string::npos)
    {
      continue;
    }

    if (match)
    {
      print("[%s] player name '%s' is ambiguous; use steamid32 or #userid\n", command_name, args.argv(index));
      return std::nullopt;
    }

    match = resolved_player_arg{static_cast<std::uint32_t>(info.friends_id), info.name};
  }

  if (!match)
  {
    print("[%s] player '%s' not found; use steamid32/steamid64 if they are not connected\n", command_name, args.argv(index));
    return std::nullopt;
  }

  return match;
}

inline void command_playerlist_load_callback(const command_args& args)
{
  const char* command_name = command_invocation_name(args, "cat_playerlist_load");
  if (!players::load())
  {
    print("[%s] no saved player list found\n", command_name);
    return;
  }

  print("[%s] loaded player list\n", command_name);
}

inline void command_playerlist_save_callback(const command_args& args)
{
  const char* command_name = command_invocation_name(args, "cat_playerlist_save");
  if (!players::save())
  {
    print("[%s] failed to save player list\n", command_name);
    return;
  }

  print("[%s] saved player list\n", command_name);
}

inline void command_playerlist_print_callback(const command_args& args)
{
  const char* command_name = command_invocation_name(args, "cat_playerlist_print");
  const bool include_runtime = args.argc() >= 2 && (std::string_view{args.argv(1)} == "all" || std::string_view{args.argv(1)} == "1");
  const auto entries = players::entries(include_runtime);
  print("[%s] %d player entries%s\n", command_name, static_cast<int>(entries.size()), include_runtime ? " including runtime" : "");
  for (const auto& entry : entries)
  {
    print("  %u -> %s%s%s%s\n",
      entry.account_id,
      players::state_name(entry.state),
      entry.runtime ? " runtime" : "",
      entry.name.empty() ? "" : " ",
      entry.name.c_str());
  }
}

inline void command_playerlist_set_callback(const command_args& args)
{
  const char* command_name = command_invocation_name(args, "cat_playerlist_set");
  const auto player = resolve_player_arg(args, command_name, 1);
  if (!player)
  {
    return;
  }

  if (args.argc() < 3)
  {
    print("[%s] usage: %s <player> <default|friend|ignored|cheater|party>\n", command_name, command_name);
    return;
  }

  const auto state = players::parse_state(args.argv(2));
  if (!state || *state == players::player_state::ipc || *state == players::player_state::textmode)
  {
    print("[%s] unknown persistent state '%s'\n", command_name, args.argv(2));
    return;
  }

  if (*state == players::player_state::default_state)
  {
    if (!players::clear_state(player->account_id))
    {
      print("[%s] failed to clear %u\n", command_name, player->account_id);
      return;
    }
    print("[%s] cleared %u\n", command_name, player->account_id);
    return;
  }

  if (!players::set_state(player->account_id, *state, player->name))
  {
    print("[%s] failed to set %u\n", command_name, player->account_id);
    return;
  }

  print("[%s] set %u to %s\n", command_name, player->account_id, players::state_name(*state));
}

inline void command_playerlist_clear_callback(const command_args& args)
{
  const char* command_name = command_invocation_name(args, "cat_playerlist_clear");
  const auto player = resolve_player_arg(args, command_name, 1);
  if (!player)
  {
    return;
  }

  if (!players::clear_state(player->account_id))
  {
    print("[%s] failed to clear %u\n", command_name, player->account_id);
    return;
  }

  print("[%s] cleared %u\n", command_name, player->account_id);
}

inline void command_playerlist_info_callback(const command_args& args)
{
  const char* command_name = command_invocation_name(args, "cat_playerlist_info");
  const auto player = resolve_player_arg(args, command_name, 1);
  if (!player)
  {
    return;
  }

  const auto state = players::state_for(player->account_id);
  print("[%s] %u state=%s friendly=%d ignored=%d prioritized=%d\n",
    command_name,
    player->account_id,
    players::state_name(state),
    players::is_friendly(player->account_id) ? 1 : 0,
    players::is_ignored(player->account_id) ? 1 : 0,
    players::is_prioritized(player->account_id) ? 1 : 0);
}

inline void command_ignore_callback(const command_args& args)
{
  const char* command_name = command_invocation_name(args, "cat_ignore");
  const auto player = resolve_player_arg(args, command_name, 1);
  if (!player)
  {
    return;
  }

  if (players::is_ignored(player->account_id))
  {
    if (!players::clear_state(player->account_id))
    {
      print("[%s] failed to unignore %u\n", command_name, player->account_id);
      return;
    }

    print("[%s] unignored %u\n", command_name, player->account_id);
    return;
  }

  if (!players::set_state(player->account_id, players::player_state::ignored, player->name))
  {
    print("[%s] failed to ignore %u\n", command_name, player->account_id);
    return;
  }

  print("[%s] ignored %u\n", command_name, player->account_id);
}

inline void add_command(const char* name, command_callback_t callback, const char* help)
{
  auto command_entry = std::make_unique<command>(name, callback, help);
  convar_system->register_command(command_entry.get());
  command_entry->registered_ = true;
  registered_commands().emplace_back(std::move(command_entry));
}

inline void register_commands() {
  if (commands_registered() || convar_system == nullptr) {
    return;
  }

  registered_commands().reserve(28);
  add_command("cat_detach", command_detach_callback, "Detach cathook from TF2");
  add_command("cat_exec", command_cat_exec_callback, "Execute tf/cfg/cat_autoexec.cfg");
  add_command("cat_exec_textmode", command_cat_exec_textmode_callback, "Execute tf/cfg/cat_autoexec_textmode.cfg");
  add_command("cat_load", command_load_callback, "Load a cathook config by name");
  add_command("cat_save", command_save_callback, "Save the current cathook config by name");
  add_command("cat_unlock_achievements", command_unlock_achievements_callback, "Unlock all TF2 achievements");
  add_command("cat_lock_achievements", command_lock_achievements_callback, "Reset all TF2 achievements");
  add_command("cat_unlock_achievement", command_unlock_achievement_callback, "Unlock one TF2 achievement by ID");
  add_command("cat_lock_achievement", command_lock_achievement_callback, "Reset one TF2 achievement by ID");
  add_command("cat_dump_achievements", command_dump_achievements_callback, "Dump TF2 achievement IDs and names");
  add_command("cat_autoitem_rent", command_autoitem_rent_callback, "Request an item preview/rental by item definition ID");
  add_command("cat_autoitem_craft", command_autoitem_craft_callback, "Craft with one or more item definition IDs");
  add_command("cat_achievement_unlock_single", command_unlock_achievement_callback, "Legacy alias: unlock one TF2 achievement by ID");
  add_command("cat_achievement_lock_single", command_lock_achievement_callback, "Legacy alias: reset one TF2 achievement by ID");
  add_command("cat_achievement_unlock", command_unlock_achievements_callback, "Legacy alias: unlock all TF2 achievements");
  add_command("cat_achievement_lock", command_lock_achievements_callback, "Legacy alias: reset all TF2 achievements");
  add_command("cat_achievement_dump", command_dump_achievements_callback, "Legacy alias: dump TF2 achievement IDs and names");
  add_command("cat_rent_item", command_autoitem_rent_callback, "Legacy alias: request an item preview/rental by item definition ID");
  add_command("cat_queue", command_queue_callback, "Start casual matchmaking queue");
  add_command("cat_cancelqueue", command_cancel_queue_callback, "Cancel casual matchmaking queue");
  add_command("cat_criteria", command_criteria_callback, "Reload saved casual matchmaking criteria");
  add_command("cat_commands", command_commands_callback, "Print registered Cat commands");
  add_command("cat_playerlist_load", command_playerlist_load_callback, "Load the persistent player list");
  add_command("cat_playerlist_save", command_playerlist_save_callback, "Save the persistent player list");
  add_command("cat_playerlist_print", command_playerlist_print_callback, "Print player list entries; pass 'all' to include runtime IPC entries");
  add_command("cat_playerlist_set", command_playerlist_set_callback, "Set a player list state");
  add_command("cat_playerlist_clear", command_playerlist_clear_callback, "Clear a player list state");
  add_command("cat_playerlist_info", command_playerlist_info_callback, "Show one player list entry");
  add_command("cat_ignore", command_ignore_callback, "Toggle ignored state for a player");
  commands_registered() = true;
}

inline void unregister_commands() {
  if (!commands_registered() || convar_system == nullptr) {
    registered_commands().clear();
    commands_registered() = false;
    return;
  }

  for (auto& command_entry : registered_commands()) {
    if (command_entry != nullptr && command_entry->is_registered()) {
      convar_system->unregister_command(command_entry.get());
      command_entry->registered_ = false;
    }
  }

  registered_commands().clear();
  commands_registered() = false;
}

} // namespace cathook::core

#endif
