/*
/^-----^\   data: 2026-05-06
V  o o  V  file: src/features/automation/region_selector/region_selector.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "features/automation/region_selector/region_selector.hpp"
#include "core/shared/modules.hpp"
#include <algorithm>
#include <cstddef>
#include "core/hooks/region_selector.hpp"
#include "core/shared/sigs.hpp"
#include "features/menu/config.hpp"
#include "libsigscan/libsigscan.h"

namespace automation::region_selector
{

namespace
{

using get_matchmaking_client_fn = void* (*)();
using tf_gc_client_system_ping_think_fn = void (*)(void*);

constexpr std::ptrdiff_t tf_gc_pending_ping_refresh_offset = 0x4cc;
struct tf_gc_client_api
{
  bool initialized = false;
  get_matchmaking_client_fn get_matchmaking_client = nullptr;
  tf_gc_client_system_ping_think_fn ping_think = nullptr;
};

tf_gc_client_api g_tf_gc_client_api{};

std::array<char, 5> pop_id_to_region(const steam_networking_pop_id pop_id)
{
  std::array<char, 5> region{};
  region[0] = static_cast<char>(pop_id >> 16u);
  region[1] = static_cast<char>(pop_id >> 8u);
  region[2] = static_cast<char>(pop_id);
  region[3] = static_cast<char>(pop_id >> 24u);
  return region;
}

void initialize_tf_gc_client_api()
{
  if (g_tf_gc_client_api.initialized)
  {
    return;
  }

  g_tf_gc_client_api.initialized = true;
  g_tf_gc_client_api.get_matchmaking_client =
    reinterpret_cast<get_matchmaking_client_fn>(sigscan_module(puphook::core::modules::tf_client, sigs::get_matchmaking_client));
  g_tf_gc_client_api.ping_think =
    reinterpret_cast<tf_gc_client_system_ping_think_fn>(sigscan_module(puphook::core::modules::tf_client, sigs::tf_gc_client_system_ping_think));
}

bool refresh_tf_gc_ping_data()
{
  initialize_tf_gc_client_api();
  if (g_tf_gc_client_api.get_matchmaking_client == nullptr || g_tf_gc_client_api.ping_think == nullptr)
  {
    return false;
  }

  auto* matchmaking_client = static_cast<std::uint8_t*>(g_tf_gc_client_api.get_matchmaking_client());
  if (matchmaking_client == nullptr)
  {
    return false;
  }

  *reinterpret_cast<bool*>(matchmaking_client + tf_gc_pending_ping_refresh_offset) = true;
  g_tf_gc_client_api.ping_think(matchmaking_client);
  return true;
}

}

bool is_region_allowed(const std::string_view region)
{
  if (region.empty())
  {
    return true;
  }

  for (const auto& data_center : data_centers)
  {
    if (region == data_center.code)
    {
      return is_region_bit_allowed(data_center.bit);
    }
  }

  return true;
}

void set_region_allowed(const std::uint64_t bit, const bool allowed)
{
  if (allowed)
  {
    config.misc.automation.region_selector_allowed_mask |= bit;
    return;
  }

  config.misc.automation.region_selector_allowed_mask &= ~bit;
}

bool is_region_bit_allowed(const std::uint64_t bit)
{
  return (config.misc.automation.region_selector_allowed_mask & bit) != 0;
}

bool are_all_continent_regions_allowed(const std::string_view continent)
{
  bool found_region = false;
  for (const auto& data_center : data_centers)
  {
    if (continent != data_center.continent)
    {
      continue;
    }

    found_region = true;
    if (!is_region_bit_allowed(data_center.bit))
    {
      return false;
    }
  }

  return found_region;
}

void set_continent_regions_allowed(const std::string_view continent, const bool allowed)
{
  for (const auto& data_center : data_centers)
  {
    if (continent == data_center.continent)
    {
      set_region_allowed(data_center.bit, allowed);
    }
  }
}

int adjust_ping(const int original_ping, const steam_networking_pop_id pop_id)
{
  if (!config.misc.automation.region_selector || original_ping < 0)
  {
    return original_ping;
  }

  const auto region_buffer = pop_id_to_region(pop_id);
  const std::string_view region{ region_buffer.data() };
  if (region.empty())
  {
    return original_ping;
  }

  return is_region_allowed(region) ? preferred_region_ping : blocked_region_ping;
}

void refresh_ping_data()
{
  install_steam_networking_utils_hooks();
  if (refresh_tf_gc_ping_data())
  {
    return;
  }

  if (steam_networking_utils_interface == nullptr)
  {
    return;
  }

  steam_networking_utils_interface->check_ping_data_up_to_date(0.0f);
}

}
