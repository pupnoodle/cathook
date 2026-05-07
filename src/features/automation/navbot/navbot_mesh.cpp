/*
/^-----^\   data: 2026-04-05
V  o o  V  file: src/features/automation/navbot/navbot_mesh.cpp
 |  Y  |   autor: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "features/automation/navbot/navbot_mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "core/math/math.hpp"

#include "games/tf2/sdk/interfaces/engine.hpp"

namespace navbot
{

namespace
{

constexpr uint32_t nav_magic_number = 0xFEEDFACEu;
constexpr uint32_t nav_current_version = 16u;
constexpr uint32_t nav_mesh_nav_blocker = 0x80000000u;

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

std::filesystem::path resolve_nav_path(const std::string& map_name)
{
  auto search_roots = std::vector<std::filesystem::path>{};
  auto current_root = std::filesystem::current_path();
  search_roots.push_back(current_root);

  for (int depth = 0; depth < 6; ++depth)
  {
    current_root = current_root.parent_path();
    if (current_root.empty())
    {
      break;
    }

    search_roots.push_back(current_root);
  }

  if (const auto* home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
  {
    const auto home_path = std::filesystem::path(home);
    search_roots.push_back(home_path / ".steam/steam/steamapps/common/Team Fortress 2");
    search_roots.push_back(home_path / ".steam/debian-installation/steamapps/common/Team Fortress 2");
    search_roots.push_back(home_path / ".local/share/Steam/steamapps/common/Team Fortress 2");
  }

  search_roots.push_back("/opt/cathook/navmeshes");
  search_roots.push_back("/opt/steamapps/common/Team Fortress 2");

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
      if (std::filesystem::exists(candidate))
      {
        return std::filesystem::weakly_canonical(candidate);
      }
    }
  }

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

  if (version <= 8)
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

      area.connections.push_back(nav_connection{nav_area_id{connection_id}});
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

} // namespace

bool navbot_mesh::rebuild_from_current_map()
{
  clear();

  if (engine == nullptr || !engine->is_in_game())
  {
    return false;
  }

  cache_.map_name = sanitize_map_name(engine->get_level_name());
  if (cache_.map_name.empty())
  {
    return false;
  }

  auto loaded = load_current_map_file();
  if (loaded)
  {
    rebuild_categories();
  }

  cache_.loaded = loaded;
  return cache_.loaded;
}

void navbot_mesh::clear()
{
  cache_ = {};
}

bool navbot_mesh::is_ready() const
{
  return cache_.loaded && !cache_.areas.empty();
}

const std::string& navbot_mesh::map_name() const
{
  return cache_.map_name;
}

const std::string& navbot_mesh::nav_file_path() const
{
  return cache_.nav_file_path;
}

const nav_mesh_cache& navbot_mesh::cache() const
{
  return cache_;
}

const nav_area_data* navbot_mesh::find_area(nav_area_id id) const
{
  auto it = cache_.area_lookup.find(id.value);
  if (it == cache_.area_lookup.end())
  {
    return nullptr;
  }

  return &cache_.areas[it->second];
}

nav_area_id navbot_mesh::find_closest_area(const Vec3& world) const
{
  nav_area_id best{};
  auto best_distance = 0.0f;
  auto found = false;

  for (const auto& area : cache_.areas)
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

Vec3 navbot_mesh::get_nearest_point(nav_area_id area_id, const Vec3& world) const
{
  auto area = find_area(area_id);
  if (area == nullptr)
  {
    return world;
  }

  return nearest_point_on_area(*area, world);
}

std::optional<uint16_t> navbot_mesh::find_connection_index(nav_area_id from_area, nav_area_id to_area) const
{
  auto area = find_area(from_area);
  if (area == nullptr)
  {
    return std::nullopt;
  }

  for (uint16_t connection_index = 0; connection_index < area->connections.size(); ++connection_index)
  {
    if (area->connections[connection_index].area_id.value == to_area.value)
    {
      return connection_index;
    }
  }

  return std::nullopt;
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

bool navbot_mesh::load_current_map_file()
{
  auto nav_path = resolve_nav_path(cache_.map_name);
  if (nav_path.empty())
  {
    cache_.nav_file_path.clear();
    return false;
  }

  cache_.nav_file_path = nav_path.string();

  auto bytes = read_file_bytes(nav_path);
  if (bytes.empty())
  {
    return false;
  }

  auto reader = nav_file_reader(std::move(bytes));

  auto magic = reader.read_u32();
  if (!reader.valid() || magic != nav_magic_number)
  {
    return false;
  }

  auto version = reader.read_u32();
  if (!reader.valid() || version < 4 || version > nav_current_version)
  {
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
    return false;
  }

  auto area_count = reader.read_u32();
  if (!reader.valid() || area_count == 0)
  {
    return false;
  }

  cache_.areas.clear();
  cache_.areas.reserve(area_count);
  cache_.area_lookup.clear();
  cache_.area_lookup.reserve(area_count);

  for (uint32_t i = 0; i < area_count; ++i)
  {
    nav_area_data area{};
    if (!parse_area(reader, version, sub_version, area))
    {
      clear();
      return false;
    }

    cache_.areas.push_back(std::move(area));
  }

  if (version >= 6 && !skip_ladders(reader, version))
  {
    clear();
    return false;
  }

  return reader.valid() && !cache_.areas.empty();
}

void navbot_mesh::rebuild_categories()
{
  cache_.health_areas.clear();
  cache_.ammo_areas.clear();
  cache_.control_point_areas.clear();
  cache_.sentry_spot_areas.clear();
  cache_.sniper_spot_areas.clear();
  cache_.spawn_room_areas.clear();
  cache_.spawn_exit_areas.clear();
  cache_.payload_areas.clear();
  cache_.flag_areas.clear();

  cache_.health_areas.reserve(cache_.areas.size());
  cache_.ammo_areas.reserve(cache_.areas.size());
  cache_.control_point_areas.reserve(cache_.areas.size());
  cache_.sentry_spot_areas.reserve(cache_.areas.size());
  cache_.sniper_spot_areas.reserve(cache_.areas.size());
  cache_.spawn_room_areas.reserve(cache_.areas.size());
  cache_.spawn_exit_areas.reserve(cache_.areas.size());

  for (uint32_t area_index = 0; area_index < cache_.areas.size(); ++area_index)
  {
    auto& area = cache_.areas[area_index];
    cache_.area_lookup[area.id.value] = area_index;

    if ((area.flags & nav_area_flag_health) != 0)
    {
      cache_.health_areas.emplace_back(area.id);
    }
    if ((area.flags & nav_area_flag_ammo) != 0)
    {
      cache_.ammo_areas.emplace_back(area.id);
    }
    if ((area.flags & nav_area_flag_control_point) != 0)
    {
      cache_.control_point_areas.emplace_back(area.id);
    }
    if ((area.flags & nav_area_flag_sentry_spot) != 0)
    {
      cache_.sentry_spot_areas.emplace_back(area.id);
    }
    if ((area.flags & nav_area_flag_sniper_spot) != 0)
    {
      cache_.sniper_spot_areas.emplace_back(area.id);
    }
    if ((area.flags & nav_area_flag_spawn_room) != 0)
    {
      cache_.spawn_room_areas.emplace_back(area.id);
    }
    if ((area.flags & nav_area_flag_spawn_exit) != 0)
    {
      cache_.spawn_exit_areas.emplace_back(area.id);
    }
  }
}

} // namespace navbot
