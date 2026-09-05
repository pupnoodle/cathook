#ifndef PROJECTILE_AIM_HPP
#define PROJECTILE_AIM_HPP
#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <vector>
#include "core/types.hpp"
#include "features/combat/simulation/movesim.hpp"
#include "features/combat/simulation/projsim.hpp"
#include "aimbot.hpp"
#include "aim_state.hpp"
#include "aim_utils.hpp"
#include "projectile_helpers.hpp"
#include "splashbot.hpp"
#include "core/entity_cache.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/interfaces/engine_trace.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/prediction.hpp"

namespace projectile_aim {

inline struct settings {
  bool strafe_prediction = true;
  bool hitchance_gate = true;
  float hitchance_minimum = 0.35f;
  int direct_trace_interval = 1;
  int splash_trace_interval = 5;
  int geometry_trace_interval = 10;
  bool interval_retest = true;
  int splash_restrict_direct = 16;
  int splash_restrict_arc = 24;
  int splash_restrict_first = 40;
  int direct_sphere_points = 14;
  int arc_sphere_points = 21;
  int air_point_count = 3;
  bool air_splash = true;
  bool sticky_arm_time = true;
  bool huntsman_pull_point = true;
  bool lob_angles = true;
  bool lob_underpredict = false;
  bool cannon_hitcharge = true;
  bool beggars_clip_guard = true;
  bool beggars_wall_guard = true;
  bool rocket_jump_radius = true;
  float splash_radius_scale = 100.0f;
} cfg{};

namespace detail {

inline bool projectile_fov_exceeds_limit(float fov);

struct target_seed {
  Entity* entity = nullptr;
  Player* player = nullptr;
  Vec3 origin{};
  Vec3 aim_offset{};
  Vec3 velocity{};
  Vec3 view_offset{};
  Vec3 bounds_mins{};
  Vec3 bounds_maxs{};
  float current_fov = FLT_MAX;
  float distance = FLT_MAX;
  bool preferred = false;
};

struct movement_path {
  movesim::storage simulation{};
  bool simulated = false;
  bool failed = false;
};

struct movesim_guard {
  movesim::storage* storage = nullptr;
  bool owned = false;

  explicit movesim_guard(movesim::storage& value) : storage(&value), owned(true) {}
  ~movesim_guard() {
    if (owned && storage != nullptr) {
      movesim::restore(*storage);
    }
  }
  movesim_guard(const movesim_guard&) = delete;
  movesim_guard& operator=(const movesim_guard&) = delete;
};

enum class calc_state : std::uint8_t {
  pending,
  good,
  timing,
  bad
};

struct point_solution {
  calc_state calculated = calc_state::pending;
  float pitch = 0.0f;
  float yaw = 0.0f;
  float time = 0.0f;
  Vec3 launch{};
  Vec3 launch_angles{};
};

inline Vec3& entity_obb_mins(Entity* entity) {
  void* collideable = entity->get_collideable();
  void** vtable = *(void***)collideable;
  Vec3& (*obb_mins_fn)(void*) = (Vec3 & (*)(void*))vtable[3];
  return obb_mins_fn(collideable);
}

inline Vec3& entity_obb_maxs(Entity* entity) {
  void* collideable = entity->get_collideable();
  void** vtable = *(void***)collideable;
  Vec3& (*obb_maxs_fn)(void*) = (Vec3 & (*)(void*))vtable[4];
  return obb_maxs_fn(collideable);
}

class target_bounds_guard {
public:
  target_bounds_guard(Entity* entity, const Vec3& predicted_origin)
    : entity_(entity),
      mins_(entity != nullptr ? entity->get_collideable_mins() : Vec3{}),
      maxs_(entity != nullptr ? entity->get_collideable_maxs() : Vec3{}),
      origin_(entity != nullptr ? entity->get_abs_origin() : Vec3{}) {
    if (entity_ == nullptr || entity_->get_collideable() == nullptr) {
      return;
    }
    Vec3& mins = entity_obb_mins(entity_);
    Vec3& maxs = entity_obb_maxs(entity_);
    mins = {std::clamp(mins.x, -24.0f, 0.0f), std::clamp(mins.y, -24.0f, 0.0f), mins.z};
    maxs = {std::clamp(maxs.x, 0.0f, 24.0f), std::clamp(maxs.y, 0.0f, 24.0f), maxs.z};
    entity_->set_abs_origin(predicted_origin);
    active_ = true;
  }

  ~target_bounds_guard() {
    if (!active_) {
      return;
    }
    entity_obb_mins(entity_) = mins_;
    entity_obb_maxs(entity_) = maxs_;
    entity_->set_abs_origin(origin_);
  }

  target_bounds_guard(const target_bounds_guard&) = delete;
  target_bounds_guard& operator=(const target_bounds_guard&) = delete;

private:
  Entity* entity_ = nullptr;
  Vec3 mins_{};
  Vec3 maxs_{};
  Vec3 origin_{};
  bool active_ = false;
};

inline Vec3 path_origin(const movement_path& path, const target_seed& seed, float seconds) {
  const std::vector<Vec3>& origins = path.simulation.path;
  if (!origins.empty()) {
    const float tick = std::max(seconds, 0.0f) / interval();
    const float last_tick = static_cast<float>(origins.size() - 1);
    if (tick > last_tick && finite(path.simulation.terminal_velocity)) {
      return origins.back() +
        path.simulation.terminal_velocity * ((tick - last_tick) * interval());
    }
    const std::size_t low =
      std::min(static_cast<std::size_t>(tick), origins.size() - 1);
    const std::size_t high = std::min(low + 1, origins.size() - 1);
    const float fraction = std::clamp(tick - static_cast<float>(low), 0.0f, 1.0f);
    return origins[low] + (origins[high] - origins[low]) * fraction;
  }
  return seed.origin + seed.velocity * seconds;
}

inline float splash_radius_for(Player* local, const projectile_info& info) {
  if (info.splash_radius <= 0.0f || cfg.splash_radius_scale <= 0.0f) {
    return 0.0f;
  }
  float radius = info.splash_radius;
  if (cfg.rocket_jump_radius && is_rocket_weapon(info.weapon_id_value) &&
      local != nullptr && local->in_cond(TF_COND_BLASTJUMPING)) {
    radius *= 0.8f;
  }
  return radius * std::clamp(cfg.splash_radius_scale, 10.0f, 300.0f) * 0.01f;
}

inline float sticky_air_radius_scale(int sim_tick) {
  const float livetime = game_convar_float("tf_grenadelauncher_livetime", 0.8f);
  const float ramp = game_convar_float("tf_sticky_radius_ramp_time", 0.15f);
  const float airdet = game_convar_float("tf_sticky_airdet_radius", 0.4f);
  return remap_val_clamped(ticks_to_time(sim_tick), livetime, livetime + ramp, airdet, 1.0f);
}

inline splash_target_state make_target_state(const target_seed& seed, const Vec3& origin) {
  splash_target_state state{};
  state.origin = origin;
  state.mins = origin + seed.bounds_mins;
  state.maxs = origin + seed.bounds_maxs;
  state.body = origin + seed.aim_offset;
  state.eye = origin + seed.view_offset;
  return state;
}

inline bool trace_hull_segment(Player* local, const projectile_info& info, Entity* ignored_target,
                               const Vec3& start, const Vec3& end, trace_t& out) {
  if (engine_trace == nullptr) {
    return false;
  }
  Vec3 mins = info.hull * -1.0f;
  Vec3 maxs = info.hull;
  Vec3 trace_start = start;
  Vec3 trace_end = end;
  ray_t ray = engine_trace->init_ray(&trace_start, &trace_end, &mins, &maxs);
  trace_filter filter{};
  engine_trace->init_projectile_trace_filter(&filter, local->to_entity(), ignored_target,
                                             ignored_target != nullptr);
  out = {};
  engine_trace->trace_ray(&ray, info.collision_mask, &filter, &out);
  return true;
}

struct shot_test {
  Vec3 launch{};
  Vec3 velocity{};
  float drag = 0.0f;
  Entity* target = nullptr;
  Vec3 predicted_origin{};
  splash_target_state state{};
  Vec3 aim_point{};
  int sim_ticks = 1;
  int kind = 0;
  float radius_sqr = 0.0f;
  float normal_offset = 0.0f;
  int trace_interval = 1;
  bool interval_retest = false;
};

inline bool validate_shot(Player* local, const projectile_info& info, const shot_test& test) {
  if (local == nullptr || engine_trace == nullptr || test.target == nullptr ||
      test.sim_ticks <= 0) {
    return false;
  }

  const bool direct = test.kind == 0;
  Entity* ignored_target = direct ? nullptr : test.target;

  projsim::params params{};
  params.origin = test.launch;
  params.velocity = test.velocity;
  params.gravity = 800.0f * info.gravity_mod;
  params.drag = test.drag;
  params.hull = info.hull;
  params.collision_mask = info.collision_mask;
  params.local_player = local->to_entity();
  params.ignore_target = !direct;
  engine_trace->init_projectile_trace_filter(&params.filter, local->to_entity(),
                                             ignored_target, !direct);

  projsim::simulation simulation{};
  simulation.reset(params);

  const target_bounds_guard bounds_guard(test.target, test.predicted_origin);

  const int tolerance_ticks =
    std::max(time_to_ticks(length(test.state.maxs - test.state.mins) /
                           std::max(length(test.velocity), 1.0f)), 1);
  Vec3 previous = simulation.position;

  for (int tick = 1; tick <= test.sim_ticks; ++tick) {
    const Vec3 segment_start = previous;
    if (!simulation.step()) {
      return false;
    }
    const Vec3 current = simulation.position;
    previous = current;
    const bool sweep = test.trace_interval <= 1 || (tick % test.trace_interval) == 0 ||
      tick == test.sim_ticks;
    if (!sweep) {
      continue;
    }

    trace_t segment{};
    if (!trace_hull_segment(local, info, ignored_target, segment_start, current, segment)) {
      return false;
    }
    const bool solid_hit = segment.start_solid || segment.all_solid ||
      segment.fraction < 1.0f || segment.entity != nullptr;
    bool candidate_hit = false;
    switch (test.kind) {
    case 0:
    case 1:
      candidate_hit = solid_hit;
      break;
    default:
      candidate_hit = length_squared(current - test.aim_point) < test.radius_sqr || solid_hit;
      break;
    }
    if (!candidate_hit) {
      continue;
    }

    const Vec3 endpos = solid_hit ? segment.endpos : current;
    switch (test.kind) {
    case 0: {
      const bool valid = segment.entity == test.target &&
        (test.sim_ticks - tick) < tolerance_ticks;
      if (!valid) {
        return false;
      }
      if (test.interval_retest && test.trace_interval > 1) {
        const std::vector<Vec3>& path = simulation.path;
        const int available = static_cast<int>(path.size());
        const int limit = std::min(tick, available - 1);
        for (int replay = 1; replay <= limit; ++replay) {
          trace_t retest_trace{};
          if (!trace_hull_segment(local, info, ignored_target, path[replay - 1],
                                  path[replay], retest_trace)) {
            return false;
          }
          if (retest_trace.start_solid || retest_trace.all_solid ||
              retest_trace.fraction < 1.0f || retest_trace.entity != nullptr) {
            return false;
          }
        }
      }
      return true;
    }
    case 1: {
      const bool valid = length_squared(endpos - test.aim_point) < test.radius_sqr &&
        splashbot_instance.exposure_clear(endpos, segment.plane.normal,
                                          test.normal_offset, test.target,
                                          test.state.eye);
      return valid;
    }
    default:
      return !solid_hit;
    }
  }

  return false;
}

inline point_solution solve_point(Player* local, const projectile_info& info, const Vec3& eye,
                                  const Vec3& point, bool lob, bool two_pass, float drag) {
  point_solution result{};
  float pitch_command = 0.0f;
  float yaw_command = 0.0f;
  float time = 0.0f;
  if (!solve_ballistic(info, eye, point, drag, lob, pitch_command, yaw_command, time)) {
    result.calculated = calc_state::bad;
    return result;
  }

  const bool needs_two_pass = two_pass && info.launch != launch_type::bat;
  if (!needs_two_pass) {
    result.calculated = calc_state::good;
    result.pitch = pitch_command;
    result.yaw = yaw_command;
    result.time = time;
    return result;
  }

  const bool ignore_friendlies = !is_rocket_weapon(info.weapon_id_value);
  Vec3 launch{};
  Vec3 launch_angles{};
  if (!launch_position(local, info, {pitch_command, yaw_command, 0.0f}, ignore_friendlies,
                       launch, &launch_angles)) {
    result.calculated = calc_state::bad;
    return result;
  }

  float muzzle_pitch = 0.0f;
  float muzzle_yaw = 0.0f;
  float muzzle_time = 0.0f;
  if (!solve_ballistic(info, launch, point, drag, lob, muzzle_pitch, muzzle_yaw, muzzle_time)) {
    result.calculated = calc_state::bad;
    return result;
  }

  if (info.launch == launch_type::fire_setup && length_squared(info.offset) > 0.0001f) {
    Vec3 forward{};
    angle_vectors(launch_angles, &forward, nullptr, nullptr);

    const Vec3 shoot_offset = launch - eye;
    const Vec3 target_offset = point - eye;
    const Vec3 forward_xy = normalized({forward.x, forward.y, 0.0f});
    float corrected_yaw = muzzle_yaw;
    if (length_squared(forward_xy) > 0.0001f) {
      const Vec3 shoot_xy{shoot_offset.x, shoot_offset.y, 0.0f};
      const Vec3 target_xy{target_offset.x, target_offset.y, 0.0f};
      float root = 0.0f;
      if (solve_quadratic_front_root(1.0f, 2.0f * dot(shoot_xy, forward_xy),
                                     length_squared(shoot_xy) - length_squared(target_xy),
                                     root)) {
        const Vec3 shifted = shoot_xy + forward_xy * root;
        corrected_yaw = std::atan2(shifted.y, shifted.x) * radpi;
      }
    }
    yaw_command = corrected_yaw;

    if (800.0f * info.gravity_mod > 0.001f) {
      pitch_command = muzzle_pitch + (pitch_command - launch_angles.x);
    } else {
      const float cyaw = std::cos(yaw_command * pideg);
      const float syaw = std::sin(yaw_command * pideg);
      const auto flatten = [cyaw, syaw](const Vec3& value) {
        return Vec3{value.x * cyaw + value.y * syaw, 0.0f, value.z};
      };
      const Vec3 shoot_plane = flatten(shoot_offset);
      const Vec3 target_plane = flatten(target_offset);
      const Vec3 forward_plane_raw = flatten(forward);
      Vec3 forward_plane = forward_plane_raw;
      const float plane_length =
        std::sqrt(forward_plane.x * forward_plane.x + forward_plane.z * forward_plane.z);
      if (plane_length > 0.0001f) {
        forward_plane = Vec3{forward_plane.x / plane_length, 0.0f,
                             forward_plane.z / plane_length};
        const float planar_b =
          2.0f * (shoot_plane.x * forward_plane.x + shoot_plane.z * forward_plane.z);
        const float planar_c = (shoot_plane.x * shoot_plane.x + shoot_plane.z * shoot_plane.z) -
          (target_plane.x * target_plane.x + target_plane.z * target_plane.z);
        float plane_root = 0.0f;
        if (solve_quadratic_front_root(1.0f, planar_b, planar_c, plane_root)) {
          const Vec3 shifted_plane = shoot_plane + forward_plane * plane_root;
          pitch_command = -std::atan2(shifted_plane.z, shifted_plane.x) * radpi;
        }
      }
    }
  } else {
    pitch_command = muzzle_pitch;
    yaw_command = muzzle_yaw;
  }

  result.calculated = calc_state::good;
  result.pitch = pitch_command;
  result.yaw = yaw_command;
  result.time = muzzle_time;
  result.launch = launch;
  result.launch_angles = launch_angles;
  return result;
}

inline bool solution_within_timing(const point_solution& solution, int sim_tick, int tolerance) {
  if (solution.calculated != calc_state::good) {
    return false;
  }
  const int time_to = time_to_ticks(solution.time);
  if (tolerance == INT_MAX) {
    return true;
  }
  if (tolerance < 0) {
    return time_to <= sim_tick;
  }
  return std::abs(time_to - sim_tick) <= tolerance;
}

inline Vec3 pull_point_toward_eye(const Vec3& point, const Vec3& eye, const Vec3& mins,
                                  const Vec3& maxs) {
  float enter_fraction = 0.0f;
  if (aimbot_segment_aabb_enter_fraction(eye, point, mins, maxs, &enter_fraction)) {
    return point + (eye - point) * std::clamp(enter_fraction, 0.0f, 1.0f);
  }
  return point;
}

struct direct_slot {
  bool active = false;
  bool head = false;
  Vec3 offset{};
};

struct direct_history_entry {
  int tick = 0;
  Vec3 origin{};
  Vec3 point{};
  bool head = false;
  point_solution solution{};
};

struct splash_history_entry {
  int tick = 0;
  Vec3 origin{};
  float time_to = 0.0f;
};

struct seed_outcome {
  bool found = false;
  float solution_time = 0.0f;
  aimbot_candidate candidate{};
};

inline seed_outcome evaluate_seed(Player* local, Weapon* weapon, const projectile_info& info,
                                  const target_seed& seed,
                                  const Vec3& original_view_angles) {
  seed_outcome outcome{};
  if (local == nullptr || weapon == nullptr || seed.entity == nullptr) {
    return outcome;
  }

  const Vec3 eye = local->get_shoot_pos();
  const int weapon_id_value = info.weapon_id_value;
  const float radius = splash_radius_for(local, info);
  const float speed = std::max(info.speed, 1.0f);
  const float bounds_time = length(seed.bounds_maxs - seed.bounds_mins) / speed;
  const float radius_time = bounds_time + radius / speed;
  const int arm_ticks = cfg.sticky_arm_time && info.arm_time > 0.0f
                          ? time_to_ticks(info.arm_time)
                          : 0;
  const bool lob_enabled = cfg.lob_angles && info.gravity_mod > 0.0f;
  const bool underpredict = cfg.lob_underpredict && radius > 0.0f;
  const float drag_base = effective_drag(info, speed, lob_enabled);
  const int splash_policy = std::clamp(config.aimbot.projectile_splash_policy, 0, 2);
  const bool splash_allowed = radius > 0.0f && splash_policy != 0 && info.direct_hit;
  const bool splash_only = !info.direct_hit;
  const bool huntsman = weapon_id_value == TF_WEAPON_COMPOUND_BOW;
  const float latency = latency_seconds();

  std::array<direct_slot, 4> slots{};
  int slot_count = 0;
  const Vec3 maxs = seed.bounds_maxs;
  const Vec3 mins = seed.bounds_mins;
  if (seed.player != nullptr && config.aimbot.projectile_aim_pos == 0) {
    const bool grounded = seed.player->is_on_ground();
    if (huntsman) {
      Vec3 head_center{};
      if (aimbot_get_hitbox_center(seed.player, aim_hitbox_head, &head_center)) {
        slots[slot_count++] = {true, true, head_center - seed.origin};
      } else {
        slots[slot_count++] = {true, true, Vec3{0.0f, 0.0f, maxs.z * 0.93f}};
      }
      slots[slot_count++] = {true, false, Vec3{0.0f, 0.0f, (maxs.z - mins.z) * 0.5f}};
    } else if (grounded && (is_rocket_weapon(weapon_id_value) || is_grenade_launcher(weapon_id_value))) {
      slots[slot_count++] = {true, false, Vec3{0.0f, 0.0f, maxs.z * 0.10f}};
      slots[slot_count++] = {true, false, Vec3{0.0f, 0.0f, (maxs.z - mins.z) * 0.5f}};
    } else {
      slots[slot_count++] = {true, false, Vec3{0.0f, 0.0f, (maxs.z - mins.z) * 0.5f}};
    }
  } else {
    slots[slot_count++] = {true, false, seed.aim_offset};
  }

  movement_path path{};
  movesim_guard path_guard(path.simulation);
  const bool simulate_movement =
    seed.player != nullptr &&
    config.aimbot.projectile_prediction_mode == Aim::ProjectilePredictionMode::MOVE_SIM;
  if (simulate_movement) {
    movesim::init_options options{};
    options.strafe_prediction = cfg.strafe_prediction;
    options.hitchance_gate = cfg.hitchance_gate;
    options.hitchance_minimum = cfg.hitchance_minimum;
    options.predict_networked = false;
    options.drain_charge = false;
    options.inject_jump = false;
    options.local_command = nullptr;
    if (!movesim::initialize(seed.player, path.simulation, options)) {
      path.failed = true;
    } else {
      path.simulated = true;
    }
  }

  const float life_cap = info.life_time > 0.0f
                           ? std::min(info.life_time, config.aimbot.projectile_max_sim_time)
                           : config.aimbot.projectile_max_sim_time;
  const int max_tick = std::clamp(time_to_ticks(life_cap), 1, 400);
  const int start_tick = 1 - time_to_ticks(latency);

  std::vector<direct_history_entry> direct_history;
  direct_history.reserve(16);
  std::vector<splash_history_entry> splash_history;
  splash_history.reserve(32);
  bool splash_exhausted = false;

  for (int tick = start_tick; tick <= max_tick; ++tick) {
    if (path.simulated && !path.failed) {
      if (!movesim::run_tick(path.simulation)) {
        path.failed = true;
      }
    }
    const float elapsed = ticks_to_time(std::max(tick, 0));
    const Vec3 origin = path.simulated && !path.failed
                          ? path.simulation.predicted_origin
                          : (path.simulated
                               ? path_origin(path, seed, elapsed)
                               : (seed.player != nullptr
                                    ? seed.origin + seed.velocity * elapsed
                                    : seed.origin));
    if (tick < 1) {
      continue;
    }

    const bool moving_target = length_squared(seed.velocity) > 100.0f;
    const bool armed = arm_ticks <= 0 || tick >= arm_ticks || !moving_target;

    bool directs_alive = false;
    if (!splash_only) {
      for (int index = 0; index < slot_count; ++index) {
        direct_slot& slot = slots[index];
        if (!slot.active) {
          continue;
        }
        if (!armed) {
          directs_alive = true;
          continue;
        }
        Vec3 point = origin + slot.offset;
        if (slot.head && cfg.huntsman_pull_point && seed.player != nullptr) {
          point = pull_point_toward_eye(point, eye, origin + mins, origin + maxs);
        }
        point_solution solution =
          solve_point(local, info, eye, point, lob_enabled, true, drag_base);
        if (solution.calculated == calc_state::bad) {
          slot.active = false;
          continue;
        }
        const int tolerance = underpredict && lob_enabled ? INT_MAX : -1;
        if (solution_within_timing(solution, tick, tolerance)) {
          direct_history.push_back({tick, origin, point, slot.head, solution});
          slot.active = false;
        } else {
          directs_alive = true;
        }
      }
    }

    bool splashes_alive = false;
    if ((splash_allowed || splash_only) && !splash_exhausted) {
      const Vec3 schedule_point = origin + seed.aim_offset;
      const point_solution schedule =
        solve_point(local, info, eye, schedule_point, lob_enabled, true, drag_base);
      if (schedule.calculated == calc_state::bad && !directs_alive) {
        splash_exhausted = true;
      } else {
        const float time_to = schedule.time - ticks_to_time(tick);
        if (time_to > radius_time) {
          splashes_alive = true;
        } else if (time_to < -radius_time) {
          splash_exhausted = true;
        } else {
          splash_history.push_back({tick, origin, std::fabs(time_to)});
          splashes_alive = true;
        }
      }
    }

    if (!directs_alive && !splashes_alive) {
      break;
    }
  }

  const auto finish_candidate = [&](const Vec3& aim_position, const Vec3& predicted_origin,
                                    const Vec3& angles, float time, bool head) {
    const Vec3 current_angles =
      aimbot_calculate_angles_to_position(eye, predicted_origin + seed.aim_offset);
    const float current_fov = aimbot_calculate_fov(current_angles, original_view_angles);
    const float predicted_fov = aimbot_calculate_fov(angles, original_view_angles);
    const float fov = config.aimbot.projectile_mode == 0 ? current_fov : predicted_fov;
    if (config.aimbot.projectile_mode != 2 && projectile_fov_exceeds_limit(fov)) {
      return false;
    }

    aimbot_candidate candidate{};
    candidate.entity = seed.entity;
    candidate.player = seed.player;
    candidate.aim_position = aim_position;
    candidate.predicted_origin = predicted_origin;
    candidate.predicted_origin_valid = seed.player != nullptr;
    candidate.aim_angles = aimbot_clamp_angles(angles);
    candidate.command_angles = candidate.aim_angles;
    candidate.fov = fov;
    candidate.distance = seed.distance;
    candidate.health = seed.player != nullptr
                         ? seed.player->get_health()
                         : aimbot_entity_health(seed.entity);
    candidate.simulation_time = seed.entity->get_simulation_time();
    candidate.visible = true;
    candidate.preferred = seed.preferred;
    candidate.hitbox = head ? aim_hitbox_head : -1;
    candidate.debug_reason = aimbot_debug_reason::attack_ready;
    outcome.found = true;
    outcome.solution_time = time;
    outcome.candidate = candidate;
    return true;
  };

  const auto try_direct = [&]() {
    std::sort(direct_history.begin(), direct_history.end(),
              [](const direct_history_entry& left, const direct_history_entry& right) {
                return left.tick < right.tick;
              });
    for (const direct_history_entry& entry : direct_history) {
      Vec3 launch{};
      Vec3 launch_angles{};
      const bool ignore_friendlies = !is_rocket_weapon(weapon_id_value);
      if (!launch_position(local, info, {entry.solution.pitch, entry.solution.yaw, 0.0f},
                           ignore_friendlies, launch, &launch_angles)) {
        continue;
      }
      const Vec3 velocity = launch_velocity(info, launch_angles, local);
      shot_test test{};
      test.launch = launch;
      test.velocity = velocity;
      test.drag = effective_drag(info, length(velocity), lob_enabled);
      test.target = seed.entity;
      test.predicted_origin = entry.origin;
      test.state = make_target_state(seed, entry.origin);
      test.aim_point = entry.point;
      test.sim_ticks = std::max(
        time_to_ticks(entry.solution.time + info.release_delay) + 1, 1);
      test.kind = 0;
      test.radius_sqr = FLT_MAX;
      test.normal_offset = 0.0f;
      test.trace_interval = std::clamp(cfg.direct_trace_interval, 1, 16);
      test.interval_retest = cfg.interval_retest;
      if (!validate_shot(local, info, test)) {
        continue;
      }
      return finish_candidate(entry.point, entry.origin,
                              {entry.solution.pitch, entry.solution.yaw, 0.0f},
                              entry.solution.time, entry.head);
    }
    return false;
  };

  const auto try_splash = [&]() {
    std::sort(splash_history.begin(), splash_history.end(),
              [](const splash_history_entry& left, const splash_history_entry& right) {
                return left.tick < right.tick;
              });
    const int restrict_arc = std::clamp(cfg.splash_restrict_arc, 1, 128);
    const int restrict_direct = std::clamp(cfg.splash_restrict_direct, 1, 128);
    const int restrict_first = std::clamp(cfg.splash_restrict_first, 1, 256);
    const int restrict_base = info.gravity_mod > 0.0f ? restrict_arc : restrict_direct;
    const int trace_interval =
      std::clamp(info.gravity_mod > 0.0f ? cfg.geometry_trace_interval
                                         : cfg.splash_trace_interval, 1, 16);

    bool first_bucket = true;
    for (const splash_history_entry& entry : splash_history) {
      const splash_target_state state = make_target_state(seed, entry.origin);
      const int capacity = 256;
      std::array<splash_candidate, capacity> candidates{};
      const int count = splashbot_instance.collect_candidates(
        state, radius, info.hull, cfg.air_splash && info.air_splash,
        cfg.air_point_count, eye, candidates.data(), capacity);
      if (count <= 0) {
        first_bucket = false;
        continue;
      }

      int limit = first_bucket ? std::max(restrict_base, restrict_first) : restrict_base;
      limit = std::min(limit, count);

      float best_score = -FLT_MAX;
      Vec3 best_point{};
      point_solution best_solution{};

      for (int index = 0; index < limit; ++index) {
        const splash_candidate& candidate = candidates[index];
        const point_solution solution =
          solve_point(local, info, eye, candidate.point, lob_enabled, true, drag_base);
        if (solution.calculated != calc_state::good) {
          continue;
        }
        if (candidate.kind == splash_point_kind::air &&
            arm_ticks > 0 && solution.time < info.arm_time) {
          continue;
        }

        Vec3 launch{};
        Vec3 launch_angles{};
        const bool ignore_friendlies = !is_rocket_weapon(weapon_id_value);
        if (!launch_position(local, info, {solution.pitch, solution.yaw, 0.0f},
                             ignore_friendlies, launch, &launch_angles)) {
          continue;
        }
        const Vec3 velocity = launch_velocity(info, launch_angles, local);
        const float effective_radius = candidate.kind == splash_point_kind::air &&
                                           info.weapon_id_value == TF_WEAPON_PIPEBOMBLAUNCHER
                                         ? radius * sticky_air_radius_scale(entry.tick)
                                         : radius;
        shot_test test{};
        test.launch = launch;
        test.velocity = velocity;
        test.drag = effective_drag(info, length(velocity), lob_enabled);
        test.target = seed.entity;
        test.predicted_origin = entry.origin;
        test.state = state;
        test.aim_point = candidate.point;
        test.sim_ticks = std::max(time_to_ticks(solution.time + info.release_delay) + 1, 1);
        test.kind = candidate.kind == splash_point_kind::air ? 2 : 1;
        test.radius_sqr = effective_radius * effective_radius;
        test.normal_offset = info.normal_offset;
        test.trace_interval = trace_interval;
        test.interval_retest = cfg.interval_retest;
        if (!validate_shot(local, info, test)) {
          continue;
        }

        const float score =
          candidate.falloff * 1000.0f - solution.time * 0.01f;
        if (score > best_score) {
          best_score = score;
          best_point = candidate.point;
          best_solution = solution;
        }
      }

      first_bucket = false;
      if (best_score > -FLT_MAX) {
        return finish_candidate(best_point, entry.origin,
                                {best_solution.pitch, best_solution.yaw, 0.0f},
                                best_solution.time, false);
      }
    }
    return false;
  };

  if (splash_only) {
    if (!try_splash()) {
      return outcome;
    }
  } else if (splash_policy == 2 && splash_allowed) {
    if (!try_splash() && !try_direct()) {
      return outcome;
    }
  } else {
    if (!try_direct() && !(splash_allowed && try_splash())) {
      return outcome;
    }
  }

  return outcome;
}

inline bool projectile_fov_exceeds_limit(float fov) {
  const float limit = config.aimbot.projectile_fov;
  if (!std::isfinite(fov)) {
    return true;
  }
  if (!std::isfinite(limit) || limit <= 0.0f) {
    return false;
  }
  if (limit >= 180.0f) {
    return false;
  }
  return fov > limit;
}

inline bool seed_better(const target_seed& left, const target_seed& right) {
  if (right.entity == nullptr) return true;
  if (left.preferred != right.preferred) return left.preferred;
  switch (config.aimbot.projectile_mode) {
  case 2:
    return left.distance < right.distance;
  case 0:
  case 1:
  default:
    return left.current_fov < right.current_fov;
  }
}

inline bool candidate_better(const aimbot_candidate& left, const aimbot_candidate& right) {
  if (right.entity == nullptr) return true;
  if (left.preferred != right.preferred) return left.preferred;
  if (config.aimbot.projectile_mode == 2) {
    return left.distance < right.distance;
  }
  return left.fov < right.fov;
}

}

struct charge_state {
  Weapon* weapon = nullptr;
  int weapon_def_id = TF_WEAPON_NONE;
  bool last_attack = false;
  bool last_aiming = false;
  float last_solution_time = 0.0f;
};

inline charge_state projectile_charge_state{};

inline float current_time(Player* local);

inline bool projectile_modifier_enabled(uint32_t modifier) {
  return (config.aimbot.projectile_modifiers & modifier) != 0;
}

inline bool is_bow(Weapon* weapon) {
  return weapon != nullptr && detail::weapon_id(weapon) == TF_WEAPON_COMPOUND_BOW;
}

inline float charge_elapsed(Weapon* weapon, Player* local) {
  if (weapon == nullptr) {
    return 0.0f;
  }

  const float begin = weapon->get_charge_begin_time();
  if (begin <= 0.0f) {
    return 0.0f;
  }

  const float now = current_time(local);
  return std::isfinite(now) ? std::max(now - begin, 0.0f) : 0.0f;
}

inline bool same_charge_weapon(Weapon* weapon) {
  return weapon != nullptr && projectile_charge_state.weapon == weapon &&
    projectile_charge_state.weapon_def_id == weapon->get_def_id();
}

inline void reset_charge_tracking() {
  projectile_charge_state = {};
}

inline bool cancel_charge_if_needed(user_cmd* cmd, Player* local, Weapon* weapon) {
  if (cmd == nullptr || local == nullptr || !is_bow(weapon) ||
      !same_charge_weapon(weapon) || !projectile_charge_state.last_aiming ||
      !projectile_modifier_enabled(Aim::projectile_mod_cancel_charge)) {
    return false;
  }

  const bool attack_held = (cmd->buttons & IN_ATTACK) != 0;
  const float charge = charge_elapsed(weapon, local);
  const bool released = !attack_held;
  const bool overcharged = charge >= 0.95f;

  if (!projectile_charge_state.last_attack || (!released && !overcharged)) {
    return false;
  }

  cmd->buttons |= IN_ATTACK2;
  cmd->buttons &= ~IN_ATTACK;
  projectile_charge_state.last_attack = false;
  projectile_charge_state.last_aiming = false;
  return true;
}

inline float current_time(Player* local) {
  return global_vars != nullptr
    ? global_vars->curtime
    : (local != nullptr ? local->get_tickbase() * detail::interval() : 0.0f);
}

struct apply_result {
  bool attack_ready = false;
  bool requested_shot = false;
  bool psilent = false;
};

inline aimbot_candidate find_candidate(Player* local, Weapon* weapon,
                                       const Vec3& original_view_angles) {
  aimbot_candidate best{};
  if (local == nullptr || weapon == nullptr || !config.aimbot.projectile_active) {
    return best;
  }

  detail::projectile_info info{};
  if (!detail::get_info(local, weapon, info)) {
    return best;
  }

  projectile_charge_state.last_solution_time = 0.0f;

  std::vector<detail::target_seed> seeds{};
  seeds.reserve(entity_cache_players().size() + 3);
  const Vec3 shoot_pos = local->get_shoot_pos();

  for (const entity_cache_player_entry& entry : entity_cache_players()) {
    ++aim_state::scan.candidates_total;
    const auto skip_reason = aimbot_player_skip_reason_for(local, entry, weapon);
    if (skip_reason != aimbot_player_skip_reason::none) {
      aim_state::record_player_skip(skip_reason, entry.player);
      continue;
    }

    detail::target_seed seed{};
    seed.entity = entry.entity != nullptr ? entry.entity : entry.player->to_entity();
    seed.player = entry.player;
    seed.origin = entry.player->get_origin();
    seed.view_offset = entry.player->get_view_offset();
    seed.bounds_mins = entry.player->get_player_mins();
    seed.bounds_maxs = entry.player->get_player_maxs();
    const int id = info.weapon_id_value;
    Vec3 aim_offset{};
    if (id == TF_WEAPON_COMPOUND_BOW) {
      Vec3 head_center{};
      if (aimbot_get_hitbox_center(entry.player, aim_hitbox_head, &head_center)) {
        aim_offset = head_center - seed.origin;
      } else {
        aim_offset = {0.0f, 0.0f, seed.bounds_maxs.z * 0.93f};
      }
    } else if ((detail::is_rocket_weapon(id) || detail::is_grenade_launcher(id)) &&
               entry.player->is_on_ground()) {
      aim_offset = {0.0f, 0.0f, seed.bounds_maxs.z * 0.10f};
    } else {
      aim_offset = {0.0f, 0.0f, seed.bounds_maxs.z * 0.50f};
    }
    if (config.aimbot.projectile_aim_pos == 1) {
      aim_offset = {0.0f, 0.0f, seed.bounds_maxs.z * 0.10f};
    } else if (config.aimbot.projectile_aim_pos == 2) {
      aim_offset = {0.0f, 0.0f, seed.bounds_maxs.z * 0.50f};
    } else if (config.aimbot.projectile_aim_pos == 3) {
      aim_offset = {0.0f, 0.0f, seed.bounds_maxs.z * 0.93f};
    }
    seed.aim_offset = aim_offset;
    seed.velocity = entry.player->get_velocity();
    seed.current_fov = aimbot_calculate_fov(
      aimbot_calculate_angles_to_position(shoot_pos, seed.origin + seed.aim_offset),
      original_view_angles);
    seed.distance = distance_3d(shoot_pos, seed.origin);
    seed.preferred = aimbot_player_is_preferred(entry.player);
    if (config.aimbot.projectile_mode == 0 &&
        detail::projectile_fov_exceeds_limit(seed.current_fov)) {
      aim_state::record_reject(aim_state::make_reject_debug(
        seed.entity, aimbot_reject_reason::fov, seed.current_fov,
        config.aimbot.projectile_fov, seed.distance));
      continue;
    }
    seeds.push_back(seed);
  }

  constexpr class_id building_ids[] = {class_id::SENTRY, class_id::DISPENSER,
                                       class_id::TELEPORTER};
  if (aimbot_aim_at_enabled(Aim::aim_at_buildings)) {
    for (const class_id id : building_ids) {
      for (Entity* entity : entity_cache[id]) {
        if (aimbot_should_skip_non_player_target(local, entity)) {
          continue;
        }
        detail::target_seed seed{};
        seed.entity = entity;
        seed.origin = entity->get_collision_origin();
        const Vec3 mins = entity->get_collideable_mins();
        const Vec3 maxs = entity->get_collideable_maxs();
        seed.bounds_mins = mins;
        seed.bounds_maxs = maxs;
        seed.aim_offset = (mins + maxs) * 0.5f;
        seed.view_offset = (mins + maxs) * 0.5f;
        seed.current_fov = aimbot_calculate_fov(
          aimbot_calculate_angles_to_position(shoot_pos, seed.origin + seed.aim_offset),
          original_view_angles);
        seed.distance = distance_3d(shoot_pos, seed.origin);
        if (config.aimbot.projectile_mode == 0 &&
            detail::projectile_fov_exceeds_limit(seed.current_fov)) {
          continue;
        }
        seeds.push_back(seed);
      }
    }
  }

  const auto append_static_target = [&](Entity* entity) {
    if (entity == nullptr || aimbot_should_skip_non_player_target(local, entity)) {
      return;
    }

    detail::target_seed seed{};
    seed.entity = entity;
    seed.origin = entity->get_collision_origin();
    const Vec3 mins = entity->get_collideable_mins();
    const Vec3 maxs = entity->get_collideable_maxs();
    seed.bounds_mins = mins;
    seed.bounds_maxs = maxs;
    seed.aim_offset = (mins + maxs) * 0.5f;
    seed.view_offset = (mins + maxs) * 0.5f;
    seed.current_fov = aimbot_calculate_fov(
      aimbot_calculate_angles_to_position(shoot_pos, seed.origin + seed.aim_offset),
      original_view_angles);
    seed.distance = distance_3d(shoot_pos, seed.origin);
    if (config.aimbot.projectile_mode == 0 &&
        detail::projectile_fov_exceeds_limit(seed.current_fov)) {
      return;
    }
    seeds.push_back(seed);
  };

  if (aimbot_aim_at_enabled(Aim::aim_at_npcs)) {
    for (Entity* entity : entity_cache_npcs()) {
      append_static_target(entity);
    }
  }
  if (aimbot_aim_at_enabled(Aim::aim_at_stickies)) {
    for (Entity* entity : entity_cache_entities(class_id::PILL_OR_STICKY)) {
      append_static_target(entity);
    }
  }
  if (aimbot_aim_at_enabled(Aim::aim_at_bombs)) {
    for (Entity* entity : entity_cache_entities(class_id::PUMPKIN)) {
      append_static_target(entity);
    }
  }

  std::stable_sort(seeds.begin(), seeds.end(), detail::seed_better);
  const int max_attempts = std::clamp(config.aimbot.projectile_max_sim_targets, 1, 6);
  int attempts = 0;
  for (const detail::target_seed& seed : seeds) {
    if (attempts >= max_attempts && seed.distance > 400.0f) {
      continue;
    }
    ++attempts;

    const detail::seed_outcome outcome =
      detail::evaluate_seed(local, weapon, info, seed, original_view_angles);
    if (!outcome.found) {
      aim_state::record_reject(aim_state::make_reject_debug(
        seed.entity, aimbot_reject_reason::no_candidate,
        std::isfinite(seed.current_fov) ? seed.current_fov : FLT_MAX,
        config.aimbot.projectile_fov, seed.distance));
      continue;
    }

    ++aim_state::scan.candidates_visible;
    if (detail::candidate_better(outcome.candidate, best)) {
      best = outcome.candidate;
      projectile_charge_state.last_solution_time = outcome.solution_time;
    }
  }

  return best;
}

inline apply_result apply(user_cmd* cmd, Player* local, Weapon* weapon,
                          const Vec3& original_view_angles, const aimbot_candidate& target,
                          bool manual_attack = false) {
  apply_result result{};
  if (cmd == nullptr || local == nullptr || weapon == nullptr || target.entity == nullptr) {
    return result;
  }

  detail::projectile_info info{};
  if (!detail::get_info(local, weapon, info)) {
    return result;
  }

  const int attack_button = info.secondary_attack ? IN_ATTACK2 : IN_ATTACK;
  const int id = info.weapon_id_value;
  const bool charge_weapon =
    id == TF_WEAPON_COMPOUND_BOW || id == TF_WEAPON_PIPEBOMBLAUNCHER;
  const float charge_time = charge_weapon ? charge_elapsed(weapon, local) : 0.0f;
  const bool charged = charge_weapon && charge_time > 0.0f;
  const bool cannon_detonating =
    id == TF_WEAPON_CANNON && weapon->get_detonate_time() > 0.0f;
  const bool beggars = weapon->get_def_id() == Soldier_m_TheBeggarsBazooka;
  const bool has_ammo = weapon->get_clip1() != 0;
  const bool raw_attack = (cmd->buttons & attack_button) != 0;
  bool manual_bow_release = is_bow(weapon) && same_charge_weapon(weapon) &&
    projectile_charge_state.last_aiming && projectile_charge_state.last_attack && !raw_attack;
  const bool can_attack = has_ammo;

  Vec3 target_angles = target.command_angles;
  const Aim::AimMode aim_mode = static_cast<Aim::AimMode>(
    std::clamp(static_cast<int>(config.aimbot.aim_mode), 0, 3));
  if (config.aimbot.projectile_smooth_flamethrowers_active && weapon->is_flamethrower()) {
    target_angles = aimbot_lerp_angles(original_view_angles, target_angles,
      std::clamp(config.aimbot.projectile_smooth_flamethrowers / 100.0f, 0.0f, 1.0f));
  } else if (aim_mode == Aim::AimMode::SMOOTH || aim_mode == Aim::AimMode::ASSISTIVE) {
    const aimbot::aimbot_state& state = aimbot::current_state();
    target_angles = aimbot_apply_mode_angles(original_view_angles, target_angles,
                                             state.last_input_angles,
                                             state.last_input_angles_valid, target);
  }

  bool release_requested =
    !manual_attack && config.aimbot.auto_shoot && has_ammo && (charged || cannon_detonating);
  if (release_requested && id == TF_WEAPON_CANNON && cfg.cannon_hitcharge &&
      weapon->get_detonate_time() > 0.0f) {
    const float remaining = weapon->get_detonate_time() - current_time(local);
    if (remaining > detail::grenade_check_interval &&
        projectile_charge_state.last_solution_time >
          remaining - detail::grenade_check_interval) {
      release_requested = false;
    }
  }

  const bool manual_release_ready = manual_bow_release;
  if (release_requested || manual_release_ready) {
    cmd->buttons &= ~attack_button;
    if (release_requested) {
      result.requested_shot = true;
    }
  } else if (!manual_attack && config.aimbot.auto_shoot && has_ammo && !charged &&
             !cannon_detonating && !manual_bow_release) {
    cmd->buttons |= attack_button;
    result.requested_shot = true;
  }

  if (beggars && cfg.beggars_clip_guard && (cmd->buttons & IN_ATTACK) != 0) {
    bool suppress = false;
    if (weapon->get_clip1() > 0) {
      suppress = true;
    } else if (cfg.beggars_wall_guard && engine_trace != nullptr) {
      Vec3 forward{};
      angle_vectors(cmd->view_angles, &forward, nullptr, nullptr);
      const float probe_distance =
        std::max(detail::splash_radius_for(local, info), 96.0f);
      Vec3 trace_start = local->get_shoot_pos();
      Vec3 trace_end = trace_start + forward * probe_distance;
      ray_t ray = engine_trace->init_ray(&trace_start, &trace_end);
      trace_filter filter{};
      engine_trace->init_world_and_props_trace_filter(&filter);
      trace_t trace{};
      engine_trace->trace_ray(&ray, detail::projectile_collision_mask, &filter, &trace);
      suppress = trace.start_solid || trace.all_solid || trace.fraction < 1.0f;
    }
    if (suppress) {
      cmd->buttons &= ~IN_ATTACK;
      result.requested_shot = false;
    }
  }

  if (is_bow(weapon) && projectile_modifier_enabled(Aim::projectile_mod_charge_weapon) &&
      !charged && same_charge_weapon(weapon) && projectile_charge_state.last_aiming &&
      projectile_charge_state.last_attack && !manual_release_ready) {
    cmd->buttons |= IN_ATTACK;
  }

  if (!has_ammo) {
    cmd->buttons &= ~attack_button;
  }

  const bool firing = (cmd->buttons & attack_button) != 0;
  const bool shot_command = firing || release_requested || manual_bow_release;
  result.attack_ready = can_attack && shot_command;
  result.psilent = aim_mode == Aim::AimMode::PSILENT && shot_command && !manual_attack;

  if (config.aimbot.spread_compensation && shot_command) {
    target_angles = detail::compensate_projectile_spread(local, weapon, cmd, info,
                                                         target_angles);
  }
  cmd->view_angles = aimbot_clamp_angles(target_angles);

  if ((aim_mode != Aim::AimMode::PSILENT || manual_attack) && prediction != nullptr) {
    prediction->set_local_view_angles(cmd->view_angles);
    prediction->set_view_angles(cmd->view_angles);
  }
  if ((aim_mode != Aim::AimMode::PSILENT || manual_attack) && engine != nullptr) {
    engine->set_view_angles(cmd->view_angles);
  }

  projectile_charge_state.weapon = weapon;
  projectile_charge_state.weapon_def_id = weapon->get_def_id();
  projectile_charge_state.last_attack = (cmd->buttons & IN_ATTACK) != 0;
  projectile_charge_state.last_aiming = true;

  return result;
}

}
#endif
