/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/hooks/equip_region_unlock.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "equip_region_unlock.hpp"
#include <cstddef>
#include "core/memory/code_scan.hpp"
#include "features/menu/config.hpp"

namespace
{

auto should_unlock_equip_regions(std::uintptr_t lookup_map) -> bool
{
  if (!config.misc.exploits.equip_region_unlock ||
      item_schema_lookup_map_original == nullptr)
  {
    return false;
  }

  return lookup_map == item_schema_lookup_map_original();
}

}

std::uintptr_t item_definition_lookup_hook(std::uintptr_t lookup_map, unsigned int item_index)
{
  PUPHOOK_HOOK_GUARD();
  if (item_definition_lookup_original == nullptr)
  {
    return 0;
  }

  // Inventory changer redirect temporarily disabled.
  const std::uintptr_t item_definition = item_definition_lookup_original(lookup_map, item_index);
  if (!should_unlock_equip_regions(lookup_map) || item_definition == 0)
  {
    return item_definition;
  }

  static const int masks_offset =
    puphook::core::memory::keyed_movq_store_offset("client.so", "equip_regions");
  if (masks_offset <= 0) {
    return item_definition;
  }

  auto* const masks = reinterpret_cast<std::uint32_t*>(item_definition + masks_offset);
  masks[0] = 0;
  masks[1] = 0;
  return item_definition;
}
