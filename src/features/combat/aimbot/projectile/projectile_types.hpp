/*
/^-----^\   data: 2026-05-16
V  o o  V  file: src/features/combat/aimbot/projectile/projectile_types.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef PROJECTILE_TYPES_HPP
#define PROJECTILE_TYPES_HPP

#include <cfloat>
#include <vector>

#include "core/types.hpp"
#include "games/tf2/sdk/entities/entity.hpp"
#include "games/tf2/sdk/interfaces/engine_trace.hpp"

struct LocalPredictionProjectileParameters {
  float speed = 0.0f;
  float gravity = 0.0f;
  float max_time = 1.5f;
  float time_step = 0.015f;
};

struct LocalPredictionLaunchState {
  Vec3 origin{};
  Vec3 direction{};
  Vec3 view_angles{};
  Vec3 inherited_velocity{};
};

struct LocalPredictionProjectileStep {
  float time = 0.0f;
  Vec3 position{};
  Vec3 velocity{};
};

struct LocalPredictionProjectileTrace {
  bool valid = false;
  std::vector<LocalPredictionProjectileStep> steps{};
};

struct LocalPredictionInterceptResult {
  bool valid = false;
  bool has_target_base_origin = false;
  float intercept_time = 0.0f;
  float intercept_distance = FLT_MAX;
  float miss_distance = FLT_MAX;
  Vec3 aim_angles{};
  Vec3 target_origin{};
  Vec3 target_base_origin{};
  Vec3 target_offset{};
  Vec3 target_velocity{};
  LocalPredictionProjectileTrace trace{};
};

enum class projectile_sim_trace_mode {
  none,
  world,
  blocking_non_player,
  world_and_target
};

enum class projectile_sim_fire_setup_mode {
  traced_forward,
  pipe_style
};

enum class projectile_sim_spawn_trace_mode {
  none,
  line,
  hull
};

enum class projectile_sim_velocity_mode {
  forward,
  pipe_lift,
  cleaver
};

struct projectile_sim_profile {
  LocalPredictionProjectileParameters params{};
  Vec3 offset{};
  Vec3 hull{};
  Vec3 spawn_trace_mins{};
  Vec3 spawn_trace_maxs{};
  float lifetime = 0.0f;
  float initial_lift = 0.0f;
  float drag = 0.0f;
  unsigned int trace_mask = MASK_SOLID;
  projectile_sim_fire_setup_mode fire_setup_mode = projectile_sim_fire_setup_mode::traced_forward;
  projectile_sim_spawn_trace_mode spawn_trace_mode = projectile_sim_spawn_trace_mode::none;
  projectile_sim_velocity_mode velocity_mode = projectile_sim_velocity_mode::forward;
  bool valid = false;
  bool inherit_velocity = true;
  bool hull_trace = false;
  bool collide_world = true;
};

struct projectile_sim_launch {
  Vec3 origin{};
  Vec3 angles{};
  Vec3 direction{};
  Vec3 inherited_velocity{};
  bool valid = false;
};

struct projectile_sim_step {
  float time = 0.0f;
  Vec3 position{};
  Vec3 velocity{};
  trace_t trace{};
  bool hit = false;
  bool hit_target = false;
};

struct projectile_sim_result {
  bool valid = false;
  bool hit = false;
  bool hit_target = false;
  float hit_time = 0.0f;
  Vec3 hit_position{};
  Vec3 hit_normal{};
  void* hit_entity = nullptr;
  std::vector<projectile_sim_step> steps{};
};

struct projectile_simulation {
  projectile_sim_profile profile{};
  projectile_sim_launch launch{};
  Entity* skip_entity = nullptr;
  Entity* target_entity = nullptr;
  projectile_sim_trace_mode trace_mode = projectile_sim_trace_mode::none;
  projectile_sim_result result{};
  Vec3 position{};
  Vec3 velocity{};
  float time = 0.0f;
  int tick = 0;
  int max_ticks = 0;
  bool initialized = false;
  bool finished = false;

  bool init(const projectile_sim_launch& launch_in,
    const projectile_sim_profile& profile_in,
    Entity* skip_entity_in = nullptr,
    Entity* target_entity_in = nullptr,
    projectile_sim_trace_mode trace_mode_in = projectile_sim_trace_mode::none);
  bool step();
  projectile_sim_result run();
};

#endif
