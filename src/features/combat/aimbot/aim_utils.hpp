/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/combat/aimbot/aim_utils.hpp
 |  Y  |   autor: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef AIM_UTILS_HPP
#define AIM_UTILS_HPP

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <climits>
#include <cstddef>

#include "core/entity_cache.hpp"
#include "core/math/math.hpp"

#include "features/menu/config.hpp"
#include "features/movement/local_prediction/local_prediction.hpp"

#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/entities/building.hpp"
#include "games/tf2/sdk/aim_hitboxes.hpp"
#include "games/tf2/sdk/interfaces/attribute_manager.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/engine_trace.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"

struct aimbot_candidate {
  Entity* entity = nullptr;
  Player* player = nullptr;
  int bone = 0;
  int hitbox = -1;
  Vec3 aim_position{};
  Vec3 aim_angles{};
  float fov = FLT_MAX;
  float distance = FLT_MAX;
  int health = 0;
  bool visible = false;
  bool preferred = false;
  bool projectile_direct = false;
  bool projectile_splash = false;
  bool projectile_has_target_base_origin = false;
  float projectile_intercept_time = 0.0f;
  float projectile_miss_distance = FLT_MAX;
  float projectile_splash_radius = 0.0f;
  Vec3 projectile_target_base_origin{};
  Vec3 projectile_target_offset{};
  bool melee_has_prediction = false;
  float melee_impact_time = 0.0f;
  Vec3 melee_swing_start{};
  Vec3 melee_target_origin{};
};

struct aimbot_point {
  bool valid = false;
  int bone = 0;
  int hitbox = -1;
  int priority = INT_MAX;
  Vec3 position{};
  Vec3 angles{};
  float fov = FLT_MAX;
};

inline bool aimbot_vec3_is_finite(const Vec3& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

inline float aimbot_distance_squared(const Vec3& left, const Vec3& right) {
  const Vec3 delta = left - right;
  return (delta.x * delta.x) + (delta.y * delta.y) + (delta.z * delta.z);
}

inline Vec3 aimbot_transform_point(const Vec3& point, const matrix_3x4& matrix) {
  return Vec3{
    (point.x * matrix.mat[0][0]) + (point.y * matrix.mat[0][1]) + (point.z * matrix.mat[0][2]) + matrix.mat[0][3],
    (point.x * matrix.mat[1][0]) + (point.y * matrix.mat[1][1]) + (point.z * matrix.mat[1][2]) + matrix.mat[1][3],
    (point.x * matrix.mat[2][0]) + (point.y * matrix.mat[2][1]) + (point.z * matrix.mat[2][2]) + matrix.mat[2][3]
  };
}

inline Vec3 aimbot_inverse_transform_point(const Vec3& point, const matrix_3x4& matrix) {
  const Vec3 delta{
    point.x - matrix.mat[0][3],
    point.y - matrix.mat[1][3],
    point.z - matrix.mat[2][3]
  };

  return Vec3{
    (delta.x * matrix.mat[0][0]) + (delta.y * matrix.mat[1][0]) + (delta.z * matrix.mat[2][0]),
    (delta.x * matrix.mat[0][1]) + (delta.y * matrix.mat[1][1]) + (delta.z * matrix.mat[2][1]),
    (delta.x * matrix.mat[0][2]) + (delta.y * matrix.mat[1][2]) + (delta.z * matrix.mat[2][2])
  };
}

inline Vec3 aimbot_clamp_to_hitbox(const Vec3& point, const studio_box& hitbox) {
  return Vec3{
    std::clamp(point.x, hitbox.bbmin.x, hitbox.bbmax.x),
    std::clamp(point.y, hitbox.bbmin.y, hitbox.bbmax.y),
    std::clamp(point.z, hitbox.bbmin.z, hitbox.bbmax.z)
  };
}

inline bool aimbot_add_local_hitbox_point(Vec3* points, int* point_count, int max_points, const Vec3& point) {
  if (points == nullptr || point_count == nullptr || *point_count >= max_points || !aimbot_vec3_is_finite(point)) {
    return false;
  }

  for (int index = 0; index < *point_count; ++index) {
    if (aimbot_distance_squared(points[index], point) < 0.25f) {
      return false;
    }
  }

  points[*point_count] = point;
  ++(*point_count);
  return true;
}

inline int aimbot_build_local_hitbox_points(const studio_box& hitbox,
  const matrix_3x4& bone_to_world,
  const Vec3& shoot_pos,
  Vec3* points,
  int max_points,
  bool include_multipoint) {
  int point_count = 0;
  const Vec3 center = (hitbox.bbmin + hitbox.bbmax) * 0.5f;
  aimbot_add_local_hitbox_point(points, &point_count, max_points, center);

  if (!include_multipoint) {
    return point_count;
  }

  const Vec3 local_shoot_pos = aimbot_inverse_transform_point(shoot_pos, bone_to_world);
  aimbot_add_local_hitbox_point(points, &point_count, max_points, aimbot_clamp_to_hitbox(local_shoot_pos, hitbox));

  const Vec3 extent = (hitbox.bbmax - hitbox.bbmin) * 0.5f;
  constexpr float point_scale = 0.62f;

  if (std::fabs(extent.x) > 1.0f) {
    aimbot_add_local_hitbox_point(points, &point_count, max_points, center + Vec3{extent.x * point_scale, 0.0f, 0.0f});
    aimbot_add_local_hitbox_point(points, &point_count, max_points, center - Vec3{extent.x * point_scale, 0.0f, 0.0f});
  }

  if (std::fabs(extent.y) > 1.0f) {
    aimbot_add_local_hitbox_point(points, &point_count, max_points, center + Vec3{0.0f, extent.y * point_scale, 0.0f});
    aimbot_add_local_hitbox_point(points, &point_count, max_points, center - Vec3{0.0f, extent.y * point_scale, 0.0f});
  }

  if (std::fabs(extent.z) > 1.0f) {
    aimbot_add_local_hitbox_point(points, &point_count, max_points, center + Vec3{0.0f, 0.0f, extent.z * point_scale});
    aimbot_add_local_hitbox_point(points, &point_count, max_points, center - Vec3{0.0f, 0.0f, extent.z * point_scale});
  }

  return point_count;
}

inline unsigned int aimbot_visibility_trace_mask() {
  unsigned int trace_mask = MASK_SHOT | CONTENTS_GRATE;
  if (config.aimbot.shoot_through_glass) {
    trace_mask &= ~CONTENTS_WINDOW;
  }

  return trace_mask;
}

inline unsigned int aimbot_hitscan_trace_mask() {
  unsigned int trace_mask = MASK_SOLID | CONTENTS_HITBOX;
  if (config.aimbot.shoot_through_glass) {
    trace_mask &= ~CONTENTS_WINDOW;
  }

  return trace_mask;
}

inline bool is_player_visible(Player* localplayer, Player* entity, int bone) {
  if (localplayer == nullptr || entity == nullptr || engine_trace == nullptr) return false;

  Vec3 start_pos = localplayer->get_shoot_pos();
  Vec3 target_pos = entity->get_bone_pos(bone);

  struct ray_t ray = engine_trace->init_ray(&start_pos, &target_pos);
  struct trace_filter filter;
  engine_trace->init_trace_filter(&filter, localplayer);

  struct trace_t trace_world{};
  engine_trace->trace_ray(&ray, aimbot_visibility_trace_mask(), &filter, &trace_world);
  return trace_world.entity == entity || (!trace_world.all_solid && !trace_world.start_solid && trace_world.fraction >= 0.999f);
}

inline bool aimbot_trace_visible_to_position(Player* localplayer,
  Entity* target,
  const Vec3& target_pos,
  unsigned int trace_mask = aimbot_visibility_trace_mask()) {
  if (localplayer == nullptr || target == nullptr || engine_trace == nullptr || !aimbot_vec3_is_finite(target_pos)) return false;

  Vec3 start_pos = localplayer->get_shoot_pos();
  Vec3 end_pos = target_pos;

  struct ray_t ray = engine_trace->init_ray(&start_pos, &end_pos);
  struct trace_filter filter;
  engine_trace->init_trace_filter(&filter, localplayer);

  struct trace_t trace_world{};
  engine_trace->trace_ray(&ray, trace_mask, &filter, &trace_world);
  return trace_world.entity == target || (!trace_world.all_solid && !trace_world.start_solid && trace_world.fraction >= 0.999f);
}

inline Vec3 aimbot_calculate_angles_to_position(const Vec3& start, const Vec3& target) {
  Vec3 diff{
    target.x - start.x,
    target.y - start.y,
    target.z - start.z
  };
  float yaw_hyp = std::sqrt((diff.x * diff.x) + (diff.y * diff.y));
  return Vec3{
    -std::atan2(diff.z, yaw_hyp) * radpi,
    std::atan2(diff.y, diff.x) * radpi,
    0.0f
  };
}

inline Vec3 aimbot_normalize_angle_delta(const Vec3& target_angles, const Vec3& source_angles) {
  float x_diff = target_angles.x - source_angles.x;
  float y_diff = target_angles.y - source_angles.y;

  return Vec3{
    std::clamp(std::remainder(x_diff, 360.0f), -89.0f, 89.0f),
    std::clamp(std::remainder(y_diff, 360.0f), -180.0f, 180.0f),
    0.0f
  };
}

inline Vec3 aimbot_clamp_angles(Vec3 angles) {
  angles.x = std::clamp(angles.x, -89.0f, 89.0f);
  angles.y = std::remainder(angles.y, 360.0f);
  angles.z = 0.0f;
  return angles;
}

inline float aimbot_calculate_fov(const Vec3& target_angles, const Vec3& source_angles) {
  Vec3 delta = aimbot_normalize_angle_delta(target_angles, source_angles);
  return std::hypot(delta.x, delta.y);
}

inline int aimbot_default_bone(Player* localplayer, Player* target, Weapon* weapon) {
  if (localplayer == nullptr || target == nullptr) return 0;

  int bone = target->get_tf_class() == tf_class::ENGINEER ? 5 : 2;
  if (localplayer->get_tf_class() == tf_class::SNIPER) {
    if (weapon != nullptr && weapon->is_headshot_weapon() && config.aimbot.wait_for_headshot) {
      bone = target->get_head_bone();
    } else if (localplayer->is_scoped() && target->get_health() > 50) {
      bone = target->get_head_bone();
    }
  } else if (localplayer->get_tf_class() == tf_class::SPY && weapon != nullptr && weapon->is_headshot_weapon()) {
    bone = target->get_head_bone();
  }

  return bone;
}

inline bool aimbot_hitbox_matches_mask(int hitbox_id, uint32_t hitbox_mask) {
  switch (hitbox_id) {
  case aim_hitbox_head:
    return (hitbox_mask & aim_hitbox_mask_head) != 0;
  case aim_hitbox_pelvis:
    return (hitbox_mask & aim_hitbox_mask_pelvis) != 0;
  case aim_hitbox_spine_0:
  case aim_hitbox_spine_1:
  case aim_hitbox_spine_2:
  case aim_hitbox_spine_3:
    return (hitbox_mask & aim_hitbox_mask_body) != 0;
  case aim_hitbox_left_upper_arm:
  case aim_hitbox_left_forearm:
  case aim_hitbox_left_hand:
  case aim_hitbox_right_upper_arm:
  case aim_hitbox_right_forearm:
  case aim_hitbox_right_hand:
    return (hitbox_mask & aim_hitbox_mask_arms) != 0;
  case aim_hitbox_left_thigh:
  case aim_hitbox_left_calf:
  case aim_hitbox_left_foot:
  case aim_hitbox_right_thigh:
  case aim_hitbox_right_calf:
  case aim_hitbox_right_foot:
    return (hitbox_mask & aim_hitbox_mask_legs) != 0;
  default:
    return false;
  }
}

inline int aimbot_hitbox_priority(Player* localplayer, Player* target, Weapon* weapon, int hitbox_id) {
  bool prefer_head = false;
  if (localplayer != nullptr && target != nullptr) {
    if (localplayer->get_tf_class() == tf_class::SNIPER && weapon != nullptr && weapon->is_headshot_weapon()) {
      prefer_head = config.aimbot.wait_for_headshot || (localplayer->is_scoped() && target->get_health() > 50);
    } else if (localplayer->get_tf_class() == tf_class::SPY && weapon != nullptr && weapon->is_headshot_weapon()) {
      prefer_head = true;
    }
  }

  switch (hitbox_id) {
  case aim_hitbox_head:
    return prefer_head ? 0 : 1;
  case aim_hitbox_spine_0:
  case aim_hitbox_spine_1:
  case aim_hitbox_spine_2:
  case aim_hitbox_spine_3:
    return prefer_head ? 1 : 0;
  case aim_hitbox_pelvis:
    return 2;
  case aim_hitbox_left_upper_arm:
  case aim_hitbox_left_forearm:
  case aim_hitbox_left_hand:
  case aim_hitbox_right_upper_arm:
  case aim_hitbox_right_forearm:
  case aim_hitbox_right_hand:
    return 3;
  case aim_hitbox_left_thigh:
  case aim_hitbox_left_calf:
  case aim_hitbox_left_foot:
  case aim_hitbox_right_thigh:
  case aim_hitbox_right_calf:
  case aim_hitbox_right_foot:
    return 4;
  default:
    return INT_MAX;
  }
}

inline aimbot_point aimbot_find_best_point(Player* localplayer,
  Player* target,
  Weapon* weapon,
  const Vec3& original_view_angles,
  uint32_t hitbox_mask,
  bool require_visibility = true,
  unsigned int trace_mask = aimbot_visibility_trace_mask()) {
  aimbot_point best_point{};
  if (localplayer == nullptr || target == nullptr) {
    return best_point;
  }

  if (hitbox_mask == aim_hitbox_mask_none) {
    hitbox_mask = aim_hitbox_mask_default_hitscan;
  }

  const model_t* model = target->get_model();
  if (model != nullptr && model_info != nullptr) {
    studio_hdr* hdr = model_info->get_studio_model(model);
    studio_hitbox_set* hitbox_set = hdr != nullptr ? hdr->hitbox_set(target->get_hitbox_set()) : nullptr;
    if (hitbox_set != nullptr) {
      matrix_3x4 bone_to_world[128]{};
      if (target->setup_bones(bone_to_world, 128, 0x100, target->get_simulation_time())) {
        const Vec3 shoot_pos = localplayer->get_shoot_pos();
        for (int hitbox_id = aim_hitbox_head; hitbox_id <= aim_hitbox_right_foot; ++hitbox_id) {
          if (!aimbot_hitbox_matches_mask(hitbox_id, hitbox_mask) || hitbox_id >= hitbox_set->num_hitboxes) {
            continue;
          }

          studio_box* hitbox = hitbox_set->hitbox(hitbox_id);
          if (hitbox == nullptr || hitbox->bone < 0 || hitbox->bone >= 128) {
            continue;
          }

          Vec3 local_points[8]{};
          const int point_count = aimbot_build_local_hitbox_points(
            *hitbox,
            bone_to_world[hitbox->bone],
            shoot_pos,
            local_points,
            8,
            require_visibility);

          for (int point_index = 0; point_index < point_count; ++point_index) {
            const Vec3 hitbox_position = aimbot_transform_point(local_points[point_index], bone_to_world[hitbox->bone]);
            if (!aimbot_vec3_is_finite(hitbox_position)) {
              continue;
            }

            if (require_visibility && !aimbot_trace_visible_to_position(localplayer, target, hitbox_position, trace_mask)) {
              continue;
            }

            aimbot_point point{};
            point.valid = true;
            point.bone = hitbox->bone;
            point.hitbox = hitbox_id;
            point.priority = aimbot_hitbox_priority(localplayer, target, weapon, hitbox_id);
            point.position = hitbox_position;
            point.angles = aimbot_calculate_angles_to_position(shoot_pos, hitbox_position);
            point.fov = aimbot_calculate_fov(point.angles, original_view_angles);

            if (!best_point.valid ||
                point.priority < best_point.priority ||
                (point.priority == best_point.priority && point.fov < best_point.fov)) {
              best_point = point;
            }
          }
        }
      }
    }
  }

  if (best_point.valid) {
    return best_point;
  }

  for (int hitbox_id = aim_hitbox_head; hitbox_id <= aim_hitbox_right_foot; ++hitbox_id) {
    if (!aimbot_hitbox_matches_mask(hitbox_id, hitbox_mask)) {
      continue;
    }

    Vec3 hitbox_position{};
    int hitbox_bone = 0;
    if (!target->get_hitbox_center(hitbox_id, &hitbox_position, &hitbox_bone)) {
      continue;
    }

    if (!aimbot_vec3_is_finite(hitbox_position)) {
      continue;
    }

    if (require_visibility && !aimbot_trace_visible_to_position(localplayer, target, hitbox_position, trace_mask)) {
      continue;
    }

    aimbot_point point{};
    point.valid = true;
    point.bone = hitbox_bone;
    point.hitbox = hitbox_id;
    point.priority = aimbot_hitbox_priority(localplayer, target, weapon, hitbox_id);
    point.position = hitbox_position;
    point.angles = aimbot_calculate_angles_to_position(localplayer->get_shoot_pos(), hitbox_position);
    point.fov = aimbot_calculate_fov(point.angles, original_view_angles);

    if (!best_point.valid ||
        point.priority < best_point.priority ||
        (point.priority == best_point.priority && point.fov < best_point.fov)) {
      best_point = point;
    }
  }

  if (best_point.valid) {
    return best_point;
  }

  const int fallback_bone = aimbot_default_bone(localplayer, target, weapon);
  const Vec3 fallback_position = target->get_bone_pos(fallback_bone);
  if (!aimbot_vec3_is_finite(fallback_position)) {
    return {};
  }

  if (require_visibility && !aimbot_trace_visible_to_position(localplayer, target, fallback_position, trace_mask)) {
    return {};
  }

  best_point.valid = true;
  best_point.bone = fallback_bone;
  best_point.position = fallback_position;
  best_point.angles = aimbot_calculate_angles_to_position(localplayer->get_shoot_pos(), fallback_position);
  best_point.fov = aimbot_calculate_fov(best_point.angles, original_view_angles);
  return best_point;
}

inline bool aimbot_segment_intersects_aabb(const Vec3& start,
  const Vec3& end,
  const Vec3& mins,
  const Vec3& maxs) {
  Vec3 delta = end - start;
  float enter = 0.0f;
  float exit = 1.0f;

  const auto clip_axis = [&](float start_axis, float delta_axis, float min_axis, float max_axis) -> bool {
    if (std::fabs(delta_axis) <= 0.0001f) {
      return start_axis >= min_axis && start_axis <= max_axis;
    }

    float inv_delta = 1.0f / delta_axis;
    float t1 = (min_axis - start_axis) * inv_delta;
    float t2 = (max_axis - start_axis) * inv_delta;
    if (t1 > t2) {
      std::swap(t1, t2);
    }

    enter = std::max(enter, t1);
    exit = std::min(exit, t2);
    return enter <= exit;
  };

  return clip_axis(start.x, delta.x, mins.x, maxs.x) &&
         clip_axis(start.y, delta.y, mins.y, maxs.y) &&
         clip_axis(start.z, delta.z, mins.z, maxs.z);
}

inline bool aimbot_is_repair_wrench(Weapon* weapon) {
  if (weapon == nullptr) {
    return false;
  }

  switch (weapon->get_def_id()) {
  case Engi_t_Wrench:
  case Engi_t_WrenchR:
  case Engi_t_TheGunslinger:
  case Engi_t_TheSouthernHospitality:
  case Engi_t_GoldenWrench:
  case Engi_t_TheJag:
  case Engi_t_TheEurekaEffect:
  case Engi_t_FestiveWrench:
  case Engi_t_SilverBotkillerWrenchMkI:
  case Engi_t_GoldBotkillerWrenchMkI:
  case Engi_t_RustBotkillerWrenchMkI:
  case Engi_t_BloodBotkillerWrenchMkI:
  case Engi_t_CarbonadoBotkillerWrenchMkI:
  case Engi_t_DiamondBotkillerWrenchMkI:
  case Engi_t_SilverBotkillerWrenchMkII:
  case Engi_t_GoldBotkillerWrenchMkII:
    return true;
  default:
    return false;
  }
}

inline bool aimbot_is_sword_melee(Weapon* weapon) {
  if (weapon == nullptr) {
    return false;
  }

  switch (weapon->get_def_id()) {
  case Demoman_t_TheEyelander:
  case Demoman_t_TheScotsmansSkullcutter:
  case Demoman_t_HorselessHeadlessHorsemannsHeadtaker:
  case Demoman_t_TheClaidheamhMor:
  case Demoman_t_TheHalfZatoichi:
  case Demoman_t_ThePersianPersuader:
  case Demoman_t_NessiesNineIron:
  case Demoman_t_FestiveEyelander:
    return true;
  default:
    return false;
  }
}

inline float aimbot_get_base_melee_range(Player* localplayer, Weapon* weapon) {
  if (localplayer != nullptr && localplayer->in_cond(TF_COND_SHIELD_CHARGE)) {
    return 128.0f;
  }

  return aimbot_is_sword_melee(weapon) ? 72.0f : 48.0f;
}

inline float aimbot_get_melee_range(Player* localplayer, Weapon* weapon, Player* target) {
  if (weapon == nullptr) {
    return 0.0f;
  }

  float melee_range = aimbot_get_base_melee_range(localplayer, weapon);

  if (localplayer != nullptr && localplayer->get_model_scale() > 1.0f) {
    melee_range *= localplayer->get_model_scale();
  }

  if (attribute_manager != nullptr) {
    melee_range = attribute_manager->attrib_hook_value(melee_range, "melee_range_multiplier", weapon->to_entity());
  }

  if (target != nullptr &&
      localplayer != nullptr &&
      target->get_team() == localplayer->get_team() &&
      aimbot_is_repair_wrench(weapon)) {
    melee_range = 70.0f;
  }

  return std::max(melee_range - 4.0f, 0.0f);
}

inline float aimbot_get_melee_hull(Player* localplayer, Weapon* weapon, Player* target) {
  if (weapon == nullptr) {
    return 0.0f;
  }

  float melee_hull = 18.0f;
  if (attribute_manager != nullptr) {
    melee_hull = attribute_manager->attrib_hook_value(melee_hull, "melee_bounds_multiplier", weapon->to_entity());
  }

  if (localplayer != nullptr && localplayer->get_model_scale() > 1.0f) {
    melee_hull *= localplayer->get_model_scale();
  }

  if (target != nullptr &&
      localplayer != nullptr &&
      target->get_team() == localplayer->get_team() &&
      aimbot_is_repair_wrench(weapon)) {
    melee_hull = 18.0f;
  }

  return melee_hull;
}

inline bool aimbot_is_friendlyfire_enabled() {
  static Convar* friendlyfire = convar_system->find_var("mp_friendlyfire");
  return friendlyfire != nullptr && friendlyfire->get_int() != 0;
}

inline bool aimbot_aim_at_enabled(uint32_t flag) {
  return (config.aimbot.aim_at & flag) != 0;
}

inline bool aimbot_is_mvm_robot(Player* player) {
  if (player == nullptr) {
    return false;
  }

  const char* model_name = player->get_model_name();
  if (strstr(model_name, "models/bots/") != nullptr) {
    return true;
  }

  player_info info{};
  return engine != nullptr && engine->get_player_info(player->get_index(), &info) && info.fakeplayer;
}

inline bool aimbot_should_target_player_type(Player* player) {
  if (aimbot_is_mvm_robot(player)) {
    return aimbot_aim_at_enabled(Aim::aim_at_mvm_robots);
  }

  return aimbot_aim_at_enabled(Aim::aim_at_enemies);
}

inline bool aimbot_should_skip_player(Player* localplayer, Player* player) {
  if (localplayer == nullptr || player == nullptr) return true;
  if (player == localplayer) return true;
  if (player->is_dormant()) return true;
  if (!player->is_alive()) return true;
  if (player->is_invulnerable()) return true;
  if (player->is_ignored()) return true;
  if (config.aimbot.ignore_friends && player->is_friend()) return true;
  if (player->get_team() == localplayer->get_team() && !aimbot_is_friendlyfire_enabled()) return true;
  if (!aimbot_should_target_player_type(player)) return true;
  return false;
}

inline bool aimbot_entity_is_enemy_owned(Player* localplayer, Entity* entity) {
  if (localplayer == nullptr || entity == nullptr) {
    return false;
  }

  Entity* owner = entity->get_owner_entity();
  if (owner != nullptr) {
    if (owner == localplayer || owner->get_team() == localplayer->get_team()) {
      return false;
    }

    return true;
  }

  const tf_team target_team = entity->get_team();
  return target_team == tf_team::UNKNOWN ||
         target_team == tf_team::SPECTATOR ||
         target_team != localplayer->get_team();
}

inline bool aimbot_is_stickybomb_target(Entity* entity) {
  if (entity == nullptr || entity->get_class_id() != class_id::PILL_OR_STICKY) {
    return false;
  }

  return strstr(entity->get_model_name(), "sticky") != nullptr;
}

inline bool aimbot_is_pumpkin_target(Entity* entity) {
  if (entity == nullptr) {
    return false;
  }

  return entity->is_network_class("CTFPumpkinBomb") ||
         strstr(entity->get_model_name(), "pumpkin_explode") != nullptr;
}

inline bool aimbot_should_skip_non_player_target(Player* localplayer, Entity* entity) {
  if (localplayer == nullptr || entity == nullptr || entity == localplayer) {
    return true;
  }

  if (entity->is_dormant()) {
    return true;
  }

  if (entity->is_building()) {
    if (!aimbot_aim_at_enabled(Aim::aim_at_buildings)) {
      return true;
    }

    auto* building = static_cast<Building*>(entity);
    return building->is_carried() ||
           building->get_health() <= 0 ||
           !aimbot_entity_is_enemy_owned(localplayer, entity);
  }

  if (aimbot_is_stickybomb_target(entity)) {
    return !aimbot_aim_at_enabled(Aim::aim_at_stickies) ||
           !aimbot_entity_is_enemy_owned(localplayer, entity);
  }

  if (aimbot_is_pumpkin_target(entity)) {
    return !aimbot_aim_at_enabled(Aim::aim_at_pumpkins);
  }

  return true;
}

inline Vec3 aimbot_entity_target_position(Entity* entity) {
  if (entity == nullptr) {
    return {};
  }

  const Vec3 mins = entity->get_collideable_mins();
  const Vec3 maxs = entity->get_collideable_maxs();
  const Vec3 center_offset = (mins + maxs) * 0.5f;
  return entity->get_collision_origin() + center_offset;
}

inline int aimbot_entity_health(Entity* entity) {
  if (entity == nullptr) {
    return 0;
  }

  if (entity->get_class_id() == class_id::PLAYER) {
    return static_cast<Player*>(entity)->get_health();
  }

  if (entity->is_building()) {
    return static_cast<Building*>(entity)->get_health();
  }

  return 1;
}

inline bool aimbot_entity_melee_reachable(Player* localplayer,
  Weapon* weapon,
  Entity* target,
  const Vec3& aim_position,
  const Vec3& aim_angles) {
  if (localplayer == nullptr || weapon == nullptr || target == nullptr) {
    return false;
  }

  const float melee_range = aimbot_get_melee_range(localplayer, weapon, nullptr);
  const float melee_hull = aimbot_get_melee_hull(localplayer, weapon, nullptr);
  if (melee_range <= 0.0f || melee_hull < 0.0f) {
    return false;
  }

  const float distance = distance_3d(localplayer->get_shoot_pos(), aim_position);
  if (distance > melee_range + melee_hull) {
    return false;
  }

  Vec3 start = localplayer->get_shoot_pos();
  Vec3 forward = local_prediction_angles_to_direction(aim_angles);
  Vec3 end = start + (forward * melee_range);
  Vec3 target_mins = target->get_collideable_mins() + target->get_collision_origin() - Vec3{melee_hull, melee_hull, melee_hull};
  Vec3 target_maxs = target->get_collideable_maxs() + target->get_collision_origin() + Vec3{melee_hull, melee_hull, melee_hull};

  return aimbot_segment_intersects_aabb(start, end, target_mins, target_maxs);
}

inline aimbot_candidate aimbot_find_non_player_candidate(Player* localplayer,
  Weapon* weapon,
  Entity* entity,
  const Vec3& original_view_angles) {
  aimbot_candidate candidate{};
  if (weapon == nullptr || aimbot_should_skip_non_player_target(localplayer, entity)) {
    return candidate;
  }

  const Vec3 target_position = aimbot_entity_target_position(entity);
  const Vec3 aim_angles = aimbot_calculate_angles_to_position(localplayer->get_shoot_pos(), target_position);
  if (weapon->is_melee() && !aimbot_entity_melee_reachable(localplayer, weapon, entity, target_position, aim_angles)) {
    return candidate;
  }

  candidate.entity = entity;
  candidate.aim_position = target_position;
  candidate.aim_angles = aim_angles;
  candidate.fov = aimbot_calculate_fov(aim_angles, original_view_angles);
  candidate.distance = distance_3d(localplayer->get_origin(), entity->get_origin());
  candidate.health = aimbot_entity_health(entity);
  candidate.visible = aimbot_trace_visible_to_position(localplayer, entity, target_position);
  return candidate;
}

inline bool aimbot_candidate_visible_shootable(Player* localplayer, const aimbot_candidate& candidate) {
  return localplayer != nullptr &&
         candidate.entity != nullptr &&
         candidate.visible &&
         localplayer->can_shoot(candidate.entity);
}

inline bool aimbot_use_key_active() {
  return config.aimbot.key.button == SDLK_UNKNOWN || is_button_active(config.aimbot.key);
}

inline bool aimbot_scoped_only_ready(Player* localplayer, Weapon* weapon) {
  if (localplayer == nullptr || weapon == nullptr) return false;
  if (!weapon->is_sniper_rifle()) return true;
  return !config.aimbot.scoped_only || localplayer->is_scoped();
}

inline bool aimbot_wait_for_headshot_ready(Player* localplayer, Weapon* weapon) {
  if (!config.aimbot.wait_for_headshot || localplayer == nullptr || weapon == nullptr) return true;
  if (!weapon->is_headshot_weapon()) return true;

  if (localplayer->get_tf_class() == tf_class::SPY) {
    return weapon->can_ambassador_headshot();
  }

  if (localplayer->get_tf_class() != tf_class::SNIPER || !weapon->is_sniper_rifle()) return true;
  if (attribute_manager != nullptr && attribute_manager->attrib_hook_value(0, "set_weapon_mode", weapon->to_entity()) == 1) return false;
  if (attribute_manager != nullptr && attribute_manager->attrib_hook_value(0, "sniper_no_headshot_without_full_charge", weapon->to_entity()) != 0) {
    if (weapon->get_charged_damage() < 150.0f) return false;
  }

  if (attribute_manager != nullptr && attribute_manager->attrib_hook_value(0, "sniper_crit_no_scope", weapon->to_entity()) != 0) return true;
  if (!localplayer->is_scoped() || localplayer->get_fov() >= localplayer->get_default_fov()) return false;

  float current_time = global_vars != nullptr ? global_vars->curtime : localplayer->get_tickbase() * TICK_INTERVAL;
  float scoped_time = current_time - localplayer->get_fov_time();
  return scoped_time >= 0.2f;
}

inline bool aimbot_is_projectile_weapon(Weapon* weapon) {
  if (weapon == nullptr) return false;

  switch (weapon->get_def_id()) {
  case Soldier_m_RocketLauncher:
  case Soldier_m_RocketLauncherR:
  case Soldier_m_TheDirectHit:
  case Soldier_m_TheBlackBox:
  case Soldier_m_RocketJumper:
  case Soldier_m_TheLibertyLauncher:
  case Soldier_m_TheCowMangler5000:
  case Soldier_m_TheOriginal:
  case Soldier_m_FestiveRocketLauncher:
  case Soldier_m_TheBeggarsBazooka:
  case Soldier_m_FestiveBlackBox:
  case Soldier_m_TheAirStrike:
  case Soldier_s_TheRighteousBison:
  case Medic_m_CrusadersCrossbow:
  case Medic_m_FestiveCrusadersCrossbow:
  case Medic_m_SyringeGun:
  case Medic_m_SyringeGunR:
  case Medic_m_TheBlutsauger:
  case Medic_m_TheOverdose:
  case Engi_m_TheRescueRanger:
  case Engi_m_ThePomson6000:
  case Sniper_m_TheHuntsman:
  case Sniper_m_FestiveHuntsman:
  case Sniper_m_TheFortifiedCompound:
  case Pyro_s_TheFlareGun:
  case Pyro_s_TheDetonator:
  case Pyro_s_TheManmelter:
  case Pyro_s_TheScorchShot:
  case Pyro_s_FestiveFlareGun:
  case Pyro_m_DragonsFury:
  case Pyro_s_GasPasser:
  case Scout_s_MadMilk:
  case Scout_s_MutatedMilk:
  case Scout_s_TheFlyingGuillotine:
  case Scout_s_TheFlyingGuillotineG:
  case Sniper_s_Jarate:
  case Sniper_s_FestiveJarate:
  case Demoman_m_GrenadeLauncher:
  case Demoman_m_GrenadeLauncherR:
  case Demoman_m_TheLochnLoad:
  case Demoman_m_TheLooseCannon:
  case Demoman_m_FestiveGrenadeLauncher:
  case Demoman_m_TheIronBomber:
  case Demoman_s_StickybombLauncher:
  case Demoman_s_StickybombLauncherR:
  case Demoman_s_FestiveStickybombLauncher:
  case Demoman_s_TheScottishResistance:
  case Demoman_s_TheQuickiebombLauncher:
      return true;
  default:
      return false;
  }
}

inline bool aimbot_is_melee_weapon(Weapon* weapon) {
  return weapon != nullptr && weapon->is_melee();
}

inline float aimbot_effective_fov(const aimbot_candidate& candidate) {
  return candidate.preferred ? candidate.fov * 0.2f : candidate.fov;
}

inline bool aimbot_fov_unlimited() {
  return config.aimbot.fov <= 0.0f;
}

inline float aimbot_fov_limit(float scale = 1.0f, float minimum = 0.0f, float extra = 0.0f) {
  if (aimbot_fov_unlimited()) {
    return FLT_MAX;
  }

  return std::max(config.aimbot.fov * scale, minimum) + extra;
}

inline bool aimbot_fov_within_limit(float fov,
  float scale = 1.0f,
  float minimum = 0.0f,
  float extra = 0.0f) {
  return fov <= aimbot_fov_limit(scale, minimum, extra);
}

inline float aimbot_effective_distance(const aimbot_candidate& candidate) {
  return candidate.preferred ? candidate.distance * 0.35f : candidate.distance;
}

inline int aimbot_effective_smallest_health(const aimbot_candidate& candidate) {
  return candidate.preferred ? candidate.health - 500 : candidate.health;
}

inline int aimbot_effective_largest_health(const aimbot_candidate& candidate) {
  return candidate.preferred ? candidate.health + 500 : candidate.health;
}

inline bool aimbot_candidate_better(const aimbot_candidate& candidate, const aimbot_candidate& best) {
  if (candidate.entity == nullptr) return false;
  if (best.entity == nullptr) return true;

  switch (config.aimbot.target_type) {
  case Aim::TargetType::FOV:
    return aimbot_effective_fov(candidate) < aimbot_effective_fov(best);
  case Aim::TargetType::DISTANCE:
    if (aimbot_effective_distance(candidate) == aimbot_effective_distance(best)) {
      return aimbot_effective_fov(candidate) < aimbot_effective_fov(best);
    }
    return aimbot_effective_distance(candidate) < aimbot_effective_distance(best);
  case Aim::TargetType::LEAST_HEALTH:
    if (aimbot_effective_smallest_health(candidate) == aimbot_effective_smallest_health(best)) {
      return aimbot_effective_fov(candidate) < aimbot_effective_fov(best);
    }
    return aimbot_effective_smallest_health(candidate) < aimbot_effective_smallest_health(best);
  case Aim::TargetType::MOST_HEALTH:
    if (aimbot_effective_largest_health(candidate) == aimbot_effective_largest_health(best)) {
      return aimbot_effective_fov(candidate) < aimbot_effective_fov(best);
    }
    return aimbot_effective_largest_health(candidate) > aimbot_effective_largest_health(best);
  default:
    return false;
  }
}

inline float aimbot_assist_strength(const Vec3& original_view_angles, const Vec3& target_view_angles) {
  const float assist_strength = std::clamp(config.aimbot.assist_strength / 100.0f, 0.0f, 1.0f);
  if (assist_strength <= 0.0f) {
    return 0.0f;
  }

  const float aim_fov = aimbot_fov_limit(1.0f, 1.0f);
  const float fov_ratio = std::clamp(aimbot_calculate_fov(target_view_angles, original_view_angles) / aim_fov, 0.0f, 1.0f);
  return std::clamp(assist_strength * std::clamp(1.0f - fov_ratio, 0.05f, 1.0f), 0.0f, 1.0f);
}

inline Vec3 aimbot_step_towards_angles(const Vec3& source_angles, const Vec3& target_angles, float max_step) {
  const Vec3 delta = aimbot_normalize_angle_delta(target_angles, source_angles);
  const float delta_length = std::hypot(delta.x, delta.y);
  if (delta_length <= 0.0001f || max_step <= 0.0f) {
    return aimbot_clamp_angles(target_angles);
  }

  const float scale = std::min(max_step / delta_length, 1.0f);
  return aimbot_clamp_angles(Vec3{
    source_angles.x + (delta.x * scale),
    source_angles.y + (delta.y * scale),
    0.0f
  });
}

inline Vec3 aimbot_apply_smooth_angles(const Vec3& source_view_angles, const Vec3& target_view_angles) {
  const float smooth_factor = std::clamp(config.aimbot.smooth_factor, 1.0f, 30.0f);
  if (smooth_factor <= 1.001f) {
    return aimbot_clamp_angles(target_view_angles);
  }

  const Vec3 delta = aimbot_normalize_angle_delta(target_view_angles, source_view_angles);
  const float delta_length = std::hypot(delta.x, delta.y);
  if (delta_length <= 0.001f) {
    return aimbot_clamp_angles(target_view_angles);
  }

  const float tick_interval = global_vars != nullptr && global_vars->interval_per_tick > 0.0f
    ? global_vars->interval_per_tick
    : TICK_INTERVAL;
  const float smooth_ratio = (smooth_factor - 1.0f) / 29.0f;
  const float response = std::lerp(42.0f, 8.0f, smooth_ratio);
  const float min_speed = std::lerp(420.0f, 65.0f, smooth_ratio);
  const float max_speed = std::lerp(2200.0f, 360.0f, smooth_ratio);
  const float snap_fov = std::lerp(0.035f, 0.18f, smooth_ratio);

  if (delta_length <= snap_fov) {
    return aimbot_clamp_angles(target_view_angles);
  }

  const float eased_step = delta_length * (1.0f - std::exp(-response * tick_interval));
  const float min_step = std::min(delta_length, min_speed * tick_interval);
  const float max_step = std::max(min_step, max_speed * tick_interval);
  const float step = std::clamp(eased_step, min_step, max_step);
  return aimbot_step_towards_angles(source_view_angles, target_view_angles, step);
}

inline Vec3 aimbot_apply_assistive_angles(const Vec3& source_view_angles,
  const Vec3& target_view_angles,
  const Vec3& last_input_angles,
  const bool has_last_input_angles) {
  if (!has_last_input_angles) {
    return source_view_angles;
  }

  const Vec3 mouse_delta = aimbot_normalize_angle_delta(source_view_angles, last_input_angles);
  const Vec3 target_delta = aimbot_normalize_angle_delta(target_view_angles, last_input_angles);
  const float mouse_delta_length = std::hypot(mouse_delta.x, mouse_delta.y);
  const float target_delta_length = std::hypot(target_delta.x, target_delta.y);
  if (mouse_delta_length <= 0.0001f || target_delta_length <= 0.0001f) {
    return source_view_angles;
  }

  const float limited_length = std::min(mouse_delta_length, target_delta_length);
  const Vec3 limited_target_delta{
    target_delta.x * (limited_length / target_delta_length),
    target_delta.y * (limited_length / target_delta_length),
    0.0f
  };
  const float strength = aimbot_assist_strength(source_view_angles, target_view_angles);
  const Vec3 blended_delta{
    mouse_delta.x + ((limited_target_delta.x - mouse_delta.x) * strength),
    mouse_delta.y + ((limited_target_delta.y - mouse_delta.y) * strength),
    0.0f
  };
  return aimbot_clamp_angles(Vec3{
    source_view_angles.x - mouse_delta.x + blended_delta.x,
    source_view_angles.y - mouse_delta.y + blended_delta.y,
    0.0f
  });
}

inline Vec3 aimbot_apply_mode_angles(const Vec3& source_view_angles,
  const Vec3& target_view_angles,
  const Vec3& last_input_angles,
  const bool has_last_input_angles) {
  switch (config.aimbot.aim_mode) {
  case Aim::AimMode::SMOOTH:
    return aimbot_apply_smooth_angles(source_view_angles, target_view_angles);
  case Aim::AimMode::ASSISTIVE:
    return aimbot_apply_assistive_angles(source_view_angles, target_view_angles, last_input_angles, has_last_input_angles);
  default:
    return target_view_angles;
  }
}

inline bool aimbot_simple_move_sim_valid(Player* localplayer, Player* target, float horizon_seconds) {
  if (localplayer == nullptr || target == nullptr) return false;

  Vec3 predicted_origin = local_prediction_predict_entity_origin(target, horizon_seconds);
  Vec3 predicted_angles = aimbot_calculate_angles_to_position(localplayer->get_shoot_pos(), predicted_origin);
  float predicted_fov = aimbot_calculate_fov(predicted_angles, localplayer->get_punch_angles());
  return aimbot_fov_within_limit(predicted_fov, 1.25f, 4.0f) &&
         aimbot_trace_visible_to_position(localplayer, target, predicted_origin);
}

inline bool aimbot_simple_move_sim_valid_no_visibility(Player* localplayer, Player* target, float horizon_seconds) {
  if (localplayer == nullptr || target == nullptr) return false;

  Vec3 predicted_origin = local_prediction_predict_entity_origin(target, horizon_seconds);
  Vec3 predicted_angles = aimbot_calculate_angles_to_position(localplayer->get_shoot_pos(), predicted_origin);
  float predicted_fov = aimbot_calculate_fov(predicted_angles, localplayer->get_punch_angles());
  return aimbot_fov_within_limit(predicted_fov, 1.25f, 4.0f);
}

inline bool aimbot_should_auto_scope(Player* localplayer, Weapon* weapon, const aimbot_candidate& candidate) {
  if (!config.aimbot.auto_scope || localplayer == nullptr || weapon == nullptr || candidate.player == nullptr) return false;
  if (localplayer->get_tf_class() != tf_class::SNIPER || !weapon->is_sniper_rifle()) return false;
  if (aimbot_autoscope_scoped_state(localplayer) || !weapon->can_primary_attack()) return false;
  if (!localplayer->is_on_ground()) return false;
  if (candidate.distance > config.aimbot.auto_scope_threshold) return false;
  if (!candidate.visible) {
    return aimbot_simple_move_sim_valid_no_visibility(localplayer, candidate.player, 0.2f);
  }
  return aimbot_simple_move_sim_valid(localplayer, candidate.player, 0.2f);
}

inline bool aimbot_should_auto_unscope(Player* localplayer, Weapon* weapon, const aimbot_candidate& candidate) {
  if (!config.aimbot.auto_unscope || localplayer == nullptr || weapon == nullptr || candidate.player == nullptr) return false;
  if (localplayer->get_tf_class() != tf_class::SNIPER || !weapon->is_sniper_rifle()) return false;
  if (!aimbot_autoscope_scoped_state(localplayer)) return false;

  float scoped_time = (localplayer->get_tickbase() * TICK_INTERVAL) - localplayer->get_fov_time();
  if (scoped_time < 0.15f) return false;
  if (aimbot_candidate_visible_shootable(localplayer, candidate)) return false;
  if (!candidate.visible) return false;
  if (candidate.distance <= config.aimbot.auto_scope_threshold) return true;
  return !aimbot_simple_move_sim_valid(localplayer, candidate.player, 0.15f);
}

inline bool aimbot_should_auto_rev(Player* localplayer, Weapon* weapon, const aimbot_candidate& candidate) {
  if (!config.aimbot.auto_rev || localplayer == nullptr || weapon == nullptr || candidate.player == nullptr) return false;
  if (localplayer->get_tf_class() != tf_class::HEAVYWEAPONS || !weapon->is_minigun()) return false;
  if (localplayer->is_heavy_revved() || !weapon->can_secondary_attack()) return false;
  if (!localplayer->is_on_ground()) return false;
  if (!aimbot_candidate_visible_shootable(localplayer, candidate) && candidate.distance <= config.aimbot.auto_rev_threshold) return false;
  return aimbot_simple_move_sim_valid(localplayer, candidate.player, 0.15f);
}

inline bool aimbot_should_auto_unrev(Player* localplayer, Weapon* weapon, const aimbot_candidate& candidate) {
  if (!config.aimbot.auto_unrev || localplayer == nullptr || weapon == nullptr || candidate.player == nullptr) return false;
  if (localplayer->get_tf_class() != tf_class::HEAVYWEAPONS || !weapon->is_minigun()) return false;
  if (!localplayer->is_heavy_revved()) return false;
  if (aimbot_candidate_visible_shootable(localplayer, candidate)) return false;
  if (candidate.distance <= config.aimbot.auto_rev_threshold) return true;
  return !aimbot_simple_move_sim_valid(localplayer, candidate.player, 0.1f);
}

#endif
