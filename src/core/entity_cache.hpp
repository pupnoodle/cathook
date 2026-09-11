/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/core/entity_cache.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef ENTITY_CACHE_HPP
#define ENTITY_CACHE_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>
#include <unordered_map>

#include "types.hpp"

#include "games/tf2/sdk/entities/entity.hpp"

class Player;

struct entity_cache_player_entry {
  Player* player = nullptr;
  Entity* entity = nullptr;
  int index = 0;
  float simulation_time = 0.0f;
  Vec3 origin{};
  Vec3 velocity{};
  tf_team team = tf_team::UNKNOWN;
  int player_class = 0;
  unsigned long friends_id = 0;
  bool alive = false;
  bool dormant = true;
  bool friendly = false;
  bool ignored = false;
  bool fakeplayer = false;
  bool player_info_valid = false;
};

struct entity_cache_snapshot {
  std::uint32_t serial = 0;
  std::vector<entity_cache_player_entry> players{};
  std::unordered_map<enum class_id, std::vector<Entity*>> entities{};
};

inline static std::unordered_map<enum class_id, std::vector<Entity*>> entity_cache;
inline static entity_cache_snapshot g_entity_cache_snapshot;
inline static std::vector<Entity*> g_entity_cache_npcs;
inline static std::unordered_map<unsigned long, bool> friend_cache;

inline bool friend_cache_lookup(unsigned long friends_id) {
  const auto found = friend_cache.find(friends_id);
  return found != friend_cache.end() && found->second;
}

inline void friend_cache_store(unsigned long friends_id, bool is_friend) {
  friend_cache[friends_id] = is_friend;
}

inline const entity_cache_snapshot& entity_cache_current_snapshot() {
  return g_entity_cache_snapshot;
}

inline const std::vector<entity_cache_player_entry>& entity_cache_players() {
  return g_entity_cache_snapshot.players;
}

inline const std::vector<Entity*>& entity_cache_entities(enum class_id id) {
  static const std::vector<Entity*> empty_entities{};
  const auto found = entity_cache.find(id);
  return found != entity_cache.end() ? found->second : empty_entities;
}

inline const std::vector<Entity*>& entity_cache_npcs() {
  return g_entity_cache_npcs;
}

inline bool entity_cache_snapshot_contains_player(Player* player) {
  if (player == nullptr) {
    return false;
  }

  for (const entity_cache_player_entry& entry : g_entity_cache_snapshot.players) {
    if (entry.player == player && entry.alive && !entry.dormant) {
      return true;
    }
  }

  return false;
}

inline void entity_cache_clear_snapshot() {
  const std::uint32_t next_serial = g_entity_cache_snapshot.serial + 1;
  g_entity_cache_snapshot = {};
  g_entity_cache_snapshot.serial = next_serial;
}

inline void entity_cache_clear_lists() {
  for (auto& entry : entity_cache) {
    entry.second.clear();
  }
  g_entity_cache_npcs.clear();
}

inline void entity_cache_publish_snapshot(entity_cache_snapshot&& snapshot) {
  snapshot.serial = g_entity_cache_snapshot.serial + 1;
  g_entity_cache_snapshot = std::move(snapshot);
}

struct PickupItem {
  Vec3 location;
  float time;
};
inline static std::vector<PickupItem> pickup_item_cache;

inline void pickup_item_cache_prune(float current_time) {
  if (!std::isfinite(current_time)) {
    return;
  }

  pickup_item_cache.erase(
    std::remove_if(pickup_item_cache.begin(), pickup_item_cache.end(), [current_time](const PickupItem& item) {
      return !std::isfinite(item.time) || item.time < current_time;
    }),
    pickup_item_cache.end());
}

inline void pickup_item_cache_clear() {
  pickup_item_cache.clear();
}

inline void pickup_item_cache_record(const Vec3& location, float expires_at, float current_time) {
  pickup_item_cache_prune(current_time);
  if (std::isfinite(expires_at) && expires_at > current_time) {
    pickup_item_cache.push_back(PickupItem{location, expires_at});
  }
}

#endif
