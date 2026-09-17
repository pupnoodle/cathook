/*
/^-----^\   data: 2026-04-05
V  o o  V  file: src/features/automation/navbot/navbot_debug.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "features/automation/navbot/navbot_debug.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>
#include "imgui/imgui.h"
#include "features/visuals/overlay_projection.hpp"

namespace navbot
{

namespace
{

const char* goal_type_name(goal_type type)
{
  switch (type)
  {
    case goal_type::get_health:
      return "get_health";
    case goal_type::get_ammo:
      return "get_ammo";
    case goal_type::capture_objective:
      return "capture_objective";
    case goal_type::escape_danger:
      return "escape_danger";
    case goal_type::push_payload:
      return "push_payload";
    case goal_type::defend_payload:
      return "defend_payload";
    case goal_type::get_flag:
      return "get_flag";
    case goal_type::return_flag:
      return "return_flag";
    case goal_type::hold_range_on_enemy:
      return "hold_range_on_enemy";
    case goal_type::melee_chase:
      return "melee_chase";
    case goal_type::sentry_snipe:
      return "sentry_snipe";
    case goal_type::engineer_build:
      return "engineer_build";
    case goal_type::engineer_maintain:
      return "engineer_maintain";
    case goal_type::reload_weapons:
      return "reload_weapons";
    case goal_type::heal_follow:
      return "heal_follow";
    case goal_type::mvm_tank:
      return "mvm_tank";
    case goal_type::mvm_combat:
      return "mvm_combat";
    case goal_type::mvm_money:
      return "mvm_money";
    case goal_type::mvm_frontline:
      return "mvm_frontline";
    case goal_type::mvm_teleporter:
      return "mvm_teleporter";
    case goal_type::mvm_upgrade_station:
      return "mvm_upgrade_station";
    case goal_type::followbot:
      return "followbot";
    case goal_type::command_path:
      return "command_path";
    case goal_type::roam:
    default:
      return "roam";
  }
}

const char* path_status_name(path_status status)
{
  switch (status)
  {
    case path_status::success:
      return "success";
    case path_status::no_start_area:
      return "no_start_area";
    case path_status::no_goal_area:
      return "no_goal_area";
    case path_status::no_path:
      return "no_path";
    case path_status::canceled:
      return "canceled";
    case path_status::stale:
      return "stale";
    case path_status::failed:
    default:
      return "failed";
  }
}

const char* failure_reason_name(follower_failure_reason reason)
{
  switch (reason)
  {
    case follower_failure_reason::blocked:
      return "blocked";
    case follower_failure_reason::no_progress:
      return "no_progress";
    case follower_failure_reason::invalid_local_area:
      return "invalid_local_area";
    case follower_failure_reason::destination_invalid:
      return "destination_invalid";
    case follower_failure_reason::stale_path:
      return "stale_path";
    case follower_failure_reason::hazard_intersection:
      return "hazard_intersection";
    case follower_failure_reason::none:
    default:
      return "none";
  }
}

void draw_text_line(ImDrawList* draw_list, const ImVec2& position, const std::string& text)
{
  if (draw_list == nullptr || text.empty())
  {
    return;
  }

  draw_list->AddText(ImVec2(position.x + 1.0f, position.y + 1.0f), IM_COL32(0, 0, 0, 255), text.c_str());
  draw_list->AddText(position, IM_COL32(255, 255, 255, 255), text.c_str());
}

}

void draw_path_imgui(
  ImDrawList* draw_list,
  const path_result& path,
  const bool draw_boxes,
  const RGBA_float& color)
{
  if (draw_list == nullptr || path.status != path_status::success || path.crumbs.empty())
  {
    return;
  }

  if (!overlay_projection::begin_frame())
  {
    return;
  }

  const auto path_color = color.resolved();
  const ImU32 draw_color = ImGui::ColorConvertFloat4ToU32(
    ImVec4(path_color.r, path_color.g, path_color.b, path_color.a));

  for (size_t crumb_index = 0; crumb_index < path.crumbs.size(); ++crumb_index)
  {
    Vec3 start_screen{};
    const auto start_world = path.crumbs[crumb_index].world;
    if (!overlay_projection::world_to_screen(start_world, &start_screen))
    {
      continue;
    }

    if (draw_boxes)
    {
      draw_list->AddRectFilled(
        ImVec2(start_screen.x - 5.0f, start_screen.y - 5.0f),
        ImVec2(start_screen.x + 5.0f, start_screen.y + 5.0f),
        draw_color);
    }

    if (crumb_index + 1 < path.crumbs.size())
    {
      Vec3 end_screen{};
      const auto end_world = path.crumbs[crumb_index + 1].world;
      if (overlay_projection::world_to_screen(end_world, &end_screen))
      {
        draw_list->AddLine(
          ImVec2(start_screen.x, start_screen.y),
          ImVec2(end_screen.x, end_screen.y),
          draw_color,
          2.0f);
      }
    }
  }
}

void draw_debug_overlay_imgui(ImDrawList* draw_list, const navbot_debug_state& debug_state)
{
  if (draw_list == nullptr)
  {
    return;
  }

  auto lines = std::vector<std::string>{};
  lines.reserve(30);
  lines.emplace_back("navbot");
  lines.emplace_back(std::string("state: ") + debug_state.runtime_state);
  lines.emplace_back(std::string("map: ") + debug_state.map_name);
  lines.emplace_back(std::string("mesh: ") + (debug_state.mesh_ready ? "ready" : "missing"));
  lines.emplace_back(std::string("goal: ") + (debug_state.goal_valid ? goal_type_name(debug_state.current_goal) : "none"));
  lines.emplace_back(std::string("path: ") + path_status_name(debug_state.current_path_status));
  lines.emplace_back(std::string("epochs: route ") + std::to_string(debug_state.active_generation_id) +
                     " world " + std::to_string(debug_state.active_world_generation) +
                     " hazards " + std::to_string(debug_state.active_hazard_generation) +
                     " pending " + std::to_string(debug_state.pending_generation_id));
  if (!debug_state.path_request_message.empty())
  {
    lines.emplace_back(std::string("path_request: ") + debug_state.path_request_message);
  }
  lines.emplace_back(std::string("active_path: ") + (debug_state.has_active_path ? "yes" : "no"));
  lines.emplace_back(std::string("crumbs: ") + std::to_string(debug_state.active_crumb_count));
  lines.emplace_back(std::string("last_fail: ") + failure_reason_name(debug_state.last_failure));
  lines.emplace_back("jobs: +=doable -=unavailable x=disabled");
  auto job_line = std::string("jobs:");
  for (size_t index = 0; index < goal_type_count; ++index)
  {
    const auto& job = debug_state.job_availability[index];
    const auto status = !job.enabled ? 'x' : job.candidate_available ? '+' : '-';
    job_line += " " + std::string(goal_type_name(static_cast<goal_type>(index))) + ":" + status;
    if (job_line.size() >= 92 && index + 1 < goal_type_count)
    {
      lines.emplace_back(std::move(job_line));
      job_line = "     ";
    }
  }
  lines.emplace_back(std::move(job_line));
  lines.emplace_back(std::string("cp: ") + std::to_string(debug_state.captured_point_index) +
                     " setup: " + (debug_state.setup_finished ? "done" : "active") +
                     " mini: " + std::to_string(debug_state.mini_round_mask));
  if (!debug_state.nav_file_path.empty())
  {
    lines.emplace_back(std::string("nav: ") + debug_state.nav_file_path);
  }
  else
  {
    lines.emplace_back("nav: not found");
  }

  auto y = 120.0f;
  auto line_height = ImGui::GetTextLineHeight();
  for (const auto& line : lines)
  {
    draw_text_line(draw_list, ImVec2(20.0f, y), line);
    y += line_height;
  }
}

}
