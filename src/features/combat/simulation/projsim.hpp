#ifndef PROJSIM_HPP
#define PROJSIM_HPP
#include <algorithm>
#include <cmath>
#include <vector>
#include "core/math/math.hpp"
#include "core/types.hpp"
#include "games/tf2/sdk/entities/entity.hpp"
#include "games/tf2/sdk/interfaces/engine_trace.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"

namespace projsim {

namespace detail {

inline float tick_interval() {
  if (global_vars != nullptr && std::isfinite(global_vars->interval_per_tick) &&
      global_vars->interval_per_tick > 0.0001f) {
    return global_vars->interval_per_tick;
  }
  return 0.015f;
}

}

struct params {
  Vec3 origin{};
  Vec3 velocity{};
  float gravity = 800.0f;
  float drag = 0.0f;
  Vec3 hull{2.0f, 2.0f, 2.0f};
  unsigned int collision_mask = MASK_SOLID | CONTENTS_DEBRIS | CONTENTS_HITBOX;
  trace_filter filter{};
  bool ignore_target = false;
  Entity* local_player = nullptr;
};

struct simulation {
  params p{};
  Vec3 position{};
  Vec3 velocity{};
  int tick = 0;
  std::vector<Vec3> path{};
  trace_t last_trace{};
  bool stopped = false;

  void reset(const params& value) {
    p = value;
    position = p.origin;
    velocity = p.velocity;
    tick = 0;
    stopped = false;
    path.clear();
    last_trace = {};
  }

  bool step() {
    const float dt = detail::tick_interval();
    Vec3 next = position + velocity * dt;
    next.z -= 0.5f * p.gravity * dt * dt;

    if (engine_trace != nullptr) {
      Vec3 mins = p.hull * -1.0f;
      Vec3 maxs = p.hull;
      Vec3 start = position;
      Vec3 end = next;
      ray_t ray = engine_trace->init_ray(&start, &end, &mins, &maxs);
      trace_t trace{};
      engine_trace->trace_ray(&ray, p.collision_mask, &p.filter, &trace);
      last_trace = trace;
      if (trace.start_solid || trace.all_solid || trace.fraction < 1.0f) {
        stopped = true;
        position = trace.endpos;
      } else {
        position = next;
      }
    } else {
      last_trace = {};
      position = next;
    }

    velocity.z -= p.gravity * dt;
    if (p.drag > 0.0f) {
      velocity = velocity * std::clamp(1.0f - p.drag * dt, 0.5f, 1.0f);
    }

    ++tick;
    path.push_back(position);
    return !stopped;
  }
};

}
#endif
