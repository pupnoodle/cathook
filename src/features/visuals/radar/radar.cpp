/*
data: 2026-08-10
file: src/features/visuals/radar/radar.cpp
author: HappyKuro
*/
#include "features/visuals/radar/radar.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

#include "imgui/imgui.h"
#include "mono/window_chrome.hpp"

#include "core/entity_cache.hpp"
#include "core/types.hpp"
#include "features/menu/config.hpp"
#include "features/menu/menu.hpp"
#include "features/visuals/esp/esp.hpp"
#include "games/tf2/sdk/entities/entity.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"

namespace radar
{

namespace
{

constexpr float radar_pi = 3.14159265358979323846f;
constexpr float radar_deg2rad = radar_pi / 180.0f;

// assets/textures/atlas.png is a 16x8 grid of 64x64 tiles. Row 5 holds the nine
// neutral class glyphs (scout first), and column 11 of rows 6 and 7 holds the
// team-coloured backing discs those glyphs sit on.
constexpr int atlas_class_glyph_row = 5;
constexpr int atlas_team_disc_column = 11;
constexpr int atlas_team_disc_first_row = 6;
constexpr int tf_class_count = 9;

// Beyond this many seconds a remembered position is too old to be worth drawing;
// the player has almost certainly moved somewhere else entirely.
constexpr float track_lifetime = 8.0f;

// A dormant player is one the server has stopped networking because it left our
// PVS, which is most of the enemy team most of the time. Its live m_vecOrigin is
// frozen at whatever it held when it went dormant, so the only way to place it on
// the radar is to remember the last position we were actually told about and draw
// that, faded, until it goes stale. Skipping dormant players outright is what makes
// a radar look empty: it would only ever show people already on screen.
struct player_track
{
  Vec3 origin{};
  tf_team team = tf_team::UNKNOWN;
  int tf_class = 0;
  float last_seen = 0.0f;
};

std::unordered_map<int, player_track> g_player_tracks{};
std::string g_track_level_name{};

void reset_tracks_on_level_change()
{
  if (engine == nullptr) {
    return;
  }

  const char* level_name = engine->get_level_name();
  const std::string current = level_name != nullptr ? std::string{ level_name } : std::string{};
  if (current != g_track_level_name) {
    g_track_level_name = current;
    g_player_tracks.clear();
  }
}

// The entity cache is rebuilt once per net update and holds exactly the living,
// non-dormant players, which is what a track refresh wants. Reading it here avoids
// walking every entity index again on every rendered frame.
void refresh_tracks(Player* localplayer, const float now)
{
  const int local_index = localplayer->get_index();
  for (const entity_cache_player_entry& entry : entity_cache_players()) {
    if (entry.index == local_index || !entry.alive || entry.dormant) {
      continue;
    }

    g_player_tracks[entry.index] = {
      .origin = entry.origin,
      .team = entry.team,
      .tf_class = entry.player_class,
      .last_seen = now
    };
  }
}

// The cache carries no dead players, so someone killed in front of us would
// otherwise linger as a stale blip for the full track lifetime. Only tracked
// indices are checked here, which is a handful of entities rather than the list.
void forget_dead_tracks()
{
  if (entity_list == nullptr) {
    return;
  }

  for (auto it = g_player_tracks.begin(); it != g_player_tracks.end();) {
    Entity* entity = entity_list->entity_from_index(static_cast<unsigned int>(it->first));
    if (entity != nullptr && entity->get_class_id() == class_id::PLAYER && !entity->is_dormant() &&
        !static_cast<Player*>(entity)->is_alive()) {
      it = g_player_tracks.erase(it);
      continue;
    }

    ++it;
  }
}

void forget_stale_tracks(const float now)
{
  for (auto it = g_player_tracks.begin(); it != g_player_tracks.end();) {
    if (now - it->second.last_seen > track_lifetime || now < it->second.last_seen) {
      it = g_player_tracks.erase(it);
    } else {
      ++it;
    }
  }
}

// Offset from the local player, scaled by zoom and rotated by view yaw so the radar
// always points where the player is facing. Returned relative to the centre of the
// radar square, and clamped to it so a distant player pins to the rim rather than
// drawing outside the frame.
ImVec2 world_to_radar_offset(const Vec3& origin, Player* localplayer)
{
  if (localplayer == nullptr || engine == nullptr || config.visuals.radar.zoom <= 0.0f) {
    return { 0.0f, 0.0f };
  }

  const Vec3 local_origin = localplayer->get_origin();
  Vec3 view_angles{};
  engine->get_view_angles(view_angles);

  const float dx = -((origin.x - local_origin.x) / config.visuals.radar.zoom);
  const float dy = (origin.y - local_origin.y) / config.visuals.radar.zoom;

  const float yaw = radar_deg2rad * view_angles.y + radar_pi / 2.0f;
  const float nx = dx * std::cos(yaw) - dy * std::sin(yaw);
  const float ny = dx * std::sin(yaw) + dy * std::cos(yaw);

  const float half = static_cast<float>(config.visuals.radar.size) * 0.5f;
  return { std::clamp(nx, -half, half), std::clamp(ny, -half, half) };
}

// Two stacked tiles: the team-coloured disc supplies the colour, the neutral class
// glyph goes on top. Both carry the same tint, which is white apart from its alpha,
// so a stale track fades as a whole rather than losing one of its two layers.
// Returns false when the icon could not be drawn, leaving the caller to fall back
// to the plain dot.
bool draw_class_icon(
  ImDrawList* draw_list,
  const ImVec2& center,
  const tf_team team,
  const int tf_class,
  const int alpha)
{
  if (tf_class <= 0 || tf_class > tf_class_count) {
    return false;
  }

  const int team_index = static_cast<int>(team) - static_cast<int>(tf_team::RED);
  if (team_index < 0 || team_index > 1) {
    return false;
  }

  if (!atlas_texture_ready()) {
    return false;
  }

  const auto size = static_cast<float>(config.visuals.radar.icon_size);
  const ImU32 tint = IM_COL32(255, 255, 255, alpha);

  draw_shared_atlas_tile(
    draw_list, atlas_team_disc_column, atlas_team_disc_first_row + team_index, center, size, tint);
  draw_shared_atlas_tile(
    draw_list, tf_class - 1, atlas_class_glyph_row, center, size, tint);
  return true;
}

// Rings, axes and the centre cross. Drawn before the blips so a player sitting on a
// line or a ring is never hidden behind it. No background or border here - the mono
// chrome supplies both.
void draw_radar_field(ImDrawList* draw_list, const ImVec2& top_left, const float scale)
{
  const auto size = static_cast<float>(config.visuals.radar.size);
  const ImVec2 center{ top_left.x + size * 0.5f, top_left.y + size * 0.5f };

  // Rings first, so the axis lines cross on top of them and the centre stays the
  // strongest mark on the radar. Spacing is in radar pixels rather than world units,
  // which keeps the rings put while zoom changes what they mean - the alternative,
  // fixed world distances, makes them slide around under the blips.
  const int rings = std::clamp(config.visuals.radar.range_rings, 0, 8);
  if (rings > 0) {
    const float step = (size * 0.5f) / static_cast<float>(rings);
    for (int ring = 1; ring <= rings; ++ring) {
      draw_list->AddCircle(center, step * static_cast<float>(ring), IM_COL32(100, 100, 100, 70), 48, 1.0f);
    }
  }

  // The full-span pair. Held dimmer than the centre cross: they give the eye a sense
  // of which quadrant something is in, rather than being read directly.
  if (config.visuals.radar.axis_lines) {
    const ImU32 axis_color = IM_COL32(100, 100, 100, 90);
    draw_list->AddLine({ top_left.x, center.y }, { top_left.x + size, center.y }, axis_color, 1.0f);
    draw_list->AddLine({ center.x, top_left.y }, { center.x, top_left.y + size }, axis_color, 1.0f);
  }

  // The short centre cross, kept whatever else is switched off - it is the only
  // thing marking where you are once the local player icon is hidden behind a blip.
  const float cross = 10.0f * scale;
  const ImU32 cross_color = IM_COL32(100, 100, 100, 150);
  draw_list->AddLine({ center.x - cross, center.y }, { center.x + cross, center.y }, cross_color, 1.0f);
  draw_list->AddLine({ center.x, center.y - cross }, { center.x, center.y + cross }, cross_color, 1.0f);
}

void draw_blip(
  ImDrawList* draw_list,
  const player_track& track,
  Player* localplayer,
  const ImVec2& top_left,
  const bool live)
{
  const bool is_enemy = track.team != localplayer->get_team();
  if (is_enemy && !config.visuals.radar.show_enemies) {
    return;
  }

  if (!is_enemy && !config.visuals.radar.show_teammates) {
    return;
  }

  const ImVec2 offset = world_to_radar_offset(track.origin, localplayer);
  const auto size = static_cast<float>(config.visuals.radar.size);
  const ImVec2 center{ top_left.x + size * 0.5f + offset.x, top_left.y + size * 0.5f + offset.y };

  // Remembered positions are drawn translucent so a stale dot is never mistaken for
  // a player the game is currently telling us about.
  const int alpha = live ? 255 : 110;

  // The atlas is shared with the ESP and may still be decoding, and the class is not
  // networked for a player we have never had in PVS, so the dot stays the fallback
  // rather than the icon path being assumed to work.
  if (config.visuals.radar.use_icons && draw_class_icon(draw_list, center, track.team, track.tf_class, alpha)) {
    return;
  }

  const float radius = static_cast<float>(config.visuals.radar.icon_size) * 0.5f;
  const ImU32 color = is_enemy ? IM_COL32(255, 50, 50, alpha) : IM_COL32(50, 150, 255, alpha);
  draw_list->AddCircleFilled(center, radius, color);
  draw_list->AddCircle(center, radius, IM_COL32(0, 0, 0, alpha), 0, 1.5f);
}

void draw_localplayer_blip(ImDrawList* draw_list, Player* localplayer, const ImVec2& top_left)
{
  const auto size = static_cast<float>(config.visuals.radar.size);
  const ImVec2 center{ top_left.x + size * 0.5f, top_left.y + size * 0.5f };

  if (config.visuals.radar.use_icons &&
      draw_class_icon(
        draw_list, center, localplayer->get_team(), static_cast<int>(localplayer->get_tf_class()), 255)) {
    return;
  }

  // The local player always sits dead centre, so it gets a smaller marker that does
  // not swallow nearby dots.
  const float radius = static_cast<float>(config.visuals.radar.icon_size) * 0.3f;
  draw_list->AddCircleFilled(center, radius, IM_COL32(255, 255, 255, 255));
  draw_list->AddCircle(center, radius, IM_COL32(0, 0, 0, 255), 0, 1.5f);
}

}

void draw_radar()
{
  if (!config.visuals.radar.enabled || entity_list == nullptr || engine == nullptr) {
    return;
  }

  Player* localplayer = entity_list->get_localplayer();
  if (localplayer == nullptr || !localplayer->is_alive()) {
    return;
  }

  const float scale = mono::window_scale();
  const float header_height = mono::window_header_height();
  const auto width = static_cast<float>(config.visuals.radar.size);
  const float height = header_height + width;

  ImGui::SetNextWindowPos({ config.visuals.radar.x, config.visuals.radar.y }, ImGuiCond_Always);
  ImGui::SetNextWindowSize({ width, height }, ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f });
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
    | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
    | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar;
  if (!menu_focused) {
    flags |= ImGuiWindowFlags_NoInputs;
  }

  if (ImGui::Begin("mono_overlay_radar", nullptr, flags)) {
    const ImVec2 position = ImGui::GetWindowPos();
    const ImVec2 end{ position.x + width, position.y + height };
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    draw_list->AddRectFilled(position, end, ImGui::GetColorU32(ImGuiCol_WindowBg), 4.0f * scale);
    mono::draw_window_header(*draw_list, position, width, { "radar" });

    const ImVec2 field_top_left{ position.x, position.y + header_height };
    draw_radar_field(draw_list, field_top_left, scale);

    reset_tracks_on_level_change();
    const float now = global_vars != nullptr ? global_vars->curtime : 0.0f;
    refresh_tracks(localplayer, now);
    forget_dead_tracks();
    forget_stale_tracks(now);

    for (const auto& [tracked_index, track] : g_player_tracks) {
      (void)tracked_index;
      draw_blip(draw_list, track, localplayer, field_top_left, track.last_seen == now);
    }

    // Local player last, so it sits on top of any blip sharing the centre.
    draw_localplayer_blip(draw_list, localplayer, field_top_left);

    // Border after the contents, so a blip clamped to the rim cannot overdraw the
    // frame or bleed past a rounded corner.
    draw_list->AddRect(
      { position.x + 0.5f * scale, position.y + 0.5f * scale },
      { end.x - 0.5f * scale, end.y - 0.5f * scale },
      ImGui::GetColorU32(ImGuiCol_CheckMark), 4.0f * scale, 0, scale);

    static ImVec2 drag_offset{};
    static bool dragging{};
    const ImVec2 header_end{ end.x, position.y + header_height };
    if (menu_focused && ImGui::IsMouseHoveringRect(position, header_end) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      dragging = true;
      const ImVec2 mouse = ImGui::GetMousePos();
      drag_offset = { mouse.x - position.x, mouse.y - position.y };
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      dragging = false;
    }
    if (dragging) {
      const ImVec2 mouse = ImGui::GetMousePos();
      ImGui::SetWindowPos({ mouse.x - drag_offset.x, mouse.y - drag_offset.y });
    }

    config.visuals.radar.x = ImGui::GetWindowPos().x;
    config.visuals.radar.y = ImGui::GetWindowPos().y;
    ImGui::Dummy({ width, height });
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}

}
