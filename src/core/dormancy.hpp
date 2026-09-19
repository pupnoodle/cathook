#ifndef DORMANCY_HPP
#define DORMANCY_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core/player_resource.hpp"
#include "core/types.hpp"
#include "games/tf2/sdk/entities/entity.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"

namespace dormancy {

struct data {
  Vec3 origin{};
  Vec3 velocity{};
  float last_update = 0.0f;
  bool valid = false;
};

inline constexpr int k_max_index = 2048;
inline constexpr float k_player_keep = 1.0f;
inline constexpr float k_building_keep = 5.0f;

inline data g_records[k_max_index]{};

[[nodiscard]] inline int index_of(Entity* entity) {
  if (entity == nullptr) {
    return -1;
  }
  const int index = entity->get_index();
  return index > 0 && index < k_max_index ? index : -1;
}

[[nodiscard]] inline float keep_seconds(Entity* entity) {
  if (entity == nullptr) {
    return 0.0f;
  }
  if (entity->get_class_id() == class_id::PLAYER) {
    return k_player_keep;
  }
  if (entity->is_building()) {
    return k_building_keep;
  }
  return 0.0f;
}

[[nodiscard]] inline float now() {
  return global_vars != nullptr ? global_vars->curtime : 0.0f;
}

inline void clear() {
  for (data& record : g_records) {
    record = {};
  }
}

inline void expire(int index) {
  if (index > 0 && index < k_max_index) {
    g_records[index] = {};
  }
}

inline void apply_player_resource(Player* player) {
  if (player == nullptr) {
    return;
  }
  Entity* resource = puphook::core::player_resource::get_player_resource_entity();
  if (resource == nullptr) {
    return;
  }
  static tf2_netvars::lazy_offset alive_offset{"DT_TFPlayerResource", {"baseclass", "m_bAlive"}};
  static tf2_netvars::lazy_offset health_offset{"DT_TFPlayerResource", {"baseclass", "m_iHealth"}};
  const int index = player->get_index();
  if (alive_offset > 0) {
    const bool alive = puphook::core::player_resource::read_value<bool>(resource, alive_offset, index);
    static tf2_netvars::lazy_offset life_state_offset{"DT_BasePlayer", {"m_lifeState"}};
    if (life_state_offset > 0) {
      *reinterpret_cast<std::uint8_t*>(reinterpret_cast<uintptr_t>(player) + life_state_offset) =
        static_cast<std::uint8_t>(alive ? life_state::alive : life_state::dead);
    }
  }
  if (health_offset > 0) {
    static tf2_netvars::lazy_offset player_health{"DT_BasePlayer", {"m_iHealth"}};
    if (player_health > 0) {
      *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(player) + player_health) =
        puphook::core::player_resource::read_value<int>(resource, health_offset, index);
    }
  }
}

inline void store(Entity* entity) {
  const int index = index_of(entity);
  if (index < 0 || global_vars == nullptr) {
    return;
  }
  const Vec3 origin = entity->get_network_origin();
  if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) {
    return;
  }
  data& record = g_records[index];
  const Vec3 previous = record.origin;
  const float previous_time = record.last_update;
  record.origin = origin;
  record.last_update = now();
  record.valid = true;
  if (entity->get_class_id() == class_id::PLAYER) {
    auto* player = static_cast<Player*>(entity);
    record.velocity = player->get_velocity();
    if (record.velocity.x == 0.0f && record.velocity.y == 0.0f && record.velocity.z == 0.0f &&
        previous_time > 0.0f && record.last_update > previous_time) {
      const float dt = record.last_update - previous_time;
      if (dt > 0.0f && dt <= 1.0f) {
        record.velocity = (origin - previous) * (1.0f / dt);
      }
    }
  }
}

[[nodiscard]] inline data* get(int index) {
  if (index <= 0 || index >= k_max_index) {
    return nullptr;
  }
  data& record = g_records[index];
  return record.valid ? &record : nullptr;
}

[[nodiscard]] inline data* get(Entity* entity) {
  const int index = index_of(entity);
  if (index < 0) {
    return nullptr;
  }
  data* record = get(index);
  if (record == nullptr || global_vars == nullptr) {
    return nullptr;
  }
  const float keep = keep_seconds(entity);
  if (keep <= 0.0f || record->last_update + keep < now()) {
    expire(index);
    return nullptr;
  }
  if (entity != nullptr && entity->get_class_id() == class_id::PLAYER) {
    auto* player = static_cast<Player*>(entity);
    if (!player->is_alive()) {
      expire(index);
      return nullptr;
    }
  }
  return record;
}

[[nodiscard]] inline bool usable(Entity* entity) {
  if (entity == nullptr) {
    return false;
  }
  if (!entity->is_dormant()) {
    return true;
  }
  return get(entity) != nullptr;
}

[[nodiscard]] inline Vec3 origin(Entity* entity) {
  if (entity == nullptr) {
    return {};
  }
  if (data* record = entity->is_dormant() ? get(entity) : nullptr; record != nullptr) {
    return record->origin;
  }
  return entity->get_collision_origin();
}

[[nodiscard]] inline Vec3 velocity(Entity* entity) {
  if (entity == nullptr) {
    return {};
  }
  if (data* record = entity->is_dormant() ? get(entity) : nullptr; record != nullptr) {
    return record->velocity;
  }
  if (entity->get_class_id() == class_id::PLAYER) {
    return static_cast<Player*>(entity)->get_velocity();
  }
  return {};
}

[[nodiscard]] inline float age(Entity* entity) {
  data* record = get(entity);
  if (record == nullptr || global_vars == nullptr) {
    return 0.0f;
  }
  return std::max(0.0f, now() - record->last_update);
}

[[nodiscard]] inline float alpha(Entity* entity) {
  if (entity == nullptr || !entity->is_dormant()) {
    return 1.0f;
  }
  data* record = get(entity);
  const float keep = keep_seconds(entity);
  if (record == nullptr || keep <= 0.0f) {
    return 0.0f;
  }
  const float t = std::clamp(1.0f - age(entity) / keep, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

inline void update(Entity* entity) {
  const int index = index_of(entity);
  if (index < 0) {
    return;
  }
  const float keep = keep_seconds(entity);
  if (keep <= 0.0f) {
    return;
  }

  if (entity->is_dormant()) {
    return;
  }

  if (entity->get_class_id() == class_id::PLAYER) {
    auto* player = static_cast<Player*>(entity);
    if (!player->is_alive()) {
      expire(index);
      return;
    }
  }
  store(entity);
}

}

#endif
