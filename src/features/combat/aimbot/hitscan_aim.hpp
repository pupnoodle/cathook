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
#include "games/tf2/sdk/interfaces/client_state.hpp"

using backtrack::backtrack_hitbox;
using backtrack::backtrack_history;
using backtrack::backtrack_record;

namespace hitscan {

struct settings {
  bool scan_records = true;
  int max_records_scanned = 12;
  bool perfect_window_gate = false;
  float perfect_window_multi_pellet_seconds = 0.25f;
  float perfect_window_single_shot_seconds = 1.25f;
};

inline settings settings{};

}

inline bool hitscan_dormant_target_viable(Player*) {
  return false;
}

struct hitscan_settings_view {
  uint32_t hitbox_mask = aim_hitbox_mask_default_hitscan;
  bool wait_for_headshot = false;
  bool wait_for_charge = false;
  bool body_aim_if_lethal = false;
  bool spread_compensation = false;
  float multipoint_scale = 0.0f;
};

struct hitscan_point {
  bool valid = false;
  int bone = 0;
  int hitbox = -1;
  int studio_hitbox = -1;
  int priority = 0;
  Vec3 position{};
  Vec3 angles{};
  float fov = FLT_MAX;
  bool pose_timing_valid = false;
  int pose_target_tick = 0;
  int pose_command_tick = 0;
  float pose_lead_seconds = 0.0f;
  Vec3 pose_offset{};
  aimbot_reject_debug reject_debug{};
};

struct hitscan_pose_timing {
  bool valid = false;
  int target_tick = 0;
  int command_tick = 0;
};

inline hitscan_pose_timing hitscan_aim_pose_timing(Player* target) {
  hitscan_pose_timing result{};
  if (target == nullptr) {
    return result;
  }

  int command_tick = 0;
  if (!backtrack::command_tick_for_current_pose(target->get_simulation_time(), &command_tick)) {
    return result;
  }

  result.valid = true;
  result.command_tick = command_tick;
  result.target_tick = command_tick - backtrack::current_timing().lerp_ticks;
  return result;
}

struct hitscan_hitbox_entry {
  int hitbox = -1;
  int studio_hitbox = -1;
  int priority = 0;
};

struct hitscan_trace_result {
  bool hit = false;
  bool clear = false;
  Entity* entity = nullptr;
  int hitbox = -1;
  int contents = 0;
  float fraction = 1.0f;
  Vec3 end{};
};

using hitscan_aim_trace_result = hitscan_trace_result;

constexpr int hitscan_aim_max_bones = aimbot_max_bones;

inline Vec3 hitscan_aim_eye_position(Player* localplayer) {
  return localplayer != nullptr
    ? localplayer->get_origin() + localplayer->get_view_offset()
    : Vec3{};
}

inline bool hitscan_aim_same_entity(Entity* left, Entity* right) {
  if (left == nullptr || right == nullptr) {
    return false;
  }
  if (left == right) {
    return true;
  }

  return left->get_index() == right->get_index();
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
  if (candidate_has_fov && candidate.fov != best.fov) {
    return candidate.fov < best.fov;
  }

  return false;
}

inline void hitscan_aim_keep_reject(hitscan_point* point, const aimbot_reject_debug& reject) {
  if (point != nullptr && hitscan_aim_reject_better(reject, point->reject_debug)) {
    point->reject_debug = reject;
  }
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

inline Vec3 hitscan_aim_bullet_angles(Player* localplayer, const Vec3& view_angles) {
  return localplayer != nullptr ? view_angles + localplayer->get_punch_angles() : view_angles;
}

inline Vec3 hitscan_aim_command_angles(Player* localplayer, const Vec3& bullet_angles) {
  return localplayer != nullptr ? bullet_angles - localplayer->get_punch_angles() : bullet_angles;
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

inline unsigned int hitscan_aim_trace_mask() {
  unsigned int trace_mask = MASK_SHOT | CONTENTS_GRATE;
  if (config.aimbot.shoot_through_glass) {
    trace_mask &= ~CONTENTS_WINDOW;
  }

  return trace_mask;
}

inline hitscan_settings_view hitscan_aim_settings(Weapon* weapon) {
  return {
    .hitbox_mask = hitscan_aim_effective_hitbox_mask(weapon),
    .wait_for_headshot = hitscan_aim_waits_for_headshot(weapon),
    .wait_for_charge = weapon != nullptr && weapon->is_sniper_rifle() &&
      aimbot_modifier_enabled(Aim::hitscan_mod_wait_for_charge),
    .body_aim_if_lethal = weapon != nullptr && weapon->is_headshot_weapon() &&
      aimbot_modifier_enabled(Aim::hitscan_mod_body_aim_if_lethal),
    .spread_compensation = config.aimbot.spread_compensation,
    .multipoint_scale = std::clamp(config.aimbot.multipoint_scale, 0.0f, 100.0f)
  };
}

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
  engine_trace->trace_ray(&ray, hitscan_aim_trace_mask(), &filter, &trace);

  result.entity = static_cast<Entity*>(trace.entity);
  result.hitbox = trace.hitbox;
  result.contents = trace.contents;
  result.fraction = trace.fraction;
  result.end = trace.endpos;
  result.clear = !trace.all_solid && !trace.start_solid && trace.fraction >= 0.999f;
  result.hit = result.entity != nullptr || result.clear;
  return result;
}

inline bool hitscan_aim_trace_point(Player* localplayer,
  Entity* target,
  const Vec3& point,
  hitscan_trace_result* result_out = nullptr) {
  if (localplayer == nullptr || target == nullptr || !aimbot_vec3_is_finite(point)) {
    return false;
  }

  const Vec3 start_pos = hitscan_aim_eye_position(localplayer);
  const Vec3 to_point = point - start_pos;
  const float distance = std::sqrt((to_point.x * to_point.x) + (to_point.y * to_point.y) + (to_point.z * to_point.z));
  if (distance <= 0.001f) {
    return false;
  }

  hitscan_trace_result result = hitscan_aim_trace_line(localplayer, start_pos, point, target);
  if (result_out != nullptr) {
    *result_out = result;
  }

  if (result.entity != nullptr) {
    return hitscan_aim_same_entity(result.entity, target);
  }

  if (target->get_class_id() == class_id::PLAYER) {
    return false;
  }

  return result.clear;
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
  const Vec3& end_pos,
  const Vec3& pose_offset = {}) {
  if (target == nullptr) {
    return false;
  }

  const Vec3 origin = target->get_collision_origin() + pose_offset;
  const Vec3 mins = target->get_collideable_mins() + origin - Vec3{2.0f, 2.0f, 2.0f};
  const Vec3 maxs = target->get_collideable_maxs() + origin + Vec3{2.0f, 2.0f, 2.0f};
  if (!aimbot_vec3_is_finite(mins) || !aimbot_vec3_is_finite(maxs)) {
    return false;
  }

  return aimbot_segment_intersects_aabb(start_pos, end_pos, mins, maxs);
}

inline bool hitscan_aim_body_forced(Player* localplayer, Weapon* weapon, Player* target) {
  if (localplayer == nullptr || weapon == nullptr || target == nullptr) {
    return false;
  }

  return weapon->is_headshot_weapon() &&
    aimbot_modifier_enabled(Aim::hitscan_mod_body_aim_if_lethal) &&
    aimbot_body_aim_lethal(localplayer, weapon, target);
}

inline bool hitscan_aim_head_only(uint32_t hitbox_mask) {
  return (hitbox_mask & aim_hitbox_mask_head) != 0 && (hitbox_mask & ~aim_hitbox_mask_head) == 0;
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
  const hitscan_settings_view& settings_view) {
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

  if (settings_view.hitbox_mask == aim_hitbox_mask_none) {
    return -1;
  }

  if (!hitscan_aim_head_only(settings_view.hitbox_mask) &&
      hitscan_aim_body_forced(localplayer, weapon, target)) {
    uint32_t body_mask = settings_view.hitbox_mask & ~aim_hitbox_mask_head;
    if (body_mask == 0) {
      body_mask = aim_hitbox_mask_body | aim_hitbox_mask_pelvis;
    }
    return hitscan_aim_first_hitbox_for_mask(body_mask, body_order);
  }

  if (weapon != nullptr && weapon->is_headshot_weapon() &&
      (settings_view.hitbox_mask & aim_hitbox_mask_head) != 0) {
    return aim_hitbox_head;
  }

  const bool head_ready = aimbot_headshot_ready_for_priority(localplayer, weapon);
  const bool wait_prefers_head = settings_view.wait_for_headshot &&
    (weapon == nullptr || !weapon->is_sniper_rifle() || aimbot_sniper_scope_active(localplayer));
  if (weapon != nullptr &&
      weapon->is_headshot_weapon() &&
      (wait_prefers_head || head_ready) &&
      (settings_view.hitbox_mask & aim_hitbox_mask_head) != 0) {
    return aim_hitbox_head;
  }

  return hitscan_aim_first_hitbox_for_mask(settings_view.hitbox_mask, body_order);
}

inline bool hitscan_aim_perfect_window_ready(Weapon* weapon) {
  if (!hitscan::settings.perfect_window_gate || weapon == nullptr || global_vars == nullptr) {
    return true;
  }

  const float last_attack = weapon->get_last_attack();
  const float elapsed = global_vars->curtime - last_attack;
  if (!std::isfinite(last_attack) || !std::isfinite(elapsed) || elapsed < 0.0f) {
    return true;
  }

  const float ready_time = weapon->get_bullets_per_shot() > 1
    ? hitscan::settings.perfect_window_multi_pellet_seconds
    : hitscan::settings.perfect_window_single_shot_seconds;
  return elapsed > ready_time;
}

struct hitscan_found {
  bool valid = false;
  hitscan_point point{};
  aimbot_reject_debug reject{};
  bool from_record = false;
  const backtrack_record* record = nullptr;
  int command_tick = 0;
  float sim_time = 0.0f;
  float distance = FLT_MAX;
  bool on_shot = false;
  Vec3 hull_world_mins{};
  Vec3 hull_world_maxs{};
  Vec3 hitbox_local_mins{};
  Vec3 hitbox_local_maxs{};
  matrix_3x4 hitbox_bone{};
  bool hitbox_bone_valid = false;
  bool pose_timing_valid = false;
  int pose_target_tick = 0;
  int pose_command_tick = 0;
};

struct hitscan_scan_state {
  Player* localplayer = nullptr;
  Player* target = nullptr;
  Weapon* weapon = nullptr;
  Vec3 view_angles{};
  Vec3 shoot_pos{};
  uint32_t hitbox_mask = 0;
  int priority_hitbox = -1;
  bool head_locked = false;
  bool body_forced = false;
};

inline int hitscan_aim_collect_usable_chain(const hitscan_scan_state& state,
  std::array<const backtrack_record*, backtrack::max_records>* chain_out) {
  if (chain_out == nullptr || global_vars == nullptr || state.target == nullptr) {
    return 0;
  }

  const backtrack_history* history = backtrack::records_for_player(state.target);
  if (history == nullptr || history->record_count <= 0) {
    return 0;
  }

  int count = 0;
  for (int index = 0; index < history->record_count && count < backtrack::max_records; ++index) {
    const backtrack_record& record = history->records[index];
    if (!record.valid || record.invalid || record.teleport) {
      continue;
    }
    if (record.dormant && !hitscan_dormant_target_viable(state.target)) {
      continue;
    }
    if (!backtrack::command_tick_for_record(record, state.target, nullptr)) {
      continue;
    }
    (*chain_out)[static_cast<std::size_t>(count)] = &record;
    ++count;
  }
  return count;
}

inline hitscan_point hitscan_aim_evaluate_point(const hitscan_scan_state& state,
  int base_hitbox,
  int studio_hitbox_id,
  int bone,
  int priority,
  const Vec3& position,
  const matrix_3x4& bone_matrix,
  const Vec3& box_mins,
  const Vec3& box_maxs) {
  hitscan_point point{};
  if (!aimbot_vec3_is_finite(position)) {
    point.reject_debug = hitscan_aim_make_reject_debug(state.target, aimbot_reject_reason::invalid);
    return point;
  }

  const float weapon_range = state.weapon->get_hitscan_range();
  const Vec3 to_point = position - state.shoot_pos;
  const float point_distance = std::sqrt((to_point.x * to_point.x) + (to_point.y * to_point.y) + (to_point.z * to_point.z));
  if (!std::isfinite(point_distance) ||
      (weapon_range > 0.0f && point_distance > weapon_range)) {
    point.reject_debug = hitscan_aim_make_reject_debug(
      state.target,
      aimbot_reject_reason::no_point,
      FLT_MAX,
      point_distance,
      base_hitbox);
    return point;
  }

  const Vec3 aim_angles = aimbot_calculate_angles_to_position(state.shoot_pos, position);
  const float fov = aimbot_calculate_fov(hitscan_aim_command_angles(state.localplayer, aim_angles), state.view_angles);

  const Vec3 local_start = aimbot_inverse_transform_point(state.shoot_pos, bone_matrix);
  const Vec3 local_end = aimbot_inverse_transform_point(position, bone_matrix);
  float fraction = 0.0f;
  if (!aimbot_segment_aabb_enter_fraction(local_start, local_end, box_mins, box_maxs, &fraction)) {
    point.reject_debug = hitscan_aim_make_reject_debug(state.target, aimbot_reject_reason::no_point);
    return point;
  }
  const Vec3 impact = state.shoot_pos + to_point * fraction;
  const hitscan_trace_result trace = hitscan_aim_trace_line(state.localplayer, state.shoot_pos, impact, state.target, true);
  if (!trace.clear) {
    point.reject_debug = hitscan_aim_make_reject_debug(state.target,
      aimbot_reject_reason::trace_blocked, fov, point_distance, base_hitbox,
      trace.entity != nullptr ? trace.entity->get_index() : -1, trace.hitbox);
    hitscan_aim_set_trace_debug(&point.reject_debug, state.shoot_pos, impact, trace);
    return point;
  }

  point.valid = true;
  point.bone = bone;
  point.hitbox = base_hitbox;
  point.studio_hitbox = studio_hitbox_id;
  point.priority = priority;
  point.position = position;
  point.angles = aim_angles;
  point.fov = fov;
  return point;
}

inline bool hitscan_aim_scan_record(const hitscan_scan_state& state,
  const backtrack_record& record,
  int command_tick,
  hitscan_found* found_out,
  hitscan_point* reject_accum) {
  if (found_out == nullptr || record.hitbox_count <= 0 || record.bone_count <= 0) {
    return false;
  }

  constexpr int max_entries = backtrack::max_hitboxes;
  std::array<int, max_entries> slots{};
  std::array<int, max_entries> priorities{};
  int entry_count = 0;
  for (int index = 0; index < record.hitbox_count && entry_count < max_entries; ++index) {
    const backtrack_hitbox& hitbox = record.hitboxes[index];
    if (!hitbox.valid || hitbox.bone < 0 || hitbox.bone >= record.bone_count) {
      continue;
    }
    if (!aimbot_hitbox_matches_mask(hitbox.hitbox, state.hitbox_mask)) {
      continue;
    }
    if (state.head_locked && hitbox.hitbox != aim_hitbox_head) {
      continue;
    }
    if (state.body_forced && hitbox.hitbox == aim_hitbox_head) {
      continue;
    }

    const int raw_priority = aimbot_hitbox_priority(state.localplayer, state.target, state.weapon, hitbox.hitbox);
    if (raw_priority == INT_MAX) {
      continue;
    }

    slots[static_cast<std::size_t>(entry_count)] = index;
    priorities[static_cast<std::size_t>(entry_count)] =
      hitbox.hitbox == state.priority_hitbox ? 0 : raw_priority + 1;
    ++entry_count;
  }

  for (int outer = 1; outer < entry_count; ++outer) {
    const int slot_key = slots[static_cast<std::size_t>(outer)];
    const int priority_key = priorities[static_cast<std::size_t>(outer)];
    int inner = outer - 1;
    while (inner >= 0 && priorities[static_cast<std::size_t>(inner)] > priority_key) {
      slots[static_cast<std::size_t>(inner) + 1] = slots[static_cast<std::size_t>(inner)];
      priorities[static_cast<std::size_t>(inner) + 1] = priorities[static_cast<std::size_t>(inner)];
      --inner;
    }
    slots[static_cast<std::size_t>(inner) + 1] = slot_key;
    priorities[static_cast<std::size_t>(inner) + 1] = priority_key;
  }

  constexpr int max_local_points = 21;
  Vec3 local_points[max_local_points]{};
  for (int entry_index = 0; entry_index < entry_count; ++entry_index) {
    const backtrack_hitbox& hitbox =
      record.hitboxes[static_cast<std::size_t>(slots[static_cast<std::size_t>(entry_index)])];
    const matrix_3x4& bone_to_world = record.bones[static_cast<std::size_t>(hitbox.bone)];

    studio_box box{};
    box.bone = hitbox.bone;
    box.group = hitbox.group;
    box.bbmin = hitbox.mins;
    box.bbmax = hitbox.maxs;

    const bool use_multipoint = hitbox.hitbox == state.priority_hitbox &&
      (hitbox.hitbox == aim_hitbox_head || config.aimbot.multipoint_scale > 0.0f);
    const int point_count = aimbot_build_local_hitbox_points(
      box,
      bone_to_world,
      state.shoot_pos,
      local_points,
      max_local_points,
      use_multipoint,
      hitbox.hitbox);

    for (int point_index = 0; point_index < point_count; ++point_index) {
      Vec3 position = aimbot_transform_point(local_points[point_index], bone_to_world);
      hitscan_point point = hitscan_aim_evaluate_point(
        state,
        hitbox.hitbox,
        hitbox.studio_hitbox,
        hitbox.bone,
        priorities[static_cast<std::size_t>(entry_index)],
        position,
        bone_to_world,
        hitbox.mins,
        hitbox.maxs);
      if (!point.valid) {
        hitscan_aim_keep_reject(reject_accum, point.reject_debug);
        continue;
      }

      found_out->valid = true;
      found_out->point = point;
      found_out->from_record = true;
      found_out->record = &record;
      found_out->command_tick = command_tick;
      found_out->sim_time = record.sim_time;
      found_out->distance = distance_3d(state.localplayer->get_origin(), record.origin);
      found_out->on_shot = record.on_shot;
      found_out->hull_world_mins = record.origin + record.mins;
      found_out->hull_world_maxs = record.origin + record.maxs;
      found_out->hitbox_local_mins = hitbox.mins;
      found_out->hitbox_local_maxs = hitbox.maxs;
      found_out->hitbox_bone = bone_to_world;
      found_out->hitbox_bone_valid = true;
      return true;
    }
  }


  return false;
}

inline bool hitscan_aim_scan_records(const hitscan_scan_state& state,
  hitscan_found* found_out,
  hitscan_point* reject_accum) {
  if (!hitscan::settings.scan_records) {
    return false;
  }

  std::array<const backtrack_record*, backtrack::max_records> chain{};
  const int chain_count = hitscan_aim_collect_usable_chain(state, &chain);
  if (chain_count <= 0) {
    return false;
  }

  const bool prefer_on_shot = config.backtrack.prefer_on_shot;
  const int pass_count = prefer_on_shot ? 2 : 1;
  int scanned = 0;
  for (int pass = 0; pass < pass_count; ++pass) {
    for (int index = 0; index < chain_count; ++index) {
      const backtrack_record* record = chain[static_cast<std::size_t>(index)];
      if (prefer_on_shot && ((pass == 0) != record->on_shot)) {
        continue;
      }
      if (scanned >= hitscan::settings.max_records_scanned) {
        return false;
      }
      ++scanned;

      int command_tick = 0;
      if (!backtrack::command_tick_for_record(*record, state.target, &command_tick)) {
        continue;
      }
      if (hitscan_aim_scan_record(state, *record, command_tick, found_out, reject_accum)) {
        return true;
      }
    }
    if (!prefer_on_shot) {
      break;
    }
  }

  return false;
}

inline int hitscan_aim_build_studio_hitbox_entries(Player* localplayer,
  Weapon* weapon,
  Player* target,
  studio_hitbox_set* hitbox_set,
  const hitscan_settings_view& settings_view,
  int priority_hitbox,
  hitscan_hitbox_entry* entries,
  int max_entries) {
  if (target == nullptr || hitbox_set == nullptr || entries == nullptr || max_entries <= 0) {
    return 0;
  }

  int count = 0;
  for (int studio_hitbox_id = 0; studio_hitbox_id < hitbox_set->num_hitboxes && count < max_entries; ++studio_hitbox_id) {
    studio_box* hitbox = hitbox_set->hitbox(studio_hitbox_id);
    if (hitbox == nullptr || hitbox->bone < 0 || hitbox->bone >= 128) {
      continue;
    }

    const int base_hitbox = aimbot_studio_hitbox_to_base(target, studio_hitbox_id);
    if (base_hitbox < 0 || !aimbot_hitbox_matches_mask(base_hitbox, settings_view.hitbox_mask)) {
      continue;
    }
    entries[count++] = {
      .hitbox = base_hitbox,
      .studio_hitbox = studio_hitbox_id,
      .priority = base_hitbox == priority_hitbox
        ? 0
        : aimbot_hitbox_priority(localplayer, target, weapon, base_hitbox) + 1
    };
  }

  std::sort(entries, entries + count, [](const hitscan_hitbox_entry& left, const hitscan_hitbox_entry& right) {
    if (left.priority != right.priority) {
      return left.priority < right.priority;
    }
    return left.studio_hitbox < right.studio_hitbox;
  });

  return count;
}

inline bool hitscan_aim_scan_live_pose(const hitscan_scan_state& state,
  hitscan_found* found_out,
  hitscan_point* reject_accum) {
  if (model_info == nullptr) {
    hitscan_aim_keep_reject(reject_accum,
      hitscan_aim_make_reject_debug(state.target, aimbot_reject_reason::invalid));
    return false;
  }

  const model_t* model = state.target->get_model();
  if (model == nullptr) {
    hitscan_aim_keep_reject(reject_accum,
      hitscan_aim_make_reject_debug(state.target, aimbot_reject_reason::no_model));
    return false;
  }

  studio_hdr* hdr = model_info->get_studio_model(model);
  if (hdr == nullptr) {
    hitscan_aim_keep_reject(reject_accum,
      hitscan_aim_make_reject_debug(state.target, aimbot_reject_reason::no_studio_model));
    return false;
  }

  studio_hitbox_set* hitbox_set = hdr->hitbox_set(state.target->get_hitbox_set());
  if (hitbox_set == nullptr) {
    hitscan_aim_keep_reject(reject_accum,
      hitscan_aim_make_reject_debug(state.target, aimbot_reject_reason::no_hitbox_set));
    return false;
  }

  matrix_3x4 bone_to_world[hitscan_aim_max_bones]{};
  int bone_count = 0;
  if (!hitscan_aim_get_bones(state.target, bone_to_world, &bone_count, state.localplayer)) {
    const aimbot_reject_reason failure = aimbot_last_bone_failure();
    hitscan_aim_keep_reject(reject_accum,
      hitscan_aim_make_reject_debug(
        state.target,
        failure == aimbot_reject_reason::none ? aimbot_reject_reason::bone_cache : failure));
    return false;
  }

  const hitscan_settings_view settings_view = hitscan_aim_settings(state.weapon);
  const int priority_hitbox =
    hitscan_aim_priority_hitbox(state.localplayer, state.weapon, state.target, settings_view);
  if (priority_hitbox < 0) {
    hitscan_aim_keep_reject(reject_accum,
      hitscan_aim_make_reject_debug(state.target, aimbot_reject_reason::no_hitbox));
    return false;
  }

  constexpr int max_entries = 32;
  hitscan_hitbox_entry entries[max_entries]{};
  const int entry_count = hitscan_aim_build_studio_hitbox_entries(
    state.localplayer,
    state.weapon,
    state.target,
    hitbox_set,
    settings_view,
    priority_hitbox,
    entries,
    max_entries);
  if (entry_count <= 0) {
    hitscan_aim_keep_reject(reject_accum,
      hitscan_aim_make_reject_debug(state.target, aimbot_reject_reason::no_hitbox));
    return false;
  }

  hitscan_point best{};
  bool have_best = false;
  for (int entry_index = 0; entry_index < entry_count; ++entry_index) {
    const hitscan_hitbox_entry& entry = entries[entry_index];
    if (state.head_locked && entry.hitbox != aim_hitbox_head) {
      continue;
    }
    if (state.body_forced && entry.hitbox == aim_hitbox_head) {
      continue;
    }
    studio_box* hitbox = hitbox_set->hitbox(entry.studio_hitbox);
    if (hitbox == nullptr || hitbox->bone < 0 || hitbox->bone >= bone_count) {
      continue;
    }

    constexpr int max_local_points = 21;
    Vec3 local_points[max_local_points]{};
    const bool use_multipoint = entry.hitbox == priority_hitbox &&
      (entry.hitbox == aim_hitbox_head || config.aimbot.multipoint_scale > 0.0f);
    const int point_count = aimbot_build_local_hitbox_points(
      *hitbox,
      bone_to_world[hitbox->bone],
      state.shoot_pos,
      local_points,
      max_local_points,
      use_multipoint,
      entry.hitbox);

    for (int point_index = 0; point_index < point_count; ++point_index) {
      const Vec3 position = aimbot_transform_point(local_points[point_index], bone_to_world[hitbox->bone]);
      hitscan_point point = hitscan_aim_evaluate_point(
        state,
        entry.hitbox,
        entry.studio_hitbox,
        hitbox->bone,
        entry.priority,
        position,
        bone_to_world[hitbox->bone],
        hitbox->bbmin,
        hitbox->bbmax);
      if (!point.valid) {
        hitscan_aim_keep_reject(reject_accum, point.reject_debug);
        continue;
      }

      if (!have_best || point.priority < best.priority ||
          (point.priority == best.priority && point.fov < best.fov)) {
        best = point;
        have_best = true;
      }
    }

    if (have_best && best.priority == 0) {
      break;
    }
  }

  if (!have_best) {
    return false;
  }

  const hitscan_pose_timing pose_timing = hitscan_aim_pose_timing(state.target);

  found_out->valid = true;
  found_out->point = best;
  found_out->from_record = false;
  found_out->record = nullptr;
  found_out->command_tick = pose_timing.command_tick;
  found_out->sim_time = state.target->get_simulation_time();
  found_out->distance = distance_3d(state.localplayer->get_origin(), state.target->get_origin());
  found_out->on_shot = false;
  found_out->pose_timing_valid = pose_timing.valid;
  found_out->pose_target_tick = pose_timing.target_tick;
  found_out->pose_command_tick = pose_timing.command_tick;
  studio_box* selected_hitbox = hitbox_set->hitbox(best.studio_hitbox);
  found_out->hitbox_local_mins = selected_hitbox->bbmin;
  found_out->hitbox_local_maxs = selected_hitbox->bbmax;
  found_out->hitbox_bone = bone_to_world[selected_hitbox->bone];
  found_out->hitbox_bone_valid = true;
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

  hitscan_scan_state state{};
  state.localplayer = localplayer;
  state.target = target;
  state.weapon = weapon;
  state.view_angles = view_angles;
  state.shoot_pos = hitscan_aim_eye_position(localplayer);
  if (!aimbot_vec3_is_finite(state.shoot_pos)) {
    return false;
  }

  const hitscan_settings_view settings_view = hitscan_aim_settings(weapon);
  state.hitbox_mask = settings_view.hitbox_mask;
  state.priority_hitbox = hitscan_aim_priority_hitbox(localplayer, weapon, target, settings_view);
  if (state.priority_hitbox < 0) {
    found_out->reject = hitscan_aim_make_reject_debug(target, aimbot_reject_reason::no_hitbox);
    return false;
  }

  const bool head_only_mask = hitscan_aim_head_only(settings_view.hitbox_mask);
  state.body_forced = !head_only_mask && hitscan_aim_body_forced(localplayer, weapon, target);
  state.head_locked = weapon->is_headshot_weapon() &&
    (settings_view.hitbox_mask & aim_hitbox_mask_head) != 0 &&
    !state.body_forced;

  hitscan_point reject_accum{};
  if (hitscan_aim_scan_records(state, found_out, &reject_accum)) {
    return true;
  }
  if (hitscan_aim_scan_live_pose(state, found_out, &reject_accum)) {
    return true;
  }

  found_out->reject = reject_accum.reject_debug.reason != aimbot_reject_reason::none
    ? reject_accum.reject_debug
    : hitscan_aim_make_reject_debug(target, aimbot_reject_reason::no_point);
  return false;
}

inline aimbot_candidate hitscan_aim_make_candidate(Player* localplayer,
  Weapon*,
  Player* player,
  const hitscan_found& found,
  const Vec3& view_angles) {
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
  if (found.hitbox_bone_valid) {
    candidate.backtrack_hitbox_mins = found.hitbox_local_mins;
    candidate.backtrack_hitbox_maxs = found.hitbox_local_maxs;
    candidate.backtrack_bone = found.hitbox_bone;
    candidate.backtrack_hitbox_valid = true;
  }

  if (found.from_record) {
    candidate.backtrack = true;
    candidate.backtrack_on_shot = found.on_shot;
    candidate.backtrack_mins = found.hull_world_mins;
    candidate.backtrack_maxs = found.hull_world_maxs;
    candidate.pose_timing_valid = false;
    candidate.pose_command_tick = found.command_tick;
    candidate.pose_target_tick = found.command_tick - backtrack::current_timing().lerp_ticks;
  } else {
    candidate.pose_timing_valid = found.pose_timing_valid;
    candidate.pose_target_tick = found.pose_target_tick;
    candidate.pose_command_tick = found.pose_command_tick;
    candidate.pose_lead_seconds = point.pose_lead_seconds;
    candidate.pose_offset = point.pose_offset;
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
  return hitscan_aim_make_candidate(localplayer, weapon, player, found, view_angles);
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
  int priority,
  const Vec3& position) {
  hitscan_point point{};
  if (localplayer == nullptr || weapon == nullptr || target == nullptr || !aimbot_vec3_is_finite(position)) {
    return point;
  }

  const float weapon_range = weapon->get_hitscan_range();
  const Vec3 to_point = position - hitscan_aim_eye_position(localplayer);
  const float point_distance = std::sqrt(
    (to_point.x * to_point.x) +
    (to_point.y * to_point.y) +
    (to_point.z * to_point.z));
  if (!std::isfinite(point_distance) ||
      (weapon_range > 0.0f && point_distance > weapon_range)) {
    return point;
  }

  hitscan_trace_result trace{};
  if (!hitscan_aim_trace_point(localplayer, target, position, &trace)) {
    return point;
  }

  const Vec3 start_pos = hitscan_aim_eye_position(localplayer);
  if ((trace.entity != nullptr && !hitscan_aim_same_entity(trace.entity, target)) ||
      (trace.entity == nullptr && !hitscan_aim_ray_hits_entity_bounds(target, start_pos, position))) {
    return point;
  }

  point.valid = true;
  point.hitbox = -1;
  point.studio_hitbox = -1;
  point.priority = priority;
  point.position = position;
  point.angles = aimbot_calculate_angles_to_position(start_pos, position);
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

inline aimbot_candidate hitscan_aim_find_occluded_candidate(Player* localplayer,
  Weapon* weapon,
  Player* player,
  const Vec3& view_angles) {
  return hitscan_aim_find_candidate(localplayer, weapon, player, view_angles);
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

  const Vec3 local_start = aimbot_inverse_transform_point(start_pos, candidate.backtrack_bone);
  const Vec3 local_end = aimbot_inverse_transform_point(end_pos, candidate.backtrack_bone);
  return aimbot_segment_aabb_enter_fraction(local_start, local_end,
    candidate.backtrack_hitbox_mins, candidate.backtrack_hitbox_maxs, fraction);
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
      engine_trace == nullptr ||
      !aimbot_vec3_is_finite(candidate.aim_position)) {
    return false;
  }

  Vec3 start_pos = hitscan_aim_eye_position(localplayer);
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
  Vec3 end_pos = start_pos + (forward * trace_length);
  if (candidate.player != nullptr) {
    float fraction = 0.0f;
    if (!hitscan_aim_trace_geometry(candidate, start_pos, end_pos, &fraction)) {
      return false;
    }
    const Vec3 target_end = start_pos + (end_pos - start_pos) * fraction;
    hitscan_trace_result trace = hitscan_aim_trace_line(localplayer, start_pos, target_end, candidate.entity, true);
    const bool hit = trace.clear;
    if (hit) {
      trace.hit = true;
      trace.entity = candidate.entity;
      trace.hitbox = candidate.studio_hitbox;
    }
    if (result != nullptr) {
      *result = trace;
    }
    return hit;
  }

  hitscan_trace_result trace = hitscan_aim_trace_line(localplayer, start_pos, end_pos, candidate.entity);
  if (result != nullptr) {
    *result = trace;
  }

  const bool trace_hit_candidate = hitscan_aim_same_entity(trace.entity, candidate.entity);
  if (trace_hit_candidate && hitscan_aim_accepts_trace_hitbox(candidate, weapon, trace.hitbox)) {
    if (result != nullptr) {
      result->hit = true;
    }
    return true;
  }

  if (trace.entity != nullptr && !hitscan_aim_same_entity(trace.entity, candidate.entity)) {
    return false;
  }


  return false;
}

inline bool hitscan_aim_headshot_ready(Player* localplayer, Weapon* weapon, const aimbot_candidate& candidate) {
  return aimbot_wait_for_headshot_ready(localplayer, weapon, candidate);
}

inline bool hitscan_aim_charge_ready(Player* localplayer, Weapon* weapon, const aimbot_candidate& candidate) {
  if (!aimbot_wait_for_charge_ready(localplayer, weapon, candidate)) {
    return false;
  }
  return hitscan_aim_perfect_window_ready(weapon);
}
#endif
