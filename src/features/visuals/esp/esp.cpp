/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/visuals/esp/esp.cpp
 |  Y  |   autor: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "imgui/dearimgui.hpp"
#include "imgui/imgui_internal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/logger.hpp"
#include "core/entity_cache.hpp"

#include "features/combat/aimbot/aimbot.hpp"
#include "features/combat/aimbot/proj_aim.hpp"
#include "features/menu/config.hpp"
#include "features/visuals/thirdperson.hpp"
#include "features/visuals/overlay_projection.hpp"

#include "games/tf2/sdk/entities/building.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/aim_hitboxes.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/interfaces/render_view.hpp"

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
constexpr float esp_smoothing_snap_distance = 180.0f;
constexpr float esp_smoothing_snap_scale = 1.75f;
constexpr unsigned int esp_smoothing_stale_frames = 2;
constexpr uintptr_t player_resource_score_offset = 0xC80;
constexpr uintptr_t player_resource_deaths_offset = 0xE18;
constexpr uintptr_t tf_player_resource_damage_offset = 0x2360;

struct head_emoji_texture_state
{
  std::unique_ptr<ImTextureData> texture{};
  std::filesystem::path loaded_path{};
  ImGuiContext* context = nullptr;
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
  ImVec2 origin_screen{};
  ImVec2 head_screen{};
  std::array<ImVec2, 8> projected_points{};
  bool bounds_valid = false;
  bool origin_screen_valid = false;
  bool head_screen_valid = false;
  bool projected_points_valid = false;
  unsigned int last_seen_frame = 0;
};

std::array<esp_entity_key, cathook_head_emoji_cache_size> g_head_emoji_keys{};
float g_next_head_emoji_cache_time = 0.0f;
float g_next_head_emoji_manual_cache_time = 0.0f;
std::unordered_map<esp_entity_key, esp_smoothing_state, esp_entity_key_hash> g_esp_smoothing_states{};
unsigned int g_esp_smoothing_frame = 0;
std::string g_esp_level_name{};
bool g_esp_was_in_game = false;

[[nodiscard]] bool get_entity_screen_bounds(Entity* entity, esp_bounds* bounds);
[[nodiscard]] bool get_entity_projected_box(Entity* entity, projected_box* box);
[[nodiscard]] Vec3 get_esp_draw_origin(Entity* entity);

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
  if (!config.esp.lerp) {
    return 1.0f;
  }

  auto frame_time = 1.0f / 60.0f;
  if (global_vars != nullptr && global_vars->frametime > 0.0f) {
    frame_time = global_vars->frametime;
  } else if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().DeltaTime > 0.0f) {
    frame_time = ImGui::GetIO().DeltaTime;
  }

  const auto speed = std::max(config.esp.lerp_speed, 1.0f);
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
  if (!config.esp.lerp) {
    g_esp_smoothing_states.clear();
    return;
  }

  ++g_esp_smoothing_frame;
  if (g_esp_smoothing_frame == 0) {
    g_esp_smoothing_states.clear();
    g_esp_smoothing_frame = 1;
  }
}

void cleanup_esp_smoothing_states()
{
  if (!config.esp.lerp) {
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

[[nodiscard]] esp_bounds smooth_esp_bounds(Entity* entity, const esp_bounds& target_bounds)
{
  const auto key = get_esp_entity_key(entity);
  if (!config.esp.lerp || !is_valid_esp_entity_key(key)) {
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
  if (!config.esp.lerp || !is_valid_esp_entity_key(key)) {
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
  if (!config.esp.lerp || !is_valid_esp_entity_key(key)) {
    return;
  }

  auto& state = g_esp_smoothing_states[key];
  state.last_seen_frame = g_esp_smoothing_frame;

  const auto amount = esp_lerp_amount();
  if (!state.projected_points_valid || amount >= 1.0f || should_snap_bounds(state.bounds, box->bounds)) {
    for (size_t index = 0; index < box->screen_points.size(); ++index) {
      state.projected_points[index] = ImVec2(box->screen_points[index].x, box->screen_points[index].y);
    }
    state.bounds = box->bounds;
    state.bounds_valid = true;
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
  state.bounds = box->bounds;
  state.bounds_valid = true;
}

[[nodiscard]] bool get_stable_entity_screen_bounds(Entity* entity, esp_bounds* bounds)
{
  if (entity == nullptr || bounds == nullptr) {
    return false;
  }

  auto current_bounds = esp_bounds{};
  if (!entity->is_dormant() && get_entity_screen_bounds(entity, &current_bounds) && is_reasonable_screen_bounds(current_bounds)) {
    *bounds = current_bounds;
    return true;
  }

  return false;
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

[[nodiscard]] ImTextureData* get_head_emoji_texture()
{
  auto* current_context = ImGui::GetCurrentContext();
  if (current_context == nullptr) {
    return nullptr;
  }

  if (g_head_emoji_texture.context != nullptr && g_head_emoji_texture.context != current_context) {
    reset_head_emoji_texture();
  }

  const auto atlas_path = resolve_head_emoji_atlas_path();
  if (atlas_path.empty()) {
    reset_head_emoji_texture();
    return nullptr;
  }

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

  const auto file_bytes = read_file_bytes(atlas_path);
  if (file_bytes.empty()) {
    return nullptr;
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
    return nullptr;
  }

  auto texture = std::make_unique<ImTextureData>();
  texture->Create(ImTextureFormat_RGBA32, image_width, image_height);
  std::memcpy(texture->Pixels, pixels, texture->GetSizeInBytes());
  stbi_image_free(pixels);
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

[[nodiscard]] RGBA player_esp_color(Player* player, Player* localplayer)
{
  auto color = config.esp.player.enemy_color.to_RGBA();
  if (player->get_team() == localplayer->get_team()) {
    color = config.esp.player.team_color.to_RGBA();
  }
  if (config.esp.player.friends && player->is_friend()) {
    color = config.esp.player.friend_color.to_RGBA();
  }

  return color;
}

[[nodiscard]] Vec3 get_esp_draw_origin(Entity* entity)
{
  if (entity == nullptr) {
    return {};
  }

  return entity->get_collision_origin();
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

  return player->get_team() != localplayer->get_team() || include_teammates || player->is_friend();
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

[[nodiscard]] int get_head_emoji_tile_column()
{
  const auto style = std::clamp(config.esp.player.head_emoji_style, 0, cathook_head_emoji_style_count - 1);
  return cathook_head_emoji_first_tile_column + style;
}

void draw_atlas_tile(
  ImDrawList* draw_list,
  ImTextureData* texture,
  int tile_column,
  int tile_row,
  const ImVec2& center,
  float size)
{
  if (draw_list == nullptr || texture == nullptr || tile_column < 0 || tile_row < 0 || size <= 0.0f ||
      texture->Width <= 0 || texture->Height <= 0) {
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
    IM_COL32_WHITE);
}

[[nodiscard]] bool get_entity_screen_bounds(Entity* entity, esp_bounds* bounds)
{
  if (entity == nullptr || bounds == nullptr || render_view == nullptr) {
    return false;
  }

  const auto origin = get_esp_draw_origin(entity);
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

[[nodiscard]] bool get_entity_projected_box(Entity* entity, projected_box* box)
{
  if (entity == nullptr || box == nullptr || render_view == nullptr) {
    return false;
  }

  const auto origin = get_esp_draw_origin(entity);
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

void draw_esp_box(ImDrawList* draw_list, Entity* entity, const esp_bounds& bounds, Esp::box_type box_style, ImU32 color, float alpha_scale)
{
  switch (box_style) {
  case Esp::box_type::OUTLINE:
    draw_outline_box(draw_list, bounds, color, alpha_scale);
    break;
  case Esp::box_type::CORNER:
    draw_corner_box(draw_list, bounds, color, alpha_scale);
    break;
  case Esp::box_type::FILLED:
    draw_filled_box(draw_list, bounds, color, alpha_scale);
    break;
  case Esp::box_type::ROUNDED:
    draw_rounded_box(draw_list, bounds, color, alpha_scale);
    break;
  case Esp::box_type::PROJECTED: {
    auto box = projected_box{};
    if (entity != nullptr && get_entity_projected_box(entity, &box)) {
      smooth_projected_box(entity, &box);
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
  draw_list->AddText(ImVec2(text_pos.x + 1.0f, text_pos.y + 1.0f), IM_COL32(0, 0, 0, 255), text.c_str());
  draw_list->AddText(text_pos, color, text.c_str());
}

void draw_text(ImDrawList* draw_list, const ImVec2& position, ImU32 color, const std::string& text)
{
  if (draw_list == nullptr || text.empty()) {
    return;
  }

  draw_list->AddText(ImVec2(position.x + 1.0f, position.y + 1.0f), IM_COL32(0, 0, 0, 255), text.c_str());
  draw_list->AddText(position, color, text.c_str());
}

void draw_player_mafia_text(ImDrawList* draw_list, const esp_bounds& bounds, Player* player, Entity* player_resource, float right_aligned_y = -1.0f)
{
  if (draw_list == nullptr || player == nullptr || player_resource == nullptr || !config.esp.player.mafia_level) {
    return;
  }

  const auto mafia_text = get_mafia_text(player, player_resource);
  if (mafia_text.empty()) {
    return;
  }

  constexpr auto mafia_color = IM_COL32(255, 255, 255, 255);
  const auto line_height = ImGui::GetTextLineHeight();
  switch (config.esp.player.mafia_level_position) {
  case Esp::Player::mafia_position::LEFT: {
    const auto text_size = ImGui::CalcTextSize(mafia_text.c_str());
    draw_text(draw_list, ImVec2(bounds.min_x - cathook_text_padding - text_size.x, bounds.min_y), mafia_color, mafia_text);
    break;
  }
  case Esp::Player::mafia_position::RIGHT:
    draw_text(draw_list, ImVec2(bounds.max_x + cathook_text_padding, right_aligned_y >= 0.0f ? right_aligned_y : bounds.min_y), mafia_color, mafia_text);
    break;
  case Esp::Player::mafia_position::UNDER_NAME:
  default: {
    auto text_y = bounds.min_y - line_height - cathook_text_padding;
    if (config.esp.player.name) {
      text_y -= line_height;
    }
    draw_text_centered(draw_list, ImVec2((bounds.min_x + bounds.max_x) * 0.5f, text_y), mafia_color, mafia_text);
    break;
  }
  }
}

void draw_player_class_icon(ImDrawList* draw_list, const esp_bounds& bounds, Player* player, Player* localplayer)
{
  if (draw_list == nullptr || player == nullptr || localplayer == nullptr || !config.esp.player.class_icon || render_view == nullptr) {
    return;
  }

  if (!should_draw_player_overlay_icon(player, localplayer, config.esp.player.class_icon_teammates)) {
    return;
  }

  auto* texture = get_head_emoji_texture();
  if (texture == nullptr) {
    return;
  }

  const auto tile_index = get_head_emoji_tile_index(player);
  if (tile_index < 0) {
    return;
  }

  auto text_lines_above = 0.0f;
  if (config.esp.player.name) {
    text_lines_above += ImGui::GetTextLineHeight();
  }
  if (config.esp.player.mafia_level && config.esp.player.mafia_level_position == Esp::Player::mafia_position::UNDER_NAME) {
    text_lines_above += ImGui::GetTextLineHeight();
  }

  const auto size = std::clamp(bounds.height() * 0.22f * config.esp.player.class_icon_scale, 16.0f, 44.0f);
  const auto center = ImVec2(
    bounds.center().x,
    bounds.min_y - text_lines_above - cathook_text_padding - (size * 0.5f));

  draw_atlas_tile(draw_list, texture, tile_index, cathook_class_icon_tile_row, center, size);
}

void draw_player_head_emoji(ImDrawList* draw_list, const esp_bounds& bounds, Player* player, Player* localplayer)
{
  if (draw_list == nullptr || player == nullptr || localplayer == nullptr || !config.esp.player.head_emoji || render_view == nullptr) {
    return;
  }

  if (!should_draw_player_overlay_icon(player, localplayer, config.esp.player.head_emoji_teammates)) {
    return;
  }

  auto* texture = get_head_emoji_texture();
  if (texture == nullptr) {
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
  const auto distance_size = ((cathook_head_emoji_size_base * config.esp.player.head_emoji_scale) / (distance + 10.0f)) + cathook_head_emoji_size_bias;
  const auto bounds_size = bounds.height() * 0.22f * config.esp.player.head_emoji_scale;
  const auto max_size = std::max(12.0f, bounds.height() * 0.45f);
  const auto size = std::clamp(std::max(bounds_size, distance_size * 0.75f), 12.0f, max_size);
  if (size <= 0.0f) {
    return;
  }

  draw_atlas_tile(
    draw_list,
    texture,
    get_head_emoji_tile_column(),
    cathook_head_emoji_tile_row,
    smoothed_screen,
    size);
}

[[nodiscard]] bool should_draw_player(Player* player, Player* localplayer)
{
  if (player == nullptr || localplayer == nullptr) {
    return false;
  }
  if (player == localplayer || !player->is_alive()) {
    return false;
  }
  if (player->is_dormant()) {
    return false;
  }
  if (player->get_team() != localplayer->get_team() && !config.esp.player.enemy && !player->is_friend()) {
    return false;
  }
  if (player->get_team() == localplayer->get_team() && !config.esp.player.team && !player->is_friend()) {
    return false;
  }
  if (player->is_friend() && !config.esp.player.friends && (!config.esp.player.team && player->get_team() == localplayer->get_team())) {
    return false;
  }

  return true;
}

void draw_player_esp(ImDrawList* draw_list, Player* player, Player* localplayer, Entity* player_resource)
{
  auto bounds = esp_bounds{};
  if (!get_stable_entity_screen_bounds(player->to_entity(), &bounds)) {
    return;
  }
  bounds = smooth_esp_bounds(player->to_entity(), bounds);

  const auto base_color = player_esp_color(player, localplayer);
  const auto color = to_imgui_color(base_color);
  constexpr float alpha_scale = 1.0f;

  if (config.esp.player.box) {
    draw_esp_box(draw_list, player->to_entity(), bounds, config.esp.player.box_style, color, alpha_scale);
  }

  if (config.esp.player.health_bar) {
    draw_vertical_health_bar(draw_list, bounds, player->get_health(), player->get_max_health(), alpha_scale);
  }

  if (config.esp.player.name) {
    wchar_t wide_name[32]{};
    player->get_player_name(wide_name);
    const auto name = wide_to_utf8(wide_name);
    draw_text_centered(draw_list, ImVec2((bounds.min_x + bounds.max_x) * 0.5f, bounds.min_y - ImGui::GetTextLineHeight() - cathook_text_padding), IM_COL32(255, 255, 255, 255), name);
  }

  float flag_y = bounds.min_y;
  const auto flag_x = bounds.max_x + cathook_text_padding;
  if (config.esp.player.flags.target_indicator && player == target_player) {
    draw_text(draw_list, ImVec2(flag_x, flag_y), IM_COL32(255, 0, 0, 255), "TARGET");
    flag_y += ImGui::GetTextLineHeight();
  }
  if (config.esp.player.flags.friend_indicator && player->is_friend()) {
    draw_text(draw_list, ImVec2(flag_x, flag_y), IM_COL32(0, 220, 80, 255), "FRIEND");
    flag_y += ImGui::GetTextLineHeight();
  }
  if (config.esp.player.flags.scoped_indicator && player->is_scoped()) {
    draw_text(draw_list, ImVec2(flag_x, flag_y), IM_COL32(0, 220, 80, 255), "ZOOM");
    flag_y += ImGui::GetTextLineHeight();
  }

  if (config.debug.show_active_flag_ids_of_players) {
    for (unsigned int cond_id = 0; cond_id < TF_COND_LAST; ++cond_id) {
      if (!player->in_cond(static_cast<tf_cond>(cond_id))) {
        continue;
      }

      draw_text(draw_list, ImVec2(flag_x, flag_y), IM_COL32(255, 225, 255, 255), std::to_string(cond_id));
      flag_y += ImGui::GetTextLineHeight();
    }
  }

  draw_player_class_icon(draw_list, bounds, player, localplayer);
  draw_player_head_emoji(draw_list, bounds, player, localplayer);
  draw_player_mafia_text(draw_list, bounds, player, player_resource, flag_y);
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

  if (entity->get_team() == localplayer->get_team() && !config.esp.buildings.team) {
    return false;
  }

  return true;
}

void draw_building_esp(ImDrawList* draw_list, Entity* entity, Player* localplayer)
{
  auto bounds = esp_bounds{};
  if (!get_stable_entity_screen_bounds(entity, &bounds)) {
    return;
  }
  bounds = smooth_esp_bounds(entity, bounds);

  auto* building = reinterpret_cast<Building*>(entity);
  auto color = config.esp.player.enemy_color.to_RGBA();
  if (entity->get_team() == localplayer->get_team()) {
    color = config.esp.player.team_color.to_RGBA();
  }

  if (config.esp.buildings.box) {
    draw_esp_box(draw_list, entity, bounds, config.esp.buildings.box_style, to_imgui_color(color), 1.0f);
  }

  if (config.esp.buildings.health_bar) {
    draw_vertical_health_bar(draw_list, bounds, building->get_health(), building->get_max_health(), 1.0f);
  }

  if (!config.esp.buildings.name) {
    return;
  }

  std::string label{};
  switch (entity->get_class_id()) {
  case class_id::DISPENSER:
    label = "DISPENSER";
    break;
  case class_id::SENTRY:
    label = "SENTRY";
    break;
  case class_id::TELEPORTER:
    label = "TELEPORTER";
    break;
  default:
    break;
  }

  draw_text_centered(draw_list, ImVec2((bounds.min_x + bounds.max_x) * 0.5f, bounds.max_y + 2.0f), IM_COL32(255, 255, 255, 255), label);
}

void draw_pickup_esp(ImDrawList* draw_list, Entity* entity, Player* localplayer)
{
  if (draw_list == nullptr || entity == nullptr) {
    return;
  }
  if (entity->is_dormant()) {
    return;
  }

  auto screen = Vec3{};
  auto origin = get_esp_draw_origin(entity);
  if (!overlay_projection::world_to_screen(origin, &screen)) {
    return;
  }
  const auto smoothed_screen = smooth_esp_point(entity, ImVec2(screen.x, screen.y), esp_smoothing_point::origin);
  screen.x = smoothed_screen.x;
  screen.y = smoothed_screen.y;

  auto bounds = esp_bounds{
    .min_x = screen.x - 5.0f,
    .min_y = screen.y - 5.0f,
    .max_x = screen.x + 5.0f,
    .max_y = screen.y + 5.0f
  };
  auto entity_bounds = esp_bounds{};
  if (get_stable_entity_screen_bounds(entity, &entity_bounds)) {
    bounds = smooth_esp_bounds(entity, entity_bounds);
  }

  if (config.esp.pickup.box) {
    draw_esp_box(draw_list, entity, bounds, config.esp.pickup.box_style, IM_COL32(255, 255, 255, 255), 1.0f);
  }

  if (!config.esp.pickup.name) {
    return;
  }

  std::string label{};
  auto text_color = IM_COL32(255, 255, 255, 255);
  if (entity->get_pickup_type() == pickup_type::AMMOPACK) {
    label = "AMMO";
  } else if (entity->get_pickup_type() == pickup_type::MEDKIT) {
    label = "HEALTH";
    text_color = IM_COL32(0, 255, 25, 255);
  }

  draw_text_centered(draw_list, ImVec2(screen.x, screen.y + 8.0f), text_color, label);
}

void draw_flag_esp(ImDrawList* draw_list, Entity* entity, Player* localplayer)
{
  if (draw_list == nullptr || entity == nullptr || localplayer == nullptr) {
    return;
  }
  if (entity->is_dormant()) {
    return;
  }
  if (entity->get_class_id() != class_id::CAPTURE_FLAG || entity->get_owner_entity() == localplayer) {
    return;
  }

  auto bounds = esp_bounds{};
  if (!get_stable_entity_screen_bounds(entity, &bounds)) {
    return;
  }
  bounds = smooth_esp_bounds(entity, bounds);

  auto color = RGBA{255, 255, 255, 255};
  if (entity->get_team() == tf_team::BLU) {
    color = RGBA{0, 0, 255, 255};
  }
  if (entity->get_team() == tf_team::RED) {
    color = RGBA{255, 0, 0, 255};
  }

  if (config.esp.intelligence.box) {
    draw_esp_box(draw_list, entity, bounds, config.esp.intelligence.box_style, to_imgui_color(color), 1.0f);
  }
  if (config.esp.intelligence.name) {
    draw_text_centered(draw_list, ImVec2((bounds.min_x + bounds.max_x) * 0.5f, bounds.max_y + 2.0f), to_imgui_color(color), "FLAG");
  }
}

void draw_pickup_timers(ImDrawList* draw_list)
{
  if (draw_list == nullptr || global_vars == nullptr) {
    return;
  }

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
      draw_text_centered(draw_list, ImVec2(screen.x, screen.y), IM_COL32(255, 255, 255, 255), time_text);
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

} // namespace

void reset_esp_runtime_state()
{
  reset_head_emoji_runtime_state();
  g_esp_smoothing_states.clear();
  g_esp_smoothing_frame = 0;
  g_esp_level_name.clear();
  g_esp_was_in_game = false;
}

void update_player_head_emoji_cache()
{
  if (!config.esp.master || !config.esp.player.head_emoji || engine == nullptr || entity_list == nullptr || global_vars == nullptr ||
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
    if (!should_draw_player(player, localplayer)) {
      continue;
    }
    if (!should_draw_player_overlay_icon(player, localplayer, config.esp.player.head_emoji_teammates)) {
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
  if (!config.esp.master || engine == nullptr || !engine->is_in_game() || render_view == nullptr || entity_list == nullptr) {
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

  auto* draw_list = ImGui::GetBackgroundDrawList();
  if (draw_list == nullptr) {
    return;
  }

  if (!overlay_projection::begin_frame()) {
    reset_esp_runtime_state();
    return;
  }

  begin_esp_smoothing_frame();

  if (config.esp.player.head_emoji) {
    refresh_player_head_emoji_cache_for_draw();
  }

  auto* player_resource = get_player_resource_entity();

  for (unsigned int index = 1; index <= entity_list->get_max_entities(); ++index) {
    auto* player = entity_list->player_from_index(index);
    if (player == nullptr) {
      continue;
    }

    if (player->get_class_id() == class_id::PLAYER) {
      if (should_draw_player(player, localplayer)) {
        draw_player_esp(draw_list, player, localplayer, player_resource);
      }
      continue;
    }

    auto* entity = player->to_entity();
    if (entity == nullptr) {
      continue;
    }

    if (entity->get_pickup_type() != pickup_type::UNKNOWN) {
      draw_pickup_esp(draw_list, entity, localplayer);
    }

    draw_flag_esp(draw_list, entity, localplayer);

    if (should_draw_building(entity, localplayer)) {
      draw_building_esp(draw_list, entity, localplayer);
    }
  }

  draw_pickup_timers(draw_list);
  cleanup_esp_smoothing_states();
}

void draw_projectile_debug_imgui()
{
  if (!config.aimbot.projectile_splash_debug || engine == nullptr || global_vars == nullptr || !engine->is_in_game()) {
    return;
  }

  if (!proj_aim_last_debug_path.valid || proj_aim_last_debug_path.expire_time < global_vars->curtime) {
    return;
  }

  auto* draw_list = ImGui::GetBackgroundDrawList();
  if (draw_list == nullptr || !overlay_projection::begin_frame()) {
    return;
  }

  const auto draw_world_path = [draw_list](const std::vector<Vec3>& path, ImU32 color) {
    if (path.size() < 2) {
      return;
    }

    constexpr size_t max_segments = 64;
    const size_t step = std::max<size_t>(1, path.size() / max_segments);
    Vec3 previous_screen{};
    bool previous_valid = false;
    for (size_t index = 0; index < path.size(); index += step) {
      Vec3 screen{};
      if (!overlay_projection::world_to_screen(path[index], &screen)) {
        previous_valid = false;
        continue;
      }

      if (previous_valid) {
        draw_list->AddLine(
          ImVec2(previous_screen.x + 1.0f, previous_screen.y + 1.0f),
          ImVec2(screen.x + 1.0f, screen.y + 1.0f),
          IM_COL32(0, 0, 0, 190),
          3.0f);
        draw_list->AddLine(ImVec2(previous_screen.x, previous_screen.y), ImVec2(screen.x, screen.y), color, 1.5f);
      }

      previous_screen = screen;
      previous_valid = true;
    }
  };

  std::vector<Vec3> projectile_points{};
  projectile_points.reserve(proj_aim_last_debug_path.projectile_trace.steps.size());
  for (const LocalPredictionProjectileStep& step : proj_aim_last_debug_path.projectile_trace.steps) {
    projectile_points.emplace_back(step.position);
  }

  draw_world_path(proj_aim_last_debug_path.target_path, IM_COL32(80, 180, 255, 230));
  draw_world_path(projectile_points, proj_aim_last_debug_path.splash ? IM_COL32(255, 170, 40, 240) : IM_COL32(80, 255, 120, 240));

  Vec3 aim_screen{};
  if (overlay_projection::world_to_screen(proj_aim_last_debug_path.aim_position, &aim_screen)) {
    const ImU32 point_color = proj_aim_last_debug_path.splash ? IM_COL32(255, 120, 40, 245) : IM_COL32(120, 255, 120, 245);
    draw_list->AddCircleFilled(ImVec2(aim_screen.x + 1.0f, aim_screen.y + 1.0f), 4.5f, IM_COL32(0, 0, 0, 200), 16);
    draw_list->AddCircleFilled(ImVec2(aim_screen.x, aim_screen.y), 3.5f, point_color, 16);
  }

  if (proj_aim_last_debug_stats.valid && proj_aim_last_debug_stats.expire_time >= global_vars->curtime) {
    const auto draw_debug_line = [draw_list](const char* text, float y, ImU32 color) {
      draw_list->AddText(ImVec2(25.0f, y + 1.0f), IM_COL32(0, 0, 0, 230), text);
      draw_list->AddText(ImVec2(24.0f, y), color, text);
    };

    const proj_aim_debug_stats& stats = proj_aim_last_debug_stats;
    const char* kind = stats.best_splash ? "splash" : (stats.best_direct ? "direct" : "none");
    char line[256]{};
    float y = 360.0f;
    std::snprintf(
      line,
      sizeof(line),
      "proj target=%d weapon=%d kind=%s fov=%.2f time=%.3f miss_d=%.1f miss_s=%.1f",
      stats.target_index,
      stats.weapon_def_id,
      kind,
      stats.best_fov,
      stats.best_time,
      stats.best_direct_miss,
      stats.best_splash_miss);
    draw_debug_line(line, y, IM_COL32(240, 245, 255, 245));
    y += 15.0f;

    std::snprintf(
      line,
      sizeof(line),
      "scan targets=%d attempts=%d cap=%d frame=%.2fms path=%d start=%.3f step=%.3f",
      stats.scan_targets,
      stats.scan_attempts,
      stats.scan_cap,
      stats.frame_time * 1000.0f,
      stats.path_positions,
      stats.path_start_time,
      stats.path_time_step);
    draw_debug_line(line, y, IM_COL32(190, 220, 255, 235));
    y += 15.0f;

    std::snprintf(
      line,
      sizeof(line),
      "path movsim=%d strafe=%d yaw=%.2f conf=%.1f target_vel=%.1f z=%.1f",
      stats.used_movement_sim ? 1 : 0,
      stats.used_strafe_prediction ? 1 : 0,
      stats.average_yaw,
      stats.strafe_confidence,
      stats.target_speed,
      stats.target_vertical_speed);
    draw_debug_line(line, y, IM_COL32(190, 220, 255, 235));
    y += 15.0f;

    std::snprintf(
      line,
      sizeof(line),
      "timing interp=%.3f out=%.3f stale=%.3f lead=%.3f",
      stats.interp_time,
      stats.outgoing_latency,
      stats.entity_staleness,
      stats.clamped_lead_time);
    draw_debug_line(line, y, IM_COL32(190, 220, 255, 235));
    y += 15.0f;

    std::snprintf(
      line,
      sizeof(line),
      "proj speed=%.1f gravity=%.1f max_time=%.2f hitbox=%s cfg=0x%02x eff=0x%02x",
      stats.projectile_speed,
      stats.projectile_gravity,
      stats.projectile_max_time,
      stats.auto_hitbox ? "auto" : "manual",
      stats.configured_hitbox_mask,
      stats.effective_hitbox_mask);
    draw_debug_line(line, y, IM_COL32(190, 220, 255, 235));
    y += 15.0f;

    std::snprintf(
      line,
      sizeof(line),
      "direct pts=%d solves=%d intercepts=%d trace_rej=%d candidates=%d",
      stats.direct_points,
      stats.direct_solves,
      stats.direct_intercepts,
      stats.direct_trace_rejects,
      stats.direct_candidates);
    draw_debug_line(line, y, IM_COL32(120, 255, 150, 235));
    y += 15.0f;

    std::snprintf(
      line,
      sizeof(line),
      "splash path=%d offsets=%d solves=%d intercepts=%d trace_rej=%d dmg_rej=%d candidates=%d",
      stats.splash_path_samples,
      stats.splash_offsets,
      stats.splash_solves,
      stats.splash_intercepts,
      stats.splash_trace_rejects,
      stats.splash_damage_rejects,
      stats.splash_candidates);
    draw_debug_line(line, y, IM_COL32(255, 190, 90, 235));
  }
}

void draw_aimbot_fov_imgui()
{
  if (engine == nullptr || entity_list == nullptr || !engine->is_in_game()) {
    return;
  }
  if (!config.aimbot.draw_fov || config.aimbot.fov >= 90.0f) {
    return;
  }

  auto* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr) {
    return;
  }

  auto* draw_list = ImGui::GetBackgroundDrawList();
  if (draw_list == nullptr) {
    return;
  }

  auto screen_size = engine->get_screen_size();
  auto local_fov = localplayer->get_fov();
  if (config.visuals.override_fov && !localplayer->is_scoped()) {
    local_fov = config.visuals.custom_fov;
  }
  if (config.visuals.removals.zoom) {
    local_fov = localplayer->get_default_fov();
  }
  if (config.visuals.override_fov && config.visuals.removals.zoom) {
    local_fov = config.visuals.custom_fov;
  }

  auto radius = (std::tan(config.aimbot.fov / 180.0f * static_cast<float>(M_PI)) /
    std::tan((local_fov / 2.0f) / 180.0f * static_cast<float>(M_PI)) *
    (static_cast<float>(screen_size.x) / 2.0f)) / 1.35f;
  if (radius <= 0.0f) {
    return;
  }

  auto center = ImVec2(static_cast<float>(screen_size.x) * 0.5f, static_cast<float>(screen_size.y) * 0.5f);
  draw_list->AddCircle(center, radius, IM_COL32(0, 0, 0, 180), 64, 3.0f);
  draw_list->AddCircle(center, radius, IM_COL32(255, 255, 255, 255), 64, 1.5f);
}

void draw_thirdperson_crosshair_imgui()
{
  thirdperson::draw_crosshair_imgui();
}
