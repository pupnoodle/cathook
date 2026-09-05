/*
/^-----^\   data: 2026-08-22
V  o o  V  file: src/features/automation/mvm_queue/mvm_queue.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "features/automation/mvm_queue/mvm_queue.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "core/hooks/equip_region_unlock.hpp"
#include "core/print.hpp"
#include "core/shared/sigs.hpp"
#include "features/menu/config.hpp"
#include "libsigscan/libsigscan.h"

namespace automation::mvm_queue
{

namespace
{

using repeated_field_reserve_fn = void (*)(void*, int);
using string_new_element_fn = void* (*)();
using mvm_completed_mask_fn = bool (*)(unsigned int, unsigned int*, unsigned int*);
using get_party_client_fn = void* (*)();
using group_criteria_getter_fn = void* (*)(void*);

constexpr const char* tf_client_module_name = "tf/bin/linux64/client.so";
constexpr int mannup_match_group = 1;
constexpr int bootcamp_match_group = 0;
constexpr int party_criteria_wrapper_offset = 432;
constexpr int criteria_proto_has_bits_offset = 16;
constexpr int criteria_proto_mannup_tour_offset = 32;
constexpr int criteria_proto_mannup_missions_offset = 40;
constexpr int criteria_proto_bootcamp_missions_offset = 64;
constexpr std::uint32_t criteria_hasbit_mannup_tour = 1u << 2;
constexpr int schema_mission_data_offset = 2200;
constexpr int schema_mission_count_offset = 2216;
constexpr int schema_tour_data_offset = 2232;
constexpr int schema_tour_count_offset = 2248;
constexpr int tour_internal_name_offset = 0;
constexpr int tour_mission_pairs_offset = 40;
constexpr int tour_mission_pair_count_offset = 56;
constexpr int mission_popfile_offset = 8;
constexpr int mission_difficulty_offset = 40;
constexpr int practice_mission_difficulty = -1;
constexpr int max_bootcamp_bits = 32;

struct repeated_string_field
{
  void** elements;
  std::int32_t current_size;
  std::int32_t total_size;
  std::int32_t capacity;
};

struct tour_entry
{
  std::string internal_name{};
};

struct bootcamp_entry
{
  std::string popfile{};
};

struct mvm_queue_state
{
  bool attempted = false;
  bool ready = false;
  repeated_field_reserve_fn reserve = nullptr;
  string_new_element_fn new_element = nullptr;
  mvm_completed_mask_fn completed_mask = nullptr;
  get_party_client_fn get_party_client = nullptr;
};

struct schema_cache
{
  bool valid = false;
  std::uintptr_t schema = 0;
  std::vector<tour_entry> tours{};
  std::vector<bootcamp_entry> bootcamp_missions{};
};

mvm_queue_state g_api{};
schema_cache g_cache{};
std::string* g_written_tour_string = nullptr;

bool resolve_api()
{
  if (g_api.ready)
  {
    return true;
  }

  if (g_api.attempted)
  {
    return false;
  }

  g_api.attempted = true;
  g_api.reserve = reinterpret_cast<repeated_field_reserve_fn>(
    sigscan_module(tf_client_module_name, sigs::protobuf_repeated_field_reserve));
  g_api.new_element = reinterpret_cast<string_new_element_fn>(
    sigscan_module(tf_client_module_name, sigs::protobuf_string_new_element));
  g_api.completed_mask =
    reinterpret_cast<mvm_completed_mask_fn>(sigscan_module(tf_client_module_name, sigs::mvm_completed_tour_mask));
  const auto get_party_client_match = sigscan_module(tf_client_module_name, sigs::get_party_client);
  g_api.get_party_client = get_party_client_match != nullptr
    ? reinterpret_cast<get_party_client_fn>(
        reinterpret_cast<std::uintptr_t>(get_party_client_match) + sigs::get_party_client_offset)
    : nullptr;
  g_api.ready = g_api.reserve != nullptr && g_api.new_element != nullptr && g_api.get_party_client != nullptr;

  if (!g_api.ready)
  {
    print("[mvm_queue] signature resolution failed reserve=%p new=%p mask=%p party=%p\n",
      reinterpret_cast<void*>(g_api.reserve),
      reinterpret_cast<void*>(g_api.new_element),
      reinterpret_cast<void*>(g_api.completed_mask),
      reinterpret_cast<void*>(g_api.get_party_client));
    return false;
  }

  return true;
}

const char* read_cstring(const std::uintptr_t address, const int offset)
{
  if (address == 0)
  {
    return "";
  }

  const auto value = *reinterpret_cast<const char**>(address + static_cast<std::uintptr_t>(offset));
  return value != nullptr ? value : "";
}

void refresh_schema_cache()
{
  if (item_schema_lookup_map_original == nullptr)
  {
    return;
  }

  const auto schema = item_schema_lookup_map_original();
  if (schema == 0 || (g_cache.valid && g_cache.schema == schema))
  {
    return;
  }

  std::vector<tour_entry> tours{};
  const int tour_count_value = *reinterpret_cast<const std::int32_t*>(
    schema + static_cast<std::uintptr_t>(schema_tour_count_offset));
  const auto tour_data = *reinterpret_cast<const std::uintptr_t*>(
    schema + static_cast<std::uintptr_t>(schema_tour_data_offset));
  tours.reserve(tour_count_value > 0 ? static_cast<std::size_t>(tour_count_value) : 0u);
  for (int index = 0; index < tour_count_value; ++index)
  {
    const auto tour = tour_data + static_cast<std::uintptr_t>(index) * 88u;
    tour_entry entry{};
    entry.internal_name = read_cstring(tour, tour_internal_name_offset);
    if (!entry.internal_name.empty())
    {
      tours.emplace_back(std::move(entry));
    }
  }

  std::vector<bootcamp_entry> bootcamp_missions{};
  const int mission_count_value = *reinterpret_cast<const std::int32_t*>(
    schema + static_cast<std::uintptr_t>(schema_mission_count_offset));
  const auto mission_data = *reinterpret_cast<const std::uintptr_t*>(
    schema + static_cast<std::uintptr_t>(schema_mission_data_offset));
  bootcamp_missions.reserve(bootcamp_missions.size() +
    (mission_count_value > 0 ? static_cast<std::size_t>(mission_count_value) : 0u));
  for (int index = 0; index < mission_count_value; ++index)
  {
    const auto mission = mission_data + static_cast<std::uintptr_t>(index) * 48u;
    const auto difficulty = *reinterpret_cast<const std::int32_t*>(
      mission + static_cast<std::uintptr_t>(mission_difficulty_offset));
    if (difficulty != practice_mission_difficulty)
    {
      continue;
    }

    bootcamp_entry entry{};
    entry.popfile = read_cstring(mission, mission_popfile_offset);
    if (!entry.popfile.empty())
    {
      bootcamp_missions.emplace_back(std::move(entry));
    }
  }

  g_cache.schema = schema;
  g_cache.tours = std::move(tours);
  g_cache.bootcamp_missions = std::move(bootcamp_missions);
  g_cache.valid = true;
}

void clear_repeated(repeated_string_field* field)
{
  if (field == nullptr || field->elements == nullptr)
  {
    return;
  }

  for (std::int32_t index = 0; index < field->current_size; ++index)
  {
    auto* element = static_cast<std::string*>(field->elements[index]);
    if (element != nullptr)
    {
      element->assign("");
    }
  }
  field->current_size = 0;
}

void append_repeated(repeated_string_field* field, const char* value, const std::size_t length)
{
  if (field == nullptr || field->elements == nullptr || value == nullptr)
  {
    return;
  }

  const std::int32_t current = field->current_size;
  const std::int32_t total = field->total_size;
  std::string* element = nullptr;
  if (current >= 0 && current < total)
  {
    element = static_cast<std::string*>(field->elements[current]);
    field->current_size = current + 1;
  }
  else
  {
    if (total == field->capacity)
    {
      g_api.reserve(field, total + 1);
    }

    element = static_cast<std::string*>(g_api.new_element());
    field->elements[current] = element;
    field->current_size = current + 1;
    field->total_size = total + 1;
  }

  if (element != nullptr)
  {
    element->assign(value, length);
  }
}

repeated_string_field* repeated_at(std::uint8_t* proto, const int offset)
{
  return reinterpret_cast<repeated_string_field*>(proto + static_cast<std::uintptr_t>(offset));
}

std::uint8_t* resolve_criteria_proto(void* party_client)
{
  if (party_client == nullptr)
  {
    return nullptr;
  }

  auto* wrapper = static_cast<std::uint8_t*>(party_client) + party_criteria_wrapper_offset;
  void** vtable = *reinterpret_cast<void***>(wrapper);
  if (vtable == nullptr || vtable[2] == nullptr)
  {
    return nullptr;
  }

  const auto getter = *reinterpret_cast<group_criteria_getter_fn*>(&vtable[2]);
  return static_cast<std::uint8_t*>(getter(wrapper));
}

std::uint32_t completed_missions_mask(const int tour_index)
{
  if (g_api.completed_mask == nullptr || tour_index < 0)
  {
    return 0;
  }

  unsigned int first_output = 0;
  unsigned int mask_output = 0;
  if (!g_api.completed_mask(static_cast<unsigned int>(tour_index), &first_output, &mask_output))
  {
    return 0;
  }

  return mask_output;
}

void apply_bootcamp_list(std::uint8_t* proto)
{
  auto* field = repeated_at(proto, criteria_proto_bootcamp_missions_offset);
  clear_repeated(field);

  if (!config.misc.automation.bootcamp_enabled)
  {
    return;
  }

  const auto& missions = g_cache.bootcamp_missions;
  const std::size_t selectable = missions.size() < static_cast<std::size_t>(max_bootcamp_bits)
    ? missions.size()
    : static_cast<std::size_t>(max_bootcamp_bits);
  for (std::size_t index = 0; index < selectable; ++index)
  {
    const auto bit = 1u << index;
    if ((config.misc.automation.bootcamp_mission_bits & bit) == 0)
    {
      continue;
    }

    const auto& popfile = missions[index].popfile;
    append_repeated(field, popfile.c_str(), popfile.size());
  }
}

void apply_mannup_list(std::uint8_t* proto)
{
  auto* field = repeated_at(proto, criteria_proto_mannup_missions_offset);
  clear_repeated(field);

  if (config.misc.automation.auto_queue_mode != mannup_match_group)
  {
    return;
  }

  const int selection = config.misc.automation.mvm_tour_index;
  if (selection <= 0 || selection > static_cast<int>(g_cache.tours.size()))
  {
    return;
  }

  const int tour_index = selection - 1;
  const auto schema = g_cache.schema;
  const auto tour_data = *reinterpret_cast<const std::uintptr_t*>(
    schema + static_cast<std::uintptr_t>(schema_tour_data_offset));
  const auto tour = tour_data + static_cast<std::uintptr_t>(tour_index) * 88u;
  const auto pairs = *reinterpret_cast<const std::uintptr_t*>(
    tour + static_cast<std::uintptr_t>(tour_mission_pairs_offset));
  const auto pair_count = *reinterpret_cast<const std::int32_t*>(
    tour + static_cast<std::uintptr_t>(tour_mission_pair_count_offset));

  std::uint32_t completed_mask = 0;
  const bool filter_completed = config.misc.automation.mvm_uncompleted_only;
  if (filter_completed)
  {
    completed_mask = completed_missions_mask(tour_index);
  }

  for (std::int32_t index = 0; pairs != 0 && index < pair_count; ++index)
  {
    const auto pair = pairs + static_cast<std::uintptr_t>(index) * 8u;
    const auto mission_schema_index = static_cast<const std::int32_t>(*reinterpret_cast<const std::int32_t*>(pair));
    const auto badge_slot = static_cast<const std::int32_t>(*reinterpret_cast<const std::int32_t*>(pair + 4u));
    if (mission_schema_index < 0 ||
        mission_schema_index >= *reinterpret_cast<const std::int32_t*>(
          schema + static_cast<std::uintptr_t>(schema_mission_count_offset)))
    {
      continue;
    }

    if (filter_completed && badge_slot >= 0 && badge_slot < max_bootcamp_bits &&
        ((completed_mask >> badge_slot) & 1u) != 0)
    {
      continue;
    }

    const auto mission = *reinterpret_cast<const std::uintptr_t*>(
      schema + static_cast<std::uintptr_t>(schema_mission_data_offset)) +
      static_cast<std::uintptr_t>(mission_schema_index) * 48u;
    const auto* popfile = read_cstring(mission, mission_popfile_offset);
    if (popfile[0] == '\0')
    {
      continue;
    }

    append_repeated(field, popfile, std::strlen(popfile));
  }
}

void apply_criteria(void* party_client)
{
  auto* proto = resolve_criteria_proto(party_client);
  if (proto == nullptr)
  {
    return;
  }

  auto* has_bits = reinterpret_cast<std::uint32_t*>(proto + criteria_proto_has_bits_offset);
  auto** tour_slot = reinterpret_cast<std::string**>(proto + criteria_proto_mannup_tour_offset);

  const int selection = config.misc.automation.mvm_tour_index;
  const bool want_tour = config.misc.automation.auto_queue_mode == mannup_match_group &&
                         selection > 0 && selection <= static_cast<int>(g_cache.tours.size());

  if (want_tour && item_schema_lookup_map_original != nullptr)
  {
    const auto& internal_name = g_cache.tours[static_cast<std::size_t>(selection - 1)].internal_name;
    if (*tour_slot == nullptr || *tour_slot != g_written_tour_string)
    {
      *tour_slot = static_cast<std::string*>(g_api.new_element());
      g_written_tour_string = *tour_slot;
    }

    if (*tour_slot != nullptr)
    {
      (*tour_slot)->assign(internal_name);
      *has_bits |= criteria_hasbit_mannup_tour;
    }
  }
  else
  {
    *has_bits &= ~criteria_hasbit_mannup_tour;
    if (*tour_slot != nullptr && *tour_slot == g_written_tour_string)
    {
      (*tour_slot)->assign("");
    }
  }

  apply_mannup_list(proto);
  apply_bootcamp_list(proto);
}

}

void tick()
{
  if (!(config.misc.automation.auto_queue || config.misc.automation.auto_requeue))
  {
    return;
  }

  const int mode = config.misc.automation.auto_queue_mode;
  if (mode != mannup_match_group && mode != bootcamp_match_group)
  {
    return;
  }

  if (item_schema_lookup_map_original == nullptr)
  {
    return;
  }

  if (!resolve_api())
  {
    return;
  }

  refresh_schema_cache();
  if (!g_cache.valid)
  {
    return;
  }

  apply_criteria(g_api.get_party_client());
}

int tour_count()
{
  refresh_schema_cache();
  return static_cast<int>(g_cache.tours.size());
}

const char* tour_display_name(const int index)
{
  if (index < 0 || index >= static_cast<int>(g_cache.tours.size()))
  {
    return "";
  }

  return g_cache.tours[static_cast<std::size_t>(index)].internal_name.c_str();
}

int bootcamp_mission_count()
{
  refresh_schema_cache();
  const std::size_t count = g_cache.bootcamp_missions.size();
  return static_cast<int>(count < static_cast<std::size_t>(max_bootcamp_bits)
    ? count
    : static_cast<std::size_t>(max_bootcamp_bits));
}

const char* bootcamp_mission_name(const int index)
{
  if (index < 0 || index >= static_cast<int>(g_cache.bootcamp_missions.size()))
  {
    return "";
  }

  return g_cache.bootcamp_missions[static_cast<std::size_t>(index)].popfile.c_str();
}

}
