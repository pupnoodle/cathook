/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/visuals/esp/esp.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "core/logger.hpp"
#include "core/entity_cache.hpp"
#include "core/math/math.hpp"
#include "core/player_manager.hpp"
#include "features/combat/aimbot/aimbot.hpp"
#include "features/combat/backtrack/backtrack.hpp"
#include "features/menu/config.hpp"
#include "features/visuals/thirdperson.hpp"
#include "features/visuals/overlay_projection.hpp"
#include "features/combat/anti_aim/anti_aim.hpp"
#include "features/visuals/groups/visual_groups.hpp"
#include "games/tf2/sdk/entities/building.hpp"
#include "games/tf2/sdk/entities/capture_flag.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/entities/weapon.hpp"
#include "games/tf2/sdk/aim_hitboxes.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/render_view.hpp"
#include "games/tf2/sdk/interfaces/surface.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "games/tf2/sdk/thirdparty/stb/stb_image.h"
#ifdef Status
#undef Status
#endif

namespace
{

constexpr float cathook_corner_scale = 0.10f;
constexpr float cathook_healthbar_width = 7.0f;
constexpr float cathook_healthbar_fill_width = 5.0f;
constexpr float cathook_text_padding = 4.0f;
constexpr int cathook_surface_font_tall = 12;
constexpr int cathook_surface_font_weight = 400;
constexpr float cathook_head_emoji_size_base = 2500.0f;
constexpr float cathook_head_emoji_size_bias = 15.0f;
constexpr float cathook_head_emoji_tile_size = 64.0f;
constexpr int cathook_class_icon_tile_row = 5;
constexpr int cathook_head_emoji_tile_row = 4;
constexpr int cathook_head_emoji_first_tile_column = 4;
constexpr int cathook_head_emoji_style_count = 2;
constexpr std::size_t cathook_head_emoji_cache_size = 65;
constexpr float cathook_head_emoji_cache_interval = 0.05f;
constexpr float cathook_head_emoji_manual_cache_interval = 0.15f;
constexpr float esp_bounds_max_screen_scale = 1.35f;
constexpr float esp_bounds_offscreen_margin_scale = 0.50f;
constexpr std::size_t esp_bounds_min_projected_points = 2;
constexpr float esp_bounds_fallback_width_scale = 0.45f;
constexpr float esp_bounds_fallback_min_width = 6.0f;
constexpr float esp_bounds_fallback_min_height = 8.0f;
constexpr float esp_smoothing_snap_distance = 180.0f;
constexpr float esp_smoothing_snap_scale = 1.75f;
constexpr unsigned int esp_smoothing_stale_frames = 2;
constexpr unsigned int esp_bounds_cache_stale_frames = 2;
constexpr uintptr_t player_resource_score_offset = 0xC80;
constexpr uintptr_t player_resource_deaths_offset = 0xE18;
constexpr uintptr_t tf_player_resource_damage_offset = 0x2360;

struct head_emoji_texture_state
{
  std::unique_ptr<ImTextureData> texture{};
  std::filesystem::path loaded_path{};
  ImGuiContext* context = nullptr;
};

struct head_emoji_atlas_state
{
  std::vector<uint8_t> pixels{};
  std::filesystem::path loaded_path{};
  int width = 0;
  int height = 0;
};

struct mafia_title_range
{
  int min_level = 0;
  int max_level = 0;
  const char* title = nullptr;
};

constexpr std::array<mafia_title_range, 10> cathook_mafia_titles = {{
  {0, 9, "Crook"},
  {50, 50, "Crook"},
  {10, 10, "Bad Cop"},
  {0, 10, "Hoody"},
  {0, 5, "Gangster"},
  {1, 1, "Poor Man"},
  {10, 10, "Rich Man"},
  {10, 34, "Hitman"},
  {15, 99, "Boss"},
  {60, 100, "God Father"},
}};

head_emoji_texture_state g_head_emoji_texture{};
head_emoji_atlas_state g_head_emoji_atlas{};
std::array<Vec3, cathook_head_emoji_cache_size> g_head_emoji_positions{};
std::array<bool, cathook_head_emoji_cache_size> g_head_emoji_position_valid{};

struct esp_bounds
{
  float min_x = 0.0f;
  float min_y = 0.0f;
  float max_x = 0.0f;
  float max_y = 0.0f;

  [[nodiscard]] float width() const
  {
    return max_x - min_x;
  }

  [[nodiscard]] float height() const
  {
    return max_y - min_y;
  }

  [[nodiscard]] ImVec2 center() const
  {
    return ImVec2((min_x + max_x) * 0.5f, (min_y + max_y) * 0.5f);
  }
};

struct esp_entity_key
{
  int index = 0;
  int class_id = 0;
  int user_id_for_players = 0;
  int team = 0;
  life_state life_state_value = life_state::dead;

  [[nodiscard]] bool operator==(const esp_entity_key& other) const
  {
    return index == other.index
        && class_id == other.class_id
        && user_id_for_players == other.user_id_for_players
        && team == other.team
        && life_state_value == other.life_state_value;
  }
};

struct esp_entity_key_hash
{
  [[nodiscard]] std::size_t operator()(const esp_entity_key& key) const
  {
    auto hash = static_cast<std::size_t>(key.index);
    hash ^= (static_cast<std::size_t>(key.class_id) + 0x9e3779b9u + (hash << 6) + (hash >> 2));
    hash ^= (static_cast<std::size_t>(key.user_id_for_players) + 0x9e3779b9u + (hash << 6) + (hash >> 2));
    hash ^= (static_cast<std::size_t>(key.team) + 0x9e3779b9u + (hash << 6) + (hash >> 2));
    hash ^= (static_cast<std::size_t>(key.life_state_value) + 0x9e3779b9u + (hash << 6) + (hash >> 2));
    return hash;
  }
};

struct projected_box
{
  std::array<Vec3, 8> screen_points{};
  esp_bounds bounds{};
};

enum class esp_smoothing_point
{
  origin,
  head
};

struct esp_smoothing_state
{
  esp_bounds bounds{};
  esp_bounds projected_bounds{};
  ImVec2 origin_screen{};
  ImVec2 head_screen{};
  std::array<ImVec2, 8> projected_points{};
  bool bounds_valid = false;
  bool projected_bounds_valid = false;
  bool origin_screen_valid = false;
  bool head_screen_valid = false;
  bool projected_points_valid = false;
  unsigned int last_seen_frame = 0;
};

struct esp_bounds_cache_entry
{
  esp_bounds bounds{};
  Entity* entity = nullptr;
  unsigned int last_seen_frame = 0;
};

std::array<esp_entity_key, cathook_head_emoji_cache_size> g_head_emoji_keys{};
float g_next_head_emoji_cache_time = 0.0f;
float g_next_head_emoji_manual_cache_time = 0.0f;
std::unordered_map<esp_entity_key, esp_smoothing_state, esp_entity_key_hash> g_esp_smoothing_states{};
std::unordered_map<esp_entity_key, esp_bounds_cache_entry, esp_entity_key_hash> g_esp_bounds_cache{};
unsigned int g_esp_smoothing_frame = 0;
std::string g_esp_level_name{};
bool g_esp_was_in_game = false;

[[nodiscard]] bool esp_lerp_enabled()
{
  return config.visuals.esp_lerp;
}

[[nodiscard]] float esp_lerp_speed()
{
  return 12.0f;
}

[[nodiscard]] bool get_entity_screen_bounds(Entity* entity, esp_bounds* bounds,
  const Vec3* origin_override = nullptr);
[[nodiscard]] bool get_entity_projected_box(Entity* entity, projected_box* box,
  const Vec3* origin_override = nullptr);
[[nodiscard]] Vec3 get_esp_draw_origin(Entity* entity);

template <typename value_type>
[[nodiscard]] value_type read_player_resource_value(Entity* player_resource, uintptr_t array_offset, int player_index);

[[nodiscard]] bool is_finite_bounds(const esp_bounds& bounds)
{
  return std::isfinite(bounds.min_x)
      && std::isfinite(bounds.min_y)
      && std::isfinite(bounds.max_x)
      && std::isfinite(bounds.max_y)
      && bounds.max_x > bounds.min_x
      && bounds.max_y > bounds.min_y;
}

[[nodiscard]] bool is_reasonable_screen_bounds(const esp_bounds& bounds)
{
  if (!is_finite_bounds(bounds)) {
    return false;
  }

  auto screen_width = overlay_projection::state.screen_width;
  auto screen_height = overlay_projection::state.screen_height;
  if (screen_width <= 0.0f || screen_height <= 0.0f) {
    const auto& io = ImGui::GetIO();
    screen_width = io.DisplaySize.x;
    screen_height = io.DisplaySize.y;
  }
  if ((screen_width <= 0.0f || screen_height <= 0.0f) && engine != nullptr) {
    const auto engine_size = engine->get_screen_size();
    screen_width = static_cast<float>(engine_size.x);
    screen_height = static_cast<float>(engine_size.y);
  }

  if (screen_width <= 0.0f || screen_height <= 0.0f) {
    return true;
  }

  if (bounds.width() > screen_width * esp_bounds_max_screen_scale
      || bounds.height() > screen_height * esp_bounds_max_screen_scale) {
    return false;
  }

  const auto x_margin = screen_width * esp_bounds_offscreen_margin_scale;
  const auto y_margin = screen_height * esp_bounds_offscreen_margin_scale;
  return bounds.max_x >= -x_margin
      && bounds.min_x <= screen_width + x_margin
      && bounds.max_y >= -y_margin
      && bounds.min_y <= screen_height + y_margin;
}

[[nodiscard]] float lerp_float(float current, float target, float amount)
{
  return current + ((target - current) * amount);
}

[[nodiscard]] ImVec2 lerp_vec2(const ImVec2& current, const ImVec2& target, float amount)
{
  return ImVec2(lerp_float(current.x, target.x, amount), lerp_float(current.y, target.y, amount));
}

[[nodiscard]] esp_bounds lerp_bounds(const esp_bounds& current, const esp_bounds& target, float amount)
{
  return esp_bounds{
    .min_x = lerp_float(current.min_x, target.min_x, amount),
    .min_y = lerp_float(current.min_y, target.min_y, amount),
    .max_x = lerp_float(current.max_x, target.max_x, amount),
    .max_y = lerp_float(current.max_y, target.max_y, amount)
  };
}

[[nodiscard]] float esp_lerp_amount()
{
  if (!esp_lerp_enabled()) {
    return 1.0f;
  }

  auto frame_time = 1.0f / 60.0f;
  if (global_vars != nullptr && global_vars->frametime > 0.0f) {
    frame_time = global_vars->frametime;
  } else if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().DeltaTime > 0.0f) {
    frame_time = ImGui::GetIO().DeltaTime;
  }

  const auto speed = std::max(esp_lerp_speed(), 1.0f);
  return std::clamp(1.0f - std::exp(-speed * frame_time), 0.0f, 1.0f);
}

[[nodiscard]] esp_entity_key get_esp_entity_key(Entity* entity)
{
  if (entity == nullptr) {
    return {};
  }

  auto key = esp_entity_key{
    .index = std::max(entity->get_index(), 0),
    .class_id = static_cast<int>(entity->get_class_id()),
    .user_id_for_players = 0,
    .team = static_cast<int>(entity->get_team()),
    .life_state_value = life_state::dead
  };

  if (key.class_id == static_cast<int>(class_id::PLAYER)) {
    auto* player = reinterpret_cast<Player*>(entity);
    key.life_state_value = player->get_lifestate();

    auto info = player_info{};
    if (engine != nullptr && engine->get_player_info(key.index, &info)) {
      key.user_id_for_players = info.user_id;
    }
  }

  return key;
}

[[nodiscard]] bool is_valid_esp_entity_key(const esp_entity_key& key)
{
  return key.index > 0 && key.class_id != 0;
}

[[nodiscard]] float distance_2d(const ImVec2& first, const ImVec2& second)
{
  const auto delta_x = first.x - second.x;
  const auto delta_y = first.y - second.y;
  return std::sqrt((delta_x * delta_x) + (delta_y * delta_y));
}

[[nodiscard]] bool should_snap_bounds(const esp_bounds& current, const esp_bounds& target)
{
  if (!is_reasonable_screen_bounds(current) || !is_reasonable_screen_bounds(target)) {
    return true;
  }

  const auto current_center = current.center();
  const auto target_center = target.center();
  if (distance_2d(current_center, target_center) > esp_smoothing_snap_distance) {
    return true;
  }

  const auto current_width = std::max(current.width(), 1.0f);
  const auto current_height = std::max(current.height(), 1.0f);
  const auto target_width = std::max(target.width(), 1.0f);
  const auto target_height = std::max(target.height(), 1.0f);
  return target_width > current_width * esp_smoothing_snap_scale
      || current_width > target_width * esp_smoothing_snap_scale
      || target_height > current_height * esp_smoothing_snap_scale
      || current_height > target_height * esp_smoothing_snap_scale;
}

[[nodiscard]] bool should_snap_point(const ImVec2& current, const ImVec2& target)
{
  return !std::isfinite(current.x)
      || !std::isfinite(current.y)
      || !std::isfinite(target.x)
      || !std::isfinite(target.y)
      || distance_2d(current, target) > esp_smoothing_snap_distance;
}

void begin_esp_smoothing_frame()
{
  ++g_esp_smoothing_frame;
  if (g_esp_smoothing_frame == 0) {
    g_esp_smoothing_states.clear();
    g_esp_bounds_cache.clear();
    g_esp_smoothing_frame = 1;
  }

  if (!esp_lerp_enabled()) g_esp_smoothing_states.clear();
}

void cleanup_esp_smoothing_states()
{
  if (!esp_lerp_enabled()) {
    g_esp_smoothing_states.clear();
    return;
  }

  for (auto iterator = g_esp_smoothing_states.begin(); iterator != g_esp_smoothing_states.end();) {
    if (g_esp_smoothing_frame - iterator->second.last_seen_frame > esp_smoothing_stale_frames) {
      iterator = g_esp_smoothing_states.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

void cleanup_esp_bounds_cache()
{
  for (auto iterator = g_esp_bounds_cache.begin(); iterator != g_esp_bounds_cache.end();) {
    const auto& key = iterator->first;
    Entity* current_entity = nullptr;
    if (entity_list != nullptr && key.index > 0) {
      current_entity = entity_list->entity_from_index(static_cast<unsigned int>(key.index));
    }

    const bool entity_reused = current_entity != nullptr && current_entity != iterator->second.entity;
    const bool entity_removed = entity_list != nullptr && current_entity == nullptr;
    const bool dormant_entity = !entity_reused && !entity_removed && iterator->second.entity != nullptr &&
      iterator->second.entity->is_dormant();
    if (entity_reused || entity_removed || (!dormant_entity &&
        g_esp_smoothing_frame - iterator->second.last_seen_frame > esp_bounds_cache_stale_frames)) {
      iterator = g_esp_bounds_cache.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

void remember_esp_bounds(Entity* entity, const esp_bounds& bounds)
{
  const auto key = get_esp_entity_key(entity);
  if (!is_valid_esp_entity_key(key)) {
    return;
  }

  g_esp_bounds_cache[key] = esp_bounds_cache_entry{
    .bounds = bounds,
    .entity = entity,
    .last_seen_frame = g_esp_smoothing_frame
  };
}

[[nodiscard]] bool get_cached_esp_bounds(Entity* entity, esp_bounds* bounds)
{
  if (entity == nullptr || bounds == nullptr) {
    return false;
  }

  const auto key = get_esp_entity_key(entity);
  if (!is_valid_esp_entity_key(key)) {
    return false;
  }

  const auto entry = g_esp_bounds_cache.find(key);
  if (entry == g_esp_bounds_cache.end()) {
    return false;
  }

  if (entry->second.entity != entity || (!entity->is_dormant() &&
      g_esp_smoothing_frame - entry->second.last_seen_frame > esp_bounds_cache_stale_frames) ||
      !is_reasonable_screen_bounds(entry->second.bounds)) {
    return false;
  }

  entry->second.last_seen_frame = g_esp_smoothing_frame;
  *bounds = entry->second.bounds;
  return true;
}

void forget_esp_entity_runtime_state(Entity* entity)
{
  const auto key = get_esp_entity_key(entity);
  if (!is_valid_esp_entity_key(key)) {
    return;
  }

  g_esp_smoothing_states.erase(key);
  g_esp_bounds_cache.erase(key);
}

[[nodiscard]] esp_bounds smooth_esp_bounds(Entity* entity, const esp_bounds& target_bounds)
{
  const auto key = get_esp_entity_key(entity);
  if (!esp_lerp_enabled() || !is_valid_esp_entity_key(key)) {
    return target_bounds;
  }

  auto& state = g_esp_smoothing_states[key];
  state.last_seen_frame = g_esp_smoothing_frame;

  const auto amount = esp_lerp_amount();
  if (!state.bounds_valid || amount >= 1.0f || should_snap_bounds(state.bounds, target_bounds)) {
    state.bounds = target_bounds;
    state.bounds_valid = true;
    return target_bounds;
  }

  state.bounds = lerp_bounds(state.bounds, target_bounds, amount);
  return state.bounds;
}

[[nodiscard]] ImVec2 smooth_esp_point(Entity* entity, const ImVec2& target_point, esp_smoothing_point point)
{
  const auto key = get_esp_entity_key(entity);
  if (!esp_lerp_enabled() || !is_valid_esp_entity_key(key)) {
    return target_point;
  }

  auto& state = g_esp_smoothing_states[key];
  state.last_seen_frame = g_esp_smoothing_frame;

  auto* current_point = &state.origin_screen;
  auto* point_valid = &state.origin_screen_valid;
  if (point == esp_smoothing_point::head) {
    current_point = &state.head_screen;
    point_valid = &state.head_screen_valid;
  }

  const auto amount = esp_lerp_amount();
  if (!*point_valid || amount >= 1.0f || should_snap_point(*current_point, target_point)) {
    *current_point = target_point;
    *point_valid = true;
    return target_point;
  }

  *current_point = lerp_vec2(*current_point, target_point, amount);
  return *current_point;
}

void smooth_projected_box(Entity* entity, projected_box* box)
{
  if (box == nullptr) {
    return;
  }

  const auto key = get_esp_entity_key(entity);
  if (!esp_lerp_enabled() || !is_valid_esp_entity_key(key)) {
    return;
  }

  auto& state = g_esp_smoothing_states[key];
  state.last_seen_frame = g_esp_smoothing_frame;

  const auto amount = esp_lerp_amount();
  if (!state.projected_points_valid || !state.projected_bounds_valid || amount >= 1.0f || should_snap_bounds(state.projected_bounds, box->bounds)) {
    for (size_t index = 0; index < box->screen_points.size(); ++index) {
      state.projected_points[index] = ImVec2(box->screen_points[index].x, box->screen_points[index].y);
    }
    state.projected_bounds = box->bounds;
    state.projected_bounds_valid = true;
    state.projected_points_valid = true;
    return;
  }

  auto min_x = std::numeric_limits<float>::max();
  auto min_y = std::numeric_limits<float>::max();
  auto max_x = std::numeric_limits<float>::lowest();
  auto max_y = std::numeric_limits<float>::lowest();
  for (size_t index = 0; index < box->screen_points.size(); ++index) {
    const auto target = ImVec2(box->screen_points[index].x, box->screen_points[index].y);
    state.projected_points[index] = lerp_vec2(state.projected_points[index], target, amount);
    box->screen_points[index].x = state.projected_points[index].x;
    box->screen_points[index].y = state.projected_points[index].y;
    min_x = std::min(min_x, state.projected_points[index].x);
    min_y = std::min(min_y, state.projected_points[index].y);
    max_x = std::max(max_x, state.projected_points[index].x);
    max_y = std::max(max_y, state.projected_points[index].y);
  }

  box->bounds = esp_bounds{.min_x = min_x, .min_y = min_y, .max_x = max_x, .max_y = max_y};
  state.projected_bounds = box->bounds;
  state.projected_bounds_valid = true;
}

[[nodiscard]] bool get_stable_entity_screen_bounds(Entity* entity, esp_bounds* bounds)
{
  if (entity == nullptr || bounds == nullptr) {
    return false;
  }

  auto current_bounds = esp_bounds{};
  if (!entity->is_dormant()) {
    if (get_entity_screen_bounds(entity, &current_bounds) && is_reasonable_screen_bounds(current_bounds)) {
      remember_esp_bounds(entity, current_bounds);
      *bounds = current_bounds;
      return true;
    }

    forget_esp_entity_runtime_state(entity);
    return false;
  }

  return get_cached_esp_bounds(entity, bounds);
}

[[nodiscard]] std::vector<uint8_t> read_file_bytes(const std::filesystem::path& path)
{
  auto file = std::ifstream(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return {};
  }

  const auto file_size = file.tellg();
  if (file_size <= 0) {
    return {};
  }

  auto bytes = std::vector<uint8_t>(static_cast<size_t>(file_size));
  file.seekg(0, std::ios::beg);
  if (!file.read(reinterpret_cast<char*>(bytes.data()), file_size)) {
    return {};
  }

  return bytes;
}

[[nodiscard]] std::array<std::filesystem::path, 8> head_emoji_atlas_candidates()
{
  const auto source_directory = std::filesystem::path(__FILE__).parent_path();
  return {
    source_directory / "atlas.png",
    cathook::core::root_directory() / "assets" / "textures" / "atlas.png",
    cathook::core::root_directory() / "assets" / "atlas.png",
    cathook::core::root_directory() / "textures" / "atlas.png",
    std::filesystem::current_path() / "assets" / "textures" / "atlas.png",
    std::filesystem::current_path() / "src" / "features" / "visuals" / "esp" / "atlas.png",
    std::filesystem::current_path() / "features" / "visuals" / "esp" / "atlas.png",
    std::filesystem::current_path() / "atlas.png"
  };
}

[[nodiscard]] std::filesystem::path resolve_head_emoji_atlas_path()
{
  for (const auto& candidate : head_emoji_atlas_candidates()) {
    std::error_code error{};
    if (std::filesystem::exists(candidate, error)) {
      return candidate;
    }
  }

  return {};
}

void reset_head_emoji_texture()
{
  if (g_head_emoji_texture.texture != nullptr && g_head_emoji_texture.context == ImGui::GetCurrentContext()) {
    ImGui::UnregisterUserTexture(g_head_emoji_texture.texture.get());
  }
  g_head_emoji_texture.texture.reset();
  g_head_emoji_texture.loaded_path.clear();
  g_head_emoji_texture.context = nullptr;
}

void reset_head_emoji_atlas()
{
  reset_head_emoji_texture();
  g_head_emoji_atlas.pixels.clear();
  g_head_emoji_atlas.loaded_path.clear();
  g_head_emoji_atlas.width = 0;
  g_head_emoji_atlas.height = 0;
}

[[nodiscard]] bool ensure_head_emoji_atlas_loaded()
{
  const auto atlas_path = resolve_head_emoji_atlas_path();
  if (atlas_path.empty()) {
    reset_head_emoji_atlas();
    return false;
  }

  if (!g_head_emoji_atlas.pixels.empty() && g_head_emoji_atlas.loaded_path == atlas_path &&
      g_head_emoji_atlas.width > 0 && g_head_emoji_atlas.height > 0) {
    return true;
  }

  reset_head_emoji_atlas();

  const auto file_bytes = read_file_bytes(atlas_path);
  if (file_bytes.empty()) {
    return false;
  }

  auto image_width = 0;
  auto image_height = 0;
  auto channels = 0;
  auto* pixels = stbi_load_from_memory(
    file_bytes.data(),
    static_cast<int>(file_bytes.size()),
    &image_width,
    &image_height,
    &channels,
    STBI_rgb_alpha);
  if (pixels == nullptr || image_width <= 0 || image_height <= 0) {
    if (pixels != nullptr) {
      stbi_image_free(pixels);
    }
    return false;
  }

  const auto pixel_size = static_cast<size_t>(image_width) * static_cast<size_t>(image_height) * 4u;
  g_head_emoji_atlas.pixels.assign(pixels, pixels + pixel_size);
  stbi_image_free(pixels);
  g_head_emoji_atlas.loaded_path = atlas_path;
  g_head_emoji_atlas.width = image_width;
  g_head_emoji_atlas.height = image_height;
  return true;
}

[[nodiscard]] ImTextureData* get_head_emoji_texture()
{
  auto* current_context = ImGui::GetCurrentContext();
  if (current_context == nullptr) {
    return nullptr;
  }

  if (g_head_emoji_texture.context != nullptr && g_head_emoji_texture.context != current_context) {
    reset_head_emoji_texture();
  }

  if (!ensure_head_emoji_atlas_loaded()) {
    return nullptr;
  }
  const auto& atlas_path = g_head_emoji_atlas.loaded_path;

  if (g_head_emoji_texture.texture != nullptr && g_head_emoji_texture.loaded_path == atlas_path) {
    if (g_head_emoji_texture.texture->Status == ImTextureStatus_Destroyed) {
      reset_head_emoji_texture();
      return nullptr;
    }

    if (g_head_emoji_texture.texture->Status != ImTextureStatus_OK) {
      return nullptr;
    }

    if (g_head_emoji_texture.texture->Pixels != nullptr) {
      g_head_emoji_texture.texture->DestroyPixels();
    }

    return g_head_emoji_texture.texture.get();
  }

  reset_head_emoji_texture();

  if (g_head_emoji_atlas.pixels.empty() || g_head_emoji_atlas.width <= 0 || g_head_emoji_atlas.height <= 0) {
    return nullptr;
  }

  auto texture = std::make_unique<ImTextureData>();
  texture->Create(ImTextureFormat_RGBA32, g_head_emoji_atlas.width, g_head_emoji_atlas.height);
  std::memcpy(texture->Pixels, g_head_emoji_atlas.pixels.data(), texture->GetSizeInBytes());
  texture->RefCount = 1;
  texture->SetStatus(ImTextureStatus_WantCreate);

  ImGui::RegisterUserTexture(texture.get());
  g_head_emoji_texture.loaded_path = atlas_path;
  g_head_emoji_texture.context = current_context;
  g_head_emoji_texture.texture = std::move(texture);
  return nullptr;
}

[[nodiscard]] ImU32 to_imgui_color(const RGBA& color)
{
  return IM_COL32(color.r, color.g, color.b, color.a);
}

[[nodiscard]] ImU32 with_alpha(const RGBA& color, float alpha_scale)
{
  const auto alpha = std::clamp(static_cast<int>(std::round(static_cast<float>(color.a) * alpha_scale)), 0, 255);
  return IM_COL32(color.r, color.g, color.b, alpha);
}

[[nodiscard]] ImU32 black_with_alpha(float alpha_scale)
{
  const auto alpha = std::clamp(static_cast<int>(std::round(255.0f * alpha_scale)), 0, 255);
  return IM_COL32(0, 0, 0, alpha);
}

[[nodiscard]] float distance_between(const Vec3& first, const Vec3& second)
{
  const auto delta = first - second;
  return std::sqrt((delta.x * delta.x) + (delta.y * delta.y) + (delta.z * delta.z));
}

[[nodiscard]] RGBA get_health_color(int health, int max_health)
{
  if (max_health <= 0) {
    return RGBA{255, 0, 0, 255};
  }

  if (health > max_health) {
    return RGBA{0, 255, 255, 255};
  }

  const auto health_ratio = static_cast<float>(health) / static_cast<float>(max_health);
  if (health_ratio >= 0.90f) {
    return RGBA{0, 255, 0, 255};
  }
  if (health_ratio > 0.60f) {
    return RGBA{90, 255, 0, 255};
  }
  if (health_ratio > 0.35f) {
    return RGBA{255, 100, 0, 255};
  }

  return RGBA{255, 0, 0, 255};
}

[[nodiscard]] std::string wide_to_utf8(const wchar_t* text)
{
  if (text == nullptr || text[0] == L'\0') {
    return {};
  }

  char buffer[128]{};
  const auto converted = std::wcstombs(buffer, text, sizeof(buffer) - 1);
  if (converted == static_cast<size_t>(-1)) {
    return {};
  }

  buffer[converted] = '\0';
  return std::string(buffer);
}

[[nodiscard]] bool esp_text_contains(std::string_view text, std::string_view needle)
{
  if (needle.empty()) {
    return true;
  }

  if (text.find(needle) != std::string_view::npos) {
    return true;
  }

  return std::search(text.begin(), text.end(), needle.begin(), needle.end(), [](char left, char right) {
    return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
  }) != text.end();
}

[[nodiscard]] std::string_view safe_string_view(const char* value)
{
  return value != nullptr ? std::string_view{value} : std::string_view{};
}

[[nodiscard]] RGBA_float esp_color_for_entity(Entity* entity, const visual_group& group)
{
  return visual_groups::resolve_color(entity, group, group.esp.override_color, group.esp.color);
}

[[nodiscard]] RGBA player_esp_color(Player* player, Player* localplayer)
{
  (void)localplayer;
  const visual_groups::visual_group_match group = player != nullptr ? visual_groups::group_for_entity(player->to_entity(), false) : visual_groups::visual_group_match{};
  return group ? esp_color_for_entity(player->to_entity(), *group).to_RGBA() : RGBA{255, 255, 255, 255};
}

[[nodiscard]] std::string player_name(Player* player)
{
  if (player == nullptr) {
    return {};
  }

  wchar_t wide_name[32]{};
  player->get_player_name(wide_name);
  return wide_to_utf8(wide_name);
}

[[nodiscard]] const char* player_class_name(tf_class class_type)
{
  switch (class_type) {
    case tf_class::SCOUT:
      return "Scout";
    case tf_class::SNIPER:
      return "Sniper";
    case tf_class::SOLDIER:
      return "Soldier";
    case tf_class::DEMOMAN:
      return "Demoman";
    case tf_class::MEDIC:
      return "Medic";
    case tf_class::HEAVYWEAPONS:
      return "Heavy";
    case tf_class::PYRO:
      return "Pyro";
    case tf_class::SPY:
      return "Spy";
    case tf_class::ENGINEER:
      return "Engineer";
    default:
      return "Unknown";
  }
}

[[nodiscard]] const char* weapon_id_name(int weapon_id)
{
  switch (weapon_id) {
    case TF_WEAPON_BAT:
    case TF_WEAPON_BAT_WOOD:
    case TF_WEAPON_BAT_FISH:
    case TF_WEAPON_BAT_GIFTWRAP:
      return "Bat";
    case TF_WEAPON_BOTTLE:
      return "Bottle";
    case TF_WEAPON_FIREAXE:
      return "Fire Axe";
    case TF_WEAPON_CLUB:
      return "Club";
    case TF_WEAPON_KNIFE:
      return "Knife";
    case TF_WEAPON_FISTS:
      return "Fists";
    case TF_WEAPON_SHOVEL:
      return "Shovel";
    case TF_WEAPON_WRENCH:
      return "Wrench";
    case TF_WEAPON_BONESAW:
      return "Bonesaw";
    case TF_WEAPON_SHOTGUN_PRIMARY:
    case TF_WEAPON_SHOTGUN_SOLDIER:
    case TF_WEAPON_SHOTGUN_HWG:
    case TF_WEAPON_SHOTGUN_PYRO:
      return "Shotgun";
    case TF_WEAPON_SCATTERGUN:
      return "Scattergun";
    case TF_WEAPON_SNIPERRIFLE:
    case TF_WEAPON_SNIPERRIFLE_DECAP:
    case TF_WEAPON_SNIPERRIFLE_CLASSIC:
      return "Sniper Rifle";
    case TF_WEAPON_MINIGUN:
      return "Minigun";
    case TF_WEAPON_SMG:
    case TF_WEAPON_CHARGED_SMG:
      return "SMG";
    case TF_WEAPON_SYRINGEGUN_MEDIC:
      return "Syringe Gun";
    case TF_WEAPON_ROCKETLAUNCHER:
    case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
      return "Rocket Launcher";
    case TF_WEAPON_GRENADELAUNCHER:
    case TF_WEAPON_CANNON:
      return "Grenade Launcher";
    case TF_WEAPON_PIPEBOMBLAUNCHER:
      return "Sticky Launcher";
    case TF_WEAPON_FLAMETHROWER:
      return "Flamethrower";
    case TF_WEAPON_PISTOL:
    case TF_WEAPON_PISTOL_SCOUT:
      return "Pistol";
    case TF_WEAPON_REVOLVER:
      return "Revolver";
    case TF_WEAPON_MEDIGUN:
      return "Medi Gun";
    case TF_WEAPON_FLAREGUN:
    case TF_WEAPON_FLAREGUN_REVENGE:
      return "Flare Gun";
    case TF_WEAPON_LUNCHBOX:
      return "Lunchbox";
    case TF_WEAPON_JAR:
      return "Jarate";
    case TF_WEAPON_JAR_MILK:
      return "Mad Milk";
    case TF_WEAPON_COMPOUND_BOW:
      return "Bow";
    case TF_WEAPON_BUFF_ITEM:
      return "Banner";
    case TF_WEAPON_SWORD:
      return "Sword";
    case TF_WEAPON_CROSSBOW:
      return "Crossbow";
    case TF_WEAPON_RAYGUN:
    case TF_WEAPON_PARTICLE_CANNON:
    case TF_WEAPON_DRG_POMSON:
      return "Energy Weapon";
    case TF_WEAPON_MECHANICAL_ARM:
      return "Short Circuit";
    case TF_WEAPON_CLEAVER:
      return "Cleaver";
    case TF_WEAPON_THROWABLE:
    case TF_WEAPON_GRENADE_THROWABLE:
      return "Throwable";
    case TF_WEAPON_SPELLBOOK:
      return "Spellbook";
    case TF_WEAPON_GRAPPLINGHOOK:
      return "Grappling Hook";
    case TF_WEAPON_PASSTIME_GUN:
      return "PASS Time";
    default:
      return nullptr;
  }
}

[[nodiscard]] std::string weapon_text_for(Weapon* weapon)
{
  if (weapon == nullptr) {
    return {};
  }

  if (const char* name = weapon_id_name(weapon->get_weapon_id())) {
    return name;
  }

  return "Weapon #" + std::to_string(weapon->get_def_id());
}

[[nodiscard]] Weapon* player_medigun(Player* player)
{
  if (player == nullptr) {
    return nullptr;
  }

  for (int index = 0; index < Player::max_weapon_count; ++index) {
    Weapon* weapon = player->get_weapon_at(index);
    if (weapon != nullptr && weapon->is_medigun()) {
      return weapon;
    }
  }

  return nullptr;
}

[[nodiscard]] std::string player_steamid(Player* player)
{
  if (player == nullptr || engine == nullptr) {
    return {};
  }

  player_info info{};
  if (!engine->get_player_info(player->get_index(), &info) || info.friends_id == 0) {
    return {};
  }

  const SteamID steam_id(
    static_cast<int>(static_cast<std::uint32_t>(info.friends_id)),
    1,
    k_EUniversePublic,
    k_EAccountTypeIndividual);
  return std::to_string(static_cast<unsigned long long>(steam_id.m_steamid.m_unAll64Bits));
}

[[nodiscard]] std::string player_tag_text(Player* player)
{
  if (player == nullptr) {
    return {};
  }

  const auto account_id = cathook::core::players::account_id_for_player_index(player->get_index());
  auto roles = cathook::core::players::roles_for(account_id);
  if (roles.empty() && cathook::core::players::has_role(account_id, cathook::core::players::friend_role)) {
    roles.push_back(cathook::core::players::friend_role);
  }
  if (roles.empty()) {
    return {};
  }

  std::string result = "[";
  for (std::size_t index = 0; index < roles.size(); ++index) {
    if (index != 0) result += ", ";
    result += cathook::core::players::role_name(roles[index]);
  }
  result += "]";
  return result;
}

[[nodiscard]] RGBA player_tag_color(Player* player)
{
  using player_state = cathook::core::players::player_state;
  if (player == nullptr) {
    return RGBA{200, 200, 200, 255};
  }

  const auto account_id = cathook::core::players::account_id_for_player_index(player->get_index());
  switch (cathook::core::players::state_for(account_id)) {
    case player_state::friend_state:
      return RGBA{100, 255, 100, 255};
    case player_state::party:
      return RGBA{100, 100, 255, 255};
    case player_state::cheater:
      return RGBA{255, 100, 100, 255};
    case player_state::ipc:
    case player_state::textmode:
    case player_state::identified:
      return RGBA{100, 220, 255, 255};
    case player_state::ignored:
    case player_state::default_state:
    default:
      return RGBA{200, 200, 200, 255};
  }
}

[[nodiscard]] std::string base_name_from_path(std::string_view path)
{
  if (path.empty()) {
    return {};
  }

  const auto slash = path.find_last_of("/\\");
  const auto start = slash == std::string_view::npos ? 0 : slash + 1;
  const auto dot = path.find_last_of('.');
  const auto end = dot == std::string_view::npos || dot < start ? path.size() : dot;
  return std::string(path.substr(start, end - start));
}

[[nodiscard]] std::string projectile_label(Entity* entity)
{
  if (entity == nullptr) {
    return {};
  }

  const std::string_view network_name = safe_string_view(entity->get_network_name());
  const std::string_view model_name = safe_string_view(entity->get_model_name());

  if (entity->get_class_id() == class_id::ROCKET || esp_text_contains(network_name, "rocket")) return "Rocket";
  if (entity->get_class_id() == class_id::PILL_OR_STICKY) return esp_text_contains(model_name, "sticky") ? "Sticky" : "Pipe";
  if (entity->get_class_id() == class_id::ARROW || esp_text_contains(network_name, "arrow")) return "Arrow";
  if (entity->get_class_id() == class_id::CROSSBOW_BOLT || esp_text_contains(network_name, "healingbolt")) return "Heal Bolt";
  if (entity->get_class_id() == class_id::FLARE || esp_text_contains(network_name, "flare")) return "Flare";
  if (esp_text_contains(network_name, "balloffire")) return "Fire";
  if (esp_text_contains(network_name, "cleaver")) return "Cleaver";
  if (esp_text_contains(network_name, "jarmilk") || esp_text_contains(network_name, "bread")) return "Milk";
  if (esp_text_contains(network_name, "jargas")) return "Gas";
  if (esp_text_contains(network_name, "jar")) return "Jarate";
  if (esp_text_contains(network_name, "stunball")) return "Baseball";
  if (esp_text_contains(network_name, "energy")) return "Energy";
  if (esp_text_contains(network_name, "mechanicalarm")) return "Short Circuit";
  if (esp_text_contains(network_name, "meteorshower")) return "Meteor";
  if (esp_text_contains(network_name, "lightning")) return "Lightning";
  if (esp_text_contains(network_name, "fireball")) return "Fireball";
  if (esp_text_contains(network_name, "merasmusgrenade")) return "Bomb";
  if (esp_text_contains(network_name, "spellbats")) return "Bats";
  if (esp_text_contains(network_name, "pumpkin")) return "Pumpkin";
  if (esp_text_contains(network_name, "spawnboss")) return "Monoculus";
  if (esp_text_contains(network_name, "spawnhorde") || esp_text_contains(network_name, "spawnzombie")) return "Skeleton";
  if (esp_text_contains(network_name, "pipebomb") || esp_text_contains(model_name, "pipebomb")) return esp_text_contains(model_name, "sticky") ? "Sticky" : "Pipe";

  const auto model_base = base_name_from_path(model_name);
  return model_base.empty() ? "Projectile" : model_base;
}

[[nodiscard]] std::string entity_label(Entity* entity)
{
  if (entity == nullptr) {
    return {};
  }

  if (entity->get_class_id() == class_id::PLAYER) {
    return "Player";
  }

  const auto generic = std::string(visual_groups::label_for_entity(entity));
  const std::string_view network_name = safe_string_view(entity->get_network_name());
  if (generic != "ENTITY" && generic != "PROJECTILE" && generic != network_name) {
    return generic;
  }
  if (generic == "PROJECTILE") {
    return projectile_label(entity);
  }

  if (entity->is_base_combat_weapon()) {
    return weapon_text_for(reinterpret_cast<Weapon*>(entity));
  }
  return generic;
}

[[nodiscard]] Player* owner_player_for_esp(Entity* entity)
{
  if (entity == nullptr || entity_list == nullptr) {
    return nullptr;
  }

  if (entity->get_class_id() == class_id::PLAYER) {
    return reinterpret_cast<Player*>(entity);
  }

  auto* owner = entity->get_owner_entity();
  if (owner != nullptr && owner->get_class_id() == class_id::PLAYER) {
    return reinterpret_cast<Player*>(owner);
  }

  if (entity->is_building()) {
    auto* builder = reinterpret_cast<Building*>(entity)->get_owner_entity();
    if (builder != nullptr && builder->get_class_id() == class_id::PLAYER) {
      return reinterpret_cast<Player*>(builder);
    }
  }

  return nullptr;
}

[[nodiscard]] bool player_is_priority(Player* player)
{
  return player != nullptr && cathook::core::players::is_prioritized(cathook::core::players::account_id_for_player_index(player->get_index()));
}

[[nodiscard]] bool player_is_invisible_esp(Player* player)
{
  return player != nullptr && player->is_cloaked();
}

[[nodiscard]] float entity_distance_hu(Entity* entity, Player* localplayer)
{
  if (entity == nullptr || localplayer == nullptr) {
    return 0.0f;
  }

  return distance_between(entity->get_collision_origin(), localplayer->get_collision_origin());
}

[[nodiscard]] int player_ping(Entity* player_resource, int player_index)
{
  static const int ping_offset = tf2_netvars::find_offset("DT_TFPlayerResource", { "baseclass", "m_iPing" });
  return ping_offset > 0 ? read_player_resource_value<int>(player_resource, static_cast<uintptr_t>(ping_offset), player_index) : 0;
}

[[nodiscard]] std::string player_kdr_text(Entity* player_resource, int player_index)
{
  if (player_resource == nullptr || player_index <= 0) {
    return {};
  }

  const int score = read_player_resource_value<int>(player_resource, player_resource_score_offset, player_index);
  const int deaths = read_player_resource_value<int>(player_resource, player_resource_deaths_offset, player_index);
  char buffer[32]{};
  if (deaths <= 0) {
    std::snprintf(buffer, sizeof(buffer), "KDR %d", score);
  } else {
    std::snprintf(buffer, sizeof(buffer), "KDR %.1f", static_cast<float>(score) / static_cast<float>(deaths));
  }
  return buffer;
}

[[nodiscard]] std::string ammo_text_for(Player* player)
{
  if (player == nullptr) {
    return {};
  }

  auto* weapon = player->get_weapon();
  if (weapon == nullptr) {
    return {};
  }

  const int clip = weapon->get_clip1();
  if (clip < 0) {
    return {};
  }

  return "Ammo " + std::to_string(clip);
}

[[nodiscard]] std::string ammo_text_for(Entity* entity)
{
  if (entity == nullptr) {
    return {};
  }

  if (entity->is_building() && entity->get_class_id() == class_id::SENTRY) {
    auto* sentry = reinterpret_cast<Building*>(entity);
    const int shells = std::max(0, sentry->get_sentry_ammo_shells());
    const int rockets = std::max(0, sentry->get_sentry_ammo_rockets());
    const int max_rockets = sentry->get_sentry_max_ammo_rockets();
    return max_rockets > 0
      ? "Ammo " + std::to_string(shells) + ", " + std::to_string(rockets)
      : "Ammo " + std::to_string(shells);
  }

  if (!entity->is_base_combat_weapon()) {
    return {};
  }

  auto* weapon = reinterpret_cast<Weapon*>(entity);
  const int clip = weapon->get_clip1();
  if (clip < 0) {
    return {};
  }

  return "Ammo " + std::to_string(clip);
}

[[nodiscard]] std::string weapon_icon_text_for(Weapon* weapon)
{
  if (weapon == nullptr) {
    return {};
  }

  if (weapon->is_medigun()) return "+";
  if (weapon->is_flamethrower()) return "F";
  if (weapon->is_melee()) return "M";

  switch (weapon->get_weapon_id()) {
    case TF_WEAPON_ROCKETLAUNCHER:
    case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
    case TF_WEAPON_GRENADELAUNCHER:
    case TF_WEAPON_PIPEBOMBLAUNCHER:
    case TF_WEAPON_COMPOUND_BOW:
    case TF_WEAPON_CROSSBOW:
      return "P";
    default:
      return "G";
  }
}

[[nodiscard]] std::string flag_status_text(Entity* entity)
{
  if (entity == nullptr || entity->get_class_id() != class_id::CAPTURE_FLAG) {
    return {};
  }

  switch (reinterpret_cast<CaptureFlag*>(entity)->get_status()) {
    case HOME:
      return "Intel home";
    case STOLEN:
      return "Intel stolen";
    case DROPPED:
      return "Intel dropped";
    default:
      return "Intel";
  }
}

[[nodiscard]] bool active_visual_group_index(std::size_t index)
{
  return index < visual_group_config::max_groups && (config.visual_groups.active_group_mask & (1u << index)) != 0;
}

[[nodiscard]] const visual_group* first_pickup_timer_group()
{
  if (!visual_groups::groups_need_pickup_timers()) {
    return nullptr;
  }

  for (std::size_t index = config.visual_groups.groups.size(); index > 0; --index) {
    const std::size_t group_index = index - 1;
    if (!active_visual_group_index(group_index)) {
      continue;
    }

    const auto& group = config.visual_groups.groups[group_index];
    if (group.pickup_timer) {
      return &group;
    }
  }

  return nullptr;
}

[[nodiscard]] bool any_backtrack_group_enabled()
{
  return visual_groups::groups_need_backtrack_visualizer();
}

[[nodiscard]] ImVec2 overlay_screen_center()
{
  return ImVec2(overlay_projection::state.screen_width * 0.5f, overlay_projection::state.screen_height * 0.5f);
}

[[nodiscard]] bool screen_point_inside_view(const ImVec2& point)
{
  return point.x >= 0.0f && point.y >= 0.0f &&
    point.x <= overlay_projection::state.screen_width &&
    point.y <= overlay_projection::state.screen_height;
}

void draw_offscreen_arrow(ImDrawList* draw_list, Entity* entity, Player* localplayer, const visual_group& group)
{
  if (draw_list == nullptr || entity == nullptr || localplayer == nullptr || !group.offscreen_arrows ||
      overlay_projection::state.screen_width <= 0.0f || overlay_projection::state.screen_height <= 0.0f) {
    return;
  }

  const float distance = entity_distance_hu(entity, localplayer);
  if (group.offscreen_arrows_max_distance > 0.0f && distance > group.offscreen_arrows_max_distance) {
    return;
  }

  const float alpha_scale = visual_groups::alpha_for_entity(entity, group.esp.start, group.esp.end, group.esp.smooth_alpha);
  if (alpha_scale <= 0.0f) {
    return;
  }

  Vec3 screen{};
  if (!overlay_projection::world_to_screen(get_esp_draw_origin(entity), &screen)) {
    return;
  }

  const ImVec2 projected(screen.x, screen.y);
  if (screen_point_inside_view(projected)) {
    return;
  }

  const ImVec2 center = overlay_screen_center();
  ImVec2 direction(projected.x - center.x, projected.y - center.y);
  const float length = std::sqrt((direction.x * direction.x) + (direction.y * direction.y));
  if (length <= 0.001f) {
    return;
  }

  direction.x /= length;
  direction.y /= length;
  const float max_margin = std::max(8.0f, (std::min(overlay_projection::state.screen_width, overlay_projection::state.screen_height) * 0.5f) - 24.0f);
  const float margin = std::clamp(static_cast<float>(group.offscreen_arrows_offset), 8.0f, max_margin);
  const float edge_x = direction.x > 0.0f
    ? (overlay_projection::state.screen_width - margin - center.x) / direction.x
    : (margin - center.x) / direction.x;
  const float edge_y = direction.y > 0.0f
    ? (overlay_projection::state.screen_height - margin - center.y) / direction.y
    : (margin - center.y) / direction.y;
  const float edge_scale = std::max(0.0f, std::min(edge_x > 0.0f ? edge_x : std::numeric_limits<float>::max(), edge_y > 0.0f ? edge_y : std::numeric_limits<float>::max()));
  if (!std::isfinite(edge_scale)) {
    return;
  }

  const ImVec2 tip(center.x + (direction.x * edge_scale), center.y + (direction.y * edge_scale));
  const ImVec2 perpendicular(-direction.y, direction.x);
  constexpr float arrow_length = 13.0f;
  constexpr float arrow_width = 8.0f;
  const ImVec2 base(tip.x - (direction.x * arrow_length), tip.y - (direction.y * arrow_length));
  const ImVec2 left(base.x + (perpendicular.x * arrow_width), base.y + (perpendicular.y * arrow_width));
  const ImVec2 right(base.x - (perpendicular.x * arrow_width), base.y - (perpendicular.y * arrow_width));

  auto arrow_color = esp_color_for_entity(entity, group);
  arrow_color.a = std::clamp(arrow_color.a * alpha_scale, 0.0f, 1.0f);
  const auto color = to_imgui_color(arrow_color.to_RGBA());
  const auto shadow_alpha = static_cast<int>(std::round(190.0f * alpha_scale));
  draw_list->AddTriangleFilled(
    ImVec2(tip.x + 1.0f, tip.y + 1.0f),
    ImVec2(left.x + 1.0f, left.y + 1.0f),
    ImVec2(right.x + 1.0f, right.y + 1.0f),
    IM_COL32(0, 0, 0, shadow_alpha));
  draw_list->AddTriangleFilled(tip, left, right, color);
}

void draw_world_line(ImDrawList* draw_list, const Vec3& start, const Vec3& end, ImU32 color,
  float thickness = 1.0f, float alpha_scale = 1.0f)
{
  if (draw_list == nullptr) {
    return;
  }

  Vec3 start_screen{};
  Vec3 end_screen{};
  if (!overlay_projection::world_to_screen(start, &start_screen) ||
      !overlay_projection::world_to_screen(end, &end_screen)) {
    return;
  }

  const auto shadow_alpha = static_cast<int>(std::round(190.0f * std::clamp(alpha_scale, 0.0f, 1.0f)));
  draw_list->AddLine(
    ImVec2(start_screen.x + 1.0f, start_screen.y + 1.0f),
    ImVec2(end_screen.x + 1.0f, end_screen.y + 1.0f),
    IM_COL32(0, 0, 0, shadow_alpha),
    thickness + 2.0f);
  draw_list->AddLine(ImVec2(start_screen.x, start_screen.y), ImVec2(end_screen.x, end_screen.y), color, thickness);
}

void draw_player_sightline(ImDrawList* draw_list, Player* player, const visual_group& group)
{
  if (draw_list == nullptr || player == nullptr || (group.sightlines & visual_group::sightline_enabled) == 0) {
    return;
  }

  const float alpha_scale = visual_groups::alpha_for_entity(player->to_entity(), group.esp.start, group.esp.end, group.esp.smooth_alpha);
  if (alpha_scale <= 0.0f) {
    return;
  }

  Vec3 forward{};
  angle_vectors(player->get_eye_angles(), &forward, nullptr, nullptr);
  const Vec3 start = player->get_shoot_pos();
  const Vec3 end = start + (forward * 8192.0f);
  auto color = esp_color_for_entity(player->to_entity(), group);
  color.a = std::min(color.a * alpha_scale, 0.78f);
  draw_world_line(draw_list, start, end, to_imgui_color(color.to_RGBA()), 1.5f, alpha_scale);
}

[[nodiscard]] Vec3 entity_velocity(Entity* entity)
{
  if (entity == nullptr) {
    return {};
  }

  return *reinterpret_cast<Vec3*>(reinterpret_cast<uintptr_t>(entity) + 0x168);
}

void draw_entity_trajectory(ImDrawList* draw_list, Entity* entity, const visual_group& group)
{
  if (draw_list == nullptr || entity == nullptr || (group.trajectory & visual_group::trajectory_enabled) == 0) {
    return;
  }

  const float alpha_scale = visual_groups::alpha_for_entity(entity, group.esp.start, group.esp.end, group.esp.smooth_alpha);
  if (alpha_scale <= 0.0f) {
    return;
  }

  const Vec3 velocity = entity_velocity(entity);
  const float speed = std::sqrt((velocity.x * velocity.x) + (velocity.y * velocity.y) + (velocity.z * velocity.z));
  if (!std::isfinite(speed) || speed < 1.0f) {
    return;
  }

  const Vec3 start = get_esp_draw_origin(entity);
  auto color = esp_color_for_entity(entity, group);
  color.a = std::min(color.a * alpha_scale, 0.82f);
  const ImU32 line_color = to_imgui_color(color.to_RGBA());
  constexpr int segments = 12;
  constexpr float step_time = 0.075f;
  Vec3 previous = start;
  for (int segment = 1; segment <= segments; ++segment) {
    const float time = static_cast<float>(segment) * step_time;
    Vec3 current = start + (velocity * time);
    if ((group.trajectory & visual_group::trajectory_predict) != 0) {
      current.z -= 400.0f * time * time;
    }

    if ((group.trajectory & visual_group::trajectory_path) != 0 || (group.trajectory & visual_group::trajectory_trace) != 0) {
      draw_world_line(draw_list, previous, current, line_color, 1.5f, alpha_scale);
    }

    previous = current;
  }

  Vec3 end_screen{};
  if (overlay_projection::world_to_screen(previous, &end_screen)) {
    if ((group.trajectory & visual_group::trajectory_sphere) != 0) {
      draw_list->AddCircle(ImVec2(end_screen.x, end_screen.y), 5.0f,
        IM_COL32(0, 0, 0, static_cast<int>(std::round(210.0f * alpha_scale))), 18, 3.0f);
      draw_list->AddCircle(ImVec2(end_screen.x, end_screen.y), 4.0f, line_color, 18, 1.5f);
    }
    if ((group.trajectory & visual_group::trajectory_radius) != 0) {
      draw_list->AddCircle(ImVec2(end_screen.x, end_screen.y), 18.0f,
        IM_COL32(0, 0, 0, static_cast<int>(std::round(160.0f * alpha_scale))), 32, 3.0f);
      draw_list->AddCircle(ImVec2(end_screen.x, end_screen.y), 17.0f, line_color, 32, 1.0f);
    }
  }
}

[[nodiscard]] Vec3 get_esp_draw_origin(Entity* entity)
{
  if (entity == nullptr) {
    return {};
  }

  return entity->get_collision_origin();
}

[[nodiscard]] esp_bounds empty_screen_bounds()
{
  return esp_bounds{
    .min_x = std::numeric_limits<float>::max(),
    .min_y = std::numeric_limits<float>::max(),
    .max_x = std::numeric_limits<float>::lowest(),
    .max_y = std::numeric_limits<float>::lowest()
  };
}

[[nodiscard]] bool add_projected_screen_point(const Vec3& projected, esp_bounds* bounds)
{
  if (bounds == nullptr || !std::isfinite(projected.x) || !std::isfinite(projected.y)) {
    return false;
  }

  bounds->min_x = std::min(bounds->min_x, projected.x);
  bounds->min_y = std::min(bounds->min_y, projected.y);
  bounds->max_x = std::max(bounds->max_x, projected.x);
  bounds->max_y = std::max(bounds->max_y, projected.y);
  return true;
}

[[nodiscard]] bool project_world_points_to_screen_bounds(const std::array<Vec3, 8>& world_points, esp_bounds* bounds)
{
  if (bounds == nullptr) {
    return false;
  }

  esp_bounds current_bounds = empty_screen_bounds();
  std::size_t projected_count = 0;
  for (const Vec3& point : world_points) {
    Vec3 projected{};
    if (overlay_projection::world_to_screen(point, &projected) && add_projected_screen_point(projected, &current_bounds)) {
      ++projected_count;
    }
  }

  if (projected_count < esp_bounds_min_projected_points || !is_reasonable_screen_bounds(current_bounds)) {
    return false;
  }

  *bounds = current_bounds;
  return true;
}

[[nodiscard]] bool get_entity_centerline_screen_bounds(Entity* entity, esp_bounds* bounds,
  const Vec3* origin_override)
{
  if (entity == nullptr || bounds == nullptr) {
    return false;
  }

  const Vec3 origin = origin_override != nullptr ? *origin_override : get_esp_draw_origin(entity);
  const Vec3 mins = entity->get_collideable_mins();
  const Vec3 maxs = entity->get_collideable_maxs();
  const Vec3 bottom_world = origin + Vec3{0.0f, 0.0f, mins.z};
  const Vec3 top_world = origin + Vec3{0.0f, 0.0f, maxs.z};

  Vec3 bottom_screen{};
  Vec3 top_screen{};
  if (!overlay_projection::world_to_screen(bottom_world, &bottom_screen) ||
      !overlay_projection::world_to_screen(top_world, &top_screen) ||
      !std::isfinite(bottom_screen.x) ||
      !std::isfinite(bottom_screen.y) ||
      !std::isfinite(top_screen.x) ||
      !std::isfinite(top_screen.y)) {
    return false;
  }

  float min_y = std::min(bottom_screen.y, top_screen.y);
  float max_y = std::max(bottom_screen.y, top_screen.y);
  float height = max_y - min_y;
  if (height < esp_bounds_fallback_min_height) {
    const float center_y = (min_y + max_y) * 0.5f;
    height = esp_bounds_fallback_min_height;
    min_y = center_y - (height * 0.5f);
    max_y = center_y + (height * 0.5f);
  }

  const float width = std::max(esp_bounds_fallback_min_width, height * esp_bounds_fallback_width_scale);
  const float center_x = (bottom_screen.x + top_screen.x) * 0.5f;
  const esp_bounds current_bounds{
    .min_x = center_x - (width * 0.5f),
    .min_y = min_y,
    .max_x = center_x + (width * 0.5f),
    .max_y = max_y
  };

  if (!is_reasonable_screen_bounds(current_bounds)) {
    return false;
  }

  *bounds = current_bounds;
  return true;
}

[[nodiscard]] bool get_entity_amalgam_screen_bounds(Entity* entity, esp_bounds* bounds,
  const Vec3* origin_override)
{
  if (entity == nullptr || bounds == nullptr) {
    return false;
  }

  const Vec3 origin = origin_override != nullptr ? *origin_override : get_esp_draw_origin(entity);
  const Vec3 mins = entity->get_collideable_mins();
  const Vec3 maxs = entity->get_collideable_maxs();
  const float middle_z = (mins.z + maxs.z) * 0.5f;
  const std::array<Vec3, 6> points = {{
    origin + Vec3{0.0f, 0.0f, mins.z},
    origin + Vec3{0.0f, 0.0f, maxs.z},
    origin + Vec3{mins.x, mins.y, middle_z},
    origin + Vec3{mins.x, maxs.y, middle_z},
    origin + Vec3{maxs.x, mins.y, middle_z},
    origin + Vec3{maxs.x, maxs.y, middle_z},
  }};

  auto current_bounds = empty_screen_bounds();
  for (const auto& point : points) {
    Vec3 projected{};
    if (!overlay_projection::world_to_screen(point, &projected) || !add_projected_screen_point(projected, &current_bounds)) {
      return false;
    }
  }

  const auto class_id_value = entity->get_class_id();
  if (class_id_value == class_id::PLAYER ||
      class_id_value == class_id::SENTRY ||
      class_id_value == class_id::DISPENSER ||
      class_id_value == class_id::TELEPORTER) {
    const float width = current_bounds.width();
    current_bounds.min_x += width * 0.125f;
    current_bounds.max_x = current_bounds.min_x + (width * 0.75f);
  }

  if (!is_reasonable_screen_bounds(current_bounds)) {
    return false;
  }

  *bounds = current_bounds;
  return true;
}

[[nodiscard]] Entity* get_player_resource_entity()
{
  if (entity_list == nullptr) {
    return nullptr;
  }

  for (unsigned int index = 1; index <= static_cast<unsigned int>(entity_list->get_max_entities()); ++index) {
    auto* entity = entity_list->entity_from_index(index);
    if (entity == nullptr) {
      continue;
    }

    if (entity->get_class_id() == class_id::PLAYER_RESOURCE) {
      return entity;
    }
  }

  return nullptr;
}

template <typename value_type>
[[nodiscard]] value_type read_player_resource_value(Entity* player_resource, uintptr_t array_offset, int player_index)
{
  if (player_resource == nullptr || player_index <= 0) {
    return {};
  }

  const auto base = reinterpret_cast<uintptr_t>(player_resource);
  const auto entry_offset = array_offset + (static_cast<uintptr_t>(player_index) * sizeof(value_type));
  return *reinterpret_cast<value_type*>(base + entry_offset);
}

[[nodiscard]] int get_mafia_level(Entity* player_resource, int player_index)
{
  const auto score = read_player_resource_value<int>(player_resource, player_resource_score_offset, player_index);
  const auto deaths = read_player_resource_value<int>(player_resource, player_resource_deaths_offset, player_index);
  const auto damage = read_player_resource_value<int>(player_resource, tf_player_resource_damage_offset, player_index);

  const auto level = (score * 3) + (damage / 100) - (deaths * 7);
  return std::clamp(level, 1, 100);
}

[[nodiscard]] uint32_t stable_mafia_seed(Player* player, int mafia_level)
{
  auto seed = static_cast<uint32_t>(mafia_level);
  auto info = player_info{};
  if (engine != nullptr && engine->get_player_info(player->get_index(), &info) && info.friends_id != 0) {
    seed ^= static_cast<uint32_t>(info.friends_id);
  } else {
    seed ^= static_cast<uint32_t>(player->get_index() * 0x9E3779B9u);
  }

  seed ^= seed >> 16;
  seed *= 0x7FEB352Du;
  seed ^= seed >> 15;
  seed *= 0x846CA68Bu;
  seed ^= seed >> 16;
  return seed;
}

[[nodiscard]] const char* get_mafia_title(Player* player, int mafia_level)
{
  std::array<const char*, cathook_mafia_titles.size()> matches{};
  size_t match_count = 0;

  for (const auto& range : cathook_mafia_titles) {
    if (mafia_level < range.min_level || mafia_level > range.max_level || range.title == nullptr) {
      continue;
    }

    matches[match_count++] = range.title;
  }

  if (match_count == 0) {
    return "Crook";
  }

  const auto seed = stable_mafia_seed(player, mafia_level);
  return matches[seed % match_count];
}

[[nodiscard]] std::string get_mafia_text(Player* player, Entity* player_resource)
{
  if (player == nullptr || player_resource == nullptr) {
    return {};
  }

  const auto mafia_level = get_mafia_level(player_resource, player->get_index());
  const auto* title = get_mafia_title(player, mafia_level);
  return "Lv." + std::to_string(mafia_level) + " " + title;
}

[[nodiscard]] int get_head_emoji_tile_index(Player* player)
{
  if (player == nullptr) {
    return -1;
  }

  const auto tf_class_value = static_cast<int>(player->get_tf_class());
  if (tf_class_value <= static_cast<int>(tf_class::UNDEFINED) || tf_class_value > static_cast<int>(tf_class::ENGINEER)) {
    return -1;
  }

  return tf_class_value - 1;
}

[[nodiscard]] bool should_draw_player_overlay_icon(Player* player, Player* localplayer, bool include_teammates)
{
  if (player == nullptr || localplayer == nullptr) {
    return false;
  }

  return player == localplayer || player->get_team() != localplayer->get_team() || include_teammates || player->is_friend();
}

[[nodiscard]] bool should_consider_player_for_esp(Player* player, Player* localplayer)
{
  if (player == nullptr || localplayer == nullptr) {
    return false;
  }
  if (!player->is_alive()) {
    return false;
  }

  return true;
}

[[nodiscard]] bool get_player_head_emoji_position(Player* player, Vec3* head_position)
{
  if (player == nullptr || head_position == nullptr) {
    return false;
  }

  if (!player->get_hitbox_center(aim_hitbox_head, head_position)) {
    const auto bone_position = player->get_bone_pos(player->get_head_bone());
    if (!std::isfinite(bone_position.x)
        || !std::isfinite(bone_position.y)
        || !std::isfinite(bone_position.z)
        || (bone_position.x == 0.0f && bone_position.y == 0.0f && bone_position.z == 0.0f)) {
      return false;
    }

    const auto origin = get_esp_draw_origin(player->to_entity());
    if (distance_between(origin, bone_position) > 160.0f) {
      return false;
    }

    *head_position = bone_position;
  }

  return true;
}

[[nodiscard]] int get_head_emoji_tile_column(int style)
{
  return cathook_head_emoji_first_tile_column + std::clamp(style, 0, cathook_head_emoji_style_count - 1);
}

void draw_atlas_tile(
  ImDrawList* draw_list,
  ImTextureData* texture,
  int tile_column,
  int tile_row,
  const ImVec2& center,
  float size,
  ImU32 tint = IM_COL32_WHITE)
{
  if (draw_list == nullptr || tile_column < 0 || tile_row < 0 || size <= 0.0f) {
    return;
  }

  if (texture == nullptr || texture->Status != ImTextureStatus_OK || texture->Width <= 0 || texture->Height <= 0) {
    if (!ensure_head_emoji_atlas_loaded() || g_head_emoji_atlas.pixels.empty() ||
        g_head_emoji_atlas.width <= 0 || g_head_emoji_atlas.height <= 0) {
      return;
    }

    constexpr int tile_size = static_cast<int>(cathook_head_emoji_tile_size);
    const int source_x = tile_column * tile_size;
    const int source_y = tile_row * tile_size;
    if (source_x < 0 || source_y < 0 ||
        source_x + tile_size > g_head_emoji_atlas.width ||
        source_y + tile_size > g_head_emoji_atlas.height) {
      return;
    }

    const int samples = std::clamp(static_cast<int>(std::ceil(size / 2.0f)), 12, tile_size);
    const float block_size = size / static_cast<float>(samples);
    const ImVec2 top_left(center.x - (size * 0.5f), center.y - (size * 0.5f));
    for (int y = 0; y < samples; ++y) {
      for (int x = 0; x < samples; ++x) {
        const int pixel_x = source_x + std::clamp((x * tile_size) / samples, 0, tile_size - 1);
        const int pixel_y = source_y + std::clamp((y * tile_size) / samples, 0, tile_size - 1);
        const size_t pixel_offset = (static_cast<size_t>(pixel_y) * static_cast<size_t>(g_head_emoji_atlas.width) + static_cast<size_t>(pixel_x)) * 4u;
        const uint8_t source_alpha = g_head_emoji_atlas.pixels[pixel_offset + 3u];
        if (source_alpha < 24) {
          continue;
        }

        const uint32_t tint_alpha = (tint >> IM_COL32_A_SHIFT) & 0xFFu;
        const auto alpha = static_cast<uint8_t>(
          (static_cast<uint32_t>(source_alpha) * tint_alpha) / 255u);
        if (alpha == 0) {
          continue;
        }

        const auto color = IM_COL32(
          g_head_emoji_atlas.pixels[pixel_offset],
          g_head_emoji_atlas.pixels[pixel_offset + 1u],
          g_head_emoji_atlas.pixels[pixel_offset + 2u],
          alpha);
        draw_list->AddRectFilled(
          ImVec2(top_left.x + (static_cast<float>(x) * block_size), top_left.y + (static_cast<float>(y) * block_size)),
          ImVec2(top_left.x + (static_cast<float>(x + 1) * block_size) + 0.5f, top_left.y + (static_cast<float>(y + 1) * block_size) + 0.5f),
          color);
      }
    }
    return;
  }

  const auto atlas_width = static_cast<float>(texture->Width);
  const auto atlas_height = static_cast<float>(texture->Height);
  const auto uv_min = ImVec2(
    (static_cast<float>(tile_column) * cathook_head_emoji_tile_size) / atlas_width,
    (static_cast<float>(tile_row) * cathook_head_emoji_tile_size) / atlas_height);
  const auto uv_max = ImVec2(
    ((static_cast<float>(tile_column) + 1.0f) * cathook_head_emoji_tile_size) / atlas_width,
    ((static_cast<float>(tile_row) + 1.0f) * cathook_head_emoji_tile_size) / atlas_height);

  draw_list->AddImage(
    texture->GetTexRef(),
    ImVec2(center.x - (size * 0.5f), center.y - (size * 0.5f)),
    ImVec2(center.x + (size * 0.5f), center.y + (size * 0.5f)),
    uv_min,
    uv_max,
    tint);
}

[[nodiscard]] bool get_entity_screen_bounds(Entity* entity, esp_bounds* bounds,
  const Vec3* origin_override)
{
  if (entity == nullptr || bounds == nullptr || render_view == nullptr) {
    return false;
  }

  if (get_entity_centerline_screen_bounds(entity, bounds, origin_override)) {
    return true;
  }

  const auto origin = origin_override != nullptr ? *origin_override : get_esp_draw_origin(entity);
  const auto mins = entity->get_collideable_mins();
  const auto maxs = entity->get_collideable_maxs();

  const std::array<Vec3, 8> world_points = {
    origin + Vec3{mins.x, mins.y, mins.z},
    origin + Vec3{maxs.x, mins.y, mins.z},
    origin + Vec3{maxs.x, maxs.y, mins.z},
    origin + Vec3{mins.x, maxs.y, mins.z},
    origin + Vec3{mins.x, mins.y, maxs.z},
    origin + Vec3{maxs.x, mins.y, maxs.z},
    origin + Vec3{maxs.x, maxs.y, maxs.z},
    origin + Vec3{mins.x, maxs.y, maxs.z},
  };

  if (project_world_points_to_screen_bounds(world_points, bounds)) {
    return true;
  }

  return get_entity_centerline_screen_bounds(entity, bounds, origin_override);
}

[[nodiscard]] bool get_entity_projected_box(Entity* entity, projected_box* box,
  const Vec3* origin_override)
{
  if (entity == nullptr || box == nullptr || render_view == nullptr) {
    return false;
  }

  const auto origin = origin_override != nullptr ? *origin_override : get_esp_draw_origin(entity);
  const auto mins = entity->get_collideable_mins();
  const auto maxs = entity->get_collideable_maxs();

  const std::array<Vec3, 8> world_points = {
    origin + Vec3{mins.x, mins.y, mins.z},
    origin + Vec3{maxs.x, mins.y, mins.z},
    origin + Vec3{maxs.x, maxs.y, mins.z},
    origin + Vec3{mins.x, maxs.y, mins.z},
    origin + Vec3{mins.x, mins.y, maxs.z},
    origin + Vec3{maxs.x, mins.y, maxs.z},
    origin + Vec3{maxs.x, maxs.y, maxs.z},
    origin + Vec3{mins.x, maxs.y, maxs.z},
  };

  float min_x = std::numeric_limits<float>::max();
  float min_y = std::numeric_limits<float>::max();
  float max_x = std::numeric_limits<float>::lowest();
  float max_y = std::numeric_limits<float>::lowest();

  for (size_t index = 0; index < world_points.size(); ++index) {
    auto projected = Vec3{};
    if (!overlay_projection::world_to_screen(world_points[index], &projected)) {
      return false;
    }

    box->screen_points[index] = projected;
    min_x = std::min(min_x, projected.x);
    min_y = std::min(min_y, projected.y);
    max_x = std::max(max_x, projected.x);
    max_y = std::max(max_y, projected.y);
  }

  const auto current_bounds = esp_bounds{
    .min_x = min_x,
    .min_y = min_y,
    .max_x = max_x,
    .max_y = max_y
  };
  if (!is_reasonable_screen_bounds(current_bounds)) {
    return false;
  }

  box->bounds = current_bounds;
  return true;
}

[[nodiscard]] ImU32 neutral_esp_text_color(const RGBA_float& base_color, float alpha_scale)
{
  auto text_color = base_color;
  text_color.a = std::clamp(text_color.a * alpha_scale, 0.0f, 1.0f);
  return to_imgui_color(text_color.to_RGBA());
}

[[nodiscard]] RGBA color_with_alpha(RGBA color, float alpha_scale)
{
  color.a = std::clamp(static_cast<int>(std::round(static_cast<float>(color.a) * alpha_scale)), 0, 255);
  return color;
}

[[nodiscard]] RGBA imgui_to_rgba(ImU32 color)
{
  return RGBA{
    static_cast<int>((color >> IM_COL32_R_SHIFT) & 0xFF),
    static_cast<int>((color >> IM_COL32_G_SHIFT) & 0xFF),
    static_cast<int>((color >> IM_COL32_B_SHIFT) & 0xFF),
    static_cast<int>((color >> IM_COL32_A_SHIFT) & 0xFF)
  };
}

[[nodiscard]] int surface_round(float value)
{
  return static_cast<int>(std::round(value));
}

[[nodiscard]] unsigned long esp_surface_font()
{
  static unsigned long font = 0;
  static int last_tall = 0;
  const int tall = std::max(8, cathook_surface_font_tall);
  if (surface == nullptr) {
    return 0;
  }

  if (font == 0 || last_tall != tall) {
    font = surface->text_create_font();
    if (font != 0) {
      surface->text_set_font_glyph_set(font, "Verdana", tall, cathook_surface_font_weight, 0, 0, 0);
      last_tall = tall;
    }
  }

  return font;
}

[[nodiscard]] std::wstring widen_for_surface(const std::string& text)
{
  auto wide = std::wstring{};
  wide.reserve(text.size());
  for (const unsigned char character : text) {
    wide.push_back(static_cast<wchar_t>(character));
  }
  return wide;
}

[[nodiscard]] ImVec2 surface_text_size(const std::string& text)
{
  const auto font = esp_surface_font();
  if (font == 0 || text.empty() || surface == nullptr) {
    return {};
  }

  const auto wide = widen_for_surface(text);
  return ImVec2(
    static_cast<float>(surface->get_string_width(font, wide.c_str())),
    static_cast<float>(std::max(surface->get_font_height(font), cathook_surface_font_tall)));
}

[[nodiscard]] float surface_text_line_height()
{
  const auto font = esp_surface_font();
  if (font == 0 || surface == nullptr) {
    return static_cast<float>(cathook_surface_font_tall + 2);
  }

  return static_cast<float>(std::max(surface->get_font_height(font), cathook_surface_font_tall) + 2);
}

[[nodiscard]] bool get_backtrack_record_screen_bounds(const backtrack::backtrack_record& record, esp_bounds* bounds)
{
  if (bounds == nullptr || !record.valid || render_view == nullptr) {
    return false;
  }

  const auto mins = record.origin + record.mins;
  const auto maxs = record.origin + record.maxs;
  const std::array<Vec3, 8> world_points = {
    Vec3{mins.x, mins.y, mins.z},
    Vec3{maxs.x, mins.y, mins.z},
    Vec3{maxs.x, maxs.y, mins.z},
    Vec3{mins.x, maxs.y, mins.z},
    Vec3{mins.x, mins.y, maxs.z},
    Vec3{maxs.x, mins.y, maxs.z},
    Vec3{maxs.x, maxs.y, maxs.z},
    Vec3{mins.x, maxs.y, maxs.z},
  };

  float min_x = std::numeric_limits<float>::max();
  float min_y = std::numeric_limits<float>::max();
  float max_x = std::numeric_limits<float>::lowest();
  float max_y = std::numeric_limits<float>::lowest();
  for (const auto& point : world_points) {
    auto projected = Vec3{};
    if (!overlay_projection::world_to_screen(point, &projected)) {
      return false;
    }

    min_x = std::min(min_x, projected.x);
    min_y = std::min(min_y, projected.y);
    max_x = std::max(max_x, projected.x);
    max_y = std::max(max_y, projected.y);
  }

  const auto current_bounds = esp_bounds{
    .min_x = min_x,
    .min_y = min_y,
    .max_x = max_x,
    .max_y = max_y
  };
  if (!is_reasonable_screen_bounds(current_bounds)) {
    return false;
  }

  *bounds = current_bounds;
  return true;
}

[[nodiscard]] bool get_backtrack_record_projected_box(const backtrack::backtrack_record& record, projected_box* box)
{
  if (box == nullptr || !record.valid || render_view == nullptr) {
    return false;
  }

  const auto mins = record.origin + record.mins;
  const auto maxs = record.origin + record.maxs;
  const std::array<Vec3, 8> world_points = {
    Vec3{mins.x, mins.y, mins.z},
    Vec3{maxs.x, mins.y, mins.z},
    Vec3{maxs.x, maxs.y, mins.z},
    Vec3{mins.x, maxs.y, mins.z},
    Vec3{mins.x, mins.y, maxs.z},
    Vec3{maxs.x, mins.y, maxs.z},
    Vec3{maxs.x, maxs.y, maxs.z},
    Vec3{mins.x, maxs.y, maxs.z},
  };

  float min_x = std::numeric_limits<float>::max();
  float min_y = std::numeric_limits<float>::max();
  float max_x = std::numeric_limits<float>::lowest();
  float max_y = std::numeric_limits<float>::lowest();
  for (size_t index = 0; index < world_points.size(); ++index) {
    auto projected = Vec3{};
    if (!overlay_projection::world_to_screen(world_points[index], &projected)) {
      return false;
    }

    box->screen_points[index] = projected;
    min_x = std::min(min_x, projected.x);
    min_y = std::min(min_y, projected.y);
    max_x = std::max(max_x, projected.x);
    max_y = std::max(max_y, projected.y);
  }

  const auto current_bounds = esp_bounds{
    .min_x = min_x,
    .min_y = min_y,
    .max_x = max_x,
    .max_y = max_y
  };
  if (!is_reasonable_screen_bounds(current_bounds)) {
    return false;
  }

  box->bounds = current_bounds;
  return true;
}

void draw_outline_box(ImDrawList* draw_list, const esp_bounds& bounds, ImU32 color, float alpha_scale)
{
  if (draw_list == nullptr) {
    return;
  }

  draw_list->AddRect(
    ImVec2(bounds.min_x, bounds.min_y),
    ImVec2(bounds.max_x, bounds.max_y),
    black_with_alpha(alpha_scale),
    0.0f,
    0,
    3.0f);
  draw_list->AddRect(
    ImVec2(bounds.min_x, bounds.min_y),
    ImVec2(bounds.max_x, bounds.max_y),
    color,
    0.0f,
    0,
    1.0f);
}

void draw_corner_box(ImDrawList* draw_list, const esp_bounds& bounds, ImU32 color, float alpha_scale)
{
  if (draw_list == nullptr) {
    return;
  }

  const auto black = black_with_alpha(alpha_scale);
  const auto height_size = std::max(4.0f, (bounds.height() - 3.0f) * cathook_corner_scale);
  const auto width_size = std::max(4.0f, (bounds.width() - 2.0f) * cathook_corner_scale);

  draw_list->AddRectFilled(ImVec2(bounds.min_x, bounds.min_y), ImVec2(bounds.min_x + width_size + 1.0f, bounds.min_y + 3.0f), black);
  draw_list->AddRectFilled(ImVec2(bounds.min_x, bounds.min_y + 3.0f), ImVec2(bounds.min_x + 3.0f, bounds.min_y + height_size), black);

  draw_list->AddRectFilled(ImVec2(bounds.max_x - width_size - 1.0f, bounds.min_y), ImVec2(bounds.max_x, bounds.min_y + 3.0f), black);
  draw_list->AddRectFilled(ImVec2(bounds.max_x - 2.0f, bounds.min_y + 3.0f), ImVec2(bounds.max_x + 1.0f, bounds.min_y + height_size), black);

  draw_list->AddRectFilled(ImVec2(bounds.min_x, bounds.max_y - 3.0f), ImVec2(bounds.min_x + width_size + 1.0f, bounds.max_y), black);
  draw_list->AddRectFilled(ImVec2(bounds.min_x, bounds.max_y - height_size), ImVec2(bounds.min_x + 3.0f, bounds.max_y), black);

  draw_list->AddRectFilled(ImVec2(bounds.max_x - width_size - 1.0f, bounds.max_y - 3.0f), ImVec2(bounds.max_x, bounds.max_y), black);
  draw_list->AddRectFilled(ImVec2(bounds.max_x - 2.0f, bounds.max_y - height_size), ImVec2(bounds.max_x + 1.0f, bounds.max_y), black);

  draw_list->AddLine(ImVec2(bounds.min_x + 1.0f, bounds.min_y + 1.0f), ImVec2(bounds.min_x + 1.0f + width_size, bounds.min_y + 1.0f), color, 1.0f);
  draw_list->AddLine(ImVec2(bounds.min_x + 1.0f, bounds.min_y + 1.0f), ImVec2(bounds.min_x + 1.0f, bounds.min_y + 1.0f + height_size), color, 1.0f);

  draw_list->AddLine(ImVec2(bounds.max_x - 1.0f, bounds.min_y + 1.0f), ImVec2(bounds.max_x - 1.0f - width_size, bounds.min_y + 1.0f), color, 1.0f);
  draw_list->AddLine(ImVec2(bounds.max_x - 1.0f, bounds.min_y + 1.0f), ImVec2(bounds.max_x - 1.0f, bounds.min_y + 1.0f + height_size), color, 1.0f);

  draw_list->AddLine(ImVec2(bounds.min_x + 1.0f, bounds.max_y - 1.0f), ImVec2(bounds.min_x + 1.0f + width_size, bounds.max_y - 1.0f), color, 1.0f);
  draw_list->AddLine(ImVec2(bounds.min_x + 1.0f, bounds.max_y - 1.0f), ImVec2(bounds.min_x + 1.0f, bounds.max_y - 1.0f - height_size), color, 1.0f);

  draw_list->AddLine(ImVec2(bounds.max_x - 1.0f, bounds.max_y - 1.0f), ImVec2(bounds.max_x - 1.0f - width_size, bounds.max_y - 1.0f), color, 1.0f);
  draw_list->AddLine(ImVec2(bounds.max_x - 1.0f, bounds.max_y - 1.0f), ImVec2(bounds.max_x - 1.0f, bounds.max_y - 1.0f - height_size), color, 1.0f);
}

void draw_filled_box(ImDrawList* draw_list, const esp_bounds& bounds, ImU32 color, float alpha_scale)
{
  if (draw_list == nullptr) {
    return;
  }

  draw_list->AddRectFilled(
    ImVec2(bounds.min_x, bounds.min_y),
    ImVec2(bounds.max_x, bounds.max_y),
    with_alpha(RGBA{
      static_cast<uint8_t>((color >> IM_COL32_R_SHIFT) & 0xFF),
      static_cast<uint8_t>((color >> IM_COL32_G_SHIFT) & 0xFF),
      static_cast<uint8_t>((color >> IM_COL32_B_SHIFT) & 0xFF),
      static_cast<uint8_t>((color >> IM_COL32_A_SHIFT) & 0xFF)
    }, 0.18f * alpha_scale));
  draw_outline_box(draw_list, bounds, color, alpha_scale);
}

void draw_rounded_box(ImDrawList* draw_list, const esp_bounds& bounds, ImU32 color, float alpha_scale)
{
  if (draw_list == nullptr) {
    return;
  }

  constexpr float rounding = 4.0f;
  draw_list->AddRect(
    ImVec2(bounds.min_x, bounds.min_y),
    ImVec2(bounds.max_x, bounds.max_y),
    black_with_alpha(alpha_scale),
    rounding,
    0,
    3.0f);
  draw_list->AddRect(
    ImVec2(bounds.min_x, bounds.min_y),
    ImVec2(bounds.max_x, bounds.max_y),
    color,
    rounding,
    0,
    1.0f);
}

void draw_projected_box(ImDrawList* draw_list, const projected_box& box, ImU32 color, float alpha_scale)
{
  if (draw_list == nullptr) {
    return;
  }

  constexpr std::array<std::pair<size_t, size_t>, 12> edges = {{
    {0, 1}, {1, 2}, {2, 3}, {3, 0},
    {4, 5}, {5, 6}, {6, 7}, {7, 4},
    {0, 4}, {1, 5}, {2, 6}, {3, 7},
  }};

  const auto shadow = black_with_alpha(alpha_scale);
  for (const auto& [start, end] : edges) {
    const auto& first = box.screen_points[start];
    const auto& second = box.screen_points[end];
    draw_list->AddLine(ImVec2(first.x + 1.0f, first.y + 1.0f), ImVec2(second.x + 1.0f, second.y + 1.0f), shadow, 3.0f);
    draw_list->AddLine(ImVec2(first.x, first.y), ImVec2(second.x, second.y), color, 1.0f);
  }
}

void draw_esp_box(ImDrawList* draw_list, Entity* entity, const esp_bounds& bounds,
  esp_box_type box_style, ImU32 color, float alpha_scale, const Vec3* origin_override = nullptr)
{
  switch (box_style) {
  case esp_box_type::outline:
    draw_outline_box(draw_list, bounds, color, alpha_scale);
    break;
  case esp_box_type::corner:
    draw_corner_box(draw_list, bounds, color, alpha_scale);
    break;
  case esp_box_type::filled:
    draw_filled_box(draw_list, bounds, color, alpha_scale);
    break;
  case esp_box_type::rounded:
    draw_rounded_box(draw_list, bounds, color, alpha_scale);
    break;
  case esp_box_type::projected: {
    auto box = projected_box{};
    if (entity != nullptr && get_entity_projected_box(entity, &box, origin_override)) {
      if (origin_override == nullptr) {
        smooth_projected_box(entity, &box);
      }
      draw_projected_box(draw_list, box, color, alpha_scale);
      break;
    }

    draw_outline_box(draw_list, bounds, color, alpha_scale);
    break;
  }
  }
}

void draw_vertical_health_bar(ImDrawList* draw_list, const esp_bounds& bounds, int health, int max_health, float alpha_scale)
{
  if (draw_list == nullptr || max_health <= 0) {
    return;
  }

  const auto border = black_with_alpha(alpha_scale);
  const auto fill_color = with_alpha(get_health_color(health, max_health), alpha_scale);
  const auto clamped_ratio = std::clamp(static_cast<float>(health) / static_cast<float>(max_health), 0.0f, 1.0f);
  const auto fill_height = (bounds.height() - 2.0f) * clamped_ratio;

  const auto outer_min = ImVec2(bounds.min_x - cathook_healthbar_width, bounds.min_y);
  const auto outer_max = ImVec2(bounds.min_x, bounds.max_y);
  draw_list->AddRect(outer_min, outer_max, border, 0.0f, 0, 1.0f);

  if (fill_height <= 0.0f) {
    return;
  }

  const auto fill_min = ImVec2(bounds.min_x - cathook_healthbar_width + 1.0f, bounds.max_y - fill_height - 1.0f);
  const auto fill_max = ImVec2(bounds.min_x - cathook_healthbar_width + 1.0f + cathook_healthbar_fill_width, bounds.max_y - 1.0f);
  draw_list->AddRectFilled(fill_min, fill_max, fill_color);
}

void draw_text_centered(ImDrawList* draw_list, const ImVec2& position, ImU32 color, const std::string& text)
{
  if (draw_list == nullptr || text.empty()) {
    return;
  }

  const auto text_size = ImGui::CalcTextSize(text.c_str());
  const auto text_pos = ImVec2(position.x - text_size.x * 0.5f, position.y);
  const auto text_alpha = static_cast<int>((color >> IM_COL32_A_SHIFT) & 0xFF);
  draw_list->AddText(ImVec2(text_pos.x + 1.0f, text_pos.y + 1.0f), IM_COL32(0, 0, 0, text_alpha), text.c_str());
  draw_list->AddText(text_pos, color, text.c_str());
}

void draw_text_centered_with_background(ImDrawList* draw_list, const ImVec2& position, ImU32 color, const std::string& text, uint8_t background_alpha, float alpha_scale)
{
  if (draw_list == nullptr || text.empty()) {
    return;
  }

  const auto text_size = ImGui::CalcTextSize(text.c_str());
  const auto text_pos = ImVec2(position.x - text_size.x * 0.5f, position.y);
  const auto alpha = std::clamp(static_cast<int>(std::round(static_cast<float>(background_alpha) * alpha_scale)), 0, 255);
  draw_list->AddRectFilled(
    ImVec2(text_pos.x - 3.0f, text_pos.y - 2.0f),
    ImVec2(text_pos.x + text_size.x + 3.0f, text_pos.y + text_size.y + 2.0f),
    IM_COL32(0, 0, 0, alpha),
    2.0f);
  draw_text_centered(draw_list, position, color, text);
}

void draw_text(ImDrawList* draw_list, const ImVec2& position, ImU32 color, const std::string& text)
{
  if (draw_list == nullptr || text.empty()) {
    return;
  }

  const auto text_alpha = static_cast<int>((color >> IM_COL32_A_SHIFT) & 0xFF);
  draw_list->AddText(ImVec2(position.x + 1.0f, position.y + 1.0f), IM_COL32(0, 0, 0, text_alpha), text.c_str());
  draw_list->AddText(position, color, text.c_str());
}

void draw_right_line(ImDrawList* draw_list, const esp_bounds& bounds, float* y, ImU32 color, const std::string& text)
{
  if (draw_list == nullptr || y == nullptr || text.empty()) {
    return;
  }

  draw_text(draw_list, ImVec2(bounds.max_x + cathook_text_padding, *y), color, text);
  *y += ImGui::GetTextLineHeight();
}

void draw_left_line(ImDrawList* draw_list, const esp_bounds& bounds, float* y, ImU32 color, const std::string& text)
{
  if (draw_list == nullptr || y == nullptr || text.empty()) {
    return;
  }

  const auto text_size = ImGui::CalcTextSize(text.c_str());
  draw_text(draw_list, ImVec2(bounds.min_x - cathook_text_padding - text_size.x, *y), color, text);
  *y += ImGui::GetTextLineHeight();
}

void draw_bottom_center_line(ImDrawList* draw_list, const esp_bounds& bounds, float* y, ImU32 color, const std::string& text)
{
  if (draw_list == nullptr || y == nullptr || text.empty()) {
    return;
  }

  draw_text_centered(draw_list, ImVec2((bounds.min_x + bounds.max_x) * 0.5f, *y), color, text);
  *y += ImGui::GetTextLineHeight();
}

void draw_bottom_progress_bar(ImDrawList* draw_list, const esp_bounds& bounds, float* y, float progress, const RGBA& fill, int divisions, float alpha_scale)
{
  if (draw_list == nullptr || y == nullptr || bounds.width() <= 0.0f) {
    return;
  }

  constexpr float height = 3.0f;
  constexpr float spacing = 3.0f;
  const float width = bounds.width();
  const float filled = width * std::clamp(progress, 0.0f, 1.0f);
  const ImVec2 minimum{bounds.min_x - 1.0f, *y - 1.0f};
  const ImVec2 maximum{bounds.max_x + 1.0f, *y + height + 1.0f};
  draw_list->AddRectFilled(minimum, maximum, black_with_alpha(alpha_scale));
  draw_list->AddRectFilled(
    ImVec2(bounds.min_x, *y),
    ImVec2(bounds.min_x + filled, *y + height),
    with_alpha(fill, alpha_scale));
  for (int index = 1; index < divisions; ++index) {
    const float division_x = bounds.min_x + width * (static_cast<float>(index) / static_cast<float>(divisions));
    draw_list->AddLine(
      ImVec2(division_x, *y),
      ImVec2(division_x, *y + height),
      black_with_alpha(alpha_scale));
  }
  *y += height + spacing;
}

void draw_player_condition_lines(ImDrawList* draw_list, const esp_bounds& bounds, float* right_y, Player* player, Player* localplayer, ImU32 neutral_text, ImU32 green_text, ImU32 red_text, int text_alpha, bool draw_common, bool draw_buffs, bool draw_debuffs)
{
  if (draw_list == nullptr || right_y == nullptr || player == nullptr) {
    return;
  }

  const auto blue_text = IM_COL32(80, 180, 255, text_alpha);
  const auto purple_text = IM_COL32(255, 120, 255, text_alpha);
  const auto orange_text = IM_COL32(255, 125, 0, text_alpha);
  const auto yellow_text = IM_COL32(255, 220, 40, text_alpha);
  const auto cyan_text = IM_COL32(220, 255, 255, text_alpha);
  const auto grey_text = IM_COL32(170, 170, 170, text_alpha);
  const auto add = [&](ImU32 color, const char* label) {
    draw_right_line(draw_list, bounds, right_y, color, label);
  };

  if (draw_common) {
    if (player->is_scoped()) add(green_text, "ZOOM");
    if (player->is_cloaked()) add(grey_text, "INVIS");
    if (player->in_cond(TF_COND_FEIGN_DEATH)) add(neutral_text, "DR");
  }

  if (draw_buffs) {
    if (player->is_invulnerable()) add(blue_text, "INVULN");
    if (player->is_crit_boosted()) add(purple_text, "CRIT");
    if (player->in_cond(TF_COND_MINICRITBOOSTED_ON_KILL)) add(purple_text, "MINICRIT");
    if (player->in_cond(TF_COND_OFFENSEBUFF)) add(green_text, "BANNER");
    if (player->in_cond(TF_COND_DEFENSEBUFF)) add(blue_text, "BACKUP");
    if (player->in_cond(TF_COND_REGENONDAMAGEBUFF)) add(green_text, "CONCH");
    if (player->in_cond(TF_COND_MEDIGUN_UBER_BULLET_RESIST)) add(blue_text, "BULLETRES");
    if (player->in_cond(TF_COND_MEDIGUN_UBER_BLAST_RESIST)) add(blue_text, "BLASTRES");
    if (player->in_cond(TF_COND_MEDIGUN_UBER_FIRE_RESIST)) add(orange_text, "FIRERES");
  }

  if (draw_debuffs) {
    if (player->in_cond(TF_COND_MARKEDFORDEATH) || player->in_cond(TF_COND_MARKEDFORDEATH_SILENT)) add(red_text, "MARKED");
    if (player->in_cond(TF_COND_MAD_MILK)) add(cyan_text, "MILK");
    if (player->in_cond(TF_COND_TAUNTING)) add(neutral_text, "TAUNT");
    if (player->in_cond(TF_COND_DISGUISED) || player->in_cond(TF_COND_DISGUISING)) add(neutral_text, "DISGUISE");
    if (player->in_cond(TF_COND_BURNING) || player->in_cond(TF_COND_BURNING_PYRO)) add(orange_text, "BURNING");
  }

  if (draw_common && (player->in_cond(TF_COND_DISGUISED) || player->in_cond(TF_COND_DISGUISING))) {
    add(neutral_text, "DISGUISE");
    if (localplayer != nullptr && player != localplayer && player->get_team() != localplayer->get_team() && localplayer->in_cond(TF_COND_TEAM_GLOWS) && !player->is_cloaked()) {
      add(neutral_text, "XRAY");
    }
  }
}

void surface_set_color(RGBA color)
{
  if (surface == nullptr) {
    return;
  }

  surface->set_rgba(color);
}

void surface_filled_rect(float x1, float y1, float x2, float y2, RGBA color)
{
  if (surface == nullptr || color.a <= 0) {
    return;
  }

  surface_set_color(color);
  surface->draw_filled_rect(surface_round(x1), surface_round(y1), surface_round(x2), surface_round(y2));
}

void surface_line(float x1, float y1, float x2, float y2, RGBA color)
{
  if (surface == nullptr || color.a <= 0) {
    return;
  }

  surface_set_color(color);
  surface->draw_line(surface_round(x1), surface_round(y1), surface_round(x2), surface_round(y2));
}

void surface_outline_box(const esp_bounds& bounds, RGBA color, float alpha_scale)
{
  if (surface == nullptr) {
    return;
  }

  const auto black = color_with_alpha(RGBA{0, 0, 0, 255}, alpha_scale);
  surface_set_color(black);
  surface->draw_outlined_rect(surface_round(bounds.min_x - 1.0f), surface_round(bounds.min_y - 1.0f), surface_round(bounds.max_x + 1.0f), surface_round(bounds.max_y + 1.0f));
  surface->draw_outlined_rect(surface_round(bounds.min_x + 1.0f), surface_round(bounds.min_y + 1.0f), surface_round(bounds.max_x - 1.0f), surface_round(bounds.max_y - 1.0f));
  surface_set_color(color);
  surface->draw_outlined_rect(surface_round(bounds.min_x), surface_round(bounds.min_y), surface_round(bounds.max_x), surface_round(bounds.max_y));
}

void surface_corner_box(const esp_bounds& bounds, RGBA color, float alpha_scale)
{
  const auto black = color_with_alpha(RGBA{0, 0, 0, 255}, alpha_scale);
  const auto height_size = std::max(4.0f, (bounds.height() - 3.0f) * cathook_corner_scale);
  const auto width_size = std::max(4.0f, (bounds.width() - 2.0f) * cathook_corner_scale);

  surface_filled_rect(bounds.min_x, bounds.min_y, bounds.min_x + width_size + 1.0f, bounds.min_y + 3.0f, black);
  surface_filled_rect(bounds.min_x, bounds.min_y + 3.0f, bounds.min_x + 3.0f, bounds.min_y + height_size, black);
  surface_filled_rect(bounds.max_x - width_size - 1.0f, bounds.min_y, bounds.max_x, bounds.min_y + 3.0f, black);
  surface_filled_rect(bounds.max_x - 2.0f, bounds.min_y + 3.0f, bounds.max_x + 1.0f, bounds.min_y + height_size, black);
  surface_filled_rect(bounds.min_x, bounds.max_y - 3.0f, bounds.min_x + width_size + 1.0f, bounds.max_y, black);
  surface_filled_rect(bounds.min_x, bounds.max_y - height_size, bounds.min_x + 3.0f, bounds.max_y, black);
  surface_filled_rect(bounds.max_x - width_size - 1.0f, bounds.max_y - 3.0f, bounds.max_x, bounds.max_y, black);
  surface_filled_rect(bounds.max_x - 2.0f, bounds.max_y - height_size, bounds.max_x + 1.0f, bounds.max_y, black);

  surface_line(bounds.min_x + 1.0f, bounds.min_y + 1.0f, bounds.min_x + 1.0f + width_size, bounds.min_y + 1.0f, color);
  surface_line(bounds.min_x + 1.0f, bounds.min_y + 1.0f, bounds.min_x + 1.0f, bounds.min_y + 1.0f + height_size, color);
  surface_line(bounds.max_x - 1.0f, bounds.min_y + 1.0f, bounds.max_x - 1.0f - width_size, bounds.min_y + 1.0f, color);
  surface_line(bounds.max_x - 1.0f, bounds.min_y + 1.0f, bounds.max_x - 1.0f, bounds.min_y + 1.0f + height_size, color);
  surface_line(bounds.min_x + 1.0f, bounds.max_y - 1.0f, bounds.min_x + 1.0f + width_size, bounds.max_y - 1.0f, color);
  surface_line(bounds.min_x + 1.0f, bounds.max_y - 1.0f, bounds.min_x + 1.0f, bounds.max_y - 1.0f - height_size, color);
  surface_line(bounds.max_x - 1.0f, bounds.max_y - 1.0f, bounds.max_x - 1.0f - width_size, bounds.max_y - 1.0f, color);
  surface_line(bounds.max_x - 1.0f, bounds.max_y - 1.0f, bounds.max_x - 1.0f, bounds.max_y - 1.0f - height_size, color);
}

void surface_esp_box(Entity* entity, const esp_bounds& bounds, esp_box_type box_style, RGBA color, float alpha_scale)
{
  switch (box_style) {
  case esp_box_type::corner:
    surface_corner_box(bounds, color, alpha_scale);
    break;
  case esp_box_type::filled:
    surface_filled_rect(bounds.min_x, bounds.min_y, bounds.max_x, bounds.max_y, color_with_alpha(color, 0.18f * alpha_scale));
    surface_outline_box(bounds, color, alpha_scale);
    break;
  case esp_box_type::projected: {
    auto box = projected_box{};
    if (entity != nullptr && get_entity_projected_box(entity, &box)) {
      constexpr std::array<std::pair<size_t, size_t>, 12> edges = {{
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
      }};
      const auto shadow = color_with_alpha(RGBA{0, 0, 0, 255}, alpha_scale);
      for (const auto& [start, end] : edges) {
        surface_line(box.screen_points[start].x + 1.0f, box.screen_points[start].y + 1.0f, box.screen_points[end].x + 1.0f, box.screen_points[end].y + 1.0f, shadow);
        surface_line(box.screen_points[start].x, box.screen_points[start].y, box.screen_points[end].x, box.screen_points[end].y, color);
      }
      break;
    }
    surface_outline_box(bounds, color, alpha_scale);
    break;
  }
  case esp_box_type::rounded:
  case esp_box_type::outline:
  default:
    surface_outline_box(bounds, color, alpha_scale);
    break;
  }
}

void surface_text_at(const ImVec2& position, RGBA color, const std::string& text)
{
  const auto font = esp_surface_font();
  if (surface == nullptr || font == 0 || text.empty() || color.a <= 0) {
    return;
  }

  const auto wide = widen_for_surface(text);
  surface->draw_set_text_font(font);
  surface->draw_set_text_color(0, 0, 0, color.a);
  surface->draw_set_text_pos(surface_round(position.x + 1.0f), surface_round(position.y + 1.0f));
  surface->draw_print_text(wide.c_str(), static_cast<int>(wide.size()));
  surface->draw_set_text_color(color);
  surface->draw_set_text_pos(surface_round(position.x), surface_round(position.y));
  surface->draw_print_text(wide.c_str(), static_cast<int>(wide.size()));
}

void surface_text_centered(const ImVec2& position, RGBA color, const std::string& text)
{
  const auto text_size = surface_text_size(text);
  surface_text_at(ImVec2(position.x - text_size.x * 0.5f, position.y), color, text);
}

void surface_text_centered_with_background(const ImVec2& position, RGBA color, const std::string& text, uint8_t background_alpha, float alpha_scale)
{
  if (text.empty()) {
    return;
  }

  const auto text_size = surface_text_size(text);
  const auto text_pos = ImVec2(position.x - text_size.x * 0.5f, position.y);
  const auto alpha = std::clamp(static_cast<int>(std::round(static_cast<float>(background_alpha) * alpha_scale)), 0, 255);
  surface_filled_rect(text_pos.x - 3.0f, text_pos.y - 2.0f, text_pos.x + text_size.x + 3.0f, text_pos.y + text_size.y + 2.0f, RGBA{0, 0, 0, alpha});
  surface_text_at(text_pos, color, text);
}

void surface_right_line(const esp_bounds& bounds, float* y, RGBA color, const std::string& text)
{
  if (y == nullptr || text.empty()) {
    return;
  }

  surface_text_at(ImVec2(bounds.max_x + cathook_text_padding, *y), color, text);
  *y += surface_text_line_height();
}

void surface_left_line(const esp_bounds& bounds, float* y, RGBA color, const std::string& text)
{
  if (y == nullptr || text.empty()) {
    return;
  }

  const auto text_size = surface_text_size(text);
  surface_text_at(ImVec2(bounds.min_x - cathook_text_padding - text_size.x, *y), color, text);
  *y += surface_text_line_height();
}

void surface_bottom_center_line(const esp_bounds& bounds, float* y, RGBA color, const std::string& text)
{
  if (y == nullptr || text.empty()) {
    return;
  }

  surface_text_centered(ImVec2((bounds.min_x + bounds.max_x) * 0.5f, *y), color, text);
  *y += surface_text_line_height();
}

void surface_vertical_health_bar(const esp_bounds& bounds, int health, int max_health, float alpha_scale)
{
  if (max_health <= 0) {
    return;
  }

  const auto border = color_with_alpha(RGBA{0, 0, 0, 255}, alpha_scale);
  const auto fill_color = color_with_alpha(get_health_color(health, max_health), alpha_scale);
  const auto clamped_ratio = std::clamp(static_cast<float>(health) / static_cast<float>(max_health), 0.0f, 1.0f);
  const auto fill_height = (bounds.height() - 2.0f) * clamped_ratio;
  surface_set_color(border);
  if (surface != nullptr) {
    surface->draw_outlined_rect(surface_round(bounds.min_x - cathook_healthbar_width), surface_round(bounds.min_y), surface_round(bounds.min_x), surface_round(bounds.max_y));
  }

  if (fill_height > 0.0f) {
    surface_filled_rect(bounds.min_x - cathook_healthbar_width + 1.0f, bounds.max_y - fill_height - 1.0f, bounds.min_x - cathook_healthbar_width + 1.0f + cathook_healthbar_fill_width, bounds.max_y - 1.0f, fill_color);
  }
}

void surface_player_bones(Player* player, RGBA color, float alpha_scale)
{
  if (player == nullptr) {
    return;
  }

  constexpr std::array<std::pair<int, int>, 16> bones = {{
    {aim_hitbox_head, aim_hitbox_spine_3}, {aim_hitbox_spine_3, aim_hitbox_spine_2},
    {aim_hitbox_spine_2, aim_hitbox_spine_1}, {aim_hitbox_spine_1, aim_hitbox_pelvis},
    {aim_hitbox_spine_3, aim_hitbox_left_upper_arm}, {aim_hitbox_left_upper_arm, aim_hitbox_left_forearm},
    {aim_hitbox_left_forearm, aim_hitbox_left_hand}, {aim_hitbox_spine_3, aim_hitbox_right_upper_arm},
    {aim_hitbox_right_upper_arm, aim_hitbox_right_forearm}, {aim_hitbox_right_forearm, aim_hitbox_right_hand},
    {aim_hitbox_pelvis, aim_hitbox_left_thigh}, {aim_hitbox_left_thigh, aim_hitbox_left_calf},
    {aim_hitbox_left_calf, aim_hitbox_left_foot}, {aim_hitbox_pelvis, aim_hitbox_right_thigh},
    {aim_hitbox_right_thigh, aim_hitbox_right_calf}, {aim_hitbox_right_calf, aim_hitbox_right_foot},
  }};

  const auto shadow = color_with_alpha(RGBA{0, 0, 0, 255}, alpha_scale);
  for (const auto& [first_hitbox, second_hitbox] : bones) {
    Vec3 first_world{}, second_world{}, first_screen{}, second_screen{};
    if (!player->get_hitbox_center(first_hitbox, &first_world) ||
        !player->get_hitbox_center(second_hitbox, &second_world) ||
        !overlay_projection::world_to_screen(first_world, &first_screen) ||
        !overlay_projection::world_to_screen(second_world, &second_screen)) {
      continue;
    }

    surface_line(first_screen.x + 1.0f, first_screen.y + 1.0f, second_screen.x + 1.0f, second_screen.y + 1.0f, shadow);
    surface_line(first_screen.x, first_screen.y, second_screen.x, second_screen.y, color);
  }
}

void draw_player_bones(ImDrawList* draw_list, Player* player, ImU32 color, float alpha_scale,
  const Vec3* origin_override = nullptr)
{
  if (draw_list == nullptr || player == nullptr) {
    return;
  }

  constexpr std::array<std::pair<int, int>, 16> bones = {{
    {aim_hitbox_head, aim_hitbox_spine_3},
    {aim_hitbox_spine_3, aim_hitbox_spine_2},
    {aim_hitbox_spine_2, aim_hitbox_spine_1},
    {aim_hitbox_spine_1, aim_hitbox_pelvis},
    {aim_hitbox_spine_3, aim_hitbox_left_upper_arm},
    {aim_hitbox_left_upper_arm, aim_hitbox_left_forearm},
    {aim_hitbox_left_forearm, aim_hitbox_left_hand},
    {aim_hitbox_spine_3, aim_hitbox_right_upper_arm},
    {aim_hitbox_right_upper_arm, aim_hitbox_right_forearm},
    {aim_hitbox_right_forearm, aim_hitbox_right_hand},
    {aim_hitbox_pelvis, aim_hitbox_left_thigh},
    {aim_hitbox_left_thigh, aim_hitbox_left_calf},
    {aim_hitbox_left_calf, aim_hitbox_left_foot},
    {aim_hitbox_pelvis, aim_hitbox_right_thigh},
    {aim_hitbox_right_thigh, aim_hitbox_right_calf},
    {aim_hitbox_right_calf, aim_hitbox_right_foot},
  }};

  const auto shadow = black_with_alpha(alpha_scale);
  const Vec3 origin_delta = origin_override != nullptr
    ? *origin_override - player->get_origin()
    : Vec3{};
  for (const auto& [first_hitbox, second_hitbox] : bones) {
    Vec3 first_world{};
    Vec3 second_world{};
    Vec3 first_screen{};
    Vec3 second_screen{};
    if (!player->get_hitbox_center(first_hitbox, &first_world) ||
        !player->get_hitbox_center(second_hitbox, &second_world)) {
      continue;
    }

    first_world = first_world + origin_delta;
    second_world = second_world + origin_delta;
    if (!overlay_projection::world_to_screen(first_world, &first_screen) ||
        !overlay_projection::world_to_screen(second_world, &second_screen)) {
      continue;
    }

    draw_list->AddLine(ImVec2(first_screen.x + 1.0f, first_screen.y + 1.0f), ImVec2(second_screen.x + 1.0f, second_screen.y + 1.0f), shadow, 3.0f);
    draw_list->AddLine(ImVec2(first_screen.x, first_screen.y), ImVec2(second_screen.x, second_screen.y), color, 1.0f);
  }
}

void draw_player_mafia_text(ImDrawList* draw_list, const esp_bounds& bounds, Player* player, Entity* player_resource, const visual_group& group, ImU32 text_color, float right_aligned_y = -1.0f)
{
  if (draw_list == nullptr || player == nullptr || player_resource == nullptr || (group.esp.draw_mask & group_esp_settings::mafia_level) == 0) {
    return;
  }

  const auto mafia_text = get_mafia_text(player, player_resource);
  if (mafia_text.empty()) {
    return;
  }

  const auto line_height = ImGui::GetTextLineHeight();
  switch (group.esp.mafia_level_position) {
  case mafia_level_position::left: {
    const auto text_size = ImGui::CalcTextSize(mafia_text.c_str());
    draw_text(draw_list, ImVec2(bounds.min_x - cathook_text_padding - text_size.x, bounds.min_y), text_color, mafia_text);
    break;
  }
  case mafia_level_position::right:
    draw_text(draw_list, ImVec2(bounds.max_x + cathook_text_padding, right_aligned_y >= 0.0f ? right_aligned_y : bounds.min_y), text_color, mafia_text);
    break;
  case mafia_level_position::under_name:
  default: {
    auto text_y = bounds.min_y - line_height - cathook_text_padding;
    if ((group.esp.draw_mask & group_esp_settings::name) != 0) {
      text_y -= line_height;
    }
    draw_text_centered(draw_list, ImVec2((bounds.min_x + bounds.max_x) * 0.5f, text_y), text_color, mafia_text);
    break;
  }
  }
}

void draw_player_class_icon(ImDrawList* draw_list, const esp_bounds& bounds, Player* player, Player* localplayer, const visual_group& group)
{
  if (draw_list == nullptr || player == nullptr || localplayer == nullptr || (group.esp.draw_mask & group_esp_settings::class_icon) == 0 || render_view == nullptr) {
    return;
  }

  if (!should_draw_player_overlay_icon(player, localplayer, (group.conditions & visual_group::condition_team) != 0)) {
    return;
  }

  auto* texture = get_head_emoji_texture();
  if (texture == nullptr && !ensure_head_emoji_atlas_loaded()) {
    return;
  }

  const auto tile_index = get_head_emoji_tile_index(player);
  if (tile_index < 0) {
    return;
  }

  auto text_lines_above = 0.0f;
  if ((group.esp.draw_mask & group_esp_settings::name) != 0) {
    text_lines_above += ImGui::GetTextLineHeight();
  }
  if ((group.esp.draw_mask & group_esp_settings::mafia_level) != 0 && group.esp.mafia_level_position == mafia_level_position::under_name) {
    text_lines_above += ImGui::GetTextLineHeight();
  }

  const auto size = std::clamp(bounds.height() * 0.22f * group.esp.class_icon_scale, 16.0f, 44.0f);
  const auto center = ImVec2(
    bounds.center().x,
    bounds.min_y - text_lines_above - cathook_text_padding - (size * 0.5f));

  const float alpha_scale = visual_groups::alpha_for_entity(player->to_entity(), group.esp.start, group.esp.end, group.esp.smooth_alpha);
  if (alpha_scale <= 0.0f) {
    return;
  }
  draw_atlas_tile(draw_list, texture, tile_index, cathook_class_icon_tile_row, center, size,
    IM_COL32(255, 255, 255, static_cast<int>(std::round(255.0f * alpha_scale))));
}

void draw_player_head_emoji(ImDrawList* draw_list, const esp_bounds& bounds, Player* player, Player* localplayer, const visual_group& group)
{
  if (draw_list == nullptr || player == nullptr || localplayer == nullptr || (group.esp.draw_mask & group_esp_settings::head_emoji) == 0 || render_view == nullptr) {
    return;
  }

  if (!should_draw_player_overlay_icon(player, localplayer, (group.conditions & visual_group::condition_team) != 0)) {
    return;
  }

  auto* texture = get_head_emoji_texture();
  if (texture == nullptr && !ensure_head_emoji_atlas_loaded()) {
    return;
  }

  const auto player_index = player->get_index();
  const auto player_key = get_esp_entity_key(player->to_entity());
  if (player_index < 0 || static_cast<std::size_t>(player_index) >= g_head_emoji_positions.size() ||
      !g_head_emoji_position_valid[static_cast<std::size_t>(player_index)] ||
      !(g_head_emoji_keys[static_cast<std::size_t>(player_index)] == player_key)) {
    auto head_position = Vec3{};
    if (!get_player_head_emoji_position(player, &head_position)) {
      return;
    }

    g_head_emoji_positions[static_cast<std::size_t>(player_index)] = head_position;
    g_head_emoji_keys[static_cast<std::size_t>(player_index)] = player_key;
    g_head_emoji_position_valid[static_cast<std::size_t>(player_index)] = true;
  }

  auto screen = Vec3{};
  if (!overlay_projection::world_to_screen(g_head_emoji_positions[static_cast<std::size_t>(player_index)], &screen)) {
    return;
  }
  const auto smoothed_screen = smooth_esp_point(player->to_entity(), ImVec2(screen.x, screen.y), esp_smoothing_point::head);

  const auto delta = get_esp_draw_origin(player->to_entity()) - get_esp_draw_origin(localplayer->to_entity());
  const auto distance = std::sqrt((delta.x * delta.x) + (delta.y * delta.y) + (delta.z * delta.z));
  (void)bounds;
  const auto distance_size = ((cathook_head_emoji_size_base * group.esp.head_emoji_scale) / (distance + 10.0f)) + cathook_head_emoji_size_bias;
  const auto size = std::clamp(distance_size, 14.0f, 48.0f);
  if (size <= 0.0f) {
    return;
  }

  const float alpha_scale = visual_groups::alpha_for_entity(player->to_entity(), group.esp.start, group.esp.end, group.esp.smooth_alpha);
  if (alpha_scale <= 0.0f) {
    return;
  }

  draw_atlas_tile(
    draw_list,
    texture,
    get_head_emoji_tile_column(group.esp.head_emoji_style),
    cathook_head_emoji_tile_row,
    smoothed_screen,
    size,
    IM_COL32(255, 255, 255, static_cast<int>(std::round(255.0f * alpha_scale))));
}

[[nodiscard]] bool should_draw_player(Player* player, Player* localplayer)
{
  if (!should_consider_player_for_esp(player, localplayer)) {
    return false;
  }

  return static_cast<bool>(visual_groups::group_for_entity(player->to_entity(), false));
}

[[nodiscard]] bool should_draw_teammate_head_emoji(Player* player, Player* localplayer)
{
  if (!should_consider_player_for_esp(player, localplayer)) {
    return false;
  }

  const visual_groups::visual_group_match group = visual_groups::group_for_entity(player->to_entity(), false);
  return group && (group->esp.draw_mask & group_esp_settings::head_emoji) != 0;
}

void draw_player_head_emoji_only(ImDrawList* draw_list, Player* player, Player* localplayer)
{
  if (draw_list == nullptr || player == nullptr || localplayer == nullptr) {
    return;
  }

  auto* entity = player->to_entity();
  if (entity == nullptr) {
    return;
  }

  auto bounds = esp_bounds{};
  if (!get_stable_entity_screen_bounds(entity, &bounds)) {
    return;
  }

  if (const visual_groups::visual_group_match group = visual_groups::group_for_entity(entity, false)) {
    draw_player_head_emoji(draw_list, smooth_esp_bounds(entity, bounds), player, localplayer, *group);
  }
}

void draw_player_esp(ImDrawList* draw_list, Player* player, Player* localplayer, Entity* player_resource, const visual_group& group)
{
  auto* entity = player != nullptr ? player->to_entity() : nullptr;
  if (draw_list == nullptr || player == nullptr || localplayer == nullptr || entity == nullptr) {
    return;
  }

  auto bounds = esp_bounds{};
  if (!get_stable_entity_screen_bounds(entity, &bounds)) {
    return;
  }
  bounds = smooth_esp_bounds(entity, bounds);

  auto base_color = esp_color_for_entity(entity, group);
  const float alpha_scale = visual_groups::alpha_for_entity(entity, group.esp.start, group.esp.end, group.esp.smooth_alpha);
  if (alpha_scale <= 0.0f) {
    return;
  }
  base_color.a *= alpha_scale;
  const auto color = to_imgui_color(base_color.to_RGBA());
  const auto text_alpha = std::clamp(static_cast<int>(std::round(255.0f * alpha_scale)), 0, 255);
  const auto neutral_text = neutral_esp_text_color(esp_color_for_entity(entity, group), alpha_scale);
  const auto green_text = IM_COL32(0, 220, 80, text_alpha);
  const auto red_text = IM_COL32(255, 50, 50, text_alpha);

  if ((group.esp.draw_mask & group_esp_settings::box) != 0) {

    draw_esp_box(draw_list, entity, bounds, group.esp.box_style, color, alpha_scale);
  }

  if ((group.esp.draw_mask & group_esp_settings::bones) != 0) {
    draw_player_bones(draw_list, player, color, alpha_scale);
  }

  if ((group.esp.draw_mask & group_esp_settings::health_bar) != 0) {
    draw_vertical_health_bar(draw_list, bounds, player->get_health(), player->get_max_health(), alpha_scale);
  }

  float left_y = bounds.min_y;
  if ((group.esp.draw_mask & group_esp_settings::health_text) != 0) {
    draw_left_line(draw_list, bounds, &left_y, with_alpha(get_health_color(player->get_health(), player->get_max_health()), alpha_scale), std::to_string(player->get_health()));
  }

  if ((group.esp.draw_mask & group_esp_settings::name) != 0) {
    const auto name = player_name(player);
    const auto name_position = ImVec2((bounds.min_x + bounds.max_x) * 0.5f, bounds.min_y - ImGui::GetTextLineHeight() - cathook_text_padding);
    if ((group.esp.draw_mask & group_esp_settings::name_background) != 0) {
      draw_text_centered_with_background(draw_list, name_position, neutral_text, name, group.esp.background_alpha, alpha_scale);
    } else {
      draw_text_centered(draw_list, name_position, neutral_text, name);
    }
  }

  float right_y = bounds.min_y;
  if ((group.esp.draw_mask & group_esp_settings::priority) != 0 && player_is_priority(player)) {
    draw_right_line(draw_list, bounds, &right_y, red_text, "PRIORITY");
  }
  if ((group.esp.draw_mask & group_esp_settings::flags) != 0) {
    if (player == aimbot::active_target_player()) draw_right_line(draw_list, bounds, &right_y, red_text, "TARGET");
    if (player->is_friend()) draw_right_line(draw_list, bounds, &right_y, green_text, "FRIEND");
    if (player->is_ignored()) draw_right_line(draw_list, bounds, &right_y, neutral_text, "IGNORED");
    if ((group.esp.draw_mask & (group_esp_settings::conditions | group_esp_settings::buffs | group_esp_settings::debuffs)) == 0) {
      if (player->is_invulnerable()) draw_right_line(draw_list, bounds, &right_y, IM_COL32(80, 180, 255, text_alpha), "UBER");
      if (player->is_crit_boosted()) draw_right_line(draw_list, bounds, &right_y, IM_COL32(255, 120, 255, text_alpha), "CRIT");
      if (player->is_scoped()) draw_right_line(draw_list, bounds, &right_y, green_text, "ZOOM");
      if (player_is_invisible_esp(player)) draw_right_line(draw_list, bounds, &right_y, IM_COL32(170, 170, 170, text_alpha), "CLOAK");
      if (player->in_cond(TF_COND_DISGUISED) || player->in_cond(TF_COND_DISGUISING)) draw_right_line(draw_list, bounds, &right_y, neutral_text, "DISGUISE");
      if (player->in_cond(TF_COND_BURNING)) draw_right_line(draw_list, bounds, &right_y, IM_COL32(255, 125, 0, text_alpha), "FIRE");
      if (player->in_cond(TF_COND_URINE)) draw_right_line(draw_list, bounds, &right_y, IM_COL32(255, 220, 40, text_alpha), "JARATE");
      if (player->in_cond(TF_COND_MAD_MILK)) draw_right_line(draw_list, bounds, &right_y, IM_COL32(220, 255, 255, text_alpha), "MILK");
      if (player->in_cond(TF_COND_BLEEDING)) draw_right_line(draw_list, bounds, &right_y, red_text, "BLEED");
      if (player->in_cond(TF_COND_MARKEDFORDEATH) || player->in_cond(TF_COND_MARKEDFORDEATH_SILENT)) draw_right_line(draw_list, bounds, &right_y, red_text, "MARKED");
      if (player->in_cond(TF_COND_STUNNED)) draw_right_line(draw_list, bounds, &right_y, neutral_text, "STUN");
    }
  }

  if ((group.esp.draw_mask & group_esp_settings::class_text) != 0) {
    draw_right_line(draw_list, bounds, &right_y, neutral_text, player_class_name(player->get_tf_class()));
  }
  if ((group.esp.draw_mask & group_esp_settings::weapon_text) != 0) {
    draw_right_line(draw_list, bounds, &right_y, neutral_text, weapon_text_for(player->get_weapon()));
  }
  if ((group.esp.draw_mask & group_esp_settings::weapon_icon) != 0) {
    draw_right_line(draw_list, bounds, &right_y, neutral_text, "Weapon " + weapon_icon_text_for(player->get_weapon()));
  }
  if ((group.esp.draw_mask & group_esp_settings::ping) != 0) {
    const int ping_value = player_ping(player_resource, player->get_index());
    if (ping_value > 0) {
      draw_right_line(draw_list, bounds, &right_y, neutral_text, std::to_string(ping_value) + "ms");
    }
  }
  if ((group.esp.draw_mask & group_esp_settings::kdr) != 0) {
    draw_right_line(draw_list, bounds, &right_y, neutral_text, player_kdr_text(player_resource, player->get_index()));
  }
  float bottom_y = bounds.max_y + 2.0f;
  if ((group.esp.draw_mask & (group_esp_settings::tags | group_esp_settings::labels)) != 0) {
    draw_bottom_center_line(draw_list, bounds, &bottom_y, with_alpha(player_tag_color(player), alpha_scale), player_tag_text(player));
  }
  if ((group.esp.draw_mask & group_esp_settings::uber) != 0 || (group.esp.draw_mask & group_esp_settings::uber_bar) != 0) {
    if (Weapon* medigun = player_medigun(player); medigun != nullptr) {
      const float charge = std::clamp(medigun->medigun_charge_level(), 0.0f, 1.0f);
      if ((group.esp.draw_mask & group_esp_settings::uber) != 0) {
        const std::string text = medigun->is_vaccinator()
          ? std::to_string(static_cast<int>(charge * 4.0f)) + "/4"
          : std::to_string(static_cast<int>(std::round(charge * 100.0f))) + "%";
        draw_right_line(draw_list, bounds, &right_y, neutral_text, text);
      }
      if ((group.esp.draw_mask & group_esp_settings::uber_bar) != 0) {
        draw_bottom_progress_bar(draw_list, bounds, &bottom_y, charge, RGBA{224, 86, 253, 255}, medigun->is_vaccinator() ? 4 : 0, alpha_scale);
      }
    }
  }
  if ((group.esp.draw_mask & group_esp_settings::steamid) != 0) {
    draw_right_line(draw_list, bounds, &right_y, neutral_text, player_steamid(player));
  }
  if ((group.esp.draw_mask & group_esp_settings::latency) != 0) {
    const int latency_ms = std::max(0, static_cast<int>(std::round(backtrack::fake_latency_seconds() * 1000.0f)));
    draw_right_line(draw_list, bounds, &right_y, neutral_text, std::to_string(latency_ms) + " ms");
  }
  if ((group.esp.draw_mask & (group_esp_settings::conditions | group_esp_settings::buffs | group_esp_settings::debuffs)) != 0) {
    const bool draw_all_conditions = (group.esp.draw_mask & group_esp_settings::conditions) != 0;
    draw_player_condition_lines(draw_list, bounds, &right_y, player, localplayer, neutral_text, green_text, red_text, text_alpha,
      draw_all_conditions,
      draw_all_conditions || (group.esp.draw_mask & group_esp_settings::buffs) != 0,
      draw_all_conditions || (group.esp.draw_mask & group_esp_settings::debuffs) != 0);
  }

  if ((group.esp.draw_mask & group_esp_settings::lag_compensation) != 0) {
    const auto* history = backtrack::records_for_player(player);
    if (history != nullptr && history->record_count > 0) {
      draw_right_line(draw_list, bounds, &right_y, red_text, "LAGCOMP");
    }
  }

  if ((group.esp.draw_mask & group_esp_settings::distance) != 0) {
    draw_bottom_center_line(draw_list, bounds, &bottom_y, neutral_text, std::to_string(static_cast<int>(entity_distance_hu(entity, localplayer))) + " HU");
  }
  if ((group.esp.draw_mask & group_esp_settings::ammo_text) != 0) {
    draw_bottom_center_line(draw_list, bounds, &bottom_y, neutral_text, ammo_text_for(player));
  }

  if (config.debug.show_active_flag_ids_of_players) {
    for (unsigned int cond_id = 0; cond_id < TF_COND_LAST; ++cond_id) {
      if (!player->in_cond(static_cast<tf_cond>(cond_id))) {
        continue;
      }

      draw_right_line(draw_list, bounds, &right_y, IM_COL32(255, 225, 255, text_alpha), std::to_string(cond_id));
    }
  }

  draw_player_class_icon(draw_list, bounds, player, localplayer, group);
  draw_player_head_emoji(draw_list, bounds, player, localplayer, group);
  draw_player_mafia_text(draw_list, bounds, player, player_resource, group, neutral_text, right_y);
}

[[nodiscard]] bool should_draw_building(Entity* entity, Player* localplayer)
{
  if (entity == nullptr || localplayer == nullptr || !entity->is_building()) {
    return false;
  }
  if (entity->is_dormant()) {
    return false;
  }

  auto* building = reinterpret_cast<Building*>(entity);
  if (building->is_carried()) {
    return false;
  }

  return static_cast<bool>(visual_groups::group_for_entity(entity, false));
}

void draw_group_entity_esp(ImDrawList* draw_list, Entity* entity, const visual_group& group)
{
  if (draw_list == nullptr || entity == nullptr) {
    return;
  }

  const float alpha_scale = visual_groups::alpha_for_entity(entity, group.esp.start, group.esp.end, group.esp.smooth_alpha);
  if (alpha_scale <= 0.0f) {
    return;
  }

  auto bounds = esp_bounds{
    .min_x = 0.0f,
    .min_y = 0.0f,
    .max_x = 0.0f,
    .max_y = 0.0f
  };
  auto text_position = ImVec2{};
  if (!get_stable_entity_screen_bounds(entity, &bounds)) {
    auto screen = Vec3{};
    if (!overlay_projection::world_to_screen(get_esp_draw_origin(entity), &screen)) {
      return;
    }
    if (!screen_point_inside_view(ImVec2(screen.x, screen.y))) {
      return;
    }
    const auto smoothed_screen = smooth_esp_point(entity, ImVec2(screen.x, screen.y), esp_smoothing_point::origin);
    bounds = esp_bounds{
      .min_x = smoothed_screen.x - 5.0f,
      .min_y = smoothed_screen.y - 5.0f,
      .max_x = smoothed_screen.x + 5.0f,
      .max_y = smoothed_screen.y + 5.0f
    };
    text_position = ImVec2(smoothed_screen.x, smoothed_screen.y + 8.0f);
  } else {
    bounds = smooth_esp_bounds(entity, bounds);
    text_position = ImVec2((bounds.min_x + bounds.max_x) * 0.5f, bounds.max_y + 2.0f);
  }

  auto color = esp_color_for_entity(entity, group);
  color.a *= alpha_scale;
  const auto imgui_color = to_imgui_color(color.to_RGBA());
  const auto neutral_text = neutral_esp_text_color(esp_color_for_entity(entity, group), alpha_scale);

  if ((group.esp.draw_mask & group_esp_settings::box) != 0) {
    draw_esp_box(draw_list, entity, bounds, group.esp.box_style, imgui_color, alpha_scale);
  }

  if ((group.esp.draw_mask & group_esp_settings::health_bar) != 0 && entity->is_building()) {
    auto* building = reinterpret_cast<Building*>(entity);
    draw_vertical_health_bar(draw_list, bounds, building->get_health(), building->get_max_health(), alpha_scale);
  }

  float left_y = bounds.min_y;
  if ((group.esp.draw_mask & group_esp_settings::health_text) != 0 && entity->is_building()) {
    auto* building = reinterpret_cast<Building*>(entity);
    draw_left_line(draw_list, bounds, &left_y, with_alpha(get_health_color(building->get_health(), building->get_max_health()), alpha_scale), std::to_string(building->get_health()));
  }

  if ((group.esp.draw_mask & group_esp_settings::name) != 0) {
    const auto label = entity_label(entity);
    if ((group.esp.draw_mask & group_esp_settings::name_background) != 0) {
      draw_text_centered_with_background(draw_list, text_position, imgui_color, label, group.esp.background_alpha, alpha_scale);
    } else {
      draw_text_centered(draw_list, text_position, imgui_color, label);
    }
  }

  float bottom_y = text_position.y + (((group.esp.draw_mask & group_esp_settings::name) != 0) ? ImGui::GetTextLineHeight() : 0.0f);
  if ((group.esp.draw_mask & group_esp_settings::distance) != 0 && entity_list != nullptr) {
    auto* localplayer = entity_list->get_localplayer();
    if (localplayer != nullptr) {
      const int distance = static_cast<int>(entity_distance_hu(entity, localplayer));
      draw_text_centered(draw_list, ImVec2(text_position.x, bottom_y), imgui_color, std::to_string(distance) + " HU");
      bottom_y += ImGui::GetTextLineHeight();
    }
  }

  if ((group.esp.draw_mask & group_esp_settings::ammo_bar) != 0 && entity->get_class_id() == class_id::SENTRY) {
    auto* sentry = reinterpret_cast<Building*>(entity);
    const int max_shells = std::max(1, sentry->get_sentry_max_ammo_shells());
    draw_bottom_progress_bar(draw_list, bounds, &bottom_y,
      static_cast<float>(std::max(0, sentry->get_sentry_ammo_shells())) / static_cast<float>(max_shells),
      RGBA{180, 180, 180, 255}, 0, alpha_scale);
    const int max_rockets = sentry->get_sentry_max_ammo_rockets();
    if (max_rockets > 0) {
      draw_bottom_progress_bar(draw_list, bounds, &bottom_y,
        static_cast<float>(std::max(0, sentry->get_sentry_ammo_rockets())) / static_cast<float>(max_rockets),
        RGBA{255, 150, 80, 255}, 0, alpha_scale);
    }
  }

  float right_y = bounds.min_y;
  if ((group.esp.draw_mask & group_esp_settings::owner) != 0) {
    if (auto* owner = owner_player_for_esp(entity); owner != nullptr && owner->to_entity() != entity) {
      draw_right_line(draw_list, bounds, &right_y, neutral_text, "Owner " + player_name(owner));
    }
  }
  if ((group.esp.draw_mask & group_esp_settings::level) != 0 && entity->is_building()) {
    auto* building = reinterpret_cast<Building*>(entity);
    draw_right_line(draw_list, bounds, &right_y, neutral_text, "Level " + std::to_string(building->get_building_level()));
  }
  if ((group.esp.draw_mask & group_esp_settings::ammo_text) != 0) {
    draw_right_line(draw_list, bounds, &right_y, neutral_text, ammo_text_for(entity));
  }
  if ((group.esp.draw_mask & group_esp_settings::weapon_icon) != 0 && entity->is_base_combat_weapon()) {
    draw_right_line(draw_list, bounds, &right_y, neutral_text,
      "Weapon " + weapon_icon_text_for(reinterpret_cast<Weapon*>(entity)));
  }
  if ((group.esp.draw_mask & group_esp_settings::intel_return_time) != 0) {
    draw_right_line(draw_list, bounds, &right_y, neutral_text, flag_status_text(entity));
  }
}

void draw_pickup_timers(ImDrawList* draw_list)
{
  if (draw_list == nullptr || global_vars == nullptr) {
    return;
  }

  const visual_group* timer_group = first_pickup_timer_group();
  if (timer_group == nullptr) {
    return;
  }

  auto timer_color = timer_group->esp.override_color ? timer_group->esp.color : timer_group->color;
  const auto imgui_color = to_imgui_color(timer_color.to_RGBA());

  for (size_t index = 0; index < pickup_item_cache.size();) {
    const auto& pickup_item = pickup_item_cache[index];
    const auto time_delta = pickup_item.time - global_vars->curtime;
    if (time_delta < 0.0f) {
      pickup_item_cache.erase(pickup_item_cache.begin() + static_cast<long long>(index));
      continue;
    }

    auto screen = Vec3{};
    auto location = pickup_item.location;
    if (overlay_projection::world_to_screen(location, &screen)) {
      auto time_text = std::to_string(time_delta);
      if (time_text.size() > 4) {
        time_text.resize(4);
      }
      draw_text_centered(draw_list, ImVec2(screen.x, screen.y), imgui_color, time_text);
    }

    ++index;
  }
}

void reset_head_emoji_runtime_state()
{
  g_head_emoji_positions.fill(Vec3{});
  g_head_emoji_position_valid.fill(false);
  g_head_emoji_keys.fill(esp_entity_key{});
  g_next_head_emoji_cache_time = 0.0f;
  g_next_head_emoji_manual_cache_time = 0.0f;
}

}

void reset_esp_runtime_state()
{
  reset_head_emoji_runtime_state();
  g_esp_smoothing_states.clear();
  g_esp_bounds_cache.clear();
  g_esp_smoothing_frame = 0;
  g_esp_level_name.clear();
  g_esp_was_in_game = false;
}

void update_player_head_emoji_cache()
{
  if (!visual_groups::groups_need_head_emojis() || engine == nullptr || entity_list == nullptr || global_vars == nullptr ||
      !engine->is_in_game()) {
    reset_head_emoji_runtime_state();
    return;
  }

  if (global_vars->realtime < g_next_head_emoji_cache_time) {
    return;
  }
  g_next_head_emoji_cache_time = global_vars->realtime + cathook_head_emoji_cache_interval;
  g_head_emoji_position_valid.fill(false);

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr) {
    return;
  }

  const auto max_entities = entity_list->get_max_entities();
  for (unsigned int index = 1; index <= max_entities && index < g_head_emoji_positions.size(); ++index) {
    auto* player = entity_list->player_from_index(index);
    if (!should_draw_player(player, localplayer) && !should_draw_teammate_head_emoji(player, localplayer)) {
      continue;
    }
    const visual_groups::visual_group_match group = player != nullptr ? visual_groups::group_for_entity(player->to_entity(), false) : visual_groups::visual_group_match{};
    if (!group || (group->esp.draw_mask & group_esp_settings::head_emoji) == 0) {
      continue;
    }
    if (!should_draw_player_overlay_icon(player, localplayer, (group->conditions & visual_group::condition_team) != 0)) {
      continue;
    }

    auto head_position = Vec3{};
    if (!get_player_head_emoji_position(player, &head_position)) {
      continue;
    }

    g_head_emoji_positions[index] = head_position;
    g_head_emoji_keys[index] = get_esp_entity_key(player->to_entity());
    g_head_emoji_position_valid[index] = true;
  }
}

void refresh_player_head_emoji_cache_for_draw()
{
  if (global_vars == nullptr || global_vars->realtime < g_next_head_emoji_manual_cache_time) {
    return;
  }

  g_next_head_emoji_manual_cache_time = global_vars->realtime + cathook_head_emoji_manual_cache_interval;
  g_next_head_emoji_cache_time = 0.0f;
  update_player_head_emoji_cache();
}

void draw_players_imgui()
{
  if (!visual_groups::groups_need_screen_overlay() || engine == nullptr || !engine->is_in_game() || render_view == nullptr || entity_list == nullptr) {
    reset_esp_runtime_state();
    return;
  }

  const auto* level_name = engine->get_level_name();
  const auto current_level_name = level_name != nullptr ? std::string(level_name) : std::string{};
  if (!g_esp_was_in_game || g_esp_level_name != current_level_name) {
    reset_esp_runtime_state();
    g_esp_level_name = current_level_name;
    g_esp_was_in_game = true;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr) {
    reset_esp_runtime_state();
    return;
  }

  auto* draw_list = ImGui::GetWindowDrawList();
  if (draw_list == nullptr) {
    return;
  }

  if (!overlay_projection::begin_frame()) {
    reset_esp_runtime_state();
    return;
  }

  begin_esp_smoothing_frame();

  refresh_player_head_emoji_cache_for_draw();

  auto* player_resource = get_player_resource_entity();

  for (unsigned int index = 1; index <= entity_list->get_max_entities(); ++index) {
    auto* entity = entity_list->entity_from_index(index);
    if (entity == nullptr) {
      continue;
    }

    const visual_groups::visual_group_match group = visual_groups::group_for_entity(entity, false);
    if (!group) {
      continue;
    }

    if (entity->get_class_id() == class_id::PLAYER) {
      auto* player = reinterpret_cast<Player*>(entity);
      if (should_draw_player(player, localplayer)) {
        draw_offscreen_arrow(draw_list, entity, localplayer, *group);
        draw_player_sightline(draw_list, player, *group);
        draw_player_esp(draw_list, player, localplayer, player_resource, *group);
      } else if (should_draw_teammate_head_emoji(player, localplayer)) {
        draw_player_head_emoji_only(draw_list, player, localplayer);
      }
      continue;
    }

    draw_offscreen_arrow(draw_list, entity, localplayer, *group);
    draw_entity_trajectory(draw_list, entity, *group);
    draw_group_entity_esp(draw_list, entity, *group);
  }

  draw_pickup_timers(draw_list);
  cleanup_esp_smoothing_states();
  cleanup_esp_bounds_cache();
}

void draw_backtrack_visualizer_imgui()
{
  const bool group_backtrack_enabled = any_backtrack_group_enabled();
  if (!backtrack::is_enabled() ||
      (!config.backtrack.visualizer && !group_backtrack_enabled) ||
      engine == nullptr ||
      entity_list == nullptr ||
      !engine->is_in_game() ||
      !overlay_projection::begin_frame()) {
    return;
  }

  auto* draw_list = ImGui::GetWindowDrawList();
  auto* localplayer = entity_list->get_localplayer();
  if (draw_list == nullptr || localplayer == nullptr) {
    return;
  }

  const int max_draw_ticks = std::clamp(config.backtrack.visualizer_ticks, 1, backtrack::max_records);
  for (unsigned int index = 1; index <= entity_list->get_max_entities(); ++index) {
    auto* player = entity_list->player_from_index(index);
    if (player == nullptr || player == localplayer) {
      continue;
    }

    const visual_groups::visual_group_match group = visual_groups::group_for_entity(player->to_entity(), false);
    const bool group_draws_backtrack = group && (group->backtrack & visual_group::backtrack_enabled) != 0;
    if (!config.backtrack.visualizer && !group_draws_backtrack) {
      continue;
    }
    if (!group_draws_backtrack && (player->is_friend() || player->is_ignored())) {
      continue;
    }

    const auto* history = backtrack::records_for_player(player);
    if (history == nullptr) {
      continue;
    }

    RGBA base_color{255, 128, 0, 255};
    uint32_t backtrack_flags = visual_group::backtrack_enabled | visual_group::backtrack_always;
    if (group_draws_backtrack) {
      base_color = esp_color_for_entity(player->to_entity(), *group).to_RGBA();
      backtrack_flags = group->backtrack;
    }

    int newest_valid_record = -1;
    int oldest_valid_record = -1;
    for (int record_index = 0; record_index < history->record_count; ++record_index) {
      const auto& record = history->records[record_index];
      if (!backtrack::is_record_valid(record, player) || record.hitbox_count <= 0 || !record.hitboxes[0].valid) {
        continue;
      }

      if (newest_valid_record < 0) {
        newest_valid_record = record_index;
      }
      oldest_valid_record = record_index;
    }

    Vec3 previous_screen{};
    bool previous_valid = false;
    int drawn_records = 0;
    const bool draw_always = (backtrack_flags & visual_group::backtrack_always) != 0 ||
      (backtrack_flags & (visual_group::backtrack_last | visual_group::backtrack_first)) ==
        (visual_group::backtrack_last | visual_group::backtrack_first);

    for (int record_index = 0; record_index < history->record_count && drawn_records < max_draw_ticks; ++record_index) {
      const auto& record = history->records[record_index];
      if (!backtrack::is_record_valid(record, player) || record.hitbox_count <= 0 || !record.hitboxes[0].valid) {
        previous_valid = false;
        continue;
      }
      if (!draw_always) {
        const bool draw_first = (backtrack_flags & visual_group::backtrack_first) != 0;
        const bool draw_newest = !draw_first || (backtrack_flags & visual_group::backtrack_last) != 0
          ? record_index == newest_valid_record
          : false;
        const bool draw_oldest = draw_first && (backtrack_flags & visual_group::backtrack_last) == 0 && record_index == oldest_valid_record;
        if (!draw_newest && !draw_oldest) {
          continue;
        }
      }

      const float age_fraction = static_cast<float>(drawn_records) / static_cast<float>(std::max(max_draw_ticks, 1));
      const int alpha_byte = std::clamp(static_cast<int>((1.0f - age_fraction) * 220.0f), 35, 220);
      const ImU32 color = IM_COL32(base_color.r, base_color.g, base_color.b, alpha_byte);
      ++drawn_records;

      auto bounds = esp_bounds{};
      switch (config.backtrack.visualizer_mode) {
      case backtrack_config::visualizer_style::boxes:
        if (get_backtrack_record_screen_bounds(record, &bounds)) {
          draw_corner_box(draw_list, bounds, color, static_cast<float>(alpha_byte) / 255.0f);
        }
        break;
      case backtrack_config::visualizer_style::projected_boxes: {
        auto box = projected_box{};
        if (get_backtrack_record_projected_box(record, &box)) {
          draw_projected_box(draw_list, box, color, static_cast<float>(alpha_byte) / 255.0f);
        }
        break;
      }
      case backtrack_config::visualizer_style::trail: {
        Vec3 screen{};
        if (overlay_projection::world_to_screen(record.hitboxes[0].center, &screen)) {
          if (previous_valid) {
            draw_list->AddLine(
              ImVec2(previous_screen.x + 1.0f, previous_screen.y + 1.0f),
              ImVec2(screen.x + 1.0f, screen.y + 1.0f),
              IM_COL32(0, 0, 0, alpha_byte),
              4.0f);
            draw_list->AddLine(ImVec2(previous_screen.x, previous_screen.y), ImVec2(screen.x, screen.y), color, 2.0f);
          }
          draw_list->AddCircleFilled(ImVec2(screen.x, screen.y), 2.5f, color, 12);
          previous_screen = screen;
          previous_valid = true;
        } else {
          previous_valid = false;
        }
        break;
      }
      case backtrack_config::visualizer_style::pulse: {
        Vec3 screen{};
        if (overlay_projection::world_to_screen(record.hitboxes[0].center, &screen)) {
          const float realtime = global_vars != nullptr ? global_vars->realtime : ImGui::GetTime();
          const float pulse = 0.5f + (0.5f * std::sin((realtime * 7.0f) + (age_fraction * 5.0f)));
          const float radius = 4.0f + (pulse * 7.0f);
          draw_list->AddCircle(ImVec2(screen.x, screen.y), radius + 1.0f, IM_COL32(0, 0, 0, alpha_byte), 24, 3.0f);
          draw_list->AddCircle(ImVec2(screen.x, screen.y), radius, color, 24, 1.5f);
          draw_list->AddCircleFilled(ImVec2(screen.x, screen.y), 2.5f, color, 12);
        }
        break;
      }
      case backtrack_config::visualizer_style::points:
      default: {
        Vec3 screen{};
        if (overlay_projection::world_to_screen(record.hitboxes[0].center, &screen)) {
          draw_list->AddRectFilled(
            ImVec2(screen.x - 3.0f, screen.y - 3.0f),
            ImVec2(screen.x + 3.0f, screen.y + 3.0f),
            IM_COL32(0, 0, 0, alpha_byte));
          draw_list->AddRectFilled(
            ImVec2(screen.x - 2.0f, screen.y - 2.0f),
            ImVec2(screen.x + 2.0f, screen.y + 2.0f),
            color);
        }
        break;
      }
      }
    }
  }

}

void draw_aimbot_fov_imgui()
{
  if (engine == nullptr || entity_list == nullptr || !engine->is_in_game()) {
    return;
  }
  if (!config.aimbot.draw_fov || config.aimbot.fov <= 0.0f) {
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive()) {
    return;
  }
  const bool projection_updated = overlay_projection::begin_frame();
  (void)projection_updated;

  auto* draw_list = ImGui::GetWindowDrawList();
  if (draw_list == nullptr) {
    return;
  }

  auto screen_size = engine->get_screen_size();
  const ImVec2 display_size = ImGui::GetIO().DisplaySize;
  if (display_size.x > 0.0f && display_size.y > 0.0f) {
    screen_size.x = static_cast<int>(display_size.x);
    screen_size.y = static_cast<int>(display_size.y);
  }
  if (screen_size.x <= 0 || screen_size.y <= 0) {
    screen_size.x = static_cast<int>(overlay_projection::state.screen_width);
    screen_size.y = static_cast<int>(overlay_projection::state.screen_height);
  }
  if (screen_size.x <= 0 || screen_size.y <= 0) {
    return;
  }

  auto local_fov = static_cast<float>(localplayer->get_fov());
  if (!std::isfinite(local_fov) || local_fov <= 1.0f) {
    local_fov = static_cast<float>(localplayer->get_default_fov());
  }
  if (config.visuals.override_fov && !localplayer->is_scoped()) {
    local_fov = config.visuals.custom_fov;
  }
  if (config.visuals.removals.zoom) {
    local_fov = static_cast<float>(localplayer->get_default_fov());
  }
  if (config.visuals.override_fov && config.visuals.removals.zoom) {
    local_fov = config.visuals.custom_fov;
  }

  const auto denominator = std::tan((local_fov / 2.0f) / 180.0f * static_cast<float>(M_PI));
  if (!std::isfinite(denominator) || denominator <= 0.0f) {
    return;
  }

  const float drawable_fov = std::clamp(config.aimbot.fov, 0.0f, 89.0f);
  auto radius = (std::tan(drawable_fov / 180.0f * static_cast<float>(M_PI)) /
    denominator *
    (static_cast<float>(screen_size.x) / 2.0f)) / 1.35f;
  if (!std::isfinite(radius) || radius <= 0.0f) {
    return;
  }

  static float smoothed_radius = 0.0f;
  static int smoothed_screen_width = 0;
  static int smoothed_screen_height = 0;
  const bool reset_smoothing = smoothed_radius <= 0.0f ||
      smoothed_screen_width != screen_size.x ||
      smoothed_screen_height != screen_size.y;
  if (reset_smoothing) {
    smoothed_radius = radius;
    smoothed_screen_width = screen_size.x;
    smoothed_screen_height = screen_size.y;
  } else {
    const auto delta_seconds = ImGui::GetIO().DeltaTime;
    const auto blend = std::clamp(delta_seconds > 0.0f ? delta_seconds * 20.0f : 1.0f, 0.0f, 1.0f);
    smoothed_radius += (radius - smoothed_radius) * blend;
  }

  auto center = ImVec2(static_cast<float>(screen_size.x) * 0.5f, static_cast<float>(screen_size.y) * 0.5f);
  draw_list->AddCircle(center, smoothed_radius, IM_COL32(0, 0, 0, 180), 64, 3.0f);
  draw_list->AddCircle(center, smoothed_radius, IM_COL32(255, 255, 255, 255), 64, 1.5f);
}

void draw_anti_aim_indicator_imgui()
{
  if (!config.misc.exploits.anti_aim_indicator) {
    return;
  }

  if (engine == nullptr || entity_list == nullptr || !engine->is_in_game()) {
    return;
  }

  if (!anti_aim::settings_enabled()) {
    return;
  }

  const float alpha_scale = anti_aim::has_visual_angles() ? 1.0f : 0.45f;

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive()) {
    return;
  }

  if (!overlay_projection::begin_frame()) {
    return;
  }

  auto* draw_list = ImGui::GetBackgroundDrawList();
  if (draw_list == nullptr) {
    return;
  }

  const Vec3 origin = localplayer->get_origin();

  Vec3 origin_screen{};
  const bool origin_on_screen = overlay_projection::world_to_screen(origin, &origin_screen);

  const float distance = std::clamp(config.misc.exploits.anti_aim_indicator_distance, 8.0f, 512.0f);

  const auto draw_marker = [&](float yaw, RGBA_float color, const char* label) {
    const float radians = yaw * pideg;
    const Vec3 end{
      origin.x + std::cos(radians) * distance,
      origin.y + std::sin(radians) * distance,
      origin.z
    };

    Vec3 end_screen{};
    if (!overlay_projection::world_to_screen(end, &end_screen)) {
      return;
    }

    color.a *= alpha_scale;
    const ImU32 imgui_color = to_imgui_color(color.to_RGBA());
    if (config.misc.exploits.anti_aim_indicator_lines && origin_on_screen) {
      draw_list->AddLine(
        ImVec2(origin_screen.x, origin_screen.y),
        ImVec2(end_screen.x, end_screen.y),
        imgui_color,
        1.5f);
    }

    draw_text_centered(draw_list, ImVec2(end_screen.x, end_screen.y), imgui_color, label);
  };
  
  draw_marker(anti_aim::fake_angles().y, config.misc.exploits.anti_aim_indicator_fake_color, "FAKE");
  draw_marker(anti_aim::real_angles().y, config.misc.exploits.anti_aim_indicator_real_color, "REAL");
}

void draw_thirdperson_crosshair_imgui()
{
  thirdperson::draw_crosshair_imgui();
}

bool atlas_texture_ready()
{
  if (get_head_emoji_texture() != nullptr) {
    return true;
  }

  return ensure_head_emoji_atlas_loaded() &&
    !g_head_emoji_atlas.pixels.empty() &&
    g_head_emoji_atlas.width > 0 &&
    g_head_emoji_atlas.height > 0;
}

void draw_shared_atlas_tile(
  ImDrawList* draw_list,
  const int tile_column,
  const int tile_row,
  const ImVec2& center,
  const float size,
  const ImU32 tint)
{
  draw_atlas_tile(draw_list, get_head_emoji_texture(), tile_column, tile_row, center, size, tint);
}
