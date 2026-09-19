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
#include "core/shared/modules.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <system_error>
#include <fstream>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include "core/shared/sigs.hpp"
#include "core/hooks/region_selector.hpp"
#include "core/detach.hpp"
#include "core/print.hpp"
#include "core/math/math.hpp"
#include "core/logger.hpp"
#include "core/ipc/ipc_client.hpp"
#include "core/player_manager.hpp"
#include "core/player_resource.hpp"
#include "core/entity_cache.hpp"
#include "features/menu/config.hpp"
#include "features/automation/autoitem/autoitem.hpp"
#include "features/automation/mvm_queue/mvm_queue.hpp"
#include "features/automation/nographics/nographics.hpp"
#include "features/automation/profile_stalker_api.hpp"
#include "features/automation/navbot/navbot_mesh.hpp"
#include "features/automation/navbot/navbot_controller.hpp"
#include "games/tf2/sdk/netvars.hpp"
#include "games/tf2/sdk/bitbuf.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/entities/entity.hpp"
#include "games/tf2/sdk/entities/weapon.hpp"
#include "games/tf2/sdk/entities/building.hpp"
#include "games/tf2/sdk/entities/team_objective_resource.hpp"
#include "games/tf2/sdk/materials/keyvalues.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/steam_runtime.hpp"
#include "games/tf2/sdk/interfaces/game_event_manager.hpp"
#include "libsigscan/libsigscan.h"

namespace automation
{

namespace
{

constexpr float auto_class_interval = 1.0f;
constexpr float auto_queue_interval = 5.0f;
constexpr float auto_queue_loading_timeout = 120.0f;
constexpr float auto_requeue_loading_timeout = 360.0f;
constexpr float boost_queue_wait_seconds = 60.0f;
constexpr float noisemaker_interval = 0.2f;
constexpr float voice_command_spam_interval = 6.5f;
constexpr int micspam_min_interval_seconds = 1;
constexpr int micspam_max_interval_seconds = 600;
constexpr const char* micspam_source_directory = "/opt/puphook/micspam";
constexpr const char* micspam_voice_input_path = "voice_input.wav";

namespace
{

struct micspam_prepare_state
{
  std::mutex mutex{};
  std::thread worker{};
  std::atomic_bool job_running{false};
  std::atomic_bool result_ready{false};
  std::atomic_bool result_value{false};

  ~micspam_prepare_state()
  {
    if (worker.joinable())
    {
      worker.join();
    }
  }
};

micspam_prepare_state& micspam_prepare()
{
  static micspam_prepare_state state{};
  return state;
}

bool poll_micspam_prepare_result()
{
  auto& state = micspam_prepare();
  if (!state.result_ready.exchange(false, std::memory_order_acq_rel))
  {
    return false;
  }

  if (state.worker.joinable())
  {
    state.worker.join();
  }
  return state.result_value.load(std::memory_order_acquire);
}

}
constexpr float mvm_command_interval = 1.0f;
constexpr float mvm_buybot_interval = 0.2f;
constexpr float ping_reduce_interval = 0.1f;
constexpr int casual_match_group_default = 7;
constexpr float anti_afk_trigger_padding = 10.0f;
constexpr int text_msg_user_message_type = 5;
constexpr int say_text2_user_message_type = 4;
constexpr int gr_state_preround = 3;
constexpr int gr_state_rnd_running = 4;
constexpr int gr_state_between_rounds = 10;
constexpr int call_vote_failed_user_message_type = 45;
constexpr int vote_start_user_message_type = 46;
constexpr int vote_pass_user_message_type = 47;
constexpr int vote_failed_user_message_type = 48;
constexpr int vote_fail_reason_rate_exceeded = 2;
constexpr int vote_fail_reason_on_cooldown = 8;
constexpr int report_reason_cheating = 1;
constexpr float party_client_scan_interval = 0.25f;
constexpr const char* auto_balance_pending_token = "#TF_Autobalance_TeamChangePending";
constexpr float autotaunt_step_interval = 0.12f;
constexpr int max_chat_command_length = 220;
constexpr const char* startup_sound_list_name = "startup_sounds.txt";
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
using promote_to_leader_fn = bool (*)(void*, std::uint64_t);
using report_player_account_fn = bool (*)(std::uint64_t, int);
using party_get_num_members_fn = int (*)(void*);
using party_get_num_online_members_fn = int (*)(void*);
using party_get_member_steamid_fn = std::uint64_t (*)(void*, int);
using party_in_party_not_leader_fn = bool (*)(void*);
using party_send_party_chat_fn = void (*)(void*, const char*);
using party_kick_player_fn = bool (*)(void*, std::uint64_t);

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
  promote_to_leader_fn promote_to_leader = nullptr;
  party_get_num_members_fn get_num_members = nullptr;
  party_get_num_online_members_fn get_num_online_members = nullptr;
  party_get_member_steamid_fn get_member_steamid = nullptr;
  party_in_party_not_leader_fn in_party_not_leader = nullptr;
  party_send_party_chat_fn send_party_chat = nullptr;
  party_kick_player_fn kick_player = nullptr;
  int scan_step = 0;
  float next_scan_time = 0.0f;
};

party_client_api g_party_client_api{};
report_player_account_fn g_report_player_account = nullptr;
bool g_report_player_account_initialized = false;

struct vote_message_reader
{
  const bf_read* message = nullptr;
  int bit = 0;
  bool ok = true;

  int read(int count)
  {
    if (message == nullptr || !message->is_valid() || count < 0 ||
        bit + count > message->data_bits)
    {
      ok = false;
      return 0;
    }
    std::uint32_t value = 0;
    for (int i = 0; i < count; ++i)
    {
      value |= static_cast<std::uint32_t>(
          (message->data[(bit + i) >> 3] >> ((bit + i) & 7)) & 1u) << i;
    }
    bit += count;
    return value;
  }

  std::string read_string()
  {
    std::string result;
    while (ok)
    {
      const int c = read(8);
      if (!ok || c == 0)
      {
        break;
      }
      result.push_back(static_cast<char>(c));
    }
    return result;
  }
};

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

const std::array<std::string_view, 5> builtin_chatspam_puphook = {
  "Puphook on Linux",
  "GNU/Linux is the best OS",
  "Open source TF2 tooling is fun",
  "puphook.club",
  "Free software, free frags"
};

const std::array<std::string_view, 3> builtin_chatspam_lmaobox = {
  "GET GOOD, GET LMAOBOX",
  "WWW.LMAOBOX.NET",
  "LMAOBOX - WAY TO THE TOP"
};

const std::array<std::string_view, 6> builtin_killsay_puphook = {
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

  return puphook::core::root_directory() / "assets" / "sound" / sound_name;
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
    puphook::core::root_directory() / requested_path,
    puphook::core::config_directory() / requested_path,
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

  return puphook::core::root_directory() / requested_path;
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
  for (const auto& entry : entity_cache_players())
  {
    auto* player = entry.player;
    if (player == nullptr || player == localplayer || !entry.alive || entry.dormant)
    {
      continue;
    }

    if (entry.team == localplayer->get_team())
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
#ifdef PUPHOOK_DEBUG_AUTO_QUEUE

  va_list args{};
  va_start(args, fmt);
  print("[auto_queue] ");
  puphook::core::vlog_raw(fmt, args);
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
  if (puphook::core::is_detach_pending())
  {
    return nullptr;
  }

  if (!region_selector_request_queue_for_match_available())
  {
    return nullptr;
  }

  return request_queue_for_match_with_region_selector;
}

bool party_client_scan_ready()
{
  if (global_vars == nullptr)
  {
    return true;
  }

  if (global_vars->realtime < g_party_client_api.next_scan_time)
  {
    return false;
  }

  g_party_client_api.next_scan_time = global_vars->realtime + party_client_scan_interval;
  return true;
}

void initialize_party_client_api()
{
  if (g_party_client_api.initialized)
  {
    return;
  }

  if (!party_client_scan_ready())
  {
    return;
  }

  switch (g_party_client_api.scan_step)
  {
    case 0:
    {
      void* get_party_client_match = sigscan_module(puphook::core::modules::tf_client, sigs::get_party_client);
      if (get_party_client_match != nullptr)
      {
        g_party_client_api.get_party_client = reinterpret_cast<get_party_client_fn>(
          reinterpret_cast<std::uintptr_t>(get_party_client_match) + sigs::get_party_client_offset);
      }
      ++g_party_client_api.scan_step;
      return;
    }
    case 1:
      g_party_client_api.get_matchmaking_client =
        reinterpret_cast<get_matchmaking_client_fn>(sigscan_module(puphook::core::modules::tf_client, sigs::get_matchmaking_client));
      ++g_party_client_api.scan_step;
      return;
    case 2:
      g_party_client_api.load_saved_casual_criteria =
        reinterpret_cast<load_saved_casual_criteria_fn>(sigscan_module(puphook::core::modules::tf_client, sigs::load_saved_casual_criteria));
      ++g_party_client_api.scan_step;
      return;
    case 3:
      g_party_client_api.is_in_queue_for_match_group =
        reinterpret_cast<is_in_queue_for_match_group_fn>(sigscan_module(puphook::core::modules::tf_client, sigs::is_in_queue_for_match_group));
      ++g_party_client_api.scan_step;
      return;
    case 4:
      g_party_client_api.is_in_standby_queue =
        reinterpret_cast<is_in_standby_queue_fn>(sigscan_module(puphook::core::modules::tf_client, sigs::is_in_standby_queue));
      ++g_party_client_api.scan_step;
      return;
    case 5:
      g_party_client_api.abandon_current_match =
        reinterpret_cast<abandon_current_match_fn>(sigscan_module(puphook::core::modules::tf_client, sigs::abandon_current_match));
      ++g_party_client_api.scan_step;
      return;
    case 6:
      g_party_client_api.request_queue_for_match = get_shared_request_queue_for_match();
      ++g_party_client_api.scan_step;
      return;
    case 7:
      g_party_client_api.request_leave_for_match =
        reinterpret_cast<request_leave_for_match_fn>(sigscan_module(puphook::core::modules::tf_client, sigs::request_leave_for_match));
      ++g_party_client_api.scan_step;
      return;
    case 8:
      g_party_client_api.request_queue_for_standby =
        reinterpret_cast<request_queue_for_standby_fn>(sigscan_module(puphook::core::modules::tf_client, sigs::request_queue_for_standby));
      ++g_party_client_api.scan_step;
      return;
    case 9:
      g_party_client_api.request_leave_standby =
        reinterpret_cast<request_leave_standby_fn>(sigscan_module(puphook::core::modules::tf_client, sigs::request_leave_standby));
      ++g_party_client_api.scan_step;
      return;
    case 10:
      g_party_client_api.promote_to_leader =
        reinterpret_cast<promote_to_leader_fn>(sigscan_module(puphook::core::modules::tf_client, sigs::promote_to_leader));
      ++g_party_client_api.scan_step;
      return;
    case 11:
      g_party_client_api.get_num_members =
        reinterpret_cast<party_get_num_members_fn>(sigscan_module(puphook::core::modules::tf_client, sigs::party_client_get_num_members));
      ++g_party_client_api.scan_step;
      return;
    case 12:
      g_party_client_api.get_num_online_members =
        reinterpret_cast<party_get_num_online_members_fn>(sigscan_module(puphook::core::modules::tf_client, sigs::party_client_get_num_online_members));
      ++g_party_client_api.scan_step;
      return;
    case 13:
      g_party_client_api.get_member_steamid =
        reinterpret_cast<party_get_member_steamid_fn>(sigscan_module(puphook::core::modules::tf_client, sigs::party_client_get_member_steamid));
      ++g_party_client_api.scan_step;
      return;
    case 14:
      g_party_client_api.in_party_not_leader =
        reinterpret_cast<party_in_party_not_leader_fn>(sigscan_module(puphook::core::modules::tf_client, sigs::party_client_in_party_not_leader));
      ++g_party_client_api.scan_step;
      return;
    case 15:
      g_party_client_api.send_party_chat =
        reinterpret_cast<party_send_party_chat_fn>(sigscan_module(puphook::core::modules::tf_client, sigs::party_client_send_party_chat));
      ++g_party_client_api.scan_step;
      return;
    case 16:
      g_party_client_api.kick_player =
        reinterpret_cast<party_kick_player_fn>(sigscan_module(puphook::core::modules::tf_client, sigs::party_client_kick_player));
      ++g_party_client_api.scan_step;
      return;
    default:
      break;
  }

  g_party_client_api.initialized = true;
  log_queue_debug(
    "api init get_party_client=%p get_matchmaking_client=%p load_saved_casual_criteria=%p is_in_queue=%p is_in_standby=%p abandon_current_match=%p request_queue=%p request_leave=%p request_queue_standby=%p request_leave_standby=%p promote_to_leader=%p num_members=%p online_members=%p member_steamid=%p not_leader=%p party_chat=%p kick=%p\n",
    reinterpret_cast<void*>(g_party_client_api.get_party_client),
    reinterpret_cast<void*>(g_party_client_api.get_matchmaking_client),
    reinterpret_cast<void*>(g_party_client_api.load_saved_casual_criteria),
    reinterpret_cast<void*>(g_party_client_api.is_in_queue_for_match_group),
    reinterpret_cast<void*>(g_party_client_api.is_in_standby_queue),
    reinterpret_cast<void*>(g_party_client_api.abandon_current_match),
    reinterpret_cast<void*>(g_party_client_api.request_queue_for_match),
    reinterpret_cast<void*>(g_party_client_api.request_leave_for_match),
    reinterpret_cast<void*>(g_party_client_api.request_queue_for_standby),
    reinterpret_cast<void*>(g_party_client_api.request_leave_standby),
    reinterpret_cast<void*>(g_party_client_api.promote_to_leader),
    reinterpret_cast<void*>(g_party_client_api.get_num_members),
    reinterpret_cast<void*>(g_party_client_api.get_num_online_members),
    reinterpret_cast<void*>(g_party_client_api.get_member_steamid),
    reinterpret_cast<void*>(g_party_client_api.in_party_not_leader),
    reinterpret_cast<void*>(g_party_client_api.send_party_chat),
    reinterpret_cast<void*>(g_party_client_api.kick_player));
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

bool request_match_queue(void* party_client, unsigned int queue_mode)
{
  initialize_party_client_api();
  if (puphook::core::is_detach_pending() ||
      party_client == nullptr ||
      g_party_client_api.request_queue_for_match == nullptr)
  {
    return false;
  }

  if (g_party_client_api.load_saved_casual_criteria != nullptr)
  {
    g_party_client_api.load_saved_casual_criteria(party_client);
  }

  g_party_client_api.request_queue_for_match(party_client, queue_mode);
  return true;
}

bool cancel_match_queue(void* party_client, unsigned int queue_mode)
{
  initialize_party_client_api();
  if (puphook::core::is_detach_pending() ||
      party_client == nullptr ||
      g_party_client_api.request_leave_for_match == nullptr)
  {
    return false;
  }

  g_party_client_api.request_leave_for_match(party_client, queue_mode);
  return true;
}

bool cancel_standby_queue(void* party_client)
{
  initialize_party_client_api();
  if (party_client == nullptr || g_party_client_api.request_leave_standby == nullptr)
  {
    return false;
  }

  g_party_client_api.request_leave_standby(party_client);
  return true;
}

bool cancel_active_queues(void* party_client, unsigned int queue_mode, bool& in_match_queue, bool& in_standby)
{
  bool cancelled = false;

  if (in_match_queue && cancel_match_queue(party_client, queue_mode))
  {
    in_match_queue = false;
    cancelled = true;
  }

  if (in_standby && cancel_standby_queue(party_client))
  {
    in_standby = false;
    cancelled = true;
  }

  return cancelled;
}

Entity* get_player_resource_entity()
{
  return puphook::core::player_resource::get_player_resource_entity();
}

template <typename value_type>
value_type read_player_resource_value(Entity* player_resource, int array_offset, int player_index)
{
  return puphook::core::player_resource::read_value<value_type>(player_resource, array_offset, player_index);
}

int get_local_ping()
{
  if (engine == nullptr)
  {
    return 0;
  }

  static tf2_netvars::lazy_offset ping_offset{"DT_TFPlayerResource", { "baseclass", "m_iPing" }};
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
  const int max_clients = puphook::core::player_resource::max_client_index();
  for (int index = 1; index <= max_clients; ++index)
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
    if (account_id != 0 && pup_ipc::client::is_local_ipc_friend(account_id))
    {
      continue;
    }

    if (config.misc.automation.rq_ignore_friends &&
        account_id != 0 &&
        (player->is_friend() || player->is_ignored() || puphook::core::players::is_friendly(account_id)))
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
  const bool hit_lte = players_lte > 0 && human_players < players_lte;
  const bool hit_gte = players_gte > 0 && human_players > players_gte;
  return hit_lte || hit_gte;
}

bool should_trigger_ipc_bot_threshold_requeue()
{
  return pup_ipc::client::is_excess_ipc_bot_on_current_server(config.misc.automation.rq_if_ipc_bots_gt);
}

bool should_trigger_no_navmesh_requeue()
{
  static std::string no_navmesh_settle_map_watch{};
  static float no_navmesh_missing_since_realtime = -1.0f;

  const auto reset_no_navmesh_settle_watch = [&]() {
    no_navmesh_settle_map_watch.clear();
    no_navmesh_missing_since_realtime = -1.0f;
  };

  if (!config.misc.automation.rq_if_no_navmesh)
  {
    reset_no_navmesh_settle_watch();
    return false;
  }

  if (engine == nullptr || !engine->is_in_game() || global_vars == nullptr)
  {
    reset_no_navmesh_settle_watch();
    return false;
  }

  if (navbot::navmesh_resolves_for_current_map())
  {
    reset_no_navmesh_settle_watch();
    return false;
  }

  constexpr float rq_if_no_navmesh_settle_seconds = 5.0f;
  const char* raw_level = engine->get_level_name();
  const std::string map_watch_key = raw_level != nullptr ? std::string(raw_level) : std::string{};

  if (map_watch_key != no_navmesh_settle_map_watch)
  {
    no_navmesh_settle_map_watch = map_watch_key;
    no_navmesh_missing_since_realtime = global_vars->realtime;
    return false;
  }

  return (global_vars->realtime - no_navmesh_missing_since_realtime) >= rq_if_no_navmesh_settle_seconds;
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

TeamObjectiveResource* current_mvm_objective_resource()
{
  const auto& resources = entity_cache_entities(class_id::OBJECTIVE_RESOURCE);
  return resources.empty() ? nullptr : reinterpret_cast<TeamObjectiveResource*>(resources.front());
}

bool is_mvm_mann_up_match()
{
  if (entity_list == nullptr)
  {
    return true;
  }

  auto* proxy = entity_list->get_game_rules_proxy();
  if (proxy == nullptr)
  {
    return true;
  }

  static tf2_netvars::lazy_offset match_group_offset{
    "DT_TFGameRulesProxy", {"m_nMatchGroupType"}};
  if (match_group_offset <= 0)
  {
    return true;
  }

  return *reinterpret_cast<const int*>(reinterpret_cast<std::uintptr_t>(proxy) + match_group_offset) == 1;
}

bool final_mvm_wave_completed()
{
  if (!is_mvm_mann_up_match())
  {
    return false;
  }

  auto* objective = current_mvm_objective_resource();
  if (objective == nullptr || !objective->is_mvm_between_waves())
  {
    return false;
  }

  const int wave = objective->get_mvm_wave_count();
  const int max_wave = objective->get_mvm_max_wave_count();
  if (max_wave <= 1)
  {
    return false;
  }

  const int completed_wave = wave > 1 ? wave - 1 : wave;
  return completed_wave == max_wave - 1;
}

Entity* nearest_mvm_upgrade_station(Player* localplayer)
{
  if (localplayer == nullptr)
  {
    return nullptr;
  }

  Entity* best = nullptr;
  auto best_distance = std::numeric_limits<float>::max();
  for (auto* entity : entity_cache_entities(class_id::MVM_UPGRADE_STATION))
  {
    if (entity == nullptr || entity->is_dormant())
    {
      continue;
    }
    const auto distance = distance_squared_2d(localplayer->get_origin(), entity->get_origin());
    if (distance < best_distance)
    {
      best_distance = distance;
      best = entity;
    }
  }
  return best;
}

void move_towards_mvm_station(Player* localplayer, user_cmd* user_cmd, const Vec3& target)
{
  if (localplayer == nullptr || user_cmd == nullptr)
  {
    return;
  }

  const auto delta = target - localplayer->get_origin();
  const auto yaw = std::atan2(delta.y, delta.x) * radpi;
  const auto yaw_delta = std::remainder(yaw - user_cmd->view_angles.y, 360.0f) * pideg;
  user_cmd->forwardmove = std::cos(yaw_delta) * 450.0f;
  user_cmd->sidemove = -std::sin(yaw_delta) * 450.0f;
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

constexpr std::array<std::uint8_t, 59> mvm_upgrade_levels{
  4, 4, 4, 4, 4, 4, 3, 3, 6, 4,
  3, 4, 1, 3, 1, 1, 1, 4, 4, 4,
  3, 3, 3, 1, 4, 1, 1, 4, 3, 1,
  4, 4, 2, 4, 1, 3, 2, 3, 2, 3,
  3, 4, 1, 4, 3, 4, 1, 4, 4, 2,
  3, 3, 3, 3, 3, 3, 5, 5, 3
};

int mvm_upgrade_max_levels(int upgrade)
{
  if (upgrade < 0 || static_cast<std::size_t>(upgrade) >= mvm_upgrade_levels.size())
  {
    return 4;
  }
  return mvm_upgrade_levels[static_cast<std::size_t>(upgrade)];
}

struct mvm_upgrade_step
{
  int upgrade;
  int slot;
};

constexpr int mvm_slot_character = -1;
constexpr int mvm_slot_primary = 0;
constexpr int mvm_slot_secondary = 1;
constexpr int mvm_slot_melee = 2;
constexpr int mvm_slot_sapper = 3;
constexpr int mvm_slot_buildings = 4;

constexpr mvm_upgrade_step mvm_sniper_plan[] = {
  {17, mvm_slot_primary}, {35, mvm_slot_primary}, {2, mvm_slot_primary}, {5, mvm_slot_primary},
  {6, mvm_slot_primary}, {12, mvm_slot_primary}, {13, mvm_slot_primary}, {40, mvm_slot_primary},
  {41, mvm_slot_primary}, {52, mvm_slot_character}, {53, mvm_slot_character}, {54, mvm_slot_character},
  {51, mvm_slot_character}, {56, mvm_slot_character}
};
constexpr mvm_upgrade_step mvm_pyro_plan[] = {
  {52, mvm_slot_character}, {53, mvm_slot_character}, {54, mvm_slot_character}, {51, mvm_slot_character},
  {56, mvm_slot_character}, {30, mvm_slot_primary}, {31, mvm_slot_primary}, {33, mvm_slot_primary},
  {0, mvm_slot_primary}, {2, mvm_slot_primary}, {5, mvm_slot_primary}, {6, mvm_slot_primary}
};
constexpr mvm_upgrade_step mvm_heavy_plan[] = {
  {52, mvm_slot_character}, {53, mvm_slot_character}, {54, mvm_slot_character}, {51, mvm_slot_character},
  {56, mvm_slot_character}, {0, mvm_slot_primary}, {2, mvm_slot_primary}, {5, mvm_slot_primary},
  {6, mvm_slot_primary}, {11, mvm_slot_primary}, {12, mvm_slot_primary}, {13, mvm_slot_primary},
  {41, mvm_slot_primary}
};
constexpr mvm_upgrade_step mvm_scout_plan[] = {
  {52, mvm_slot_character}, {53, mvm_slot_character}, {54, mvm_slot_character}, {51, mvm_slot_character},
  {55, mvm_slot_character}, {56, mvm_slot_character}, {46, mvm_slot_secondary}, {0, mvm_slot_primary},
  {2, mvm_slot_primary}, {5, mvm_slot_primary}, {6, mvm_slot_primary}, {12, mvm_slot_primary}
};
constexpr mvm_upgrade_step mvm_engineer_plan[] = {
  {22, mvm_slot_buildings}, {20, mvm_slot_buildings}, {52, mvm_slot_character}, {53, mvm_slot_character},
  {54, mvm_slot_character}, {51, mvm_slot_character}, {57, mvm_slot_character}, {56, mvm_slot_character},
  {4, mvm_slot_melee}, {0, mvm_slot_melee}
};
constexpr mvm_upgrade_step mvm_soldier_plan[] = {
  {52, mvm_slot_character}, {0, mvm_slot_primary}, {53, mvm_slot_character}, {2, mvm_slot_primary},
  {54, mvm_slot_character}, {5, mvm_slot_primary}, {51, mvm_slot_character}, {6, mvm_slot_primary},
  {56, mvm_slot_character}, {47, mvm_slot_primary}, {33, mvm_slot_primary}
};
constexpr mvm_upgrade_step mvm_demoman_plan[] = {
  {52, mvm_slot_character}, {0, mvm_slot_primary}, {53, mvm_slot_character}, {2, mvm_slot_primary},
  {54, mvm_slot_character}, {5, mvm_slot_primary}, {51, mvm_slot_character}, {8, mvm_slot_primary},
  {56, mvm_slot_character}, {27, mvm_slot_primary}, {33, mvm_slot_primary}
};
constexpr mvm_upgrade_step mvm_spy_plan[] = {
  {37, mvm_slot_sapper}, {52, mvm_slot_character}, {53, mvm_slot_character}, {54, mvm_slot_character},
  {51, mvm_slot_character}, {4, mvm_slot_melee}, {0, mvm_slot_melee}
};

const mvm_upgrade_step* buybot_plan_for_class(tf_class player_class, int& count)
{
  switch (player_class)
  {
    case tf_class::SNIPER:
      count = static_cast<int>(std::size(mvm_sniper_plan));
      return mvm_sniper_plan;
    case tf_class::PYRO:
      count = static_cast<int>(std::size(mvm_pyro_plan));
      return mvm_pyro_plan;
    case tf_class::HEAVYWEAPONS:
      count = static_cast<int>(std::size(mvm_heavy_plan));
      return mvm_heavy_plan;
    case tf_class::SCOUT:
      count = static_cast<int>(std::size(mvm_scout_plan));
      return mvm_scout_plan;
    case tf_class::ENGINEER:
      count = static_cast<int>(std::size(mvm_engineer_plan));
      return mvm_engineer_plan;
    case tf_class::SOLDIER:
      count = static_cast<int>(std::size(mvm_soldier_plan));
      return mvm_soldier_plan;
    case tf_class::DEMOMAN:
      count = static_cast<int>(std::size(mvm_demoman_plan));
      return mvm_demoman_plan;
    case tf_class::SPY:
      count = static_cast<int>(std::size(mvm_spy_plan));
      return mvm_spy_plan;
    default:
      count = 0;
      return nullptr;
  }
}

bool has_vaccinator(Player* localplayer)
{
  if (localplayer == nullptr)
  {
    return false;
  }
  for (int index = 0; index < Player::max_weapon_count; ++index)
  {
    auto* weapon = localplayer->get_weapon_at(index);
    if (weapon != nullptr && weapon->get_def_id() == Medic_s_TheVaccinator)
    {
      return true;
    }
  }
  return false;
}

bool parse_say_text2(const bf_read* message_data, int& entity_index, std::string& text)
{
  if (message_data == nullptr || !message_data->is_valid() || message_data->data == nullptr
    || message_data->data_bytes < 4)
  {
    return false;
  }

  const auto* cursor = message_data->data;
  const auto* end = cursor + message_data->data_bytes;
  entity_index = *cursor++;
  if (cursor >= end)
  {
    return false;
  }
  ++cursor;

  auto skip_string = [&]() {
    while (cursor < end && *cursor != 0)
    {
      ++cursor;
    }
    if (cursor < end)
    {
      ++cursor;
    }
  };

  skip_string();
  skip_string();
  if (cursor >= end)
  {
    return false;
  }

  const auto* start = reinterpret_cast<const char*>(cursor);
  skip_string();
  text.assign(start, strnlen(start, static_cast<std::size_t>(end - reinterpret_cast<const std::uint8_t*>(start))));
  return !text.empty();
}

void initialize_report_player_account()
{
  if (g_report_player_account_initialized)
  {
    return;
  }

  g_report_player_account_initialized = true;
  g_report_player_account = reinterpret_cast<report_player_account_fn>(sigscan_module("client.so", sigs::report_player_account));
#ifdef PUPHOOK_DEBUG_AUTO_REPORT

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

}

automation_controller& controller()
{
  static automation_controller instance{};
  return instance;
}

void shutdown()
{
  {
    auto& state = micspam_prepare();
    std::scoped_lock lock{state.mutex};
    if (state.worker.joinable())
    {
      state.worker.join();
    }
    state.job_running.store(false, std::memory_order_release);
    state.result_ready.store(false, std::memory_order_release);
    state.result_value.store(false, std::memory_order_release);
  }

  g_party_client_api = {};
  g_report_player_account = nullptr;
  g_report_player_account_initialized = false;
}

bool reload_casual_criteria()
{
  initialize_party_client_api();

  if (puphook::core::is_detach_pending() ||
      g_party_client_api.get_party_client == nullptr ||
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

  if (puphook::core::is_detach_pending() ||
      g_party_client_api.get_party_client == nullptr)
  {
    return false;
  }

  auto* party_client = g_party_client_api.get_party_client();
  if (party_client == nullptr)
  {
    return false;
  }

  return request_match_queue(party_client, casual_match_group_default);
}

bool cancel_casual_queue()
{
  initialize_party_client_api();

  if (puphook::core::is_detach_pending() ||
      g_party_client_api.get_party_client == nullptr ||
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

bool abandon_current_match()
{
  initialize_party_client_api();

  if (puphook::core::is_detach_pending() ||
      g_party_client_api.get_matchmaking_client == nullptr ||
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

constexpr std::uint64_t steamid64_individual_base = 0x0110000100000000ULL;

bool party_has_account(void* party_client, std::uint32_t account_id)
{
  if (g_party_client_api.get_num_members == nullptr
    || g_party_client_api.get_member_steamid == nullptr)
  {
    return puphook::core::players::has_role(account_id, puphook::core::players::party_role);
  }

  const int count = g_party_client_api.get_num_members(party_client);
  if (count < 2)
  {
    return false;
  }

  for (int index = 0; index < count; ++index)
  {
    const auto steam_id = g_party_client_api.get_member_steamid(party_client, index);
    if (steam_id != 0 && static_cast<std::uint32_t>(steam_id & 0xffffffffull) == account_id)
    {
      return true;
    }
  }
  return false;
}

bool promote_party_leader(std::uint32_t account_id)
{
  initialize_party_client_api();
  if (account_id == 0
    || g_party_client_api.get_party_client == nullptr
    || g_party_client_api.promote_to_leader == nullptr)
  {
    return false;
  }

  auto* party_client = g_party_client_api.get_party_client();
  if (party_client == nullptr)
  {
    return false;
  }

  auto* party = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(party_client) + 0x30);
  const bool is_leader = *reinterpret_cast<bool*>(reinterpret_cast<std::uintptr_t>(party_client) + 0x40);
  if (party == nullptr || !is_leader || !party_has_account(party_client, account_id))
  {
    return false;
  }

  return g_party_client_api.promote_to_leader(party_client, steamid64_individual_base | account_id);
}

bool autoparty_api_ready()
{
  initialize_party_client_api();
  return g_party_client_api.get_party_client != nullptr &&
         g_party_client_api.get_num_members != nullptr &&
         g_party_client_api.get_num_online_members != nullptr &&
         g_party_client_api.get_member_steamid != nullptr &&
         g_party_client_api.in_party_not_leader != nullptr &&
         g_party_client_api.send_party_chat != nullptr &&
         g_party_client_api.kick_player != nullptr;
}

std::uint32_t local_steam_account_id()
{
  auto* user = steam_runtime::resolve_steam_user();
  if (user == nullptr)
  {
    return 0;
  }
  return static_cast<std::uint32_t>(user->get_steam_id() & 0xffffffffull);
}

void parse_party_hosts(std::string_view text, std::vector<std::uint32_t>& out)
{
  out.clear();
  std::size_t pos = 0;
  while (pos < text.size())
  {
    const auto next = text.find_first_of(",; \t", pos);
    const auto token = text.substr(pos, next == std::string_view::npos ? std::string_view::npos : next - pos);
    if (!token.empty())
    {
      char token_buffer[24]{};
      const auto length = std::min<std::size_t>(token.size(), sizeof(token_buffer) - 1);
      std::memcpy(token_buffer, token.data(), length);
      const auto id = std::strtoul(token_buffer, nullptr, 10);
      if (id != 0)
      {
        out.push_back(static_cast<std::uint32_t>(id));
      }
    }
    if (next == std::string_view::npos)
    {
      break;
    }
    pos = next + 1;
  }
}

void automation_controller::refresh_party_hosts()
{
  const auto& cfg = config.misc.automation;
  if (cfg.autoparty_ipc_mode)
  {
    autoparty_hosts_ = pup_ipc::client::ipc_peer_friend_ids_by_injection_time(cfg.autoparty_ipc_count);
    autoparty_hosts_source_.clear();
    autoparty_from_ipc_ = true;
    return;
  }

  if (autoparty_from_ipc_ || autoparty_hosts_source_ != cfg.autoparty_party_hosts)
  {
    autoparty_from_ipc_ = false;
    autoparty_hosts_source_ = cfg.autoparty_party_hosts;
    parse_party_hosts(autoparty_hosts_source_, autoparty_hosts_);
  }
}

void automation_controller::run_autoparty()
{
  const auto& cfg = config.misc.automation;
  if (!cfg.autoparty ||
      puphook::core::is_detach_pending() ||
      engine == nullptr ||
      global_vars == nullptr)
  {
    return;
  }

  const float now = global_vars->realtime;
  if (now < next_autoparty_time_)
  {
    return;
  }
  next_autoparty_time_ = now + static_cast<float>(std::max(1, cfg.autoparty_run_frequency));

  refresh_party_hosts();
  if (autoparty_hosts_.empty() || !autoparty_api_ready())
  {
    return;
  }

  void* client = g_party_client_api.get_party_client();
  if (client == nullptr)
  {
    return;
  }

  const int max_size = std::clamp(cfg.autoparty_max_party_size, 1, 6);
  const std::uint32_t local_account = local_steam_account_id();
  const int member_count = g_party_client_api.get_num_members(client);
  const int online_count = g_party_client_api.get_num_online_members(client);
  const bool is_host = local_account != 0 &&
    std::find(autoparty_hosts_.begin(), autoparty_hosts_.end(), local_account) != autoparty_hosts_.end();

  const auto lock_party = [&]()
  {
    engine->client_cmd_unrestricted("tf_party_join_request_mode 2");
  };
  const auto unlock_party = [&]()
  {
    engine->client_cmd_unrestricted("tf_party_join_request_mode 0");
  };
  const auto request_join = [&](std::uint32_t account, bool expect_invite)
  {
    char command[96]{};
    std::snprintf(command, sizeof(command), "tf_party_request_join_user %llu%s",
                  static_cast<unsigned long long>(steamid64_individual_base | account),
                  expect_invite ? " 1" : "");
    engine->client_cmd_unrestricted(command);
  };
  const auto invite_user = [&](std::uint32_t account)
  {
    char command[96]{};
    std::snprintf(command, sizeof(command), "tf_party_invite_user %llu",
                  static_cast<unsigned long long>(steamid64_individual_base | account));
    engine->client_cmd_unrestricted(command);
  };
  const auto invite_listed_hosts = [&](const std::vector<std::uint32_t>& members)
  {
    for (const auto host : autoparty_hosts_)
    {
      if (host == 0 || host == local_account ||
          std::find(members.begin(), members.end(), host) != members.end())
      {
        continue;
      }
      invite_user(host);
    }
  };
  const auto leave_party = [&](bool was_leader)
  {
    if (cfg.autoparty_log)
    {
      print("[autoparty] leaving party (%d/%d members offline)\n",
            std::max(0, member_count - online_count), member_count);
    }
    engine->client_cmd_unrestricted("tf_party_leave");
    if (was_leader && cfg.autoparty_auto_unlock)
    {
      unlock_party();
    }
  };

  if (member_count <= 1)
  {
    if (is_host)
    {
      if (cfg.autoparty_auto_unlock)
      {
        unlock_party();
      }
      invite_listed_hosts({});
    }
    else
    {
      for (const auto host : autoparty_hosts_)
      {
        if (host == 0 || host == local_account)
        {
          continue;
        }
        request_join(host, false);
        request_join(host, true);
      }
    }
    return;
  }

  if (!is_host)
  {
    if (cfg.autoparty_auto_lock)
    {
      lock_party();
    }
    if (cfg.autoparty_auto_leave && member_count > online_count)
    {
      leave_party(false);
    }
    return;
  }

  if (g_party_client_api.in_party_not_leader(client))
  {
    if (cfg.autoparty_log)
    {
      print("[autoparty] leaving party: we are a host but not the leader\n");
    }
    engine->client_cmd_unrestricted("tf_party_leave");
    if (cfg.autoparty_auto_unlock)
    {
      unlock_party();
    }
    return;
  }

  if (cfg.autoparty_auto_leave && member_count > online_count)
  {
    leave_party(true);
    return;
  }

  std::vector<std::uint32_t> members;
  members.reserve(static_cast<std::size_t>(member_count));
  for (int i = 0; i < member_count; ++i)
  {
    const auto steam_id = g_party_client_api.get_member_steamid(client, i);
    if (steam_id != 0)
    {
      members.push_back(static_cast<std::uint32_t>(steam_id & 0xffffffffull));
    }
  }

  if (cfg.autoparty_kick_rage)
  {
    for (const auto account : members)
    {
      if (account == local_account ||
          !puphook::core::players::has_role(account, puphook::core::players::cheater_role))
      {
        continue;
      }
      if (cfg.autoparty_log)
      {
        print("[autoparty] kicking cheater %u\n", account);
      }
      if (cfg.autoparty_message_kicks)
      {
        char message[160]{};
        std::snprintf(message, sizeof(message),
                      "Kicking Steam32 ID %u from the party because they are marked as a cheater", account);
        g_party_client_api.send_party_chat(client, message);
      }
      g_party_client_api.kick_player(client, steamid64_individual_base | account);
      return;
    }
  }

  const auto member_total = static_cast<int>(members.size());
  if (cfg.autoparty_auto_lock && member_total >= max_size)
  {
    lock_party();
  }

  if (member_total > max_size)
  {
    const int to_kick = member_total - max_size;
    if (cfg.autoparty_log)
    {
      print("[autoparty] kicking %d members: %d/%d allowed\n", to_kick, member_total, max_size);
    }
    if (cfg.autoparty_message_kicks)
    {
      char message[192]{};
      std::snprintf(message, sizeof(message),
                    "Kicking %d party members because there are %d out of %d allowed members",
                    to_kick, member_total, max_size);
      g_party_client_api.send_party_chat(client, message);
    }
    int kicked = 0;
    for (auto it = members.rbegin(); it != members.rend() && kicked < to_kick; ++it)
    {
      if (*it == local_account)
      {
        continue;
      }
      g_party_client_api.kick_player(client, steamid64_individual_base | *it);
      ++kicked;
    }
  }

  if (cfg.autoparty_auto_unlock && member_total < max_size)
  {
    unlock_party();
  }

  if (member_total < max_size)
  {
    invite_listed_hosts(members);
  }
}

void mvm_quit()
{
  abandon_current_match();
  if (engine != nullptr)
  {
    engine->client_cmd_unrestricted("disconnect");
  }
}

void automation_controller::on_create_move(user_cmd* user_cmd)
{
  if (puphook::core::is_detach_pending() ||
      engine == nullptr ||
      global_vars == nullptr)
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
  run_mvm_actions(user_cmd);

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
  if (puphook::core::is_detach_pending() ||
      engine == nullptr ||
      global_vars == nullptr)
  {
    return;
  }

  run_auto_report();
  run_auto_vote();
  run_ping_reducer();
  run_mvm_actions();
  autoitem::on_tick();
  mvm_queue::tick();
  run_queueing();
  profile_stalker::tick();
}

void automation_controller::on_paint()
{
  if (puphook::core::is_detach_pending())
  {
    return;
  }

  nographics::update();
  run_startup_sound();
  process_killsay();

  if (engine == nullptr || global_vars == nullptr)
  {
    return;
  }

  if (!engine->is_in_game())
  {
    autoitem::on_tick();
    mvm_queue::tick();
    run_queueing();
    profile_stalker::tick();
  }

  run_autoparty();
}

void automation_controller::run_startup_sound()
{
  if (startup_sound_played_ || engine == nullptr)
  {
    return;
  }

  startup_sound_played_ = true;

  const auto list_path = puphook::core::root_directory() / "assets" / startup_sound_list_name;
  std::ifstream sound_file{ list_path };
  if (!sound_file.is_open())
  {
    print("[startup_sound] list not found: %s\n", list_path.c_str());
    return;
  }

  std::vector<std::string> sounds{};
  std::string sound_name{};
  while (std::getline(sound_file, sound_name))
  {
    if (!sound_name.empty() && sound_name.back() == '\r')
    {
      sound_name.pop_back();
    }

    if (sound_name.empty() || sound_name.front() == '#')
    {
      continue;
    }

    const bool safe_name = std::all_of(sound_name.begin(), sound_name.end(), [](const char character)
    {
      return std::isalnum(static_cast<unsigned char>(character)) ||
        character == '/' || character == '_' || character == '-' || character == '.';
    });
    if (safe_name)
    {
      sounds.push_back(sound_name);
    }
  }

  if (sounds.empty())
  {
    print("[startup_sound] list is empty: %s\n", list_path.c_str());
    return;
  }

  std::random_device random_device{};
  std::mt19937 generator{ random_device() };
  std::uniform_int_distribution<std::size_t> distribution{ 0, sounds.size() - 1 };
  const std::string& selected_sound = sounds[distribution(generator)];
  const std::string command = "play " + selected_sound;
  engine->client_cmd_unrestricted(command.c_str());
  print("[startup_sound] playing %s\n", selected_sound.c_str());
}

void automation_controller::on_menu_tick()
{
#if defined(PUPHOOK_TEXTMODE) && PUPHOOK_TEXTMODE

  if (puphook::core::is_detach_pending() ||
      engine == nullptr ||
      global_vars == nullptr)
  {
    return;
  }

  autoitem::on_tick();
  mvm_queue::tick();
  run_queueing();
  run_autoparty();
  profile_stalker::tick();
#endif

}

void automation_controller::on_dispatch_user_message(int message_type, const bf_read* message_data)
{
  if (puphook::core::is_detach_pending() ||
      engine == nullptr)
  {
    return;
  }

  run_auto_vote_message(message_type, message_data);

  if (message_type == say_text2_user_message_type)
  {
    int entity_index = 0;
    std::string text;
    if (parse_say_text2(message_data, entity_index, text))
    {
      run_chat_commands(text, puphook::core::players::account_id_for_player_index(entity_index), false);
    }
    return;
  }

  if (message_type != text_msg_user_message_type)
  {
    return;
  }

  const auto message = read_text_message_token(message_data);
  if (message == "#GameUI_vote_failed_vote_in_progress")
  {
    vote_active_ = true;
    return;
  }
  if (message != auto_balance_pending_token)
  {
    const bool class_change_blocked =
      (message.find("change class") != std::string_view::npos
        && message.find("wave") != std::string_view::npos)
      || (message.find("Change_Class") != std::string_view::npos
        && message.find("Wave") != std::string_view::npos);
    if (class_change_blocked
      && config.misc.automation.mvm_buybot
      && config.misc.automation.mvm_buybot_auto_class
      && global_vars != nullptr
      && global_vars->realtime >= next_mvm_class_retry_time_)
    {
      engine->client_cmd_unrestricted("retry");
      next_mvm_class_retry_time_ = global_vars->realtime + 10.0f;
    }
    return;
  }

  if (!config.misc.automation.anti_autobalance)
  {
    return;
  }

  print("[anti_autobalance] retrying on pending team change\n");
  engine->client_cmd_unrestricted("retry");
}

void automation_controller::on_game_event(GameEvent* event)
{
  if (puphook::core::is_detach_pending())
  {
    return;
  }

  if (event != nullptr)
  {
    const char* name = event->get_name();
    if (name != nullptr)
    {
      if (std::strcmp(name, "teamplay_round_start") == 0
        || std::strcmp(name, "teamplay_setup_finished") == 0)
      {
        warmup_active_ = false;
      }
      else if (std::strcmp(name, "teamplay_waiting_begins") == 0
        || std::strcmp(name, "teamplay_restart_round") == 0
        || std::strcmp(name, "teamplay_round_win") == 0)
      {
        warmup_active_ = true;
        mvm_auto_abandoned_ = false;
      }

      if (std::strcmp(name, "mvm_reset_stats") == 0
        || std::strcmp(name, "game_newmap") == 0
        || std::strcmp(name, "client_disconnect") == 0
        || std::strcmp(name, "teamplay_round_start") == 0)
      {
        mvm_auto_abandoned_ = false;
      }

      if (std::strcmp(name, "mvm_wave_complete") == 0
        && config.misc.automation.auto_mvm_abandon_mannup
        && !mvm_auto_abandoned_
        && final_mvm_wave_completed())
      {
        mvm_auto_abandoned_ = abandon_current_match();
      }

      if (std::strcmp(name, "party_chat") == 0
        && event->get_int("type", 0) == 1)
      {
        const char* steam_id_text = event->get_string("steamid", "");
        const std::uint64_t steam_id = steam_id_text != nullptr ? std::strtoull(steam_id_text, nullptr, 10) : 0;
        const char* text = event->get_string("text", "");
        if (text != nullptr)
        {
          run_chat_commands(text, static_cast<std::uint32_t>(steam_id & 0xFFFFFFFFull), true);
        }
      }

      if (std::strcmp(name, "revive_player_notify") == 0
        && config.misc.automation.mvm_instant_revive
        && entity_list != nullptr
        && event->get_int("entindex", -1) == engine->get_localplayer_index())
      {
        auto* revive_response = new KeyValues("MVM_Revive_Response");
        revive_response->set_int("accepted", 1);
        send_mvm_command(revive_response);
      }
    }
  }

  run_auto_vote_map(event);
  run_autotaunt(event);
  run_custom_announcer(event);
  run_killsay(event);
}

bool automation_controller::is_setup_time() const
{
  if (entity_list == nullptr)
  {
    return warmup_active_;
  }

  void* rules = tf2_netvars::game_rules_object();
  if (rules == nullptr)
  {
    return warmup_active_;
  }

  static tf2_netvars::lazy_offset waiting_offset{"DT_TFGameRulesProxy", { "m_bInWaitingForPlayers" }};
  static tf2_netvars::lazy_offset state_offset{"DT_TFGameRulesProxy", { "m_iRoundState" }};
  static tf2_netvars::lazy_offset setup_offset{"DT_TFGameRulesProxy", { "m_bInSetup" }};
  const auto proxy_address = reinterpret_cast<std::uintptr_t>(rules);

  const bool waiting = waiting_offset > 0 && *reinterpret_cast<bool*>(proxy_address + waiting_offset);
  const bool in_setup = setup_offset > 0 && *reinterpret_cast<bool*>(proxy_address + setup_offset);
  const int state = state_offset > 0 ? *reinterpret_cast<int*>(proxy_address + state_offset) : -1;

  if (state == gr_state_preround)
  {
    return true;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || localplayer->get_team() != tf_team::BLU)
  {
    return false;
  }

  std::string map_name = engine != nullptr && engine->get_level_name() != nullptr
    ? engine->get_level_name()
    : "";
  const auto slash = map_name.find_last_of("/\\");
  if (slash != std::string::npos)
  {
    map_name = map_name.substr(slash + 1);
  }
  if (map_name.ends_with(".bsp"))
  {
    map_name.resize(map_name.size() - 4);
  }
  if (map_name == "plr_pipeline")
  {
    return false;
  }

  const bool cp_or_payload = map_name.starts_with("cp_") || map_name.starts_with("pl_");
  return in_setup || (waiting && cp_or_payload);
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

  static Convar* no_push = nullptr;
  if (no_push == nullptr) no_push = convar_system->find_var("tf_avoidteammates_pushaway");
  if (no_push != nullptr)
  {
    const int wanted_value = config.misc.movement.no_push ? 0 : 1;
    if (no_push->get_int() != wanted_value)
    {
      no_push->set_int(wanted_value);
    }
  }

  static Convar* engine_no_focus_sleep = nullptr;
  if (engine_no_focus_sleep == nullptr) engine_no_focus_sleep = convar_system->find_var("engine_no_focus_sleep");
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

  static Convar* weapon_allow_inspect = nullptr;
  if (weapon_allow_inspect == nullptr) weapon_allow_inspect = convar_system->find_var("weapon_allow_inspect");
  if (weapon_allow_inspect != nullptr)
  {
    const int wanted_value = config.misc.automation.allow_mvm_inspect ? 1 : 0;
    if (weapon_allow_inspect->get_int() != wanted_value)
    {
      weapon_allow_inspect->set_int(wanted_value);
    }
  }

  if (!nographics::is_enabled())
  {
    return;
  }

  static Convar* mat_queue_mode = nullptr;
  if (mat_queue_mode == nullptr) mat_queue_mode = convar_system->find_var("mat_queue_mode");
  if (mat_queue_mode != nullptr && mat_queue_mode->get_int() != 0)
  {
    mat_queue_mode->set_int(0);
  }

  static Convar* volume = nullptr;
  if (volume == nullptr) volume = convar_system->find_var("volume");
  if (volume != nullptr && volume->get_float() != 0.0f)
  {
    volume->set_float(0.0f);
  }

  static Convar* fps_max = nullptr;
  if (fps_max == nullptr) fps_max = convar_system->find_var("fps_max");
  if (fps_max != nullptr && fps_max->get_int() != 30)
  {
    fps_max->set_int(30);
  }
  static Convar* cl_interp = convar_system->find_var("cl_interp");
  if (cl_interp != nullptr && cl_interp->get_float() > 0.152f) {
    cl_interp->set_float(0.152f);
  }
}

void automation_controller::run_auto_class_select()
{
  if (!config.misc.automation.auto_class_select)
  {
    return;
  }

  if (config.misc.automation.auto_class_dont_join_during_warmup && is_setup_time())
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
    }
    return;
  }

  if (!engine->is_in_game())
  {
    reported_account_ids_.clear();
    return;
  }

  initialize_report_player_account();
  if (g_report_player_account == nullptr)
  {
    return;
  }

  const int local_index = engine->get_localplayer_index();
  const int max_clients = puphook::core::player_resource::max_client_index();
  for (int index = 1; index <= max_clients; ++index)
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

    const auto account_id = static_cast<std::uint32_t>(info.friends_id);
    if (pup_ipc::client::is_known_local_ipc_friend(account_id) ||
        player->is_friend() ||
        player->is_party() ||
        player->is_ignored() ||
        puphook::core::players::is_friendly(account_id) ||
        puphook::core::players::is_ignored(account_id))
    {
      continue;
    }

    if (std::ranges::find(reported_account_ids_, account_id) != reported_account_ids_.end())
    {
      continue;
    }

    const auto steam_id = SteamID(account_id, 1, k_EUniversePublic, k_EAccountTypeIndividual);
    const bool report_sent = g_report_player_account(
      static_cast<std::uint64_t>(steam_id.m_steamid.m_unAll64Bits),
      report_reason_cheating);
    (void)report_sent;
    reported_account_ids_.push_back(account_id);
#ifdef PUPHOOK_DEBUG_AUTO_REPORT

    print("[auto_report] %s %s (%lu)\n", report_sent ? "reported" : "report rejected", info.name, info.friends_id);
#endif
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

namespace
{

bool auto_vote_protected(std::uint32_t account_id)
{
  if (account_id == 0)
  {
    return false;
  }
  return puphook::core::players::is_friendly(account_id) ||
         puphook::core::players::is_ignored(account_id) ||
         puphook::core::players::has_role(account_id, puphook::core::players::party_role);
}

std::uint32_t vote_account_id(Entity* player_resource, int index)
{
  static tf2_netvars::lazy_offset account_id_offset{"DT_TFPlayerResource", {"baseclass", "m_iAccountID"}};
  return puphook::core::player_resource::read_value<std::uint32_t>(
      player_resource, account_id_offset, index);
}

bool vote_index_valid(Entity* player_resource, int index)
{
  static tf2_netvars::lazy_offset valid_offset{"DT_TFPlayerResource", {"baseclass", "m_bValid"}};
  return puphook::core::player_resource::read_value<std::uint8_t>(
             player_resource, valid_offset, index) != 0;
}

int vote_user_id(Entity* player_resource, int index)
{
  static tf2_netvars::lazy_offset user_id_offset{"DT_TFPlayerResource", {"baseclass", "m_iUserID"}};
  return puphook::core::player_resource::read_value<int>(
      player_resource, user_id_offset, index);
}

int vote_team(Entity* player_resource, int index)
{
  static tf2_netvars::lazy_offset team_offset{"DT_TFPlayerResource", {"baseclass", "m_iTeam"}};
  return puphook::core::player_resource::read_value<int>(
      player_resource, team_offset, index);
}

void cast_vote(int vote_id, int option)
{
  char command[40]{};
  std::snprintf(command, sizeof(command), "vote %d option%d", vote_id, option);
  engine->client_cmd_unrestricted(command);
}

}

void automation_controller::run_auto_vote_message(int message_type, const bf_read* message_data)
{
  const auto& settings = config.misc.automation;
  if (settings.auto_vote == 0 || message_data == nullptr || global_vars == nullptr)
  {
    return;
  }

  vote_message_reader reader{message_data, message_data->current_bit};
  switch (message_type)
  {
  case vote_start_user_message_type:
    {
      const int team = reader.read(8);
      const int vote_id = reader.read(32);
      const int caller = reader.read(8);
      const std::string reason = reader.read_string();
      reader.read_string();
      const int target = reader.read(8) >> 1;
      if (!reader.ok || reason.find("kick") == std::string::npos)
      {
        return;
      }

      const int local_index = engine->get_localplayer_index();
      if (caller == local_index || target == local_index)
      {
        vote_active_ = true;
        return;
      }

      pending_votes_[vote_id] = pending_vote{team, caller, target, global_vars->curtime, 0.0f, false};
      return;
    }
  case vote_pass_user_message_type:
  case vote_failed_user_message_type:
    {
      reader.read(4);
      const int vote_id = reader.read(32);
      if (!reader.ok)
      {
        return;
      }
      pending_votes_.erase(vote_id);
      vote_active_ = false;
      return;
    }
  case call_vote_failed_user_message_type:
    {
      if ((settings.auto_vote & Misc::Automation::auto_vote_kick) == 0)
      {
        return;
      }
      const int reason = reader.read(8);
      const int time_left = reader.read(16);
      if (reader.ok &&
          (reason == vote_fail_reason_on_cooldown || reason == vote_fail_reason_rate_exceeded))
      {
        vote_call_cooldown_expire_ = global_vars->curtime + static_cast<float>(time_left);
      }
      return;
    }
  default:
    return;
  }
}

void automation_controller::run_auto_vote()
{
  const auto& settings = config.misc.automation;
  if (settings.auto_vote == 0 || engine == nullptr || entity_list == nullptr ||
      global_vars == nullptr || !engine->is_in_game())
  {
    pending_votes_.clear();
    vote_active_ = false;
    return;
  }

  Player* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr)
  {
    return;
  }

  const int local_index = engine->get_localplayer_index();
  const int local_team = static_cast<int>(localplayer->get_team());
  Entity* player_resource = puphook::core::player_resource::get_player_resource_entity();
  const float curtime = global_vars->curtime;

  if (!pending_votes_.empty() && player_resource != nullptr)
  {
    static Convar* vote_timer_duration = nullptr;
    if (vote_timer_duration == nullptr && convar_system != nullptr)
    {
      vote_timer_duration = convar_system->find_var("sv_vote_timer_duration");
    }
    const float max_duration = (vote_timer_duration != nullptr ? vote_timer_duration->get_float() : 15.0f) + 1.0f;

    std::vector<int> expired{};
    for (auto& [vote_id, vote] : pending_votes_)
    {
      const bool other_team = vote.team != local_team;
      if (other_team || !vote_index_valid(player_resource, vote.target) ||
          vote.start_time + max_duration <= curtime)
      {
        if (!other_team || vote.voted)
        {
          vote_active_ = false;
        }
        expired.push_back(vote_id);
        continue;
      }
      if (vote.voted)
      {
        continue;
      }

      vote_active_ = true;
      if ((settings.auto_vote & (Misc::Automation::auto_vote_defend | Misc::Automation::auto_vote_assist)) == 0)
      {
        vote.voted = true;
        continue;
      }

      if (settings.auto_vote_delay)
      {
        if (vote.vote_time == 0.0f)
        {
          const float low = std::min(settings.auto_vote_delay_min, settings.auto_vote_delay_max);
          const float high = std::max(settings.auto_vote_delay_min, settings.auto_vote_delay_max);
          vote.vote_time = vote.start_time +
              low + static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * (high - low);
        }
        if (vote.vote_time > curtime)
        {
          break;
        }
      }

      vote.voted = true;

      const std::uint32_t target_account = vote_account_id(player_resource, vote.target);
      const bool target_protected = auto_vote_protected(target_account);
      const bool target_marked =
          puphook::core::players::has_role(target_account, puphook::core::players::cheater_role);
      const bool defend_target =
          (settings.auto_vote & Misc::Automation::auto_vote_defend) != 0 && target_protected;

      if (vote_index_valid(player_resource, vote.caller))
      {
        const std::uint32_t caller_account = vote_account_id(player_resource, vote.caller);
        if (auto_vote_protected(caller_account) && !target_protected)
        {
          cast_vote(vote_id, 1);
        }
        else if (defend_target)
        {
          cast_vote(vote_id, 2);
        }
        else if ((settings.auto_vote & Misc::Automation::auto_vote_kick) != 0 && target_marked)
        {
          cast_vote(vote_id, 1);
        }
        else if ((settings.auto_vote & Misc::Automation::auto_vote_assist) != 0 &&
                 auto_vote_protected(caller_account))
        {
          cast_vote(vote_id, 1);
        }
      }
      else
      {
        cast_vote(vote_id, defend_target ? 2 : 1);
      }
      break;
    }
    for (const int vote_id : expired)
    {
      pending_votes_.erase(vote_id);
    }
    if (vote_active_)
    {
      return;
    }
  }

  if ((settings.auto_vote & Misc::Automation::auto_vote_kick) == 0 ||
      player_resource == nullptr || local_index <= 0)
  {
    return;
  }

  if (vote_active_ || vote_call_cooldown_expire_ > curtime ||
      curtime < next_auto_vote_kick_time_)
  {
    return;
  }
  next_auto_vote_kick_time_ = curtime + 1.0f;

  std::vector<int> candidates{};
  const int max_clients = global_vars->max_clients;
  for (int index = 1; index <= max_clients; ++index)
  {
    if (index == local_index || !vote_index_valid(player_resource, index) ||
        vote_team(player_resource, index) != local_team)
    {
      continue;
    }

    player_info info{};
    if (!engine->get_player_info(index, &info) || info.fakeplayer)
    {
      continue;
    }

    const std::uint32_t account_id = vote_account_id(player_resource, index);
    if (account_id == 0 || auto_vote_protected(account_id))
    {
      continue;
    }
    const bool marked =
        puphook::core::players::has_role(account_id, puphook::core::players::cheater_role);
    if (!marked && (settings.auto_vote & Misc::Automation::auto_vote_kick_all) == 0)
    {
      continue;
    }
    candidates.push_back(index);
  }

  if (candidates.empty())
  {
    return;
  }

  static Convar* vote_creation_timer = nullptr;
  if (vote_creation_timer == nullptr && convar_system != nullptr)
  {
    vote_creation_timer = convar_system->find_var("sv_vote_creation_timer");
  }
  vote_call_cooldown_expire_ =
      curtime + (vote_creation_timer != nullptr ? vote_creation_timer->get_float() : 300.0f);

  const int target = candidates[static_cast<std::size_t>(std::rand()) % candidates.size()];
  char command[64]{};
  std::snprintf(command, sizeof(command), "callvote Kick \"%d other\"",
                vote_user_id(player_resource, target));
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

  int wanted_slot = std::clamp(config.misc.automation.autotaunt_weapon_slot, 0, 5);
  const auto local_class = localplayer->get_tf_class();
  if (wanted_slot == 4 && local_class != tf_class::SPY && local_class != tf_class::ENGINEER)
  {
    wanted_slot = 1;
  }
  if (wanted_slot == 5 && local_class != tf_class::ENGINEER)
  {
    wanted_slot = 1;
  }
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
  [[maybe_unused]] const int command_status = std::system(command.c_str());
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
    case Misc::Automation::chatspam_source::PUPHOOK:
      message = choose_message(builtin_chatspam_puphook, config.misc.automation.chatspam_random, chatspam_index_, chatspam_last_index_);
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
    case Misc::Automation::killsay_mode::PUPHOOK:
      message = choose_message(builtin_killsay_puphook, true, chatspam_index_, chatspam_last_index_);
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
    next_noisemaker_time_ = 0.0f;
    return;
  }

  auto* localplayer = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  if (!engine->is_in_game())
  {
    next_noisemaker_time_ = 0.0f;
    return;
  }

  if (localplayer == nullptr || !localplayer->is_alive() || global_vars->realtime < next_noisemaker_time_)
  {
    return;
  }

  KeyValues* press = new KeyValues("+use_action_slot_item_server");
  engine->server_cmd_keyvalues(press);
  KeyValues* release = new KeyValues("-use_action_slot_item_server");
  engine->server_cmd_keyvalues(release);
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

bool convert_micspam_voice_file(const std::filesystem::path& selected)
{
  namespace fs = std::filesystem;

  std::error_code ec;
  const std::string source_path = selected.string();

  const std::string cmd =
    "ffmpeg -hide_banner -loglevel error -y -i " + shell_quote(source_path) +
    " -ac 1 -ar 22050 -acodec pcm_s16le -fflags +bitexact -flags +bitexact " +
    shell_quote(micspam_voice_input_path) + " >/dev/null 2>&1";
  if (std::system(cmd.c_str()) == 0 && fs::exists(micspam_voice_input_path, ec))
  {
    return true;
  }

  fs::copy_file(selected, micspam_voice_input_path, fs::copy_options::overwrite_existing, ec);
  return !ec && fs::exists(micspam_voice_input_path, ec);
}

void micspam_prepare_worker(const std::filesystem::path source)
{
  auto& state = micspam_prepare();
  const bool converted = convert_micspam_voice_file(source);
  state.result_value.store(converted, std::memory_order_release);
  state.result_ready.store(true, std::memory_order_release);
  state.job_running.store(false, std::memory_order_release);
}

bool automation_controller::prepare_micspam_voice_file(){
  namespace fs = std::filesystem;

  std::error_code ec;
  if (!fs::is_directory(micspam_source_directory, ec))
  {
    return false;
  }

  std::vector<fs::path> candidates;
  for (const auto& entry : fs::directory_iterator(micspam_source_directory, ec))
  {
    if (ec || !entry.is_regular_file(ec))
    {
      continue;
    }
    std::string ext = entry.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    if (ext == ".wav" || ext == ".mp3" || ext == ".webm")
    {
      candidates.push_back(entry.path());
    }
  }

  if (candidates.empty())
  {
    return false;
  }

  const std::size_t index = candidates.size() == 1
    ? 0
    : static_cast<std::size_t>(std::rand()) % candidates.size();
  const fs::path selected = candidates[index];

  auto& state = micspam_prepare();
  std::scoped_lock lock{state.mutex};
  if (state.result_ready.load(std::memory_order_acquire) ||
      state.job_running.load(std::memory_order_acquire))
  {
    return false;
  }
  if (state.worker.joinable())
  {
    state.worker.join();
  }

  fs::remove(micspam_voice_input_path, ec);

  const auto source_ext = [&]() {
    std::string e = selected.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return std::tolower(c); });
    return e;
  }();
  const bool needs_conversion = source_ext != ".wav";

  state.result_ready.store(false, std::memory_order_release);
  state.result_value.store(false, std::memory_order_release);
  if (!needs_conversion)
  {
    fs::copy_file(selected, micspam_voice_input_path, fs::copy_options::overwrite_existing, ec);
    return !ec && fs::exists(micspam_voice_input_path, ec);
  }

  state.job_running.store(true, std::memory_order_release);
  state.worker = std::thread{micspam_prepare_worker, std::move(selected)};
  return false;
}

void automation_controller::stop_micspam()
{
  next_micspam_on_time_ = 0.0f;
  next_micspam_off_time_ = 0.0f;

  if (micspam_recording_ && engine != nullptr)
  {
    engine->client_cmd_unrestricted("-voicerecord");
  }
  micspam_recording_ = false;

  if (micspam_voice_inputfromfile_active_ && convar_system != nullptr)
  {
    if (Convar* voice_inputfromfile = convar_system->find_var("voice_inputfromfile"))
    {
      voice_inputfromfile->set_int(0);
    }
  }
  micspam_voice_inputfromfile_active_ = false;
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

  if (!micspam_recording_ && next_micspam_on_time_ <= 0.0f)
  {
    next_micspam_on_time_ = now;
  }

  if (!micspam_recording_ && now >= next_micspam_on_time_)
  {
    bool voice_file_ready = false;
    if (config.misc.automation.micspam_from_file)
    {
      voice_file_ready = poll_micspam_prepare_result();
      if (!voice_file_ready)
      {
        voice_file_ready = prepare_micspam_voice_file();
      }
      if (voice_file_ready && convar_system != nullptr)
      {
        Convar* sv_allow_voice_from_file = convar_system->find_var("sv_allow_voice_from_file");
        Convar* voice_inputfromfile = convar_system->find_var("voice_inputfromfile");
        if (sv_allow_voice_from_file != nullptr && voice_inputfromfile != nullptr)
        {
          sv_allow_voice_from_file->set_int(1);
          voice_inputfromfile->set_int(1);
          micspam_voice_inputfromfile_active_ = true;
          engine->client_cmd_unrestricted("+voicerecord");
          micspam_recording_ = true;
        }
      }
    }
    else
    {
      engine->client_cmd_unrestricted("+voicerecord");
      micspam_recording_ = true;
    }
    if (micspam_recording_)
    {
      next_micspam_off_time_ = now + static_cast<float>(on_seconds);
    }
    else
    {

      next_micspam_on_time_ = now + static_cast<float>(off_seconds);
    }
  }

  if (micspam_recording_ && now >= next_micspam_off_time_)
  {
    engine->client_cmd_unrestricted("-voicerecord");
    micspam_recording_ = false;
    if (micspam_voice_inputfromfile_active_ && convar_system != nullptr)
    {
      if (Convar* voice_inputfromfile = convar_system->find_var("voice_inputfromfile"))
      {
        voice_inputfromfile->set_int(0);
      }
      micspam_voice_inputfromfile_active_ = false;
    }
    next_micspam_on_time_ = now + static_cast<float>(off_seconds);
    next_micspam_off_time_ = 0.0f;
  }
}

void automation_controller::reset_buybot()
{
  mvm_buybot_step_ = 1;
  mvm_buybot_upgrade_slot_step_ = 0;
  mvm_buybot_upgrade_index_ = 0;
  mvm_buybot_priority_step_ = 0;
  mvm_buybot_cash_limit_reached_ = false;
  mvm_buybot_finished_upgrades_ = false;
  mvm_buybot_navigating_ = false;
  mvm_buybot_stall_time_ = 0.0f;
  next_mvm_buybot_time_ = 0.0f;
  next_mvm_scout_equip_time_ = 0.0f;
}

bool automation_controller::is_buybot_busy() const
{
  if (!config.misc.automation.mvm_buybot || mvm_buybot_finished_upgrades_)
  {
    return false;
  }
  if (global_vars != nullptr && mvm_buybot_stall_time_ > 0.0f
    && global_vars->curtime - mvm_buybot_stall_time_ > 45.0f)
  {
    return false;
  }
  if (entity_list == nullptr)
  {
    return false;
  }
  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr)
  {
    return false;
  }
  if (localplayer->in_upgrade_zone())
  {
    return true;
  }
  if (config.misc.automation.mvm_buybot_auto_class
    && global_vars != nullptr
    && global_vars->realtime < next_class_action_time_)
  {
    return true;
  }
  return mvm_buybot_navigating_;
}

void automation_controller::mvm_fix()
{
  if (!is_mvm_context())
  {
    print("[pup_mvm_fix] not in Mann vs. Machine\n");
    return;
  }

  config.misc.automation.mvm_buybot = true;
  mvm_buybot_cash_limit_reached_ = true;
  mvm_buybot_finished_upgrades_ = false;
  mvm_buybot_navigating_ = false;
  mvm_buybot_stall_time_ = 0.0f;
  if (engine != nullptr)
  {
    engine->client_cmd_unrestricted("retry");
  }
  print("[pup_mvm_fix] buybot marked as funded, reconnecting\n");
}

void automation_controller::run_chat_commands(std::string_view message, std::uint32_t account_id, bool party_chat)
{
  if (config.misc.automation.mvm_chat_commands == Misc::Automation::mvm_chat_command_mode::OFF
    || message.empty())
  {
    return;
  }

  std::string cleaned;
  cleaned.reserve(message.size());
  for (unsigned char character : message)
  {
    if (character >= ' ')
    {
      cleaned.push_back(static_cast<char>(character));
    }
  }

  const auto start = cleaned.find_first_not_of(' ');
  if (start == std::string::npos)
  {
    return;
  }
  cleaned.erase(0, start);
  if (!cleaned.empty() && (cleaned.front() == '!' || cleaned.front() == '/'))
  {
    cleaned.erase(0, 1);
  }

  std::vector<std::string> tokens;
  for (std::size_t pos = 0; pos < cleaned.size();)
  {
    const auto end = cleaned.find(' ', pos);
    const auto token_end = end == std::string::npos ? cleaned.size() : end;
    if (token_end > pos)
    {
      tokens.emplace_back(cleaned.substr(pos, token_end - pos));
    }
    pos = token_end + 1;
  }
  if (tokens.empty())
  {
    return;
  }

  for (char& character : tokens[0])
  {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  const std::string& command = tokens[0];
  const bool allowed_command =
    command == "pup_mvm_fix" || command == "pup_mvm_quit" || command == "pup_mvm_tele"
    || command == "pup_mvm_rent" || command == "pup_party_givelead";
  if (!allowed_command)
  {
    return;
  }

  const std::uint32_t local_account = engine != nullptr
    ? puphook::core::players::account_id_for_player_index(engine->get_localplayer_index())
    : 0;
  bool allowed = local_account != 0 && account_id == local_account;
  if (!allowed)
  {
    switch (config.misc.automation.mvm_chat_commands)
    {
      case Misc::Automation::mvm_chat_command_mode::PARTY:
        allowed = party_chat || puphook::core::players::has_role(account_id, puphook::core::players::party_role);
        break;
      case Misc::Automation::mvm_chat_command_mode::FRIENDS:
        allowed = puphook::core::players::has_role(account_id, puphook::core::players::friend_role);
        break;
      case Misc::Automation::mvm_chat_command_mode::ROLE:
        allowed = account_id != 0
          && puphook::core::players::has_role(account_id, config.misc.automation.mvm_chat_commands_role);
        break;
      default:
        break;
    }
  }
  if (!allowed)
  {
    return;
  }

  if (command == "pup_mvm_fix")
  {
    mvm_fix();
  }
  else if (command == "pup_mvm_quit")
  {
    mvm_quit();
  }
  else if (command == "pup_mvm_tele")
  {
    if (!navbot::controller().path_to_teleporter())
    {
      print("[pup_mvm_tele] no reachable teleporter entrance found\n");
    }
  }
  else if (command == "pup_mvm_rent")
  {
    autoitem::mvm_rent();
  }
  else if (command == "pup_party_givelead")
  {
    if (!promote_party_leader(account_id))
    {
      print("[pup_party_givelead] failed to promote %u\n", account_id);
      return;
    }
    print("[pup_party_givelead] gave party leadership to %u\n", account_id);
  }
}

void automation_controller::run_mvm_actions(user_cmd* user_cmd)
{
  if (engine == nullptr || global_vars == nullptr || entity_list == nullptr || convar_system == nullptr)
  {
    return;
  }

  if (!engine->is_in_game() || !is_mvm_context())
  {
    reset_buybot();
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr)
  {
    return;
  }

  if ((localplayer->get_tf_class() == tf_class::MEDIC || localplayer->in_upgrade_zone())
    && !has_vaccinator(localplayer))
  {
    mvm_buybot_cash_limit_reached_ = true;
  }

  const int max_cash = std::max(config.misc.automation.mvm_buybot_max_cash, 0);
  if (max_cash > 0 && localplayer->get_currency() >= max_cash)
  {
    mvm_buybot_cash_limit_reached_ = true;
  }

  int round_state = -1;
  if (void* rules = tf2_netvars::game_rules_object(); rules != nullptr)
  {
    static tf2_netvars::lazy_offset state_offset{"DT_TFGameRulesProxy", {"m_iRoundState"}};
    if (state_offset > 0)
    {
      round_state = *reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(rules) + state_offset);
    }
  }

  if (auto* objective = current_mvm_objective_resource(); objective != nullptr)
  {
    const bool between_waves = objective->is_mvm_between_waves();
    const bool wave_running = !between_waves && round_state == gr_state_rnd_running;
    if (objective->get_mvm_wave_count() > 1)
    {
      mvm_buybot_finished_upgrades_ = true;
      mvm_buybot_cash_limit_reached_ = true;
      mvm_buybot_navigating_ = false;
      mvm_buybot_stall_time_ = 0.0f;
    }
    else if (wave_running)
    {
      mvm_buybot_cash_limit_reached_ = true;
    }
  }
  else if (round_state == gr_state_rnd_running)
  {
    mvm_buybot_cash_limit_reached_ = true;
  }

  const bool cash_limit_reached = mvm_buybot_cash_limit_reached_;
  auto* upgrade_station = nearest_mvm_upgrade_station(localplayer);
  if (config.misc.automation.mvm_buybot && !mvm_buybot_finished_upgrades_
    && !localplayer->in_upgrade_zone())
  {
    if (mvm_buybot_stall_time_ == 0.0f)
    {
      mvm_buybot_stall_time_ = global_vars->curtime;
    }
  }
  else
  {
    mvm_buybot_stall_time_ = 0.0f;
  }

  if (config.misc.automation.mvm_buybot
    && config.misc.automation.mvm_buybot_auto_class)
  {
    auto desired_class = cash_limit_reached
      ? config.misc.automation.mvm_buybot_class
      : tf_class::MEDIC;
    if (desired_class == tf_class::MEDIC && cash_limit_reached)
    {
      desired_class = tf_class::HEAVYWEAPONS;
    }
    if (localplayer->get_tf_class() != desired_class
      && global_vars->realtime >= next_class_action_time_)
    {
      if (localplayer->in_upgrade_zone())
      {
        if (upgrade_station != nullptr)
        {
          move_towards_mvm_station(localplayer, user_cmd,
            localplayer->get_origin() * 2.0f - upgrade_station->get_origin());
        }
        return;
      }
      static const char* class_names[] = {
        "undefined", "scout", "sniper", "soldier", "demoman", "medic",
        "heavyweapons", "pyro", "spy", "engineer"
      };
      const auto class_index = static_cast<int>(desired_class);
      if (class_index > 0 && class_index < static_cast<int>(std::size(class_names)))
      {
        char command[64]{};
        std::snprintf(command, sizeof(command), "joinclass %s", class_names[class_index]);
        engine->client_cmd_unrestricted(command);
        engine->client_cmd_unrestricted("menuclosed");
        next_class_action_time_ = global_vars->realtime + auto_class_interval;
      }
      return;
    }
  }

  mvm_buybot_navigating_ = config.misc.automation.mvm_buybot
    && !mvm_buybot_finished_upgrades_
    && !localplayer->in_upgrade_zone()
    && upgrade_station != nullptr;

  if (mvm_buybot_navigating_ && user_cmd != nullptr && !navbot::controller().is_pathing())
  {
    move_towards_mvm_station(localplayer, user_cmd, upgrade_station->get_origin());
  }

  bool issued_command = false;
  if (global_vars->realtime >= next_mvm_command_time_ &&
      config.misc.automation.mvm_instant_respawn &&
      !localplayer->is_alive())
  {
    auto* revive_response = new KeyValues("MVM_Revive_Response");
    revive_response->set_int("accepted", 1);
    send_mvm_command(revive_response);
    issued_command = true;
  }

  bool mvm_ready_window = false;
  bool already_ready = false;
  if (void* rules = tf2_netvars::game_rules_object(); rules != nullptr)
  {
    static tf2_netvars::lazy_offset waiting_offset{
      "DT_TFGameRulesProxy", {"m_bInWaitingForPlayers"}};
    static tf2_netvars::lazy_offset state_offset{
      "DT_TFGameRulesProxy", {"m_iRoundState"}};
    static tf2_netvars::lazy_offset ready_offset{
      "DT_TFGameRulesProxy", {"m_bPlayerReady"}};
    const auto proxy_address = reinterpret_cast<std::uintptr_t>(rules);
    mvm_ready_window = waiting_offset > 0 && state_offset > 0
      && *reinterpret_cast<bool*>(proxy_address + waiting_offset)
      && *reinterpret_cast<int*>(proxy_address + state_offset) == gr_state_between_rounds;
    const int local_index = engine->get_localplayer_index();
    if (ready_offset > 0 && local_index >= 0 && local_index < 102)
    {
      already_ready = reinterpret_cast<bool*>(proxy_address + ready_offset)[local_index];
    }
  }
  if (global_vars->realtime >= next_mvm_command_time_
    && config.misc.automation.auto_mvm_ready_up && mvm_ready_window && !already_ready)
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
    reset_buybot();
    return;
  }

  if (mvm_buybot_finished_upgrades_ || !localplayer->in_upgrade_zone())
  {
    return;
  }

  if (global_vars->realtime < next_mvm_buybot_time_)
  {
    return;
  }

  const bool waiting_for_medic = config.misc.automation.mvm_buybot_auto_class
    && !cash_limit_reached
    && localplayer->get_tf_class() != tf_class::MEDIC;
  if (waiting_for_medic)
  {
    return;
  }

  if (cash_limit_reached)
  {
    if (localplayer->get_tf_class() == tf_class::MEDIC)
    {
      next_mvm_buybot_time_ = global_vars->realtime + mvm_buybot_interval;
      return;
    }

    if (localplayer->get_tf_class() == tf_class::SCOUT
      && global_vars->realtime >= next_mvm_scout_equip_time_)
    {
      autoitem::equip_item(static_cast<int>(tf_class::SCOUT), 0, Scout_m_ForceANature);
      autoitem::equip_item(static_cast<int>(tf_class::SCOUT), 1, Scout_s_MadMilk);
      next_mvm_scout_equip_time_ = global_vars->realtime + 3.0f;
    }

    int plan_count = 0;
    const auto* plan = buybot_plan_for_class(localplayer->get_tf_class(), plan_count);
    if (plan != nullptr && mvm_buybot_priority_step_ < plan_count)
    {
      const auto step = plan[mvm_buybot_priority_step_++];
      const int levels = mvm_upgrade_max_levels(step.upgrade);
      send_mvm_command(new KeyValues("MvM_UpgradesBegin"));
      for (int level = 0; level < levels; ++level)
      {
        send_mvm_upgrade(step.slot, step.upgrade, 1);
      }
      send_mvm_upgrades_done(levels);
      next_mvm_buybot_time_ = global_vars->realtime + 0.05f;
      return;
    }

    constexpr int max_upgrade_index = 128;
    constexpr std::array<int, 2> upgrade_slots{0, -1};
    const int slot = upgrade_slots[
      static_cast<std::size_t>(mvm_buybot_upgrade_slot_step_) % upgrade_slots.size()];
    const int levels = mvm_upgrade_max_levels(mvm_buybot_upgrade_index_);

    send_mvm_command(new KeyValues("MvM_UpgradesBegin"));
    for (int level = 0; level < levels; ++level)
    {
      send_mvm_upgrade(slot, mvm_buybot_upgrade_index_, 1);
    }
    send_mvm_upgrades_done(levels);

    ++mvm_buybot_upgrade_index_;
    if (mvm_buybot_upgrade_index_ >= max_upgrade_index)
    {
      mvm_buybot_upgrade_index_ = 0;
      ++mvm_buybot_upgrade_slot_step_;
      if (mvm_buybot_upgrade_slot_step_ >= static_cast<int>(upgrade_slots.size()))
      {
        mvm_buybot_finished_upgrades_ = true;
        mvm_buybot_navigating_ = false;
      }
    }

    next_mvm_buybot_time_ = global_vars->realtime + 0.05f;
    return;
  }

  mvm_buybot_upgrade_slot_step_ = 0;
  mvm_buybot_upgrade_index_ = 0;

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
  constexpr int signon_state_full = 6;

  if (convar_system == nullptr || global_vars == nullptr || engine == nullptr)
  {
    return;
  }

  static Convar* cl_cmdrate = convar_system->find_var("cl_cmdrate");
  if (cl_cmdrate == nullptr)
  {
    return;
  }

  const bool connected = engine->is_connected() && client_state != nullptr &&
    client_state->m_nSignonState == signon_state_full && client_state->m_NetChannel != nullptr &&
    !client_state->m_NetChannel->is_loopback();

  if (!connected)
  {
    ping_reducer_server_rate_sent_ = false;
    ping_reducer_last_server_rate_ = 0;
    ping_reducer_saved_cmd_rate_ = false;
    return;
  }

  if (!ping_reducer_saved_cmd_rate_)
  {
    original_cmd_rate_ = cl_cmdrate->get_int();
    ping_reducer_saved_cmd_rate_ = true;
  }

  if (global_vars->realtime < next_ping_reduce_time_)
  {
    return;
  }

  next_ping_reduce_time_ = global_vars->realtime + ping_reduce_interval;

  int desired_server_rate = original_cmd_rate_;
  if (config.misc.exploits.ping_reducer)
  {
    const int ping = get_local_ping();
    if (ping <= 0)
    {
      return;
    }

    if (ping > config.misc.exploits.ping_target)
    {

      desired_server_rate = -1;
    }
  }

  if (ping_reducer_server_rate_sent_ && ping_reducer_last_server_rate_ == desired_server_rate)
  {
    return;
  }

  char value[32]{};
  std::snprintf(value, sizeof(value), "%d", desired_server_rate);
  net_set_convar_message cmdrate_message("cl_cmdrate", value);
  if (client_state->m_NetChannel->send_net_msg(cmdrate_message, false, false))
  {
    ping_reducer_server_rate_sent_ = true;
    ping_reducer_last_server_rate_ = desired_server_rate;
  }
}

void automation_controller::run_boost_queueing()
{
  static float next_debug_log_time = 0.0f;
  const bool emit_debug_log = should_emit_queue_debug(next_debug_log_time);

  const bool in_game = engine->is_in_game();
  const bool is_connected = engine->is_connected();
  const bool is_loading = engine->is_drawing_loading_image();
  const bool has_net_channel = client_state != nullptr && client_state->m_NetChannel != nullptr;
  const bool still_attached_to_server = in_game || is_connected || has_net_channel;
  const bool loading_or_connecting = !in_game && still_attached_to_server;

  if (!config.misc.automation.boost_queue_enabled)
  {
    boost_leave_requested_ = false;
    boost_match_start_time_ = 0.0f;
    queue_loading_start_time_ = 0.0f;
    pup_ipc::client::set_in_casual_queue(false);
    return;
  }

  auto* party_client = get_party_client();
  if (party_client == nullptr)
  {
    if (emit_debug_log)
    {
      log_queue_debug("boost skip party_client=null api_ready=%d\n", party_client_api_ready() ? 1 : 0);
    }
    return;
  }

  bool in_match_queue = g_party_client_api.is_in_queue_for_match_group != nullptr &&
                        g_party_client_api.is_in_queue_for_match_group(party_client, casual_match_group_default);
  bool in_standby = is_in_standby_queue(party_client);
  pup_ipc::client::set_in_casual_queue(in_match_queue || in_standby);

  const auto leave_current_match = [&](const char* reason) {
    if (boost_leave_requested_)
    {
      return;
    }

    const bool used_abandon = abandon_current_match();
    log_queue_debug("boost %s abandon=%d\n", reason, used_abandon ? 1 : 0);
    if (!used_abandon)
    {
      engine->client_cmd_unrestricted("disconnect");
    }

    boost_leave_requested_ = true;
    next_queue_action_time_ = 0.0f;
  };

  if (still_attached_to_server)
  {
    if (config.misc.automation.boost_queue == Misc::Automation::boost_queue_mode::INSTANT)
    {
      leave_current_match("instant leave");
      return;
    }

    if (loading_or_connecting || is_loading)
    {
      boost_match_start_time_ = 0.0f;
      if (queue_loading_start_time_ <= 0.0f)
      {
        queue_loading_start_time_ = global_vars->realtime;
      }

      const float loading_duration = global_vars->realtime - queue_loading_start_time_;
      if (loading_duration >= boost_queue_wait_seconds)
      {
        leave_current_match("loading wait timeout");
      }
      else if (emit_debug_log)
      {
        log_queue_debug("boost wait loading duration=%.2f\n", loading_duration);
      }
      return;
    }

    queue_loading_start_time_ = 0.0f;
    if (in_game)
    {
      const int ipc_peer_count = pup_ipc::client::local_ipc_peer_count_on_current_server();
      if (ipc_peer_count <= 1)
      {
        if (boost_match_start_time_ <= 0.0f)
        {
          boost_match_start_time_ = global_vars->realtime;
        }

        const float match_duration = global_vars->realtime - boost_match_start_time_;
        if (match_duration >= boost_queue_wait_seconds)
        {
          leave_current_match("solo match wait timeout");
        }
        else if (emit_debug_log)
        {
          log_queue_debug("boost wait solo ipc_peers=%d duration=%.2f\n", ipc_peer_count, match_duration);
        }
      }
      else
      {
        boost_match_start_time_ = 0.0f;
        if (pup_ipc::client::is_first_local_ipc_peer_on_current_server())
        {
          leave_current_match("first ipc peer");
        }
        else if (emit_debug_log)
        {
          log_queue_debug("boost wait staying ipc_peers=%d first=0\n", ipc_peer_count);
        }
      }
      return;
    }

    return;
  }

  boost_leave_requested_ = false;
  boost_match_start_time_ = 0.0f;
  queue_loading_start_time_ = 0.0f;

  if (in_standby)
  {
    if (cancel_standby_queue(party_client))
    {
      log_queue_debug("boost leaving standby queue\n");
    }
    next_queue_action_time_ = global_vars->realtime + auto_queue_interval;
    return;
  }

  if (in_match_queue || global_vars->realtime < next_queue_action_time_)
  {
    if (emit_debug_log)
    {
      log_queue_debug(
        "boost skip queued_or_waiting in_match_queue=%d waiting=%d\n",
        in_match_queue ? 1 : 0,
        global_vars->realtime < next_queue_action_time_ ? 1 : 0);
    }
    return;
  }

  log_queue_debug("boost requesting casual queue\n");
  if (request_match_queue(party_client, casual_match_group_default))
  {
    next_queue_action_time_ = global_vars->realtime + auto_queue_interval;
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

  if (config.misc.automation.queue_mode == Misc::Automation::queueing_mode::BOOST)
  {
    queued_once = false;
    queued_from_player_threshold = false;
    leave_for_requeue_requested = false;
    cancel_queue_requested = false;
    was_disconnected = false;
    run_boost_queueing();
    return;
  }

  boost_leave_requested_ = false;
  boost_match_start_time_ = 0.0f;

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
    pup_ipc::client::set_in_casual_queue(false);
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
  bool in_standby = is_in_standby_queue(party_client);
  pup_ipc::client::set_in_casual_queue(
    (in_match_queue || in_standby) && queue_mode == casual_match_group_default);
  if (!in_match_queue && !in_standby)
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
    const bool loading_no_navmesh_requeue = in_game && should_trigger_no_navmesh_requeue();
    const bool loading_requeue_conditions_met =
        loading_player_threshold_requeue || loading_ipc_bot_threshold_requeue || loading_no_navmesh_requeue;

    if ((in_match_queue || in_standby) && !cancel_queue_requested && !loading_requeue_conditions_met)
    {
      log_queue_debug(
        "loading screen active and rq_if requirements not met, leaving queues match_group=%u standby=%d humans=%d ipc_excess=%d no_navmesh=%d\n",
        queue_mode,
        in_standby ? 1 : 0,
        loading_human_players,
        loading_ipc_bot_threshold_requeue ? 1 : 0,
        loading_no_navmesh_requeue ? 1 : 0);
      if (cancel_active_queues(party_client, queue_mode, in_match_queue, in_standby))
      {
        cancel_queue_requested = true;
        queued_from_player_threshold = false;
        next_queue_action_time_ = global_vars->realtime + auto_queue_interval;
      }
      else if (emit_debug_log)
      {
        log_queue_debug("loading screen active but no leave queue request is available\n");
      }
    }

    const float loading_duration = global_vars->realtime - queue_loading_start_time_;
    const bool auto_queue_loading_timeout_hit =
      config.misc.automation.auto_queue && loading_duration >= auto_queue_loading_timeout;
    const bool auto_requeue_loading_timeout_hit =
      config.misc.automation.auto_requeue && loading_duration >= auto_requeue_loading_timeout;

    if (auto_queue_loading_timeout_hit || auto_requeue_loading_timeout_hit)
    {
      const bool used_abandon = abandon_current_match();
      log_queue_debug(
        "loading timeout hit auto_queue=%d auto_requeue=%d duration=%.2f abandon=%d\n",
        auto_queue_loading_timeout_hit ? 1 : 0,
        auto_requeue_loading_timeout_hit ? 1 : 0,
        loading_duration,
        used_abandon ? 1 : 0);
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
  const bool no_navmesh_requeue = in_game && should_trigger_no_navmesh_requeue();
  const bool threshold_requeue = player_threshold_requeue || ipc_bot_threshold_requeue || no_navmesh_requeue;

  if (threshold_requeue)
  {
    if (config.misc.automation.requeue_action == Misc::Automation::requeue_action_mode::LEAVE_AND_REQUEUE)
    {
      if (!leave_for_requeue_requested)
      {
        const bool used_abandon = abandon_current_match();
        log_queue_debug(
          "rq_if hit (%d humans, ipc_excess=%d, no_navmesh=%d), leave_and_requeue lte=%d gte=%d ipc_gt=%d abandon=%d\n",
          human_players,
          ipc_bot_threshold_requeue ? 1 : 0,
          no_navmesh_requeue ? 1 : 0,
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

  if ((in_match_queue || in_standby) && in_game && !threshold_requeue && !cancel_queue_requested)
  {
    const bool had_standby = in_standby;
    if (cancel_active_queues(party_client, queue_mode, in_match_queue, in_standby))
    {
      log_queue_debug("rq_if requirements not met, leaving queues match_group=%u standby=%d\n", queue_mode, had_standby ? 1 : 0);
      cancel_queue_requested = true;
      next_queue_action_time_ = global_vars->realtime + auto_queue_interval;
    }
    else if (emit_debug_log)
    {
      log_queue_debug("rq_if requirements not met but no leave queue request is available\n");
    }
    queued_from_player_threshold = false;
    return;
  }

  if (threshold_requeue)
  {
    if (!in_match_queue && !in_standby && global_vars->realtime >= next_queue_action_time_)
    {
      log_queue_debug(
        "rq_if hit (%d humans, ipc_excess=%d, no_navmesh=%d), requesting rolling queue for match group %u\n",
        human_players,
        ipc_bot_threshold_requeue ? 1 : 0,
        no_navmesh_requeue ? 1 : 0,
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
    if (cancel_standby_queue(party_client))
    {
      log_queue_debug("leaving standby queue\n");
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

}
