#ifndef MELEE_AIM_HPP
#define MELEE_AIM_HPP
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>
#include "core/types.hpp"
#include "features/combat/simulation/movesim.hpp"
#include "features/combat/tickbase/tickbase.hpp"
#include "games/tf2/sdk/entities/building.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "aim_utils.hpp"
#include "resolver.hpp"

namespace melee_aim {

inline struct settings {
  bool hold_fire = true;
  bool drain_charge = true;
  bool strafe_prediction = true;
  int backstab_ping_mode = 0;
  bool whip_team = false;
  bool sapper_priority = true;
  bool wrench_friendly_buildings = false;
} cfg{};

}

namespace melee_aim_detail {

inline float movement_interval() {
  if (global_vars != nullptr && std::isfinite(global_vars->interval_per_tick) &&
      global_vars->interval_per_tick > 0.0001f) {
    return global_vars->interval_per_tick;
  }
  return static_cast<float>(TICK_INTERVAL);
}

inline bool is_knife(Weapon* weapon) {
  if (weapon == nullptr) {
    return false;
  }
  if (weapon->get_weapon_id() == TF_WEAPON_KNIFE) {
    return true;
  }
  switch (weapon->get_def_id()) {
  case Spy_t_Knife:
  case Spy_t_KnifeR:
  case Spy_t_YourEternalReward:
  case Spy_t_ConniversKunai:
  case Spy_t_TheBigEarner:
  case Spy_t_TheWangaPrick:
  case Spy_t_TheSharpDresser:
  case Spy_t_TheSpycicle:
  case Spy_t_FestiveKnife:
  case Spy_t_TheBlackRose:
  case Spy_t_SilverBotkillerKnifeMkI:
  case Spy_t_GoldBotkillerKnifeMkI:
  case Spy_t_RustBotkillerKnifeMkI:
  case Spy_t_BloodBotkillerKnifeMkI:
  case Spy_t_CarbonadoBotkillerKnifeMkI:
  case Spy_t_DiamondBotkillerKnifeMkI:
  case Spy_t_SilverBotkillerKnifeMkII:
  case Spy_t_GoldBotkillerKnifeMkII:
    return true;
  default:
    return false;
  }
}

inline Vec3 forward_xy(const Vec3& angles) {
  Vec3 forward{};
  angle_vectors(angles, &forward, nullptr, nullptr);
  forward.z = 0.0f;
  const float length = std::sqrt((forward.x * forward.x) + (forward.y * forward.y));
  return length > 0.0001f ? forward * (1.0f / length) : Vec3{1.0f, 0.0f, 0.0f};
}

inline float normalize_angle(float yaw) {
  return std::remainder(yaw, 360.0f);
}

inline float vector_yaw(const Vec3& direction) {
  return std::atan2(direction.y, direction.x) * 57.29577951308232f;
}

inline float attribute_value(float fallback, const char* name, Entity* entity) {
  return attribute_manager != nullptr
    ? attribute_manager->attrib_hook_value(fallback, name, entity)
    : fallback;
}

inline float game_convar_float(const char* name, float fallback) {
  if (name == nullptr || convar_system == nullptr) {
    return fallback;
  }
  Convar* var = convar_system->find_var(name);
  if (var == nullptr) {
    return fallback;
  }
  const float value = var->get_float();
  return std::isfinite(value) ? value : fallback;
}

inline float current_time(Player* local) {
  return global_vars != nullptr && std::isfinite(global_vars->curtime)
    ? global_vars->curtime
    : (local != nullptr ? local->get_tickbase() * static_cast<float>(TICK_INTERVAL) : 0.0f);
}

inline bool smack_pending(Player* local, Weapon* weapon) {
  if (local == nullptr || weapon == nullptr || is_knife(weapon)) {
    return false;
  }
  const float smack_time = weapon->get_smack_time();
  if (!(smack_time > 0.0f)) {
    return false;
  }
  const float now = current_time(local);
  return smack_time > now && smack_time - now <= 0.5f;
}

inline int real_smack_ticks(Weapon* weapon) {
  if (weapon == nullptr || is_knife(weapon)) {
    return 0;
  }
  return std::clamp(
    static_cast<int>(std::ceil(weapon->get_smack_delay() / movement_interval())), 0, 32);
}

inline int simulated_ticks_setting() {
  return config.aimbot.melee_swing_prediction
    ? std::clamp(config.aimbot.melee_swing_ticks, 0, 14)
    : 0;
}

inline int pending_shift_ticks() {
  if (!config.misc.exploits.tickbase || client_state == nullptr) {
    return 0;
  }
  const tickbase::indicator_state state = tickbase::get_indicator_state();
  return std::clamp(state.available_shift_ticks, 0, 14);
}

inline bool shift_window_allows_swing(Player* local, Weapon* weapon) {
  const int shift_ticks = pending_shift_ticks();
  if (shift_ticks <= 0 || local == nullptr) {
    return true;
  }
  return shift_ticks <= real_smack_ticks(weapon) ||
    (local->get_flags() & FL_ONGROUND) != 0;
}

struct swing_geometry {
  float range = 0.0f;
  float hull = 0.0f;
};

inline swing_geometry swing_geometry_for(Player* local, Weapon* weapon, Entity* target,
                                         bool friendly_target) {
  swing_geometry geometry{};
  if (weapon == nullptr) {
    return geometry;
  }
  float range = game_convar_float("tf_meleeattackrange", 48.0f);
  float hull = 18.0f;
  range = attribute_value(range, "melee_range_multiplier", weapon->to_entity());
  hull = attribute_value(hull, "melee_bounds_multiplier", weapon->to_entity());
  if (local != nullptr) {
    const float model_scale = local->get_model_scale();
    if (model_scale > 1.0f) {
      range *= model_scale;
      hull *= model_scale;
    }
  }
  if (friendly_target && weapon->get_weapon_id() == TF_WEAPON_WRENCH) {
    range = 70.0f;
    hull = 18.0f;
  }
  if (!std::isfinite(range) || range <= 0.0f ||
      !std::isfinite(hull) || hull <= 0.0f) {
    return geometry;
  }
  geometry.range = range;
  geometry.hull = hull;
  return geometry;
}

inline user_cmd straight_input_command(Player* local) {
  user_cmd command{};
  command.command_number = global_vars != nullptr ? global_vars->tickcount : 0;
  command.tick_count = command.command_number;
  if (local == nullptr) {
    return command;
  }
  Vec3 direction = local->get_velocity();
  direction.z = 0.0f;
  const float speed = std::sqrt((direction.x * direction.x) + (direction.y * direction.y));
  if (speed > 1.0f) {
    command.view_angles = aimbot_direction_to_angles(direction);
  } else {
    command.view_angles = local->get_eye_angles();
  }
  command.view_angles.x = 0.0f;
  command.view_angles.z = 0.0f;
  if ((local->get_flags() & FL_ONGROUND) != 0) {
    const float max_speed =
      local->get_max_speed() > 1.0f ? local->get_max_speed() : 320.0f;
    command.forwardmove = std::min(speed, max_speed);
  }
  return command;
}

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

inline float measured_latency() {
  if (client_state == nullptr || client_state->m_NetChannel == nullptr) {
    return 0.0f;
  }
  const float latency = client_state->m_NetChannel->get_latency(0) +
    client_state->m_NetChannel->get_latency(1);
  return std::isfinite(latency) ? std::clamp(latency, 0.0f, 0.25f) : 0.0f;
}

inline float lag_yaw_delta(int index) {
  const float latency = measured_latency();
  if (latency <= 0.0f) {
    return 0.0f;
  }
  const std::vector<movesim::move_record>& history = movesim::history(index);
  if (history.size() < 2) {
    return 0.0f;
  }
  float latest_yaw = 0.0f;
  bool have_latest = false;
  for (auto entry = history.rbegin(); entry != history.rend(); ++entry) {
    const float speed_squared = (entry->direction.x * entry->direction.x) +
      (entry->direction.y * entry->direction.y);
    if (speed_squared > 1.0f) {
      latest_yaw = vector_yaw(entry->direction);
      have_latest = true;
      break;
    }
  }
  if (!have_latest) {
    return 0.0f;
  }
  const float cutoff = history.back().sim_time - latency;
  for (auto entry = history.rbegin(); entry != history.rend(); ++entry) {
    const float speed_squared = (entry->direction.x * entry->direction.x) +
      (entry->direction.y * entry->direction.y);
    if (speed_squared <= 1.0f) {
      continue;
    }
    if (entry->sim_time <= cutoff) {
      return normalize_angle(latest_yaw - vector_yaw(entry->direction));
    }
  }
  return 0.0f;
}

inline float backstab_target_yaw(Player* target) {
  if (target == nullptr) {
    return 0.0f;
  }
  const resolver::resolver_debug_info info = resolver::debug_for_player(target);
  return info.active && std::isfinite(info.yaw) ? info.yaw : target->get_eye_angles().y;
}

inline bool razorback_blocks_backstab(Player* target) {
  if (target == nullptr || !config.aimbot.melee_ignore_razorback ||
      attribute_manager == nullptr) {
    return false;
  }
  if (attribute_manager->attrib_hook_value(0.0f, "set_blockbackstab_once", target->to_entity()) ==
      0.0f) {
    return false;
  }

  for (Entity* wearable : entity_cache_entities(class_id::WEARABLE_RAZORBACK)) {
    if (wearable != nullptr && wearable->get_owner_entity() == target && wearable->should_draw()) {
      return true;
    }
  }
  return false;
}

inline bool backstab_dots_pass(const Vec3& to_target, const Vec3& owner_forward,
                               const Vec3& target_forward, float distance) {
  const float extra = 0.125f / distance;
  const float position_vs_target =
    (to_target.x * target_forward.x) + (to_target.y * target_forward.y);
  const float position_vs_owner =
    (to_target.x * owner_forward.x) + (to_target.y * owner_forward.y);
  const float view_dot =
    (target_forward.x * owner_forward.x) + (target_forward.y * owner_forward.y);
  return position_vs_target > 0.0031f + extra &&
    position_vs_owner > 0.5f + extra &&
    view_dot > -0.2969f;
}

inline bool backstab_geometry_ok(Player* target, const Vec3& target_origin,
                                 const Vec3& swing_start, const Vec3& aim_angles) {
  Vec3 to_target = target_origin - swing_start;
  to_target.z = 0.0f;
  const float distance = std::sqrt((to_target.x * to_target.x) + (to_target.y * to_target.y));
  if (distance < 0.0884f) {
    return false;
  }
  to_target = to_target * (1.0f / distance);

  const Vec3 owner_forward = forward_xy(aim_angles);
  const float base_yaw = backstab_target_yaw(target);
  const bool ping_compensated = melee_aim::cfg.backstab_ping_mode > 0;

  if (!ping_compensated) {
    const Vec3 target_forward = forward_xy(Vec3{0.0f, base_yaw, 0.0f});
    return backstab_dots_pass(to_target, owner_forward, target_forward, distance);
  }

  if (melee_aim::cfg.backstab_ping_mode >= 2) {
    const Vec3 raw_forward = forward_xy(Vec3{0.0f, base_yaw, 0.0f});
    if (!backstab_dots_pass(to_target, owner_forward, raw_forward, distance)) {
      return false;
    }
  }

  const float delta = lag_yaw_delta(target->get_index());
  const Vec3 shifted_forward = forward_xy(Vec3{0.0f, normalize_angle(base_yaw + delta), 0.0f});
  return backstab_dots_pass(to_target, owner_forward, shifted_forward, distance);
}

inline Vec3 backstab_approach_position(Player* localplayer, Player* target) {
  if (target == nullptr) {
    return {};
  }

  const Vec3 target_origin = target->get_origin();
  const Vec3 target_forward = forward_xy(Vec3{0.0f, backstab_target_yaw(target), 0.0f});
  const Vec3 target_side{-target_forward.y, target_forward.x, 0.0f};
  const float side_sign = localplayer != nullptr &&
      ((localplayer->get_index() + target->get_index()) & 1) != 0
    ? 1.0f
    : -1.0f;
  return target_origin - target_forward * 68.0f + target_side * (side_sign * 28.0f);
}

struct origin_guard {
  Player* target = nullptr;
  Vec3 origin{};
  Vec3 abs_origin{};
  bool active = false;

  origin_guard(Player* value, const Vec3& predicted_origin)
    : target(value),
      origin(value != nullptr ? value->get_origin() : Vec3{}),
      abs_origin(value != nullptr ? value->get_abs_origin() : Vec3{}),
      active(value != nullptr) {
    if (active) {
      target->set_origin(predicted_origin);
      target->set_abs_origin(predicted_origin);
    }
  }

  ~origin_guard() {
    if (active) {
      target->set_origin(origin);
      target->set_abs_origin(abs_origin);
    }
  }

  origin_guard(const origin_guard&) = delete;
  origin_guard& operator=(const origin_guard&) = delete;
};

inline bool run_melee_trace(Player* local, Entity* target, const Vec3& start,
                            const Vec3& end, bool swept_hull, float hull, trace_t* out) {
  if (local == nullptr || target == nullptr || engine_trace == nullptr) {
    return false;
  }
  Vec3 trace_start = start;
  Vec3 trace_end = end;
  Vec3 mins{-hull, -hull, -hull};
  Vec3 maxs{hull, hull, hull};
  ray_t ray = swept_hull
    ? engine_trace->init_ray(&trace_start, &trace_end, &mins, &maxs)
    : engine_trace->init_ray(&trace_start, &trace_end);
  trace_filter filter{};
  engine_trace->init_melee_trace_filter(&filter, local->to_entity(), target);
  if (aimbot_is_friendlyfire_enabled()) {
    filter.skip_team = -1;
  }
  trace_t result{};
  engine_trace->trace_ray(&ray, MASK_SOLID, &filter, &result);
  if (out != nullptr) {
    *out = result;
  }
  return true;
}

inline bool melee_reach_hit(Player* local, Weapon* weapon, Player* target,
                            const Vec3& predicted_origin, const Vec3& swing_start,
                            const Vec3& aim_angles, const swing_geometry& geometry) {
  if (geometry.range <= 0.0f) {
    return false;
  }
  Vec3 forward{};
  angle_vectors(aim_angles, &forward, nullptr, nullptr);
  if (!aimbot_vec3_is_finite(swing_start) || !aimbot_vec3_is_finite(forward)) {
    return false;
  }
  const Vec3 end = swing_start + forward * geometry.range;
  origin_guard guard{target, predicted_origin};
  trace_t line{};
  if (!run_melee_trace(local, target->to_entity(), swing_start, end, false, geometry.hull,
                       &line)) {
    return false;
  }
  if (!line.all_solid && !line.start_solid && line.entity == target->to_entity()) {
    return true;
  }
  if (line.all_solid || line.start_solid || line.fraction < 1.0f) {
    return false;
  }
  trace_t swept{};
  if (!run_melee_trace(local, target->to_entity(), swing_start, end, true, geometry.hull,
                       &swept)) {
    return false;
  }
  return !swept.all_solid && !swept.start_solid && swept.entity == target->to_entity();
}

inline bool melee_reach_hit_relaxed(Player* local, Weapon* weapon, Player* target,
                                    const Vec3& predicted_origin, const Vec3& swing_start,
                                    const Vec3& aim_angles, const swing_geometry& geometry) {
  if (geometry.range <= 0.0f) {
    return false;
  }
  Vec3 forward{};
  angle_vectors(aim_angles, &forward, nullptr, nullptr);
  if (!aimbot_vec3_is_finite(swing_start) || !aimbot_vec3_is_finite(forward)) {
    return false;
  }
  const Vec3 end = swing_start + forward * geometry.range;
  origin_guard guard{target, predicted_origin};
  trace_t swept{};
  if (!run_melee_trace(local, target->to_entity(), swing_start, end, true, geometry.hull,
                       &swept)) {
    return false;
  }
  return !swept.all_solid && !swept.start_solid && swept.entity == target->to_entity();
}

struct target_frame {
  int sim_ticks = 0;
  int smack_ticks = 0;
  int max_ticks = 0;
  bool simulated_local = false;
  std::vector<Vec3> local_origins{};
  std::vector<std::pair<int, Vec3>> records{};
};

inline target_frame build_frame(Player* local, Weapon* weapon, Player* target,
                                user_cmd* command) {
  target_frame frame{};
  if (local == nullptr || weapon == nullptr || target == nullptr) {
    return frame;
  }
  frame.smack_ticks = real_smack_ticks(weapon);
  frame.sim_ticks = simulated_ticks_setting();
  const int shift_ticks = pending_shift_ticks();
  frame.max_ticks = std::max(frame.sim_ticks, shift_ticks);
  if (frame.max_ticks <= 0) {
    return frame;
  }

  user_cmd synthetic{};
  const user_cmd* local_command = command;
  if (local_command == nullptr) {
    synthetic = straight_input_command(local);
    local_command = &synthetic;
  }

  movesim::storage local_storage{};
  movesim::storage target_storage{};
  movesim_guard local_guard(local_storage);
  movesim_guard target_guard(target_storage);

  movesim::init_options local_options{};
  local_options.strafe_prediction = false;
  local_options.predict_networked = false;
  local_options.drain_charge = melee_aim::cfg.drain_charge;
  local_options.local_command = local_command;
  if (!movesim::initialize(local, local_storage, local_options)) {
    return frame;
  }

  movesim::init_options target_options{};
  target_options.strafe_prediction = melee_aim::cfg.strafe_prediction;
  target_options.predict_networked = config.aimbot.melee_swing_predict_lag;
  target_options.drain_charge = false;
  target_options.local_command = nullptr;
  bool target_failed = !movesim::initialize(target, target_storage, target_options);

  frame.local_origins.assign(static_cast<std::size_t>(frame.max_ticks) + 1,
                             local->get_origin());
  const bool grounded = (local->get_flags() & FL_ONGROUND) != 0;
  const bool lag_gated = config.aimbot.melee_swing_predict_lag;
  int loop_end = frame.max_ticks;
  bool swung = false;
  for (int tick = 0; tick < loop_end && tick < 32; ++tick) {
    if (!swung && (shift_ticks == 0 || grounded ||
                   frame.max_ticks - tick <= std::max(frame.smack_ticks, 1))) {
      swung = true;
      if (frame.smack_ticks > 0) {
        loop_end = std::min(tick + frame.smack_ticks, frame.max_ticks);
      }
    }
    if (!movesim::run_tick(local_storage)) {
      break;
    }
    frame.local_origins[static_cast<std::size_t>(tick) + 1] = local_storage.predicted_origin;
    frame.simulated_local = true;

    if (target_failed || tick >= frame.sim_ticks - shift_ticks) {
      continue;
    }
    if (!movesim::run_tick(target_storage)) {
      target_failed = true;
      continue;
    }
    if (lag_gated && !target_storage.predict_networked) {
      continue;
    }
    frame.records.emplace_back(tick + 1, target_storage.predicted_origin);
  }
  return frame;
}

inline Vec3 clamp_point_to_bounds(Player* target, const Vec3& origin, Vec3 point) {
  if (target == nullptr) {
    return point;
  }
  const Vec3 mins = target->get_player_mins(target->is_ducking()) + origin;
  const Vec3 maxs = target->get_player_maxs(target->is_ducking()) + origin;
  point.x = std::clamp(point.x, mins.x, maxs.x);
  point.y = std::clamp(point.y, mins.y, maxs.y);
  point.z = std::clamp(point.z, mins.z, maxs.z);
  return point;
}

inline bool record_origin_at(const target_frame& frame, Player* target, int tick,
                             bool lag_gated, Vec3* out) {
  if (tick <= 0) {
    *out = target->get_origin();
    return aimbot_vec3_is_finite(*out);
  }
  for (const auto& record : frame.records) {
    if (record.first == tick) {
      *out = record.second;
      return aimbot_vec3_is_finite(*out);
    }
  }
  if (lag_gated) {
    return false;
  }
  const Vec3 velocity = target->get_velocity();
  *out = target->get_origin() +
    velocity * (static_cast<float>(tick) * movement_interval());
  return aimbot_vec3_is_finite(*out);
}

inline Vec3 local_eye_at(const target_frame& frame, Player* local, int tick) {
  if (tick <= 0 || static_cast<std::size_t>(tick) >= frame.local_origins.size()) {
    return local->get_shoot_pos();
  }
  return frame.local_origins[static_cast<std::size_t>(tick)] + local->get_view_offset();
}

struct swing_solution {
  bool valid = false;
  bool steer_only = false;
  Vec3 target_origin{};
  Vec3 swing_start{};
  Vec3 aim_position{};
  Vec3 aim_angles{};
  int tick = 0;
};

inline bool knife_backstab_required(Weapon* weapon, Player* local, Player* target) {
  return is_knife(weapon) && config.aimbot.melee_auto_backstab &&
    target->get_team() != local->get_team();
}

inline swing_solution evaluate_tick(Player* local, Weapon* weapon, Player* target,
                                    const aimbot_point& point, const target_frame& frame,
                                    int tick) {
  swing_solution solution{};
  if (local == nullptr || weapon == nullptr || target == nullptr || !point.valid) {
    return solution;
  }

  const bool lag_gated = config.aimbot.melee_swing_predict_lag;
  Vec3 target_origin{};
  if (!record_origin_at(frame, target, tick, lag_gated, &target_origin)) {
    return solution;
  }
  const Vec3 swing_start = local_eye_at(frame, local, tick);
  const bool teammate = target->get_team() == local->get_team();
  const swing_geometry geometry =
    swing_geometry_for(local, weapon, target->to_entity(), teammate);
  if (geometry.range <= 0.0f) {
    return solution;
  }

  Vec3 aim_position = clamp_point_to_bounds(target, target_origin, point.position);
  if (knife_backstab_required(weapon, local, target)) {
    aim_position.x = target_origin.x;
    aim_position.y = target_origin.y;
  }
  const Vec3 aim_angles = aimbot_calculate_angles_to_position(swing_start, aim_position);
  if (!melee_reach_hit(local, weapon, target, target_origin, swing_start, aim_angles,
                       geometry)) {
    return solution;
  }
  if (knife_backstab_required(weapon, local, target) &&
      (razorback_blocks_backstab(target) ||
       !backstab_geometry_ok(target, target_origin, swing_start, aim_angles))) {
    return solution;
  }

  solution.valid = true;
  solution.tick = tick;
  solution.target_origin = target_origin;
  solution.swing_start = swing_start;
  solution.aim_position = aim_position;
  solution.aim_angles = aimbot_clamp_angles(aim_angles);
  return solution;
}

inline swing_solution evaluate_steer(Player* local, Weapon* weapon, Player* target,
                                     const aimbot_point& point) {
  swing_solution solution{};
  if (local == nullptr || weapon == nullptr || target == nullptr || !point.valid) {
    return solution;
  }

  const Vec3 target_origin = target->get_origin();
  const Vec3 swing_start = local->get_shoot_pos();
  const bool teammate = target->get_team() == local->get_team();
  const swing_geometry geometry =
    swing_geometry_for(local, weapon, target->to_entity(), teammate);
  if (geometry.range <= 0.0f) {
    return solution;
  }

  Vec3 aim_position = clamp_point_to_bounds(target, target_origin, point.position);
  if (knife_backstab_required(weapon, local, target)) {
    aim_position.x = target_origin.x;
    aim_position.y = target_origin.y;
  }
  const Vec3 aim_angles = aimbot_calculate_angles_to_position(swing_start, aim_position);
  if (!melee_reach_hit_relaxed(local, weapon, target, target_origin, swing_start, aim_angles,
                               geometry)) {
    return solution;
  }
  if (knife_backstab_required(weapon, local, target) &&
      (razorback_blocks_backstab(target) ||
       !backstab_geometry_ok(target, target_origin, swing_start, aim_angles))) {
    return solution;
  }

  solution.valid = true;
  solution.steer_only = true;
  solution.tick = -1;
  solution.target_origin = target_origin;
  solution.swing_start = swing_start;
  solution.aim_position = aim_position;
  solution.aim_angles = aimbot_clamp_angles(aim_angles);
  return solution;
}

inline bool is_building_class(Entity* entity) {
  if (entity == nullptr) {
    return false;
  }
  switch (entity->get_class_id()) {
  case class_id::SENTRY:
  case class_id::DISPENSER:
  case class_id::OBJECT_CART_DISPENSER:
  case class_id::TELEPORTER:
    return true;
  default:
    return false;
  }
}

inline Building* as_building(Entity* entity) {
  return entity != nullptr ? reinterpret_cast<Building*>(entity) : nullptr;
}

inline bool building_visible(Player* local, Entity* building, const Vec3& from,
                             const Vec3& to) {
  if (engine_trace == nullptr) {
    return false;
  }
  Vec3 start = from;
  Vec3 end = to;
  ray_t ray = engine_trace->init_ray(&start, &end);
  trace_filter filter{};
  engine_trace->init_world_and_props_trace_filter(&filter);
  trace_t result{};
  engine_trace->trace_ray(&ray, MASK_SOLID, &filter, &result);
  return !result.start_solid && !result.all_solid &&
    (result.fraction >= 0.99f || result.entity == building);
}

inline aimbot_candidate building_candidate_base(Entity* building, Player* local,
                                                const Vec3& center,
                                                const Vec3& eye,
                                                const Vec3& original_view_angles) {
  aimbot_candidate candidate{};
  candidate.entity = building;
  candidate.player = nullptr;
  const Vec3 angles = aimbot_calculate_angles_to_position(eye, center);
  candidate.aim_position = center;
  candidate.approach_position = center;
  candidate.aim_angles = aimbot_clamp_angles(angles);
  candidate.fov = aimbot_calculate_fov(candidate.aim_angles, original_view_angles);
  candidate.distance = std::sqrt(aimbot_distance_squared(eye, center));
  candidate.health = as_building(building) != nullptr ? as_building(building)->get_health() : 0;
  candidate.simulation_time = building->get_simulation_time();
  candidate.visible = true;
  candidate.hitbox = -1;
  candidate.studio_hitbox = -1;
  candidate.predicted_origin_valid = false;
  candidate.melee_swing_tick = 0;
  return candidate;
}

inline aimbot_candidate find_sapper_candidate(Player* local, Weapon* weapon,
                                              const Vec3& original_view_angles) {
  aimbot_candidate best{};
  if (local == nullptr || weapon == nullptr || !melee_aim::cfg.sapper_priority) {
    return best;
  }

  const Vec3 eye = local->get_shoot_pos();
  constexpr class_id building_ids[] = {class_id::SENTRY, class_id::DISPENSER,
                                       class_id::OBJECT_CART_DISPENSER,
                                       class_id::TELEPORTER};
  float best_distance_squared = 0.0f;
  for (const class_id id : building_ids) {
    for (Entity* entity : entity_cache_entities(id)) {
      if (entity == nullptr || entity->get_team() == local->get_team() ||
          !is_building_class(entity)) {
        continue;
      }
      Building* building = as_building(entity);
      if (building == nullptr || building->is_carried() || building->is_sapped()) {
        continue;
      }
      const Vec3 center = entity->get_collision_origin() +
        (entity->get_collideable_mins() + entity->get_collideable_maxs()) * 0.5f;
      if (!building_visible(local, entity, eye, center)) {
        continue;
      }
      const float distance_squared = aimbot_distance_squared(eye, center);
      if (best.entity != nullptr && distance_squared >= best_distance_squared) {
        continue;
      }
      best = building_candidate_base(entity, local, center, eye, original_view_angles);
      best.aim_angles.x = normalize_angle(original_view_angles.x);
      best.command_angles = best.aim_angles;
      best.distance = std::sqrt(distance_squared);
      best_distance_squared = distance_squared;
    }
  }
  return best;
}

inline bool building_needs_wrench(Building* building, bool has_metal) {
  if (building == nullptr || building->is_carried()) {
    return false;
  }
  if (!has_metal) {
    return false;
  }
  if (building->is_sapped() || building->get_health() < building->get_max_health()) {
    return true;
  }
  return !building->is_mini_sentry() && building->get_building_level() < 3;
}

inline aimbot_candidate find_friendly_wrench_candidate(Player* local, Weapon* weapon,
                                                       const Vec3& original_view_angles) {
  aimbot_candidate best{};
  if (local == nullptr || weapon == nullptr || !melee_aim::cfg.wrench_friendly_buildings ||
      weapon->get_weapon_id() != TF_WEAPON_WRENCH || local->get_currency() <= 0) {
    return best;
  }

  const bool has_metal = local->get_currency() > 0;
  const Vec3 eye = local->get_shoot_pos();
  constexpr class_id building_ids[] = {class_id::SENTRY, class_id::DISPENSER,
                                       class_id::OBJECT_CART_DISPENSER,
                                       class_id::TELEPORTER};
  float best_distance_squared = 0.0f;
  for (const class_id id : building_ids) {
    for (Entity* entity : entity_cache_entities(id)) {
      if (entity == nullptr || entity->get_team() != local->get_team() ||
          !is_building_class(entity)) {
        continue;
      }
      Building* building = as_building(entity);
      if (!building_needs_wrench(building, has_metal)) {
        continue;
      }
      const Vec3 center = entity->get_collision_origin() +
        (entity->get_collideable_mins() + entity->get_collideable_maxs()) * 0.5f;
      if (!building_visible(local, entity, eye, center)) {
        continue;
      }
      const float distance_squared = aimbot_distance_squared(eye, center);
      if (best.entity != nullptr && distance_squared >= best_distance_squared) {
        continue;
      }
      best = building_candidate_base(entity, local, center, eye, original_view_angles);
      best.distance = std::sqrt(distance_squared);
      best_distance_squared = distance_squared;
    }
  }
  return best;
}

}

inline uint32_t melee_aim_configured_hitbox_mask() {
  const uint32_t configured_mask = config.aimbot.melee_hitboxes & aim_hitbox_mask_all;
  return configured_mask != aim_hitbox_mask_none
    ? configured_mask
    : aim_hitbox_mask_default_melee;
}

inline bool melee_aim_trace_candidate(Player* localplayer,
  Weapon* weapon,
  Player* target,
  const Vec3& target_origin,
  const Vec3& swing_start,
  const Vec3& aim_angles) {
  if (localplayer == nullptr || weapon == nullptr || target == nullptr) {
    return false;
  }
  if (melee_aim_detail::smack_pending(localplayer, weapon)) {
    return melee_aim::cfg.hold_fire;
  }
  if (!melee_aim_detail::shift_window_allows_swing(localplayer, weapon)) {
    return false;
  }
  if (!aimbot_vec3_is_finite(target_origin) || !aimbot_vec3_is_finite(swing_start) ||
      !aimbot_vec3_is_finite(aim_angles)) {
    return false;
  }

  const bool teammate = target->get_team() == localplayer->get_team();
  const melee_aim_detail::swing_geometry geometry =
    melee_aim_detail::swing_geometry_for(localplayer, weapon, target->to_entity(), teammate);
  if (geometry.range <= 0.0f) {
    return false;
  }

  if (!melee_aim_detail::melee_reach_hit(localplayer, weapon, target, target_origin,
                                         swing_start, aim_angles, geometry)) {
    return false;
  }

  if (melee_aim_detail::knife_backstab_required(weapon, localplayer, target) &&
      (melee_aim_detail::razorback_blocks_backstab(target) ||
       !melee_aim_detail::backstab_geometry_ok(target, target_origin, swing_start,
                                               aim_angles))) {
    return false;
  }
  return true;
}

inline bool melee_aim_trace_candidate(Player* localplayer,
  Weapon* weapon,
  Player* target,
  const Vec3& target_origin,
  const Vec3& aim_angles) {
  return localplayer != nullptr && melee_aim_trace_candidate(
    localplayer,
    weapon,
    target,
    target_origin,
    localplayer->get_shoot_pos(),
    aim_angles);
}

inline aimbot_candidate melee_aim_find_candidate(Player* localplayer,
  Weapon* weapon,
  Player* player,
  user_cmd* command,
  const Vec3& original_view_angles) {
  aimbot_candidate candidate{};
  if (localplayer == nullptr || weapon == nullptr || player == nullptr) {
    return candidate;
  }

  if (weapon->get_weapon_id() == TF_WEAPON_BUILDER) {
    return melee_aim_detail::find_sapper_candidate(localplayer, weapon,
                                                   original_view_angles);
  }

  const bool teammate = player->get_team() == localplayer->get_team();
  if (teammate && !aimbot_is_friendlyfire_enabled() &&
      !(melee_aim::cfg.whip_team &&
        melee_aim_detail::attribute_value(0.0f, "speed_buff_ally",
                                          weapon->to_entity()) > 0.0f)) {
    return candidate;
  }

  const aimbot_point point = aimbot_find_best_point(
    localplayer,
    player,
    weapon,
    original_view_angles,
    melee_aim_configured_hitbox_mask(),
    false);
  if (!point.valid) {
    return candidate;
  }

  const melee_aim_detail::target_frame frame =
    melee_aim_detail::build_frame(localplayer, weapon, player, command);

  std::vector<int> validation_ticks{};
  validation_ticks.push_back(0);
  if (frame.max_ticks > 0) {
    const auto push_tick = [&validation_ticks](int tick) {
      if (tick > 0 &&
          std::find(validation_ticks.begin(), validation_ticks.end(), tick) ==
            validation_ticks.end()) {
        validation_ticks.push_back(tick);
      }
    };
    switch (std::clamp(config.aimbot.melee_swing_validate_mode, 0, 2)) {
    case 1:
      push_tick(frame.smack_ticks);
      break;
    case 2:
      push_tick(frame.sim_ticks);
      break;
    default:
      push_tick(frame.sim_ticks);
      if (frame.smack_ticks != frame.sim_ticks) {
        push_tick(frame.smack_ticks);
      }
      break;
    }
  }

  melee_aim_detail::swing_solution chosen{};
  for (const int tick : validation_ticks) {
    const melee_aim_detail::swing_solution solution = melee_aim_detail::evaluate_tick(
      localplayer, weapon, player, point, frame, tick);
    if (solution.valid) {
      chosen = solution;
      break;
    }
  }

  if (!chosen.valid) {
    chosen = melee_aim_detail::evaluate_steer(localplayer, weapon, player, point);
  }

  if (!chosen.valid) {
    return melee_aim_detail::find_friendly_wrench_candidate(
      localplayer, weapon, original_view_angles);
  }

  const bool knife = melee_aim_detail::is_knife(weapon);
  candidate.entity = player->to_entity();
  candidate.player = player;
  candidate.preferred = aimbot_player_is_preferred(player);
  candidate.bone = point.bone;
  candidate.hitbox = point.hitbox;
  candidate.studio_hitbox = point.studio_hitbox;
  candidate.aim_position = chosen.aim_position;
  candidate.approach_position =
    knife && config.aimbot.melee_auto_backstab && !teammate
      ? melee_aim_detail::backstab_approach_position(localplayer, player)
      : chosen.target_origin;
  candidate.aim_angles = chosen.aim_angles;
  candidate.command_angles = chosen.aim_angles;
  candidate.fov = aimbot_calculate_fov(chosen.aim_angles, original_view_angles);
  candidate.distance = std::sqrt(
    aimbot_distance_squared(chosen.swing_start, chosen.aim_position));
  candidate.health = player->get_health();
  candidate.visible = true;
  candidate.simulation_time = player->get_simulation_time();
  if (chosen.steer_only) {
    candidate.melee_swing_tick = -1;
    candidate.predicted_origin_valid = false;
  } else {
    candidate.melee_swing_start = chosen.swing_start;
    candidate.melee_swing_tick = chosen.tick;
    candidate.predicted_origin = chosen.target_origin;
    candidate.predicted_origin_valid = chosen.tick > 0;
  }
  return candidate;
}
#endif
