/*
/^-----^\   data: 2026-05-05
V  o o  V  file: src/features/automation/autoitem/autoitem.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |     \     )
  || (___\====
*/
#include "features/automation/autoitem/autoitem.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include "core/print.hpp"
#include "core/shared/sigs.hpp"
#include "features/automation/nographics/nographics.hpp"
#include "features/menu/config.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/achievement_mgr.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/steam_runtime.hpp"
#include "libsigscan/libsigscan.h"

namespace autoitem
{

namespace
{

constexpr int primary_slot = 0;
constexpr int secondary_slot = 1;
constexpr int melee_slot = 2;
constexpr int building_slot = 4;
constexpr int pda_slot = 5;
constexpr int pda2_slot = 6;
constexpr int head_slot = 7;
constexpr int misc_slot = 8;
constexpr int action_slot = 9;
constexpr int misc2_slot = 10;
constexpr int taunt_slot = 11;
constexpr int birthday_noisemaker_def = 536;
constexpr int winter_noisemaker_def = 673;
constexpr int fallback_attempt_limit = 3;
constexpr int max_crafting_inputs = 12;
constexpr int pending_pickup_ack_attempt_limit = 6;
constexpr float pending_pickup_ack_retry_seconds = 0.5f;
constexpr std::uint32_t gc_msg_craft = 1002;
constexpr std::uint32_t gc_msg_item_preview_request = 1703;
constexpr std::uint16_t gc_header_version = 1;
constexpr std::uint64_t gc_invalid_job_id = ~0ull;
constexpr std::uint16_t gc_custom_craft_recipe = 0xFFFEu;
constexpr std::uint64_t unequipped_item_id = static_cast<std::uint64_t>(-1);
constexpr int max_item_def_id = 65535;

constexpr std::uintptr_t inventory_item_array_offset = 0x60;
constexpr std::uintptr_t inventory_item_count_offset = 0x70;
constexpr std::uintptr_t inventory_item_stride = 0x150;
constexpr std::uintptr_t inventory_item_id_high_offset = 0x58;
constexpr std::uintptr_t inventory_item_id_low_offset = 0x5C;
constexpr std::uintptr_t inventory_item_def_offset = 0x44;

constexpr int inventory_manager_get_local_inventory_index = 24;
constexpr int inventory_manager_update_inventory_equipped_state_index = 33;
constexpr int inventory_manager_show_items_picked_up_index = 35;

using get_local_inventory_fn = void* (*)(void*);
using update_inventory_equipped_state_fn = void (*)(void*, void*, std::uint64_t, std::uint16_t, std::uint16_t);
using show_items_picked_up_fn = bool (*)(void*, bool, bool, bool);

struct inventory_api
{
  bool initialized = false;
  void* inventory_manager = nullptr;
};

struct achievement_item
{
  int item_def_id;
  int achievement_id;
  const char* name;
};

enum class spec_kind : std::uint8_t
{
  skip,
  unequip,
  single,
  alternatives,
  craft
};

struct parsed_spec
{
  spec_kind kind = spec_kind::skip;
  std::vector<int> defs{};
  std::vector<std::vector<int>> craft_groups{};
  int craft_result = -1;
};

struct slot_task_state
{
  std::string raw{};
  parsed_spec spec{};
  int attempts = 0;
};

enum task_id : std::size_t
{
  task_primary,
  task_secondary,
  task_melee,
  task_building,
  task_pda,
  task_pda2,
  task_action,
  task_taunt,
  task_hat1,
  task_hat2,
  task_hat3,
  task_count
};

struct equip_request_record
{
  bool valid = false;
  std::uint64_t item_id = 0;
};

inventory_api g_inventory_api{};
float g_next_auto_item_time = 0.0f;
std::array<slot_task_state, task_count> g_task_states{};
int g_hat_rotation_offset = 0;
std::unordered_map<int, std::vector<std::uint64_t>> g_item_ids_by_def{};
bool g_item_ids_valid = false;
std::unordered_map<std::uint32_t, equip_request_record> g_last_equip_requests{};
std::string g_cache_level_name{};
int g_cache_class_id = 0;
int g_pending_pickup_ack_attempts = 0;
float g_next_pending_pickup_ack_time = 0.0f;
bool g_initialize_diagnostics_emitted = false;
int g_initialize_retry_count = 0;
float g_next_initialize_retry_time = 0.0f;

constexpr std::array<achievement_item, 41> achievement_items{{
  {45, 1036, "TF_SCOUT_ACHIEVE_PROGRESS1"},
  {44, 1037, "TF_SCOUT_ACHIEVE_PROGRESS2"},
  {46, 1038, "TF_SCOUT_ACHIEVE_PROGRESS3"},
  {128, 1236, "TF_SOLDIER_ACHIEVE_PROGRESS1"},
  {127, 1237, "TF_SOLDIER_ACHIEVE_PROGRESS2"},
  {129, 1238, "TF_SOLDIER_ACHIEVE_PROGRESS3"},
  {39, 1637, "TF_PYRO_ACHIEVE_PROGRESS1"},
  {40, 1638, "TF_PYRO_ACHIEVE_PROGRESS2"},
  {38, 1639, "TF_PYRO_ACHIEVE_PROGRESS3"},
  {131, 1336, "TF_DEMOMAN_ACHIEVE_PROGRESS1"},
  {132, 1337, "TF_DEMOMAN_ACHIEVE_PROGRESS2"},
  {130, 1338, "TF_DEMOMAN_ACHIEVE_PROGRESS3"},
  {42, 1537, "TF_HEAVY_ACHIEVE_PROGRESS1"},
  {41, 1538, "TF_HEAVY_ACHIEVE_PROGRESS2"},
  {43, 1539, "TF_HEAVY_ACHIEVE_PROGRESS3"},
  {141, 1801, "TF_ENGINEER_ACHIEVE_PROGRESS1"},
  {142, 1802, "TF_ENGINEER_ACHIEVE_PROGRESS2"},
  {140, 1803, "TF_ENGINEER_ACHIEVE_PROGRESS3"},
  {36, 1437, "TF_MEDIC_ACHIEVE_PROGRESS1"},
  {35, 1438, "TF_MEDIC_ACHIEVE_PROGRESS2"},
  {37, 1439, "TF_MEDIC_ACHIEVE_PROGRESS3"},
  {56, 1136, "TF_SNIPER_ACHIEVE_PROGRESS1"},
  {58, 1137, "TF_SNIPER_ACHIEVE_PROGRESS2"},
  {57, 1138, "TF_SNIPER_ACHIEVE_PROGRESS3"},
  {61, 1735, "TF_SPY_ACHIEVE_PROGRESS1"},
  {60, 1736, "TF_SPY_ACHIEVE_PROGRESS2"},
  {59, 1737, "TF_SPY_ACHIEVE_PROGRESS3"},
  {1123, 1928, "TF_HALLOWEEN_DOOMSDAY_MILESTONE"},
  {940, 1902, "TF_HALLOWEEN_DOMINATE_FOR_HAT"},
  {115, 1901, "TF_HALLOWEEN_COLLECT_PUMPKINS"},
  {278, 1906, "TF_HALLOWEEN_BOSS_KILL"},
  {302, 2006, "TF_REPLAY_YOUTUBE_VIEWS_TIER2"},
  {668, 2212, "TF_MAPS_FOUNDRY_ACHIEVE_PROGRESS1"},
  {756, 2412, "TF_MAPS_DOOMSDAY_ACHIEVE_PROGRESS1"},
  {941, 1912, "TF_HALLOWEEN_MERASMUS_COLLECT_LOOT"},
  {581, 1911, "TF_HALLOWEEN_LOOT_ISLAND"},
  {744, 156, "TF_DOMINATE_FOR_GOGGLES"},
  {1164, 167, "TF_PASS_TIME_GRIND"},
  {1169, 166, "TF_PASS_TIME_HAT"},
  {1170, 166, "TF_PASS_TIME_HAT"},
  {267, 1909, "TF_HALLOWEEN_BOSS_KILL_MELEE"},
}};

void debug_log(const char* fmt, ...)
{
  if (!config.misc.automation.auto_item_debug)
  {
    return;
  }

  va_list args{};
  va_start(args, fmt);
  print("[autoitem] ");
  cathook::core::vlog_raw(fmt, args);
  va_end(args);
}

void error_log(const char* fmt, ...)
{
  va_list args{};
  va_start(args, fmt);
  print("[autoitem][error] ");
  cathook::core::vlog_raw(fmt, args);
  va_end(args);
}

std::string trim(std::string_view value)
{
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r' || value.front() == '\n'))
  {
    value.remove_prefix(1);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' || value.back() == '\n'))
  {
    value.remove_suffix(1);
  }
  return std::string{ value };
}

std::vector<std::string> split(std::string_view value, const char delimiter)
{
  std::vector<std::string> pieces{};
  std::size_t cursor = 0;
  while (cursor <= value.size())
  {
    const std::size_t next = value.find(delimiter, cursor);
    const std::size_t length = next == std::string_view::npos ? value.size() - cursor : next - cursor;
    auto piece = trim(value.substr(cursor, length));
    if (!piece.empty())
    {
      pieces.emplace_back(std::move(piece));
    }

    if (next == std::string_view::npos)
    {
      break;
    }
    cursor = next + 1;
  }
  return pieces;
}

std::vector<std::string> split_craft_groups(std::string_view value)
{
  std::vector<std::string> groups{};
  std::size_t cursor = 0;
  while (cursor <= value.size())
  {
    const auto next_semicolon = value.find(';', cursor);
    const auto next_dash = value.find('-', cursor);
    const auto next = std::min(
      next_semicolon == std::string_view::npos ? value.size() : next_semicolon,
      next_dash == std::string_view::npos ? value.size() : next_dash);
    auto group = trim(value.substr(cursor, next - cursor));
    if (!group.empty())
    {
      groups.emplace_back(std::move(group));
    }
    if (next >= value.size())
    {
      break;
    }
    cursor = next + 1;
  }
  return groups;
}

std::optional<int> parse_int(std::string_view value)
{
  auto text = trim(value);
  if (text.empty())
  {
    return std::nullopt;
  }

  std::size_t cursor = 0;
  if (text[0] == '+' || text[0] == '-')
  {
    cursor = 1;
  }
  if (cursor >= text.size() || text.find_first_not_of("0123456789", cursor) != std::string::npos)
  {
    return std::nullopt;
  }

  try
  {
    return std::stoi(text);
  }
  catch (...)
  {
    return std::nullopt;
  }
}

template <typename value_type>
value_type read_unaligned(const void* address)
{
  value_type value{};
  std::memcpy(&value, address, sizeof(value));
  return value;
}

std::uint8_t* decode_rip_relative(std::uint8_t* instruction, const int displacement_offset, const int instruction_size)
{
  const auto displacement = read_unaligned<std::int32_t>(instruction + displacement_offset);
  return instruction + instruction_size + displacement;
}

std::optional<int> parse_item_def(std::string_view value)
{
  const auto parsed = parse_int(value);
  if (!parsed || *parsed < 0 || *parsed > max_item_def_id)
  {
    return std::nullopt;
  }
  return parsed;
}

std::vector<int> parse_item_def_list(std::string_view value, const char delimiter)
{
  const auto pieces = split(value, delimiter);
  std::vector<int> parsed_values{};
  parsed_values.reserve(pieces.size());
  for (const auto& piece : pieces)
  {
    const auto parsed = parse_item_def(piece);
    if (!parsed)
    {
      return {};
    }
    parsed_values.emplace_back(*parsed);
  }
  return parsed_values;
}

parsed_spec parse_spec(std::string_view raw)
{
  parsed_spec parsed{};
  const std::string cleaned = trim(raw);
  if (cleaned.empty())
  {
    return parsed;
  }

  if (cleaned == "-1")
  {
    parsed.kind = spec_kind::unequip;
    return parsed;
  }

  const std::size_t result_separator = cleaned.find('-');
  const bool is_craft_spec =
    cleaned.find(',') != std::string::npos ||
    cleaned.find(';') != std::string::npos ||
    (result_separator != std::string::npos && result_separator > 0);

  if (is_craft_spec)
  {
    if (result_separator == std::string::npos)
    {
      debug_log("craft spec '%s' has no result item\n", cleaned.c_str());
      return parsed;
    }

    const auto result = parse_item_def(cleaned.substr(result_separator + 1));
    if (!result)
    {
      debug_log("craft spec '%s' has invalid result item\n", cleaned.c_str());
      return parsed;
    }

    const auto result_token = std::to_string(*result);
    for (const auto& group : split_craft_groups(cleaned))
    {
      if (group == result_token)
      {
        continue;
      }

      auto inputs = parse_item_def_list(group, ',');
      if (inputs.empty())
      {
        debug_log("invalid crafting group '%s'\n", group.c_str());
        continue;
      }
      parsed.craft_groups.emplace_back(std::move(inputs));
    }

    if (parsed.craft_groups.empty())
    {
      debug_log("craft spec '%s' has no usable input groups\n", cleaned.c_str());
      return parsed;
    }

    parsed.kind = spec_kind::craft;
    parsed.craft_result = *result;
    return parsed;
  }

  if (cleaned.find('/') != std::string::npos)
  {
    auto defs = parse_item_def_list(cleaned, '/');
    if (defs.empty())
    {
      debug_log("invalid alternative weapon spec '%s'\n", cleaned.c_str());
      return parsed;
    }

    parsed.kind = spec_kind::alternatives;
    parsed.defs = std::move(defs);
    return parsed;
  }

  const auto def = parse_item_def(cleaned);
  if (!def)
  {
    debug_log("invalid item spec '%s'\n", cleaned.c_str());
    return parsed;
  }

  parsed.kind = spec_kind::single;
  parsed.defs.push_back(*def);
  return parsed;
}

const achievement_item* find_achievement_item(const int item_def_id)
{
  for (const auto& item : achievement_items)
  {
    if (item.item_def_id == item_def_id)
    {
      return &item;
    }
  }
  return nullptr;
}

SteamClient* resolve_steam_client()
{
  return steam_runtime::resolve_steam_client();
}

steam_user_stats* resolve_steam_user_stats()
{
  return steam_runtime::resolve_steam_user_stats();
}

achievement_manager* resolve_achievement_manager()
{
  if (engine == nullptr)
  {
    return nullptr;
  }

  return engine->get_achievement_manager();
}

achievement* find_achievement_by_id(const int achievement_id)
{
  auto* manager = resolve_achievement_manager();
  if (manager == nullptr)
  {
    return nullptr;
  }

  const int count = manager->get_achievement_count();
  for (int index = 0; index < count; ++index)
  {
    auto* entry = manager->get_achievement_by_index(index);
    if (entry != nullptr && entry->get_achievement_id() == achievement_id)
    {
      return entry;
    }
  }

  return nullptr;
}

bool has_achievement(const int achievement_id)
{
  auto* entry = find_achievement_by_id(achievement_id);
  return entry != nullptr && entry->is_achieved();
}

bool inventory_api_resolved()
{
  return g_inventory_api.inventory_manager != nullptr;
}

bool api_ready()
{
  initialize();
  return inventory_api_resolved();
}

void* get_local_inventory()
{
  if (!api_ready())
  {
    return nullptr;
  }

  auto** vtable = *reinterpret_cast<void***>(g_inventory_api.inventory_manager);
  if (vtable == nullptr)
  {
    return nullptr;
  }

  auto call_get_local_inventory = reinterpret_cast<get_local_inventory_fn>(vtable[inventory_manager_get_local_inventory_index]);
  if (call_get_local_inventory == nullptr)
  {
    return nullptr;
  }

  return call_get_local_inventory(g_inventory_api.inventory_manager);
}

std::uint32_t read_item_def_id(std::uint8_t* item)
{
  return read_unaligned<std::uint16_t>(item + inventory_item_def_offset);
}

std::uint64_t read_item_id(std::uint8_t* item)
{
  const auto high = read_unaligned<std::uint32_t>(item + inventory_item_id_high_offset);
  const auto low = read_unaligned<std::uint32_t>(item + inventory_item_id_low_offset);
  return (static_cast<std::uint64_t>(high) << 32u) | static_cast<std::uint64_t>(low);
}

void rebuild_inventory_index()
{
  g_item_ids_by_def.clear();
  g_item_ids_valid = false;

  auto* inventory = reinterpret_cast<std::uint8_t*>(get_local_inventory());
  if (inventory == nullptr)
  {
    return;
  }

  auto* item_array = read_unaligned<std::uint8_t*>(inventory + inventory_item_array_offset);
  const int item_count = read_unaligned<int>(inventory + inventory_item_count_offset);
  if (item_array == nullptr || item_count <= 0 || item_count > 20000)
  {
    return;
  }

  g_item_ids_by_def.reserve(static_cast<std::size_t>(item_count));
  for (int index = 0; index < item_count; ++index)
  {
    auto* item = item_array + (static_cast<std::uintptr_t>(index) * inventory_item_stride);
    const int item_def = static_cast<int>(read_item_def_id(item));
    const auto item_id = read_item_id(item);
    if (item_def <= 0 || item_id == 0)
    {
      continue;
    }

    auto& item_ids = g_item_ids_by_def[item_def];
    if (item_ids.size() < max_crafting_inputs)
    {
      item_ids.emplace_back(item_id);
    }
  }

  g_item_ids_valid = true;
}

const std::vector<std::uint64_t>* find_item_ids_of_def(const int item_def_id)
{
  if (!g_item_ids_valid)
  {
    return nullptr;
  }

  const auto found = g_item_ids_by_def.find(item_def_id);
  if (found == g_item_ids_by_def.end() || found->second.empty())
  {
    return nullptr;
  }

  return &found->second;
}

bool has_item_def(const int item_def_id)
{
  return find_item_ids_of_def(item_def_id) != nullptr;
}

std::optional<std::uint64_t> first_owned_item_id(const int item_def_id)
{
  const auto* item_ids = find_item_ids_of_def(item_def_id);
  if (item_ids == nullptr)
  {
    return std::nullopt;
  }

  return item_ids->front();
}

void append_u16(std::vector<std::uint8_t>& output, const std::uint16_t value)
{
  output.emplace_back(static_cast<std::uint8_t>(value & 0xFFu));
  output.emplace_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
}

void append_u32(std::vector<std::uint8_t>& output, const std::uint32_t value)
{
  output.emplace_back(static_cast<std::uint8_t>(value & 0xFFu));
  output.emplace_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
  output.emplace_back(static_cast<std::uint8_t>((value >> 16u) & 0xFFu));
  output.emplace_back(static_cast<std::uint8_t>((value >> 24u) & 0xFFu));
}

void append_u64(std::vector<std::uint8_t>& output, const std::uint64_t value)
{
  for (int index = 0; index < 8; ++index)
  {
    output.emplace_back(static_cast<std::uint8_t>((value >> (index * 8)) & 0xFFu));
  }
}

void append_gc_header(std::vector<std::uint8_t>& output)
{
  append_u16(output, gc_header_version);
  append_u64(output, gc_invalid_job_id);
  append_u64(output, gc_invalid_job_id);
}

bool send_gc_message(const std::uint32_t message_type, const std::vector<std::uint8_t>& payload)
{
  steam_game_coordinator* coordinator = steam_runtime::resolve_steam_game_coordinator();
  if (coordinator == nullptr || payload.empty())
  {
    error_log("steam game coordinator unavailable for message %u\n", message_type);
    return false;
  }

  return coordinator->send_message(
    message_type,
    payload.data(),
    static_cast<std::uint32_t>(payload.size()));
}

bool send_equip_item_request(const int class_id, const int slot, const std::uint64_t item_id)
{
  if (!api_ready())
  {
    return false;
  }

  void* local_inventory = get_local_inventory();
  if (local_inventory == nullptr)
  {
    return false;
  }

  void** vtable = *reinterpret_cast<void***>(g_inventory_api.inventory_manager);
  if (vtable == nullptr)
  {
    return false;
  }

  update_inventory_equipped_state_fn update_inventory_equipped_state =
    reinterpret_cast<update_inventory_equipped_state_fn>(
      vtable[inventory_manager_update_inventory_equipped_state_index]);
  if (update_inventory_equipped_state == nullptr)
  {
    return false;
  }

  update_inventory_equipped_state(
    g_inventory_api.inventory_manager,
    local_inventory,
    item_id,
    static_cast<std::uint16_t>(class_id),
    static_cast<std::uint16_t>(slot));
  return true;
}

bool send_preview_item_request(const int item_def_id)
{
  std::vector<std::uint8_t> payload{};
  payload.reserve(22);
  append_gc_header(payload);
  append_u32(payload, static_cast<std::uint32_t>(item_def_id));
  return send_gc_message(gc_msg_item_preview_request, payload);
}

bool send_craft_request(const std::vector<std::uint64_t>& item_ids)
{
  if (item_ids.empty() || item_ids.size() > max_crafting_inputs)
  {
    return false;
  }

  std::vector<std::uint8_t> payload{};
  payload.reserve(22 + (item_ids.size() * sizeof(std::uint64_t)));
  append_gc_header(payload);
  append_u16(payload, gc_custom_craft_recipe);
  append_u16(payload, static_cast<std::uint16_t>(item_ids.size()));
  for (const std::uint64_t item_id : item_ids)
  {
    append_u64(payload, item_id);
  }
  return send_gc_message(gc_msg_craft, payload);
}

int corrected_loadout_slot(const int class_id, const int slot)
{
  if (class_id != static_cast<int>(tf_class::SPY))
  {
    return slot;
  }

  if (slot == primary_slot)
  {
    return secondary_slot;
  }
  if (slot == secondary_slot)
  {
    return pda2_slot;
  }

  return slot;
}

std::uint32_t equip_cache_key(const int class_id, const int loadout_slot)
{
  return static_cast<std::uint32_t>(class_id) * 100u + static_cast<std::uint32_t>(loadout_slot);
}

bool request_equip(const int class_id, const int slot, const std::uint64_t item_id)
{
  const int loadout_slot = corrected_loadout_slot(class_id, slot);
  const auto key = equip_cache_key(class_id, loadout_slot);

  if (const auto found = g_last_equip_requests.find(key);
      found != g_last_equip_requests.end() && found->second.valid && found->second.item_id == item_id)
  {
    return true;
  }

  if (!api_ready() || get_local_inventory() == nullptr)
  {
    debug_log("local inventory not ready, skipping equip class=%d slot=%d\n", class_id, loadout_slot);
    return false;
  }

  if (!send_equip_item_request(class_id, loadout_slot, item_id))
  {
    error_log("equip failed class=%d slot=%d item_id=%llu\n",
      class_id, loadout_slot, static_cast<unsigned long long>(item_id));
    return false;
  }

  g_last_equip_requests.insert_or_assign(key, equip_request_record{ true, item_id });
  debug_log("equipped class=%d slot=%d item_id=%llu\n",
    class_id, loadout_slot, static_cast<unsigned long long>(item_id));
  return true;
}

void reset_runtime_caches()
{
  g_last_equip_requests.clear();
  for (auto& state : g_task_states)
  {
    state.attempts = 0;
  }
}

void refresh_runtime_caches(const int class_id)
{
  const char* raw_level = engine != nullptr ? engine->get_level_name() : nullptr;
  const std::string_view level_name = raw_level != nullptr ? raw_level : "";

  if (level_name != std::string_view{ g_cache_level_name } || class_id != g_cache_class_id)
  {
    reset_runtime_caches();
    g_cache_level_name.assign(level_name);
    g_cache_class_id = class_id;
  }
}

bool acknowledge_new_items_without_panel()
{
  initialize();
  if (g_inventory_api.inventory_manager == nullptr)
  {
    return false;
  }

  auto** vtable = *reinterpret_cast<void***>(g_inventory_api.inventory_manager);
  if (vtable == nullptr)
  {
    return false;
  }

  auto show_items_picked_up =
    reinterpret_cast<show_items_picked_up_fn>(vtable[inventory_manager_show_items_picked_up_index]);
  if (show_items_picked_up == nullptr)
  {
    return false;
  }

  debug_log("acknowledging new items without pickup panel\n");
  return show_items_picked_up(g_inventory_api.inventory_manager, true, true, true);
}

void queue_pending_pickup_ack()
{
  if (!nographics::is_enabled())
  {
    return;
  }

  g_pending_pickup_ack_attempts = pending_pickup_ack_attempt_limit;
  g_next_pending_pickup_ack_time = global_vars != nullptr
    ? global_vars->realtime + pending_pickup_ack_retry_seconds
    : 0.0f;
}

void process_pending_pickup_ack()
{
  if (g_pending_pickup_ack_attempts <= 0)
  {
    return;
  }

  if (!nographics::is_enabled())
  {
    g_pending_pickup_ack_attempts = 0;
    g_next_pending_pickup_ack_time = 0.0f;
    return;
  }

  if (global_vars == nullptr || global_vars->realtime < g_next_pending_pickup_ack_time)
  {
    return;
  }

  acknowledge_new_items_without_panel();
  --g_pending_pickup_ack_attempts;
  if (g_pending_pickup_ack_attempts > 0)
  {
    g_next_pending_pickup_ack_time = global_vars->realtime + pending_pickup_ack_retry_seconds;
  }
  else
  {
    g_next_pending_pickup_ack_time = 0.0f;
  }
}

void trigger_new_item_notification()
{
  if (nographics::is_enabled())
  {
    acknowledge_new_items_without_panel();
    queue_pending_pickup_ack();
    return;
  }

  if (engine != nullptr)
  {
    engine->client_cmd_unrestricted("cl_trigger_first_notification");
  }
}

bool get_item(const int item_def_id, const bool allow_rent)
{
  debug_log("trying to get item def %d\n", item_def_id);
  if (const auto* item = find_achievement_item(item_def_id))
  {
    if (unlock_achievement_by_id(item->achievement_id))
    {
      trigger_new_item_notification();
      return true;
    }
    return false;
  }

  if (allow_rent)
  {
    return rent_item(item_def_id);
  }

  debug_log("item def %d is not achievement-backed and rent is disabled\n", item_def_id);
  return false;
}

void acquire_item(const int item_def_id, const bool allow_rent, slot_task_state& state)
{
  if (state.attempts >= fallback_attempt_limit)
  {
    debug_log("stopping acquisition of item def %d after %d attempt(s)\n", item_def_id, state.attempts);
    return;
  }

  ++state.attempts;
  get_item(item_def_id, allow_rent);
}

void run_craft_task(slot_task_state& state, const int class_id, const int slot)
{
  const int result_def = state.spec.craft_result;
  if (auto item_id = first_owned_item_id(result_def))
  {
    state.attempts = 0;
    request_equip(class_id, slot, *item_id);
    return;
  }

  if (state.attempts >= fallback_attempt_limit)
  {
    debug_log("crafting paused for result def %d after %d attempt(s)\n", result_def, state.attempts);
    return;
  }

  for (const auto& inputs : state.spec.craft_groups)
  {
    bool ready = true;
    for (const int input_def : inputs)
    {
      if (has_item_def(input_def))
      {
        continue;
      }

      const auto* achievement_backed = find_achievement_item(input_def);
      if (achievement_backed == nullptr || has_achievement(achievement_backed->achievement_id))
      {
        debug_log("cannot obtain crafting material item def %d, skipping group\n", input_def);
        ready = false;
        break;
      }

      ++state.attempts;
      get_item(input_def, false);
      return;
    }

    if (ready && craft_items(inputs))
    {
      ++state.attempts;
      trigger_new_item_notification();
      return;
    }
  }
}

void run_slot_task(
  const std::size_t task,
  const std::string& raw,
  const int class_id,
  const int slot,
  const bool allow_rent)
{
  auto& state = g_task_states[task];
  if (state.raw != raw)
  {
    state.raw = raw;
    state.spec = parse_spec(raw);
    state.attempts = 0;
  }

  switch (state.spec.kind)
  {
    case spec_kind::skip:
      return;

    case spec_kind::unequip:
      request_equip(class_id, slot, unequipped_item_id);
      return;

    case spec_kind::single:
    {
      const int item_def = state.spec.defs.front();
      if (auto item_id = first_owned_item_id(item_def))
      {
        state.attempts = 0;
        request_equip(class_id, slot, *item_id);
        return;
      }
      acquire_item(item_def, allow_rent, state);
      return;
    }

    case spec_kind::alternatives:
    {
      const auto& defs = state.spec.defs;
      for (const int item_def : defs)
      {
        if (auto item_id = first_owned_item_id(item_def))
        {
          state.attempts = 0;
          request_equip(class_id, slot, *item_id);
          return;
        }
      }

      const int wanted_def = state.attempts >= fallback_attempt_limit && defs.size() > 1 ? defs.back() : defs.front();
      acquire_item(wanted_def, true, state);
      return;
    }

    case spec_kind::craft:
      run_craft_task(state, class_id, slot);
      return;
  }
}

int seasonal_noisemaker_item_def()
{
  const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm time_info{};
  if (auto* local_time = std::localtime(&now))
  {
    time_info = *local_time;
  }

  const int day = time_info.tm_mday;
  const int month = time_info.tm_mon + 1;
  if ((month == 12 && day >= 1) || (month == 1 && day <= 12))
  {
    return winter_noisemaker_def;
  }

  return birthday_noisemaker_def;
}

void log_resolution_status(const char* tag)
{
  print("[autoitem] %s after %d attempt(s) manager=%p\n",
    tag,
    g_initialize_retry_count,
    g_inventory_api.inventory_manager);
}

}

void initialize()
{
  if (inventory_api_resolved()) return;

  if (global_vars != nullptr && global_vars->realtime < g_next_initialize_retry_time)
  {
    return;
  }
  g_next_initialize_retry_time = global_vars != nullptr ? global_vars->realtime + 2.0f : 0.0f;

  g_inventory_api.initialized = true;
  ++g_initialize_retry_count;

  if (g_inventory_api.inventory_manager == nullptr)
  {
    auto* initializer = reinterpret_cast<std::uint8_t*>(sigscan_module("client.so", sigs::tf_inventory_manager_initializer));
    if (initializer != nullptr)
    {
      g_inventory_api.inventory_manager = decode_rip_relative(initializer + 1, 3, 7);
    }
  }

  if (!inventory_api_resolved())
  {
    if (g_initialize_retry_count == 1 || g_initialize_retry_count % 30 == 0)
    {
      log_resolution_status("signature scan incomplete");
    }
    return;
  }

  if (!g_initialize_diagnostics_emitted)
  {
    g_initialize_diagnostics_emitted = true;
    log_resolution_status("resolved");
  }
}

void on_tick()
{
  if (!config.misc.automation.auto_item)
  {
    g_next_auto_item_time = 0.0f;
  }

  if (engine == nullptr || global_vars == nullptr || !engine->is_in_game())
  {
    return;
  }

  process_pending_pickup_ack();

  if (!config.misc.automation.auto_item || entity_list == nullptr)
  {
    return;
  }

  const int interval_ms = std::clamp(config.misc.automation.auto_item_interval_ms, 1000, 120000);
  if (global_vars->realtime < g_next_auto_item_time)
  {
    return;
  }

  g_next_auto_item_time = global_vars->realtime + (static_cast<float>(interval_ms) / 1000.0f);

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr)
  {
    return;
  }

  const int class_id = static_cast<int>(localplayer->get_tf_class());
  if (class_id <= static_cast<int>(tf_class::UNDEFINED) || class_id > static_cast<int>(tf_class::ENGINEER))
  {
    return;
  }

  if (!api_ready())
  {
    return;
  }

  refresh_runtime_caches(class_id);
  rebuild_inventory_index();

  const auto& settings = config.misc.automation;
  if (settings.auto_item_weapons)
  {
    run_slot_task(task_primary, settings.auto_item_primary, class_id, primary_slot, true);
    run_slot_task(task_secondary, settings.auto_item_secondary, class_id, secondary_slot, true);
    run_slot_task(task_melee, settings.auto_item_melee, class_id, melee_slot, true);
  }

  if (settings.auto_item_equipment)
  {
    run_slot_task(task_building, settings.auto_item_building, class_id, building_slot, true);
    run_slot_task(task_pda, settings.auto_item_pda, class_id, pda_slot, true);
    run_slot_task(task_pda2, settings.auto_item_pda2, class_id, pda2_slot, true);
    run_slot_task(task_action, settings.auto_item_action, class_id, action_slot, true);
    run_slot_task(task_taunt, settings.auto_item_taunt, class_id, taunt_slot, true);
  }

  if (settings.auto_item_hats)
  {
    constexpr std::array<int, 3> hat_slots{ head_slot, misc_slot, misc2_slot };
    const std::array<const std::string*, 3> hat_specs{
      &settings.auto_item_hat1,
      &settings.auto_item_hat2,
      &settings.auto_item_hat3
    };
    for (int index = 0; index < 3; ++index)
    {
      const int rotated_slot = hat_slots[static_cast<std::size_t>((g_hat_rotation_offset + index) % 3)];
      const auto task = static_cast<std::size_t>(task_hat1 + index);
      run_slot_task(task, *hat_specs[static_cast<std::size_t>(index)], class_id, rotated_slot, false);
    }
    g_hat_rotation_offset = (g_hat_rotation_offset + 1) % 3;
  }

  const bool action_slot_claimed =
    settings.auto_item_equipment && g_task_states[task_action].spec.kind != spec_kind::skip;
  if (settings.auto_item_noisemaker && !action_slot_claimed)
  {
    const int item_def = seasonal_noisemaker_item_def();
    if (auto item_id = first_owned_item_id(item_def))
    {
      request_equip(class_id, action_slot, *item_id);
    }
  }
}

bool rent_item(const int item_def_id)
{
  if (item_def_id <= 0)
  {
    return false;
  }

  debug_log("requesting preview item def %d\n", item_def_id);
  if (!send_preview_item_request(item_def_id))
  {
    return false;
  }
  queue_pending_pickup_ack();
  return true;
}

bool craft_items(const std::vector<int>& item_def_ids)
{
  if (!api_ready() || item_def_ids.empty() || item_def_ids.size() > max_crafting_inputs)
  {
    return false;
  }

  if (!g_item_ids_valid)
  {
    rebuild_inventory_index();
  }

  std::vector<std::uint64_t> selected_item_ids{};
  selected_item_ids.reserve(item_def_ids.size());
  for (const int item_def_id : item_def_ids)
  {
    const auto* candidates = find_item_ids_of_def(item_def_id);
    if (candidates == nullptr)
    {
      debug_log("cannot craft, missing unique item def %d\n", item_def_id);
      return false;
    }

    const auto selected = std::find_if(candidates->begin(), candidates->end(), [&](const std::uint64_t item_id)
    {
      return std::find(selected_item_ids.begin(), selected_item_ids.end(), item_id) == selected_item_ids.end();
    });

    if (selected == candidates->end())
    {
      debug_log("cannot craft, no spare item instance of def %d\n", item_def_id);
      return false;
    }

    selected_item_ids.emplace_back(*selected);
  }

  if (!send_craft_request(selected_item_ids))
  {
    return false;
  }

  queue_pending_pickup_ack();
  return true;
}

bool unlock_achievement_by_id(const int achievement_id)
{
  auto* manager = resolve_achievement_manager();
  if (manager == nullptr)
  {
    error_log("achievement manager unavailable, cannot award id=%d\n", achievement_id);
    return false;
  }

  auto* entry = find_achievement_by_id(achievement_id);
  if (entry == nullptr)
  {
    error_log("achievement id=%d not found in manager\n", achievement_id);
    return false;
  }

  auto* stats = resolve_steam_user_stats();
  if (stats != nullptr)
  {
    stats->request_current_stats();
  }

  if (entry->is_achieved())
  {
    debug_log("achievement id=%d already achieved, queuing item ack\n", achievement_id);
    queue_pending_pickup_ack();
    return true;
  }

  print("[autoitem] awarding achievement id=%d (%s)\n", achievement_id, entry->get_name() ? entry->get_name() : "?");
  manager->award_achievement(achievement_id);
  if (stats != nullptr)
  {
    stats->store_stats();
  }
  queue_pending_pickup_ack();
  return true;
}

bool lock_achievement_by_id(const int achievement_id)
{
  auto* entry = find_achievement_by_id(achievement_id);
  if (entry == nullptr)
  {
    return false;
  }

  const auto* name = entry->get_name();
  if (name == nullptr || name[0] == '\0')
  {
    return false;
  }

  auto* stats = resolve_steam_user_stats();
  if (stats == nullptr)
  {
    return false;
  }

  stats->request_current_stats();
  const bool cleared = stats->clear_achievement(name);
  stats->store_stats();
  stats->request_current_stats();
  return cleared;
}

bool dump_achievements(const char* path)
{
  auto* manager = resolve_achievement_manager();
  if (manager == nullptr || path == nullptr || path[0] == '\0')
  {
    return false;
  }

  std::ofstream output{ path, std::ios::trunc };
  if (!output.is_open())
  {
    return false;
  }

  const int count = manager->get_achievement_count();
  for (int index = 0; index < count; ++index)
  {
    auto* entry = manager->get_achievement_by_index(index);
    if (entry == nullptr)
    {
      continue;
    }

    output << '[' << index << "] " << entry->get_name() << ' ' << entry->get_achievement_id() << '\n';
  }

  return output.good();
}

}
