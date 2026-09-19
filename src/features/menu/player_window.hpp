/*
/^-----^\   data: 2026-05-17
V  o o  V  file: src/features/menu/player_window.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef PLAYER_WINDOW_HPP
#define PLAYER_WINDOW_HPP
#include "core/player_manager.hpp"
#include "core/player_resource.hpp"
#include "core/ipc/ipc_client.hpp"
#include "features/automation/spectate/spectate.hpp"
#include "games/tf2/sdk/entities/entity.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/netvars.hpp"
#include "games/tf2/sdk/interfaces/steam_friends.hpp"
#include "mono/icon_definitions.hpp"
#include "mono/mono.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace pup_menu
{

struct player_row
{
  int entity_index = 0;
  int user_id = 0;
  std::uint32_t account_id = 0;
  std::string name{};
  tf_team team = tf_team::UNKNOWN;
  bool alive = false;
  bool local = false;
  bool fake = false;
  puphook::core::players::player_state state = puphook::core::players::player_state::default_state;
  std::vector<puphook::core::players::role_id> roles{};
  bool steam_friend = false;
  bool ipc_friend = false;
};

namespace
{

inline Entity* get_player_resource_entity()
{
  return puphook::core::player_resource::get_player_resource_entity();
}

template <typename value_type>
inline value_type read_player_resource_value(Entity* player_resource, int array_offset, int player_index)
{
  return puphook::core::player_resource::read_value<value_type>(player_resource, array_offset, player_index);
}

inline unsigned long long steam_id64(const std::uint32_t account_id)
{
  return 76561197960265728ULL + static_cast<unsigned long long>(account_id);
}

inline void open_external(const char* command)
{
  [[maybe_unused]] const int status = std::system(command);
}

inline void open_steam_profile(const std::uint32_t account_id)
{
  char command[192]{};
  std::snprintf(command, sizeof(command), "xdg-open 'steam://url/SteamIDPage/%llu' >/dev/null 2>&1 &", steam_id64(account_id));
  open_external(command);
}

inline void open_steam_history(const std::uint32_t account_id)
{
  char command[192]{};
  std::snprintf(command, sizeof(command), "xdg-open 'https://steamhistory.net/id/%llu' >/dev/null 2>&1 &", steam_id64(account_id));
  open_external(command);
}

inline void call_votekick(const int user_id, const char* reason)
{
  if (engine == nullptr) return;
  char command[96]{};
  if (reason == nullptr || reason[0] == '\0') {
    std::snprintf(command, sizeof(command), "callvote Kick \"%d\"", user_id);
  } else {
    std::snprintf(command, sizeof(command), "callvote Kick \"%d %s\"", user_id, reason);
  }
  engine->client_cmd_unrestricted(command);
}

inline ImVec4 role_color(const puphook::core::players::role_id role)
{
  using puphook::core::players::role_id;
  switch (role) {
  case puphook::core::players::ignored_role: return { 0.55f, 0.55f, 0.58f, 1.0f };
  case puphook::core::players::cheater_role: return { 0.86f, 0.28f, 0.28f, 1.0f };
  case puphook::core::players::friend_role: return { 0.35f, 0.78f, 0.42f, 1.0f };
  case puphook::core::players::party_role: return { 0.30f, 0.72f, 0.86f, 1.0f };
  case puphook::core::players::f2p_role: return { 0.90f, 0.74f, 0.28f, 1.0f };
  case puphook::core::players::ipc_role: return { 0.70f, 0.45f, 0.90f, 1.0f };
  case puphook::core::players::textmode_role: return { 0.92f, 0.55f, 0.28f, 1.0f };
  case puphook::core::players::identified_role: return menu_accent();
  default: return { 0.50f, 0.55f, 0.62f, 1.0f };
  }
}

inline ImVec4 team_card_color(const tf_team team, const bool alive)
{
  ImVec4 color{ 0.50f, 0.50f, 0.50f, 1.0f };
  switch (team) {
  case tf_team::BLU: color = { 0.39f, 0.59f, 0.78f, 1.0f }; break;
  case tf_team::RED: color = { 1.00f, 0.39f, 0.39f, 1.0f }; break;
  default: break;
  }
  color.x = color.x * 0.5f + k_bg_panel.x * 0.5f;
  color.y = color.y * 0.5f + k_bg_panel.y * 0.5f;
  color.z = color.z * 0.5f + k_bg_panel.z * 0.5f;
  color.w = alive ? 1.0f : 0.55f;
  return color;
}

inline bool role_chip_removable(const puphook::core::players::role_id role)
{
  for (const auto& definition : puphook::core::players::role_definitions()) {
    if (definition.id == role) return definition.assignable && !definition.runtime;
  }
  return false;
}

inline std::string lowercase_copy(std::string text)
{
  for (char& character : text) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  return text;
}

}

inline std::vector<player_row> collect_player_rows() {
  std::vector<player_row> rows{};
  if (engine == nullptr || entity_list == nullptr || global_vars == nullptr) {
    return rows;
  }
  if (!engine->is_connected() || !engine->is_in_game() || engine->is_drawing_loading_image()) {
    return rows;
  }

  auto* player_resource = get_player_resource_entity();
  if (player_resource == nullptr) {
    return rows;
  }

  static tf2_netvars::lazy_offset connected_offset{"DT_TFPlayerResource", { "baseclass", "m_bConnected" }};
  static tf2_netvars::lazy_offset team_offset{"DT_TFPlayerResource", { "baseclass", "m_iTeam" }};
  static tf2_netvars::lazy_offset alive_offset{"DT_TFPlayerResource", { "baseclass", "m_bAlive" }};
  static tf2_netvars::lazy_offset ping_offset{"DT_TFPlayerResource", { "baseclass", "m_iPing" }};

  if (connected_offset <= 0 || team_offset <= 0 || alive_offset <= 0) {
    return rows;
  }

  const int local_index = engine->get_localplayer_index();
  const int max_clients = global_vars->max_clients;
  rows.reserve(max_clients);
  for (int index = 1; index <= max_clients; ++index) {
    const bool is_connected = read_player_resource_value<bool>(player_resource, connected_offset, index);
    if (!is_connected) {
      continue;
    }

    player_info pinfo{};
    if (!engine->get_player_info(index, &pinfo)) {
      continue;
    }

    const char* name_ptr = puphook::core::player_resource::name_pointer(player_resource, ping_offset, index);

    player_row row{};
    row.entity_index = index;
    row.user_id = pinfo.user_id;
    row.account_id = static_cast<std::uint32_t>(pinfo.friends_id);
    row.name = (name_ptr != nullptr && name_ptr[0] != '\0') ? name_ptr : pinfo.name;
    row.team = static_cast<tf_team>(read_player_resource_value<int>(player_resource, team_offset, index));
    row.alive = read_player_resource_value<bool>(player_resource, alive_offset, index);
    row.local = (index == local_index);
    row.fake = pinfo.fakeplayer;

    if (row.account_id != 0) {
      row.state = puphook::core::players::state_for(row.account_id);
      row.roles = puphook::core::players::roles_for(row.account_id);
      row.ipc_friend = pup_ipc::client::is_local_ipc_friend(row.account_id);
      if (steam_friends != nullptr) {
        row.steam_friend = steam_friends->is_friend(pinfo.friends_id);
      }
    }

    rows.push_back(std::move(row));
  }

  return rows;
}

inline void draw_player_context_menu(const char* popup_id, const player_row& row, const bool marked_card)
{
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
  ImGui::PushStyleColor(ImGuiCol_PopupBg, k_bg_panel);
  if (ImGui::BeginPopup(popup_id)) {
    if (!row.fake && row.account_id != 0) {
      if (ImGui::MenuItem("Profile")) open_steam_profile(row.account_id);
      if (ImGui::MenuItem("History")) open_steam_history(row.account_id);
    }

    if (row.user_id > 0) {
      const bool spectating = spectate::target_userid() == row.user_id;
      if (ImGui::MenuItem(spectating ? "Unspectate" : "Spectate")) {
        spectate::set_target_userid(row.user_id);
      }
    }

    if (!row.local && row.user_id > 0 && ImGui::BeginMenu("Votekick")) {
      if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        call_votekick(row.user_id, nullptr);
        ImGui::CloseCurrentPopup();
      }
      if (ImGui::MenuItem("No reason")) call_votekick(row.user_id, "other");
      if (ImGui::MenuItem("Cheating")) call_votekick(row.user_id, "cheating");
      if (ImGui::MenuItem("Idle")) call_votekick(row.user_id, "idle");
      if (ImGui::MenuItem("Scamming")) call_votekick(row.user_id, "scamming");
      ImGui::EndMenu();
    }

    if (!row.fake && row.account_id != 0) {
      if (ImGui::BeginMenu("Add tag")) {
        for (const auto& definition : puphook::core::players::role_definitions()) {
          if (definition.id == puphook::core::players::default_role || !definition.assignable) continue;
          if (puphook::core::players::has_role(row.account_id, definition.id)) continue;
          const ImVec4 color = role_color(definition.id);
          ImGui::PushStyleColor(ImGuiCol_Text, color);
          if (ImGui::MenuItem(definition.name)) {
            (void)puphook::core::players::add_role(row.account_id, definition.id, row.name);
          }
          ImGui::PopStyleColor();
        }
        ImGui::EndMenu();
      }

      if (!row.roles.empty() && ImGui::MenuItem(marked_card ? "Clear role" : "Clear tags")) {
        (void)puphook::core::players::clear_state(row.account_id);
      }
    }
    ImGui::EndPopup();
  }
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);
}

inline void draw_role_chips(const player_row& row, const float card_x, const float card_y, const float width, const float height, const float reserved_left, bool& popup)
{
  if (row.fake || row.roles.empty()) return;

  float chips_width = 8.0f;
  for (const auto role : row.roles) {
    const char* name = puphook::core::players::role_name(role);
    chips_width += ImGui::CalcTextSize(name).x + (role_chip_removable(role) ? 28.0f : 14.0f);
  }
  chips_width = std::min(chips_width, std::max(width * 0.5f, width - reserved_left));
  if (chips_width < 12.0f) return;

  ImGui::SetCursorPos(ImVec2(card_x + width - chips_width, card_y));
  char child_id[40]{};
  std::snprintf(child_id, sizeof(child_id), "tags_%u_%d", row.account_id, row.entity_index);
  if (ImGui::BeginChild(child_id, ImVec2(chips_width, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground)) {
    float offset = 4.0f;
    const ImVec2 origin = ImGui::GetWindowPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    for (const auto role : row.roles) {
      const char* name = puphook::core::players::role_name(role);
      const bool removable = role_chip_removable(role);
      const float chip_width = ImGui::CalcTextSize(name).x + (removable ? 24.0f : 10.0f);
      const ImVec2 chip_min{ origin.x + offset, origin.y + 4.0f };
      const ImVec2 chip_max{ chip_min.x + chip_width, origin.y + height - 4.0f };
      const ImVec4 color = role_color(role);
      draw_list->AddRectFilled(chip_min, chip_max, ImGui::ColorConvertFloat4ToU32(color), 4.0f);
      const bool bright = (0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z) > 0.62f;
      ImGui::SetCursorPos(ImVec2(offset + 5.0f, 6.0f));
      ImGui::PushStyleColor(ImGuiCol_Text, bright ? ImVec4(0.0f, 0.0f, 0.0f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
      ImGui::TextUnformatted(name);
      ImGui::PopStyleColor();
      if (removable) {
        ImGui::SetCursorPos(ImVec2(offset + chip_width - 20.0f, 3.0f));
        ImGui::PushID(static_cast<int>(role));
        if (icon_button(ICON_MD_CANCEL, ImVec2(16.0f, 16.0f))) {
          (void)puphook::core::players::remove_role(row.account_id, role);
        }
        ImGui::PopID();
      }
      offset += chip_width + 4.0f;
    }
    popup = popup || (ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right));
  }
  ImGui::EndChild();
}

inline void draw_player_card(const player_row& row, const float x, const float y, const float width, const bool marked_card)
{
  const float height = marked_card ? 52.0f : 28.0f;
  ImGui::PushID(row.entity_index != 0 ? row.entity_index : static_cast<int>(row.account_id));
  ImGui::SetCursorPos(ImVec2(x, y));
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  const ImVec4 fill = marked_card && !row.roles.empty()
    ? ImVec4{
        role_color(row.roles.front()).x * 0.45f + k_bg_panel.x * 0.55f,
        role_color(row.roles.front()).y * 0.45f + k_bg_panel.y * 0.55f,
        role_color(row.roles.front()).z * 0.45f + k_bg_panel.z * 0.55f,
        1.0f
      }
    : team_card_color(row.team, row.alive);
  draw_list->AddRectFilled(origin, { origin.x + width, origin.y + height }, ImGui::ColorConvertFloat4ToU32(fill), 4.0f);

  ImGui::SetCursorPos(ImVec2(x, y));
  char button_id[40]{};
  std::snprintf(button_id, sizeof(button_id), "##card_%u_%d", row.account_id, row.entity_index);
  ImGui::InvisibleButton(button_id, ImVec2(width, height));
  bool popup = ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right);

  const bool spectating = row.user_id > 0 && spectate::target_userid() == row.user_id;
  const bool show_icon = row.local || spectating || row.steam_friend || row.ipc_friend
    || row.state == puphook::core::players::player_state::friend_state
    || row.state == puphook::core::players::player_state::party;
  float text_x = 10.0f;
  if (show_icon) {
    const char* icon = ICON_MD_PERSON;
    if (!row.local) {
      if (spectating) icon = ICON_MD_VISIBILITY;
      else if (row.steam_friend || row.state == puphook::core::players::player_state::friend_state) icon = ICON_MD_GROUP;
      else if (row.ipc_friend || row.state == puphook::core::players::player_state::party) icon = ICON_MD_GROUPS;
    }
    ImGui::SetCursorPos(ImVec2(x + 6.0f, y + (marked_card ? 8.0f : 4.0f)));
    if (ImFont* icons = font_icons(); icons != nullptr) {
      ImGui::PushFont(icons);
      ImGui::TextUnformatted(icon);
      ImGui::PopFont();
    }
    text_x = 28.0f;
  }

  if (!marked_card) {
    draw_role_chips(row, x, y, width, height, text_x + 24.0f, popup);
  }

  const float name_max = width - text_x - (marked_card ? 32.0f : 12.0f);
  ImGui::SetCursorPos(ImVec2(x + text_x, y + (marked_card ? 6.0f : 6.0f)));
  ImGui::TextUnformatted(truncate_ui_text(row.name, name_max).c_str());
  if (marked_card) {
    std::string roles{};
    for (std::size_t index = 0; index < row.roles.size(); ++index) {
      if (index != 0) roles += ", ";
      roles += puphook::core::players::role_name(row.roles[index]);
    }
    if (roles.empty()) roles = "Unmarked";
    ImGui::SetCursorPos(ImVec2(x + text_x, y + 26.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, row.roles.empty() ? k_text_soft : role_color(row.roles.front()));
    ImGui::TextUnformatted(truncate_ui_text("Role: " + roles, name_max).c_str());
    ImGui::PopStyleColor();
    if (row.account_id != 0) {
      ImGui::SetCursorPos(ImVec2(x + width - 24.0f, y + 4.0f));
      if (icon_button(ICON_MD_DELETE, ImVec2(18.0f, 18.0f))) {
        (void)puphook::core::players::clear_state(row.account_id);
      }
    }
  }

  char popup_id[40]{};
  std::snprintf(popup_id, sizeof(popup_id), "##pm_popup_%u_%d", row.account_id, row.entity_index);
  if (popup) ImGui::OpenPopup(popup_id);
  draw_player_context_menu(popup_id, row, marked_card);
  ImGui::PopID();
}

inline void draw_player_grid(const std::vector<player_row>& blu, const std::vector<player_row>& red, const std::vector<player_row>& other)
{
  const float width = ImGui::GetContentRegionAvail().x;
  const float gap = 6.0f;
  const float column = (width - gap) * 0.5f;
  const float step = 36.0f;
  const float start_y = ImGui::GetCursorPosY();
  for (std::size_t index = 0; index < blu.size(); ++index) {
    draw_player_card(blu[index], 0.0f, start_y + step * static_cast<float>(index), column, false);
  }
  for (std::size_t index = 0; index < red.size(); ++index) {
    draw_player_card(red[index], column + gap, start_y + step * static_cast<float>(index), column, false);
  }
  const float other_start = start_y + step * static_cast<float>(std::max(blu.size(), red.size()));
  for (std::size_t index = 0; index < other.size(); ++index) {
    const bool left = (index % 2) == 0;
    draw_player_card(
      other[index],
      left ? 0.0f : column + gap,
      other_start + step * static_cast<float>(index / 2),
      column,
      false);
  }
  const std::size_t other_rows = (other.size() + 1) / 2;
  ImGui::SetCursorPosY(other_start + step * static_cast<float>(other_rows));
  ImGui::Dummy(ImVec2(0.0f, 8.0f));
}

inline void draw_marked_players()
{
  static std::string search{};
  mono::input_string_with_hint("##marked_search", &search, "Search marked players...");

  std::vector<player_row> marked{};
  for (const auto& entry : puphook::core::players::entries(false)) {
    player_row row{};
    row.account_id = entry.account_id;
    row.name = entry.name.empty() ? std::to_string(entry.account_id) : entry.name;
    row.state = entry.state;
    row.roles = entry.roles;
    marked.push_back(std::move(row));
  }

  if (!search.empty()) {
    const std::string needle = lowercase_copy(search);
    std::erase_if(marked, [&](const player_row& row) {
      std::string haystack = lowercase_copy(row.name + " " + std::to_string(row.account_id));
      for (const auto role : row.roles) {
        haystack += " ";
        haystack += lowercase_copy(puphook::core::players::role_name(role));
      }
      return haystack.find(needle) == std::string::npos;
    });
  }

  if (marked.empty()) {
    ImGui::TextDisabled("%s", search.empty() ? "Nothings here..." : "No matching marked players.");
    return;
  }

  const float width = ImGui::GetContentRegionAvail().x;
  const float gap = 6.0f;
  const float column = (width - gap) * 0.5f;
  const float step = 60.0f;
  const float start_y = ImGui::GetCursorPosY();
  for (std::size_t index = 0; index < marked.size(); ++index) {
    const bool left = (index % 2) == 0;
    draw_player_card(
      marked[index],
      left ? 0.0f : column + gap,
      start_y + step * static_cast<float>(index / 2),
      column,
      true);
  }
  ImGui::SetCursorPosY(start_y + step * static_cast<float>((marked.size() + 1) / 2));
  ImGui::Dummy(ImVec2(0.0f, 8.0f));
}

inline void draw_player_window_content() {
  if (engine == nullptr || !engine->is_connected() || !engine->is_in_game()) {
    ImGui::PushStyleColor(ImGuiCol_Text, k_text_soft);
    ImGui::Dummy(ImVec2(0.0f, 20.0f));
    const char* message = "Not in a game.";
    const ImVec2 text_size = ImGui::CalcTextSize(message);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - text_size.x) * 0.5f);
    ImGui::TextUnformatted(message);
    ImGui::PopStyleColor();
    return;
  }

  const std::vector<player_row> rows = collect_player_rows();
  std::vector<player_row> blu_rows{}, red_rows{}, other_rows{};
  blu_rows.reserve(rows.size());
  red_rows.reserve(rows.size());
  other_rows.reserve(rows.size());
  for (const auto& row : rows) {
    switch (row.team) {
      case tf_team::BLU: blu_rows.push_back(row); break;
      case tf_team::RED: red_rows.push_back(row); break;
      default: other_rows.push_back(row); break;
    }
  }
  auto sort_by_name = [](std::vector<player_row>& list) {
    std::sort(list.begin(), list.end(), [](const player_row& left, const player_row& right) {
      return left.name < right.name;
    });
  };
  sort_by_name(blu_rows);
  sort_by_name(red_rows);
  sort_by_name(other_rows);

  if (ImGui::BeginChild("##pm_body", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
    mono::begin_panel("Players", { 0.0f, 0.0f });
    draw_player_grid(blu_rows, red_rows, other_rows);
    mono::end_panel();
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    mono::begin_panel("Marked Players", { 0.0f, 0.0f });
    draw_marked_players();
    mono::end_panel();
  }
  ImGui::EndChild();
}

}
#endif
