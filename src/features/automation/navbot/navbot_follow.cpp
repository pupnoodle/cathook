/*
/^-----^\   data: 2026-04-05
V  o o  V  file: src/features/automation/navbot/navbot_follow.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "features/automation/navbot/navbot_follow.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "features/combat/aimbot/aim_utils.hpp"
#include "features/combat/aimbot/aimbot.hpp"
#include "features/menu/config.hpp"

#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/engine_trace.hpp"
#include "games/tf2/sdk/interfaces/client.hpp"

namespace navbot
{

namespace
{

float distance_sq_2d_follow(const Vec3& left, const Vec3& right)
{
  auto dx = left.x - right.x;
  auto dy = left.y - right.y;
  return dx * dx + dy * dy;
}

float vertical_distance(const Vec3& left, const Vec3& right)
{
  return std::fabs(left.z - right.z);
}

float crumb_reach_distance_for(const path_result& path, size_t crumb_index)
{
  if (crumb_index >= path.crumbs.size())
  {
    return crumb_reach_distance;
  }

  if (path.crumbs[crumb_index].kind == crumb_kind::destination)
  {
    return path.destination_reach_distance;
  }

  return crumb_reach_distance;
}

bool is_crumb_reached(const path_result& path, size_t crumb_index, const Vec3& origin)
{
  if (crumb_index >= path.crumbs.size())
  {
    return false;
  }

  const auto& target = path.crumbs[crumb_index].world;
  const auto reach_distance = crumb_reach_distance_for(path, crumb_index);
  if (distance_sq_2d_follow(origin, target) > reach_distance * reach_distance)
  {
    return false;
  }

  return vertical_distance(origin, target) <= crumb_reach_vertical_tolerance;
}

float distance_sq_to_segment_2d(const Vec3& point, const Vec3& start, const Vec3& end, float* fraction_out)
{
  auto segment_x = end.x - start.x;
  auto segment_y = end.y - start.y;
  auto segment_length_sq = segment_x * segment_x + segment_y * segment_y;
  if (segment_length_sq <= 1.0f)
  {
    if (fraction_out != nullptr)
    {
      *fraction_out = 0.0f;
    }
    return distance_sq_2d_follow(point, start);
  }

  auto point_x = point.x - start.x;
  auto point_y = point.y - start.y;
  auto fraction = ((point_x * segment_x) + (point_y * segment_y)) / segment_length_sq;
  fraction = std::clamp(fraction, 0.0f, 1.0f);
  if (fraction_out != nullptr)
  {
    *fraction_out = fraction;
  }

  auto closest = Vec3{
    start.x + segment_x * fraction,
    start.y + segment_y * fraction,
    start.z + (end.z - start.z) * fraction
  };
  return distance_sq_2d_follow(point, closest);
}

float segment_z_at_fraction(const Vec3& start, const Vec3& end, float fraction)
{
  return start.z + ((end.z - start.z) * fraction);
}

bool did_hit_trace(const trace_t& trace)
{
  return trace.fraction < 1.0f || trace.all_solid || trace.start_solid;
}

bool trace_navigation_ray(Player* localplayer, const Vec3& start, const Vec3& end)
{
  if (engine_trace == nullptr || localplayer == nullptr)
  {
    return true;
  }

  auto delta = Vec3{
    end.x - start.x,
    end.y - start.y,
    0.0f
  };
  auto length_sq = delta.x * delta.x + delta.y * delta.y;
  if (length_sq <= 1.0f)
  {
    return true;
  }

  auto planar_distance = std::sqrt(length_sq);
  auto vertical_delta = end.z - start.z;

  auto mins = localplayer->get_player_mins(localplayer->is_ducking());
  auto maxs = localplayer->get_player_maxs(localplayer->is_ducking());

  const auto is_climbing = vertical_delta > player_step_height
    && !(vertical_delta <= walkable_ramp_height && planar_distance >= walkable_ramp_run);

  if (!is_climbing)
  {
    auto walk_start = Vec3{start.x, start.y, start.z + player_step_height};
    auto walk_end = Vec3{end.x, end.y, end.z + player_step_height};
    auto walk_trace = trace_t{};
    engine_trace->trace_hull(&walk_start, &walk_end, &mins, &maxs, MASK_PLAYERSOLID, &walk_trace);
    if (did_hit_trace(walk_trace))
    {
      return false;
    }
  }

  if (is_climbing)
  {
    auto lifted_start = Vec3{start.x, start.y, start.z + player_jump_height};
    auto lifted_end = Vec3{end.x, end.y, end.z + player_jump_height};
    auto jump_trace = trace_t{};
    engine_trace->trace_hull(&lifted_start, &lifted_end, &mins, &maxs, MASK_PLAYERSOLID, &jump_trace);
    if (did_hit_trace(jump_trace))
    {
      return false;
    }
  }

  return true;
}

bool is_transition_passable(const path_result& path, size_t crumb_index, Player* localplayer)
{
  if (crumb_index + 1 >= path.crumbs.size())
  {
    return true;
  }

  return trace_navigation_ray(localplayer, path.crumbs[crumb_index].world, path.crumbs[crumb_index + 1].world);
}

bool navbot_requires_jump(Player* localplayer, const Vec3& target)
{
  if (localplayer == nullptr)
  {
    return false;
  }

  auto delta = Vec3{
    target.x - localplayer->get_origin().x,
    target.y - localplayer->get_origin().y,
    target.z - localplayer->get_origin().z
  };
  auto planar_distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);

  return delta.z > jump_trigger_height
    && planar_distance <= jump_trigger_run
    && (localplayer->get_flags() & FL_ONGROUND) != 0;
}

bool navbot_is_in_water_transition(Player* localplayer)
{
  if (localplayer == nullptr)
  {
    return false;
  }

  auto flags = localplayer->get_flags();
  return (flags & FL_INWATER) != 0 || (flags & FL_WATERJUMP) != 0;
}

bool navbot_engage_target_visible(Player* localplayer)
{
  if (localplayer == nullptr || target_player == nullptr)
  {
    return false;
  }

  if (target_player->is_dormant() || !target_player->is_alive() || target_player->is_invulnerable())
  {
    return false;
  }

  Vec3 target_position{};
  if (!target_player->get_hitbox_center(aim_hitbox_spine_2, &target_position))
  {
    target_position = target_player->get_bone_pos(target_player->get_head_bone());
  }

  return aimbot_trace_visible_to_position(localplayer, target_player, target_position)
    && localplayer->can_shoot(target_player);
}

bool navbot_auto_engage_can_block_jump(Player* localplayer, Weapon* weapon)
{
  if (localplayer == nullptr || weapon == nullptr || !navbot_engage_target_visible(localplayer))
  {
    return false;
  }

  if (localplayer->get_tf_class() == tf_class::SNIPER && weapon->is_sniper_rifle())
  {
    return config.aimbot.auto_scope || config.aimbot.auto_unscope;
  }

  if (localplayer->get_tf_class() == tf_class::HEAVYWEAPONS && weapon->is_minigun())
  {
    return config.aimbot.auto_rev || config.aimbot.auto_unrev;
  }

  return false;
}

bool navbot_release_auto_engage(Player* localplayer, Weapon* weapon, user_cmd* user_cmd)
{
  if (localplayer == nullptr || weapon == nullptr || user_cmd == nullptr)
  {
    return false;
  }

  if (localplayer->get_tf_class() == tf_class::SNIPER
    && weapon->is_sniper_rifle()
    && localplayer->is_scoped()
    && (config.aimbot.auto_scope || config.aimbot.auto_unscope))
  {
    user_cmd->buttons |= IN_ATTACK2;
    return true;
  }

  if (localplayer->get_tf_class() == tf_class::HEAVYWEAPONS
    && weapon->is_minigun()
    && localplayer->is_heavy_revved()
    && (config.aimbot.auto_rev || config.aimbot.auto_unrev))
  {
    user_cmd->buttons |= IN_ATTACK2;
    return true;
  }

  return false;
}

void apply_walk_towards(Player* localplayer, user_cmd* user_cmd, const Vec3& target)
{
  auto local_origin = localplayer->get_origin();
  auto delta = Vec3{
    target.x - local_origin.x,
    target.y - local_origin.y,
    target.z - local_origin.z
  };

  auto yaw = std::atan2(delta.y, delta.x) * 57.295779513082f;
  auto yaw_delta = (yaw - user_cmd->view_angles.y) * 0.017453293f;
  auto planar_distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);

  const auto speed_scale = std::clamp(planar_distance / crumb_reach_distance, 0.25f, 1.0f);
  const auto move_speed = follower_move_speed * speed_scale;
  user_cmd->forwardmove = std::cos(yaw_delta) * move_speed;
  user_cmd->sidemove = -std::sin(yaw_delta) * move_speed;

  if (delta.z > jump_trigger_height
    && planar_distance <= jump_trigger_run
    && (localplayer->get_flags() & FL_ONGROUND) != 0)
  {
    user_cmd->buttons |= IN_JUMP;
  }
}

nav_edge_id find_failed_edge(const navbot_mesh& mesh, const path_result& path, size_t crumb_index)
{
  if (path.area_path.size() < 2 || crumb_index >= path.crumbs.size())
  {
    return {};
  }

  auto edge_index = crumb_index / 2;
  if (edge_index >= path.area_path.size() - 1)
  {
    return {};
  }

  auto from_area = path.area_path[edge_index];
  auto to_area = path.area_path[edge_index + 1];
  auto connection_index = mesh.find_connection_index(from_area, to_area);
  if (!connection_index.has_value())
  {
    return {};
  }

  return nav_edge_id{from_area.value, *connection_index};
}

void fill_failure_result(follower_tick_result& result,
  follower_failure_reason reason,
  const navbot_mesh& mesh,
  const path_result& path,
  size_t crumb_index)
{
  result.failed = true;
  result.failure_reason = reason;
  result.failed_edge = find_failed_edge(mesh, path, crumb_index);
  result.failed_crumb_index = static_cast<uint32_t>(crumb_index);

  if (crumb_index < path.crumbs.size())
  {
    result.failed_crumb_area = path.crumbs[crumb_index].area_id;
    if (!result.failed_crumb_area.valid() && crumb_index > 0)
    {
      result.failed_crumb_area = path.crumbs[crumb_index - 1].area_id;
    }
  }
}

} // namespace

void navbot_follow::mark_crumb_reached(size_t crumb_index, float current_time)
{
  if (crumb_index < reached_crumb_times_.size())
  {
    reached_crumb_times_[crumb_index] = current_time;
  }
}

void navbot_follow::advance_to_crumb(size_t crumb_index, float current_time)
{
  auto next_index = std::min(crumb_index, active_path_.crumbs.size());
  while (current_crumb_index_ < next_index)
  {
    mark_crumb_reached(current_crumb_index_, current_time);
    ++current_crumb_index_;
  }

  current_crumb_start_time_ = current_time;
  last_progress_time_ = current_time;
  last_progress_distance_sq_ = 0.0f;
  duck_jump_active_ = false;
  duck_jump_ticks_ = 0;
}

size_t navbot_follow::find_skip_ahead_crumb(Player* localplayer, const Vec3& local_origin) const
{
  if (localplayer == nullptr || !has_path())
  {
    return current_crumb_index_;
  }

  constexpr size_t max_skip_lookahead = 8;
  const auto last_crumb_index = active_path_.crumbs.size() - 1;
  const auto max_index = std::min(last_crumb_index, current_crumb_index_ + max_skip_lookahead);

  for (auto crumb_index = max_index; crumb_index > current_crumb_index_; --crumb_index)
  {
    if (is_crumb_reached(active_path_, crumb_index, local_origin))
    {
      return std::min(crumb_index + 1, active_path_.crumbs.size());
    }
  }

  auto best_index = current_crumb_index_;
  for (auto segment_index = current_crumb_index_; segment_index < max_index; ++segment_index)
  {
    const auto& start = active_path_.crumbs[segment_index].world;
    const auto& end = active_path_.crumbs[segment_index + 1].world;
    auto fraction = 0.0f;
    const auto segment_distance_sq = distance_sq_to_segment_2d(local_origin, start, end, &fraction);
    if (fraction < 0.35f)
    {
      continue;
    }

    const auto segment_z = segment_z_at_fraction(start, end, fraction);
    if (std::fabs(local_origin.z - segment_z) > crumb_reach_vertical_tolerance)
    {
      continue;
    }

    if (segment_distance_sq <= crumb_skip_segment_distance * crumb_skip_segment_distance)
    {
      best_index = std::max(best_index, segment_index + 1);
    }
  }

  return best_index;
}

void navbot_follow::clear()
{
  active_path_ = path_result{};
  reached_crumb_times_.clear();
  current_crumb_index_ = 0;
  current_crumb_start_time_ = 0.0f;
  last_progress_time_ = 0.0f;
  last_progress_distance_sq_ = 0.0f;
  last_vischeck_time_ = 0.0f;
  last_jump_time_ = 0.0f;
  duck_jump_active_ = false;
  duck_jump_ticks_ = 0;
}

void navbot_follow::set_path(path_result path)
{
  active_path_ = std::move(path);
  reached_crumb_times_.assign(active_path_.crumbs.size(), 0.0f);
  current_crumb_index_ = 0;
  current_crumb_start_time_ = 0.0f;
  last_progress_time_ = 0.0f;
  last_progress_distance_sq_ = 0.0f;
  last_vischeck_time_ = 0.0f;
  last_jump_time_ = 0.0f;
  duck_jump_active_ = false;
  duck_jump_ticks_ = 0;
}

bool navbot_follow::has_path() const
{
  return active_path_.status == path_status::success && !active_path_.crumbs.empty() && current_crumb_index_ < active_path_.crumbs.size();
}

const std::vector<crumb>& navbot_follow::crumbs() const
{
  return active_path_.crumbs;
}

const crumb* navbot_follow::current_crumb() const
{
  if (!has_path())
  {
    return nullptr;
  }

  return &active_path_.crumbs[current_crumb_index_];
}

uint32_t navbot_follow::generation_id() const
{
  return active_path_.generation_id;
}

size_t navbot_follow::current_crumb_index() const
{
  return current_crumb_index_;
}

const std::vector<float>& navbot_follow::reached_crumb_times() const
{
  return reached_crumb_times_;
}

bool navbot_follow::try_unstuck(Player* localplayer, user_cmd* user_cmd, const crumb& current_crumb, float current_time, follower_tick_result& result)
{
  if (localplayer == nullptr || user_cmd == nullptr)
  {
    return false;
  }

  if ((localplayer->get_flags() & FL_ONGROUND) == 0)
  {
    if (duck_jump_active_)
    {
      user_cmd->buttons |= IN_DUCK;
      ++duck_jump_ticks_;
      result.attempted_unstuck = true;
      return true;
    }

    return false;
  }

  if (duck_jump_active_)
  {
    if (duck_jump_ticks_ > 3)
    {
      duck_jump_active_ = false;
      duck_jump_ticks_ = 0;
      last_jump_time_ = current_time;
    }
    else
    {
      user_cmd->buttons |= IN_DUCK;
      ++duck_jump_ticks_;
      result.attempted_unstuck = true;
      return true;
    }
  }

  if (current_time - last_progress_time_ < stuck_jump_retry_time)
  {
    return false;
  }

  if (current_time - last_jump_time_ < stuck_jump_interval)
  {
    return false;
  }

  auto local_origin = localplayer->get_origin();
  auto height_delta = current_crumb.world.z - local_origin.z;
  if (height_delta > player_step_height || current_time - last_progress_time_ >= stuck_fail_time)
  {
    user_cmd->buttons |= IN_JUMP;
    if (height_delta > jump_trigger_height)
    {
      user_cmd->buttons |= IN_DUCK;
    }

    duck_jump_active_ = true;
    duck_jump_ticks_ = 0;
    last_jump_time_ = current_time;
    result.attempted_unstuck = true;
    return true;
  }

  return false;
}

follower_tick_result navbot_follow::tick(const navbot_mesh& mesh, Player* localplayer, user_cmd* user_cmd, float current_time)
{
  follower_tick_result result{};
  if (localplayer == nullptr || user_cmd == nullptr)
  {
    return result;
  }

  if (!has_path())
  {
    return result;
  }

  if (current_crumb_start_time_ <= 0.0f)
  {
    current_crumb_start_time_ = current_time;
    last_progress_time_ = current_time;
  }

  auto local_origin = localplayer->get_origin();
  while (current_crumb_index_ < active_path_.crumbs.size())
  {
    if (!is_crumb_reached(active_path_, current_crumb_index_, local_origin))
    {
      break;
    }

    advance_to_crumb(current_crumb_index_ + 1, current_time);
    if (!has_path())
    {
      clear();
      return result;
    }
  }

  if (current_crumb_index_ + 1 < active_path_.crumbs.size())
  {
    if (is_crumb_reached(active_path_, current_crumb_index_ + 1, local_origin))
    {
      advance_to_crumb(current_crumb_index_ + 2, current_time);
      if (!has_path())
      {
        clear();
        return result;
      }
    }
  }

  if (!has_path())
  {
    return result;
  }

  auto skip_index = find_skip_ahead_crumb(localplayer, local_origin);
  if (skip_index > current_crumb_index_)
  {
    advance_to_crumb(skip_index, current_time);
    if (!has_path())
    {
      clear();
      return result;
    }
  }

  if (active_path_.goal == goal_type::hold_range_on_enemy && current_crumb_index_ + 1 < active_path_.crumbs.size())
  {
    auto current_target = active_path_.crumbs[current_crumb_index_].world;
    auto next_target = active_path_.crumbs[current_crumb_index_ + 1].world;
    auto current_distance_sq = distance_sq_2d_follow(local_origin, current_target);
    auto next_distance_sq = distance_sq_2d_follow(local_origin, next_target);
    if (next_distance_sq + 256.0f < current_distance_sq)
    {
      advance_to_crumb(current_crumb_index_ + 1, current_time);
      if (!has_path())
      {
        clear();
        return result;
      }
    }
  }

  auto current_target = active_path_.crumbs[current_crumb_index_].world;
  auto current_distance_sq = distance_sq_2d_follow(local_origin, current_target);

  const auto near_target_distance = crumb_reach_distance * 2.0f;
  const auto vischeck_interval = current_distance_sq <= near_target_distance * near_target_distance ? 0.05f : 0.15f;
  if (current_time - last_vischeck_time_ >= vischeck_interval)
  {
    last_vischeck_time_ = current_time;
    if (!is_transition_passable(active_path_, current_crumb_index_, localplayer))
    {
      fill_failure_result(result, follower_failure_reason::hazard_intersection, mesh, active_path_, current_crumb_index_);
      return result;
    }
  }

  if (last_progress_distance_sq_ <= 0.0f || current_distance_sq + 64.0f < last_progress_distance_sq_)
  {
    last_progress_distance_sq_ = current_distance_sq;
    last_progress_time_ = current_time;
    duck_jump_active_ = false;
    duck_jump_ticks_ = 0;
  }

  auto* weapon = localplayer->get_weapon();
  auto needs_jump = navbot_requires_jump(localplayer, current_target);
  auto in_water = navbot_is_in_water_transition(localplayer);

  if (needs_jump)
  {
    if (navbot_auto_engage_can_block_jump(localplayer, weapon))
    {
      fill_failure_result(result, follower_failure_reason::hazard_intersection, mesh, active_path_, current_crumb_index_);
      return result;
    }

    if (navbot_release_auto_engage(localplayer, weapon, user_cmd))
    {
      result.has_movement = true;
      return result;
    }
  }
  else if (in_water && navbot_release_auto_engage(localplayer, weapon, user_cmd))
  {
    result.has_movement = true;
    return result;
  }

  apply_walk_towards(localplayer, user_cmd, current_target);

  if (try_unstuck(localplayer, user_cmd, active_path_.crumbs[current_crumb_index_], current_time, result))
  {
    result.has_movement = true;
    return result;
  }

  if (current_time - last_progress_time_ > stuck_fail_time)
  {
    fill_failure_result(result, follower_failure_reason::no_progress, mesh, active_path_, current_crumb_index_);
    return result;
  }

  if (current_time - current_crumb_start_time_ > blocked_fail_time)
  {
    fill_failure_result(result, follower_failure_reason::blocked, mesh, active_path_, current_crumb_index_);
    return result;
  }

  result.has_movement = true;
  return result;
}

} // namespace navbot
