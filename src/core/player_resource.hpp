#ifndef PLAYER_RESOURCE_HPP
#define PLAYER_RESOURCE_HPP

#include <algorithm>
#include <cstdint>

#include "games/tf2/sdk/entities/entity.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"

namespace puphook::core::player_resource
{

inline constexpr int absolute_max_client_index = 2048;

inline Entity* g_cached_player_resource = nullptr;
inline int g_cached_player_resource_index = -1;

[[nodiscard]] inline int max_client_index()
{
  if (global_vars == nullptr) {
    return 0;
  }
  return std::clamp(global_vars->max_clients, 0, absolute_max_client_index);
}

[[nodiscard]] inline bool index_in_range(const int player_index)
{
  const int max_index = max_client_index();
  return player_index > 0 && max_index > 0 && player_index <= max_index;
}

inline void invalidate_player_resource_cache()
{
  g_cached_player_resource = nullptr;
  g_cached_player_resource_index = -1;
}

inline void cache_player_resource_entity(Entity* entity, const int index)
{
  if (entity == nullptr || index <= 0) {
    invalidate_player_resource_cache();
    return;
  }
  g_cached_player_resource = entity;
  g_cached_player_resource_index = index;
}

[[nodiscard]] inline Entity* get_player_resource_entity()
{
  if (entity_list == nullptr)
  {
    invalidate_player_resource_cache();
    return nullptr;
  }

  if (g_cached_player_resource_index > 0)
  {
    auto* entity = entity_list->entity_from_index(static_cast<unsigned int>(g_cached_player_resource_index));
    if (entity != nullptr && entity->get_class_id() == class_id::PLAYER_RESOURCE)
    {
      g_cached_player_resource = entity;
      return entity;
    }
    invalidate_player_resource_cache();
  }

  const int max_entities = entity_list->get_max_entities();
  for (int index = 1; index < max_entities; ++index)
  {
    auto* entity = entity_list->entity_from_index(static_cast<unsigned int>(index));
    if (entity != nullptr && entity->get_class_id() == class_id::PLAYER_RESOURCE)
    {
      cache_player_resource_entity(entity, index);
      return entity;
    }
  }

  return nullptr;
}

template <typename value_type>
[[nodiscard]] value_type read_value(Entity* player_resource, int array_offset, int player_index)
{
  if (player_resource == nullptr || array_offset <= 0 || !index_in_range(player_index))
  {
    return {};
  }

  const auto base = reinterpret_cast<std::uintptr_t>(player_resource);
  const auto entry_offset = static_cast<std::uintptr_t>(array_offset) +
    static_cast<std::uintptr_t>(player_index) * sizeof(value_type);
  return *reinterpret_cast<value_type*>(base + entry_offset);
}

inline constexpr int name_array_end_above_ping = 816;

[[nodiscard]] inline const char* name_pointer(Entity* player_resource, int ping_offset, int player_index)
{
  if (player_resource == nullptr || ping_offset <= name_array_end_above_ping || !index_in_range(player_index))
  {
    return nullptr;
  }

  const auto base = reinterpret_cast<std::uintptr_t>(player_resource);
  const auto array_offset = static_cast<std::uintptr_t>(ping_offset - name_array_end_above_ping);
  const auto entry_offset = array_offset + static_cast<std::uintptr_t>(player_index) * sizeof(const char*);
  return *reinterpret_cast<const char* const*>(base + entry_offset);
}

}

#endif
