#ifndef SPLASHBOT_HPP
#define SPLASHBOT_HPP
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include "projectile_helpers.hpp"

namespace projectile_aim {
namespace detail {

struct splash_target_state {
  Vec3 origin{};
  Vec3 mins{};
  Vec3 maxs{};
  Vec3 body{};
  Vec3 eye{};
};

enum class splash_point_kind : std::uint8_t {
  geometry,
  air
};

struct splash_candidate {
  Vec3 point{};
  Vec3 normal{};
  float falloff = 0.0f;
  float order = 0.0f;
  splash_point_kind kind = splash_point_kind::geometry;
};

class splashbot final {
  static constexpr int max_cached_faces = 2048;

  struct cached_face {
    Vec3 point{};
    Vec3 normal{};
    float order = 0.0f;
  };

  std::array<cached_face, max_cached_faces> faces_{};
  int face_count_ = 0;
  float next_face_order_ = 0.0f;
  std::string map_name_{};
  float last_curtime_ = 0.0f;
  float cache_time_ = 0.0f;

  static Vec3 nearest_point(const Vec3& point, const Vec3& mins, const Vec3& maxs) {
    return {
      std::clamp(point.x, mins.x, maxs.x),
      std::clamp(point.y, mins.y, maxs.y),
      std::clamp(point.z, mins.z, maxs.z)
    };
  }

  static float hull_support(const Vec3& hull, const Vec3& normal) {
    return std::fabs(hull.x * normal.x) + std::fabs(hull.y * normal.y) +
      std::fabs(hull.z * normal.z);
  }

  static bool overlaps(const Vec3& point, const splash_target_state& target, float radius) {
    return point.x >= target.mins.x - radius && point.x <= target.maxs.x + radius &&
      point.y >= target.mins.y - radius && point.y <= target.maxs.y + radius &&
      point.z >= target.mins.z - radius && point.z <= target.maxs.z + radius;
  }

  static bool duplicate(const splash_candidate* candidates, int count, const Vec3& point,
                        const Vec3& normal) {
    for (int index = 0; index < count; ++index) {
      if (length_squared(candidates[index].point - point) <= 1.0f &&
          dot(candidates[index].normal, normal) > 0.99f) {
        return true;
      }
    }
    return false;
  }

  bool add_candidate(const splash_target_state& target, float radius, const Vec3& hull,
                     const Vec3& surface_point, const Vec3& normal, float order,
                     splash_point_kind kind, splash_candidate* out, int& count,
                     int capacity) const {
    if (out == nullptr || count >= capacity || radius <= 0.0f ||
        length_squared(normal) <= 0.0001f) {
      return false;
    }

    const Vec3 normalized_normal = aimbot_normalize_vector(normal);
    const Vec3 point = surface_point + normalized_normal * hull_support(hull, normalized_normal);
    const Vec3 nearest = nearest_point(point, target.mins, target.maxs);
    const float distance = distance_3d(point, nearest);
    const Vec3 to_body = target.body - surface_point;
    const float facing = dot(normalized_normal, to_body);

    if (distance > radius || facing < -0.01f || duplicate(out, count, point, normalized_normal)) {
      return false;
    }

    out[count++] = {point, normalized_normal,
                    std::clamp(1.0f - distance / radius, 0.0f, 1.0f), order, kind};
    return true;
  }

  void reset_cache() {
    face_count_ = 0;
    next_face_order_ = 0.0f;
    cache_time_ = global_vars != nullptr ? global_vars->curtime : 0.0f;
  }

  void ensure_fresh() {
    const char* raw_name = engine != nullptr ? engine->get_level_name() : nullptr;
    const std::string current = raw_name != nullptr ? std::string(raw_name) : std::string{};
    const float now = global_vars != nullptr ? global_vars->curtime : 0.0f;
    const bool restarted = now + 0.5f < last_curtime_;
    const bool expired = now - cache_time_ > 60.0f;
    last_curtime_ = now;
    if (current != map_name_ || restarted || expired) {
      map_name_ = current;
      reset_cache();
    }
  }

  void cache_face(const Vec3& point, const Vec3& normal) {
    if (face_count_ >= max_cached_faces || length_squared(normal) <= 0.0001f) {
      return;
    }

    const Vec3 normalized_normal = aimbot_normalize_vector(normal);
    for (int index = 0; index < face_count_; ++index) {
      const cached_face& face = faces_[index];
      if (length_squared(face.point - point) <= 1.0f &&
          dot(face.normal, normalized_normal) > 0.99f) {
        return;
      }
    }

    faces_[face_count_++] = {point, normalized_normal, next_face_order_};
    next_face_order_ += 1.0f;
  }

  void collect_face_points(const splash_target_state& target, float radius, const Vec3& hull,
                           splash_candidate* out, int& count, int capacity) {
    if (engine_trace == nullptr) {
      return;
    }

    const Vec3 center = (target.mins + target.maxs) * 0.5f;
    const Vec3 extents = (target.maxs - target.mins) * 0.5f;
    const std::array<Vec3, 15> probes{
      center,
      {target.mins.x, center.y, center.z}, {target.maxs.x, center.y, center.z},
      {center.x, target.mins.y, center.z}, {center.x, target.maxs.y, center.z},
      {center.x, center.y, target.mins.z}, {center.x, center.y, target.maxs.z},
      {center.x - extents.x, center.y - extents.y, center.z},
      {center.x - extents.x, center.y + extents.y, center.z},
      {center.x + extents.x, center.y - extents.y, center.z},
      {center.x + extents.x, center.y + extents.y, center.z},
      {center.x, center.y - extents.y, center.z - extents.z},
      {center.x, center.y + extents.y, center.z - extents.z},
      {center.x, center.y - extents.y, center.z + extents.z},
      {center.x, center.y + extents.y, center.z + extents.z}
    };
    const std::array<Vec3, 14> directions{
      Vec3{1.0f, 0.0f, 0.0f}, Vec3{-1.0f, 0.0f, 0.0f},
      Vec3{0.0f, 1.0f, 0.0f}, Vec3{0.0f, -1.0f, 0.0f},
      Vec3{0.0f, 0.0f, 1.0f}, Vec3{0.0f, 0.0f, -1.0f},
      aimbot_normalize_vector(Vec3{1.0f, 1.0f, 0.0f}),
      aimbot_normalize_vector(Vec3{1.0f, -1.0f, 0.0f}),
      aimbot_normalize_vector(Vec3{-1.0f, 1.0f, 0.0f}),
      aimbot_normalize_vector(Vec3{-1.0f, -1.0f, 0.0f}),
      aimbot_normalize_vector(Vec3{1.0f, 0.0f, 1.0f}),
      aimbot_normalize_vector(Vec3{-1.0f, 0.0f, 1.0f}),
      aimbot_normalize_vector(Vec3{0.0f, 1.0f, 1.0f}),
      aimbot_normalize_vector(Vec3{0.0f, -1.0f, 1.0f})
    };

    for (int index = 0; index < face_count_ && count < capacity; ++index) {
      const cached_face& face = faces_[index];
      if (overlaps(face.point, target, radius)) {
        add_candidate(target, radius, hull, face.point, face.normal, face.order,
                      splash_point_kind::geometry, out, count, capacity);
      }
    }

    const float hull_length = length(hull);
    float probe_order = 100000.0f;
    for (const Vec3& probe : probes) {
      for (const Vec3& direction : directions) {
        Vec3 start = probe;
        Vec3 end = probe + direction * (radius + hull_length + 2.0f);
        ray_t ray = engine_trace->init_ray(&start, &end);
        trace_filter filter{};
        engine_trace->init_world_and_props_trace_filter(&filter);
        trace_t trace{};
        engine_trace->trace_ray(&ray, MASK_SOLID | CONTENTS_DEBRIS, &filter, &trace);
        if (trace.start_solid || trace.all_solid || trace.fraction >= 1.0f ||
            (trace.surface.flags & 0x0004u) != 0u) {
          continue;
        }
        if (trace.entity == nullptr) {
          cache_face(trace.endpos, trace.plane.normal);
        }
        add_candidate(target, radius, hull, trace.endpos, trace.plane.normal,
                      probe_order++, splash_point_kind::geometry, out, count, capacity);
        if (count >= capacity) {
          break;
        }
      }
      if (count >= capacity) {
        break;
      }
    }
  }

public:
  void invalidate() {
    reset_cache();
    map_name_.clear();
  }

  bool exposure_clear(const Vec3& surface_point, const Vec3& normal, float normal_offset,
                      Entity* skip_entity, const Vec3& target_eye) const {
    if (engine_trace == nullptr) {
      return false;
    }

    constexpr float exposure_epsilon = 0.03125f;
    Vec3 start = surface_point + aimbot_normalize_vector(normal) *
      (exposure_epsilon + std::max(normal_offset, 0.0f));
    Vec3 end = target_eye;
    ray_t ray = engine_trace->init_ray(&start, &end);
    trace_filter filter{};
    engine_trace->init_trace_filter(&filter, skip_entity);
    trace_t trace{};
    engine_trace->trace_ray(&ray, MASK_SHOT, &filter, &trace);
    return !trace.start_solid && !trace.all_solid && trace.fraction >= 1.0f;
  }

  int collect_candidates(const splash_target_state& target, float radius, const Vec3& hull,
                         bool air_splash, int air_point_count, const Vec3& local_eye,
                         splash_candidate* out, int capacity) {
    ensure_fresh();
    if (out == nullptr || capacity <= 0 || radius <= 0.0f) {
      return 0;
    }

    int count = 0;
    collect_face_points(target, radius, hull, out, count, capacity);
    if (engine_trace == nullptr) {
      return count;
    }

    const Vec3 center = (target.mins + target.maxs) * 0.5f;
    const int sphere_samples = std::clamp(air_splash ? 21 : 14, 4, 64);
    const float golden_angle = 3.14159265358979f * (3.0f - std::sqrt(5.0f));
    const float hull_length = length(hull);

    for (int sample = 0; sample < sphere_samples && count < capacity; ++sample) {
      const float height = sample > 0 ? 1.0f - (static_cast<float>(sample) /
        static_cast<float>(sphere_samples - 1)) * 2.0f : 0.0f;
      const float ring_radius = std::sqrt(std::max(0.0f, 1.0f - height * height));
      const float theta = golden_angle * static_cast<float>(sample);
      const Vec3 direction{
        std::cos(theta) * ring_radius,
        height,
        std::sin(theta) * ring_radius
      };

      Vec3 start = center;
      Vec3 end = center + direction * (radius + hull_length + 2.0f);
      ray_t ray = engine_trace->init_ray(&start, &end);
      trace_filter filter{};
      engine_trace->init_world_and_props_trace_filter(&filter);
      trace_t trace{};
      engine_trace->trace_ray(&ray, MASK_SOLID | CONTENTS_DEBRIS, &filter, &trace);
      const bool hit_surface = !trace.start_solid && !trace.all_solid &&
        trace.fraction < 1.0f && (trace.surface.flags & 0x0004u) == 0u;

      if (hit_surface) {
        add_candidate(target, radius, hull, trace.endpos, trace.plane.normal,
                      200000.0f + static_cast<float>(sample), splash_point_kind::geometry,
                      out, count, capacity);
        if (trace.entity == nullptr) {
          cache_face(trace.endpos, trace.plane.normal);
        }
      } else if (air_splash && count < capacity) {
        for (int fraction_index = 1; fraction_index <= air_point_count && count < capacity;
             ++fraction_index) {
          const float fraction =
            static_cast<float>(fraction_index) / static_cast<float>(air_point_count);
          const Vec3 air_point = center + direction * radius * fraction;
          out[count++] = {air_point, direction * -1.0f,
                          1.0f - fraction * 0.25f,
                          300000.0f + static_cast<float>(sample),
                          splash_point_kind::air};
        }
      }
    }

    int write_index = 0;
    for (int index = 0; index < count; ++index) {
      const splash_candidate& candidate = out[index];
      if (candidate.kind != splash_point_kind::air &&
          length_squared(local_eye - candidate.point) > 1.0f &&
          dot(candidate.normal, aimbot_normalize_vector(local_eye - candidate.point)) <= 0.0f) {
        continue;
      }
      out[write_index++] = candidate;
    }
    count = write_index;

    std::sort(out, out + count, [](const splash_candidate& left, const splash_candidate& right) {
      if (left.kind != right.kind) {
        return left.kind == splash_point_kind::geometry;
      }
      if (left.falloff != right.falloff) {
        return left.falloff > right.falloff;
      }
      return left.order < right.order;
    });
    return count;
  }
};

inline splashbot splashbot_instance{};

}
}
#endif
