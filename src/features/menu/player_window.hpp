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
#include "core/ipc/ipc_client.hpp"
#include "games/tf2/sdk/entities/entity.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/global_vars.hpp"
#include "games/tf2/sdk/netvars.hpp"
#include "games/tf2/sdk/interfaces/steam_friends.hpp"
#include "mono/mono.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace cat_menu
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
  cathook::core::players::player_state state = cathook::core::players::player_state::default_state;
  std::vector<cathook::core::players::role_id> roles{};
  bool steam_friend = false;
  bool ipc_friend = false;
};

namespace
{

inline Entity* get_player_resource_entity()
{
  if (entity_list == nullptr)
  {
    return nullptr;
  }

  const int max_entities = entity_list->get_max_entities();
  for (int index = 1; index <= max_entities; ++index)
  {
    auto* entity = entity_list->entity_from_index(index);
    if (entity != nullptr && entity->get_class_id() == class_id::PLAYER_RESOURCE)
    {
      return entity;
    }
  }

  return nullptr;
}

template <typename value_type>
inline value_type read_player_resource_value(Entity* player_resource, int array_offset, int player_index)
{
  if (player_resource == nullptr || player_index <= 0)
  {
    return {};
  }

  const auto base = reinterpret_cast<std::uintptr_t>(player_resource);
  const auto entry_offset = static_cast<std::uintptr_t>(array_offset) + (static_cast<std::uintptr_t>(player_index) * sizeof(value_type));
  return *reinterpret_cast<value_type*>(base + entry_offset);
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

  static const int connected_offset = tf2_netvars::find_offset("DT_TFPlayerResource", { "baseclass", "m_bConnected" });
  static const int team_offset = tf2_netvars::find_offset("DT_TFPlayerResource", { "baseclass", "m_iTeam" });
  static const int alive_offset = tf2_netvars::find_offset("DT_TFPlayerResource", { "baseclass", "m_bAlive" });
  static const int ping_offset = tf2_netvars::find_offset("DT_TFPlayerResource", { "baseclass", "m_iPing" });

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

    const char* name_ptr = nullptr;
    if (ping_offset > 816) {
      name_ptr = reinterpret_cast<const char* const*>(reinterpret_cast<uintptr_t>(player_resource) + ping_offset - 816)[index];
    }

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
      row.state = cathook::core::players::state_for(row.account_id);
      row.roles = cathook::core::players::roles_for(row.account_id);
      row.ipc_friend = cat_ipc::client::is_local_ipc_friend(row.account_id);
      if (steam_friends != nullptr) {
        row.steam_friend = steam_friends->is_friend(pinfo.friends_id);
      }
    }

    rows.push_back(std::move(row));
  }

  return rows;
}

inline const char* player_state_label(cathook::core::players::player_state state) {
  using cathook::core::players::player_state;
  switch (state) {
    case player_state::friend_state: return "FRIEND";
    case player_state::ignored: return "IGNORE";
    case player_state::cheater: return "CHEATER";
    case player_state::ipc: return "IPC";
    case player_state::textmode: return "TEXTMODE";
    case player_state::party: return "PARTY";
    default: return "";
  }
}

inline void draw_player_row_box(const player_row& row) {
  using namespace cathook::core::players;
  std::string label{};
  if (row.local) {
    label += "@ ";
  } else if (row.steam_friend || row.state == player_state::friend_state || row.ipc_friend) {
    label += "+ ";
  }
  label += row.name;
  if (!row.roles.empty()) {
    label += " [";
    for (std::size_t index = 0; index < row.roles.size(); ++index) {
      if (index != 0) label += ", ";
      label += cathook::core::players::role_name(row.roles[index]);
    }
    label += "]";
  } else if (const char* tag_label = player_state_label(row.state); tag_label != nullptr && tag_label[0] != '\0') {
    label += " [";
    label += tag_label;
    label += "]";
  }
  mono::list_item(label.c_str(), false, { 0.0f, 28.0f });
  if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
    char popup_id[32]{};
    std::snprintf(popup_id, sizeof(popup_id), "##pm_popup_%d", row.entity_index);
    ImGui::OpenPopup(popup_id);
  }

  char popup_id[32]{};
  std::snprintf(popup_id, sizeof(popup_id), "##pm_popup_%d", row.entity_index);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
  ImGui::PushStyleColor(ImGuiCol_PopupBg, k_bg_panel);
  if (ImGui::BeginPopup(popup_id)) {
    if (!row.fake && row.account_id != 0) {
      if (ImGui::MenuItem("Open Steam profile")) {
        const unsigned long long sid64 = 76561197960265728ULL + static_cast<unsigned long long>(row.account_id);
        char cmd[160]{};
        std::snprintf(cmd, sizeof(cmd), "xdg-open 'steam://url/SteamIDPage/%llu' >/dev/null 2>&1 &", sid64);
        [[maybe_unused]] const int command_status = std::system(cmd);
      }
      ImGui::Separator();
    }

    if (!row.local && row.user_id > 0) {
      if (ImGui::MenuItem("Votekick (cheating)")) {
        if (engine != nullptr) {
          char cmd[96]{};
          std::snprintf(cmd, sizeof(cmd), "callvote Kick \"%d cheating\"", row.user_id);
          engine->client_cmd_unrestricted(cmd);
        }
      }
      if (ImGui::MenuItem("Votekick (idle)")) {
        if (engine != nullptr) {
          char cmd[96]{};
          std::snprintf(cmd, sizeof(cmd), "callvote Kick \"%d idle\"", row.user_id);
          engine->client_cmd_unrestricted(cmd);
        }
      }
      if (ImGui::MenuItem("Spectate")) {
        if (engine != nullptr) {

          char cmd[64]{};
          std::snprintf(cmd, sizeof(cmd), "spec_player \"#%d\"", row.user_id);
          engine->client_cmd_unrestricted(cmd);
        }
      }
      ImGui::Separator();
    }

    if (!row.fake && row.account_id != 0) {
      for (const auto& definition : cathook::core::players::role_definitions()) {
        if (definition.id == cathook::core::players::default_role || !definition.assignable || definition.runtime) {
          continue;
        }
        char role_label[96]{};
        std::snprintf(role_label, sizeof(role_label), "Role: %s", definition.name);
        const bool assigned = cathook::core::players::has_role(row.account_id, definition.id);
        if (ImGui::MenuItem(role_label, nullptr, assigned)) {
          if (assigned) {
            (void)cathook::core::players::remove_role(row.account_id, definition.id);
          } else {
            (void)cathook::core::players::add_role(row.account_id, definition.id, row.name);
          }
        }
      }
      if (!row.roles.empty() && !row.ipc_friend) {
        ImGui::Separator();
        if (ImGui::MenuItem("Clear state")) {
          cathook::core::players::clear_state(row.account_id);
        }
      }
    }
    ImGui::EndPopup();
  }
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
}

inline void draw_player_team_column(const char* title, ImVec4 title_color, const std::vector<player_row>& rows) {
  (void)title_color;
  char panel_title[64]{};
  std::snprintf(panel_title, sizeof(panel_title), "%s (%d)", title, static_cast<int>(rows.size()));
  mono::begin_panel(panel_title, { 0.0f, 0.0f });

  if (rows.empty()) {
    ImGui::TextUnformatted("No players");
  } else {
    for (const auto& row : rows) {
      draw_player_row_box(row);
    }
  }
  mono::end_panel();
}

inline void draw_player_window_content() {
  if (engine == nullptr || !engine->is_connected() || !engine->is_in_game()) {
    ImGui::PushStyleColor(ImGuiCol_Text, k_text_soft);
    ImGui::Dummy(ImVec2(0.0f, scaled(20.0f)));
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
  auto sort_by_name = [](std::vector<player_row>& v) {
    std::sort(v.begin(), v.end(), [](const player_row& a, const player_row& b) {
      return a.name < b.name;
    });
  };
  sort_by_name(blu_rows);
  sort_by_name(red_rows);
  sort_by_name(other_rows);

  const float total_height = ImGui::GetContentRegionAvail().y;
  const float spectator_height = other_rows.empty() ? 0.0f : scaled(100.0f);
  const float top_height = total_height - spectator_height - (spectator_height > 0.0f ? scaled(6.0f) : 0.0f);
  const float column_width = (ImGui::GetContentRegionAvail().x - scaled(6.0f)) * 0.5f;

  ImGui::BeginChild("##pm_teams", ImVec2(0.0f, top_height), false, ImGuiWindowFlags_NoBackground);
  ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
  ImGui::BeginChild("##pm_blu_wrap", ImVec2(column_width, 0.0f), false, ImGuiWindowFlags_NoBackground);
  draw_player_team_column("BLU", ImVec4(0.45f, 0.65f, 0.95f, 1.0f), blu_rows);
  ImGui::EndChild();
  ImGui::SameLine(0.0f, scaled(6.0f));
  ImGui::BeginChild("##pm_red_wrap", ImVec2(column_width, 0.0f), false, ImGuiWindowFlags_NoBackground);
  draw_player_team_column("RED", ImVec4(0.95f, 0.45f, 0.45f, 1.0f), red_rows);
  ImGui::EndChild();
  ImGui::EndChild();

  if (!other_rows.empty()) {
    ImGui::Dummy(ImVec2(0.0f, scaled(6.0f)));
    ImGui::BeginChild("##pm_spec_wrap", ImVec2(0.0f, spectator_height), false, ImGuiWindowFlags_NoBackground);
    draw_player_team_column("SPECTATORS", k_text_soft, other_rows);
    ImGui::EndChild();
  }
}

}
#endif
