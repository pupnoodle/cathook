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
#include "games/tf2/sdk/interfaces/steam_friends.hpp"
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
  bool steam_friend = false;
  bool ipc_friend = false;
};

inline std::vector<player_row> collect_player_rows() {
  std::vector<player_row> rows{};
  if (engine == nullptr || entity_list == nullptr) {
    return rows;
  }
  if (!engine->is_connected() || !engine->is_in_game() || engine->is_drawing_loading_image()) {
    return rows;
  }

  const int local_index = engine->get_localplayer_index();
  const int max_entities = entity_list->get_max_entities();
  rows.reserve(32);
  for (int index = 1; index <= max_entities; ++index) {
    auto* player = entity_list->player_from_index(index);
    if (player == nullptr || player->get_class_id() != class_id::PLAYER) {
      continue;
    }

    player_info pinfo{};
    if (!engine->get_player_info(index, &pinfo)) {
      continue;
    }

    player_row row{};
    row.entity_index = index;
    row.user_id = pinfo.user_id;
    row.account_id = static_cast<std::uint32_t>(pinfo.friends_id);
    row.name = pinfo.name;
    row.team = player->get_team();
    row.alive = player->is_alive();
    row.local = (index == local_index);
    row.fake = pinfo.fakeplayer;

    if (row.account_id != 0) {
      row.state = cathook::core::players::state_for(row.account_id);
      row.ipc_friend = cat_ipc::client::is_local_ipc_friend(row.account_id);
      if (steam_friends != nullptr) {
        row.steam_friend = steam_friends->is_friend(pinfo.friends_id);
      }
    }

    rows.push_back(std::move(row));
  }

  return rows;
}

inline ImVec4 player_team_color(tf_team team, bool alive) {
  ImVec4 base{};
  switch (team) {
    case tf_team::RED:
      base = ImVec4(0.74f, 0.27f, 0.27f, 1.0f);
      break;
    case tf_team::BLU:
      base = ImVec4(0.27f, 0.47f, 0.74f, 1.0f);
      break;
    case tf_team::SPECTATOR:
      base = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
      break;
    default:
      base = ImVec4(0.45f, 0.45f, 0.45f, 1.0f);
      break;
  }
  if (!alive) {
    base.x *= 0.55f;
    base.y *= 0.55f;
    base.z *= 0.55f;
  }
  return base;
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

inline ImVec4 player_state_color(cathook::core::players::player_state state) {
  using cathook::core::players::player_state;
  switch (state) {
    case player_state::friend_state: return ImVec4(0.32f, 0.78f, 0.45f, 1.0f);
    case player_state::ignored: return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
    case player_state::cheater: return k_danger;
    case player_state::ipc: return k_accent;
    case player_state::textmode: return ImVec4(0.66f, 0.51f, 0.83f, 1.0f);
    case player_state::party: return ImVec4(0.32f, 0.78f, 0.78f, 1.0f);
    default: return k_text_soft;
  }
}

inline void draw_player_row_box(const player_row& row) {
  using namespace cathook::core::players;

  ImGuiWindow* window = ImGui::GetCurrentWindow();
  if (window == nullptr || window->SkipItems) return;

  const ImGuiID id = ImGui::GetID((void*)(intptr_t)(row.entity_index + 1));
  const float row_height = 28.0f;
  const float row_width = ImGui::GetContentRegionAvail().x;
  const ImVec2 pos = window->DC.CursorPos;
  const ImRect bb(pos, ImVec2(pos.x + row_width, pos.y + row_height));

  ImGui::ItemSize(ImVec2(row_width, row_height), 0.0f);
  if (!ImGui::ItemAdd(bb, id)) return;

  bool hovered = false, held = false;
  ImGui::ButtonBehavior(bb, id, &hovered, &held, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

  const ImVec4 team_tint = player_team_color(row.team, row.alive);
  ImVec4 fill = team_tint;
  fill.x = fill.x * 0.35f + k_bg_panel.x * 0.65f;
  fill.y = fill.y * 0.35f + k_bg_panel.y * 0.65f;
  fill.z = fill.z * 0.35f + k_bg_panel.z * 0.65f;
  fill.w = 0.95f;
  if (hovered) {
    fill.x = ImMin(fill.x * 1.18f, 1.0f);
    fill.y = ImMin(fill.y * 1.18f, 1.0f);
    fill.z = ImMin(fill.z * 1.18f, 1.0f);
  }

  ImDrawList* dl = window->DrawList;
  dl->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(fill), 0.0f);
  dl->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(k_line), 0.0f, 0, 1.0f);
  dl->AddRectFilled(bb.Min, ImVec2(bb.Min.x + 3.0f, bb.Max.y), ImGui::GetColorU32(team_tint), 0.0f);

  float text_x = bb.Min.x + 8.0f;
  const float text_y = bb.GetCenter().y - (font_regular()->LegacySize * 0.5f);

  if (row.local) {
    dl->AddText(font_regular(), font_regular()->LegacySize, ImVec2(text_x, text_y), ImGui::GetColorU32(k_accent), "@");
    text_x += 12.0f;
  } else if (row.steam_friend || row.state == player_state::friend_state || row.ipc_friend) {
    dl->AddText(font_regular(), font_regular()->LegacySize, ImVec2(text_x, text_y), ImGui::GetColorU32(ImVec4(0.32f, 0.78f, 0.45f, 1.0f)), "+");
    text_x += 12.0f;
  }

  const std::string name = ellipsize_text(row.name.c_str(), bb.Max.x - text_x - 60.0f);
  dl->AddText(font_regular(), font_regular()->LegacySize, ImVec2(text_x, text_y), ImGui::GetColorU32(k_text), name.c_str());

  const char* tag_label = player_state_label(row.state);
  if (tag_label != nullptr && tag_label[0] != '\0') {
    const ImVec4 tag_color = player_state_color(row.state);
    const ImVec2 tag_size = font_regular()->CalcTextSizeA(font_regular()->LegacySize, FLT_MAX, 0.0f, tag_label);
    const float tag_w = tag_size.x + 8.0f;
    const float tag_h = font_regular()->LegacySize + 4.0f;
    const ImVec2 tag_min(bb.Max.x - tag_w - 6.0f, bb.GetCenter().y - tag_h * 0.5f);
    const ImVec2 tag_max(tag_min.x + tag_w, tag_min.y + tag_h);
    dl->AddRectFilled(tag_min, tag_max, ImGui::GetColorU32(with_alpha(tag_color, 0.85f)), 0.0f);
    dl->AddText(font_regular(), font_regular()->LegacySize, ImVec2(tag_min.x + 4.0f, tag_min.y + 2.0f), ImGui::GetColorU32(k_text), tag_label);
  }

  if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
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
        std::system(cmd);
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
          // spec_player needs '#' prefix for userid, else it parses as partial name match
          char cmd[64]{};
          std::snprintf(cmd, sizeof(cmd), "spec_player \"#%d\"", row.user_id);
          engine->client_cmd_unrestricted(cmd);
        }
      }
      ImGui::Separator();
    }

    if (!row.fake && row.account_id != 0) {
      if (ImGui::MenuItem("Mark as friend", nullptr, row.state == player_state::friend_state)) {
        cathook::core::players::set_state(
          row.account_id,
          row.state == player_state::friend_state ? player_state::default_state : player_state::friend_state,
          row.name);
      }
      if (ImGui::MenuItem("Mark as cheater", nullptr, row.state == player_state::cheater)) {
        cathook::core::players::set_state(
          row.account_id,
          row.state == player_state::cheater ? player_state::default_state : player_state::cheater,
          row.name);
      }
      if (ImGui::MenuItem("Ignore", nullptr, row.state == player_state::ignored)) {
        cathook::core::players::set_state(
          row.account_id,
          row.state == player_state::ignored ? player_state::default_state : player_state::ignored,
          row.name);
      }
      if (row.state != player_state::default_state && !row.ipc_friend) {
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
  constexpr float header_h = 26.0f;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 4.0f));
  ImGui::BeginChild(title, ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_NoBackground);

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 child_pos = ImGui::GetWindowPos();
  const ImVec2 child_size = ImGui::GetWindowSize();
  const ImVec2 child_max(child_pos.x + child_size.x, child_pos.y + child_size.y);
  dl->AddRectFilled(child_pos, child_max, ImGui::GetColorU32(k_bg_panel), 0.0f);
  dl->AddRect(child_pos, child_max, ImGui::GetColorU32(k_line), 0.0f, 0, 1.0f);

  // header bar
  const ImVec2 header_min = child_pos;
  const ImVec2 header_max(child_max.x, child_pos.y + header_h);
  dl->AddRectFilled(header_min, header_max, ImGui::GetColorU32(with_alpha(title_color, 0.18f)), 0.0f);
  dl->AddLine(ImVec2(header_min.x, header_max.y), ImVec2(header_max.x, header_max.y), ImGui::GetColorU32(k_line), 1.0f);
  dl->AddRectFilled(header_min, ImVec2(header_min.x + 3.0f, header_max.y), ImGui::GetColorU32(title_color), 0.0f);

  char header_label[64]{};
  std::snprintf(header_label, sizeof(header_label), "%s  (%d)", title, static_cast<int>(rows.size()));
  ImFont* hfont = font_regular_large();
  const ImVec2 hsize = hfont->CalcTextSizeA(hfont->LegacySize, FLT_MAX, 0.0f, header_label);
  dl->AddText(hfont, hfont->LegacySize,
    ImVec2(header_min.x + 10.0f, header_min.y + (header_h - hsize.y) * 0.5f),
    ImGui::GetColorU32(title_color), header_label);

  ImGui::SetCursorPos(ImVec2(8.0f, header_h + 6.0f));
  ImGui::BeginChild("##list", ImVec2(child_size.x - 16.0f, child_size.y - header_h - 12.0f), false,
    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);

  if (rows.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, k_text_soft);
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    const char* msg = "No players";
    const ImVec2 msg_size = ImGui::CalcTextSize(msg);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - msg_size.x) * 0.5f);
    ImGui::TextUnformatted(msg);
    ImGui::PopStyleColor();
  } else {
    for (const auto& row : rows) {
      draw_player_row_box(row);
    }
  }

  ImGui::EndChild();
  ImGui::EndChild();
  ImGui::PopStyleVar(2);
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
  auto sort_by_name = [](std::vector<player_row>& v) {
    std::sort(v.begin(), v.end(), [](const player_row& a, const player_row& b) {
      return a.name < b.name;
    });
  };
  sort_by_name(blu_rows);
  sort_by_name(red_rows);
  sort_by_name(other_rows);

  const float total_height = ImGui::GetContentRegionAvail().y;
  const float spectator_height = other_rows.empty() ? 0.0f : 100.0f;
  const float top_height = total_height - spectator_height - (spectator_height > 0.0f ? 6.0f : 0.0f);
  const float column_width = (ImGui::GetContentRegionAvail().x - 6.0f) * 0.5f;

  ImGui::BeginChild("##pm_teams", ImVec2(0.0f, top_height), false, ImGuiWindowFlags_NoBackground);
  ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
  ImGui::BeginChild("##pm_blu_wrap", ImVec2(column_width, 0.0f), false, ImGuiWindowFlags_NoBackground);
  draw_player_team_column("BLU", ImVec4(0.45f, 0.65f, 0.95f, 1.0f), blu_rows);
  ImGui::EndChild();
  ImGui::SameLine(0.0f, 6.0f);
  ImGui::BeginChild("##pm_red_wrap", ImVec2(column_width, 0.0f), false, ImGuiWindowFlags_NoBackground);
  draw_player_team_column("RED", ImVec4(0.95f, 0.45f, 0.45f, 1.0f), red_rows);
  ImGui::EndChild();
  ImGui::EndChild();

  if (!other_rows.empty()) {
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::BeginChild("##pm_spec_wrap", ImVec2(0.0f, spectator_height), false, ImGuiWindowFlags_NoBackground);
    draw_player_team_column("SPECTATORS", k_text_soft, other_rows);
    ImGui::EndChild();
  }
}

} // namespace cat_menu

#endif // PLAYER_WINDOW_HPP
