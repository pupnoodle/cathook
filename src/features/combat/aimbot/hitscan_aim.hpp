/*
/^-----^\   data: 2026-04-05
V  o o  V  file: src/features/combat/aimbot/hitscan_aim.hpp
 |  Y  |   autor: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef HITSCAN_AIM_HPP
#define HITSCAN_AIM_HPP

#include "aim_utils.hpp"

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
  return candidate;
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
    return false;
  }

  if (candidate.hitbox == aim_hitbox_head && candidate.player != nullptr) {
    return trace_world.hitbox == aim_hitbox_head;
  }

  return true;
}

#endif
