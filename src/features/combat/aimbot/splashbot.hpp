#ifndef SPLASHBOT_HPP
#define SPLASHBOT_HPP
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include "games/tf2/sdk/interfaces/world_collision.hpp"
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
  float ground = 0.0f;
  splash_point_kind kind = splash_point_kind::geometry;
};

class splashbot final {
  static constexpr int max_cached_faces = 2048;
  static constexpr float duplicate_distance_sqr = 36.0f;
  static constexpr float dist_epsilon = 0.03125f;

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

  struct collect_key {
    int origin_x = 0;
    int origin_y = 0;
    int origin_z = 0;
    int radius = 0;
    int sphere = 0;
    int air_points = 0;
    bool air = false;
  };

  collect_key last_key_{};
  std::array<splash_candidate, 256> last_hits_{};
  int last_hit_count_ = 0;

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
      if (length_squared(candidates[index].point - point) <= duplicate_distance_sqr &&
          dot(candidates[index].normal, normal) > 0.92f) {
        return true;
      }
    }
    return false;
  }

  static int quantize(float value, float step) {
    return static_cast<int>(std::lround(value / step));
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

    if (distance > radius || facing < -0.15f || duplicate(out, count, point, normalized_normal)) {
      return false;
    }

    const float height_span = std::max(target.maxs.z - target.mins.z, 1.0f);
    const float ground = std::clamp(1.0f - (point.z - target.mins.z) / height_span, 0.0f, 1.0f);
    out[count++] = {point, normalized_normal,
                    std::clamp(1.0f - distance / radius, 0.0f, 1.0f), order, ground, kind};
    return true;
  }

  void reset_cache() {
    face_count_ = 0;
    next_face_order_ = 0.0f;
    cache_time_ = global_vars != nullptr ? global_vars->curtime : 0.0f;
    last_key_ = {};
    last_hit_count_ = 0;
  }

  void ensure_fresh() {
    const char* raw_name = engine != nullptr ? engine->get_level_name() : nullptr;
    const std::string_view current = raw_name != nullptr ? raw_name : std::string_view{};
    const float now = global_vars != nullptr ? global_vars->curtime : 0.0f;
    const bool restarted = now + 0.5f < last_curtime_;
    const bool expired = now - cache_time_ > 60.0f;
    last_curtime_ = now;
    if (current != map_name_ || restarted || expired) {
      map_name_.assign(current);
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
      if (length_squared(face.point - point) <= duplicate_distance_sqr &&
          dot(face.normal, normalized_normal) > 0.92f) {
        return;
      }
    }

    faces_[face_count_++] = {point, normalized_normal, next_face_order_};
    next_face_order_ += 1.0f;
  }

  bool world_trace(const Vec3& start, const Vec3& end, const Vec3& hull, trace_t& trace) const {
    if (engine_trace == nullptr) {
      return false;
    }
    Vec3 mins = hull * -1.0f;
    Vec3 maxs = hull;
    Vec3 trace_start = start;
    Vec3 trace_end = end;
    ray_t ray = engine_trace->init_ray(&trace_start, &trace_end, &mins, &maxs);
    trace_filter filter{};
    engine_trace->init_world_and_props_trace_filter(&filter);
    trace = {};
    engine_trace->trace_ray(&ray, MASK_SOLID | CONTENTS_DEBRIS, &filter, &trace);
    return true;
  }

  bool usable_hit(const trace_t& trace) const {
    return !trace.start_solid && !trace.all_solid && trace.fraction < 1.0f &&
      (trace.surface.flags & SURF_SKY) == 0u;
  }

  void consider_hit(const splash_target_state& target, float radius, const Vec3& hull,
                    const trace_t& trace, float order, splash_candidate* out, int& count,
                    int capacity) {
    if (!usable_hit(trace)) {
      return;
    }
    if (trace.entity == nullptr) {
      cache_face(trace.endpos, trace.plane.normal);
    }
    add_candidate(target, radius, hull, trace.endpos, trace.plane.normal, order,
                  splash_point_kind::geometry, out, count, capacity);
  }

  void probe_segment(const splash_target_state& target, float radius, const Vec3& hull,
                     const Vec3& start, const Vec3& end, float order, splash_candidate* out,
                     int& count, int capacity) {
    if (count >= capacity) {
      return;
    }
    trace_t trace{};
    if (!world_trace(start, end, hull, trace)) {
      return;
    }
    consider_hit(target, radius, hull, trace, order, out, count, capacity);
  }

  static Vec3 closest_point_on_triangle(const Vec3& point, const Vec3& a, const Vec3& b,
                                        const Vec3& c) {
    const Vec3 ab = b - a;
    const Vec3 ac = c - a;
    const Vec3 ap = point - a;
    const float d1 = dot(ab, ap);
    const float d2 = dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
      return a;
    }
    const Vec3 bp = point - b;
    const float d3 = dot(ab, bp);
    const float d4 = dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) {
      return b;
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
      return a + ab * (d1 / (d1 - d3));
    }
    const Vec3 cp = point - c;
    const float d5 = dot(ab, cp);
    const float d6 = dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) {
      return c;
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
      return a + ac * (d2 / (d2 - d6));
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
      return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    }
    const float denom = 1.0f / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
  }

  bool point_inside_solid(const Vec3& point) const {
    return engine_trace != nullptr && (engine_trace->get_point_contents(point) & MASK_SOLID) != 0;
  }

  void consider_surface(const splash_target_state& target, float radius, const Vec3& hull,
                        const Vec3& surface, const Vec3& normal, float order, splash_candidate* out,
                        int& count, int capacity) {
    if (length_squared(normal) <= 0.0001f) {
      return;
    }
    const Vec3 unit = aimbot_normalize_vector(normal);
    const Vec3 point = surface + unit * (hull_support(hull, unit) + dist_epsilon);
    if (point_inside_solid(point)) {
      return;
    }
    add_candidate(target, radius, hull, surface, unit, order, splash_point_kind::geometry, out, count,
                  capacity);
  }

  void sample_world_face(const world_face& face, const splash_target_state& target, float radius,
                         const Vec3& hull, splash_candidate* out, int& count, int capacity) {
    if (face.vertex_count < 3 || count >= capacity) {
      return;
    }
    const float radius_sqr = radius * radius;
    for (int index = 1; index + 1 < face.vertex_count && count < capacity; ++index) {
      const Vec3& a = face.vertices[0];
      const Vec3& b = face.vertices[index];
      const Vec3& c = face.vertices[index + 1];
      const Vec3 ab = b - a;
      const Vec3 ac = c - a;
      const float area = length(Vec3{
        ab.y * ac.z - ab.z * ac.y,
        ab.z * ac.x - ab.x * ac.z,
        ab.x * ac.y - ab.y * ac.x
      }) * 0.5f;
      const int samples = std::clamp(static_cast<int>(std::ceil(area / std::max(radius_sqr, 1.0f) * 4.0f)),
                                     1, 6);
      consider_surface(target, radius, hull, closest_point_on_triangle(target.origin, a, b, c),
                       face.normal, face.type == face_prop ? 50.0f : 10.0f, out, count, capacity);
      if (samples >= 2) {
        consider_surface(target, radius, hull, (a + b + c) * (1.0f / 3.0f), face.normal, 20.0f, out,
                         count, capacity);
      }
      if (samples >= 3) {
        consider_surface(target, radius, hull, (a + b) * 0.5f, face.normal, 30.0f, out, count, capacity);
        consider_surface(target, radius, hull, (b + c) * 0.5f, face.normal, 31.0f, out, count, capacity);
        consider_surface(target, radius, hull, (c + a) * 0.5f, face.normal, 32.0f, out, count, capacity);
      }
      if (samples >= 5) {
        consider_surface(target, radius, hull, a + ab * 0.25f + ac * 0.25f, face.normal, 40.0f, out,
                         count, capacity);
        consider_surface(target, radius, hull, a + ab * 0.5f + ac * 0.2f, face.normal, 41.0f, out,
                         count, capacity);
      }
    }
  }

  void collect_world_faces(const splash_target_state& target, float radius, const Vec3& hull,
                           splash_candidate* out, int& count, int capacity) {
    const Vec3 center = (target.mins + target.maxs) * 0.5f;
    const Vec3 mins = center - Vec3{radius, radius, radius};
    const Vec3 maxs = center + Vec3{radius, radius, radius};
    world_face faces[64]{};
    const int face_count = world_faces::get_faces_in_aabb(mins, maxs, MASK_SOLID, faces, 64, true);
    for (int index = 0; index < face_count && count < capacity; ++index) {
      sample_world_face(faces[index], target, radius, hull, out, count, capacity);
      if (faces[index].vertex_count >= 3) {
        Vec3 centroid{};
        for (int vertex = 0; vertex < faces[index].vertex_count; ++vertex) {
          centroid += faces[index].vertices[vertex];
        }
        centroid = centroid * (1.0f / static_cast<float>(faces[index].vertex_count));
        cache_face(centroid, faces[index].normal);
      }
    }
  }

  void collect_face_points(const splash_target_state& target, float radius, const Vec3& hull,
                           splash_candidate* out, int& count, int capacity) {
    for (int index = 0; index < face_count_ && count < capacity; ++index) {
      const cached_face& face = faces_[index];
      if (overlaps(face.point, target, radius)) {
        add_candidate(target, radius, hull, face.point, face.normal, face.order,
                      splash_point_kind::geometry, out, count, capacity);
      }
    }
  }

  void collect_volume_points(const splash_target_state& target, float radius, const Vec3& hull,
                             bool air_splash, int air_point_count, int sphere_samples,
                             const Vec3& local_eye, splash_candidate* out, int& count,
                             int capacity) {
    const Vec3 center = (target.mins + target.maxs) * 0.5f;
    const Vec3 eye = target.eye;
    const float hull_length = std::max(length(hull), 1.0f);
    const float reach = radius + hull_length + 2.0f;
    const Vec3 shooter = local_eye - eye;
    float order = 1000.0f;

    const Vec3 corners[8] = {
      {target.mins.x, target.mins.y, target.mins.z},
      {target.maxs.x, target.mins.y, target.mins.z},
      {target.mins.x, target.maxs.y, target.mins.z},
      {target.maxs.x, target.maxs.y, target.mins.z},
      {target.mins.x, target.mins.y, target.maxs.z},
      {target.maxs.x, target.mins.y, target.maxs.z},
      {target.mins.x, target.maxs.y, target.maxs.z},
      {target.maxs.x, target.maxs.y, target.maxs.z}
    };
    for (const Vec3& corner : corners) {
      const Vec3 direction = aimbot_normalize_vector(corner - center);
      probe_segment(target, radius, hull, center, center + direction * reach, order++, out, count,
                    capacity);
    }

    const Vec3 axes[6] = {
      {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
      {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
      {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}
    };
    for (const Vec3& axis : axes) {
      probe_segment(target, radius, hull, eye, eye + axis * reach, order++, out, count, capacity);
    }

    const float ring_radius = std::max(std::max(target.maxs.x - target.mins.x,
                                                target.maxs.y - target.mins.y) * 0.5f, 8.0f);
    const Vec3 feet{center.x, center.y, target.mins.z + 2.0f};
    for (int spoke = 0; spoke < 8; ++spoke) {
      const float angle = (6.28318530718f / 8.0f) * static_cast<float>(spoke);
      const Vec3 offset{std::cos(angle) * ring_radius, std::sin(angle) * ring_radius, 0.0f};
      const Vec3 ring = feet + offset;
      probe_segment(target, radius, hull, ring, ring + Vec3{0.0f, 0.0f, -72.0f}, order++, out,
                    count, capacity);
      probe_segment(target, radius, hull, feet, ring + Vec3{0.0f, 0.0f, 0.0f} +
                    aimbot_normalize_vector(offset) * radius, order++, out, count, capacity);
    }

    const int samples = std::clamp(sphere_samples, 8, 96);
    const float golden_angle = 3.14159265358979f * (3.0f - std::sqrt(5.0f));
    for (int sample = 0; sample < samples && count < capacity; ++sample) {
      const float height = sample > 0 ? 1.0f - (static_cast<float>(sample) /
        static_cast<float>(samples - 1)) * 2.0f : -1.0f;
      const float ring = std::sqrt(std::max(0.0f, 1.0f - height * height));
      const float theta = golden_angle * static_cast<float>(sample);
      const Vec3 direction{
        std::cos(theta) * ring,
        std::sin(theta) * ring,
        height
      };
      const Vec3 far_point = center + direction * reach;

      trace_t outward{};
      if (world_trace(eye, far_point, hull, outward)) {
        consider_hit(target, radius, hull, outward, 200000.0f + static_cast<float>(sample), out,
                     count, capacity);
      }

      const bool missed = !usable_hit(outward);
      if (missed && air_splash) {
        const int shells = std::clamp(air_point_count, 1, 6);
        for (int shell = shells; shell >= 1 && count < capacity; --shell) {
          const float fraction = static_cast<float>(shell) / static_cast<float>(shells);
          const Vec3 air_point = center + direction * radius * fraction;
          const Vec3 nearest = nearest_point(air_point, target.mins, target.maxs);
          const float distance = distance_3d(air_point, nearest);
          if (distance > radius) {
            continue;
          }
          const float height_span = std::max(target.maxs.z - target.mins.z, 1.0f);
          out[count++] = {air_point, direction * -1.0f,
                          std::clamp(1.0f - distance / radius, 0.0f, 1.0f),
                          300000.0f + static_cast<float>(sample) + fraction,
                          std::clamp(1.0f - (air_point.z - target.mins.z) / height_span, 0.0f, 1.0f),
                          splash_point_kind::air};
        }
      }

      if (dot(shooter, direction) >= 0.0f) {
        probe_segment(target, radius, hull, far_point, eye,
                      150000.0f + static_cast<float>(sample), out, count, capacity);
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

  static void sort_candidates(splash_candidate* out, int count) {
    std::sort(out, out + count, [](const splash_candidate& left, const splash_candidate& right) {
      if (left.kind != right.kind) {
        return left.kind == splash_point_kind::geometry;
      }
      const float left_score = left.falloff * 4.0f + left.ground;
      const float right_score = right.falloff * 4.0f + right.ground;
      if (left_score != right_score) {
        return left_score > right_score;
      }
      return left.order < right.order;
    });
  }

  int keep_visible(const Vec3& local_eye, splash_candidate* out, int count) const {
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
    sort_candidates(out, write_index);
    return write_index;
  }

  int collect_candidates(const splash_target_state& target, float radius, const Vec3& hull,
                         bool air_splash, int air_point_count, const Vec3& local_eye,
                         splash_candidate* out, int capacity, int sphere_samples = 32) {
    ensure_fresh();
    if (out == nullptr || capacity <= 0 || radius <= 0.0f) {
      return 0;
    }

    const collect_key key{
      quantize(target.origin.x, 24.0f), quantize(target.origin.y, 24.0f),
      quantize(target.origin.z, 24.0f), quantize(radius, 8.0f),
      sphere_samples, air_point_count, air_splash
    };
    if (last_hit_count_ > 0 &&
        key.origin_x == last_key_.origin_x && key.origin_y == last_key_.origin_y &&
        key.origin_z == last_key_.origin_z && key.radius == last_key_.radius &&
        key.sphere == last_key_.sphere && key.air_points == last_key_.air_points &&
        key.air == last_key_.air) {
      const int copy = std::min(last_hit_count_, capacity);
      std::copy(last_hits_.begin(), last_hits_.begin() + copy, out);
      return keep_visible(local_eye, out, copy);
    }

    int count = 0;
    collect_world_faces(target, radius, hull, out, count, capacity);
    collect_face_points(target, radius, hull, out, count, capacity);
    collect_volume_points(target, radius, hull, air_splash, air_point_count, sphere_samples,
                          local_eye, out, count, capacity);

    last_key_ = key;
    last_hit_count_ = std::min(count, static_cast<int>(last_hits_.size()));
    std::copy(out, out + last_hit_count_, last_hits_.begin());
    return keep_visible(local_eye, out, count);
  }
};

inline splashbot splashbot_instance{};

}
}
#endif
