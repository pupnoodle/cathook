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
  nav_edge_id parent_edge{};
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

constexpr uint32_t tf_team_red = 2u;
constexpr uint32_t tf_team_blue = 3u;
constexpr uint32_t path_tf_nav_spawn_room_red = 0x00000002u;
constexpr uint32_t path_tf_nav_spawn_room_blue = 0x00000004u;
constexpr uint32_t path_nav_mesh_avoid = 0x00000080u;
constexpr uint32_t path_tf_nav_blue_sentry_danger = 0x00000080u;
constexpr uint32_t path_tf_nav_red_sentry_danger = 0x00000100u;
constexpr uint32_t path_tf_nav_blue_setup_gate = 0x00000800u;
constexpr uint32_t path_tf_nav_red_setup_gate = 0x00001000u;
constexpr uint32_t path_tf_nav_blocked_after_point_capture = 0x00002000u;
constexpr uint32_t path_tf_nav_blocked_until_point_capture = 0x00004000u;
constexpr uint32_t path_tf_nav_blue_one_way_door = 0x00008000u;
constexpr uint32_t path_tf_nav_red_one_way_door = 0x00010000u;
constexpr uint32_t path_tf_nav_with_second_point = 0x00020000u;
constexpr uint32_t path_tf_nav_with_third_point = 0x00040000u;
constexpr uint32_t path_tf_nav_with_fourth_point = 0x00080000u;
constexpr uint32_t path_tf_nav_with_fifth_point = 0x00100000u;
constexpr uint32_t path_tf_nav_door_always_blocks = 0x20000000u;
constexpr uint32_t path_tf_nav_unblockable = 0x40000000u;
constexpr float start_area_search_radius = half_player_width + player_clearance_margin + 24.0f;
constexpr float avoid_area_cost = 650.0f;
constexpr float dropdown_edge_cost = 350.0f;
constexpr float dropdown_height_cost = 2.0f;
constexpr float jump_edge_cost = 120.0f;
constexpr float enemy_spawn_area_cost = 4000.0f;
constexpr float sentry_danger_area_cost = 350.0f;

bool nav_edge_valid_path(nav_edge_id edge_id)
{
  return edge_id.from_area != 0;
}

}

bool area_has_tf_attribute(const nav_area_data& area, uint32_t attributes)
{
  return (area.tf_attributes & attributes) != 0;
}

int capture_point_threshold_for_area(const nav_area_data& area)
{
  if (area_has_tf_attribute(area, path_tf_nav_with_second_point))
  {
    return 1;
  }
  if (area_has_tf_attribute(area, path_tf_nav_with_third_point))
  {
    return 2;
  }
  if (area_has_tf_attribute(area, path_tf_nav_with_fourth_point))
  {
    return 3;
  }
  if (area_has_tf_attribute(area, path_tf_nav_with_fifth_point))
  {
    return 4;
  }

  return 0;
}

bool capture_point_threshold_reached(const nav_area_data& area, const path_request& request)
{
  return request.captured_point_index >= capture_point_threshold_for_area(area);
}

bool nav_area_is_blocked_for_request(const nav_area_data& area, const path_request& request)
{
  if (area_has_tf_attribute(area, path_tf_nav_unblockable))
  {
    return false;
  }

  if ((area.flags & nav_area_flag_blocked) != 0)
  {
    return true;
  }

  if (request.team == tf_team_red && area_has_tf_attribute(area, path_tf_nav_blue_one_way_door))
  {
    return true;
  }
  if (request.team == tf_team_blue && area_has_tf_attribute(area, path_tf_nav_red_one_way_door))
  {
    return true;
  }

  if (area_has_tf_attribute(area, path_tf_nav_door_always_blocks))
  {
    return true;
  }

  if (!request.setup_finished && area_has_tf_attribute(area, path_tf_nav_blue_setup_gate | path_tf_nav_red_setup_gate))
  {
    return true;
  }

  if (area_has_tf_attribute(area, path_tf_nav_blocked_until_point_capture))
  {
    return !capture_point_threshold_reached(area, request);
  }
  if (area_has_tf_attribute(area, path_tf_nav_blocked_after_point_capture))
  {
    return capture_point_threshold_reached(area, request);
  }

  return false;
}

bool request_allows_area(const nav_mesh_cache& cache, nav_area_id area_id, const navbot_hazards& hazards, const path_request& request, float current_time)
{
  auto lookup = cache.area_lookup.find(area_id.value);
  if (lookup == cache.area_lookup.end())
  {
    return false;
  }

  if (hazards.is_area_blocked(area_id, current_time))
  {
    return false;
  }

  const auto& area = cache.areas[lookup->second];
  if (nav_area_is_blocked_for_request(area, request))
  {
    return false;
  }

  return true;
}

std::optional<uint32_t> find_nearest_crumb_node(const nav_mesh_cache& cache,
  nav_area_id area_id,
  const Vec3& world,
  const navbot_hazards& hazards,
  const path_request& request,
  float current_time,
  bool allow_blocked_area = false)
{
  auto area_nodes = cache.crumb_nodes_by_area.find(area_id.value);
  if (area_nodes == cache.crumb_nodes_by_area.end())
  {
    return std::nullopt;
  }

  auto best_node = 0u;
  auto best_distance = std::numeric_limits<float>::max();
  auto found = false;
  for (auto node_index : area_nodes->second)
  {
    if (node_index >= cache.crumb_nodes.size())
    {
      continue;
    }

    const auto& node = cache.crumb_nodes[node_index];
    if (!allow_blocked_area && !request_allows_area(cache, node.area_id, hazards, request, current_time))
    {
      continue;
    }

    const auto node_distance = length_squared(node.world - world);
    if (!found || node_distance < best_distance)
    {
      found = true;
      best_node = node_index;
      best_distance = node_distance;
    }
  }

  if (!found)
  {
    return std::nullopt;
  }

  return best_node;
}

std::optional<uint32_t> find_nearest_start_crumb_node(const navbot_mesh& mesh,
  const nav_mesh_cache& cache,
  const navbot_hazards& hazards,
  const path_request& request,
  float current_time)
{
  auto candidates = std::vector<navbot_mesh::nearby_area>{};
  candidates.reserve(16);
  candidates.push_back(navbot_mesh::nearby_area{request.start_area, 0.0f});

  auto nearby_areas = mesh.areas_in_radius(request.start_world, start_area_search_radius);
  std::sort(nearby_areas.begin(), nearby_areas.end(), [](const navbot_mesh::nearby_area& left, const navbot_mesh::nearby_area& right)
  {
    if (left.distance_sq != right.distance_sq)
    {
      return left.distance_sq < right.distance_sq;
    }

    return left.id.value < right.id.value;
  });

  for (const auto& nearby : nearby_areas)
  {
    if (std::find_if(candidates.begin(), candidates.end(), [&nearby](const navbot_mesh::nearby_area& candidate)
      {
        return candidate.id.value == nearby.id.value;
      }) != candidates.end())
    {
      continue;
    }

    candidates.push_back(nearby);
  }

  for (const auto& candidate : candidates)
  {
    const bool is_current_area = candidate.id.value == request.start_area.value;
    if (!is_current_area && !request_allows_area(cache, candidate.id, hazards, request, current_time))
    {
      continue;
    }

    auto node = find_nearest_crumb_node(
      cache,
      candidate.id,
      request.start_world,
      hazards,
      request,
      current_time,
      is_current_area);
    if (node.has_value())
    {
      return node;
    }
  }

  return std::nullopt;
}

std::vector<uint32_t> reconstruct_crumb_node_path(const std::vector<path_node>& nodes, uint32_t start_index, uint32_t goal_index)
{
  auto node_path = std::vector<uint32_t>{};
  for (auto index = goal_index;; index = nodes[index].parent_index)
  {
    node_path.push_back(index);
    if (index == start_index)
    {
      break;
    }
  }

  std::reverse(node_path.begin(), node_path.end());
  return node_path;
}

std::vector<nav_area_id> build_area_path_from_crumb_nodes(const nav_mesh_cache& cache, const std::vector<uint32_t>& node_path)
{
  auto area_path = std::vector<nav_area_id>{};
  area_path.reserve(node_path.size());

  for (auto node_index : node_path)
  {
    if (node_index >= cache.crumb_nodes.size())
    {
      continue;
    }

    auto area_id = cache.crumb_nodes[node_index].area_id;
    if (!area_id.valid())
    {
      continue;
    }

    if (area_path.empty() || area_path.back().value != area_id.value)
    {
      area_path.push_back(area_id);
    }
  }

  return area_path;
}

std::vector<crumb> build_crumbs_from_crumb_nodes(const nav_mesh_cache& cache,
  const std::vector<path_node>& nodes,
  const std::vector<uint32_t>& node_path,
  const Vec3& destination)
{
  auto crumbs = std::vector<crumb>{};
  crumbs.reserve(node_path.size() + 3);

  auto find_edge = [&cache](uint32_t from_node, uint32_t to_node, nav_edge_id preferred) -> const nav_crumb_edge*
  {
    if (from_node >= cache.crumb_nodes.size())
    {
      return nullptr;
    }

    const nav_crumb_edge* fallback = nullptr;
    for (const auto& edge : cache.crumb_nodes[from_node].edges)
    {
      if (edge.to_node != to_node)
      {
        continue;
      }

      if (fallback == nullptr)
      {
        fallback = &edge;
      }
      if (edge.nav_edge.from_area == preferred.from_area
        && edge.nav_edge.connection_index == preferred.connection_index)
      {
        return &edge;
      }
    }

    return fallback;
  };

  for (size_t i = 0; i < node_path.size(); ++i)
  {
    const auto node_index = node_path[i];
    if (node_index >= cache.crumb_nodes.size())
    {
      continue;
    }

    const auto incoming_edge = i > 0 ? find_edge(node_path[i - 1], node_index, nodes[node_index].parent_edge) : nullptr;
    if (incoming_edge != nullptr && incoming_edge->has_dropdown_waypoint)
    {

      continue;
    }

    auto edge_id = nav_edge_id{};
    const auto outgoing_edge = i + 1 < node_path.size()
      ? find_edge(node_index, node_path[i + 1], nodes[node_path[i + 1]].parent_edge)
      : nullptr;
    if (i + 1 < node_path.size())
    {
      const auto next_index = node_path[i + 1];
      if (next_index < nodes.size())
      {
        edge_id = nodes[next_index].parent_edge;
      }
    }

    const auto& node = cache.crumb_nodes[node_index];
    auto world = node.world;
    if (outgoing_edge != nullptr && outgoing_edge->has_dropdown_waypoint)
    {
      crumbs.push_back(crumb{
        node.area_id,
        outgoing_edge->nav_edge,
        outgoing_edge->dropdown_approach,
        crumb_kind::dropdown_approach
      });
      crumbs.push_back(crumb{
        cache.crumb_nodes[outgoing_edge->to_node].area_id,
        {},
        outgoing_edge->dropdown_landing,
        crumb_kind::area_center
      });
      continue;
    }

    crumbs.push_back(crumb{
      node.area_id,
      edge_id,
      world,
      nav_edge_valid_path(edge_id) ? crumb_kind::transition_center : crumb_kind::area_center
    });
  }

  crumbs.push_back(crumb{{}, {}, destination, crumb_kind::destination});
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
  if (cache.crumb_nodes.empty())
  {
    result.status = path_status::failed;
    return result;
  }

  auto start_node = find_nearest_start_crumb_node(mesh, cache, hazards, request, current_time);
  if (!start_node.has_value())
  {
    result.status = path_status::no_start_area;
    return result;
  }

  const bool goal_is_start_area = request.goal_area.value == request.start_area.value;
  auto goal_node = find_nearest_crumb_node(
    cache,
    request.goal_area,
    request.goal_world,
    hazards,
    request,
    current_time,
    goal_is_start_area);
  if (!goal_node.has_value())
  {
    result.status = path_status::no_goal_area;
    return result;
  }

  auto start_index = *start_node;
  auto goal_index = *goal_node;
  auto start_heuristic = distance_3d(cache.crumb_nodes[start_index].world, cache.crumb_nodes[goal_index].world);
  std::vector<path_node> nodes(cache.crumb_nodes.size());
  std::priority_queue<queue_entry, std::vector<queue_entry>, queue_entry_less> open_set{};

  nodes[start_index].g_cost = 0.0f;
  nodes[start_index].f_cost = start_heuristic;
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
    if (entry.area_index == goal_index)
    {
      solved = true;
      break;
    }

    auto& crumb_node = cache.crumb_nodes[entry.area_index];
    for (const auto& edge : crumb_node.edges)
    {
      if (edge.to_node >= cache.crumb_nodes.size())
      {
        continue;
      }

      const auto next_index = edge.to_node;
      const auto next_id = cache.crumb_nodes[next_index].area_id;

      if (nav_edge_valid_path(edge.nav_edge) && hazards.is_edge_blocked(edge.nav_edge, current_time))
      {
        continue;
      }

      if (next_id.value != request.start_area.value
        && !request_allows_area(cache, next_id, hazards, request, current_time))
      {
        continue;
      }

      auto step_cost = edge.cost;
      const auto* next_area = cache.areas.data() + cache.area_lookup.at(next_id.value);
      if ((next_area->base_attributes & path_nav_mesh_avoid) != 0)
      {
        step_cost += avoid_area_cost;
      }
      const auto enemy_spawn_attribute = request.team == tf_team_red
        ? path_tf_nav_spawn_room_blue
        : request.team == tf_team_blue ? path_tf_nav_spawn_room_red : 0u;
      if ((next_area->tf_attributes & enemy_spawn_attribute) != 0)
      {
        step_cost += enemy_spawn_area_cost;
      }
      const auto sentry_danger_attribute = request.team == tf_team_red
        ? path_tf_nav_blue_sentry_danger
        : request.team == tf_team_blue ? path_tf_nav_red_sentry_danger : 0u;
      if ((next_area->tf_attributes & sentry_danger_attribute) != 0)
      {
        step_cost += sentry_danger_area_cost;
      }
      if (edge.is_dropdown)
      {
        const auto& current_world = cache.crumb_nodes[entry.area_index].world;
        const auto& next_world = cache.crumb_nodes[next_index].world;
        const auto height_drop = std::max(0.0f, current_world.z - next_world.z - player_step_height);
        step_cost += dropdown_edge_cost + height_drop * dropdown_height_cost;
      }
      if (edge.requires_jump)
      {
        step_cost += jump_edge_cost;
      }
      step_cost += hazards.area_cost(next_id, current_time);
      auto new_cost = node.g_cost + step_cost;

      if (new_cost >= nodes[next_index].g_cost)
      {
        continue;
      }

      auto heuristic = distance_3d(cache.crumb_nodes[next_index].world, cache.crumb_nodes[goal_index].world);
      nodes[next_index].g_cost = new_cost;
      nodes[next_index].f_cost = new_cost + heuristic;
      nodes[next_index].parent_index = entry.area_index;
      nodes[next_index].parent_edge = edge.nav_edge;
      open_set.push(queue_entry{next_index, nodes[next_index].f_cost, nodes[next_index].g_cost});
    }
  }

  if (!solved)
  {
    result.status = path_status::no_path;
    return result;
  }
  else
  {
    auto node_path = reconstruct_crumb_node_path(nodes, start_index, goal_index);
    result.area_path = build_area_path_from_crumb_nodes(cache, node_path);
    result.crumbs = build_crumbs_from_crumb_nodes(cache, nodes, node_path, request.goal_world);
    result.status = path_status::success;
  }

  auto end_time = std::chrono::steady_clock::now();
  result.solve_time_ms = std::chrono::duration<float, std::milli>(end_time - start_time).count();
  return result;
}

}
