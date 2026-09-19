/*
/^-----^\   data: 2026-05-06
V  o o  V  file: src/core/hooks/region_selector.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "region_selector.hpp"
#include "core/detach.hpp"
#include "features/automation/region_selector/region_selector.hpp"
#include "features/menu/config.hpp"

int steam_networking_utils_get_ping_to_data_center_hook(
  void* self,
  const steam_networking_pop_id pop_id,
  steam_networking_pop_id* via_relay_pop)
{
  PUPHOOK_HOOK_GUARD();
  if (steam_networking_utils_get_ping_to_data_center_original == nullptr)
  {
    return -1;
  }

  const int original_ping = steam_networking_utils_get_ping_to_data_center_original(self, pop_id, via_relay_pop);
  return automation::region_selector::adjust_ping(original_ping, pop_id);
}

int steam_networking_utils_get_direct_ping_to_pop_hook(void* self, const steam_networking_pop_id pop_id)
{
  PUPHOOK_HOOK_GUARD();
  if (steam_networking_utils_get_direct_ping_to_pop_original == nullptr)
  {
    return -1;
  }

  const int original_ping = steam_networking_utils_get_direct_ping_to_pop_original(self, pop_id);
  return automation::region_selector::adjust_ping(original_ping, pop_id);
}

bool region_selector_request_queue_for_match_available()
{
  return region_selector_request_queue_for_match_original != nullptr &&
         !puphook::core::is_detach_pending();
}

void request_queue_for_match_with_region_selector(void* self, const unsigned int match_group)
{
  if (puphook::core::is_detach_pending())
  {
    return;
  }

  region_selector_request_queue_for_match_hook(self, match_group);
}

void region_selector_request_queue_for_match_hook(void* self, const unsigned int match_group)
{
  PUPHOOK_HOOK_GUARD();
  if (puphook::core::is_detach_pending())
  {
    return;
  }

  if (config.misc.automation.region_selector)
  {
    install_steam_networking_utils_hooks();
    automation::region_selector::refresh_ping_data();
  }

  if (region_selector_request_queue_for_match_original != nullptr)
  {
    region_selector_request_queue_for_match_original(self, match_group);
  }
}
