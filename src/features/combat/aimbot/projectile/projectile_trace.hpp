/*
/^-----^\   data: 2026-05-16
V  o o  V  file: src/features/combat/aimbot/projectile/projectile_trace.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef PROJECTILE_TRACE_HPP
#define PROJECTILE_TRACE_HPP

#include "features/combat/aimbot/proj_aim/proj_aim_budget.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/entities/weapon.hpp"
#include "games/tf2/sdk/interfaces/engine_trace.hpp"

enum class projectile_trace_contract {
  fire_setup,
  spawn,
  world_block,
  radius_damage
};

inline unsigned int projectile_trace_mask(projectile_trace_contract contract) {
  switch (contract) {
  case projectile_trace_contract::fire_setup:
    return MASK_SOLID;
  case projectile_trace_contract::spawn:
    return MASK_SOLID_BRUSHONLY;
  case projectile_trace_contract::radius_damage:
    return MASK_RADIUS_DAMAGE;
  case projectile_trace_contract::world_block:
  default:
    return MASK_SOLID;
  }
}

inline bool projectile_trace_ray(const Vec3& start,
  const Vec3& end,
  const Vec3* hull_mins,
  const Vec3* hull_maxs,
  projectile_trace_contract contract,
  Entity* skip_entity,
  int skip_team,
  trace_t* trace_out) {
  if (engine_trace == nullptr || trace_out == nullptr) {
    return false;
  }

  const bool budgeted_trace = contract == projectile_trace_contract::world_block ||
    contract == projectile_trace_contract::radius_damage;
  if (budgeted_trace && !proj_aim_budget_try_trace_call()) {
    return false;
  }

  Vec3 local_mins{};
  Vec3 local_maxs{};
  ray_t ray{};
  if (hull_mins != nullptr && hull_maxs != nullptr) {
    local_mins = *hull_mins;
    local_maxs = *hull_maxs;
    ray = engine_trace->init_ray(
      const_cast<Vec3*>(&start),
      const_cast<Vec3*>(&end),
      &local_mins,
      &local_maxs);
  } else {
    ray = engine_trace->init_ray(const_cast<Vec3*>(&start), const_cast<Vec3*>(&end));
  }

  trace_filter filter{};
  switch (contract) {
  case projectile_trace_contract::fire_setup:
    engine_trace->init_projectile_fire_setup_trace_filter(&filter, skip_entity, skip_team);
    break;
  case projectile_trace_contract::spawn:
    engine_trace->init_projectile_spawn_trace_filter(&filter, skip_entity);
    break;
  case projectile_trace_contract::radius_damage:
    engine_trace->init_projectile_radius_damage_trace_filter(&filter, skip_entity, skip_team);
    break;
  case projectile_trace_contract::world_block:
  default:
    engine_trace->init_projectile_world_block_trace_filter(&filter, skip_entity);
    break;
  }

  engine_trace->trace_ray(&ray, projectile_trace_mask(contract), &filter, trace_out);
  return true;
}

inline bool projectile_trace_clear(const trace_t& trace, float clear_fraction = 0.97f) {
  return !trace.all_solid && !trace.start_solid && trace.fraction >= clear_fraction;
}

#endif
