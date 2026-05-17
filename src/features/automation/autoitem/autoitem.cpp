/*
/^-----^\   data: 2026-05-05
V  o o  V  file: src/features/automation/autoitem/autoitem.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "features/automation/autoitem/autoitem.hpp"

#include <algorithm>
#include <array>
#include <charconv>
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
#include <vector>

#include "core/memory/byte_patch.hpp"
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

void* get_interface(const char* lib_path, const char* version);

namespace autoitem
{

namespace
{

constexpr int primary_slot = 0;
constexpr int secondary_slot = 1;
constexpr int melee_slot = 2;
constexpr int noisemaker_slot = 9;
constexpr int birthday_noisemaker_def = 536;
constexpr int winter_noisemaker_def = 673;
constexpr int fallback_attempt_limit = 3;
constexpr int max_crafting_inputs = 12;
constexpr int pending_pickup_ack_attempt_limit = 6;
constexpr float pending_pickup_ack_retry_seconds = 0.5f;

constexpr std::uintptr_t inventory_item_array_offset = 0x60;
constexpr std::uintptr_t inventory_item_count_offset = 0x70;
constexpr std::uintptr_t inventory_item_stride = 0x150;
constexpr std::uintptr_t inventory_item_id_high_offset = 0x58;
constexpr std::uintptr_t inventory_item_id_low_offset = 0x5C;
constexpr std::uintptr_t inventory_item_def_offset = 0x64;

constexpr std::uintptr_t crafting_panel_input_items_offset = 0x310;
constexpr std::uintptr_t crafting_panel_recipe_offset = 0x450;
constexpr std::size_t crafting_panel_size = 0x51C;
constexpr std::uintptr_t crafting_ui_skip_patch_offset = 0x219;
constexpr std::uint8_t crafting_ui_skip_patch[] = { 0xE9, 0x84, 0x00, 0x00, 0x00 };
constexpr int inventory_manager_get_local_inventory_index = 24;
constexpr int inventory_manager_show_items_picked_up_index = 35;
constexpr std::uintptr_t inventory_manager_schema_offset = 0x60;


using get_first_item_of_item_def_fn = void* (*)(void*, int);
using equip_item_in_loadout_fn = bool (*)(void*, int, int, std::uint64_t);
using do_preview_item_fn = void (*)(void*, int);
using craft_custom_fn = void (*)(void*);
using get_local_inventory_fn = void* (*)(void*);
using show_items_picked_up_fn = bool (*)(void*, bool, bool, bool);

struct inventory_api
{
  bool initialized = false;
  void* inventory_manager = nullptr;
  get_first_item_of_item_def_fn get_first_item_of_item_def = nullptr;
  equip_item_in_loadout_fn equip_item_in_loadout = nullptr;
  do_preview_item_fn do_preview_item = nullptr;
  craft_custom_fn craft_custom = nullptr;
};

struct achievement_item
{
  int item_def_id;
  int achievement_id;
  const char* name;
};

struct fallback_state
{
  std::string spec{};
  int attempts = 0;
};

inventory_api g_inventory_api{};
float g_next_auto_item_time = 0.0f;
std::array<fallback_state, 3> g_fallback_states{};
int g_hat_rotation_offset = 0;
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

  int parsed = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
  {
    return std::nullopt;
  }
  return parsed;
}

template <typename value_type>
value_type read_unaligned(const void* address)
{
  value_type value{};
  std::memcpy(&value, address, sizeof(value));
  return value;
}

template <typename value_type>
void write_unaligned(void* address, const value_type& value)
{
  std::memcpy(address, &value, sizeof(value));
}

std::vector<int> parse_int_list(std::string_view value, const char delimiter)
{
  const auto pieces = split(value, delimiter);
  std::vector<int> parsed_values{};
  parsed_values.reserve(pieces.size());
  for (const auto& piece : pieces)
  {
    const auto parsed = parse_int(piece);
    if (!parsed)
    {
      return {};
    }
    parsed_values.emplace_back(*parsed);
  }
  return parsed_values;
}

std::uint8_t* decode_rip_relative(std::uint8_t* instruction, const int displacement_offset, const int instruction_size)
{
  const auto displacement = read_unaligned<std::int32_t>(instruction + displacement_offset);
  return instruction + instruction_size + displacement;
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
  return g_inventory_api.inventory_manager != nullptr &&
         g_inventory_api.get_first_item_of_item_def != nullptr &&
         g_inventory_api.equip_item_in_loadout != nullptr &&
         g_inventory_api.do_preview_item != nullptr &&
         g_inventory_api.craft_custom != nullptr;
}

bool api_ready()
{
  initialize();
  return inventory_api_resolved();
}

bool inventory_schema_ready()
{
  if (g_inventory_api.inventory_manager == nullptr) return false;
  auto* base = reinterpret_cast<std::uint8_t*>(g_inventory_api.inventory_manager);
  auto* schema = read_unaligned<void*>(base + inventory_manager_schema_offset);
  if (schema == nullptr) return false;
  auto* schema_inner = read_unaligned<void*>(reinterpret_cast<std::uint8_t*>(schema) + sizeof(void*));
  return schema_inner != nullptr;
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
  const auto raw_def = read_unaligned<std::uint32_t>(item + inventory_item_def_offset);
  if ((raw_def & 0x40000000u) != 0)
  {
    return 0;
  }
  return raw_def & 0xFFFFu;
}

std::uint64_t read_item_id(std::uint8_t* item)
{
  const auto high = read_unaligned<std::uint32_t>(item + inventory_item_id_high_offset);
  const auto low = read_unaligned<std::uint32_t>(item + inventory_item_id_low_offset);
  return (static_cast<std::uint64_t>(high) << 32u) | static_cast<std::uint64_t>(low);
}

std::vector<std::uint64_t> get_item_ids_of_item_def(const int item_def_id)
{
  std::vector<std::uint64_t> item_ids{};
  auto* inventory = reinterpret_cast<std::uint8_t*>(get_local_inventory());
  if (inventory == nullptr)
  {
    return item_ids;
  }

  auto* item_array = read_unaligned<std::uint8_t*>(inventory + inventory_item_array_offset);
  const int item_count = read_unaligned<int>(inventory + inventory_item_count_offset);
  if (item_array == nullptr || item_count <= 0 || item_count > 20000)
  {
    return item_ids;
  }

  item_ids.reserve(static_cast<std::size_t>(std::min(item_count, 64)));
  for (int index = 0; index < item_count; ++index)
  {
    auto* item = item_array + (static_cast<std::uintptr_t>(index) * inventory_item_stride);
    if (static_cast<int>(read_item_def_id(item)) != item_def_id)
    {
      continue;
    }

    const auto item_id = read_item_id(item);
    if (item_id != 0)
    {
      item_ids.emplace_back(item_id);
    }
  }

  return item_ids;
}

std::optional<std::uint64_t> get_first_item_id_of_item_def(const int item_def_id)
{
  const auto item_ids = get_item_ids_of_item_def(item_def_id);
  if (item_ids.empty())
  {
    return std::nullopt;
  }
  return item_ids.front();
}

bool has_item_def(const int item_def_id)
{
  return get_first_item_id_of_item_def(item_def_id).has_value();
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
    return 6;
  }

  return slot;
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

bool equip_item(const int class_id, const int slot, const int item_def_id, const bool get_missing = true, const bool allow_rent = false)
{
  if (!api_ready())
  {
    return false;
  }

  const int loadout_slot = corrected_loadout_slot(class_id, slot);
  if (!inventory_schema_ready())
  {
    debug_log("inventory schema not ready, skipping equip class=%d slot=%d def=%d\n", class_id, loadout_slot, item_def_id);
    return false;
  }

  if (item_def_id == -1)
  {
    const bool ok = g_inventory_api.equip_item_in_loadout(
      g_inventory_api.inventory_manager,
      class_id,
      loadout_slot,
      static_cast<std::uint64_t>(-1));
    if (!ok) error_log("unequip failed class=%d slot=%d\n", class_id, loadout_slot);
    return ok;
  }

  auto item_id = get_first_item_id_of_item_def(item_def_id);
  if (!item_id)
  {
    debug_log("no owned item id for def=%d (class=%d slot=%d)\n", item_def_id, class_id, loadout_slot);
    if (get_missing)
    {
      get_item(item_def_id, allow_rent);
    }
    return false;
  }

  const bool ok = g_inventory_api.equip_item_in_loadout(g_inventory_api.inventory_manager, class_id, loadout_slot, *item_id);
  if (!ok)
  {
    error_log("equip failed class=%d slot=%d def=%d item_id=%llu\n",
      class_id, loadout_slot, item_def_id, static_cast<unsigned long long>(*item_id));
  }
  else
  {
    debug_log("equipped class=%d slot=%d def=%d item_id=%llu\n",
      class_id, loadout_slot, item_def_id, static_cast<unsigned long long>(*item_id));
  }
  return ok;
}

bool is_metal_item_def(const int item_def_id)
{
  return item_def_id == 5000 || item_def_id == 5001 || item_def_id == 5002;
}

void equip_weapon_list_spec(
  std::string_view spec,
  fallback_state& state,
  const int class_id,
  const int slot)
{
  const auto ids = parse_int_list(spec, '/');
  if (ids.empty())
  {
    debug_log("invalid weapon spec '%.*s'\n", static_cast<int>(spec.size()), spec.data());
    return;
  }

  if (ids.size() < 2)
  {
    equip_item(class_id, slot, ids.front(), true, true);
    return;
  }

  if (has_item_def(ids[0]))
  {
    state.attempts = 0;
    equip_item(class_id, slot, ids[0], false, false);
    return;
  }

  if (state.attempts >= fallback_attempt_limit)
  {
    equip_item(class_id, slot, ids[1], true, true);
    return;
  }

  ++state.attempts;
  equip_item(class_id, slot, ids[0], true, true);
}

bool try_craft_group(const std::vector<int>& required_item_defs)
{
  for (const int required_item_def : required_item_defs)
  {
    if (is_metal_item_def(required_item_def) || has_item_def(required_item_def))
    {
      continue;
    }

    const auto* achievement_backed = find_achievement_item(required_item_def);
    if (achievement_backed != nullptr)
    {
      if (has_achievement(achievement_backed->achievement_id))
      {
        debug_log("achievement-backed item def %d is already unlocked but missing\n", required_item_def);
        continue;
      }

      get_item(required_item_def, false);
      return false;
    }

    debug_log("missing crafting material item def %d\n", required_item_def);
    return false;
  }

  if (craft_items(required_item_defs))
  {
    trigger_new_item_notification();
  }
  return true;
}

void equip_craft_spec(std::string_view spec, const int class_id, const int slot)
{
  const std::size_t result_separator = spec.find('-');
  if (result_separator == std::string::npos)
  {
    debug_log("craft spec '%.*s' has no result item\n", static_cast<int>(spec.size()), spec.data());
    return;
  }

  const auto result = parse_int(spec.substr(result_separator + 1));
  if (!result)
  {
    debug_log("craft spec '%.*s' has invalid result item\n", static_cast<int>(spec.size()), spec.data());
    return;
  }

  if (has_item_def(*result))
  {
    equip_item(class_id, slot, *result, false, false);
    return;
  }

  const auto result_token = std::to_string(*result);
  for (const auto& group : split_craft_groups(spec))
  {
    if (group == result_token)
    {
      continue;
    }

    const auto required_item_defs = parse_int_list(group, ',');
    if (required_item_defs.empty())
    {
      debug_log("invalid crafting group '%s'\n", group.c_str());
      return;
    }

    if (try_craft_group(required_item_defs))
    {
      return;
    }
  }
}

void equip_weapon_spec(std::string_view spec, const int class_id, const int slot)
{
  const auto cleaned_spec = trim(spec);
  if (cleaned_spec.empty())
  {
    return;
  }

  auto& state = g_fallback_states[static_cast<std::size_t>(std::clamp(slot, 0, 2))];
  if (state.spec != cleaned_spec)
  {
    state.spec = cleaned_spec;
    state.attempts = 0;
  }

  if (cleaned_spec == "-1")
  {
    equip_item(class_id, slot, -1, false, false);
    return;
  }

  const bool is_craft_spec =
    cleaned_spec.find(',') != std::string::npos || cleaned_spec.find(';') != std::string::npos;
  if (is_craft_spec)
  {
    equip_craft_spec(cleaned_spec, class_id, slot);
  }
  else
  {
    equip_weapon_list_spec(cleaned_spec, state, class_id, slot);
  }
}

void equip_hat_spec(std::string_view spec, const int class_id, const int slot)
{
  const auto item_def_id = parse_int(spec);
  if (!item_def_id)
  {
    debug_log("invalid hat spec '%s'\n", trim(spec).c_str());
    return;
  }

  if (*item_def_id < -1 || *item_def_id > 65535)
  {
    debug_log("hat item def %d is out of range\n", *item_def_id);
    return;
  }

  equip_item(class_id, slot, *item_def_id);
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

template <typename function_type>
void resolve_signature(function_type& target, const char* signature)
{
  if (target != nullptr) return;
  target = reinterpret_cast<function_type>(sigscan_module("client.so", signature));
}

void log_resolution_status(const char* tag)
{
  print("[autoitem] %s after %d attempt(s) manager=%p first=%p equip=%p rent=%p craft=%p\n",
    tag,
    g_initialize_retry_count,
    g_inventory_api.inventory_manager,
    reinterpret_cast<void*>(g_inventory_api.get_first_item_of_item_def),
    reinterpret_cast<void*>(g_inventory_api.equip_item_in_loadout),
    reinterpret_cast<void*>(g_inventory_api.do_preview_item),
    reinterpret_cast<void*>(g_inventory_api.craft_custom));
}

} // namespace

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

  resolve_signature(g_inventory_api.get_first_item_of_item_def, sigs::tf_inventory_get_first_item_of_item_def);
  resolve_signature(g_inventory_api.equip_item_in_loadout, sigs::tf_inventory_equip_item_in_loadout);
  resolve_signature(g_inventory_api.do_preview_item, sigs::tf_inventory_do_preview_item);
  resolve_signature(g_inventory_api.craft_custom, sigs::tf_inventory_craft_custom);

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

void on_create_move()
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

  if (!config.misc.automation.auto_item)
  {
    return;
  }

  if (entity_list == nullptr)
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

  if (config.misc.automation.auto_item_weapons)
  {
    equip_weapon_spec(config.misc.automation.auto_item_primary, class_id, primary_slot);
    equip_weapon_spec(config.misc.automation.auto_item_secondary, class_id, secondary_slot);
    equip_weapon_spec(config.misc.automation.auto_item_melee, class_id, melee_slot);
  }

  if (config.misc.automation.auto_item_hats)
  {
    constexpr std::array<int, 3> hat_slots{ 7, 8, 10 };
    const std::array<std::string_view, 3> hat_specs{
      config.misc.automation.auto_item_hat1,
      config.misc.automation.auto_item_hat2,
      config.misc.automation.auto_item_hat3,
    };

    for (int index = 0; index < 3; ++index)
    {
      const int rotated_slot = hat_slots[static_cast<std::size_t>((g_hat_rotation_offset + index) % 3)];
      equip_hat_spec(hat_specs[static_cast<std::size_t>(index)], class_id, rotated_slot);
    }
    g_hat_rotation_offset = (g_hat_rotation_offset + 1) % 3;
  }

  if (config.misc.automation.auto_item_noisemaker)
  {
    const int item_def = seasonal_noisemaker_item_def();
    if (has_item_def(item_def))
    {
      equip_item(class_id, noisemaker_slot, item_def, false, false);
    }
  }
}

bool rent_item(const int item_def_id)
{
  if (!api_ready() || item_def_id <= 0)
  {
    return false;
  }

  debug_log("requesting preview item def %d\n", item_def_id);
  g_inventory_api.do_preview_item(nullptr, item_def_id);
  queue_pending_pickup_ack();
  return true;
}

bool craft_items(const std::vector<int>& item_def_ids)
{
  if (!api_ready() || item_def_ids.empty() || item_def_ids.size() > max_crafting_inputs)
  {
    return false;
  }

  std::vector<std::uint64_t> selected_item_ids{};
  selected_item_ids.reserve(item_def_ids.size());
  for (const int item_def_id : item_def_ids)
  {
    const auto candidates = get_item_ids_of_item_def(item_def_id);
    auto selected = std::find_if(candidates.begin(), candidates.end(), [&](const std::uint64_t item_id)
    {
      return std::find(selected_item_ids.begin(), selected_item_ids.end(), item_id) == selected_item_ids.end();
    });

    if (selected == candidates.end())
    {
      debug_log("cannot craft, missing unique item def %d\n", item_def_id);
      return false;
    }

    selected_item_ids.emplace_back(*selected);
  }

  std::array<std::uint8_t, crafting_panel_size> panel{};
  for (std::size_t index = 0; index < selected_item_ids.size(); ++index)
  {
    write_unaligned(
      panel.data() + crafting_panel_input_items_offset + (index * sizeof(std::uint64_t)),
      selected_item_ids[index]);
  }
  const int custom_recipe = -2;
  write_unaligned(panel.data() + crafting_panel_recipe_offset, custom_recipe);

  auto* patch_target = reinterpret_cast<std::uint8_t*>(reinterpret_cast<void*>(g_inventory_api.craft_custom)) + crafting_ui_skip_patch_offset;
  byte_patch ui_skip_patch(
    patch_target,
    { crafting_ui_skip_patch[0], crafting_ui_skip_patch[1], crafting_ui_skip_patch[2], crafting_ui_skip_patch[3], crafting_ui_skip_patch[4] });

  if (!ui_skip_patch.apply())
  {
    return false;
  }

  g_inventory_api.craft_custom(panel.data());
  ui_skip_patch.restore();
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

} // namespace autoitem
