/*
/^-----^\   data: 2026-04-05
V  o o  V  file: src/features/combat/aimbot/hitscan_aim.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef HITSCAN_AIM_HPP
#define HITSCAN_AIM_HPP

#include "aim_utils.hpp"

#include "features/automation/nographics/nographics.hpp"

inline Vec3 hitscan_aim_bullet_angles(Player* localplayer, const Vec3& view_angles) {
  if (localplayer == nullptr) {
    return view_angles;
  }

  return view_angles + localplayer->get_punch_angles();
}

inline Vec3 hitscan_aim_command_angles(Player* localplayer, const Vec3& bullet_angles) {
  if (localplayer == nullptr) {
    return bullet_angles;
  }

  return bullet_angles - localplayer->get_punch_angles();
}

inline bool hitscan_aim_textmode_candidate_visible(Player* localplayer, const aimbot_candidate& candidate);

inline aimbot_candidate hitscan_aim_find_candidate(Player* localplayer, Weapon* weapon, Player* player, const Vec3& original_view_angles) {
  aimbot_candidate candidate{};
  if (localplayer == nullptr || weapon == nullptr || player == nullptr) return candidate;

  const Vec3 bullet_view_angles = hitscan_aim_bullet_angles(localplayer, original_view_angles);
  aimbot_point point = aimbot_find_best_point(
    localplayer,
    player,
    weapon,
    bullet_view_angles,
    config.aimbot.hitscan_hitboxes,
    true,
    aimbot_hitscan_trace_mask());
  if (!point.valid) {
    return candidate;
  }

  candidate.entity = player;
  candidate.player = player;
  candidate.preferred = has_aimbot_preference(player);
  candidate.bone = point.bone;
  candidate.hitbox = point.hitbox;
  candidate.aim_position = point.position;
  candidate.aim_angles = point.angles;
  candidate.fov = aimbot_calculate_fov(hitscan_aim_command_angles(localplayer, point.angles), original_view_angles);
  candidate.distance = distance_3d(localplayer->get_origin(), player->get_origin());
  candidate.health = player->get_health();
  candidate.visible = true;
  return candidate;
}

inline aimbot_candidate hitscan_aim_find_occluded_candidate(Player* localplayer, Weapon* weapon, Player* player, const Vec3& original_view_angles) {
  aimbot_candidate candidate{};
  if (localplayer == nullptr || weapon == nullptr || player == nullptr) return candidate;

  const Vec3 bullet_view_angles = hitscan_aim_bullet_angles(localplayer, original_view_angles);
  aimbot_point point = aimbot_find_best_point(
    localplayer,
    player,
    weapon,
    bullet_view_angles,
    config.aimbot.hitscan_hitboxes,
    false,
    aimbot_hitscan_trace_mask());
  if (!point.valid) {
    const int fallback_bone = aimbot_default_bone(localplayer, player, weapon);
    const Vec3 fallback_position = player->get_bone_pos(fallback_bone);
    if (!aimbot_vec3_is_finite(fallback_position)) {
      return candidate;
    }

    point.valid = true;
    point.bone = fallback_bone;
    point.position = fallback_position;
    point.angles = aimbot_calculate_angles_to_position(localplayer->get_shoot_pos(), fallback_position);
    point.fov = aimbot_calculate_fov(point.angles, bullet_view_angles);
  }

  candidate.entity = player;
  candidate.player = player;
  candidate.preferred = has_aimbot_preference(player);
  candidate.bone = point.bone;
  candidate.hitbox = point.hitbox;
  candidate.aim_position = point.position;
  candidate.aim_angles = point.angles;
  candidate.fov = aimbot_calculate_fov(hitscan_aim_command_angles(localplayer, point.angles), original_view_angles);
  candidate.distance = distance_3d(localplayer->get_origin(), player->get_origin());
  candidate.health = player->get_health();
  candidate.visible = aimbot_trace_visible_to_position(localplayer, player, point.position, aimbot_hitscan_trace_mask());
  if (!candidate.visible) {
    candidate.visible = hitscan_aim_textmode_candidate_visible(localplayer, candidate);
  }
  return candidate;
}

inline unsigned int hitscan_aim_world_trace_mask() {
  unsigned int trace_mask = MASK_SOLID_BRUSHONLY;
  if (config.aimbot.shoot_through_glass) {
    trace_mask &= ~CONTENTS_WINDOW;
  }

  return trace_mask;
}

inline bool hitscan_aim_world_clear(const Vec3& start_pos, const Vec3& end_pos) {
  if (engine_trace == nullptr || !aimbot_vec3_is_finite(start_pos) || !aimbot_vec3_is_finite(end_pos)) {
    return false;
  }

  Vec3 start = start_pos;
  Vec3 end = end_pos;
  struct ray_t ray = engine_trace->init_ray(&start, &end);
  struct trace_filter filter;
  engine_trace->init_world_trace_filter(&filter);

  struct trace_t trace_world{};
  engine_trace->trace_ray(&ray, hitscan_aim_world_trace_mask(), &filter, &trace_world);
  return !trace_world.all_solid && !trace_world.start_solid && trace_world.fraction >= 0.999f;
}

inline bool hitscan_aim_ray_hits_hitbox(Player* target, int hitbox_id, const Vec3& start_pos, const Vec3& end_pos) {
  if (target == nullptr || hitbox_id < 0 || model_info == nullptr) {
    return false;
  }

  const model_t* model = target->get_model();
  studio_hdr* hdr = model != nullptr ? model_info->get_studio_model(model) : nullptr;
  studio_hitbox_set* hitbox_set = hdr != nullptr ? hdr->hitbox_set(target->get_hitbox_set()) : nullptr;
  if (hitbox_set == nullptr || hitbox_id >= hitbox_set->num_hitboxes) {
    return false;
  }

  studio_box* hitbox = hitbox_set->hitbox(hitbox_id);
  if (hitbox == nullptr || hitbox->bone < 0 || hitbox->bone >= 128) {
    return false;
  }

  matrix_3x4 bone_to_world[128]{};
  if (!target->setup_bones(bone_to_world, 128, 0x100, target->get_simulation_time())) {
    return false;
  }

  const Vec3 local_start = aimbot_inverse_transform_point(start_pos, bone_to_world[hitbox->bone]);
  const Vec3 local_end = aimbot_inverse_transform_point(end_pos, bone_to_world[hitbox->bone]);
  constexpr float hitbox_expansion = 1.5f;
  const Vec3 mins = hitbox->bbmin - Vec3{hitbox_expansion, hitbox_expansion, hitbox_expansion};
  const Vec3 maxs = hitbox->bbmax + Vec3{hitbox_expansion, hitbox_expansion, hitbox_expansion};
  return aimbot_segment_intersects_aabb(local_start, local_end, mins, maxs);
}

inline bool hitscan_aim_ray_hits_player_bounds(Player* target, const Vec3& start_pos, const Vec3& end_pos) {
  if (target == nullptr) {
    return false;
  }

  constexpr float bounds_expansion = 2.0f;
  const Vec3 origin = target->get_collision_origin();
  const Vec3 mins = target->get_collideable_mins() + origin - Vec3{bounds_expansion, bounds_expansion, bounds_expansion};
  const Vec3 maxs = target->get_collideable_maxs() + origin + Vec3{bounds_expansion, bounds_expansion, bounds_expansion};
  return aimbot_segment_intersects_aabb(start_pos, end_pos, mins, maxs);
}

inline bool hitscan_aim_textmode_candidate_visible(Player* localplayer, const aimbot_candidate& candidate) {
  if (!nographics::should_use_aimbot_trace_fallback() ||
      localplayer == nullptr ||
      candidate.player == nullptr ||
      !aimbot_vec3_is_finite(candidate.aim_position)) {
    return false;
  }

  const Vec3 start_pos = localplayer->get_shoot_pos();
  const Vec3 end_pos = candidate.aim_position;
  if (!aimbot_vec3_is_finite(start_pos) || !hitscan_aim_world_clear(start_pos, candidate.aim_position)) {
    return false;
  }

  if (candidate.hitbox >= 0 && hitscan_aim_ray_hits_hitbox(candidate.player, candidate.hitbox, start_pos, end_pos)) {
    return true;
  }

  return hitscan_aim_ray_hits_player_bounds(candidate.player, start_pos, end_pos);
}

inline bool hitscan_aim_textmode_trace_fallback(const aimbot_candidate& candidate,
  const Vec3& start_pos,
  const Vec3& end_pos) {
  if (!nographics::should_use_aimbot_trace_fallback() ||
      candidate.player == nullptr ||
      !aimbot_vec3_is_finite(candidate.aim_position)) {
    return false;
  }

  if (!hitscan_aim_world_clear(start_pos, candidate.aim_position)) {
    return false;
  }

  if (hitscan_aim_ray_hits_hitbox(candidate.player, candidate.hitbox, start_pos, end_pos)) {
    return true;
  }

  return hitscan_aim_ray_hits_player_bounds(candidate.player, start_pos, end_pos);
}

inline bool hitscan_aim_trace_candidate(Player* localplayer,
  const aimbot_candidate& candidate,
  const Vec3& command_view_angles) {
  if (localplayer == nullptr ||
      candidate.entity == nullptr ||
      engine_trace == nullptr ||
      !aimbot_vec3_is_finite(candidate.aim_position)) {
    return false;
  }

  Vec3 start_pos = localplayer->get_shoot_pos();
  const Vec3 bullet_angles = hitscan_aim_bullet_angles(localplayer, command_view_angles);
  Vec3 forward{};
  angle_vectors(bullet_angles, &forward, nullptr, nullptr);
  if (!aimbot_vec3_is_finite(forward)) {
    return false;
  }

  const float target_distance = distance_3d(start_pos, candidate.aim_position);
  const float trace_distance = std::max(target_distance + 64.0f, 128.0f);
  Vec3 end_pos = start_pos + (forward * trace_distance);

  struct ray_t ray = engine_trace->init_ray(&start_pos, &end_pos);
  struct trace_filter filter;
  engine_trace->init_trace_filter(&filter, localplayer);

  struct trace_t trace_world{};
  engine_trace->trace_ray(&ray, aimbot_hitscan_trace_mask(), &filter, &trace_world);
  if (trace_world.entity != candidate.entity) {
    return hitscan_aim_textmode_trace_fallback(candidate, start_pos, end_pos);
  }

  if (candidate.hitbox == aim_hitbox_head && candidate.player != nullptr) {
    return trace_world.hitbox == aim_hitbox_head ||
           hitscan_aim_textmode_trace_fallback(candidate, start_pos, end_pos);
  }

  return true;
}

#endif
