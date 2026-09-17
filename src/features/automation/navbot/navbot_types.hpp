/*
/^-----^\   data: 2026-04-05
V  o o  V  file: src/features/automation/navbot/navbot_types.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef NAVBOT_TYPES_HPP
#define NAVBOT_TYPES_HPP
#include <atomic>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "core/types.hpp"

namespace navbot
{

constexpr float player_width = 49.0f;
constexpr float half_player_width = player_width * 0.5f;
constexpr float player_clearance_margin = 4.0f;
constexpr float player_jump_height = 72.0f;
constexpr float player_ledge_mount_height = 64.0f;
constexpr float player_step_height = 18.0f;
constexpr float jump_trigger_height = 28.0f;
constexpr float jump_trigger_run = 60.0f;
constexpr float crumb_reach_distance = 50.0f;
constexpr float crumb_reach_up_tolerance = player_step_height + 4.0f;
constexpr float crumb_reach_drop_tolerance = player_jump_height * 4.0f;
constexpr float crumb_skip_segment_distance = 64.0f;
constexpr float pickup_destination_reach_distance = 20.0f;

constexpr float melee_destination_clearance = 1.0f;
constexpr float melee_destination_reach_distance = player_width + melee_destination_clearance;
constexpr float melee_chase_switch_distance = 125.0f;
constexpr float melee_chase_vertical_limit = 80.0f;
constexpr float melee_chase_spy_approach_distance = 250.0f;
constexpr float enemy_yolo_reach_distance = 500.0f;
constexpr float follower_move_speed = 450.0f;
constexpr float stuck_jump_interval = 0.2f;
constexpr float stuck_jump_retry_time = 0.5f;
constexpr float stuck_progress_speed_threshold = 32.0f;
constexpr float stuck_fail_time = 1.25f;
constexpr float blocked_fail_time = 4.0f;

struct nav_area_id
{
  uint32_t value = 0;

  constexpr bool valid() const
  {
    return value != 0;
  }
};

struct nav_edge_id
{
  uint32_t from_area = 0;
  uint16_t connection_index = 0;
};

enum class goal_type : uint8_t
{
  get_health,
  get_ammo,
  capture_objective,
  push_payload,
  defend_payload,
  get_flag,
  return_flag,
  escape_danger,
  hold_range_on_enemy,
  melee_chase,
  sentry_snipe,
  engineer_build,
  engineer_maintain,
  reload_weapons,
  roam,
  heal_follow,
  mvm_tank,
  mvm_combat,
  mvm_money,
  mvm_frontline,
  mvm_teleporter,
  mvm_upgrade_station,
  followbot,
  command_path
};

enum class crumb_kind : uint8_t
{
  area_center,
  transition_center,
  dropdown_approach,
  destination
};

struct crumb
{
  nav_area_id area_id{};
  nav_edge_id edge_id{};
  Vec3 world{};
  crumb_kind kind = crumb_kind::area_center;
};

enum class path_status : uint8_t
{
  success,
  no_start_area,
  no_goal_area,
  no_path,
  canceled,
  stale,
  failed
};

struct path_request
{
  uint64_t request_id = 0;
  uint32_t generation_id = 0;
  uint32_t world_generation = 0;
  goal_type goal = goal_type::roam;
  nav_area_id start_area{};
  nav_area_id goal_area{};
  Vec3 start_world{};
  Vec3 goal_world{};
  uint32_t team = 0;
  uint32_t class_id = 0;
  uint32_t hazard_generation = 0;
  int captured_point_index = -1;
  float destination_reach_distance = crumb_reach_distance;
  bool setup_finished = false;
  bool require_exact_goal_area = false;
};

struct path_result
{
  uint64_t request_id = 0;
  uint32_t generation_id = 0;
  uint32_t world_generation = 0;
  uint32_t hazard_generation = 0;
  goal_type goal = goal_type::roam;
  path_status status = path_status::failed;
  float destination_reach_distance = crumb_reach_distance;
  float solve_time_ms = 0.0f;
  std::vector<nav_area_id> area_path{};
  std::vector<crumb> crumbs{};
};

enum class hazard_kind : uint8_t
{
  static_blocked,
  sentry,
  sticky,
  enemy_pressure,
  transition_failure,
  crumb_blacklist
};

enum class hazard_policy : uint8_t
{
  hard_block,
  soft_cost,
  temporary_forbid
};

struct hazard_record
{
  hazard_kind kind = hazard_kind::enemy_pressure;
  hazard_policy policy = hazard_policy::soft_cost;
  nav_area_id area_id{};
  nav_edge_id edge_id{};
  float cost = 0.0f;
  float expire_time = 0.0f;
};

constexpr uint32_t goal_type_bit(goal_type type)
{
  return 1u << static_cast<uint32_t>(type);
}

constexpr size_t goal_type_count = static_cast<size_t>(goal_type::command_path) + 1;

constexpr size_t goal_type_index(goal_type type)
{
  return static_cast<size_t>(type);
}

constexpr bool goal_type_can_be_excluded(goal_type type)
{
  return type != goal_type::roam && type != goal_type::command_path;
}

struct goal_candidate
{
  goal_type type = goal_type::roam;
  float score = 0.0f;
  Vec3 destination{};
  nav_area_id destination_area{};
  int entity_index = 0;
  bool rejected = false;
};

struct goal_rejection
{
  goal_type type = goal_type::roam;
  nav_area_id destination_area{};
  Vec3 destination{};
  int entity_index = 0;
  float expire_time = 0.0f;
};

enum class follower_failure_reason : uint8_t
{
  none,
  blocked,
  no_progress,
  invalid_local_area,
  destination_invalid,
  stale_path,
  hazard_intersection
};

struct follower_tick_result
{
  bool has_movement = false;
  bool failed = false;
  bool attempted_unstuck = false;
  follower_failure_reason failure_reason = follower_failure_reason::none;
  nav_edge_id failed_edge{};
  nav_area_id failed_crumb_area{};
  uint32_t failed_crumb_index = 0;
};

struct job_handle
{
  uint64_t id = 0;
  uint32_t generation_id = 0;
};

struct navbot_job_availability
{
  bool enabled = false;
  bool candidate_available = false;
};

struct navbot_debug_state
{
  goal_type current_goal = goal_type::roam;
  uint32_t active_generation_id = 0;
  uint32_t active_world_generation = 0;
  uint32_t active_hazard_generation = 0;
  uint32_t pending_generation_id = 0;
  path_status current_path_status = path_status::failed;
  follower_failure_reason last_failure = follower_failure_reason::none;
  float last_solve_time_ms = 0.0f;
  uint32_t rejected_job_count = 0;
  uint32_t stale_result_count = 0;
  int captured_point_index = -1;
  uint32_t mini_round_mask = 0;
  bool mesh_ready = false;
  bool goal_valid = false;
  bool has_active_path = false;
  bool setup_finished = false;
  uint32_t active_crumb_count = 0;
  std::array<navbot_job_availability, goal_type_count> job_availability{};
  std::string map_name{};
  std::string runtime_state{};
  std::string nav_file_path{};
  std::string path_request_message{};
};

struct navbot_goal_state
{
  bool valid = false;
  float score = -1.0f;
  goal_candidate goal{};
};

struct cancellation_token
{
  uint64_t id = 0;
  std::shared_ptr<std::atomic_bool> canceled{};

  [[nodiscard]] bool is_canceled() const
  {
    return canceled != nullptr && canceled->load(std::memory_order_relaxed);
  }

  void cancel() const
  {
    if (canceled != nullptr)
    {
      canceled->store(true, std::memory_order_relaxed);
    }
  }
};

}
#endif
