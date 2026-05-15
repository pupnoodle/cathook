/*
/^-----^\   data: 2026-05-15
V  o o  V  file: src/features/combat/aimbot/projectile/splash_trace_cache.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====

*/


#ifndef SPLASH_TRACE_CACHE_HPP
#define SPLASH_TRACE_CACHE_HPP

#include <cmath>
#include <cstdint>
#include <unordered_map>

#include "core/types.hpp"

#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/engine_trace.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "features/combat/aimbot/projectile/projectile_trace.hpp"

namespace splash_trace_cache {

inline std::uint32_t hash_level_name(const char* level_name) {
  std::uint32_t h = 2166136261u;
  if (level_name == nullptr) {
    return h;
  }
  for (const char* p = level_name; *p != '\0'; ++p) {
    h ^= static_cast<std::uint32_t>(static_cast<unsigned char>(*p));
    h *= 16777619u;
  }
  return h;
}

inline std::uint64_t quantize_point(const Vec3& v) {
  constexpr float inv_cell = 1.0f / 14.0f;
  const auto q = [](float c) -> std::uint64_t {
    return static_cast<std::uint64_t>(static_cast<std::int32_t>(std::floor(c * inv_cell))) & 0x1FFFFF;
  };
  return (q(v.x) << 42) | (q(v.y) << 21) | q(v.z);
}

inline std::uint64_t make_key(const Vec3& trace_start,
  const Vec3& trace_end,
  const Vec3& hull,
  unsigned int mask,
  projectile_trace_contract contract) {
  const std::uint64_t trace_contract = 0x50524F4A54524345ULL ^ static_cast<std::uint64_t>(contract);
  const std::uint64_t a = quantize_point(trace_start);
  const std::uint64_t b = quantize_point(trace_end);
  const std::uint64_t h = quantize_point(hull);
  return trace_contract ^
    (static_cast<std::uint64_t>(mask) << 32) ^
    (a * 14695981039346656037ULL) ^
    (b * 1099511628211ULL) ^
    h;
}

struct cache_entry {
  int valid_until_tick = 0;
  bool ok = false;
  Vec3 endpos{};
  Vec3 plane_normal{};
};

inline std::unordered_map<std::uint64_t, cache_entry>& table() {
  static std::unordered_map<std::uint64_t, cache_entry> storage{};
  return storage;
}

inline std::uint32_t& stored_map_hash() {
  static std::uint32_t value = 0;
  return value;
}

inline void invalidate_if_map_changed() {
  const char* level = engine != nullptr ? engine->get_level_name() : nullptr;
  const std::uint32_t h = hash_level_name(level);
  if (h == stored_map_hash()) {
    return;
  }
  stored_map_hash() = h;
  table().clear();
}

inline void touch_budget() {
  auto& t = table();
  if (t.size() > 6144) {
    t.clear();
  }
}

// Cached hull trace (blocking non-player solids: props, doors, world geometry).
inline bool trace_world_hull_brush_cached(const Vec3& trace_start,
  const Vec3& trace_end,
  const Vec3& hull,
  Vec3* point_out,
  Vec3* plane_normal_out) {
  if (engine_trace == nullptr || point_out == nullptr || global_vars == nullptr) {
    return false;
  }

  invalidate_if_map_changed();

  const projectile_trace_contract contract = projectile_trace_contract::world_block;
  const unsigned int mask = projectile_trace_mask(contract);
  const std::uint64_t key = make_key(trace_start, trace_end, hull, mask, contract);
  const int now_tick = global_vars->tickcount;
  const int ttl_ticks = 96;

  cache_entry* slot = nullptr;
  {
    auto& t = table();
    const auto it = t.find(key);
    if (it != t.end() && it->second.valid_until_tick >= now_tick) {
      if (!it->second.ok) {
        return false;
      }
      *point_out = it->second.endpos;
      if (plane_normal_out != nullptr) {
        *plane_normal_out = it->second.plane_normal;
      }
      return true;
    }
    touch_budget();
    slot = &t[key];
  }

  const Vec3 hull_mins = hull * -1.0f;
  const Vec3 hull_maxs = hull;
  trace_t trace{};
  if (!projectile_trace_ray(
      trace_start,
      trace_end,
      &hull_mins,
      &hull_maxs,
      contract,
      nullptr,
      -1,
      &trace) ||
      trace.all_solid || trace.start_solid || trace.fraction >= 0.995f) {
    *slot = cache_entry{
      .valid_until_tick = now_tick + ttl_ticks,
      .ok = false,
      .endpos = {},
      .plane_normal = {}
    };
    return false;
  }

  *slot = cache_entry{
    .valid_until_tick = now_tick + ttl_ticks,
    .ok = true,
    .endpos = trace.endpos,
    .plane_normal = trace.plane.normal
  };

  *point_out = trace.endpos;
  if (plane_normal_out != nullptr) {
    *plane_normal_out = trace.plane.normal;
  }
  return true;
}

} // namespace splash_trace_cache

#endif
