/*
/^-----^\   data: 2026-04-05
V  o o  V  file: src/features/automation/navbot/navbot_mesh.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "features/automation/navbot/navbot_mesh.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <vector>
#include "core/logger.hpp"
#include "core/math/math.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/engine_trace.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"

namespace navbot
{

namespace
{

constexpr uint32_t nav_magic_number = 0xFEEDFACEu;
constexpr uint32_t nav_current_version = 16u;
constexpr uint32_t nav_mesh_avoid = 0x00000080u;
constexpr uint32_t nav_mesh_nav_blocker = 0x80000000u;
constexpr auto navmesh_resolve_refresh_interval = std::chrono::seconds(1);

constexpr uint32_t tf_nav_blocked = 0x00000001u;
constexpr uint32_t tf_nav_spawn_room_red = 0x00000002u;
constexpr uint32_t tf_nav_spawn_room_blue = 0x00000004u;
constexpr uint32_t tf_nav_spawn_room_exit = 0x00000008u;
constexpr uint32_t tf_nav_has_ammo = 0x00000010u;
constexpr uint32_t tf_nav_has_health = 0x00000020u;
constexpr uint32_t tf_nav_control_point = 0x00000040u;
constexpr uint32_t tf_nav_blue_setup_gate = 0x00000800u;
constexpr uint32_t tf_nav_red_setup_gate = 0x00001000u;
constexpr uint32_t tf_nav_blue_one_way_door = 0x00008000u;
constexpr uint32_t tf_nav_red_one_way_door = 0x00010000u;
constexpr uint32_t tf_nav_sniper_spot = 0x00200000u;
constexpr uint32_t tf_nav_sentry_spot = 0x00400000u;

constexpr uint32_t tf_nav_sub_version_with_attributes = 2u;
constexpr float crumb_graph_spacing = 100.0f;
constexpr float crumb_graph_sample_spacing = 50.0f;
constexpr float crumb_graph_duplicate_distance = 36.0f;
constexpr float crumb_graph_connect_distance = 148.0f;

void navbot_log(const char* fmt, ...)
{
  cathook::core::log_raw("[navbot] ");

  va_list args{};
  va_start(args, fmt);
  cathook::core::vlog_raw(fmt, args);
  va_end(args);
}

class nav_file_reader
{
public:
  explicit nav_file_reader(std::vector<uint8_t> bytes)
    : bytes_(std::move(bytes))
  {
  }

  [[nodiscard]] bool valid() const
  {
    return valid_;
  }

  [[nodiscard]] bool has_bytes(size_t count) const
  {
    return valid_ && offset_ + count <= bytes_.size();
  }

  [[nodiscard]] size_t offset() const
  {
    return offset_;
  }

  [[nodiscard]] size_t remaining() const
  {
    if (offset_ >= bytes_.size())
    {
      return 0;
    }

    return bytes_.size() - offset_;
  }

  uint8_t read_u8()
  {
    if (!has_bytes(1))
    {
      valid_ = false;
      return 0;
    }

    return bytes_[offset_++];
  }

  uint16_t read_u16()
  {
    if (!has_bytes(2))
    {
      valid_ = false;
      return 0;
    }

    auto value = static_cast<uint16_t>(bytes_[offset_])
      | (static_cast<uint16_t>(bytes_[offset_ + 1]) << 8);
    offset_ += 2;
    return value;
  }

  uint32_t read_u32()
  {
    if (!has_bytes(4))
    {
      valid_ = false;
      return 0;
    }

    auto value = static_cast<uint32_t>(bytes_[offset_])
      | (static_cast<uint32_t>(bytes_[offset_ + 1]) << 8)
      | (static_cast<uint32_t>(bytes_[offset_ + 2]) << 16)
      | (static_cast<uint32_t>(bytes_[offset_ + 3]) << 24);
    offset_ += 4;
    return value;
  }

  int32_t read_i32()
  {
    return static_cast<int32_t>(read_u32());
  }

  float read_f32()
  {
    auto bits = read_u32();
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  Vec3 read_vec3()
  {
    return Vec3{read_f32(), read_f32(), read_f32()};
  }

  std::string read_string(size_t length)
  {
    if (!has_bytes(length))
    {
      valid_ = false;
      return {};
    }

    auto begin = reinterpret_cast<const char*>(bytes_.data() + offset_);
    auto end = begin + length;
    offset_ += length;

    auto zero = std::find(begin, end, '\0');
    return std::string(begin, zero);
  }

  void skip(size_t count)
  {
    if (!has_bytes(count))
    {
      valid_ = false;
      return;
    }

    offset_ += count;
  }

private:
  std::vector<uint8_t> bytes_{};
  size_t offset_ = 0;
  bool valid_ = true;
};

std::string sanitize_map_name(const char* raw_name)
{
  if (raw_name == nullptr)
  {
    return {};
  }

  auto map_name = std::string(raw_name);
  auto slash = map_name.find_last_of("/\\");
  if (slash != std::string::npos)
  {
    map_name = map_name.substr(slash + 1);
  }

  if (map_name.ends_with(".bsp"))
  {
    map_name.resize(map_name.size() - 4);
  }

  return map_name;
}

void append_search_root(std::vector<std::filesystem::path>& search_roots, std::filesystem::path path)
{
  if (path.empty())
  {
    return;
  }

  if (std::find(search_roots.begin(), search_roots.end(), path) != search_roots.end())
  {
    return;
  }

  search_roots.emplace_back(std::move(path));
}

bool can_open_nav_file(const std::filesystem::path& path)
{
  auto file = std::ifstream(path, std::ios::binary);
  return file.good();
}

std::filesystem::path resolve_nav_path(const std::string& map_name)
{
  auto search_roots = std::vector<std::filesystem::path>{};
  std::error_code error{};
  auto current_root = std::filesystem::current_path(error);
  if (error)
  {
    navbot_log("navmesh current_path failed: %s\n", error.message().c_str());
  }
  else
  {
    append_search_root(search_roots, current_root);
  }

  for (int depth = 0; depth < 6; ++depth)
  {
    current_root = current_root.parent_path();
    if (current_root.empty())
    {
      break;
    }

    append_search_root(search_roots, current_root);
  }

  auto append_env_search_root =
    [&search_roots](const char* env_name, const std::filesystem::path& suffix)
  {
    if (const auto* value = std::getenv(env_name); value != nullptr && value[0] != '\0')
    {
      auto root = std::filesystem::path(value);
      if (!suffix.empty())
      {
        root /= suffix;
      }

      append_search_root(search_roots, std::move(root));
    }
  };

  append_env_search_root("TF2_PATH", std::filesystem::path{});
  append_env_search_root("CAT_TF2_PATH", std::filesystem::path{});
  append_env_search_root("CAT_STEAMAPPS_PATH", std::filesystem::path("common/Team Fortress 2"));
  append_env_search_root("CAT_STEAM_ROOT", std::filesystem::path("steamapps/common/Team Fortress 2"));

  if (const auto* cathook_root = std::getenv("CATHOOK_ROOT"); cathook_root != nullptr && cathook_root[0] != '\0')
  {
    append_search_root(search_roots, std::filesystem::path(cathook_root) / "navmeshes");
  }

  append_search_root(search_roots, cathook::core::root_directory() / "navmeshes");
  append_search_root(search_roots, "/opt/cathook/navmeshes");
  append_search_root(search_roots, "/opt/steamapps/common/Team Fortress 2");

  if (const auto* home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
  {
    const auto home_path = std::filesystem::path(home);
    append_search_root(search_roots, home_path / ".steam/steam/steamapps/common/Team Fortress 2");
    append_search_root(search_roots, home_path / ".local/share/Steam/steamapps/common/Team Fortress 2");
    append_search_root(search_roots, home_path / ".steam/debian-installation/steamapps/common/Team Fortress 2");
  }

  for (const auto& root : search_roots)
  {
    const std::array<std::filesystem::path, 5> relative_roots{
      std::filesystem::path("maps"),
      std::filesystem::path("tf/maps"),
      std::filesystem::path("download/maps"),
      std::filesystem::path("tf/download/maps"),
      std::filesystem::path{}
    };

    for (const auto& relative_root : relative_roots)
    {
      auto candidate = root / relative_root / (map_name + ".nav");
      error.clear();
      const auto exists = std::filesystem::exists(candidate, error);
      if (error)
      {
        navbot_log("navmesh check failed path='%s' error='%s'\n", candidate.string().c_str(), error.message().c_str());
        continue;
      }

      navbot_log("navmesh check path='%s' exists=%d\n", candidate.string().c_str(), exists ? 1 : 0);
      if (exists)
      {
        if (!can_open_nav_file(candidate))
        {
          navbot_log("navmesh candidate unreadable path='%s'\n", candidate.string().c_str());
          continue;
        }

        error.clear();
        auto canonical = std::filesystem::weakly_canonical(candidate, error);
        if (error)
        {
          navbot_log("navmesh canonicalize failed path='%s' error='%s'\n", candidate.string().c_str(), error.message().c_str());
          return candidate;
        }

        navbot_log("navmesh found path='%s'\n", canonical.string().c_str());
        return canonical;
      }
    }
  }

  navbot_log("navmesh not found map='%s' searched_roots=%zu\n", map_name.c_str(), search_roots.size());
  return {};
}

std::vector<uint8_t> read_file_bytes(const std::filesystem::path& path)
{
  auto file = std::ifstream(path, std::ios::binary | std::ios::ate);
  if (!file)
  {
    return {};
  }

  auto size = static_cast<size_t>(file.tellg());
  file.seekg(0, std::ios::beg);

  std::vector<uint8_t> bytes{};
  bytes.resize(size);
  if (size != 0)
  {
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
  }

  if (!file && size != 0)
  {
    return {};
  }

  return bytes;
}

float area_z_at(const nav_area_data& area, float x, float y)
{
  auto width = area.se_corner.x - area.nw_corner.x;
  auto depth = area.se_corner.y - area.nw_corner.y;
  if (width <= 0.0f || depth <= 0.0f)
  {
    return area.ne_z;
  }

  auto u = clampf((x - area.nw_corner.x) / width, 0.0f, 1.0f);
  auto v = clampf((y - area.nw_corner.y) / depth, 0.0f, 1.0f);

  auto north_z = area.nw_corner.z + u * (area.ne_z - area.nw_corner.z);
  auto south_z = area.sw_z + u * (area.se_corner.z - area.sw_z);
  return north_z + v * (south_z - north_z);
}

Vec3 nearest_point_on_area(const nav_area_data& area, const Vec3& world)
{
  auto x = clampf(world.x, area.nw_corner.x, area.se_corner.x);
  auto y = clampf(world.y, area.nw_corner.y, area.se_corner.y);
  return Vec3{x, y, area_z_at(area, x, y)};
}

float distance_to_area_sq(const nav_area_data& area, const Vec3& world)
{
  auto nearest = nearest_point_on_area(area, world);
  auto dx = world.x - nearest.x;
  auto dy = world.y - nearest.y;
  auto dz = world.z - nearest.z;
  return dx * dx + dy * dy + dz * dz;
}

float distance_sq_2d_mesh(const Vec3& left, const Vec3& right)
{
  auto dx = left.x - right.x;
  auto dy = left.y - right.y;
  return dx * dx + dy * dy;
}

float distance_value_mesh(const Vec3& left, const Vec3& right)
{
  auto dx = left.x - right.x;
  auto dy = left.y - right.y;
  auto dz = left.z - right.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool point_inside_area_2d(const nav_area_data& area, const Vec3& point)
{
  return point.x >= area.mins.x && point.x <= area.maxs.x
      && point.y >= area.mins.y && point.y <= area.maxs.y;
}

bool area_overlaps_player_xy_expanded(const nav_area_data& area, const Vec3& world, float pad)
{
  return world.x + pad >= area.mins.x && world.x - pad <= area.maxs.x
      && world.y + pad >= area.mins.y && world.y - pad <= area.maxs.y;
}

float area_z_at_world_clamped(const nav_area_data& area, const Vec3& world)
{
  float clamped_x = clampf(world.x, area.nw_corner.x, area.se_corner.x);
  float clamped_y = clampf(world.y, area.nw_corner.y, area.se_corner.y);
  return area_z_at(area, clamped_x, clamped_y);
}

bool hull_can_fall_to_area(const Vec3& world, const nav_area_data& area)
{
  if (engine_trace == nullptr)
  {
    return true;
  }

  float clamped_x = clampf(world.x, area.nw_corner.x, area.se_corner.x);
  float clamped_y = clampf(world.y, area.nw_corner.y, area.se_corner.y);
  float area_z = area_z_at(area, clamped_x, clamped_y);

  Vec3 start{world.x, world.y, world.z + 4.0f};
  Vec3 end{clamped_x, clamped_y, area_z + 5.0f};

  if (start.z <= end.z + 1.0f)
  {
    return true;
  }

  Vec3 mins{-half_player_width, -half_player_width, 0.0f};
  Vec3 maxs{half_player_width, half_player_width, 72.0f};

  trace_t trace{};
  engine_trace->trace_hull(&start, &end, &mins, &maxs, MASK_PLAYERSOLID, &trace);

  if (trace.start_solid || trace.all_solid)
  {
    return false;
  }

  return trace.fraction >= 0.99f;
}

nav_area_id try_find_overlapping_area_below_linear(const Vec3& world, const std::vector<nav_area_data>& areas)
{
  const float xy_pad = half_player_width;
  nav_area_id best{};
  float best_dz = std::numeric_limits<float>::max();
  float best_dist_sq = std::numeric_limits<float>::max();
  bool found = false;

  for (const auto& area : areas)
  {
    if (!area_overlaps_player_xy_expanded(area, world, xy_pad))
    {
      continue;
    }

    float area_z = area_z_at_world_clamped(area, world);
    float dz = world.z - area_z;

    if (dz < -player_step_height)
    {
      continue;
    }

    if (!hull_can_fall_to_area(world, area))
    {
      continue;
    }

    float dist_sq = distance_to_area_sq(area, world);

    if (!found || dz < best_dz - 0.01f || (std::fabs(dz - best_dz) < 0.01f && dist_sq < best_dist_sq))
    {
      found = true;
      best = area.id;
      best_dz = dz;
      best_dist_sq = dist_sq;
    }
  }

  if (found)
  {
    return best;
  }

  return nav_area_id{};
}

nav_area_id try_find_overlapping_area_below_grid(const Vec3& world, const nav_mesh_cache& cache)
{
  const auto& grid = cache.grid;
  if (!grid.valid() || cache.areas.empty())
  {
    return nav_area_id{};
  }

  const float xy_pad = half_player_width;

  float search_min_x = world.x - xy_pad;
  float search_max_x = world.x + xy_pad;
  float search_min_y = world.y - xy_pad;
  float search_max_y = world.y + xy_pad;

  int cx0 = std::clamp(static_cast<int>((search_min_x - grid.min_x) / grid.cell_size), 0, grid.columns - 1);
  int cx1 = std::clamp(static_cast<int>((search_max_x - grid.min_x) / grid.cell_size), 0, grid.columns - 1);
  int cy0 = std::clamp(static_cast<int>((search_min_y - grid.min_y) / grid.cell_size), 0, grid.rows - 1);
  int cy1 = std::clamp(static_cast<int>((search_max_y - grid.min_y) / grid.cell_size), 0, grid.rows - 1);

  nav_area_id best{};
  float best_dz = std::numeric_limits<float>::max();
  float best_dist_sq = std::numeric_limits<float>::max();
  bool found = false;

  std::vector<uint32_t> seen;
  seen.reserve(32);

  for (int cy = cy0; cy <= cy1; ++cy)
  {
    for (int cx = cx0; cx <= cx1; ++cx)
    {
      for (uint32_t index : grid.cells[static_cast<size_t>(cy) * grid.columns + cx])
      {
        if (std::find(seen.begin(), seen.end(), index) != seen.end())
        {
          continue;
        }
        seen.push_back(index);

        const auto& area = cache.areas[index];
        if (!area_overlaps_player_xy_expanded(area, world, xy_pad))
        {
          continue;
        }

        float area_z = area_z_at_world_clamped(area, world);
        float dz = world.z - area_z;

        if (dz < -player_step_height)
        {
          continue;
        }

        if (!hull_can_fall_to_area(world, area))
        {
          continue;
        }

        float dist_sq = distance_to_area_sq(area, world);

        if (!found || dz < best_dz - 0.01f || (std::fabs(dz - best_dz) < 0.01f && dist_sq < best_dist_sq))
        {
          found = true;
          best = area.id;
          best_dz = dz;
          best_dist_sq = dist_sq;
        }
      }
    }
  }

  if (found)
  {
    return best;
  }

  const float fallback_pad = half_player_width + player_clearance_margin + 12.0f;
  for (int ring = 1; ring <= 2; ++ring)
  {
    int query_column = std::clamp(static_cast<int>((world.x - grid.min_x) / grid.cell_size), 0, grid.columns - 1);
    int query_row = std::clamp(static_cast<int>((world.y - grid.min_y) / grid.cell_size), 0, grid.rows - 1);
    int row_begin = query_row - ring;
    int row_end = query_row + ring;
    int column_begin = query_column - ring;
    int column_end = query_column + ring;

    for (int cy = row_begin; cy <= row_end; ++cy)
    {
      if (cy < 0 || cy >= grid.rows)
      {
        continue;
      }
      for (int cx = column_begin; cx <= column_end; ++cx)
      {
        if (cx < 0 || cx >= grid.columns)
        {
          continue;
        }
        if (std::max(std::abs(cx - query_column), std::abs(cy - query_row)) != ring)
        {
          continue;
        }
        for (uint32_t index : grid.cells[static_cast<size_t>(cy) * grid.columns + cx])
        {
          if (std::find(seen.begin(), seen.end(), index) != seen.end())
          {
            continue;
          }
          seen.push_back(index);
          const auto& area = cache.areas[index];
          if (!area_overlaps_player_xy_expanded(area, world, fallback_pad))
          {
            continue;
          }
          float area_z = area_z_at_world_clamped(area, world);
          float dz = world.z - area_z;
          if (dz < -player_step_height || dz > 120.0f)
          {
            continue;
          }
          if (!hull_can_fall_to_area(world, area))
          {
            continue;
          }
          float dist_sq = distance_to_area_sq(area, world);
          if (!found || dz < best_dz - 0.01f || (std::fabs(dz - best_dz) < 0.01f && dist_sq < best_dist_sq))
          {
            found = true;
            best = area.id;
            best_dz = dz;
            best_dist_sq = dist_sq;
          }
        }
      }
    }
    if (found)
    {
      return best;
    }
  }

  return nav_area_id{};
}

std::vector<float> build_axis_samples(float min_value, float max_value, float center_value)
{
  auto samples = std::vector<float>{};
  const auto inset = half_player_width + player_clearance_margin;
  const auto start = min_value + inset;
  const auto end = max_value - inset;

  if (end <= start)
  {
    samples.push_back(center_value);
    return samples;
  }

  const auto span = end - start;
  const auto segment_count = std::max(1, static_cast<int>(std::ceil(span / crumb_graph_spacing)));
  samples.reserve(static_cast<size_t>(segment_count) + 2);
  for (int i = 0; i <= segment_count; ++i)
  {
    const auto t = static_cast<float>(i) / static_cast<float>(segment_count);
    samples.push_back(start + span * t);
  }

  if (std::find_if(samples.begin(), samples.end(), [center_value](float sample)
    {
      return std::fabs(sample - center_value) <= crumb_graph_duplicate_distance;
    }) == samples.end())
  {
    samples.push_back(center_value);
  }

  std::sort(samples.begin(), samples.end());
  return samples;
}

bool directed_height_delta_reachable(const Vec3& from, const Vec3& to)
{
  const auto vertical_delta = to.z - from.z;
  if (vertical_delta <= player_step_height)
  {
    return true;
  }
  if (vertical_delta > player_jump_height)
  {
    return false;
  }

  const auto planar_distance = std::sqrt(distance_sq_2d_mesh(from, to));
  if (planar_distance <= 1.0f)
  {
    return false;
  }

  return vertical_delta <= (planar_distance * (walkable_ramp_height / walkable_ramp_run));
}

bool directed_area_segment_reachable(const nav_area_data& area, const Vec3& from, const Vec3& to)
{
  if (!point_inside_area_2d(area, from) || !point_inside_area_2d(area, to))
  {
    return false;
  }

  const auto planar_distance = std::sqrt(distance_sq_2d_mesh(from, to));
  const auto sample_count = std::max(1, static_cast<int>(std::ceil(planar_distance / crumb_graph_sample_spacing)));
  auto previous = nearest_point_on_area(area, from);
  for (int i = 1; i <= sample_count; ++i)
  {
    const auto t = static_cast<float>(i) / static_cast<float>(sample_count);
    auto sample = Vec3{
      from.x + (to.x - from.x) * t,
      from.y + (to.y - from.y) * t,
      0.0f
    };
    sample = nearest_point_on_area(area, sample);
    if (!directed_height_delta_reachable(previous, sample))
    {
      return false;
    }

    previous = sample;
  }

  return directed_height_delta_reachable(from, to);
}

struct mesh_portal_info
{
  Vec3 center{};
  float half_width = 0.0f;
  bool valid = false;
};

mesh_portal_info compute_mesh_portal(const nav_area_data& from_area, const nav_area_data& to_area, uint8_t direction)
{
  mesh_portal_info portal{};

  if (direction == nav_direction_north || direction == nav_direction_south)
  {
    portal.center.y = (direction == nav_direction_north) ? from_area.nw_corner.y : from_area.se_corner.y;

    auto left = std::max(from_area.nw_corner.x, to_area.nw_corner.x);
    auto right = std::min(from_area.se_corner.x, to_area.se_corner.x);
    left = std::clamp(left, from_area.nw_corner.x, from_area.se_corner.x);
    right = std::clamp(right, from_area.nw_corner.x, from_area.se_corner.x);

    portal.center.x = (left + right) * 0.5f;
    portal.half_width = std::max(0.0f, (right - left) * 0.5f);
  }
  else
  {
    portal.center.x = (direction == nav_direction_west) ? from_area.nw_corner.x : from_area.se_corner.x;

    auto top = std::max(from_area.nw_corner.y, to_area.nw_corner.y);
    auto bottom = std::min(from_area.se_corner.y, to_area.se_corner.y);
    top = std::clamp(top, from_area.nw_corner.y, from_area.se_corner.y);
    bottom = std::clamp(bottom, from_area.nw_corner.y, from_area.se_corner.y);

    portal.center.y = (top + bottom) * 0.5f;
    portal.half_width = std::max(0.0f, (bottom - top) * 0.5f);
  }

  portal.center = nearest_point_on_area(from_area, portal.center);
  portal.valid = portal.half_width * 2.0f >= player_width;
  return portal;
}

bool directed_cross_area_segment_reachable(const nav_area_data& from_area, const nav_area_data& to_area, const Vec3& from, const Vec3& to)
{
  if (!point_inside_area_2d(from_area, from) || !point_inside_area_2d(to_area, to))
  {
    return false;
  }

  if (distance_value_mesh(from, to) <= player_step_height)
  {
    return true;
  }

  if (std::sqrt(distance_sq_2d_mesh(from, to)) > crumb_graph_spacing * 1.5f)
  {
    return false;
  }

  return directed_height_delta_reachable(from, to);
}

bool is_dropdown_transition(const Vec3& from, const Vec3& next_center)
{
  return from.z - next_center.z > player_jump_height;
}

struct dropdown_waypoint
{
  bool valid = false;
  Vec3 approach{};
  Vec3 landing{};
};

dropdown_waypoint make_dropdown_waypoint(const Vec3& current, const Vec3& next_center, const nav_area_data& next_area)
{
  auto to_target = next_center - current;
  if (current.z - next_center.z <= player_jump_height)
  {
    return {};
  }

  to_target.z = 0.0f;
  const auto length = std::sqrt(distance_sq_2d_mesh(to_target, Vec3{}));
  if (length <= 1.0f)
  {
    return {};
  }

  return dropdown_waypoint{
    true,
    current + to_target * ((player_width * 2.0f) / length),
    next_area.center
  };
}

uint32_t convert_area_flags(uint32_t base_attributes, uint32_t tf_attributes)
{
  auto flags = 0u;

  if ((tf_attributes & tf_nav_has_health) != 0)
  {
    flags |= nav_area_flag_health;
  }
  if ((tf_attributes & tf_nav_has_ammo) != 0)
  {
    flags |= nav_area_flag_ammo;
  }
  if ((tf_attributes & tf_nav_control_point) != 0)
  {
    flags |= nav_area_flag_control_point;
  }
  if ((tf_attributes & tf_nav_sentry_spot) != 0)
  {
    flags |= nav_area_flag_sentry_spot;
  }
  if ((tf_attributes & tf_nav_sniper_spot) != 0)
  {
    flags |= nav_area_flag_sniper_spot;
  }
  if ((tf_attributes & (tf_nav_spawn_room_red | tf_nav_spawn_room_blue)) != 0)
  {
    flags |= nav_area_flag_spawn_room;
  }
  if ((tf_attributes & tf_nav_spawn_room_exit) != 0)
  {
    flags |= nav_area_flag_spawn_exit;
  }
  if ((base_attributes & nav_mesh_nav_blocker) != 0 || (tf_attributes & tf_nav_blocked) != 0)
  {
    flags |= nav_area_flag_blocked;
  }
  if ((tf_attributes & (tf_nav_blue_setup_gate | tf_nav_red_setup_gate)) != 0)
  {
    flags |= nav_area_flag_setup_gate;
  }
  if ((tf_attributes & (tf_nav_blue_one_way_door | tf_nav_red_one_way_door)) != 0)
  {
    flags |= nav_area_flag_one_way;
  }

  return flags;
}

bool skip_hiding_spots(nav_file_reader& reader, uint32_t version, uint8_t count)
{
  if (version == 1)
  {
    reader.skip(static_cast<size_t>(count) * sizeof(float) * 3);
    return reader.valid();
  }

  for (uint8_t i = 0; i < count; ++i)
  {
    reader.skip(4 + 4 + 4 + 4 + 1);
  }

  return reader.valid();
}

bool skip_approach_areas(nav_file_reader& reader)
{
  auto count = reader.read_u8();
  for (uint8_t i = 0; i < count; ++i)
  {
    reader.skip(4 + 4 + 1 + 4 + 1);
  }

  return reader.valid();
}

bool skip_encounter_paths(nav_file_reader& reader, uint32_t version)
{
  auto encounter_count = reader.read_u32();
  if (version < 3)
  {
    for (uint32_t i = 0; i < encounter_count; ++i)
    {
      reader.skip(4 + 4 + 6 * 4);
      auto spot_count = reader.read_u8();
      reader.skip(static_cast<size_t>(spot_count) * (4 * 4));
    }

    return reader.valid();
  }

  for (uint32_t i = 0; i < encounter_count; ++i)
  {
    reader.skip(4 + 1 + 4 + 1);
    auto spot_count = reader.read_u8();
    reader.skip(static_cast<size_t>(spot_count) * (4 + 1));
  }

  return reader.valid();
}

bool skip_ladder_links(nav_file_reader& reader)
{
  constexpr uint32_t ladder_direction_count = 2;
  for (uint32_t dir = 0; dir < ladder_direction_count; ++dir)
  {
    auto count = reader.read_u32();
    reader.skip(static_cast<size_t>(count) * 4);
  }

  return reader.valid();
}

bool skip_ladders(nav_file_reader& reader, uint32_t version)
{
  auto ladder_count = reader.read_u32();
  for (uint32_t i = 0; i < ladder_count; ++i)
  {
    reader.skip(4 + 4 + 3 * 4 + 3 * 4 + 4 + 4);
    if (version == 6)
    {
      reader.skip(1);
    }
    reader.skip(5 * 4);
  }

  return reader.valid();
}

bool parse_place_directory(nav_file_reader& reader, uint32_t version)
{
  if (version < 5)
  {
    return true;
  }

  auto count = reader.read_u16();
  for (uint16_t i = 0; i < count; ++i)
  {
    auto length = reader.read_u16();
    reader.read_string(length);
  }

  if (version > 11)
  {
    reader.read_u8();
  }

  return reader.valid();
}

bool parse_area(nav_file_reader& reader, uint32_t version, uint32_t sub_version, nav_area_data& area)
{
  area.id.value = reader.read_u32();

  if (version <= 7)
  {
    area.base_attributes = reader.read_u8();
  }
  else if (version < 13)
  {
    area.base_attributes = reader.read_u16();
  }
  else
  {
    area.base_attributes = static_cast<uint32_t>(reader.read_i32());
  }

  area.nw_corner = reader.read_vec3();
  area.se_corner = reader.read_vec3();
  area.ne_z = reader.read_f32();
  area.sw_z = reader.read_f32();

  area.center.x = (area.nw_corner.x + area.se_corner.x) * 0.5f;
  area.center.y = (area.nw_corner.y + area.se_corner.y) * 0.5f;
  area.center.z = (area.nw_corner.z + area.se_corner.z) * 0.5f;

  auto min_z = std::min(std::min(area.nw_corner.z, area.se_corner.z), std::min(area.ne_z, area.sw_z));
  auto max_z = std::max(std::max(area.nw_corner.z, area.se_corner.z), std::max(area.ne_z, area.sw_z));
  area.mins = Vec3{area.nw_corner.x, area.nw_corner.y, min_z};
  area.maxs = Vec3{area.se_corner.x, area.se_corner.y, max_z};

  area.connections.clear();
  for (uint32_t dir = 0; dir < 4; ++dir)
  {
    auto count = reader.read_u32();
    for (uint32_t i = 0; i < count; ++i)
    {
      auto connection_id = reader.read_u32();
      if (connection_id == 0 || connection_id == area.id.value)
      {
        continue;
      }

      area.connections.push_back(nav_connection{nav_area_id{connection_id}, static_cast<uint8_t>(dir)});
    }
  }

  auto hiding_spot_count = reader.read_u8();
  if (!skip_hiding_spots(reader, version, hiding_spot_count))
  {
    return false;
  }

  if (version < 15 && !skip_approach_areas(reader))
  {
    return false;
  }

  if (!skip_encounter_paths(reader, version))
  {
    return false;
  }

  if (version >= 5)
  {
    area.place_index = reader.read_u16();
  }

  if (version >= 7 && !skip_ladder_links(reader))
  {
    return false;
  }

  if (version >= 8)
  {
    reader.skip(2 * 4);
  }

  if (version >= 11)
  {
    reader.skip(4 * 4);
  }

  if (version >= 16)
  {
    auto visible_area_count = reader.read_u32();
    reader.skip(static_cast<size_t>(visible_area_count) * (4 + 1));
    reader.skip(4);
  }

  if (sub_version >= tf_nav_sub_version_with_attributes)
  {
    area.tf_attributes = reader.read_u32();
  }
  else
  {
    area.tf_attributes = 0;
  }

  area.flags = convert_area_flags(area.base_attributes, area.tf_attributes);
  return reader.valid();
}

}

bool navbot_mesh::rebuild_from_current_map()
{
  clear();

  if (engine == nullptr)
  {
    navbot_log("mesh rebuild skipped: engine unavailable\n");
    return false;
  }

  if (!engine->is_in_game())
  {
    navbot_log("mesh rebuild skipped: not in game\n");
    return false;
  }

  const auto* raw_level_name = engine->get_level_name();
  cache_->map_name = sanitize_map_name(raw_level_name);
  navbot_log(
    "mesh rebuild requested raw_level='%s' map='%s'\n",
    raw_level_name != nullptr ? raw_level_name : "",
    cache_->map_name.c_str());
  if (cache_->map_name.empty())
  {
    navbot_log("mesh rebuild failed: empty map name\n");
    return false;
  }

  auto loaded = load_current_map_file();
  if (loaded)
  {
    rebuild_categories();
    build_spatial_index();
    build_crumb_graph();
  }

  cache_->loaded = loaded;
  navbot_log(
    "mesh rebuild finished map='%s' loaded=%d areas=%zu nav='%s'\n",
    cache_->map_name.c_str(),
    cache_->loaded ? 1 : 0,
    cache_->areas.size(),
    cache_->nav_file_path.c_str());
  return cache_->loaded;
}

void navbot_mesh::clear()
{
  cache_ = std::make_shared<nav_mesh_cache>();
}

void navbot_mesh::clear_nav_data()
{
  cache_->loaded = false;
  cache_->areas.clear();
  cache_->area_lookup.clear();
  cache_->grid = {};
  cache_->crumb_nodes.clear();
  cache_->crumb_nodes_by_area.clear();
  cache_->health_areas.clear();
  cache_->ammo_areas.clear();
  cache_->control_point_areas.clear();
  cache_->sentry_spot_areas.clear();
  cache_->sniper_spot_areas.clear();
  cache_->spawn_room_areas.clear();
  cache_->spawn_exit_areas.clear();
  cache_->payload_areas.clear();
  cache_->flag_areas.clear();
}

bool navbot_mesh::is_ready() const
{
  return cache_->loaded && !cache_->areas.empty();
}

const std::string& navbot_mesh::map_name() const
{
  return cache_->map_name;
}

const std::string& navbot_mesh::nav_file_path() const
{
  return cache_->nav_file_path;
}

const nav_mesh_cache& navbot_mesh::cache() const
{
  return *cache_;
}

const nav_area_data* navbot_mesh::find_area(nav_area_id id) const
{
  auto it = cache_->area_lookup.find(id.value);
  if (it == cache_->area_lookup.end())
  {
    return nullptr;
  }

  return &cache_->areas[it->second];
}

nav_area_id navbot_mesh::find_closest_area_linear(const Vec3& world) const
{
  if (!cache_->areas.empty())
  {
    auto overlapping = try_find_overlapping_area_below_linear(world, cache_->areas);
    if (overlapping.valid())
    {
      return overlapping;
    }
  }

  nav_area_id best{};
  auto best_distance = 0.0f;
  auto found = false;

  for (const auto& area : cache_->areas)
  {
    auto distance = distance_to_area_sq(area, world);
    if (!found || distance < best_distance)
    {
      found = true;
      best = area.id;
      best_distance = distance;
    }
  }

  return best;
}

nav_area_id navbot_mesh::find_closest_area(const Vec3& world) const
{
  const auto& grid = cache_->grid;
  if (!grid.valid())
  {
    return find_closest_area_linear(world);
  }

  {
    auto overlapping = try_find_overlapping_area_below_grid(world, *cache_);
    if (overlapping.valid())
    {
      return overlapping;
    }
    auto linear_overlap = try_find_overlapping_area_below_linear(world, cache_->areas);
    if (linear_overlap.valid())
    {
      return linear_overlap;
    }
  }

  const int query_column =
    std::clamp(static_cast<int>((world.x - grid.min_x) / grid.cell_size), 0, grid.columns - 1);
  const int query_row =
    std::clamp(static_cast<int>((world.y - grid.min_y) / grid.cell_size), 0, grid.rows - 1);

  nav_area_id best{};
  float best_distance = std::numeric_limits<float>::max();
  bool found = false;
  const int max_ring = std::max(grid.columns, grid.rows);

  for (int ring = 0; ring <= max_ring; ++ring)
  {
    if (found && ring > 0)
    {
      const float ring_min_distance = static_cast<float>(ring - 1) * grid.cell_size;
      if (ring_min_distance * ring_min_distance > best_distance)
      {
        break;
      }
    }

    const int row_begin = query_row - ring;
    const int row_end = query_row + ring;
    const int column_begin = query_column - ring;
    const int column_end = query_column + ring;

    for (int cy = row_begin; cy <= row_end; ++cy)
    {
      if (cy < 0 || cy >= grid.rows)
      {
        continue;
      }

      for (int cx = column_begin; cx <= column_end; ++cx)
      {
        if (cx < 0 || cx >= grid.columns)
        {
          continue;
        }

        if (ring > 0 && std::max(std::abs(cx - query_column), std::abs(cy - query_row)) != ring)
        {
          continue;
        }

        for (uint32_t index : grid.cells[static_cast<size_t>(cy) * grid.columns + cx])
        {
          const auto& area = cache_->areas[index];
          const float distance = distance_to_area_sq(area, world);
          if (!found || distance < best_distance)
          {
            found = true;
            best = area.id;
            best_distance = distance;
          }
        }
      }
    }
  }

  if (!found)
  {
    return find_closest_area_linear(world);
  }

  return best;
}

void navbot_mesh::build_spatial_index()
{
  auto& grid = cache_->grid;
  grid = {};

  if (cache_->areas.empty())
  {
    return;
  }

  float min_x = std::numeric_limits<float>::max();
  float min_y = std::numeric_limits<float>::max();
  float max_x = std::numeric_limits<float>::lowest();
  float max_y = std::numeric_limits<float>::lowest();
  for (const auto& area : cache_->areas)
  {
    min_x = std::min(min_x, std::min(area.nw_corner.x, area.se_corner.x));
    min_y = std::min(min_y, std::min(area.nw_corner.y, area.se_corner.y));
    max_x = std::max(max_x, std::max(area.nw_corner.x, area.se_corner.x));
    max_y = std::max(max_y, std::max(area.nw_corner.y, area.se_corner.y));
  }

  constexpr float cell_size = 256.0f;
  constexpr int max_dimension = 512;

  const float span_x = std::max(0.0f, max_x - min_x);
  const float span_y = std::max(0.0f, max_y - min_y);
  const int columns = std::clamp(static_cast<int>(span_x / cell_size) + 1, 1, max_dimension);
  const int rows = std::clamp(static_cast<int>(span_y / cell_size) + 1, 1, max_dimension);

  grid.min_x = min_x;
  grid.min_y = min_y;
  grid.cell_size = cell_size;
  grid.columns = columns;
  grid.rows = rows;
  grid.cells.assign(static_cast<size_t>(columns) * static_cast<size_t>(rows), {});

  for (uint32_t index = 0; index < cache_->areas.size(); ++index)
  {
    const auto& area = cache_->areas[index];
    const float lo_x = std::min(area.nw_corner.x, area.se_corner.x);
    const float hi_x = std::max(area.nw_corner.x, area.se_corner.x);
    const float lo_y = std::min(area.nw_corner.y, area.se_corner.y);
    const float hi_y = std::max(area.nw_corner.y, area.se_corner.y);

    const int cx0 = std::clamp(static_cast<int>((lo_x - min_x) / cell_size), 0, columns - 1);
    const int cx1 = std::clamp(static_cast<int>((hi_x - min_x) / cell_size), 0, columns - 1);
    const int cy0 = std::clamp(static_cast<int>((lo_y - min_y) / cell_size), 0, rows - 1);
    const int cy1 = std::clamp(static_cast<int>((hi_y - min_y) / cell_size), 0, rows - 1);

    for (int cy = cy0; cy <= cy1; ++cy)
    {
      for (int cx = cx0; cx <= cx1; ++cx)
      {
        grid.cells[static_cast<size_t>(cy) * columns + cx].push_back(index);
      }
    }
  }
}

void navbot_mesh::build_crumb_graph()
{
  cache_->crumb_nodes.clear();
  cache_->crumb_nodes_by_area.clear();

  if (cache_->areas.empty())
  {
    return;
  }

  cache_->crumb_nodes.reserve(cache_->areas.size() * 4);
  cache_->crumb_nodes_by_area.reserve(cache_->areas.size());

  const auto duplicate_distance_sq = crumb_graph_duplicate_distance * crumb_graph_duplicate_distance;
  auto add_node = [this, duplicate_distance_sq](const nav_area_data& area, const Vec3& point) -> uint32_t
  {
    auto world = nearest_point_on_area(area, point);
    auto& area_nodes = cache_->crumb_nodes_by_area[area.id.value];
    for (auto node_index : area_nodes)
    {
      const auto& node = cache_->crumb_nodes[node_index];
      if (distance_sq_2d_mesh(node.world, world) <= duplicate_distance_sq && std::fabs(node.world.z - world.z) <= player_step_height)
      {
        return node_index;
      }
    }

    const auto node_index = static_cast<uint32_t>(cache_->crumb_nodes.size());
    cache_->crumb_nodes.push_back(nav_crumb_node{area.id, world, {}});
    area_nodes.push_back(node_index);
    return node_index;
  };

  for (const auto& area : cache_->areas)
  {
    const auto xs = build_axis_samples(area.mins.x, area.maxs.x, area.center.x);
    const auto ys = build_axis_samples(area.mins.y, area.maxs.y, area.center.y);
    for (auto x : xs)
    {
      for (auto y : ys)
      {
        add_node(area, Vec3{x, y, 0.0f});
      }
    }

    add_node(area, area.center);
  }

  for (const auto& area : cache_->areas)
  {
    for (const auto& connection : area.connections)
    {
      const auto* next = find_area(connection.area_id);
      if (next == nullptr)
      {
        continue;
      }

      auto portal = compute_mesh_portal(area, *next, connection.direction);
      if (!portal.valid)
      {
        continue;
      }

      add_node(area, portal.center);
      add_node(*next, nearest_point_on_area(*next, portal.center));
    }
  }

  auto add_directed_edge = [this](uint32_t from_node,
    uint32_t to_node,
    nav_edge_id nav_edge,
    bool is_dropdown,
    const dropdown_waypoint& waypoint)
  {
    if (from_node == to_node || from_node >= cache_->crumb_nodes.size() || to_node >= cache_->crumb_nodes.size())
    {
      return;
    }

    auto& edges = cache_->crumb_nodes[from_node].edges;
    for (const auto& edge : edges)
    {
      if (edge.to_node == to_node
        && edge.nav_edge.from_area == nav_edge.from_area
        && edge.nav_edge.connection_index == nav_edge.connection_index)
      {
        return;
      }
    }

    const auto cost = distance_value_mesh(cache_->crumb_nodes[from_node].world, cache_->crumb_nodes[to_node].world);
    edges.push_back(nav_crumb_edge{
      to_node,
      nav_edge,
      cost,
      is_dropdown,
      waypoint.valid,
      waypoint.approach,
      waypoint.landing
    });
  };

  const auto connect_distance_sq = crumb_graph_connect_distance * crumb_graph_connect_distance;
  for (const auto& area : cache_->areas)
  {
    auto area_nodes_it = cache_->crumb_nodes_by_area.find(area.id.value);
    if (area_nodes_it == cache_->crumb_nodes_by_area.end())
    {
      continue;
    }

    const auto& area_nodes = area_nodes_it->second;
    for (size_t i = 0; i < area_nodes.size(); ++i)
    {
      for (size_t j = i + 1; j < area_nodes.size(); ++j)
      {
        const auto from_node = area_nodes[i];
        const auto to_node = area_nodes[j];
        const auto& from = cache_->crumb_nodes[from_node].world;
        const auto& to = cache_->crumb_nodes[to_node].world;
        if (distance_sq_2d_mesh(from, to) > connect_distance_sq)
        {
          continue;
        }

        if (directed_area_segment_reachable(area, from, to))
        {
          add_directed_edge(from_node, to_node, {}, false, {});
        }
        if (directed_area_segment_reachable(area, to, from))
        {
          add_directed_edge(to_node, from_node, {}, false, {});
        }
      }
    }
  }

  for (const auto& area : cache_->areas)
  {
    for (uint16_t connection_index = 0; connection_index < area.connections.size(); ++connection_index)
    {
      const auto& connection = area.connections[connection_index];
      const auto* next = find_area(connection.area_id);
      if (next == nullptr)
      {
        continue;
      }

      auto portal = compute_mesh_portal(area, *next, connection.direction);
      if (!portal.valid)
      {
        continue;
      }

      const auto from_node = add_node(area, portal.center);
      const auto to_node = add_node(*next, nearest_point_on_area(*next, portal.center));
      const auto& from = cache_->crumb_nodes[from_node].world;
      const auto& to = cache_->crumb_nodes[to_node].world;
      if (!directed_cross_area_segment_reachable(area, *next, from, to))
      {
        continue;
      }

      const auto dropdown = is_dropdown_transition(from, next->center);
      const auto waypoint = dropdown ? make_dropdown_waypoint(from, next->center, *next) : dropdown_waypoint{};
      add_directed_edge(
        from_node,
        to_node,
        nav_edge_id{area.id.value, connection_index},
        dropdown,
        waypoint);
    }
  }

  size_t edge_count = 0;
  for (const auto& node : cache_->crumb_nodes)
  {
    edge_count += node.edges.size();
  }

  navbot_log("crumb graph built nodes=%zu edges=%zu spacing=%.0f\n", cache_->crumb_nodes.size(), edge_count, crumb_graph_spacing);
}

Vec3 navbot_mesh::get_nearest_point(nav_area_id area_id, const Vec3& world) const
{
  auto area = find_area(area_id);
  if (area == nullptr)
  {
    return world;
  }

  return nearest_point_on_area(*area, world);
}

bool navbot_mesh::area_has_flag(nav_area_id area_id, uint32_t flag) const
{
  auto area = find_area(area_id);
  if (area == nullptr)
  {
    return false;
  }

  return (area->flags & flag) != 0;
}

std::vector<navbot_mesh::nearby_area> navbot_mesh::areas_in_radius(const Vec3& origin, float radius) const
{
  std::vector<nearby_area> result;
  if (radius <= 0.0f)
  {
    return result;
  }

  const auto radius_sq = radius * radius;
  result.reserve(64);

  const auto& grid = cache_->grid;
  if (!grid.valid())
  {
    for (const auto& area : cache_->areas)
    {
      auto distance_sq = distance_to_area_sq(area, origin);
      if (distance_sq <= radius_sq)
      {
        result.push_back(nearby_area{area.id, distance_sq});
      }
    }

    return result;
  }

  const int cx0 = std::clamp(static_cast<int>((origin.x - radius - grid.min_x) / grid.cell_size), 0, grid.columns - 1);
  const int cx1 = std::clamp(static_cast<int>((origin.x + radius - grid.min_x) / grid.cell_size), 0, grid.columns - 1);
  const int cy0 = std::clamp(static_cast<int>((origin.y - radius - grid.min_y) / grid.cell_size), 0, grid.rows - 1);
  const int cy1 = std::clamp(static_cast<int>((origin.y + radius - grid.min_y) / grid.cell_size), 0, grid.rows - 1);

  for (int cy = cy0; cy <= cy1; ++cy)
  {
    for (int cx = cx0; cx <= cx1; ++cx)
    {
      for (uint32_t index : grid.cells[static_cast<size_t>(cy) * grid.columns + cx])
      {
        const auto& area = cache_->areas[index];
        const auto distance_sq = distance_to_area_sq(area, origin);
        if (distance_sq > radius_sq)
        {
          continue;
        }

        if (std::find_if(result.begin(), result.end(), [&](const nearby_area& entry) {
              return entry.id.value == area.id.value;
            }) != result.end())
        {
          continue;
        }

        result.push_back(nearby_area{area.id, distance_sq});
      }
    }
  }

  return result;
}

bool navbot_mesh::load_current_map_file()
{
  auto nav_path = resolve_nav_path(cache_->map_name);
  if (nav_path.empty())
  {
    cache_->nav_file_path.clear();
    navbot_log("load failed map='%s': nav file not found\n", cache_->map_name.c_str());
    return false;
  }

  cache_->nav_file_path = nav_path.string();

  auto bytes = read_file_bytes(nav_path);
  if (bytes.empty())
  {
    navbot_log("load failed path='%s': file is empty or unreadable\n", cache_->nav_file_path.c_str());
    return false;
  }

  auto reader = nav_file_reader(std::move(bytes));

  auto magic = reader.read_u32();
  if (!reader.valid() || magic != nav_magic_number)
  {
    navbot_log("load failed path='%s': bad magic 0x%08x\n", cache_->nav_file_path.c_str(), magic);
    return false;
  }

  auto version = reader.read_u32();
  if (!reader.valid() || version < 4 || version > nav_current_version)
  {
    navbot_log("load failed path='%s': unsupported version %u\n", cache_->nav_file_path.c_str(), version);
    return false;
  }

  auto sub_version = 0u;
  if (version >= 10)
  {
    sub_version = reader.read_u32();
  }

  if (version >= 4)
  {
    reader.read_u32();
  }

  if (version >= 14)
  {
    reader.read_u8();
  }

  if (!parse_place_directory(reader, version))
  {
    navbot_log("load failed path='%s': place directory parse failed\n", cache_->nav_file_path.c_str());
    return false;
  }

  auto area_count = reader.read_u32();
  if (!reader.valid() || area_count == 0)
  {
    navbot_log("load failed path='%s': invalid area count %u\n", cache_->nav_file_path.c_str(), area_count);
    return false;
  }

  cache_->areas.clear();
  cache_->areas.reserve(area_count);
  cache_->area_lookup.clear();
  cache_->area_lookup.reserve(area_count);

  for (uint32_t i = 0; i < area_count; ++i)
  {
    nav_area_data area{};
    if (!parse_area(reader, version, sub_version, area))
    {
      navbot_log("load failed path='%s': area %u parse failed\n", cache_->nav_file_path.c_str(), i);
      clear_nav_data();
      return false;
    }

    cache_->areas.push_back(std::move(area));
  }

  const auto has_ladder_table = version >= 6 && reader.has_bytes(4);
  if (has_ladder_table && !skip_ladders(reader, version))
  {
    navbot_log("load failed path='%s': ladder parse failed\n", cache_->nav_file_path.c_str());
    clear_nav_data();
    return false;
  }

  if (version >= 6 && !has_ladder_table)
  {
    if (reader.remaining() != 0)
    {
      navbot_log(
        "load failed path='%s': truncated ladder header remaining=%zu offset=%zu\n",
        cache_->nav_file_path.c_str(),
        reader.remaining(),
        reader.offset());
      clear_nav_data();
      return false;
    }

    navbot_log(
      "load path='%s': no trailing ladder table remaining=%zu offset=%zu\n",
      cache_->nav_file_path.c_str(),
      reader.remaining(),
      reader.offset());
  }

  if (!reader.valid() || cache_->areas.empty())
  {
    navbot_log("load failed path='%s': reader invalid after parse\n", cache_->nav_file_path.c_str());
    return false;
  }

  navbot_log(
    "load succeeded path='%s' version=%u sub_version=%u areas=%zu\n",
    cache_->nav_file_path.c_str(),
    version,
    sub_version,
    cache_->areas.size());
  return true;
}

void navbot_mesh::rebuild_categories()
{
  cache_->health_areas.clear();
  cache_->ammo_areas.clear();
  cache_->control_point_areas.clear();
  cache_->sentry_spot_areas.clear();
  cache_->sniper_spot_areas.clear();
  cache_->spawn_room_areas.clear();
  cache_->spawn_exit_areas.clear();
  cache_->payload_areas.clear();
  cache_->flag_areas.clear();

  cache_->health_areas.reserve(cache_->areas.size());
  cache_->ammo_areas.reserve(cache_->areas.size());
  cache_->control_point_areas.reserve(cache_->areas.size());
  cache_->sentry_spot_areas.reserve(cache_->areas.size());
  cache_->sniper_spot_areas.reserve(cache_->areas.size());
  cache_->spawn_room_areas.reserve(cache_->areas.size());
  cache_->spawn_exit_areas.reserve(cache_->areas.size());

  for (uint32_t area_index = 0; area_index < cache_->areas.size(); ++area_index)
  {
    auto& area = cache_->areas[area_index];
    cache_->area_lookup[area.id.value] = area_index;

    if ((area.flags & nav_area_flag_health) != 0)
    {
      cache_->health_areas.emplace_back(area.id);
    }
    if ((area.flags & nav_area_flag_ammo) != 0)
    {
      cache_->ammo_areas.emplace_back(area.id);
    }
    if ((area.flags & nav_area_flag_control_point) != 0)
    {
      cache_->control_point_areas.emplace_back(area.id);
    }
    if ((area.flags & nav_area_flag_sentry_spot) != 0)
    {
      cache_->sentry_spot_areas.emplace_back(area.id);
    }
    if ((area.flags & nav_area_flag_sniper_spot) != 0)
    {
      cache_->sniper_spot_areas.emplace_back(area.id);
    }
    if ((area.flags & nav_area_flag_spawn_room) != 0)
    {
      cache_->spawn_room_areas.emplace_back(area.id);
    }
    if ((area.flags & nav_area_flag_spawn_exit) != 0)
    {
      cache_->spawn_exit_areas.emplace_back(area.id);
    }
  }
}

bool navmesh_resolves_for_current_map()
{
  if (engine == nullptr || !engine->is_in_game())
  {
    return false;
  }

  const auto* raw_level_name = engine->get_level_name();
  const std::string map_name = sanitize_map_name(raw_level_name);
  if (map_name.empty())
  {
    return false;
  }

  static std::string cached_map_name{};
  static bool cached_result = false;
  static std::chrono::steady_clock::time_point next_lookup{};
  const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  if (cached_map_name == map_name && now < next_lookup)
  {
    return cached_result;
  }

  cached_map_name = map_name;
  cached_result = !resolve_nav_path(map_name).empty();
  next_lookup = now + navmesh_resolve_refresh_interval;
  return cached_result;
}

}
