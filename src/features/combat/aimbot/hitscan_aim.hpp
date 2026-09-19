#ifndef HITSCAN_AIM_HPP
#define HITSCAN_AIM_HPP
#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <climits>
#include <cstdint>
#include "aimbot.hpp"
#include "aim_utils.hpp"
#include "resolver.hpp"
#include "features/combat/backtrack/backtrack.hpp"
#include "games/tf2/sdk/combat_offsets.hpp"
#include "games/tf2/sdk/interfaces/client_state.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"

using backtrack::backtrack_hitbox;
using backtrack::backtrack_record;
using backtrack::backtrack_record_view;
using backtrack::backtrack_timing;

namespace hitscan {

constexpr int max_hitbox_points = 21;
constexpr int max_hitbox_slots = 32;

}


inline Vec3 hitscan_aim_eye_position(Player* localplayer) {
  return localplayer != nullptr ? localplayer->get_shoot_pos() : Vec3{};
}

inline bool hitscan_aim_same_entity(Entity* left, Entity* right) {
  if (left == nullptr || right == nullptr ||
      trace_is_static_prop(left) || trace_is_static_prop(right)) {
    return false;
  }
  if (left == right) {
    return true;
  }

  const int left_index = left->get_index();
  const int right_index = right->get_index();
  return left_index > 0 && left_index == right_index;
}

inline Vec3 hitscan_aim_bullet_angles(Player* localplayer, const Vec3& view_angles) {
  return localplayer != nullptr ? view_angles + localplayer->get_punch_angles() : view_angles;
}

inline Vec3 hitscan_aim_command_angles(Player* localplayer, const Vec3& bullet_angles) {
  return aimbot_clamp_angles(localplayer != nullptr ? bullet_angles - localplayer->get_punch_angles() : bullet_angles);
}


inline aimbot_reject_debug hitscan_aim_make_reject_debug(Player* target,
  aimbot_reject_reason reason,
  float fov = FLT_MAX,
  float distance = FLT_MAX,
  int hitbox = -1,
  int trace_entity_index = -1,
  int trace_hitbox = -1) {
  aimbot_reject_debug debug{};
  debug.reason = reason;
  debug.fov = fov;
  debug.distance = distance;
  debug.hitbox = hitbox;
  debug.trace_entity_index = trace_entity_index;
  debug.trace_hitbox = trace_hitbox;

  if (target != nullptr) {
    debug.entity_index = target->get_index();
    debug.team = static_cast<int>(target->get_team());
    debug.health = target->get_health();
  }

  return debug;
}

inline bool hitscan_aim_reject_better(const aimbot_reject_debug& candidate, const aimbot_reject_debug& best) {
  if (candidate.reason == aimbot_reject_reason::none) {
    return false;
  }
  if (best.reason == aimbot_reject_reason::none) {
    return true;
  }

  const bool candidate_has_fov = std::isfinite(candidate.fov) && candidate.fov < FLT_MAX;
  const bool best_has_fov = std::isfinite(best.fov) && best.fov < FLT_MAX;
  if (candidate_has_fov != best_has_fov) {
    return candidate_has_fov;
  }

  return candidate_has_fov && candidate.fov < best.fov;
}

inline void hitscan_aim_keep_reject(aimbot_reject_debug* accum, const aimbot_reject_debug& reject) {
  if (accum != nullptr && hitscan_aim_reject_better(reject, *accum)) {
    *accum = reject;
  }
}


inline uint32_t hitscan_aim_configured_hitbox_mask() {
  const uint32_t mask = config.aimbot.hitscan_hitboxes & aim_hitbox_mask_all;
  return mask != aim_hitbox_mask_none ? mask : aim_hitbox_mask_default_hitscan;
}

inline bool hitscan_aim_waits_for_headshot(Weapon* weapon) {
  return weapon != nullptr && weapon->is_headshot_weapon() &&
    aimbot_modifier_enabled(Aim::hitscan_mod_wait_for_headshot);
}

inline uint32_t hitscan_aim_effective_hitbox_mask(Weapon* weapon) {
  const uint32_t configured_mask = hitscan_aim_configured_hitbox_mask();
  const bool configured_head_only =
    (configured_mask & aim_hitbox_mask_head) != 0 &&
    (configured_mask & ~aim_hitbox_mask_head) == 0;

  if (weapon != nullptr && !weapon->is_headshot_weapon() && configured_head_only) {
    return configured_mask | aim_hitbox_mask_body | aim_hitbox_mask_pelvis;
  }

  if (weapon != nullptr && weapon->is_headshot_weapon() &&
      aimbot_modifier_enabled(Aim::hitscan_mod_wait_for_headshot) &&
      aimbot_modifier_enabled(Aim::hitscan_mod_body_aim_if_lethal)) {
    return configured_mask | aim_hitbox_mask_body | aim_hitbox_mask_pelvis;
  }

  return ((weapon != nullptr && weapon->is_headshot_weapon() &&
      aimbot_modifier_enabled(Aim::hitscan_mod_headshot_only)) ||
      hitscan_aim_waits_for_headshot(weapon))
    ? aim_hitbox_mask_head
    : configured_mask;
}

inline bool hitscan_aim_head_only(uint32_t hitbox_mask) {
  return (hitbox_mask & aim_hitbox_mask_head) != 0 && (hitbox_mask & ~aim_hitbox_mask_head) == 0;
}

inline bool hitscan_aim_body_forced(Player* localplayer, Weapon* weapon, Player* target) {
  if (localplayer == nullptr || weapon == nullptr || target == nullptr) {
    return false;
  }

  return weapon->is_headshot_weapon() &&
    aimbot_modifier_enabled(Aim::hitscan_mod_body_aim_if_lethal) &&
    aimbot_body_aim_lethal(localplayer, weapon, target);
}

inline int hitscan_aim_first_hitbox_for_mask(uint32_t mask, const std::array<int, 18>& order) {
  for (const int hitbox : order) {
    if (aimbot_hitbox_matches_mask(hitbox, mask)) {
      return hitbox;
    }
  }

  return -1;
}

inline int hitscan_aim_priority_hitbox(Player* localplayer,
  Weapon* weapon,
  Player* target,
  uint32_t hitbox_mask,
  bool wait_for_headshot) {
  constexpr std::array<int, 18> body_order{
    aim_hitbox_spine_3,
    aim_hitbox_spine_2,
    aim_hitbox_spine_1,
    aim_hitbox_spine_0,
    aim_hitbox_pelvis,
    aim_hitbox_left_upper_arm,
    aim_hitbox_right_upper_arm,
    aim_hitbox_left_thigh,
    aim_hitbox_right_thigh,
    aim_hitbox_left_forearm,
    aim_hitbox_right_forearm,
    aim_hitbox_left_calf,
    aim_hitbox_right_calf,
    aim_hitbox_left_hand,
    aim_hitbox_right_hand,
    aim_hitbox_left_foot,
    aim_hitbox_right_foot,
    aim_hitbox_head
  };

  if (hitbox_mask == aim_hitbox_mask_none) {
    return -1;
  }

  if (!hitscan_aim_head_only(hitbox_mask) &&
      hitscan_aim_body_forced(localplayer, weapon, target)) {
    uint32_t body_mask = hitbox_mask & ~aim_hitbox_mask_head;
    if (body_mask == 0) {
      body_mask = aim_hitbox_mask_body | aim_hitbox_mask_pelvis;
    }
    return hitscan_aim_first_hitbox_for_mask(body_mask, body_order);
  }

  if (weapon != nullptr && weapon->is_headshot_weapon() &&
      (hitbox_mask & aim_hitbox_mask_head) != 0) {
    return aim_hitbox_head;
  }

  const bool head_ready = aimbot_headshot_ready_for_priority(localplayer, weapon);
  const bool wait_prefers_head = wait_for_headshot &&
    (weapon == nullptr || !weapon->is_sniper_rifle() || aimbot_sniper_scope_active(localplayer));
  if (weapon != nullptr &&
      weapon->is_headshot_weapon() &&
      (wait_prefers_head || head_ready) &&
      (hitbox_mask & aim_hitbox_mask_head) != 0) {
    return aim_hitbox_head;
  }

  return hitscan_aim_first_hitbox_for_mask(hitbox_mask, body_order);
}


inline bool hitscan_aim_get_bones(Player* target,
  matrix_3x4* bone_to_world,
  int* bone_count_out = nullptr,
  Player* localplayer = nullptr) {
  if (target == nullptr || bone_to_world == nullptr) {
    return false;
  }

  resolver::hitscan_pose_guard resolver_pose{};
  if (resolver::begin_hitscan_pose(localplayer, target, &resolver_pose)) {
    aimbot_clear_network_pose(target);
    aimbot_capture_latest_network_pose(target, true);
    const bool copied = aimbot_copy_network_pose_bones(target, bone_to_world, bone_count_out);
    resolver_pose.restore();
    aimbot_bone_failure = copied
      ? aimbot_reject_reason::none
      : aimbot_reject_reason::bone_reconstruction;
    return copied;
  }

  return aimbot_get_bones(target, bone_to_world, bone_count_out);
}


struct hitscan_trace_result {
  bool hit = false;
  bool clear = false;
  bool start_solid = false;
  bool all_solid = false;
  Entity* entity = nullptr;
  int hitbox = -1;
  int contents = 0;
  float fraction = 1.0f;
  Vec3 end{};
};

using hitscan_aim_trace_result = hitscan_trace_result;

inline hitscan_trace_result hitscan_aim_trace_line(Player* localplayer,
  const Vec3& start_pos,
  const Vec3& end_pos,
  Entity* target = nullptr,
  bool ignore_target = false) {
  hitscan_trace_result result{};
  if (engine_trace == nullptr || localplayer == nullptr) {
    return result;
  }

  Vec3 start = start_pos;
  Vec3 end = end_pos;
  ray_t ray = engine_trace->init_ray(&start, &end);
  trace_filter filter{};
  engine_trace->init_hitscan_trace_filter(&filter, localplayer, target);
  filter.ignore_target = ignore_target;
  trace_t trace{};
  engine_trace->trace_ray(&ray, aimbot_visibility_trace_mask(), &filter, &trace);

  result.entity = static_cast<Entity*>(trace.entity);
  result.hitbox = trace.hitbox;
  result.contents = trace.contents;
  result.fraction = trace.fraction;
  result.end = trace.endpos;
  result.start_solid = trace.start_solid;
  result.all_solid = trace.all_solid;
  result.clear = !trace.all_solid && !trace.start_solid && trace.fraction >= 0.999f;
  result.hit = result.entity != nullptr || result.clear;
  return result;
}

inline void hitscan_aim_set_trace_debug(aimbot_reject_debug* debug,
  const Vec3& start_pos,
  const Vec3& point,
  const hitscan_trace_result& trace) {
  if (debug == nullptr) {
    return;
  }

  debug->trace_contents = trace.contents;
  debug->trace_fraction = trace.fraction;
  debug->trace_start = start_pos;
  debug->trace_point = point;
  debug->trace_end = trace.end;
}

inline bool hitscan_aim_ray_hits_entity_bounds(Entity* target,
  const Vec3& start_pos,
  const Vec3& end_pos);

inline bool hitscan_aim_collision_bounds(Entity* target, Vec3* mins_out, Vec3* maxs_out) {
  if (target == nullptr || mins_out == nullptr || maxs_out == nullptr ||
      trace_is_static_prop(target)) {
    return false;
  }

  const Vec3 origin = target->get_collision_origin();
  const Vec3 mins = origin + target->get_collideable_mins();
  const Vec3 maxs = origin + target->get_collideable_maxs();
  if (!aimbot_vec3_is_finite(mins) || !aimbot_vec3_is_finite(maxs)) {
    return false;
  }

  *mins_out = mins;
  *maxs_out = maxs;
  return true;
}

inline bool hitscan_aim_world_blocks_before(const Vec3& start_pos, const Vec3& end_pos,
  const Vec3& mins, const Vec3& maxs) {
  if (!aimbot_vec3_is_finite(start_pos) || !aimbot_vec3_is_finite(end_pos)) {
    return true;
  }
  return aimbot_world_hits_before_bounds(start_pos, end_pos, mins, maxs);
}

inline bool hitscan_aim_world_blocks_before(const Vec3& start_pos, const Vec3& end_pos,
  Entity* target) {
  Vec3 mins{};
  Vec3 maxs{};
  if (!hitscan_aim_collision_bounds(target, &mins, &maxs)) {
    return true;
  }
  return hitscan_aim_world_blocks_before(start_pos, end_pos, mins, maxs);
}

inline bool hitscan_aim_trace_point(Player* localplayer,
  Entity* target,
  const Vec3& point,
  const Vec3& shoot_pos,
  hitscan_trace_result* result_out = nullptr) {
  if (localplayer == nullptr || target == nullptr || !aimbot_vec3_is_finite(point)) {
    return false;
  }

  const Vec3 to_point = point - shoot_pos;
  const float distance = std::sqrt((to_point.x * to_point.x) + (to_point.y * to_point.y) + (to_point.z * to_point.z));
  if (distance <= 0.001f) {
    return false;
  }

  if (hitscan_aim_world_blocks_before(shoot_pos, point, target)) {
    if (result_out != nullptr) {
      *result_out = {};
    }
    return false;
  }

  hitscan_trace_result result = hitscan_aim_trace_line(localplayer, shoot_pos, point, target);
  if (result_out != nullptr) {
    *result_out = result;
  }
  return !result.start_solid && !result.all_solid && hitscan_aim_same_entity(result.entity, target);
}

inline bool hitscan_aim_ray_hits_entity_bounds(Entity* target,
  const Vec3& start_pos,
  const Vec3& end_pos) {
  if (target == nullptr) {
    return false;
  }

  const Vec3 origin = target->get_collision_origin();
  const Vec3 mins = target->get_collideable_mins() + origin - Vec3{2.0f, 2.0f, 2.0f};
  const Vec3 maxs = target->get_collideable_maxs() + origin + Vec3{2.0f, 2.0f, 2.0f};
  if (!aimbot_vec3_is_finite(mins) || !aimbot_vec3_is_finite(maxs)) {
    return false;
  }

  return aimbot_segment_intersects_aabb(start_pos, end_pos, mins, maxs);
}


struct hitscan_scan_context {
  Player* localplayer = nullptr;
  Player* target = nullptr;
  Weapon* weapon = nullptr;
  Vec3 view_angles{};
  Vec3 last_input_angles{};
  Vec3 shoot_pos{};
  Vec3 peek_pos{};
  Vec3 hull_mins{};
  Vec3 hull_maxs{};
  uint32_t hitbox_mask = 0;
  int priority_hitbox = -1;
  bool head_locked = false;
  bool body_forced = false;
  bool has_last_input_angles = false;
  bool peek_enabled = false;
  bool hull_valid = false;
};

struct hitscan_point {
  bool valid = false;
  bool fireable = false;
  int bone = 0;
  int hitbox = -1;
  int studio_hitbox = -1;
  int priority = 0;
  Vec3 position{};
  Vec3 angles{};
  float fov = FLT_MAX;
  float inset = 0.0f;
  aimbot_reject_debug reject_debug{};
};

struct hitscan_hitbox_slot {
  int hitbox = -1;
  int studio_hitbox = -1;
  int bone = -1;
  int priority = 0;
  Vec3 mins{};
  Vec3 maxs{};
  const matrix_3x4* bone_to_world = nullptr;
};

inline bool hitscan_aim_hull_usable(const Vec3& mins, const Vec3& maxs) {
  return aimbot_vec3_is_finite(mins) &&
    aimbot_vec3_is_finite(maxs) &&
    maxs.x > mins.x + 1.0f &&
    maxs.y > mins.y + 1.0f &&
    maxs.z > mins.z + 8.0f;
}

inline float (*hitscan_aim_get_bullet_spread)(void*) = nullptr;
inline bool hitscan_aim_bullet_spread_ready = false;
inline bool hitscan_aim_bullet_spread_signature_found = false;

inline bool hitscan_aim_init_bullet_spread() {
  if (hitscan_aim_bullet_spread_ready) {
    return hitscan_aim_get_bullet_spread != nullptr;
  }

  hitscan_aim_bullet_spread_ready = true;
  hitscan_aim_get_bullet_spread = reinterpret_cast<float (*)(void*)>(
    sigscan_module("client.so", sigs::tf_weapon_base_gun_get_bullet_spread));
  hitscan_aim_bullet_spread_signature_found = hitscan_aim_get_bullet_spread != nullptr;
  return hitscan_aim_get_bullet_spread != nullptr;
}

inline float hitscan_aim_weapon_spread(Weapon* weapon) {
  if (weapon == nullptr || aimbot_is_projectile_weapon(weapon) || aimbot_is_melee_weapon(weapon)) {
    return 0.0f;
  }

  if (hitscan_aim_init_bullet_spread()) {
    const float spread = hitscan_aim_get_bullet_spread(weapon);
    if (std::isfinite(spread)) {
      return spread > 0.0f ? spread : 0.0f;
    }
  }

  using get_weapon_spread_fn = float (*)(void*);
  get_weapon_spread_fn fn = reinterpret_cast<get_weapon_spread_fn>(tf2_combat::get().get_weapon_spread_fn);
  if (fn == nullptr) {
    void** vtable = *reinterpret_cast<void***>(weapon);
    const std::size_t slot = tf2_combat::weapon::get_weapon_spread();
    if (vtable != nullptr && slot != 0 && vtable[slot] != nullptr) {
      fn = reinterpret_cast<get_weapon_spread_fn>(vtable[slot]);
    }
  }
  if (fn != nullptr) {
    const float spread = fn(weapon);
    if (std::isfinite(spread)) {
      return spread > 0.0f ? spread : 0.0f;
    }
  }

  return weapon->get_hitscan_spread();
}

inline bool hitscan_aim_want_peek(Weapon* weapon) {
  return config.aimbot.peek_ticks > 0 &&
    weapon != nullptr &&
    hitscan_aim_weapon_spread(weapon) > 0.0f;
}

inline bool hitscan_aim_peek_origin(Player* localplayer, const Vec3& shoot_pos, Vec3* peek_out) {
  if (localplayer == nullptr || peek_out == nullptr || !aimbot_vec3_is_finite(shoot_pos)) {
    return false;
  }

  *peek_out = shoot_pos - localplayer->get_velocity() * ticks_to_time(config.aimbot.peek_ticks);
  if (!aimbot_vec3_is_finite(*peek_out)) {
    return false;
  }

  const Vec3 delta = *peek_out - shoot_pos;
  return (delta.x * delta.x) + (delta.y * delta.y) + (delta.z * delta.z) > 1.0f;
}

inline bool hitscan_aim_peek_visible(const hitscan_scan_context& ctx, const Vec3& point) {
  if (!ctx.peek_enabled) {
    return true;
  }
  return hitscan_aim_trace_point(ctx.localplayer, ctx.target, point, ctx.peek_pos);
}

inline bool hitscan_aim_peek_clears_shot(Player* localplayer,
  Weapon* weapon,
  Entity* target,
  const Vec3& shoot_pos,
  const Vec3& aim_position) {
  Vec3 peek_pos{};
  if (!hitscan_aim_want_peek(weapon) ||
      !hitscan_aim_peek_origin(localplayer, shoot_pos, &peek_pos)) {
    return true;
  }
  return hitscan_aim_trace_point(localplayer, target, aim_position, peek_pos);
}

inline bool hitscan_aim_fired_ray_hits(const hitscan_scan_context& ctx,
  const hitscan_hitbox_slot& slot,
  const Vec3& command_angles,
  const Vec3& spread_offset,
  bool use_spread,
  float* inset_out = nullptr,
  hitscan_trace_result* trace_out = nullptr) {
  if (slot.bone_to_world == nullptr || ctx.weapon == nullptr || ctx.localplayer == nullptr) {
    return false;
  }

  Vec3 forward{};
  Vec3 right{};
  Vec3 up{};
  angle_vectors(hitscan_aim_bullet_angles(ctx.localplayer, command_angles), &forward, &right, &up);
  if (!aimbot_vec3_is_finite(forward)) {
    return false;
  }
  if (use_spread) {
    forward = aimbot_normalize_vector(forward + (right * spread_offset.x) + (up * spread_offset.y));
    if (!aimbot_vec3_is_finite(forward)) {
      return false;
    }
  }

  const float weapon_range = ctx.weapon->get_hitscan_range();
  const float trace_length = weapon_range > 0.0f ? weapon_range : 8192.0f;
  const Vec3 end_pos = ctx.shoot_pos + (forward * trace_length);

  Vec3 shrunk_mins{};
  Vec3 shrunk_maxs{};
  if (!aimbot_shrink_aabb(slot.mins, slot.maxs, &shrunk_mins, &shrunk_maxs)) {
    shrunk_mins = slot.mins;
    shrunk_maxs = slot.maxs;
  }

  float enter = 0.0f;
  float inset = 0.0f;
  if (!aimbot_ray_hits_obb(ctx.shoot_pos, end_pos, *slot.bone_to_world, shrunk_mins, shrunk_maxs, &enter, &inset)) {
    return false;
  }
  if (ctx.hull_valid &&
      !aimbot_segment_intersects_aabb(ctx.shoot_pos, end_pos, ctx.hull_mins, ctx.hull_maxs)) {
    return false;
  }

  const Vec3 surface = ctx.shoot_pos + (end_pos - ctx.shoot_pos) * std::clamp(enter, 0.0f, 1.0f);
  if (ctx.hull_valid) {
    if (hitscan_aim_world_blocks_before(ctx.shoot_pos, surface, ctx.hull_mins, ctx.hull_maxs)) {
      return false;
    }
  } else if (hitscan_aim_world_blocks_before(ctx.shoot_pos, surface, ctx.target)) {
    return false;
  }

  hitscan_trace_result trace = hitscan_aim_trace_line(ctx.localplayer, ctx.shoot_pos, end_pos, ctx.target);
  if (trace_out != nullptr) {
    *trace_out = trace;
  }
  if (trace.start_solid || trace.all_solid || !hitscan_aim_same_entity(trace.entity, ctx.target)) {
    return false;
  }
  if (inset_out != nullptr) {
    *inset_out = inset;
  }
  return true;
}

inline bool hitscan_aim_point_fireable(const hitscan_scan_context& ctx,
  const hitscan_hitbox_slot& slot,
  const hitscan_point& point,
  float* inset_out = nullptr) {
  if (ctx.hull_valid) {
    if (hitscan_aim_world_blocks_before(ctx.shoot_pos, point.position, ctx.hull_mins, ctx.hull_maxs)) {
      return false;
    }
  } else if (hitscan_aim_world_blocks_before(ctx.shoot_pos, point.position, ctx.target)) {
    return false;
  }
  return hitscan_aim_fired_ray_hits(
    ctx,
    slot,
    hitscan_aim_command_angles(ctx.localplayer, point.angles),
    {},
    false,
    inset_out);
}

inline bool hitscan_aim_point_better(const hitscan_point& candidate,
  const hitscan_point& best,
  bool have_best) {
  if (!have_best) {
    return true;
  }
  if (candidate.priority != best.priority) {
    return candidate.priority < best.priority;
  }
  if (candidate.inset != best.inset) {
    return candidate.inset > best.inset;
  }
  return candidate.fov < best.fov;
}

inline hitscan_point hitscan_aim_evaluate_point(const hitscan_scan_context& ctx,
  const hitscan_hitbox_slot& slot,
  const Vec3& position) {
  hitscan_point point{};
  if (!aimbot_vec3_is_finite(position) || slot.bone_to_world == nullptr) {
    point.reject_debug = hitscan_aim_make_reject_debug(ctx.target, aimbot_reject_reason::invalid);
    return point;
  }

  const float weapon_range = ctx.weapon->get_hitscan_range();
  const Vec3 to_point = position - ctx.shoot_pos;
  const float point_distance = std::sqrt((to_point.x * to_point.x) + (to_point.y * to_point.y) + (to_point.z * to_point.z));
  if (!std::isfinite(point_distance) ||
      (weapon_range > 0.0f && point_distance > weapon_range)) {
    point.reject_debug = hitscan_aim_make_reject_debug(
      ctx.target,
      aimbot_reject_reason::no_point,
      FLT_MAX,
      point_distance,
      slot.hitbox);
    return point;
  }

  const Vec3 aim_angles = aimbot_calculate_angles_to_position(ctx.shoot_pos, position);
  const float fov = aimbot_calculate_fov(hitscan_aim_command_angles(ctx.localplayer, aim_angles), ctx.view_angles);
  if (aimbot_fov_exceeds_limit(fov, 1.35f)) {
    point.reject_debug = hitscan_aim_make_reject_debug(
      ctx.target, aimbot_reject_reason::fov, fov, point_distance, slot.hitbox);
    return point;
  }

  point.bone = slot.bone;
  point.hitbox = slot.hitbox;
  point.studio_hitbox = slot.studio_hitbox;
  point.priority = slot.priority;
  point.position = position;
  point.angles = aim_angles;
  point.fov = fov;

  float inset = 0.0f;
  if (hitscan_aim_point_fireable(ctx, slot, point, &inset) &&
      hitscan_aim_peek_visible(ctx, position)) {
    point.valid = true;
    point.fireable = true;
    point.inset = inset;
    return point;
  }

  hitscan_trace_result vis{};
  if (!hitscan_aim_trace_point(ctx.localplayer, ctx.target, position, ctx.shoot_pos, &vis)) {
    point.reject_debug = hitscan_aim_make_reject_debug(ctx.target,
      aimbot_reject_reason::trace_blocked, fov, point_distance, slot.hitbox,
      vis.entity != nullptr ? vis.entity->get_index() : -1, vis.hitbox);
    hitscan_aim_set_trace_debug(&point.reject_debug, ctx.shoot_pos, position, vis);
    return point;
  }

  point.reject_debug = hitscan_aim_make_reject_debug(ctx.target,
    aimbot_reject_reason::trace_blocked, fov, point_distance, slot.hitbox);
  return point;
}

inline bool hitscan_aim_scan_slots(const hitscan_scan_context& ctx,
  const std::array<hitscan_hitbox_slot, hitscan::max_hitbox_slots>& slots,
  int slot_count,
  int pass_mode,
  hitscan_point* point_out,
  const hitscan_hitbox_slot** slot_out,
  aimbot_reject_debug* reject_accum) {
  if (point_out == nullptr || slot_count <= 0) {
    return false;
  }

  hitscan_point best_fireable{};
  const hitscan_hitbox_slot* best_fireable_slot = nullptr;

  for (int index = 0; index < slot_count; ++index) {
    const hitscan_hitbox_slot& slot = slots[static_cast<std::size_t>(index)];
    if (slot.bone_to_world == nullptr) {
      continue;
    }
    if (pass_mode == 0 && slot.priority != 0) {
      continue;
    }
    if (pass_mode == 1 && slot.priority == 0) {
      continue;
    }
    if (ctx.head_locked && slot.hitbox != aim_hitbox_head) {
      continue;
    }
    if (ctx.body_forced && slot.hitbox == aim_hitbox_head) {
      continue;
    }

    studio_box box{};
    box.bone = slot.bone;
    box.bbmin = slot.mins;
    box.bbmax = slot.maxs;

    Vec3 local_points[hitscan::max_hitbox_points]{};
    const bool use_multipoint = slot.priority == 0 &&
      (slot.hitbox == aim_hitbox_head || config.aimbot.multipoint_scale > 0.0f);
    const int point_count = aimbot_build_local_hitbox_points(
      box,
      *slot.bone_to_world,
      ctx.shoot_pos,
      local_points,
      hitscan::max_hitbox_points,
      use_multipoint,
      slot.hitbox);

    for (int point_index = 0; point_index < point_count; ++point_index) {
      const Vec3 position = aimbot_transform_point(local_points[point_index], *slot.bone_to_world);
      hitscan_point point = hitscan_aim_evaluate_point(ctx, slot, position);
      if (!point.valid) {
        hitscan_aim_keep_reject(reject_accum, point.reject_debug);
        continue;
      }

      if (point.fireable && hitscan_aim_point_better(point, best_fireable, best_fireable_slot != nullptr)) {
        best_fireable = point;
        best_fireable_slot = &slot;
      }

      if (point.fireable && point_index == 0) {
        break;
      }
    }

    if (best_fireable_slot != nullptr &&
        index + 1 < slot_count &&
        slots[static_cast<std::size_t>(index + 1)].priority > best_fireable.priority) {
      break;
    }
  }

  if (best_fireable_slot != nullptr) {
    *point_out = best_fireable;
    if (slot_out != nullptr) {
      *slot_out = best_fireable_slot;
    }
    return true;
  }
  return false;
}


inline int hitscan_aim_build_record_slots(const hitscan_scan_context& ctx,
  const backtrack_record& record,
  std::array<hitscan_hitbox_slot, hitscan::max_hitbox_slots>* slots_out) {
  if (slots_out == nullptr || record.hitbox_count <= 0 || record.bone_count <= 0) {
    return 0;
  }

  std::array<hitscan_hitbox_slot, hitscan::max_hitbox_slots>& slots = *slots_out;
  int count = 0;
  for (int index = 0; index < record.hitbox_count && count < hitscan::max_hitbox_slots; ++index) {
    const backtrack_hitbox& hitbox = record.hitboxes[index];
    if (!hitbox.valid || hitbox.bone < 0 || hitbox.bone >= record.bone_count) {
      continue;
    }
    if (!aimbot_hitbox_matches_mask(hitbox.hitbox, ctx.hitbox_mask)) {
      continue;
    }

    const int raw_priority = aimbot_hitbox_priority(ctx.localplayer, ctx.target, ctx.weapon, hitbox.hitbox);
    if (raw_priority == INT_MAX) {
      continue;
    }

    slots[static_cast<std::size_t>(count)] = {
      .hitbox = hitbox.hitbox,
      .studio_hitbox = hitbox.studio_hitbox,
      .bone = hitbox.bone,
      .priority = hitbox.hitbox == ctx.priority_hitbox ? 0 : raw_priority + 1,
      .mins = hitbox.mins,
      .maxs = hitbox.maxs,
      .bone_to_world = &record.bones[static_cast<std::size_t>(hitbox.bone)]
    };
    ++count;
  }

  std::sort(slots.begin(), slots.begin() + count, [](const hitscan_hitbox_slot& left, const hitscan_hitbox_slot& right) {
    if (left.priority != right.priority) {
      return left.priority < right.priority;
    }
    return left.studio_hitbox < right.studio_hitbox;
  });

  return count;
}

struct hitscan_found {
  bool valid = false;
  hitscan_point point{};
  const backtrack_record* record = nullptr;
  int command_tick = 0;
  float sim_time = 0.0f;
  float distance = FLT_MAX;
  bool on_shot = false;
  float timing_error = 0.0f;
  float capture_gap = 0.0f;
  Vec3 hull_world_mins{};
  Vec3 hull_world_maxs{};
  Vec3 hitbox_local_mins{};
  Vec3 hitbox_local_maxs{};
  matrix_3x4 hitbox_bone{};
  bool pose_timing_valid = false;
  int pose_target_tick = 0;
  float inset = 0.0f;
  bool hull_valid = false;
  aimbot_reject_debug reject{};
};

inline bool hitscan_aim_apply_hull(hitscan_scan_context* ctx, const Vec3& origin, const Vec3& mins, const Vec3& maxs) {
  if (ctx == nullptr) {
    return false;
  }

  ctx->hull_mins = origin + mins;
  ctx->hull_maxs = origin + maxs;
  ctx->hull_valid = hitscan_aim_hull_usable(ctx->hull_mins, ctx->hull_maxs);
  if (!ctx->hull_valid) {
    ctx->hull_mins = {};
    ctx->hull_maxs = {};
  }
  return ctx->hull_valid;
}

inline bool hitscan_aim_apply_entity_hull(hitscan_scan_context* ctx, Entity* target) {
  if (ctx == nullptr || target == nullptr) {
    return false;
  }

  return hitscan_aim_apply_hull(
    ctx,
    target->get_collision_origin(),
    target->get_collideable_mins(),
    target->get_collideable_maxs());
}

inline bool hitscan_aim_found_better(const hitscan_found& candidate, const hitscan_found& best) {
  if (!best.valid) {
    return true;
  }
  if (candidate.point.fireable != best.point.fireable) {
    return candidate.point.fireable;
  }
  if (config.backtrack.prefer_on_shot && candidate.on_shot != best.on_shot) {
    return candidate.on_shot;
  }
  if (candidate.point.priority != best.point.priority) {
    return candidate.point.priority < best.point.priority;
  }
  if (candidate.timing_error != best.timing_error) {
    return candidate.timing_error < best.timing_error;
  }
  if (candidate.point.inset != best.point.inset) {
    return candidate.point.inset > best.point.inset;
  }
  return candidate.point.fov < best.point.fov;
}

inline bool hitscan_aim_scan_records(const hitscan_scan_context& ctx,
  const backtrack_timing& timing,
  hitscan_found* found_out,
  aimbot_reject_debug* reject_accum) {
  if (found_out == nullptr) {
    return false;
  }

  const backtrack_record_view view = backtrack::valid_records(ctx.target, 0.0f, true);
  if (view.count <= 0) {
    return false;
  }

  hitscan_found best{};
  const auto fill = [&](hitscan_found* out, const backtrack_record& record, const hitscan_point& point,
    const hitscan_hitbox_slot* slot, int command_tick) {
    out->valid = true;
    out->point = point;
    out->record = &record;
    out->command_tick = command_tick;
    out->sim_time = record.sim_time;
    out->distance = distance_3d(ctx.localplayer->get_origin(), record.origin);
    out->on_shot = record.on_shot;
    out->timing_error = backtrack::record_timing_score(timing, record);
    out->capture_gap = backtrack::record_capture_gap(record);
    out->hull_world_mins = record.origin + record.mins;
    out->hull_world_maxs = record.origin + record.maxs;
    out->hull_valid = hitscan_aim_hull_usable(out->hull_world_mins, out->hull_world_maxs);
    if (!out->hull_valid) {
      out->hull_world_mins = {};
      out->hull_world_maxs = {};
    }
    out->hitbox_local_mins = slot != nullptr ? slot->mins : Vec3{};
    out->hitbox_local_maxs = slot != nullptr ? slot->maxs : Vec3{};
    out->hitbox_bone = slot != nullptr ? *slot->bone_to_world : matrix_3x4{};
    out->pose_target_tick = time_to_ticks(record.sim_time);
    out->inset = point.inset;
  };

  for (int index = 0; index < view.count; ++index) {
    const backtrack_record* record = view.records[static_cast<std::size_t>(index)];
    if (record == nullptr) {
      continue;
    }

    hitscan_scan_context record_ctx = ctx;
    hitscan_aim_apply_hull(&record_ctx, record->origin, record->mins, record->maxs);

    std::array<hitscan_hitbox_slot, hitscan::max_hitbox_slots> slots{};
    const int slot_count = hitscan_aim_build_record_slots(record_ctx, *record, &slots);
    if (slot_count <= 0) {
      continue;
    }

    hitscan_point point{};
    const hitscan_hitbox_slot* slot = nullptr;
    if (!hitscan_aim_scan_slots(record_ctx, slots, slot_count, -1, &point, &slot, reject_accum)) {
      continue;
    }

    int command_tick = 0;
    if (!backtrack::command_tick_for_record(*record, ctx.target, &command_tick)) {
      continue;
    }

    hitscan_found candidate{};
    fill(&candidate, *record, point, slot, command_tick);
    if (hitscan_aim_found_better(candidate, best)) {
      best = candidate;
    }
    if (best.point.fireable &&
        best.point.priority == 0 &&
        (best.on_shot || !config.backtrack.prefer_on_shot)) {
      break;
    }
  }

  if (!best.valid) {
    return false;
  }
  *found_out = best;
  return true;
}


inline int hitscan_aim_build_studio_slots(const hitscan_scan_context& ctx,
  studio_hitbox_set* hitbox_set,
  const matrix_3x4* bone_to_world,
  int bone_count,
  std::array<hitscan_hitbox_slot, hitscan::max_hitbox_slots>* slots_out) {
  if (hitbox_set == nullptr || slots_out == nullptr || bone_to_world == nullptr) {
    return 0;
  }

  std::array<hitscan_hitbox_slot, hitscan::max_hitbox_slots>& slots = *slots_out;
  int count = 0;
  for (int studio_hitbox_id = 0; studio_hitbox_id < hitbox_set->num_hitboxes && count < hitscan::max_hitbox_slots; ++studio_hitbox_id) {
    studio_box* hitbox = hitbox_set->hitbox(studio_hitbox_id);
    if (hitbox == nullptr || hitbox->bone < 0 || hitbox->bone >= bone_count) {
      continue;
    }

    const int base_hitbox = aimbot_studio_hitbox_to_base(ctx.target, studio_hitbox_id);
    if (base_hitbox < 0 || !aimbot_hitbox_matches_mask(base_hitbox, ctx.hitbox_mask)) {
      continue;
    }

    const int raw_priority = aimbot_hitbox_priority(ctx.localplayer, ctx.target, ctx.weapon, base_hitbox);
    if (raw_priority == INT_MAX) {
      continue;
    }

    slots[static_cast<std::size_t>(count)] = {
      .hitbox = base_hitbox,
      .studio_hitbox = studio_hitbox_id,
      .bone = hitbox->bone,
      .priority = base_hitbox == ctx.priority_hitbox ? 0 : raw_priority + 1,
      .mins = hitbox->bbmin,
      .maxs = hitbox->bbmax,
      .bone_to_world = &bone_to_world[hitbox->bone]
    };
    ++count;
  }

  std::sort(slots.begin(), slots.begin() + count, [](const hitscan_hitbox_slot& left, const hitscan_hitbox_slot& right) {
    if (left.priority != right.priority) {
      return left.priority < right.priority;
    }
    return left.studio_hitbox < right.studio_hitbox;
  });

  return count;
}

inline bool hitscan_aim_scan_live_pose(const hitscan_scan_context& ctx,
  hitscan_found* found_out,
  aimbot_reject_debug* reject_accum) {
  if (found_out == nullptr || model_info == nullptr) {
    return false;
  }

  const model_t* model = ctx.target->get_model();
  studio_hdr* hdr = model != nullptr ? model_info->get_studio_model(model) : nullptr;
  studio_hitbox_set* hitbox_set = hdr != nullptr ? hdr->hitbox_set(ctx.target->get_hitbox_set()) : nullptr;
  if (hitbox_set == nullptr) {
    hitscan_aim_keep_reject(reject_accum,
      hitscan_aim_make_reject_debug(ctx.target,
        model == nullptr ? aimbot_reject_reason::no_model
          : hdr == nullptr ? aimbot_reject_reason::no_studio_model
          : aimbot_reject_reason::no_hitbox_set));
    return false;
  }

  matrix_3x4 bone_to_world[aimbot_max_bones]{};
  int bone_count = 0;
  if (!hitscan_aim_get_bones(ctx.target, bone_to_world, &bone_count, ctx.localplayer)) {
    const aimbot_reject_reason failure = aimbot_last_bone_failure();
    hitscan_aim_keep_reject(reject_accum,
      hitscan_aim_make_reject_debug(
        ctx.target,
        failure == aimbot_reject_reason::none ? aimbot_reject_reason::bone_cache : failure));
    return false;
  }

  std::array<hitscan_hitbox_slot, hitscan::max_hitbox_slots> slots{};
  const int slot_count = hitscan_aim_build_studio_slots(
    ctx, hitbox_set, bone_to_world, bone_count, &slots);
  if (slot_count <= 0) {
    hitscan_aim_keep_reject(reject_accum,
      hitscan_aim_make_reject_debug(ctx.target, aimbot_reject_reason::no_hitbox));
    return false;
  }

  hitscan_point best{};
  const hitscan_hitbox_slot* best_slot = nullptr;
  if (!hitscan_aim_scan_slots(ctx, slots, slot_count, -1, &best, &best_slot, reject_accum)) {
    return false;
  }

  found_out->valid = true;
  found_out->point = best;
  found_out->hitbox_local_mins = best_slot != nullptr ? best_slot->mins : Vec3{};
  found_out->hitbox_local_maxs = best_slot != nullptr ? best_slot->maxs : Vec3{};
  found_out->hitbox_bone = best_slot != nullptr ? *best_slot->bone_to_world : matrix_3x4{};
  found_out->record = nullptr;
  found_out->sim_time = ctx.target->get_simulation_time();
  found_out->distance = distance_3d(ctx.localplayer->get_origin(), ctx.target->get_origin());
  found_out->on_shot = false;
  found_out->hull_world_mins = ctx.hull_mins;
  found_out->hull_world_maxs = ctx.hull_maxs;
  found_out->hull_valid = ctx.hull_valid;
  found_out->inset = best.inset;

  found_out->pose_target_tick = time_to_ticks(found_out->sim_time);
  const backtrack_timing live_timing = backtrack::current_timing();
  if (std::isfinite(found_out->sim_time) && found_out->sim_time > 0.0f) {
    const float fake_interp = live_timing.valid ? live_timing.fake_interp : backtrack::interpolation_time();
    found_out->command_tick = time_to_ticks(found_out->sim_time + fake_interp);
  }
  if (backtrack::command_tick_for_current_pose(found_out->sim_time, &found_out->command_tick)) {
    found_out->pose_timing_valid = true;
  } else {
    found_out->timing_error = FLT_MAX;
  }

  return true;
}

inline int hitscan_aim_build_entity_points(Entity* entity, Vec3* points, int max_points);
inline hitscan_point hitscan_aim_make_entity_point(Player* localplayer,
  Weapon* weapon,
  Entity* target,
  const Vec3& view_angles,
  const Vec3& shoot_pos,
  int priority,
  const Vec3& position);

inline bool hitscan_aim_scan_hull_pose(const hitscan_scan_context& ctx,
  hitscan_found* found_out,
  aimbot_reject_debug* reject_accum) {
  if (found_out == nullptr || ctx.target == nullptr || ctx.localplayer == nullptr ||
      ctx.weapon == nullptr) {
    return false;
  }

  constexpr int max_points = 16;
  Vec3 points[max_points]{};
  int hitboxes[max_points]{};
  int point_count = 0;

  const Vec3 origin = ctx.target->get_collision_origin();
  const Vec3 mins = ctx.target->get_collideable_mins();
  const Vec3 maxs = ctx.target->get_collideable_maxs();
  if (!aimbot_vec3_is_finite(origin) || !aimbot_vec3_is_finite(mins) || !aimbot_vec3_is_finite(maxs)) {
    hitscan_aim_keep_reject(reject_accum,
      hitscan_aim_make_reject_debug(ctx.target, aimbot_reject_reason::no_point));
    return false;
  }

  const Vec3 world_mins = origin + mins;
  const Vec3 world_maxs = origin + maxs;
  const Vec3 center = (world_mins + world_maxs) * 0.5f;
  const Vec3 head{origin.x, origin.y, origin.z + maxs.z * 0.93f};
  const Vec3 pelvis{origin.x, origin.y, origin.z + (maxs.z + mins.z) * 0.5f};
  const bool head_only = hitscan_aim_head_only(ctx.hitbox_mask);

  const auto push = [&](const Vec3& position, int hitbox) {
    if (point_count >= max_points || !aimbot_vec3_is_finite(position)) {
      return;
    }
    if (hitbox >= 0 && !aimbot_hitbox_matches_mask(hitbox, ctx.hitbox_mask)) {
      return;
    }
    points[point_count] = position;
    hitboxes[point_count] = hitbox;
    ++point_count;
  };

  if (!ctx.body_forced && (ctx.hitbox_mask & aim_hitbox_mask_head) != 0) {
    push(head, aim_hitbox_head);
  }
  if (!head_only) {
    push(center, aim_hitbox_spine_2);
    push(pelvis, aim_hitbox_pelvis);
    Vec3 extra[9]{};
    const int extra_count = hitscan_aim_build_entity_points(ctx.target->to_entity(), extra, 9);
    for (int index = 0; index < extra_count; ++index) {
      push(extra[index], aim_hitbox_spine_2);
    }
  }

  hitscan_point best{};
  for (int index = 0; index < point_count; ++index) {
    hitscan_point point = hitscan_aim_make_entity_point(
      ctx.localplayer,
      ctx.weapon,
      ctx.target,
      ctx.view_angles,
      ctx.shoot_pos,
      hitboxes[index] == aim_hitbox_head ? 0 : 1,
      points[index]);
    if (!point.valid) {
      continue;
    }
    point.hitbox = hitboxes[index];
    point.fireable = hitscan_aim_trace_point(ctx.localplayer, ctx.target, point.position, ctx.shoot_pos) &&
      hitscan_aim_peek_visible(ctx, point.position);
    if (!point.fireable) {
      continue;
    }
    if (hitscan_aim_point_better(point, best, best.valid)) {
      best = point;
    }
  }

  if (!best.valid) {
    hitscan_aim_keep_reject(reject_accum,
      hitscan_aim_make_reject_debug(ctx.target, aimbot_reject_reason::no_point));
    return false;
  }

  found_out->valid = true;
  found_out->point = best;
  found_out->record = nullptr;
  found_out->sim_time = ctx.target->get_simulation_time();
  found_out->distance = distance_3d(ctx.localplayer->get_origin(), ctx.target->get_origin());
  found_out->hull_world_mins = world_mins;
  found_out->hull_world_maxs = world_maxs;
  found_out->hull_valid = hitscan_aim_hull_usable(world_mins, world_maxs);
  found_out->inset = best.inset;
  found_out->pose_target_tick = time_to_ticks(found_out->sim_time);
  if (std::isfinite(found_out->sim_time) && found_out->sim_time > 0.0f) {
    const backtrack_timing live_timing = backtrack::current_timing();
    const float fake_interp = live_timing.valid ? live_timing.fake_interp : backtrack::interpolation_time();
    found_out->command_tick = time_to_ticks(found_out->sim_time + fake_interp);
  }
  if (backtrack::command_tick_for_current_pose(found_out->sim_time, &found_out->command_tick)) {
    found_out->pose_timing_valid = true;
  }
  return true;
}


inline bool hitscan_aim_find_solution(Player* localplayer,
  Weapon* weapon,
  Player* target,
  const Vec3& view_angles,
  hitscan_found* found_out) {
  if (localplayer == nullptr || weapon == nullptr || target == nullptr || global_vars == nullptr ||
      found_out == nullptr) {
    return false;
  }

  hitscan_scan_context ctx{};
  ctx.localplayer = localplayer;
  ctx.target = target;
  ctx.weapon = weapon;
  ctx.view_angles = view_angles;
  ctx.last_input_angles = aimbot::current_state().last_input_angles;
  ctx.has_last_input_angles = aimbot::current_state().last_input_angles_valid;
  ctx.shoot_pos = hitscan_aim_eye_position(localplayer);
  if (!aimbot_vec3_is_finite(ctx.shoot_pos)) {
    return false;
  }
  hitscan_aim_apply_entity_hull(&ctx, target);

  ctx.peek_enabled = hitscan_aim_want_peek(weapon) &&
    hitscan_aim_peek_origin(localplayer, ctx.shoot_pos, &ctx.peek_pos);

  ctx.hitbox_mask = hitscan_aim_effective_hitbox_mask(weapon);
  const bool wait_for_headshot = hitscan_aim_waits_for_headshot(weapon);
  ctx.priority_hitbox = hitscan_aim_priority_hitbox(localplayer, weapon, target, ctx.hitbox_mask, wait_for_headshot);
  if (ctx.priority_hitbox < 0) {
    found_out->reject = hitscan_aim_make_reject_debug(target, aimbot_reject_reason::no_hitbox);
    return false;
  }

  const bool head_only_mask = hitscan_aim_head_only(ctx.hitbox_mask);
  ctx.body_forced = !head_only_mask && hitscan_aim_body_forced(localplayer, weapon, target);
  ctx.head_locked = weapon->is_headshot_weapon() &&
    (ctx.hitbox_mask & aim_hitbox_mask_head) != 0 &&
    !ctx.body_forced;

  const backtrack_timing timing = backtrack::current_timing();
  aimbot_reject_debug reject_accum{};
  hitscan_found records{};
  const bool have_records = hitscan_aim_scan_records(ctx, timing, &records, &reject_accum);
  hitscan_found live{};
  bool have_live = hitscan_aim_scan_live_pose(ctx, &live, &reject_accum);
  if (!have_live) {
    have_live = hitscan_aim_scan_hull_pose(ctx, &live, &reject_accum);
  }
  if (have_live && live.pose_timing_valid && timing.valid) {
    live.timing_error = std::fabs(
      timing.correct - ticks_to_time(timing.server_tick - time_to_ticks(live.sim_time)));
  }
  const bool live_fireable = have_live && live.point.fireable;
  if (have_records && records.point.fireable && live_fireable) {
    *found_out = hitscan_aim_found_better(live, records) ? live : records;
    return true;
  }
  if (have_records && records.point.fireable) {
    *found_out = records;
    return true;
  }
  if (live_fireable) {
    *found_out = live;
    return true;
  }

  found_out->reject = reject_accum.reason != aimbot_reject_reason::none
    ? reject_accum
    : hitscan_aim_make_reject_debug(target, aimbot_reject_reason::no_point);
  return false;
}

inline aimbot_candidate hitscan_aim_make_candidate(Player* localplayer,
  Player* player,
  const hitscan_found& found) {
  aimbot_candidate candidate{};
  if (!found.valid) {
    if (player != nullptr) {
      candidate.player = player;
    }
    candidate.reject_debug = found.reject.reason != aimbot_reject_reason::none
      ? found.reject
      : hitscan_aim_make_reject_debug(player, aimbot_reject_reason::no_point);
    return candidate;
  }

  const hitscan_point& point = found.point;
  candidate.entity = player;
  candidate.player = player;
  candidate.preferred = aimbot_player_is_preferred(player);
  candidate.bone = point.bone;
  candidate.hitbox = point.hitbox;
  candidate.studio_hitbox = point.studio_hitbox;
  candidate.aim_position = point.position;
  candidate.aim_angles = point.angles;
  candidate.fov = point.fov;
  candidate.distance = found.distance;
  candidate.health = player->get_health();
  candidate.simulation_time = found.sim_time;
  candidate.tick_count = found.command_tick;
  candidate.command_angles = hitscan_aim_command_angles(localplayer, point.angles);
  candidate.visible = true;
  candidate.backtrack_hitbox_mins = found.hitbox_local_mins;
  candidate.backtrack_hitbox_maxs = found.hitbox_local_maxs;
  candidate.backtrack_bone = found.hitbox_bone;
  candidate.backtrack_hitbox_valid = point.studio_hitbox >= 0;
  candidate.backtrack_mins = found.hull_world_mins;
  candidate.backtrack_maxs = found.hull_world_maxs;
  candidate.hull_valid = found.hull_valid;
  candidate.fire_inset = found.inset;

  if (found.record != nullptr) {
    candidate.backtrack = true;
    candidate.backtrack_on_shot = found.on_shot;
    candidate.backtrack_timing_error = found.timing_error;
    candidate.backtrack_capture_gap = found.capture_gap;
    candidate.pose_timing_valid = false;
    candidate.pose_command_tick = found.command_tick;
    candidate.pose_target_tick = found.pose_target_tick;
  } else {
    candidate.pose_timing_valid = found.pose_timing_valid;
    candidate.pose_target_tick = found.pose_target_tick;
    candidate.pose_command_tick = found.command_tick;
    candidate.backtrack = false;
  }

  return candidate;
}

inline aimbot_candidate hitscan_aim_find_candidate(Player* localplayer,
  Weapon* weapon,
  Player* player,
  const Vec3& view_angles) {
  if (localplayer == nullptr || weapon == nullptr || player == nullptr || !player->is_alive()) {
    return {};
  }

  hitscan_found found{};
  hitscan_aim_find_solution(localplayer, weapon, player, view_angles, &found);
  return hitscan_aim_make_candidate(localplayer, player, found);
}


inline int hitscan_aim_build_entity_points(Entity* entity, Vec3* points, int max_points) {
  int point_count = 0;
  if (entity == nullptr || points == nullptr || max_points <= 0) {
    return point_count;
  }

  const Vec3 origin = entity->get_collision_origin();
  const Vec3 mins = entity->get_collideable_mins() + origin;
  const Vec3 maxs = entity->get_collideable_maxs() + origin;
  if (!aimbot_vec3_is_finite(mins) || !aimbot_vec3_is_finite(maxs)) {
    return point_count;
  }

  const Vec3 center = (mins + maxs) * 0.5f;
  aimbot_add_local_hitbox_point(points, &point_count, max_points, center);

  const float scale = aimbot_effective_multipoint_scale();
  if (scale <= 0.0f) {
    return point_count;
  }

  const float subtract = aimbot_effective_bone_size_subtract();
  const Vec3 extent_raw = (maxs - mins) * 0.5f;
  const Vec3 extent{
    std::max(extent_raw.x - subtract, extent_raw.x * aimbot_effective_bone_size_min_scale()),
    std::max(extent_raw.y - subtract, extent_raw.y * aimbot_effective_bone_size_min_scale()),
    std::max(extent_raw.z - subtract, extent_raw.z * aimbot_effective_bone_size_min_scale())
  };
  const Vec3 scaled_extent = extent * scale;
  if (std::fabs(scaled_extent.x) <= 1.0f ||
      std::fabs(scaled_extent.y) <= 1.0f ||
      std::fabs(scaled_extent.z) <= 1.0f) {
    return point_count;
  }

  for (const float x_sign : { -1.0f, 1.0f }) {
    for (const float y_sign : { -1.0f, 1.0f }) {
      for (const float z_sign : { -1.0f, 1.0f }) {
        aimbot_add_local_hitbox_point(
          points,
          &point_count,
          max_points,
          center + Vec3{scaled_extent.x * x_sign, scaled_extent.y * y_sign, scaled_extent.z * z_sign});
      }
    }
  }

  return point_count;
}

inline hitscan_point hitscan_aim_make_entity_point(Player* localplayer,
  Weapon* weapon,
  Entity* target,
  const Vec3& view_angles,
  const Vec3& shoot_pos,
  int priority,
  const Vec3& position) {
  hitscan_point point{};
  if (localplayer == nullptr || weapon == nullptr || target == nullptr || !aimbot_vec3_is_finite(position)) {
    return point;
  }

  const float weapon_range = weapon->get_hitscan_range();
  const Vec3 to_point = position - shoot_pos;
  const float point_distance = std::sqrt(
    (to_point.x * to_point.x) +
    (to_point.y * to_point.y) +
    (to_point.z * to_point.z));
  if (!std::isfinite(point_distance) ||
      (weapon_range > 0.0f && point_distance > weapon_range)) {
    return point;
  }

  hitscan_trace_result trace{};
  if (!hitscan_aim_trace_point(localplayer, target, position, shoot_pos, &trace)) {
    return point;
  }

  if (!hitscan_aim_same_entity(trace.entity, target)) {
    return point;
  }

  if (!hitscan_aim_peek_clears_shot(localplayer, weapon, target, shoot_pos, position)) {
    return point;
  }

  point.valid = true;
  point.hitbox = -1;
  point.studio_hitbox = -1;
  point.priority = priority;
  point.position = position;
  point.angles = aimbot_calculate_angles_to_position(shoot_pos, position);
  point.fov = aimbot_calculate_fov(hitscan_aim_command_angles(localplayer, point.angles), view_angles);
  return point;
}

inline aimbot_candidate hitscan_aim_find_non_player_candidate(Player* localplayer,
  Weapon* weapon,
  Entity* entity,
  const Vec3& view_angles) {
  aimbot_candidate candidate{};
  if (localplayer == nullptr ||
      weapon == nullptr ||
      entity == nullptr ||
      aimbot_is_projectile_weapon(weapon) ||
      aimbot_is_melee_weapon(weapon) ||
      aimbot_should_skip_non_player_target(localplayer, entity)) {
    return candidate;
  }

  const Vec3 shoot_pos = hitscan_aim_eye_position(localplayer);
  constexpr int max_points = 9;
  Vec3 points[max_points]{};
  const int point_count = hitscan_aim_build_entity_points(entity, points, max_points);
  hitscan_point best{};
  for (int point_index = 0; point_index < point_count; ++point_index) {
    hitscan_point point = hitscan_aim_make_entity_point(
      localplayer,
      weapon,
      entity,
      view_angles,
      shoot_pos,
      point_index,
      points[point_index]);
    if (!point.valid) {
      continue;
    }

    if (!best.valid || point.priority < best.priority || (point.priority == best.priority && point.fov < best.fov)) {
      best = point;
    }

    if (best.valid && best.priority == 0) {
      break;
    }
  }

  if (!best.valid) {
    return candidate;
  }

  candidate.entity = entity;
  candidate.aim_position = best.position;
  candidate.aim_angles = best.angles;
  candidate.fov = best.fov;
  candidate.distance = distance_3d(localplayer->get_origin(), entity->get_origin());
  candidate.health = aimbot_entity_health(entity);
  candidate.command_angles = hitscan_aim_command_angles(localplayer, best.angles);
  candidate.visible = true;
  return candidate;
}


inline bool hitscan_aim_accepts_trace_hitbox(
  const aimbot_candidate& candidate,
  Weapon* weapon,
  int trace_hitbox) {
  if (candidate.player == nullptr || candidate.hitbox < 0) {
    return true;
  }

  if (trace_hitbox < 0) {
    return true;
  }

  const int base_hitbox = aimbot_studio_hitbox_to_base(candidate.player, trace_hitbox);
  if (base_hitbox < 0) {
    return false;
  }

  if (candidate.hitbox == aim_hitbox_head) {
    return base_hitbox == aim_hitbox_head;
  }

  if (base_hitbox == aim_hitbox_head) {
    return false;
  }

  return base_hitbox == candidate.hitbox ||
    aimbot_hitbox_matches_mask(base_hitbox, hitscan_aim_effective_hitbox_mask(weapon));
}

inline bool hitscan_aim_trace_geometry(const aimbot_candidate& candidate,
  const Vec3& start_pos,
  const Vec3& end_pos,
  float* fraction) {
  if (candidate.player == nullptr || !candidate.backtrack_hitbox_valid ||
      !aimbot_vec3_is_finite(start_pos) || !aimbot_vec3_is_finite(end_pos)) {
    return false;
  }

  Vec3 shrunk_mins{};
  Vec3 shrunk_maxs{};
  if (!aimbot_shrink_aabb(
        candidate.backtrack_hitbox_mins,
        candidate.backtrack_hitbox_maxs,
        &shrunk_mins,
        &shrunk_maxs)) {
    shrunk_mins = candidate.backtrack_hitbox_mins;
    shrunk_maxs = candidate.backtrack_hitbox_maxs;
  }
  return aimbot_ray_hits_obb(
    start_pos,
    end_pos,
    candidate.backtrack_bone,
    shrunk_mins,
    shrunk_maxs,
    fraction);
}

inline bool hitscan_aim_trace_candidate(Player* localplayer,
  Weapon* weapon,
  const aimbot_candidate& candidate,
  const Vec3& command_view_angles,
  const Vec3& spread_offset = {},
  bool use_spread = false,
  hitscan_trace_result* result = nullptr) {
  if (result != nullptr) {
    *result = {};
  }

  if (localplayer == nullptr ||
      weapon == nullptr ||
      candidate.entity == nullptr ||
      !aimbot_vec3_is_finite(candidate.aim_position)) {
    return false;
  }

  const Vec3 start_pos = hitscan_aim_eye_position(localplayer);
  const float weapon_range = weapon->get_hitscan_range();
  const Vec3 to_target = candidate.aim_position - start_pos;
  const float target_distance = std::sqrt(
    (to_target.x * to_target.x) +
    (to_target.y * to_target.y) +
    (to_target.z * to_target.z));
  if (!std::isfinite(target_distance) ||
      target_distance <= 0.001f ||
      (weapon_range > 0.0f && target_distance > weapon_range)) {
    return false;
  }

  const Vec3 bullet_angles = hitscan_aim_bullet_angles(localplayer, command_view_angles);
  Vec3 forward{};
  Vec3 right{};
  Vec3 up{};
  angle_vectors(bullet_angles, &forward, &right, &up);
  if (!aimbot_vec3_is_finite(forward)) {
    return false;
  }

  if (use_spread) {
    forward = aimbot_normalize_vector(forward + (right * spread_offset.x) + (up * spread_offset.y));
    if (!aimbot_vec3_is_finite(forward)) {
      return false;
    }
  }

  const float trace_length = weapon_range > 0.0f
    ? weapon_range
    : std::max(target_distance + 64.0f, 128.0f);
  const Vec3 end_pos = start_pos + (forward * trace_length);
  if (candidate.player != nullptr) {
    if (candidate.hull_valid &&
        !aimbot_segment_intersects_aabb(start_pos, end_pos, candidate.backtrack_mins, candidate.backtrack_maxs)) {
      return false;
    }
    float fraction = 1.0f;
    bool geometric = false;
    if (candidate.backtrack_hitbox_valid) {
      geometric = hitscan_aim_trace_geometry(candidate, start_pos, end_pos, &fraction);
    } else if (candidate.hull_valid) {
      geometric = aimbot_segment_aabb_enter_fraction(
        start_pos, end_pos, candidate.backtrack_mins, candidate.backtrack_maxs, &fraction);
    } else {
      geometric = hitscan_aim_ray_hits_entity_bounds(candidate.entity, start_pos, end_pos);
      if (geometric) {
        const Vec3 origin = candidate.entity->get_collision_origin();
        geometric = aimbot_segment_aabb_enter_fraction(
          start_pos,
          end_pos,
          candidate.entity->get_collideable_mins() + origin,
          candidate.entity->get_collideable_maxs() + origin,
          &fraction);
      }
    }
    if (!geometric) {
      return false;
    }
    const Vec3 surface = start_pos + (end_pos - start_pos) * std::clamp(fraction, 0.0f, 1.0f);
    if (candidate.hull_valid) {
      if (hitscan_aim_world_blocks_before(start_pos, surface, candidate.backtrack_mins, candidate.backtrack_maxs) ||
          hitscan_aim_world_blocks_before(start_pos, candidate.aim_position, candidate.backtrack_mins, candidate.backtrack_maxs)) {
        return false;
      }
    } else if (hitscan_aim_world_blocks_before(start_pos, surface, candidate.entity) ||
               hitscan_aim_world_blocks_before(start_pos, candidate.aim_position, candidate.entity)) {
      return false;
    }
    hitscan_trace_result trace{};
    if (engine_trace == nullptr) {
      return false;
    }
    trace = hitscan_aim_trace_line(localplayer, start_pos, end_pos, candidate.entity);
    const bool hit = !trace.start_solid && !trace.all_solid &&
      hitscan_aim_same_entity(trace.entity, candidate.entity);
    if (hit &&
        !hitscan_aim_peek_clears_shot(
          localplayer, weapon, candidate.entity, start_pos, candidate.aim_position)) {
      if (result != nullptr) {
        *result = trace;
      }
      return false;
    }
    if (result != nullptr) {
      *result = trace;
    }
    return hit;
  }

  if (hitscan_aim_world_blocks_before(start_pos, candidate.aim_position, candidate.entity)) {
    return false;
  }
  hitscan_trace_result trace = hitscan_aim_trace_line(localplayer, start_pos, end_pos, candidate.entity);
  const bool hit = !trace.start_solid && !trace.all_solid &&
    hitscan_aim_same_entity(trace.entity, candidate.entity) &&
    hitscan_aim_accepts_trace_hitbox(candidate, weapon, trace.hitbox);
  if (hit &&
      !hitscan_aim_peek_clears_shot(
        localplayer, weapon, candidate.entity, start_pos, candidate.aim_position)) {
    if (result != nullptr) {
      *result = trace;
    }
    return false;
  }
  if (result != nullptr) {
    *result = trace;
  }

  return hit;
}


inline bool hitscan_aim_candidate_matches_configured_hitbox(
  const aimbot_candidate& candidate,
  Player* localplayer,
  Weapon* weapon = nullptr) {
  if (candidate.player == nullptr || candidate.hitbox < 0) {
    return true;
  }

  const uint32_t hitbox_mask = hitscan_aim_effective_hitbox_mask(weapon);
  if (!aimbot_hitbox_matches_mask(candidate.hitbox, hitbox_mask)) {
    return false;
  }

  if (weapon != nullptr && weapon->is_headshot_weapon() &&
      (hitbox_mask & aim_hitbox_mask_head) != 0) {
    const bool body_forced = candidate.player != nullptr &&
      hitscan_aim_body_forced(localplayer, weapon, candidate.player);
    return body_forced
      ? candidate.hitbox != aim_hitbox_head
      : candidate.hitbox == aim_hitbox_head;
  }

  return !hitscan_aim_head_only(hitbox_mask) || candidate.hitbox == aim_hitbox_head;
}

inline bool hitscan_aim_head_only_fire_ready(Player* localplayer,
  Weapon* weapon,
  const aimbot_candidate& candidate) {
  if (candidate.player == nullptr || weapon == nullptr || candidate.hitbox < 0) {
    return true;
  }

  const uint32_t hitbox_mask = hitscan_aim_effective_hitbox_mask(weapon);
  if (!hitscan_aim_head_only(hitbox_mask)) {
    return true;
  }
  if (candidate.hitbox != aim_hitbox_head) {
    return false;
  }

  switch (weapon->get_weapon_id()) {
  case TF_WEAPON_SNIPERRIFLE:
  case TF_WEAPON_SNIPERRIFLE_DECAP:

    return localplayer != nullptr && aimbot_sniper_scope_confirmed(localplayer) &&
      aimbot_sniper_headshot_ready(localplayer, weapon);
  case TF_WEAPON_SNIPERRIFLE_CLASSIC:
    return localplayer != nullptr && aimbot_sniper_headshot_ready(localplayer, weapon);
  case TF_WEAPON_REVOLVER:
    return attribute_manager == nullptr ||
      attribute_manager->attrib_hook_value(0, "set_weapon_mode", weapon->to_entity()) != 1 ||
      weapon->can_ambassador_headshot();
  default:
    return true;
  }
}

inline bool hitscan_aim_headshot_ready(Player* localplayer, Weapon* weapon, const aimbot_candidate& candidate) {
  return aimbot_wait_for_headshot_ready(localplayer, weapon, candidate);
}

inline bool hitscan_aim_charge_ready(Player* localplayer, Weapon* weapon, const aimbot_candidate& candidate) {
  return aimbot_wait_for_charge_ready(localplayer, weapon, candidate);
}
#endif
