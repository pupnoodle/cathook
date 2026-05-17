/*
/^-----^\   data: 2026-04-05
V  o o  V  file: src/features/automation/navbot/navbot_mesh.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef NAVBOT_MESH_HPP
#define NAVBOT_MESH_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/types.hpp"

#include "features/automation/navbot/navbot_types.hpp"

namespace navbot
{

enum nav_area_flags : uint32_t
{
  nav_area_flag_none = 0,
  nav_area_flag_health = 1 << 0,
  nav_area_flag_ammo = 1 << 1,
  nav_area_flag_control_point = 1 << 2,
  nav_area_flag_sentry_spot = 1 << 3,
  nav_area_flag_sniper_spot = 1 << 4,
  nav_area_flag_spawn_room = 1 << 5,
  nav_area_flag_spawn_exit = 1 << 6,
  nav_area_flag_blocked = 1 << 7,
  nav_area_flag_setup_gate = 1 << 8,
  nav_area_flag_one_way = 1 << 9,
  nav_area_flag_payload = 1 << 10,
  nav_area_flag_flag = 1 << 11
};

enum nav_direction : uint8_t
{
  nav_direction_north = 0,
  nav_direction_east = 1,
  nav_direction_south = 2,
  nav_direction_west = 3,
  nav_direction_count = 4
};

struct nav_connection
{
  nav_area_id area_id{};
  uint8_t direction = nav_direction_north;
};

struct nav_area_data
{
  nav_area_id id{};
  Vec3 center{};
  Vec3 mins{};
  Vec3 maxs{};
  Vec3 nw_corner{};
  Vec3 se_corner{};
  float ne_z = 0.0f;
  float sw_z = 0.0f;
  uint32_t base_attributes = 0;
  uint32_t tf_attributes = 0;
  uint16_t place_index = 0;
  uint32_t flags = 0;
  std::vector<nav_connection> connections{};
};

struct nav_mesh_cache
{
  bool loaded = false;
  std::string map_name{};
  std::string nav_file_path{};
  std::vector<nav_area_data> areas{};
  std::unordered_map<uint32_t, uint32_t> area_lookup{};
  std::vector<nav_area_id> health_areas{};
  std::vector<nav_area_id> ammo_areas{};
  std::vector<nav_area_id> control_point_areas{};
  std::vector<nav_area_id> sentry_spot_areas{};
  std::vector<nav_area_id> sniper_spot_areas{};
  std::vector<nav_area_id> spawn_room_areas{};
  std::vector<nav_area_id> spawn_exit_areas{};
  std::vector<nav_area_id> payload_areas{};
  std::vector<nav_area_id> flag_areas{};
};

class navbot_mesh
{
public:
  bool rebuild_from_current_map();
  void clear();

  [[nodiscard]] bool is_ready() const;
  [[nodiscard]] const std::string& map_name() const;
  [[nodiscard]] const std::string& nav_file_path() const;
  [[nodiscard]] const nav_mesh_cache& cache() const;
  [[nodiscard]] const nav_area_data* find_area(nav_area_id id) const;
  [[nodiscard]] nav_area_id find_closest_area(const Vec3& world) const;
  [[nodiscard]] Vec3 get_nearest_point(nav_area_id area_id, const Vec3& world) const;
  [[nodiscard]] std::optional<uint16_t> find_connection_index(nav_area_id from_area, nav_area_id to_area) const;
  [[nodiscard]] bool area_has_flag(nav_area_id area_id, uint32_t flag) const;

private:
  bool load_current_map_file();
  void clear_nav_data();
  void rebuild_categories();

  nav_mesh_cache cache_{};
};

[[nodiscard]] bool navmesh_resolves_for_current_map();

} // namespace navbot

#endif
