/*
/^-----^\   data: 2026-04-05
V  o o  V  file: src/features/automation/navbot/navbot_path.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "features/automation/navbot/navbot_path.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <queue>

#include "core/math/math.hpp"

namespace navbot
{

namespace
{

struct path_node
{
  float g_cost = std::numeric_limits<float>::max();
  float f_cost = std::numeric_limits<float>::max();
  uint32_t parent_index = 0;
  bool visited = false;
};

struct queue_entry
{
  uint32_t area_index = 0;
  float f_cost = 0.0f;
  float g_cost = 0.0f;
};

struct queue_entry_less
{
  bool operator()(const queue_entry& left, const queue_entry& right) const
  {
    if (left.f_cost != right.f_cost)
    {
      return left.f_cost > right.f_cost;
    }
    if (left.g_cost != right.g_cost)
    {
      return left.g_cost > right.g_cost;
    }
    return left.area_index > right.area_index;
  }
};

std::vector<nav_area_id> reconstruct_area_path(const nav_mesh_cache& cache, const std::vector<path_node>& nodes, uint32_t start_index, uint32_t goal_index)
{
  auto area_path = std::vector<nav_area_id>{};
  for (auto index = goal_index;; index = nodes[index].parent_index)
  {
    area_path.push_back(cache.areas[index].id);
    if (index == start_index)
    {
      break;
    }
  }

  std::reverse(area_path.begin(), area_path.end());
  return area_path;
}

float distance_sq(const Vec3& left, const Vec3& right)
{
  auto dx = left.x - right.x;
  auto dy = left.y - right.y;
  auto dz = left.z - right.z;
  return dx * dx + dy * dy + dz * dz;
}

float distance_value(const Vec3& left, const Vec3& right)
{
  return std::sqrt(distance_sq(left, right));
}

Vec3 normalize_2d(const Vec3& value)
{
  auto length_sq = value.x * value.x + value.y * value.y;
  if (length_sq <= 0.0001f)
  {
    return {};
  }

  auto inv_length = 1.0f / std::sqrt(length_sq);
  return Vec3{value.x * inv_length, value.y * inv_length, 0.0f};
}

bool use_other_center(const Vec3& center_point, const Vec3& area_center, const Vec3& next_center)
{
  return center_point.x != area_center.x
    && center_point.y != area_center.y
    && center_point.x != next_center.x
    && center_point.y != next_center.y;
}

struct portal_info
{
  Vec3 center{};
  float half_width = 0.0f;
  uint8_t direction = nav_direction_north;
  bool valid = false;
};

portal_info compute_portal(const nav_area_data& from_area, const nav_area_data& to_area, uint8_t direction)
{
  portal_info portal{};
  portal.direction = direction;

  if (direction == nav_direction_north || direction == nav_direction_south)
  {
    portal.center.y = (direction == nav_direction_north) ? from_area.nw_corner.y : from_area.se_corner.y;

    auto left = std::max(from_area.nw_corner.x, to_area.nw_corner.x);
    auto right = std::min(from_area.se_corner.x, to_area.se_corner.x);
    left = std::clamp(left, from_area.nw_corner.x, from_area.se_corner.x);
    right = std::clamp(right, from_area.nw_corner.x, from_area.se_corner.x);

    if (right < left)
    {
      return portal;
    }

    portal.center.x = (left + right) * 0.5f;
    portal.half_width = (right - left) * 0.5f;
  }
  else
  {
    portal.center.x = (direction == nav_direction_west) ? from_area.nw_corner.x : from_area.se_corner.x;

    auto top = std::max(from_area.nw_corner.y, to_area.nw_corner.y);
    auto bottom = std::min(from_area.se_corner.y, to_area.se_corner.y);
    top = std::clamp(top, from_area.nw_corner.y, from_area.se_corner.y);
    bottom = std::clamp(bottom, from_area.nw_corner.y, from_area.se_corner.y);

    if (bottom < top)
    {
      return portal;
    }

    portal.center.y = (top + bottom) * 0.5f;
    portal.half_width = (bottom - top) * 0.5f;
  }

  portal.valid = true;
  return portal;
}

} // namespace

bool nav_area_has_clearance(const nav_area_data& area)
{
  const auto required_width = player_width + (player_clearance_margin * 2.0f);
  return (area.maxs.x - area.mins.x) >= required_width
      && (area.maxs.y - area.mins.y) >= required_width;
}

bool nav_transition_has_clearance(const nav_area_data& from_area, const nav_area_data& to_area)
{
  if (!nav_area_has_clearance(from_area) || !nav_area_has_clearance(to_area))
  {
    return false;
  }

  const auto overlap_x = std::min(from_area.maxs.x, to_area.maxs.x) - std::max(from_area.mins.x, to_area.mins.x);
  const auto overlap_y = std::min(from_area.maxs.y, to_area.maxs.y) - std::max(from_area.mins.y, to_area.mins.y);
  const auto center_delta_x = std::fabs(to_area.center.x - from_area.center.x);
  const auto center_delta_y = std::fabs(to_area.center.y - from_area.center.y);
  const auto required_width = player_width + (player_clearance_margin * 2.0f);

  if (center_delta_x > center_delta_y)
  {
    return overlap_y >= required_width;
  }

  if (center_delta_y > center_delta_x)
  {
    return overlap_x >= required_width;
  }

  return std::max(overlap_x, overlap_y) >= required_width;
}

Vec3 clamp_point_to_player_clearance(const nav_area_data& area, const Vec3& point)
{
  const auto inset = half_player_width + player_clearance_margin;
  auto clamped = point;

  if ((area.maxs.x - area.mins.x) > inset * 2.0f)
  {
    clamped.x = std::clamp(clamped.x, area.mins.x + inset, area.maxs.x - inset);
  }
  else
  {
    clamped.x = area.center.x;
  }

  if ((area.maxs.y - area.mins.y) > inset * 2.0f)
  {
    clamped.y = std::clamp(clamped.y, area.mins.y + inset, area.maxs.y - inset);
  }
  else
  {
    clamped.y = area.center.y;
  }

  return clamped;
}

transition_points build_transition_points(const navbot_mesh& mesh, nav_area_id current_area, nav_area_id next_area)
{
  auto current = mesh.find_area(current_area);
  auto next = mesh.find_area(next_area);
  if (current == nullptr || next == nullptr)
  {
    return {};
  }

  auto area_center = current->center;
  auto next_center = next->center;

  auto center_point = Vec3{};
  auto center_next = Vec3{};
  auto use_portal = false;

  auto connection_index = mesh.find_connection_index(current_area, next_area);
  if (connection_index.has_value() && *connection_index < current->connections.size())
  {
    auto direction = current->connections[*connection_index].direction;
    auto portal = compute_portal(*current, *next, direction);
    if (portal.valid)
    {
      center_point = mesh.get_nearest_point(current_area, portal.center);
      center_next = mesh.get_nearest_point(next_area, portal.center);
      use_portal = true;
    }
  }

  if (!use_portal)
  {
    auto area_closest = mesh.get_nearest_point(current_area, next_center);
    auto next_closest = mesh.get_nearest_point(next_area, area_center);

    center_point = area_closest;
    if (use_other_center(center_point, area_center, next_center))
    {
      center_point = next_closest;
      center_point.z = mesh.get_nearest_point(current_area, next_closest).z;
    }
    center_next = mesh.get_nearest_point(next_area, center_point);
  }

  return transition_points{
    clamp_point_to_player_clearance(*current, area_center),
    center_point,
    center_next,
    clamp_point_to_player_clearance(*next, next_center)
  };
}

Vec3 apply_dropdown_adjustment(const Vec3& current_pos, const Vec3& next_pos)
{
  auto to_target = Vec3{
    next_pos.x - current_pos.x,
    next_pos.y - current_pos.y,
    next_pos.z - current_pos.z
  };
  if (-to_target.z <= player_jump_height)
  {
    return current_pos;
  }

  auto flat_direction = normalize_2d(to_target);
  return Vec3{
    current_pos.x + flat_direction.x * player_width * 2.0f,
    current_pos.y + flat_direction.y * player_width * 2.0f,
    current_pos.z
  };
}

std::vector<crumb> build_crumbs_from_area_path(const navbot_mesh& mesh, const std::vector<nav_area_id>& area_path, const Vec3& destination)
{
  std::vector<crumb> crumbs{};
  crumbs.reserve(area_path.size() * 2 + 1);

  for (size_t area_index = 0; area_index < area_path.size(); ++area_index)
  {
    auto current_area = area_path[area_index];
    auto current = mesh.find_area(current_area);
    if (current == nullptr)
    {
      continue;
    }

    if (area_index + 1 < area_path.size())
    {
      auto next_area = area_path[area_index + 1];
      auto points = build_transition_points(mesh, current_area, next_area);
      points.center = apply_dropdown_adjustment(points.center, points.next);

      crumbs.push_back(crumb{current_area, points.current, crumb_kind::area_center});
      crumbs.push_back(crumb{current_area, points.center, crumb_kind::transition_center});
    }
    else
    {
      crumbs.push_back(crumb{current_area, current->center, crumb_kind::area_center});
    }
  }

  crumbs.push_back(crumb{{}, destination, crumb_kind::destination});
  return crumbs;
}

path_result solve_path_request(const navbot_mesh& mesh, const navbot_hazards& hazards, const path_request& request, const cancellation_token& token, float current_time)
{
  auto start_time = std::chrono::steady_clock::now();

  path_result result{};
  result.request_id = request.request_id;
  result.generation_id = request.generation_id;
  result.world_generation = request.world_generation;
  result.hazard_generation = request.hazard_generation;
  result.goal = request.goal;
  result.destination_reach_distance = request.destination_reach_distance;

  if (token.is_canceled())
  {
    result.status = path_status::canceled;
    return result;
  }

  if (!request.start_area.valid())
  {
    result.status = path_status::no_start_area;
    return result;
  }

  if (!request.goal_area.valid())
  {
    result.status = path_status::no_goal_area;
    return result;
  }

  if (!mesh.is_ready())
  {
    result.status = path_status::failed;
    return result;
  }

  auto& cache = mesh.cache();
  auto start_lookup = cache.area_lookup.find(request.start_area.value);
  auto goal_lookup = cache.area_lookup.find(request.goal_area.value);
  if (start_lookup == cache.area_lookup.end())
  {
    result.status = path_status::no_start_area;
    return result;
  }
  if (goal_lookup == cache.area_lookup.end())
  {
    result.status = path_status::no_goal_area;
    return result;
  }

  auto start_index = start_lookup->second;
  auto goal_index = goal_lookup->second;
  auto best_index = start_index;
  auto best_heuristic = distance_value(cache.areas[start_index].center, cache.areas[goal_index].center);

  std::vector<path_node> nodes(cache.areas.size());
  std::priority_queue<queue_entry, std::vector<queue_entry>, queue_entry_less> open_set{};

  nodes[start_index].g_cost = 0.0f;
  nodes[start_index].f_cost = distance_value(cache.areas[start_index].center, cache.areas[goal_index].center);
  open_set.push(queue_entry{start_index, nodes[start_index].f_cost, nodes[start_index].g_cost});

  auto solved = false;

  while (!open_set.empty())
  {
    if (token.is_canceled())
    {
      result.status = path_status::canceled;
      return result;
    }

    auto entry = open_set.top();
    open_set.pop();

    auto& node = nodes[entry.area_index];
    if (node.visited)
    {
      continue;
    }

    node.visited = true;
    auto current_heuristic = distance_value(cache.areas[entry.area_index].center, cache.areas[goal_index].center);
    if (current_heuristic < best_heuristic)
    {
      best_heuristic = current_heuristic;
      best_index = entry.area_index;
    }

    if (entry.area_index == goal_index)
    {
      solved = true;
      break;
    }

    auto& area = cache.areas[entry.area_index];
    for (uint16_t connection_index = 0; connection_index < area.connections.size(); ++connection_index)
    {
      auto next_id = area.connections[connection_index].area_id;
      auto next_lookup = cache.area_lookup.find(next_id.value);
      if (next_lookup == cache.area_lookup.end())
      {
        continue;
      }

      auto next_index = next_lookup->second;
      auto edge_id = nav_edge_id{area.id.value, connection_index};

      if (hazards.is_edge_blocked(edge_id, current_time))
      {
        continue;
      }
      if (hazards.is_area_blocked(next_id, current_time))
      {
        continue;
      }
      if (mesh.area_has_flag(next_id, nav_area_flag_blocked) || mesh.area_has_flag(next_id, nav_area_flag_setup_gate))
      {
        continue;
      }

      auto points = build_transition_points(mesh, area.id, next_id);
      points.center = apply_dropdown_adjustment(points.center, points.next);

      auto height_diff = points.center_next.z - points.center.z;
      if (height_diff > player_jump_height)
      {
        continue;
      }

      auto step_cost = distance_value(points.current, points.next);
      step_cost += hazards.area_cost(next_id, current_time);
      auto new_cost = node.g_cost + step_cost;

      if (new_cost >= nodes[next_index].g_cost)
      {
        continue;
      }

      auto heuristic = distance_value(cache.areas[next_index].center, cache.areas[goal_index].center);
      nodes[next_index].g_cost = new_cost;
      nodes[next_index].f_cost = new_cost + heuristic;
      nodes[next_index].parent_index = entry.area_index;
      open_set.push(queue_entry{next_index, nodes[next_index].f_cost, nodes[next_index].g_cost});
    }
  }

  if (!solved)
  {
    if (request.require_exact_goal_area)
    {
      result.status = path_status::no_path;
      return result;
    }

    if (best_index == start_index)
    {
      result.status = path_status::no_path;
      return result;
    }

    result.area_path = reconstruct_area_path(cache, nodes, start_index, best_index);
    auto fallback_destination = mesh.get_nearest_point(cache.areas[best_index].id, request.goal_world);
    result.crumbs = build_crumbs_from_area_path(mesh, result.area_path, fallback_destination);
    result.status = path_status::success;
  }
  else
  {
    result.area_path = reconstruct_area_path(cache, nodes, start_index, goal_index);
    result.crumbs = build_crumbs_from_area_path(mesh, result.area_path, request.goal_world);
    result.status = path_status::success;
  }

  auto end_time = std::chrono::steady_clock::now();
  result.solve_time_ms = std::chrono::duration<float, std::milli>(end_time - start_time).count();
  return result;
}

} // namespace navbot
