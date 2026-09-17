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
using backtrack::backtrack_record;
using backtrack::backtrack_record_view;
using backtrack::backtrack_timing;

namespace hitscan {

constexpr int max_records_scanned = 12;
constexpr int max_hitbox_points = 21;
constexpr int max_hitbox_slots = 32;

}


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

inline Vec3 hitscan_aim_bullet_angles(Player* localplayer, const Vec3& view_angles) {
  return localplayer != nullptr ? view_angles + localplayer->get_punch_angles() : view_angles;
}

inline Vec3 hitscan_aim_command_angles(Player* localplayer, const Vec3& bullet_angles) {
  return localplayer != nullptr ? bullet_angles - localplayer->get_punch_angles() : bullet_angles;
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

  hitscan_trace_result result = hitscan_aim_trace_line(localplayer, shoot_pos, point, target);
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
  Vec3 shoot_pos{};
  uint32_t hitbox_mask = 0;
  int priority_hitbox = -1;
  bool head_locked = false;
  bool body_forced = false;
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

inline hitscan_point hitscan_aim_evaluate_point(const hitscan_scan_context& ctx,
  const hitscan_hitbox_slot& slot,
  const Vec3& position) {
  hitscan_point point{};
  if (!aimbot_vec3_is_finite(position)) {
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

  const Vec3 local_start = aimbot_inverse_transform_point(ctx.shoot_pos, *slot.bone_to_world);
  const Vec3 local_end = aimbot_inverse_transform_point(position, *slot.bone_to_world);
  float fraction = 0.0f;
  if (!aimbot_segment_aabb_enter_fraction(local_start, local_end, slot.mins, slot.maxs, &fraction)) {
    point.reject_debug = hitscan_aim_make_reject_debug(ctx.target, aimbot_reject_reason::no_point);
    return point;
  }
  const Vec3 impact = ctx.shoot_pos + to_point * fraction;
  const hitscan_trace_result trace = hitscan_aim_trace_line(ctx.localplayer, ctx.shoot_pos, impact, ctx.target, true);
  if (!trace.clear) {
    point.reject_debug = hitscan_aim_make_reject_debug(ctx.target,
      aimbot_reject_reason::trace_blocked, fov, point_distance, slot.hitbox,
      trace.entity != nullptr ? trace.entity->get_index() : -1, trace.hitbox);
    hitscan_aim_set_trace_debug(&point.reject_debug, ctx.shoot_pos, impact, trace);
    return point;
  }

  point.valid = true;
  point.bone = slot.bone;
  point.hitbox = slot.hitbox;
  point.studio_hitbox = slot.studio_hitbox;
  point.priority = slot.priority;
  point.position = position;
  point.angles = aim_angles;
  point.fov = fov;
  return point;
}

inline bool hitscan_aim_scan_slots(const hitscan_scan_context& ctx,
  const std::array<hitscan_hitbox_slot, hitscan::max_hitbox_slots>& slots,
  int slot_count,
  int pass_mode,
  bool first_valid,
  hitscan_point* point_out,
  const hitscan_hitbox_slot** slot_out,
  aimbot_reject_debug* reject_accum) {
  if (point_out == nullptr || slot_count <= 0) {
    return false;
  }

  hitscan_point best{};
  const hitscan_hitbox_slot* best_slot = nullptr;
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

      if (first_valid) {
        *point_out = point;
        if (slot_out != nullptr) {
          *slot_out = &slot;
        }
        return true;
      }

      if (best_slot == nullptr || point.priority < best.priority ||
          (point.priority == best.priority && point.fov < best.fov)) {
        best = point;
        best_slot = &slot;
      }
    }

    if (best_slot != nullptr && best.priority == 0) {
      break;
    }
  }

  if (best_slot == nullptr) {
    return false;
  }

  *point_out = best;
  if (slot_out != nullptr) {
    *slot_out = best_slot;
  }
  return true;
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
  aimbot_reject_debug reject{};
};

inline bool hitscan_aim_scan_records(const hitscan_scan_context& ctx,
  const backtrack_timing& timing,
  hitscan_found* found_out,
  aimbot_reject_debug* reject_accum) {
  if (found_out == nullptr) {
    return false;
  }

  const backtrack_record_view view = backtrack::valid_records(ctx.target);
  if (view.count <= 0) {
    return false;
  }

  for (int pass = 0; pass < 2; ++pass) {
    int scanned = 0;
    for (int index = 0; index < view.count && scanned < hitscan::max_records_scanned; ++index) {
      const backtrack_record* record = view.records[static_cast<std::size_t>(index)];
      if (record == nullptr) {
        continue;
      }
      ++scanned;

      std::array<hitscan_hitbox_slot, hitscan::max_hitbox_slots> slots{};
      const int slot_count = hitscan_aim_build_record_slots(ctx, *record, &slots);
      if (slot_count <= 0) {
        continue;
      }

      hitscan_point point{};
      const hitscan_hitbox_slot* slot = nullptr;
      if (!hitscan_aim_scan_slots(ctx, slots, slot_count, pass, true, &point, &slot, reject_accum)) {
        continue;
      }

      int command_tick = 0;
      if (!backtrack::command_tick_for_record(*record, ctx.target, &command_tick)) {
        continue;
      }

      found_out->valid = true;
      found_out->point = point;
      found_out->record = record;
      found_out->command_tick = command_tick;
      found_out->sim_time = record->sim_time;
      found_out->distance = distance_3d(ctx.localplayer->get_origin(), record->origin);
      found_out->on_shot = record->on_shot;
      found_out->timing_error = backtrack::record_timing_score(timing, *record);
      found_out->capture_gap = backtrack::record_capture_gap(*record);
      found_out->hull_world_mins = record->origin + record->mins;
      found_out->hull_world_maxs = record->origin + record->maxs;
      found_out->hitbox_local_mins = slot != nullptr ? slot->mins : Vec3{};
      found_out->hitbox_local_maxs = slot != nullptr ? slot->maxs : Vec3{};
      found_out->hitbox_bone = slot != nullptr ? *slot->bone_to_world : matrix_3x4{};
      return true;
    }
  }

  return false;
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
  if (!hitscan_aim_scan_slots(ctx, slots, slot_count, -1, false, &best, &best_slot, reject_accum)) {
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

  int command_tick = 0;
  if (backtrack::command_tick_for_current_pose(found_out->sim_time, &command_tick)) {
    found_out->pose_timing_valid = true;
    found_out->pose_target_tick = command_tick - backtrack::current_timing().lerp_ticks;
    found_out->command_tick = command_tick;
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
  ctx.shoot_pos = hitscan_aim_eye_position(localplayer);
  if (!aimbot_vec3_is_finite(ctx.shoot_pos)) {
    return false;
  }

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
  if (hitscan_aim_scan_records(ctx, timing, found_out, &reject_accum)) {
    return true;
  }
  if (hitscan_aim_scan_live_pose(ctx, found_out, &reject_accum)) {
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
  candidate.backtrack_hitbox_valid = true;

  if (found.record != nullptr) {
    candidate.backtrack = true;
    candidate.backtrack_on_shot = found.on_shot;
    candidate.backtrack_mins = found.hull_world_mins;
    candidate.backtrack_maxs = found.hull_world_maxs;
    candidate.backtrack_timing_error = found.timing_error;
    candidate.backtrack_capture_gap = found.capture_gap;
    candidate.pose_timing_valid = false;
    candidate.pose_command_tick = found.command_tick;
    candidate.pose_target_tick = found.command_tick - backtrack::current_timing().lerp_ticks;
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

  if ((trace.entity != nullptr && !hitscan_aim_same_entity(trace.entity, target)) ||
      (trace.entity == nullptr && !hitscan_aim_ray_hits_entity_bounds(target, shoot_pos, position))) {
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

  return hitscan_aim_same_entity(trace.entity, candidate.entity) &&
    hitscan_aim_accepts_trace_hitbox(candidate, weapon, trace.hitbox);
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
