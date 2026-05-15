/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/automation/misc/misc.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "features/automation/misc/misc.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "core/shared/sigs.hpp"
#include "core/hooks/region_selector.hpp"
#include "core/print.hpp"
#include "core/math/math.hpp"
#include "core/logger.hpp"
#include "core/ipc/ipc_client.hpp"
#include "core/player_manager.hpp"

#include "features/menu/config.hpp"
#include "features/automation/autoitem/autoitem.hpp"
#include "features/automation/nographics/nographics.hpp"

#include "games/tf2/sdk/netvars.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/entities/entity.hpp"
#include "games/tf2/sdk/entities/weapon.hpp"
#include "games/tf2/sdk/materials/keyvalues.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/game_event_manager.hpp"

#include "libsigscan/libsigscan.h"

namespace automation
{

namespace
{

constexpr float auto_class_interval = 1.0f;
constexpr float auto_report_interval = 5.0f;
constexpr float auto_queue_interval = 5.0f;
constexpr float auto_queue_loading_timeout = 360.0f;
constexpr float noisemaker_interval = 0.2f;
constexpr float voice_command_spam_interval = 6.5f;
constexpr int micspam_min_interval_seconds = 1;
constexpr int micspam_max_interval_seconds = 600;
constexpr float mvm_command_interval = 1.0f;
constexpr float mvm_buybot_interval = 0.2f;
constexpr float ping_reduce_interval = 0.1f;
constexpr int casual_match_group_default = 7;
constexpr float anti_afk_trigger_padding = 10.0f;
constexpr int text_msg_user_message_type = 5;
constexpr int report_reason_cheating = 1;
constexpr const char* auto_balance_pending_token = "#TF_Autobalance_TeamChangePending";
constexpr float autotaunt_step_interval = 0.12f;
constexpr int max_chat_command_length = 220;
constexpr float announcer_combo_window = 5.0f;

using get_party_client_fn = void* (*)();
using get_matchmaking_client_fn = void* (*)();
using load_saved_casual_criteria_fn = void (*)(void*);
using is_in_queue_for_match_group_fn = bool (*)(void*, unsigned int);
using is_in_standby_queue_fn = bool (*)(void*);
using abandon_current_match_fn = void (*)(void*);
using request_queue_for_match_fn = void (*)(void*, unsigned int);
using request_leave_for_match_fn = void (*)(void*, unsigned int);
using request_queue_for_standby_fn = void (*)(void*);
using request_leave_standby_fn = void (*)(void*);
using report_player_account_fn = bool (*)(std::uint64_t, int);

struct party_client_api
{
  bool initialized = false;
  get_party_client_fn get_party_client = nullptr;
  get_matchmaking_client_fn get_matchmaking_client = nullptr;
  load_saved_casual_criteria_fn load_saved_casual_criteria = nullptr;
  is_in_queue_for_match_group_fn is_in_queue_for_match_group = nullptr;
  is_in_standby_queue_fn is_in_standby_queue = nullptr;
  abandon_current_match_fn abandon_current_match = nullptr;
  request_queue_for_match_fn request_queue_for_match = nullptr;
  request_leave_for_match_fn request_leave_for_match = nullptr;
  request_queue_for_standby_fn request_queue_for_standby = nullptr;
  request_leave_standby_fn request_leave_standby = nullptr;
};

party_client_api g_party_client_api{};
report_player_account_fn g_report_player_account = nullptr;
bool g_report_player_account_initialized = false;

struct text_file_cache
{
  std::string filename{};
  std::filesystem::path loaded_path{};
  std::filesystem::file_time_type last_write_time{};
  std::vector<std::string> lines{};
  bool attempted = false;
};

struct announcer_entry
{
  int count;
  const char* sound_name;
};

struct voice_command_entry
{
  Misc::Automation::voice_command_spam_mode mode;
  int menu;
  int command;
};

text_file_cache chatspam_file_cache{};
text_file_cache killsay_file_cache{};

const std::array<std::string_view, 5> builtin_chatspam_cathook = {
  "Cathook on Linux",
  "GNU/Linux is the best OS",
  "Open source TF2 tooling is fun",
  "cathook.club",
  "Free software, free frags"
};

const std::array<std::string_view, 3> builtin_chatspam_lmaobox = {
  "GET GOOD, GET LMAOBOX",
  "WWW.LMAOBOX.NET",
  "LMAOBOX - WAY TO THE TOP"
};

const std::array<std::string_view, 6> builtin_killsay_cathook = {
  "%name% met the respawn timer.",
  "%name%, perhaps your strategy should include trying.",
  "That one was for %myteam%.",
  "%class% down.",
  "Better luck next life, %name%.",
  "%killer% sends regards."
};

const std::array<std::string_view, 5> builtin_killsay_mlg = {
  "GET REKT",
  "2 FAST 4 U",
  "NICE TRY %name%",
  "%class% deleted",
  "QUICKSCOPED"
};

constexpr std::array<announcer_entry, 4> announcer_headshot_combo_sounds{{
  {1, "headshot.wav"},
  {2, "headshot.wav"},
  {4, "hattrick.wav"},
  {6, "headhunter.wav"}
}};

constexpr std::array<voice_command_entry, 20> voice_command_spam_commands{{
  {Misc::Automation::voice_command_spam_mode::medic, 0, 0},
  {Misc::Automation::voice_command_spam_mode::thanks, 0, 1},
  {Misc::Automation::voice_command_spam_mode::nice_shot, 2, 6},
  {Misc::Automation::voice_command_spam_mode::cheers, 2, 2},
  {Misc::Automation::voice_command_spam_mode::jeers, 2, 3},
  {Misc::Automation::voice_command_spam_mode::go_go_go, 0, 2},
  {Misc::Automation::voice_command_spam_mode::move_up, 0, 3},
  {Misc::Automation::voice_command_spam_mode::go_left, 0, 4},
  {Misc::Automation::voice_command_spam_mode::go_right, 0, 5},
  {Misc::Automation::voice_command_spam_mode::yes, 0, 6},
  {Misc::Automation::voice_command_spam_mode::no, 0, 7},
  {Misc::Automation::voice_command_spam_mode::incoming, 1, 0},
  {Misc::Automation::voice_command_spam_mode::spy, 1, 1},
  {Misc::Automation::voice_command_spam_mode::sentry, 1, 2},
  {Misc::Automation::voice_command_spam_mode::need_teleporter, 1, 3},
  {Misc::Automation::voice_command_spam_mode::pootis, 1, 4},
  {Misc::Automation::voice_command_spam_mode::need_sentry, 1, 5},
  {Misc::Automation::voice_command_spam_mode::activate_charge, 1, 6},
  {Misc::Automation::voice_command_spam_mode::help, 2, 0},
  {Misc::Automation::voice_command_spam_mode::battle_cry, 2, 1}
}};

constexpr std::array<announcer_entry, 12> announcer_killstreak_sounds{{
  {1, "firstblood.wav"},
  {5, "dominating.wav"},
  {7, "rampage.wav"},
  {9, "killingspree.wav"},
  {11, "monsterkill.wav"},
  {15, "unstoppable.wav"},
  {17, "ultrakill.wav"},
  {19, "godlike.wav"},
  {21, "wickedsick.wav"},
  {23, "impressive.wav"},
  {25, "ludicrouskill.wav"},
  {27, "holyshit.wav"}
}};

constexpr std::array<announcer_entry, 4> announcer_kill_combo_sounds{{
  {2, "doublekill.wav"},
  {3, "triplekill.wav"},
  {4, "multikill.wav"},
  {5, "combowhore.wav"}
}};

const char* class_name(tf_class value)
{
  switch (value)
  {
    case tf_class::SCOUT:
      return "scout";
    case tf_class::SNIPER:
      return "sniper";
    case tf_class::SOLDIER:
      return "soldier";
    case tf_class::DEMOMAN:
      return "demoman";
    case tf_class::MEDIC:
      return "medic";
    case tf_class::HEAVYWEAPONS:
      return "heavy";
    case tf_class::PYRO:
      return "pyro";
    case tf_class::SPY:
      return "spy";
    case tf_class::ENGINEER:
      return "engineer";
    case tf_class::UNDEFINED:
    default:
      return "class";
  }
}

const char* team_name(tf_team value)
{
  switch (value)
  {
    case tf_team::RED:
      return "RED";
    case tf_team::BLU:
      return "BLU";
    default:
      return "team";
  }
}

template<std::size_t count>
const announcer_entry* find_announcer_entry(const std::array<announcer_entry, count>& entries, const int value)
{
  for (const auto& entry : entries)
  {
    if (entry.count == value)
    {
      return &entry;
    }
  }

  return nullptr;
}

std::string shell_quote(std::string value)
{
  std::string quoted{};
  quoted.reserve(value.size() + 2);
  quoted.push_back('\'');
  for (const char character : value)
  {
    if (character == '\'')
    {
      quoted += "'\\''";
      continue;
    }

    quoted.push_back(character);
  }
  quoted.push_back('\'');
  return quoted;
}

std::filesystem::path resolve_announcer_sound_path(const char* sound_name)
{
  if (sound_name == nullptr || sound_name[0] == '\0')
  {
    return {};
  }

  return cathook::core::root_directory() / "assets" / "sound" / sound_name;
}

void replace_all(std::string& text, std::string_view token, std::string_view replacement)
{
  std::size_t pos = 0;
  while ((pos = text.find(token, pos)) != std::string::npos)
  {
    text.replace(pos, token.size(), replacement);
    pos += replacement.size();
  }
}

std::string sanitize_chat_text(std::string text)
{
  text.erase(std::remove_if(text.begin(), text.end(), [](const char character)
  {
    return character == '\n' || character == '\r' || character == ';';
  }), text.end());

  replace_all(text, "\"", "'");

  if (text.size() > max_chat_command_length)
  {
    text.resize(max_chat_command_length);
  }

  return text;
}

void send_chat_message(std::string message, bool team_only)
{
  if (engine == nullptr)
  {
    return;
  }

  message = sanitize_chat_text(std::move(message));
  if (message.empty())
  {
    return;
  }

  char command[320]{};
  std::snprintf(command, sizeof(command), "%s \"%s\"", team_only ? "say_team" : "say", message.c_str());
  engine->client_cmd_unrestricted(command);
}

std::string trim_chat_file_line(std::string line)
{
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
  {
    line.pop_back();
  }
  return line;
}

std::filesystem::path resolve_text_file_path(const std::string& filename)
{
  if (filename.empty())
  {
    return {};
  }

  std::error_code error{};
  const std::filesystem::path requested_path{ filename };
  if (requested_path.is_absolute() && std::filesystem::exists(requested_path, error))
  {
    return requested_path;
  }

  const std::array<std::filesystem::path, 4> candidates{
    cathook::core::root_directory() / requested_path,
    cathook::core::config_directory() / requested_path,
    std::filesystem::current_path(error) / requested_path,
    std::filesystem::current_path(error) / "config_data" / requested_path
  };

  for (const auto& candidate : candidates)
  {
    error.clear();
    if (std::filesystem::exists(candidate, error))
    {
      return candidate;
    }
  }

  return cathook::core::root_directory() / requested_path;
}

const std::vector<std::string>& load_text_lines(text_file_cache& cache, const std::string& filename)
{
  const auto path = resolve_text_file_path(filename);
  std::error_code error{};
  const auto write_time = std::filesystem::exists(path, error)
    ? std::filesystem::last_write_time(path, error)
    : std::filesystem::file_time_type{};

  if (cache.attempted && cache.filename == filename && cache.loaded_path == path && cache.last_write_time == write_time)
  {
    return cache.lines;
  }

  cache.filename = filename;
  cache.loaded_path = path;
  cache.last_write_time = write_time;
  cache.lines.clear();
  cache.attempted = true;

  std::ifstream input{ path };
  if (!input.is_open())
  {
    return cache.lines;
  }

  std::string line{};
  while (std::getline(input, line))
  {
    line = trim_chat_file_line(std::move(line));
    if (!line.empty())
    {
      cache.lines.emplace_back(std::move(line));
    }
  }

  return cache.lines;
}

template <typename message_container>
std::string choose_message(const message_container& messages, bool random_order, int& index, int& last_index)
{
  if (messages.empty())
  {
    return {};
  }

  int selected_index = index;
  if (random_order)
  {
    selected_index = std::rand() % static_cast<int>(messages.size());
    if (messages.size() > 1)
    {
      while (selected_index == last_index)
      {
        selected_index = std::rand() % static_cast<int>(messages.size());
      }
    }
  }
  else
  {
    selected_index %= static_cast<int>(messages.size());
    index = selected_index + 1;
  }

  last_index = selected_index;
  return std::string{ messages[static_cast<std::size_t>(selected_index)] };
}

std::string format_player_message(std::string message, Player* victim, Player* attacker)
{
  player_info victim_info{};
  player_info attacker_info{};
  const char* victim_name = "target";
  const char* attacker_name = "killer";

  if (victim != nullptr && engine->get_player_info(victim->get_index(), &victim_info))
  {
    victim_name = victim_info.name;
  }
  if (attacker != nullptr && engine->get_player_info(attacker->get_index(), &attacker_info))
  {
    attacker_name = attacker_info.name;
  }

  replace_all(message, "%name%", victim_name);
  replace_all(message, "%killer%", attacker_name);
  replace_all(message, "%class%", victim != nullptr ? class_name(victim->get_tf_class()) : "class");
  replace_all(message, "%myclass%", attacker != nullptr ? class_name(attacker->get_tf_class()) : "class");
  replace_all(message, "%team%", victim != nullptr ? team_name(victim->get_team()) : "team");
  replace_all(message, "%myteam%", attacker != nullptr ? team_name(attacker->get_team()) : "team");
  return message;
}

bool is_enemy_close_to_local(float safety_distance)
{
  if (entity_list == nullptr)
  {
    return true;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr)
  {
    return true;
  }

  const Vec3 local_origin = localplayer->get_origin();
  const int max_entities = entity_list->get_max_entities();
  for (int index = 1; index <= max_entities; ++index)
  {
    auto* player = entity_list->player_from_index(index);
    if (player == nullptr || player == localplayer || player->get_class_id() != class_id::PLAYER || !player->is_alive() || player->is_dormant())
    {
      continue;
    }

    if (player->get_team() == localplayer->get_team())
    {
      continue;
    }

    if (distance_3d(local_origin, player->get_origin()) <= safety_distance)
    {
      return true;
    }
  }

  for (auto* sentry : entity_cache[class_id::SENTRY])
  {
    if (sentry == nullptr || sentry->is_dormant() || sentry->get_team() == localplayer->get_team())
    {
      continue;
    }

    if (distance_3d(local_origin, sentry->get_origin()) <= safety_distance)
    {
      return true;
    }
  }

  return false;
}

void send_voice_command(int menu, int command)
{
  if (engine == nullptr)
  {
    return;
  }

  char command_text[32]{};
  std::snprintf(command_text, sizeof(command_text), "voicemenu %d %d", menu, command);
  engine->client_cmd_unrestricted(command_text);
}

void log_queue_debug(const char* fmt, ...)
{
#ifdef CATHOOK_DEBUG_AUTO_QUEUE
  va_list args{};
  va_start(args, fmt);
  print("[auto_queue] ");
  cathook::core::vlog_raw(fmt, args);
  va_end(args);
#else
  (void)fmt;
#endif
}

bool should_emit_queue_debug(float& next_log_time)
{
  if (global_vars == nullptr)
  {
    return false;
  }

  if (global_vars->realtime < next_log_time)
  {
    return false;
  }

  next_log_time = global_vars->realtime + 2.0f;
  return true;
}

request_queue_for_match_fn get_shared_request_queue_for_match()
{
  if (!region_selector_request_queue_for_match_available())
  {
    return nullptr;
  }

  return request_queue_for_match_with_region_selector;
}

void initialize_party_client_api()
{
  if (g_party_client_api.initialized)
  {
    return;
  }

  auto* get_party_client_match = sigscan_module("client.so", sigs::get_party_client);
  auto* get_matchmaking_client_match = sigscan_module("client.so", sigs::get_matchmaking_client);

  g_party_client_api.initialized = true;
  if (get_party_client_match != nullptr)
  {
    g_party_client_api.get_party_client = reinterpret_cast<get_party_client_fn>(
      reinterpret_cast<std::uintptr_t>(get_party_client_match) + sigs::get_party_client_offset);
  }
  if (get_matchmaking_client_match != nullptr)
  {
    g_party_client_api.get_matchmaking_client = reinterpret_cast<get_matchmaking_client_fn>(get_matchmaking_client_match);
  }
  g_party_client_api.load_saved_casual_criteria = reinterpret_cast<load_saved_casual_criteria_fn>(sigscan_module("client.so", sigs::load_saved_casual_criteria));
  g_party_client_api.is_in_queue_for_match_group = reinterpret_cast<is_in_queue_for_match_group_fn>(sigscan_module("client.so", sigs::is_in_queue_for_match_group));
  g_party_client_api.is_in_standby_queue = reinterpret_cast<is_in_standby_queue_fn>(sigscan_module("client.so", sigs::is_in_standby_queue));
  g_party_client_api.abandon_current_match = reinterpret_cast<abandon_current_match_fn>(sigscan_module("client.so", sigs::abandon_current_match));
  g_party_client_api.request_queue_for_match = get_shared_request_queue_for_match();
  g_party_client_api.request_leave_for_match = reinterpret_cast<request_leave_for_match_fn>(sigscan_module("client.so", sigs::request_leave_for_match));
  g_party_client_api.request_queue_for_standby = reinterpret_cast<request_queue_for_standby_fn>(sigscan_module("client.so", sigs::request_queue_for_standby));
  g_party_client_api.request_leave_standby = reinterpret_cast<request_leave_standby_fn>(sigscan_module("client.so", sigs::request_leave_standby));

  log_queue_debug(
    "api init get_party_client=%p get_matchmaking_client=%p load_saved_casual_criteria=%p is_in_queue=%p is_in_standby=%p abandon_current_match=%p request_queue=%p request_leave=%p request_queue_standby=%p request_leave_standby=%p\n",
    reinterpret_cast<void*>(g_party_client_api.get_party_client),
    reinterpret_cast<void*>(g_party_client_api.get_matchmaking_client),
    reinterpret_cast<void*>(g_party_client_api.load_saved_casual_criteria),
    reinterpret_cast<void*>(g_party_client_api.is_in_queue_for_match_group),
    reinterpret_cast<void*>(g_party_client_api.is_in_standby_queue),
    reinterpret_cast<void*>(g_party_client_api.abandon_current_match),
    reinterpret_cast<void*>(g_party_client_api.request_queue_for_match),
    reinterpret_cast<void*>(g_party_client_api.request_leave_for_match),
    reinterpret_cast<void*>(g_party_client_api.request_queue_for_standby),
    reinterpret_cast<void*>(g_party_client_api.request_leave_standby));
}

bool party_client_api_ready()
{
  initialize_party_client_api();
  return g_party_client_api.get_party_client != nullptr &&
         g_party_client_api.is_in_queue_for_match_group != nullptr &&
         g_party_client_api.is_in_standby_queue != nullptr &&
         g_party_client_api.request_queue_for_match != nullptr;
}

void* get_party_client()
{
  if (!party_client_api_ready())
  {
    return nullptr;
  }

  return g_party_client_api.get_party_client();
}

bool is_in_standby_queue(void* party_client)
{
  if (party_client == nullptr || g_party_client_api.is_in_standby_queue == nullptr)
  {
    return false;
  }

  return g_party_client_api.is_in_standby_queue(party_client);
}

bool abandon_current_match()
{
  initialize_party_client_api();

  if (g_party_client_api.get_matchmaking_client == nullptr ||
      g_party_client_api.abandon_current_match == nullptr)
  {
    return false;
  }

  auto* matchmaking_client = g_party_client_api.get_matchmaking_client();
  if (matchmaking_client == nullptr)
  {
    return false;
  }

  g_party_client_api.abandon_current_match(matchmaking_client);
  return true;
}

bool request_match_queue(void* party_client, unsigned int queue_mode)
{
  initialize_party_client_api();
  if (party_client == nullptr || g_party_client_api.request_queue_for_match == nullptr)
  {
    return false;
  }

  if (queue_mode == casual_match_group_default &&
      g_party_client_api.load_saved_casual_criteria != nullptr)
  {
    g_party_client_api.load_saved_casual_criteria(party_client);
  }

  g_party_client_api.request_queue_for_match(party_client, queue_mode);
  return true;
}

bool cancel_match_queue(void* party_client, unsigned int queue_mode)
{
  initialize_party_client_api();
  if (party_client == nullptr || g_party_client_api.request_leave_for_match == nullptr)
  {
    return false;
  }

  g_party_client_api.request_leave_for_match(party_client, queue_mode);
  return true;
}

Entity* get_player_resource_entity()
{
  if (entity_list == nullptr)
  {
    return nullptr;
  }

  const int max_entities = entity_list->get_max_entities();
  for (int index = 1; index <= max_entities; ++index)
  {
    auto* entity = entity_list->entity_from_index(index);
    if (entity == nullptr)
    {
      continue;
    }

    if (entity->get_class_id() == class_id::PLAYER_RESOURCE)
    {
      return entity;
    }
  }

  return nullptr;
}

template <typename value_type>
value_type read_player_resource_value(Entity* player_resource, int array_offset, int player_index)
{
  if (player_resource == nullptr || array_offset <= 0 || player_index <= 0)
  {
    return {};
  }

  const auto base = reinterpret_cast<std::uintptr_t>(player_resource);
  const auto entry_offset = static_cast<std::uintptr_t>(array_offset) + (static_cast<std::uintptr_t>(player_index) * sizeof(value_type));
  return *reinterpret_cast<value_type*>(base + entry_offset);
}

int get_local_ping()
{
  if (engine == nullptr)
  {
    return 0;
  }

  static const int ping_offset = tf2_netvars::find_offset("DT_TFPlayerResource", { "baseclass", "m_iPing" });
  if (ping_offset <= 0)
  {
    return 0;
  }

  auto* player_resource = get_player_resource_entity();
  return read_player_resource_value<int>(player_resource, ping_offset, engine->get_localplayer_index());
}

int count_requeue_players()
{
  if (entity_list == nullptr || engine == nullptr)
  {
    return 0;
  }

  int human_players = 0;
  const int max_entities = entity_list->get_max_entities();
  for (int index = 1; index <= max_entities; ++index)
  {
    auto* player = entity_list->player_from_index(index);
    if (player == nullptr || player->get_class_id() != class_id::PLAYER)
    {
      continue;
    }

    player_info info{};
    if (!engine->get_player_info(index, &info) || info.fakeplayer)
    {
      continue;
    }

    const auto account_id = static_cast<std::uint32_t>(info.friends_id);
    if (account_id != 0 && cat_ipc::client::is_local_ipc_friend(account_id))
    {
      continue;
    }

    if (config.misc.automation.rq_ignore_friends &&
        account_id != 0 &&
        (player->is_friend() || player->is_ignored() || cathook::core::players::is_friendly(account_id)))
    {
      continue;
    }

    ++human_players;
  }

  return human_players;
}

bool should_trigger_player_threshold_requeue(int human_players)
{
  const int players_lte = config.misc.automation.rq_if_players_lte;
  const int players_gte = config.misc.automation.rq_if_players_gte;
  const bool hit_lte = players_lte > 0 && human_players <= players_lte;
  const bool hit_gte = players_gte > 0 && human_players >= players_gte;
  return hit_lte || hit_gte;
}

bool should_trigger_ipc_bot_threshold_requeue()
{
  return cat_ipc::client::is_excess_ipc_bot_on_current_server(config.misc.automation.rq_if_ipc_bots_gt);
}

const char* class_name_for_join(tf_class selected_class)
{
  switch (selected_class)
  {
    case tf_class::SCOUT:
      return "scout";
    case tf_class::SNIPER:
      return "sniper";
    case tf_class::SOLDIER:
      return "soldier";
    case tf_class::DEMOMAN:
      return "demoman";
    case tf_class::MEDIC:
      return "medic";
    case tf_class::HEAVYWEAPONS:
      return "heavyweapons";
    case tf_class::PYRO:
      return "pyro";
    case tf_class::SPY:
      return "spy";
    case tf_class::ENGINEER:
      return "engineer";
    case tf_class::UNDEFINED:
    default:
      return "sniper";
  }
}

bool in_valid_team(tf_team team)
{
  return team == tf_team::RED || team == tf_team::BLU;
}

bool has_movement_input(user_cmd* user_cmd)
{
  if (user_cmd == nullptr)
  {
    return false;
  }

  return (user_cmd->buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT)) != 0;
}

bool is_mvm_context()
{
  if (engine == nullptr || !engine->is_in_game())
  {
    return false;
  }

  const char* level_name = engine->get_level_name();
  return level_name != nullptr && std::strstr(level_name, "mvm_") != nullptr;
}

void send_mvm_command(KeyValues* key_values)
{
  if (engine == nullptr || key_values == nullptr)
  {
    return;
  }

  engine->server_cmd_keyvalues(key_values);
}

void send_mvm_upgrade(int item_slot, int upgrade, int count)
{
  auto* key_values = new KeyValues("MVM_Upgrade");
  char buffer[192]{};
  std::snprintf(
    buffer,
    sizeof(buffer),
    "\"MVM_Upgrade\"\n{\n \"Upgrade\"\n {\n  \"itemslot\" \"%d\"\n  \"Upgrade\" \"%d\"\n  \"count\" \"%d\"\n }\n}\n",
    item_slot,
    upgrade,
    count);
  key_values->load_from_buffer("MVM_Upgrade", buffer);
  send_mvm_command(key_values);
}

void send_mvm_upgrades_done(int num_upgrades)
{
  auto* key_values = new KeyValues("MvM_UpgradesDone");
  key_values->set_int("num_upgrades", num_upgrades);
  send_mvm_command(key_values);
}

void initialize_report_player_account()
{
  if (g_report_player_account_initialized)
  {
    return;
  }

  g_report_player_account_initialized = true;
  g_report_player_account = reinterpret_cast<report_player_account_fn>(sigscan_module("client.so", sigs::report_player_account));
#ifdef CATHOOK_DEBUG_AUTO_REPORT
  print("[auto_report] report_player_account=%p\n", reinterpret_cast<void*>(g_report_player_account));
#endif
}

std::string_view read_text_message_token(const bf_read* message_data)
{
  if (message_data == nullptr || !message_data->is_valid() || message_data->data_bytes <= 1)
  {
    return {};
  }

  const auto* text = reinterpret_cast<const char*>(message_data->data + 1);
  std::size_t text_length = 0;
  const auto max_length = static_cast<std::size_t>(message_data->data_bytes - 1);
  while (text_length < max_length && text[text_length] != '\0')
  {
    ++text_length;
  }
  return std::string_view{text, text_length};
}

} // namespace

automation_controller& controller()
{
  static automation_controller instance{};
  return instance;
}

bool reload_casual_criteria()
{
  initialize_party_client_api();

  if (g_party_client_api.get_party_client == nullptr ||
      g_party_client_api.load_saved_casual_criteria == nullptr)
  {
    return false;
  }

  auto* party_client = g_party_client_api.get_party_client();
  if (party_client == nullptr)
  {
    return false;
  }

  g_party_client_api.load_saved_casual_criteria(party_client);
  return true;
}

bool request_casual_queue()
{
  initialize_party_client_api();

  if (g_party_client_api.get_party_client == nullptr ||
      g_party_client_api.request_queue_for_match == nullptr)
  {
    return false;
  }

  auto* party_client = g_party_client_api.get_party_client();
  if (party_client == nullptr)
  {
    return false;
  }

  if (g_party_client_api.load_saved_casual_criteria != nullptr)
  {
    g_party_client_api.load_saved_casual_criteria(party_client);
  }

  g_party_client_api.request_queue_for_match(party_client, casual_match_group_default);
  return true;
}

bool cancel_casual_queue()
{
  initialize_party_client_api();

  if (g_party_client_api.get_party_client == nullptr ||
      g_party_client_api.request_leave_for_match == nullptr)
  {
    return false;
  }

  auto* party_client = g_party_client_api.get_party_client();
  if (party_client == nullptr)
  {
    return false;
  }

  g_party_client_api.request_leave_for_match(party_client, casual_match_group_default);
  return true;
}

void automation_controller::on_create_move(user_cmd* user_cmd)
{
  if (engine == nullptr || global_vars == nullptr)
  {
    return;
  }

  apply_misc_convars();
  nographics::update();
  run_auto_class_select();
  run_anti_afk(user_cmd);
  run_chatspam();
  run_noisemaker_spam();
  run_voice_command_spam();
  run_micspam();
  autoitem::on_create_move();
  run_mvm_actions();

  auto* localplayer = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  if (autotaunt_previous_slot_ != -1 && localplayer != nullptr && localplayer->is_alive() && global_vars->realtime >= next_autotaunt_time_)
  {
    if (!autotaunt_waiting_for_taunt_)
    {
      engine->client_cmd_unrestricted("taunt");
      autotaunt_waiting_for_taunt_ = true;
      next_autotaunt_time_ = global_vars->realtime + autotaunt_step_interval;
    }
    else if (!localplayer->in_cond(TF_COND_TAUNTING))
    {
      char command[32]{};
      std::snprintf(command, sizeof(command), "slot%d", autotaunt_previous_slot_ + 1);
      engine->client_cmd_unrestricted(command);
      autotaunt_previous_slot_ = -1;
      autotaunt_waiting_for_taunt_ = false;
    }
    else
    {
      next_autotaunt_time_ = global_vars->realtime + autotaunt_step_interval;
    }
  }
}

void automation_controller::on_frame_stage_notify()
{
  if (engine == nullptr || global_vars == nullptr)
  {
    return;
  }

  run_auto_report();
  run_ping_reducer();
  run_mvm_actions();
  run_queueing();
}

void automation_controller::on_paint()
{
  nographics::update();
  process_killsay();

  if (engine == nullptr || global_vars == nullptr)
  {
    return;
  }

  run_queueing();
}

void automation_controller::on_dispatch_user_message(int message_type, const bf_read* message_data)
{
  if (!config.misc.automation.anti_autobalance || engine == nullptr)
  {
    return;
  }

  if (message_type != text_msg_user_message_type)
  {
    return;
  }

  const auto message = read_text_message_token(message_data);
  if (message != auto_balance_pending_token)
  {
    return;
  }

  print("[anti_autobalance] retrying on pending team change\n");
  engine->client_cmd_unrestricted("retry");
}

void automation_controller::on_game_event(GameEvent* event)
{
  run_auto_vote_map(event);
  run_autotaunt(event);
  run_custom_announcer(event);
  run_killsay(event);
}

void automation_controller::apply_misc_convars()
{
  auto* allow_secure_servers = get_allow_secure_servers_flag();
  if (allow_secure_servers != nullptr)
  {
    if (config.misc.exploits.vac_bypass)
    {
      if (!vac_bypass_applied_)
      {
        original_allow_secure_servers_ = *allow_secure_servers;
        vac_bypass_applied_ = true;
      }

      *allow_secure_servers = true;
    }
    else if (vac_bypass_applied_)
    {
      *allow_secure_servers = original_allow_secure_servers_;
      vac_bypass_applied_ = false;
    }
  }

  if (convar_system == nullptr)
  {
    return;
  }

  static Convar* no_push = convar_system->find_var("tf_avoidteammates_pushaway");
  if (no_push != nullptr)
  {
    const int wanted_value = config.misc.movement.no_push ? 0 : 1;
    if (no_push->get_int() != wanted_value)
    {
      no_push->set_int(wanted_value);
    }
  }

  static Convar* engine_no_focus_sleep = convar_system->find_var("engine_no_focus_sleep");
  if (engine_no_focus_sleep != nullptr)
  {
    const int wanted_value = config.misc.exploits.no_engine_sleep ? 0 : 50;
    if (engine_no_focus_sleep->get_int() != wanted_value)
    {
      engine_no_focus_sleep->set_int(wanted_value);
    }
  }

  static Convar* sv_cheats = convar_system->find_var("sv_cheats");
  if (sv_cheats != nullptr)
  {
    const bool should_bypass_cheats = config.misc.exploits.cheats_bypass;
    if (should_bypass_cheats)
    {
      if (!cheats_bypass_applied_)
      {
        original_sv_cheats_value_ = sv_cheats->get_int();
        cheats_bypass_applied_ = true;
      }

      if (sv_cheats->get_int() != 1)
      {
        sv_cheats->set_int(1);
      }
    }
    else if (cheats_bypass_applied_)
    {
      sv_cheats->set_int(original_sv_cheats_value_);
      cheats_bypass_applied_ = false;
    }
  }

  static Convar* weapon_allow_inspect = convar_system->find_var("weapon_allow_inspect");
  if (weapon_allow_inspect != nullptr)
  {
    const int wanted_value = config.misc.automation.allow_mvm_inspect ? 1 : 0;
    if (weapon_allow_inspect->get_int() != wanted_value)
    {
      weapon_allow_inspect->set_int(wanted_value);
    }
  }
}

void automation_controller::run_auto_class_select()
{
  if (!config.misc.automation.auto_class_select)
  {
    return;
  }

  if (!engine->is_in_game() || global_vars->realtime < next_class_action_time_)
  {
    return;
  }

  auto* localplayer = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  if (localplayer == nullptr)
  {
    return;
  }

  if (!in_valid_team(localplayer->get_team()))
  {
    engine->client_cmd_unrestricted("team_ui_setup");
    engine->client_cmd_unrestricted("menuopen");
    engine->client_cmd_unrestricted("autoteam");
    engine->client_cmd_unrestricted("menuclosed");
    next_class_action_time_ = global_vars->realtime + auto_class_interval;
    return;
  }

  const auto selected_class = config.misc.automation.class_selected;
  const bool is_selected_class = localplayer->get_tf_class() == selected_class;
  if (localplayer->is_alive() && is_selected_class)
  {
    return;
  }

  char join_class_command[64]{};
  std::snprintf(join_class_command, sizeof(join_class_command), "joinclass %s", class_name_for_join(selected_class));
  engine->client_cmd_unrestricted(join_class_command);
  engine->client_cmd_unrestricted("menuclosed");
  next_class_action_time_ = global_vars->realtime + auto_class_interval;
}

void automation_controller::run_anti_afk(user_cmd* user_cmd)
{
  if (global_vars == nullptr || convar_system == nullptr || entity_list == nullptr)
  {
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (!engine->is_in_game() || localplayer == nullptr)
  {
    last_active_input_time_ = 0.0f;
    return;
  }

  if (last_active_input_time_ <= 0.0f)
  {
    last_active_input_time_ = global_vars->realtime;
  }

  if (has_movement_input(user_cmd) || !localplayer->is_alive())
  {
    last_active_input_time_ = global_vars->realtime;
    return;
  }

  if (!config.misc.automation.anti_afk || user_cmd == nullptr)
  {
    return;
  }

  static Convar* mp_idledealmethod = convar_system->find_var("mp_idledealmethod");
  static Convar* mp_idlemaxtime = convar_system->find_var("mp_idlemaxtime");
  if (mp_idledealmethod == nullptr || mp_idlemaxtime == nullptr)
  {
    return;
  }

  const int idle_method = mp_idledealmethod->get_int();
  const float max_idle_time = mp_idlemaxtime->get_float();
  if (idle_method == 0 || max_idle_time <= 0.0f)
  {
    return;
  }

  const float trigger_time = std::max(0.0f, (max_idle_time * 60.0f) - anti_afk_trigger_padding);
  if ((global_vars->realtime - last_active_input_time_) < trigger_time)
  {
    return;
  }

  user_cmd->buttons |= (global_vars->tickcount & 1) != 0 ? IN_FORWARD : IN_BACK;
  last_active_input_time_ = global_vars->realtime;
}

void automation_controller::run_auto_report()
{
  if (!config.misc.automation.auto_report ||
      global_vars == nullptr ||
      engine == nullptr ||
      entity_list == nullptr)
  {
    if (engine == nullptr || !engine->is_in_game())
    {
      reported_account_ids_.clear();
      next_auto_report_time_ = 0.0f;
    }
    return;
  }

  if (!engine->is_in_game())
  {
    reported_account_ids_.clear();
    next_auto_report_time_ = 0.0f;
    return;
  }

  if (global_vars->realtime < next_auto_report_time_)
  {
    return;
  }

  next_auto_report_time_ = global_vars->realtime + auto_report_interval;
  initialize_report_player_account();
  if (g_report_player_account == nullptr)
  {
    return;
  }

  const int local_index = engine->get_localplayer_index();
  const int max_entities = entity_list->get_max_entities();
  for (int index = 1; index <= max_entities; ++index)
  {
    if (index == local_index)
    {
      continue;
    }

    auto* player = entity_list->player_from_index(index);
    if (player == nullptr || player->get_class_id() != class_id::PLAYER)
    {
      continue;
    }

    player_info info{};
    if (!engine->get_player_info(index, &info) || info.fakeplayer || info.friends_id == 0)
    {
      continue;
    }

    if (player->is_friend())
    {
      continue;
    }

    if (std::ranges::find(reported_account_ids_, info.friends_id) != reported_account_ids_.end())
    {
      continue;
    }

    const auto steam_id = SteamID(info.friends_id, 1, k_EUniversePublic, k_EAccountTypeIndividual);
    if (g_report_player_account(static_cast<std::uint64_t>(steam_id.m_steamid.m_unAll64Bits), report_reason_cheating))
    {
      reported_account_ids_.push_back(info.friends_id);
#ifdef CATHOOK_DEBUG_AUTO_REPORT
      print("[auto_report] reported %s (%lu)\n", info.name, info.friends_id);
#endif
    }
  }
}

void automation_controller::run_auto_vote_map(GameEvent* event)
{
  if (!config.misc.automation.auto_vote_map || event == nullptr || engine == nullptr)
  {
    return;
  }

  const char* event_name = event->get_name();
  if (event_name == nullptr || std::strcmp(event_name, "vote_maps_changed") != 0)
  {
    return;
  }

  const int vote_option = std::clamp(config.misc.automation.auto_vote_map_option, 0, 2);
  char command[32]{};
  std::snprintf(command, sizeof(command), "next_map_vote %d", vote_option);
  engine->client_cmd_unrestricted(command);
}

void automation_controller::run_autotaunt(GameEvent* event)
{
  if (!config.misc.automation.autotaunt || event == nullptr || engine == nullptr || entity_list == nullptr || global_vars == nullptr)
  {
    return;
  }

  const char* event_name = event->get_name();
  if (event_name == nullptr || std::strcmp(event_name, "player_death") != 0)
  {
    return;
  }

  if (engine->get_player_index_from_id(event->get_int("attacker")) != engine->get_localplayer_index())
  {
    return;
  }

  const int victim_index = engine->get_player_index_from_id(event->get_int("userid"));
  if (victim_index == engine->get_localplayer_index())
  {
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive())
  {
    return;
  }

  const float chance = std::clamp(config.misc.automation.autotaunt_chance, 0.0f, 100.0f);
  if (chance <= 0.0f || static_cast<float>(std::rand() % 10000) / 100.0f > chance)
  {
    return;
  }

  if (is_enemy_close_to_local(std::max(0.0f, config.misc.automation.autotaunt_safety_distance)))
  {
    return;
  }

  autotaunt_previous_slot_ = -1;
  autotaunt_waiting_for_taunt_ = false;

  const int wanted_slot = std::clamp(config.misc.automation.autotaunt_weapon_slot, 0, 5);
  auto* weapon = localplayer->get_weapon();
  if (wanted_slot > 0 && weapon != nullptr)
  {
    autotaunt_previous_slot_ = weapon->get_slot();
    char command[32]{};
    std::snprintf(command, sizeof(command), "slot%d", wanted_slot);
    engine->client_cmd_unrestricted(command);
  }
  else
  {
    autotaunt_previous_slot_ = weapon != nullptr ? weapon->get_slot() : 0;
  }

  next_autotaunt_time_ = global_vars->realtime + autotaunt_step_interval;
}

void automation_controller::run_custom_announcer(GameEvent* event)
{
  if (!config.misc.automation.custom_announcer ||
      event == nullptr ||
      engine == nullptr ||
      entity_list == nullptr ||
      global_vars == nullptr)
  {
    return;
  }

  const char* event_name = event->get_name();
  if (event_name == nullptr)
  {
    return;
  }

  if (std::strcmp(event_name, "player_spawn") == 0)
  {
    const int player_index = engine->get_player_index_from_id(event->get_int("userid"));
    if (player_index == engine->get_localplayer_index())
    {
      reset_custom_announcer();
    }
    return;
  }

  if (std::strcmp(event_name, "player_death") != 0)
  {
    return;
  }

  const int attacker_index = engine->get_player_index_from_id(event->get_int("attacker"));
  const int victim_index = engine->get_player_index_from_id(event->get_int("userid"));
  const int local_index = engine->get_localplayer_index();
  if (victim_index == local_index)
  {
    reset_custom_announcer();
    return;
  }

  if (attacker_index != local_index || attacker_index == victim_index)
  {
    return;
  }

  if ((global_vars->realtime - announcer_last_kill_time_) > announcer_combo_window)
  {
    announcer_kill_combo_ = 0;
  }
  if ((global_vars->realtime - announcer_last_headshot_time_) > announcer_combo_window)
  {
    announcer_headshot_combo_ = 0;
  }

  announcer_last_kill_time_ = global_vars->realtime;
  ++announcer_killstreak_;
  ++announcer_kill_combo_;

  auto* localplayer = entity_list->get_localplayer();
  auto* weapon = localplayer != nullptr ? localplayer->get_weapon() : nullptr;
  if (weapon != nullptr && weapon->is_melee())
  {
    play_custom_announcer_sound("humiliation.wav");
    return;
  }

  if (event->get_int("customkill") == 1)
  {
    announcer_last_headshot_time_ = global_vars->realtime;
    ++announcer_headshot_combo_;

    if (const auto* entry = find_announcer_entry(announcer_headshot_combo_sounds, static_cast<int>(announcer_headshot_combo_)))
    {
      play_custom_announcer_sound(entry->sound_name);
      return;
    }
  }

  if (const auto* entry = find_announcer_entry(announcer_kill_combo_sounds, static_cast<int>(announcer_kill_combo_)))
  {
    play_custom_announcer_sound(entry->sound_name);
    return;
  }

  if (const auto* entry = find_announcer_entry(announcer_killstreak_sounds, static_cast<int>(announcer_killstreak_)))
  {
    play_custom_announcer_sound(entry->sound_name);
  }
}

void automation_controller::reset_custom_announcer()
{
  announcer_killstreak_ = 0;
  announcer_kill_combo_ = 0;
  announcer_headshot_combo_ = 0;
  announcer_last_kill_time_ = -100000.0f;
  announcer_last_headshot_time_ = -100000.0f;
}

void automation_controller::play_custom_announcer_sound(const char* sound_name)
{
  const auto sound_path = resolve_announcer_sound_path(sound_name);
  if (sound_path.empty() || !std::filesystem::exists(sound_path))
  {
    return;
  }

  const auto quoted_path = shell_quote(sound_path.string());
  const std::string command =
    "(paplay " + quoted_path +
    " || pw-play " + quoted_path +
    " || aplay -q " + quoted_path +
    ") >/dev/null 2>&1 &";
  std::system(command.c_str());
}

void automation_controller::run_chatspam()
{
  if (engine == nullptr || global_vars == nullptr || !engine->is_in_game())
  {
    next_chatspam_time_ = 0.0f;
    return;
  }

  if (config.misc.automation.chatspam == Misc::Automation::chatspam_source::OFF)
  {
    return;
  }

  if (global_vars->realtime < next_chatspam_time_)
  {
    return;
  }

  std::string message{};
  switch (config.misc.automation.chatspam)
  {
    case Misc::Automation::chatspam_source::CATHOOK:
      message = choose_message(builtin_chatspam_cathook, config.misc.automation.chatspam_random, chatspam_index_, chatspam_last_index_);
      break;
    case Misc::Automation::chatspam_source::LMAOBOX:
      message = choose_message(builtin_chatspam_lmaobox, config.misc.automation.chatspam_random, chatspam_index_, chatspam_last_index_);
      break;
    case Misc::Automation::chatspam_source::CUSTOM:
      message = choose_message(
        load_text_lines(chatspam_file_cache, config.misc.automation.chatspam_file),
        config.misc.automation.chatspam_random,
        chatspam_index_,
        chatspam_last_index_);
      break;
    case Misc::Automation::chatspam_source::OFF:
    default:
      break;
  }

  send_chat_message(std::move(message), config.misc.automation.chatspam_team);
  const int delay_ms = std::clamp(config.misc.automation.chatspam_delay_ms, 250, 60000);
  next_chatspam_time_ = global_vars->realtime + (static_cast<float>(delay_ms) / 1000.0f);
}

void automation_controller::run_killsay(GameEvent* event)
{
  if (config.misc.automation.killsay == Misc::Automation::killsay_mode::OFF ||
      event == nullptr ||
      engine == nullptr ||
      entity_list == nullptr ||
      global_vars == nullptr)
  {
    return;
  }

  const char* event_name = event->get_name();
  if (event_name == nullptr || std::strcmp(event_name, "player_death") != 0)
  {
    return;
  }

  const int attacker_index = engine->get_player_index_from_id(event->get_int("attacker"));
  const int victim_index = engine->get_player_index_from_id(event->get_int("userid"));
  if (attacker_index != engine->get_localplayer_index() || victim_index == attacker_index)
  {
    return;
  }

  auto* attacker = entity_list->player_from_index(attacker_index);
  auto* victim = entity_list->player_from_index(victim_index);
  if (attacker == nullptr || victim == nullptr)
  {
    return;
  }

  std::string message{};
  switch (config.misc.automation.killsay)
  {
    case Misc::Automation::killsay_mode::CATHOOK:
      message = choose_message(builtin_killsay_cathook, true, chatspam_index_, chatspam_last_index_);
      break;
    case Misc::Automation::killsay_mode::MLG:
      message = choose_message(builtin_killsay_mlg, true, chatspam_index_, chatspam_last_index_);
      break;
    case Misc::Automation::killsay_mode::CUSTOM:
      message = choose_message(
        load_text_lines(killsay_file_cache, config.misc.automation.killsay_file),
        true,
        chatspam_index_,
        chatspam_last_index_);
      break;
    case Misc::Automation::killsay_mode::OFF:
    default:
      break;
  }

  message = format_player_message(std::move(message), victim, attacker);
  if (message.empty())
  {
    return;
  }

  const int delay_ms = std::clamp(config.misc.automation.killsay_delay_ms, 0, 10000);
  pending_killsays_.emplace_back(global_vars->realtime + (static_cast<float>(delay_ms) / 1000.0f), std::move(message));
}

void automation_controller::process_killsay()
{
  if (pending_killsays_.empty() || global_vars == nullptr)
  {
    return;
  }

  for (auto& entry : pending_killsays_)
  {
    if (entry.first <= global_vars->realtime && !entry.second.empty())
    {
      send_chat_message(std::move(entry.second), false);
      entry.second.clear();
    }
  }

  pending_killsays_.erase(
    std::remove_if(pending_killsays_.begin(), pending_killsays_.end(), [](const auto& entry)
    {
      return entry.second.empty();
    }),
    pending_killsays_.end());
}

void automation_controller::run_noisemaker_spam()
{
  if (!config.misc.automation.noisemaker_spam || engine == nullptr || global_vars == nullptr)
  {
    return;
  }

  if (!engine->is_in_game() || global_vars->realtime < next_noisemaker_time_)
  {
    return;
  }

  engine->client_cmd_unrestricted("use_action_slot_item_server");
  next_noisemaker_time_ = global_vars->realtime + noisemaker_interval;
}

void automation_controller::run_voice_command_spam()
{
  if (engine == nullptr || entity_list == nullptr || global_vars == nullptr)
  {
    return;
  }

  const auto mode = config.misc.automation.voice_command_spam;
  if (mode == Misc::Automation::voice_command_spam_mode::off || !engine->is_in_game())
  {
    next_voice_command_time_ = 0.0f;
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive())
  {
    return;
  }

  if (global_vars->realtime < next_voice_command_time_)
  {
    return;
  }

  if (mode == Misc::Automation::voice_command_spam_mode::random)
  {
    send_voice_command(std::rand() % 3, std::rand() % 9);
    next_voice_command_time_ = global_vars->realtime + voice_command_spam_interval;
    return;
  }

  const auto found = std::ranges::find_if(voice_command_spam_commands, [mode](const voice_command_entry& entry)
  {
    return entry.mode == mode;
  });

  if (found != voice_command_spam_commands.end())
  {
    send_voice_command(found->menu, found->command);
  }

  next_voice_command_time_ = global_vars->realtime + voice_command_spam_interval;
}

void automation_controller::stop_micspam()
{
  next_micspam_on_time_ = 0.0f;
  next_micspam_off_time_ = 0.0f;

  if (!micspam_recording_ || engine == nullptr)
  {
    micspam_recording_ = false;
    return;
  }

  engine->client_cmd_unrestricted("-voicerecord");
  micspam_recording_ = false;
}

void automation_controller::run_micspam()
{
  if (!config.misc.automation.micspam || engine == nullptr || global_vars == nullptr || !engine->is_in_game())
  {
    stop_micspam();
    return;
  }

  const float now = global_vars->realtime;
  const int on_seconds = std::clamp(
    config.misc.automation.micspam_interval_on_seconds,
    micspam_min_interval_seconds,
    micspam_max_interval_seconds);
  const int off_seconds = std::clamp(
    config.misc.automation.micspam_interval_off_seconds,
    micspam_min_interval_seconds,
    micspam_max_interval_seconds);

  if (next_micspam_on_time_ <= 0.0f)
  {
    next_micspam_on_time_ = now;
  }
  if (next_micspam_off_time_ <= 0.0f)
  {
    next_micspam_off_time_ = now + static_cast<float>(off_seconds);
  }

  if (now >= next_micspam_on_time_)
  {
    engine->client_cmd_unrestricted("+voicerecord");
    micspam_recording_ = true;
    next_micspam_on_time_ = now + static_cast<float>(on_seconds);
  }

  if (now >= next_micspam_off_time_)
  {
    engine->client_cmd_unrestricted("-voicerecord");
    micspam_recording_ = false;
    next_micspam_off_time_ = now + static_cast<float>(off_seconds);
  }
}

void automation_controller::run_mvm_actions()
{
  if (engine == nullptr || global_vars == nullptr || entity_list == nullptr || convar_system == nullptr)
  {
    return;
  }

  if (!engine->is_in_game())
  {
    mvm_buybot_step_ = 1;
    next_mvm_buybot_time_ = 0.0f;
    return;
  }

  if (!is_mvm_context())
  {
    mvm_buybot_step_ = 1;
    next_mvm_buybot_time_ = 0.0f;
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr)
  {
    return;
  }

  bool issued_command = false;
  if (global_vars->realtime >= next_mvm_command_time_ &&
      (config.misc.automation.mvm_instant_respawn || config.misc.automation.mvm_instant_revive) &&
      !localplayer->is_alive())
  {
    engine->client_cmd_unrestricted("MVM_Revive_Response accepted 1");
    issued_command = true;
  }

  if (global_vars->realtime >= next_mvm_command_time_ && config.misc.automation.auto_mvm_ready_up)
  {
    engine->client_cmd_unrestricted("tournament_player_readystate 1");
    issued_command = true;
  }

  if (issued_command)
  {
    next_mvm_command_time_ = global_vars->realtime + mvm_command_interval;
  }

  if (!config.misc.automation.mvm_buybot)
  {
    mvm_buybot_step_ = 1;
    next_mvm_buybot_time_ = 0.0f;
    return;
  }

  if (!localplayer->in_upgrade_zone())
  {
    mvm_buybot_step_ = 1;
    next_mvm_buybot_time_ = 0.0f;
    return;
  }

  if (global_vars->realtime < next_mvm_buybot_time_)
  {
    return;
  }

  const int max_cash = std::max(config.misc.automation.mvm_buybot_max_cash, 0);
  if (max_cash > 0 && localplayer->get_currency() >= max_cash)
  {
    return;
  }

  static Convar* tf_mvm_respec_enabled = nullptr;
  if (tf_mvm_respec_enabled == nullptr)
  {
    tf_mvm_respec_enabled = convar_system->find_var("tf_mvm_respec_enabled");
  }

  if (tf_mvm_respec_enabled == nullptr || tf_mvm_respec_enabled->get_int() != 1)
  {
    return;
  }

  send_mvm_command(new KeyValues("MvM_UpgradesBegin"));
  switch (mvm_buybot_step_)
  {
    case 1:
      send_mvm_upgrade(1, 19, 1);
      send_mvm_upgrade(1, 19, 1);
      send_mvm_upgrades_done(2);
      break;
    case 2:
      send_mvm_upgrade(1, 19, -1);
      send_mvm_upgrade(1, 19, 1);
      send_mvm_command(new KeyValues("MVM_Respec"));
      send_mvm_upgrades_done(-1);
      break;
    case 3:
    default:
      send_mvm_upgrade(1, 19, 1);
      send_mvm_upgrade(1, 19, 1);
      send_mvm_upgrade(1, 19, -1);
      send_mvm_upgrade(1, 19, -1);
      send_mvm_upgrades_done(0);
      break;
  }

  mvm_buybot_step_ = (mvm_buybot_step_ % 3) + 1;
  next_mvm_buybot_time_ = global_vars->realtime + mvm_buybot_interval;
}

void automation_controller::run_ping_reducer()
{
  if (convar_system == nullptr || global_vars == nullptr)
  {
    return;
  }

  static Convar* cl_cmdrate = convar_system->find_var("cl_cmdrate");
  if (cl_cmdrate == nullptr)
  {
    return;
  }

  if (!ping_reducer_saved_cmd_rate_)
  {
    original_cmd_rate_ = cl_cmdrate->get_int();
    ping_reducer_saved_cmd_rate_ = true;
  }

  if (!config.misc.exploits.ping_reducer)
  {
    if (ping_reducer_saved_cmd_rate_ && cl_cmdrate->get_int() != original_cmd_rate_)
    {
      cl_cmdrate->set_int(original_cmd_rate_);
    }
    return;
  }

  if (global_vars->realtime < next_ping_reduce_time_)
  {
    return;
  }

  next_ping_reduce_time_ = global_vars->realtime + ping_reduce_interval;
  const int ping = get_local_ping();
  if (ping <= 0)
  {
    return;
  }

  const int target_ping = std::clamp(config.misc.exploits.ping_target, 1, 100);
  const int wanted_cmd_rate = ping > target_ping ? -1 : original_cmd_rate_;
  if (cl_cmdrate->get_int() != wanted_cmd_rate)
  {
    cl_cmdrate->set_int(wanted_cmd_rate);
  }
}

void automation_controller::run_queueing()
{
  static float next_debug_log_time = 0.0f;
  const bool emit_debug_log = should_emit_queue_debug(next_debug_log_time);
  static bool queued_from_player_threshold = false;
  static bool leave_for_requeue_requested = false;
  static bool cancel_queue_requested = false;
  static bool queued_once = false;
  static bool was_disconnected = false;

  const bool in_game = engine->is_in_game();
  const bool is_connected = engine->is_connected();
  const bool is_loading = engine->is_drawing_loading_image();
  const bool has_net_channel = client_state != nullptr && client_state->m_NetChannel != nullptr;
  const bool still_attached_to_server = in_game || is_connected || has_net_channel;

  const bool queueing_enabled = config.misc.automation.auto_queue || config.misc.automation.auto_requeue;
  if (!queueing_enabled)
  {
    queued_once = false;
    queued_from_player_threshold = false;
    leave_for_requeue_requested = false;
    cancel_queue_requested = false;
    was_disconnected = false;
    queue_loading_start_time_ = 0.0f;
    return;
  }

  if (was_in_game_ && !in_game && !is_loading && config.misc.automation.requeue_on_kick)
  {
    was_disconnected = true;
    next_queue_action_time_ = 0.0f;
    if (emit_debug_log)
    {
      log_queue_debug("requeue_on_kick triggered, resetting next action time\n");
    }
  }
  else if (was_in_game_ && !in_game && !is_loading)
  {
    was_disconnected = true;
  }

  was_in_game_ = in_game;

  auto queue_mode = static_cast<unsigned int>(config.misc.automation.auto_queue_mode);

  auto* party_client = get_party_client();
  if (party_client == nullptr)
  {
    if (emit_debug_log)
    {
      log_queue_debug("skip party_client=null api_ready=%d\n", party_client_api_ready() ? 1 : 0);
    }
    return;
  }

  bool in_match_queue = g_party_client_api.is_in_queue_for_match_group != nullptr &&
                        g_party_client_api.is_in_queue_for_match_group(party_client, queue_mode);
  const bool in_standby = is_in_standby_queue(party_client);
  if (!in_match_queue)
  {
    cancel_queue_requested = false;
  }

  if (is_loading)
  {
    if (queue_loading_start_time_ <= 0.0f)
    {
      queue_loading_start_time_ = global_vars->realtime;
    }

    const int loading_human_players = in_game ? count_requeue_players() : 0;
    const bool loading_player_threshold_requeue = in_game && should_trigger_player_threshold_requeue(loading_human_players);
    const bool loading_ipc_bot_threshold_requeue = in_game && should_trigger_ipc_bot_threshold_requeue();
    const bool loading_requeue_conditions_met = loading_player_threshold_requeue || loading_ipc_bot_threshold_requeue;

    if (in_match_queue && !cancel_queue_requested && !loading_requeue_conditions_met)
    {
      log_queue_debug(
        "loading screen active and rq_if requirements not met, leaving queued match group %u humans=%d ipc_excess=%d\n",
        queue_mode,
        loading_human_players,
        loading_ipc_bot_threshold_requeue ? 1 : 0);
      if (cancel_match_queue(party_client, queue_mode))
      {
        cancel_queue_requested = true;
        in_match_queue = false;
        queued_from_player_threshold = false;
        next_queue_action_time_ = global_vars->realtime + auto_queue_interval;
      }
      else if (emit_debug_log)
      {
        log_queue_debug("loading screen active but request_leave_for_match is null\n");
      }
    }

    if (config.misc.automation.auto_requeue &&
        global_vars->realtime - queue_loading_start_time_ >= auto_queue_loading_timeout)
    {
      const bool used_abandon = abandon_current_match();
      log_queue_debug("loading timeout hit, abandon=%d\n", used_abandon ? 1 : 0);
      if (!used_abandon)
      {
        engine->client_cmd_unrestricted("disconnect");
      }
      queue_loading_start_time_ = global_vars->realtime;
      was_disconnected = true;
      next_queue_action_time_ = 0.0f;
    }

    if (emit_debug_log)
    {
      log_queue_debug(
        "skip loading attached=%d in_game=%d connected=%d net_channel=%d in_match_queue=%d rq_if=%d next_action=%.2f realtime=%.2f\n",
        still_attached_to_server ? 1 : 0,
        in_game ? 1 : 0,
        is_connected ? 1 : 0,
        has_net_channel ? 1 : 0,
        in_match_queue ? 1 : 0,
        loading_requeue_conditions_met ? 1 : 0,
        next_queue_action_time_,
        global_vars->realtime);
    }
    return;
  }

  queue_loading_start_time_ = 0.0f;

  const int human_players = in_game ? count_requeue_players() : 0;
  const bool player_threshold_requeue = in_game && should_trigger_player_threshold_requeue(human_players);
  const bool ipc_bot_threshold_requeue = in_game && should_trigger_ipc_bot_threshold_requeue();
  const bool threshold_requeue = player_threshold_requeue || ipc_bot_threshold_requeue;

  if (threshold_requeue)
  {
    if (config.misc.automation.requeue_action == Misc::Automation::requeue_action_mode::LEAVE_AND_REQUEUE)
    {
      if (!leave_for_requeue_requested)
      {
        const bool used_abandon = abandon_current_match();
        log_queue_debug(
          "rq_if hit (%d humans, ipc_excess=%d), leave_and_requeue lte=%d gte=%d ipc_gt=%d abandon=%d\n",
          human_players,
          ipc_bot_threshold_requeue ? 1 : 0,
          config.misc.automation.rq_if_players_lte,
          config.misc.automation.rq_if_players_gte,
          config.misc.automation.rq_if_ipc_bots_gt,
          used_abandon ? 1 : 0);
        if (!used_abandon)
        {
          log_queue_debug("abandon_current_match unresolved, falling back to disconnect\n");
          engine->client_cmd_unrestricted("disconnect");
        }
        leave_for_requeue_requested = true;
        was_disconnected = true;
        queued_from_player_threshold = false;
        next_queue_action_time_ = 0.0f;
      }
      return;
    }
  }
  else
  {
    leave_for_requeue_requested = false;
  }

  if (emit_debug_log)
  {
    log_queue_debug(
      "state party_client=%p queue_mode=%u in_match_queue=%d in_standby=%d next_action=%.2f realtime=%.2f auto_queue=%d auto_requeue=%d human_players=%d rq_threshold=%d action=%d attached=%d queued_once=%d disconnected=%d\n",
      party_client,
      queue_mode,
      in_match_queue ? 1 : 0,
      in_standby ? 1 : 0,
      next_queue_action_time_,
      global_vars->realtime,
      config.misc.automation.auto_queue ? 1 : 0,
      config.misc.automation.auto_requeue ? 1 : 0,
      human_players,
      threshold_requeue ? 1 : 0,
      static_cast<int>(config.misc.automation.requeue_action),
      still_attached_to_server ? 1 : 0,
      queued_once ? 1 : 0,
      was_disconnected ? 1 : 0);
  }

  if (in_match_queue && in_game && !threshold_requeue && !cancel_queue_requested)
  {
    if (cancel_match_queue(party_client, queue_mode))
    {
      log_queue_debug("rq_if requirements not met, leaving queued match group %u\n", queue_mode);
      cancel_queue_requested = true;
      next_queue_action_time_ = global_vars->realtime + auto_queue_interval;
    }
    else if (emit_debug_log)
    {
      log_queue_debug("rq_if requirements not met but request_leave_for_match is null\n");
    }
    queued_from_player_threshold = false;
    return;
  }

  if (threshold_requeue)
  {
    if (!in_match_queue && global_vars->realtime >= next_queue_action_time_)
    {
      log_queue_debug(
        "rq_if hit (%d humans, ipc_excess=%d), requesting rolling queue for match group %u\n",
        human_players,
        ipc_bot_threshold_requeue ? 1 : 0,
        queue_mode);
      if (request_match_queue(party_client, queue_mode))
      {
        queued_from_player_threshold = true;
        cancel_queue_requested = false;
        queued_once = true;
        next_queue_action_time_ = global_vars->realtime + auto_queue_interval;
      }
    }
    return;
  }

  if (still_attached_to_server)
  {
    if (emit_debug_log)
    {
      log_queue_debug("skip still_attached_to_server after in-game requeue handling\n");
    }
    return;
  }

  if (in_match_queue || global_vars->realtime < next_queue_action_time_)
  {
    if (emit_debug_log)
    {
      log_queue_debug(
        "skip queued_or_waiting in_match_queue=%d waiting=%d\n",
        in_match_queue ? 1 : 0,
        global_vars->realtime < next_queue_action_time_ ? 1 : 0);
    }
    return;
  }

  if (in_standby)
  {
    if (g_party_client_api.request_leave_standby != nullptr)
    {
      log_queue_debug("leaving standby queue\n");
      g_party_client_api.request_leave_standby(party_client);
    }
    else
    {
      log_queue_debug("standby detected but request_leave_standby is null\n");
    }
    next_queue_action_time_ = global_vars->realtime + auto_queue_interval;
    return;
  }

  const bool should_queue = config.misc.automation.auto_queue ||
                            (config.misc.automation.auto_requeue && was_disconnected);
  if (!should_queue)
  {
    if (emit_debug_log)
    {
      log_queue_debug("skip should_queue=false auto_queue=%d auto_requeue=%d disconnected=%d\n",
        config.misc.automation.auto_queue ? 1 : 0,
        config.misc.automation.auto_requeue ? 1 : 0,
        was_disconnected ? 1 : 0);
    }
    return;
  }

  log_queue_debug("requesting queue for match group %u\n", queue_mode);
  if (request_match_queue(party_client, queue_mode))
  {
    queued_from_player_threshold = false;
    cancel_queue_requested = false;
    queued_once = true;
    was_disconnected = false;
    next_queue_action_time_ = global_vars->realtime + auto_queue_interval;
  }
}

} // namespace automation
