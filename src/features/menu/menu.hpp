/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/menu/menu.hpp
 |  Y  |   autor: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef MENU_HPP
#define MENU_HPP

#include "config.hpp"
#include "binds.hpp"

#include "core/config/config_store.hpp"
#include "core/detach.hpp"
#include "core/ipc/ipc_client.hpp"
#include "core/logger.hpp"
#include "features/automation/region_selector/region_selector.hpp"
#include "imgui/dearimgui.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_stdlib.h"
#include "core/render/bytes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <SDL2/SDL_mouse.h>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

inline static SDL_Window* sdl_window = NULL;
inline static bool menu_focused = false;
inline static char config_name[64] = "default";
inline static int selected_config = 0;
inline static ImFont* menu_font_regular = nullptr;
inline static ImFont* menu_font_bold_small = nullptr;
inline static ImFont* menu_font_regular_large = nullptr;

namespace cat_menu
{

constexpr float k_ui_scale{ 1.18f };
constexpr ImVec2 k_menu_size{ 708.0f, 555.0f };
constexpr float k_title_height{ 24.0f };
constexpr float k_tab_height{ 19.0f };
constexpr float k_subtab_height{ 19.0f };
constexpr float k_tab_strip_height{ 20.0f };
constexpr float k_subtab_strip_height{ 20.0f };
constexpr float k_strip_inset_x{ 0.0f };
constexpr float k_tab_strip_padding_x{ 0.0f };
constexpr float k_gap{ 5.0f };
constexpr float k_panel_padding_x{ 14.0f };
constexpr float k_panel_padding_y{ 8.0f };
constexpr float k_content_padding{ 5.0f };
constexpr float k_content_height{ 531.0f };

constexpr ImVec4 k_bg_outer{ 0.114f, 0.184f, 0.251f, 0.985f };
constexpr ImVec4 k_bg_nav{ 0.114f, 0.184f, 0.251f, 0.985f };
constexpr ImVec4 k_bg_header{ 0.114f, 0.184f, 0.251f, 1.000f };
constexpr ImVec4 k_bg_panel{ 0.114f, 0.184f, 0.251f, 0.975f };
constexpr ImVec4 k_bg_panel_header{ 0.114f, 0.184f, 0.251f, 1.000f };
constexpr ImVec4 k_frame{ 0.114f, 0.184f, 0.251f, 1.000f };
constexpr ImVec4 k_frame_hover{ 0.160f, 0.230f, 0.320f, 1.000f };
constexpr ImVec4 k_line{ 0.267f, 0.392f, 0.596f, 1.000f };
constexpr ImVec4 k_text{ 1.000f, 1.000f, 1.000f, 1.000f };
constexpr ImVec4 k_text_muted{ 0.800f, 0.800f, 0.800f, 1.000f };
constexpr ImVec4 k_text_soft{ 0.667f, 0.667f, 0.667f, 1.000f };
constexpr ImVec4 k_accent{ 0.259f, 0.737f, 0.600f, 1.000f };
constexpr ImVec4 k_accent_soft{ 0.267f, 0.392f, 0.596f, 1.000f };
constexpr ImVec4 k_danger{ 0.84f, 0.30f, 0.32f, 1.0f };

enum tab_id
{
  tab_aimbot,
  tab_visuals,
  tab_misc,
  tab_cat_bot,
  tab_debug,
  tab_config
};

enum visuals_subtab_id
{
  visuals_subtab_esp,
  visuals_subtab_chams,
  visuals_subtab_glow,
  visuals_subtab_other
};

inline float lerp_value(float current, float target, float speed) {
  return ImLerp(current, target, ImMin(ImGui::GetIO().DeltaTime * speed, 1.0f));
}

inline ImVec4 lerp_color(const ImVec4& a, const ImVec4& b, float t) {
  return ImVec4(ImLerp(a.x, b.x, t), ImLerp(a.y, b.y, t), ImLerp(a.z, b.z, t), ImLerp(a.w, b.w, t));
}

inline ImVec4 with_alpha(ImVec4 color, float alpha) {
  color.w *= alpha;
  return color;
}

inline float storage_anim(ImGuiID id, const char* suffix, float target, float speed) {
  ImGuiStorage* storage = ImGui::GetStateStorage();
  ImGuiID key = ImHashStr(suffix, 0, id);
  float current = storage->GetFloat(key, target);
  float updated = lerp_value(current, target, speed);
  storage->SetFloat(key, updated);
  return updated;
}

inline std::string ellipsize_text(const char* text, float max_width) {
  if (text == nullptr) {
    return {};
  }

  std::string result = text;
  if (result.empty() || ImGui::CalcTextSize(result.c_str()).x <= max_width) {
    return result;
  }

  constexpr const char* ellipsis = "...";
  while (!result.empty() && ImGui::CalcTextSize((result + ellipsis).c_str()).x > max_width) {
    result.pop_back();
  }

  if (result.empty()) {
    return ellipsis;
  }

  result += ellipsis;
  return result;
}

inline float slider_value_box_width(const char* format, float minimum, float maximum, float value) {
  char min_buffer[32]{};
  char max_buffer[32]{};
  char value_buffer[32]{};
  ImFormatString(min_buffer, IM_ARRAYSIZE(min_buffer), format, minimum);
  ImFormatString(max_buffer, IM_ARRAYSIZE(max_buffer), format, maximum);
  ImFormatString(value_buffer, IM_ARRAYSIZE(value_buffer), format, value);

  const float text_width = ImMax(
    ImGui::CalcTextSize(min_buffer).x,
    ImMax(ImGui::CalcTextSize(max_buffer).x, ImGui::CalcTextSize(value_buffer).x));
  return ImMax(64.0f, text_width + 18.0f);
}

inline float slider_value_box_width(const char* format, int minimum, int maximum, int value) {
  char min_buffer[32]{};
  char max_buffer[32]{};
  char value_buffer[32]{};
  ImFormatString(min_buffer, IM_ARRAYSIZE(min_buffer), format, minimum);
  ImFormatString(max_buffer, IM_ARRAYSIZE(max_buffer), format, maximum);
  ImFormatString(value_buffer, IM_ARRAYSIZE(value_buffer), format, value);

  const float text_width = ImMax(
    ImGui::CalcTextSize(min_buffer).x,
    ImMax(ImGui::CalcTextSize(max_buffer).x, ImGui::CalcTextSize(value_buffer).x));
  return ImMax(64.0f, text_width + 18.0f);
}

inline void field_label(const char* label) {
  ImGui::PushStyleColor(ImGuiCol_Text, k_text_soft);
  ImGui::TextUnformatted(label);
  ImGui::PopStyleColor();
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 6.0f);
}

inline ImFont* font_regular() {
  return menu_font_regular ? menu_font_regular : ImGui::GetFont();
}

inline ImFont* font_bold_small() {
  return menu_font_bold_small ? menu_font_bold_small : font_regular();
}

inline ImFont* font_regular_large() {
  return menu_font_regular_large ? menu_font_regular_large : font_regular();
}

struct panel_layout_state
{
  float header_height{ 0.0f };
  ImVec2 border_min{};
  ImVec2 border_max{};
  ImVec2 title_bg_min{};
  ImVec2 title_bg_max{};
  ImVec2 title_pos{};
  std::string title{};
};

inline std::filesystem::path assets_font_directory() {
  return "/opt/cathook/assets/fonts";
}

inline std::vector<std::string>& available_font_names() {
  static std::vector<std::string> names{};
  static bool loaded = false;

  if (loaded) {
    return names;
  }

  loaded = true;
  names.clear();
  names.emplace_back("Default");

  std::error_code error{};
  const std::filesystem::path font_dir = assets_font_directory();
  if (!std::filesystem::exists(font_dir, error)) {
    return names;
  }

  for (const auto& entry : std::filesystem::directory_iterator{ font_dir, error }) {
    if (error || !entry.is_regular_file(error)) {
      continue;
    }

    const std::filesystem::path path = entry.path();
    if (path.extension() == ".ttf" || path.extension() == ".otf") {
      names.emplace_back(path.filename().string());
    }
  }

  std::sort(names.begin() + 1, names.end());
  return names;
}

inline std::vector<panel_layout_state>& current_panel_layout_stack() {
  static std::vector<panel_layout_state> stack{};
  return stack;
}

inline void ensure_fonts() {
  ImGuiIO& io = ImGui::GetIO();
  static bool loaded_custom_font = false;
  static std::string loaded_font_name{};
  const bool wants_custom_font = config.misc.menu.use_custom_font && !config.misc.menu.custom_font.empty();
  const std::string wanted_font_name = wants_custom_font ? config.misc.menu.custom_font : std::string{};

  if (menu_font_regular != nullptr && menu_font_bold_small != nullptr && menu_font_regular_large != nullptr &&
      loaded_custom_font == wants_custom_font && loaded_font_name == wanted_font_name) {
    return;
  }

  menu_font_regular = nullptr;
  menu_font_bold_small = nullptr;
  menu_font_regular_large = nullptr;

  io.Fonts->Clear();

  ImFontConfig font_config{};
  font_config.FontDataOwnedByAtlas = false;
  font_config.OversampleH = 4;
  font_config.OversampleV = 4;
  font_config.PixelSnapH = false;

  if (wants_custom_font) {
    const std::filesystem::path font_path = assets_font_directory() / std::filesystem::path{ config.misc.menu.custom_font }.filename();
    const std::string font_path_string = font_path.string();
    ImFontConfig file_font_config = font_config;
    file_font_config.FontDataOwnedByAtlas = true;
    file_font_config.Flags |= ImFontFlags_NoLoadError;
    menu_font_regular = io.Fonts->AddFontFromFileTTF(font_path_string.c_str(), 14.0f, &file_font_config);
    menu_font_bold_small = io.Fonts->AddFontFromFileTTF(font_path_string.c_str(), 14.0f, &file_font_config);
    menu_font_regular_large = io.Fonts->AddFontFromFileTTF(font_path_string.c_str(), 16.0f, &file_font_config);

    if (menu_font_regular == nullptr) {
      cathook::core::log_raw("failed to load menu font: %s\n", font_path_string.c_str());
    }
  }

  if (menu_font_regular == nullptr) {
    menu_font_regular = io.Fonts->AddFontFromMemoryTTF(font_medium_bin, sizeof(font_medium_bin), 14.0f, &font_config);
  }
  if (menu_font_bold_small == nullptr) {
    menu_font_bold_small = io.Fonts->AddFontFromMemoryTTF(font_bold_bin, sizeof(font_bold_bin), 14.0f, &font_config);
  }
  if (menu_font_regular_large == nullptr) {
    menu_font_regular_large = io.Fonts->AddFontFromMemoryTTF(font_bold_bin, sizeof(font_bold_bin), 16.0f, &font_config);
  }

  if (!menu_font_regular) {
    menu_font_regular = io.Fonts->AddFontDefault();
  }
  if (!menu_font_bold_small) {
    menu_font_bold_small = menu_font_regular;
  }
  if (!menu_font_regular_large) {
    menu_font_regular_large = menu_font_regular;
  }

  io.FontDefault = menu_font_regular;
  loaded_custom_font = wants_custom_font;
  loaded_font_name = wanted_font_name;
}

inline bool accent_button(const char* label, const ImVec2& size = ImVec2(0.0f, 26.0f), const bool danger = false) {
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  if (window == nullptr || window->SkipItems || label == nullptr) return false;

  const ImGuiID id = window->GetID(label);
  const char* label_end = ImGui::FindRenderedTextEnd(label);
  const ImVec2 label_size = ImGui::CalcTextSize(label, label_end, true);
  const ImVec2 pos = window->DC.CursorPos;
  const ImVec2 button_size = ImGui::CalcItemSize(size, label_size.x + 20.0f, 18.0f);
  const ImRect bb(pos, ImVec2(pos.x + button_size.x, pos.y + button_size.y));

  ImGui::ItemSize(button_size, 3.0f);
  if (!ImGui::ItemAdd(bb, id)) return false;

  bool hovered = false, held = false;
  bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

  float hover_anim = storage_anim(id, "button_hover", hovered ? 1.0f : 0.0f, 12.0f);
  float held_anim = storage_anim(id, "button_held", held ? 1.0f : 0.0f, 14.0f);

  ImVec4 target = danger ? k_danger : k_accent;
  ImVec4 fill = held ? target : hovered ? with_alpha(target, 0.35f) : k_frame;
  ImVec4 border = hovered ? target : k_line;

  window->DrawList->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(fill), 0.0f);
  window->DrawList->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(border), 0.0f, 0, 1.0f);
  window->DrawList->AddText(
    font_regular(),
    font_regular()->LegacySize,
    ImVec2(bb.GetCenter().x - (label_size.x * 0.5f), bb.GetCenter().y - (label_size.y * 0.5f)),
    ImGui::GetColorU32(k_text),
    label,
    label_end);

  return pressed;
}

inline bool list_row(const char* label, bool selected, const ImVec2& size = ImVec2(0.0f, 24.0f)) {
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  if (window == nullptr || window->SkipItems || label == nullptr) return false;

  const ImGuiID id = window->GetID(label);
  const ImVec2 label_size = ImGui::CalcTextSize(label, nullptr, true);
  const ImVec2 pos = window->DC.CursorPos;
  const float row_width = ImMax(0.0f, ImGui::GetContentRegionAvail().x - 10.0f);
  const ImVec2 row_size = ImGui::CalcItemSize(size, row_width, 24.0f);
  const ImRect bb(pos, ImVec2(pos.x + row_size.x, pos.y + row_size.y));

  ImGui::ItemSize(row_size, 2.0f);
  if (!ImGui::ItemAdd(bb, id)) return false;

  bool hovered = false, held = false;
  const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
  const float hover_anim = storage_anim(id, "list_hover", hovered ? 1.0f : 0.0f, 12.0f);
  const float select_anim = storage_anim(id, "list_select", selected ? 1.0f : 0.0f, 12.0f);

  const ImVec4 fill = selected ? k_accent_soft : hovered ? with_alpha(k_frame_hover, 0.85f) : with_alpha(k_frame, 0.55f);
  const ImVec4 border = lerp_color(k_line, k_accent, select_anim + (hover_anim * 0.25f));
  const ImVec4 text_color = selected ? k_text : hovered ? k_text : k_text_muted;

  window->DrawList->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(fill), 0.0f);
  window->DrawList->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(border), 0.0f, 0, 1.0f);
  if (selected) {
    window->DrawList->AddRectFilled(bb.Min, ImVec2(bb.Min.x + 3.0f, bb.Max.y), ImGui::GetColorU32(k_accent), 0.0f);
  }
  const std::string preview = ellipsize_text(label, row_size.x - 16.0f);
  const ImVec2 preview_size = ImGui::CalcTextSize(preview.c_str());
  window->DrawList->AddText(font_regular(), font_regular()->LegacySize, ImVec2(bb.Min.x + 8.0f, bb.GetCenter().y - (preview_size.y * 0.5f)), ImGui::GetColorU32(text_color), preview.c_str());
  return pressed;
}

inline void begin_panel(const char* name, const ImVec2& size) {
  ImGui::BeginChild((std::string(name) + "##panel_shell").c_str(), size, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetWindowPos();
  ImVec2 panel_size = ImGui::GetWindowSize();
  ImVec2 max = ImVec2(pos.x + panel_size.x, pos.y + panel_size.y);

  const ImVec2 text_size = font_regular()->CalcTextSizeA(font_regular()->LegacySize, FLT_MAX, 0.0f, name);
  const float title_y = pos.y + 1.0f;
  const float border_inset = 6.0f;
  const float title_padding_x = 4.0f;
  const float title_start = pos.x + 8.0f;
  const ImVec2 title_bg_min(title_start - title_padding_x, title_y);
  const ImVec2 title_bg_max(title_start + text_size.x + title_padding_x, title_y + text_size.y);

  current_panel_layout_stack().push_back({
    .header_height = text_size.y + 8.0f,
    .border_min = ImVec2(pos.x + border_inset, title_bg_min.y + (text_size.y * 0.5f)),
    .border_max = ImVec2(max.x - border_inset, max.y - 3.0f),
    .title_bg_min = title_bg_min,
    .title_bg_max = title_bg_max,
    .title_pos = ImVec2(title_start, title_y),
    .title = name
  });

  draw_list->AddRectFilled(pos, max, ImGui::GetColorU32(k_bg_panel), 0.0f);

  ImGui::SetCursorPos(ImVec2(11.0f, text_size.y + 8.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(k_panel_padding_x, k_panel_padding_y));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7.0f, 5.0f));
  ImGui::BeginChild(name, ImVec2(-1.0f, -1.0f), false, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

inline void begin_panel_at(const char* name, const ImVec2& position, const ImVec2& size) {
  ImGui::SetCursorPos(position);
  begin_panel(name, size);
}

inline void end_panel();
inline float end_panel_ex();

struct flow_layout_state
{
  ImVec2 origin{};
  float column_width{ 0.0f };
  float total_width{ 0.0f };
  std::vector<float> column_offsets{};
  bool active{ false };
};

inline flow_layout_state& current_flow_layout() {
  static flow_layout_state state{};
  return state;
}

inline void begin_flow_layout(const char* id, int column_count) {
  auto& state = current_flow_layout();
  state.origin = ImGui::GetCursorPos();
  if (column_count <= 1) {
    state.column_width = ImGui::GetContentRegionAvail().x;
  } else {
    state.column_width =
      (ImGui::GetContentRegionAvail().x - (k_gap * static_cast<float>(column_count - 1))) / static_cast<float>(column_count);
  }
  state.total_width = (state.column_width * static_cast<float>(column_count)) + (k_gap * static_cast<float>(column_count - 1));
  state.column_offsets.assign(column_count, 0.0f);
  state.active = true;
  ImGui::PushID(id);
}

template <typename draw_fn_t>
inline void flow_panel(const char* name, int column_index, float height, draw_fn_t&& draw_fn, const bool auto_fit = true) {
  auto& state = current_flow_layout();
  if (!state.active || column_index < 0 || column_index >= static_cast<int>(state.column_offsets.size())) {
    return;
  }

  ImGuiStorage* storage = ImGui::GetStateStorage();
  const ImGuiID height_key = ImHashStr("flow_panel_height", 0, ImGui::GetID(name));
  const float panel_height = auto_fit ? ImMax(height, storage->GetFloat(height_key, height)) : height;
  const ImVec2 position(
    state.origin.x + (static_cast<float>(column_index) * (state.column_width + k_gap)),
    state.origin.y + state.column_offsets[static_cast<size_t>(column_index)]);

  ImGui::SetCursorPos(position);
  begin_panel(name, ImVec2(state.column_width, panel_height));
  draw_fn();
  const float required_height = end_panel_ex();
  const float next_panel_height = auto_fit ? ImMax(height, ImCeil(required_height)) : height;
  storage->SetFloat(height_key, next_panel_height);

  state.column_offsets[static_cast<size_t>(column_index)] += panel_height + k_gap;
}

inline void end_flow_layout() {
  auto& state = current_flow_layout();
  float height = 0.0f;
  for (float offset : state.column_offsets) {
    height = ImMax(height, offset);
  }

  if (height > 0.0f) {
    height -= k_gap;
  }

  ImGui::SetCursorPos(state.origin);
  ImGui::Dummy(ImVec2(state.total_width, height));
  ImGui::PopID();
  state = {};
}

inline float end_panel_ex() {
  auto& stack = current_panel_layout_stack();
  const float content_height = ImGui::GetCursorPosY();
  panel_layout_state panel_state{};
  if (!stack.empty()) {
    panel_state = stack.back();
  }
  const float header_height = panel_state.header_height;
  const float required_height = header_height + content_height + k_panel_padding_y;

  ImGui::EndChild();
  if (!stack.empty()) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRect(panel_state.border_min, panel_state.border_max, ImGui::GetColorU32(k_line), 0.0f, 0, 1.0f);
    draw_list->AddRectFilled(panel_state.title_bg_min, panel_state.title_bg_max, ImGui::GetColorU32(k_bg_panel), 0.0f);
    draw_list->AddText(font_regular(), font_regular()->LegacySize, panel_state.title_pos, ImGui::GetColorU32(k_text), panel_state.title.c_str());
    stack.pop_back();
  }
  ImGui::PopStyleVar(2);
  ImGui::EndChild();
  return required_height;
}

inline void end_panel() {
  (void)end_panel_ex();
}

inline float column_width(const int column_count) {
  if (column_count <= 1) {
    return ImGui::GetContentRegionAvail().x;
  }

  return (ImGui::GetContentRegionAvail().x - (k_gap * static_cast<float>(column_count - 1))) / static_cast<float>(column_count);
}

inline void begin_column() {
  ImGui::BeginGroup();
}

inline void next_column() {
  ImGui::EndGroup();
  ImGui::SameLine(0.0f, k_gap);
  ImGui::BeginGroup();
}

inline void end_column() {
  ImGui::EndGroup();
}

inline void draw_tab_strip_frame(const ImVec2& pos, const ImVec2& size, bool draw_top_border, bool draw_bottom_border) {
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  const ImVec2 max(pos.x + size.x, pos.y + size.y);
  const ImU32 line_color = ImGui::GetColorU32(k_line);

  draw_list->AddRectFilled(pos, max, ImGui::GetColorU32(k_bg_nav), 0.0f);
  if (draw_bottom_border) {
    draw_list->AddLine(ImVec2(pos.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f), line_color, 1.0f);
  }
  if (draw_top_border) {
    draw_list->AddLine(pos, ImVec2(max.x, pos.y), line_color, 1.0f);
  }
}

inline int& current_tab_strip_button_index() {
  static int index = 0;
  return index;
}

inline void begin_tab_strip(const char* id, float height, bool draw_top_border, bool draw_bottom_border, float padding_x, bool apply_outer_inset = true) {
  const float available_width = ImGui::GetContentRegionAvail().x;
  const float outer_inset = apply_outer_inset ? k_strip_inset_x : 0.0f;
  const float strip_width = ImMax(0.0f, available_width - (outer_inset * 2.0f));

  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + outer_inset);
  ImGui::BeginChild(id, ImVec2(strip_width, height), false, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  draw_tab_strip_frame(ImGui::GetWindowPos(), ImGui::GetWindowSize(), draw_top_border, draw_bottom_border);
  current_tab_strip_button_index() = 0;
  ImGui::SetCursorPos(ImVec2(padding_x, 0.0f));
}

inline void end_tab_strip() {
  ImGui::EndChild();
}

inline ImVec4 tab_button_fill(int index, bool selected, bool hovered) {
  const ImVec4 odd_fill = with_alpha(k_frame_hover, 0.12f);
  const ImVec4 even_fill = with_alpha(k_frame_hover, 0.24f);

  if (selected) {
    return k_accent_soft;
  }

  if (hovered) {
    return with_alpha(k_frame_hover, 0.50f);
  }

  return (index % 2 == 0) ? odd_fill : even_fill;
}

inline bool tab_button_shared(const char* label, bool selected, float height, float width_padding) {
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  if (window == nullptr || window->SkipItems || label == nullptr) return false;

  const int tab_index = current_tab_strip_button_index()++;
  ImGuiID id = window->GetID(label);
  ImVec2 pos = window->DC.CursorPos;
  const ImVec2 text_size = font_regular()->CalcTextSizeA(font_regular()->LegacySize, FLT_MAX, 0.0f, label);
  const float max_width = ImMax(1.0f, window->WorkRect.Max.x - pos.x);
  const float desired_width = text_size.x + width_padding;
  ImVec2 size = ImGui::CalcItemSize(ImVec2(ImMin(desired_width, max_width), height), ImMin(desired_width, max_width), height);
  ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));

  ImGui::ItemSize(size, 0.0f);
  if (!ImGui::ItemAdd(bb, id)) return false;

  bool hovered = false, held = false;
  bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

  const ImVec4 fill = tab_button_fill(tab_index, selected, hovered);
  const ImVec4 text_color = selected ? k_text : hovered ? k_text : k_text_muted;
  window->DrawList->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(fill), 0.0f);
  if (selected) {
    window->DrawList->AddLine(ImVec2(bb.Min.x + 6.0f, bb.Max.y - 3.0f), ImVec2(bb.Max.x - 6.0f, bb.Max.y - 3.0f), ImGui::GetColorU32(k_line), 1.0f);
  } else if (hovered) {
    window->DrawList->AddLine(ImVec2(bb.Min.x + 6.0f, bb.Max.y - 3.0f), ImVec2(bb.Max.x - 6.0f, bb.Max.y - 3.0f), ImGui::GetColorU32(k_line), 1.0f);
  }
  window->DrawList->AddText(font_regular(), font_regular()->LegacySize, ImVec2(bb.GetCenter().x - (text_size.x * 0.5f), bb.GetCenter().y - (text_size.y * 0.5f)), ImGui::GetColorU32(text_color), label);

  return pressed;
}

inline bool nav_button(const char* label, bool selected) {
  return tab_button_shared(label, selected, k_tab_height, 18.0f);
}

inline bool subtab_button(const char* label, bool selected) {
  return tab_button_shared(label, selected, k_subtab_height, 18.0f);
}

inline bool checkbox(const char* label, bool* value) {
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  if (window == nullptr || window->SkipItems || label == nullptr || value == nullptr) return false;

  const ImGuiID id = window->GetID(label);
  const char* label_end = ImGui::FindRenderedTextEnd(label);
  const ImVec2 label_size = ImGui::CalcTextSize(label, nullptr, true);
  const ImVec2 pos = window->DC.CursorPos;
  constexpr float square_size = 14.0f;
  const ImRect box_bb(pos, ImVec2(pos.x + square_size, pos.y + square_size));
  const ImRect total_bb(pos, ImVec2(pos.x + square_size + ImGui::GetStyle().ItemInnerSpacing.x + label_size.x, pos.y + square_size));

  ImGui::ItemSize(total_bb);
  if (!ImGui::ItemAdd(total_bb, id)) return false;

  bool hovered = false, held = false;
  const bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);
  if (pressed) {
    *value = !*value;
    ImGui::MarkItemEdited(id);
  }

  const float checked_anim = storage_anim(id, "checkbox_checked", *value ? 1.0f : 0.0f, 12.0f);
  const float hover_anim = storage_anim(id, "checkbox_hover", hovered && !*value ? 1.0f : 0.0f, 12.0f);
  const ImVec4 frame_color = lerp_color(k_frame, k_frame_hover, hover_anim);
  const ImVec4 border_color = lerp_color(k_line, k_accent, checked_anim);

  window->DrawList->AddText(
    font_regular(),
    font_regular()->LegacySize,
    ImVec2(total_bb.Max.x - label_size.x, box_bb.GetCenter().y - (label_size.y * 0.5f)),
    ImGui::GetColorU32(k_text),
    label,
    label_end);
  window->DrawList->AddRectFilled(box_bb.Min, box_bb.Max, ImGui::GetColorU32(frame_color), ImGui::GetStyle().FrameRounding);
  if (checked_anim > 0.0f) {
    window->DrawList->AddRectFilled(
      ImVec2(box_bb.Min.x + 2.0f, box_bb.Min.y + 2.0f),
      ImVec2(box_bb.Max.x - 2.0f, box_bb.Max.y - 2.0f),
      ImGui::GetColorU32(with_alpha(k_accent, checked_anim)),
      ImGui::GetStyle().FrameRounding);
  }
  window->DrawList->AddRect(box_bb.Min, box_bb.Max, ImGui::GetColorU32(border_color), ImGui::GetStyle().FrameRounding, 0, 1.0f);
  cat_bind::bindable_checkbox(label, value, pressed);
  return pressed;
}

inline bool combo(const char* label, int* value, const char* const items[], const int item_count) {
  if (label == nullptr || value == nullptr || items == nullptr || item_count <= 0) return false;

  ImGuiWindow* window = ImGui::GetCurrentWindow();
  if (window == nullptr || window->SkipItems) return false;

  field_label(label);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

  const ImGuiID id = window->GetID(label);
  const ImVec2 pos = window->DC.CursorPos;
  const ImVec2 size = ImGui::CalcItemSize(ImVec2(ImGui::CalcItemWidth(), 18.0f), 72.0f, 18.0f);
  const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
  const ImGuiID popup_id = ImHashStr("combo_popup", 0, id);

  ImGui::ItemSize(size);
  if (!ImGui::ItemAdd(bb, id)) return false;

  bool hovered = false, held = false;
  const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
  const float hover_anim = storage_anim(id, "combo_hover", hovered ? 1.0f : 0.0f, 12.0f);
  const ImVec4 frame_color = lerp_color(k_frame, k_frame_hover, hover_anim);
  const char* preview = (*value >= 0 && *value < item_count && items[*value] != nullptr) ? items[*value] : "None";

  if (pressed) ImGui::OpenPopupEx(popup_id, ImGuiPopupFlags_None);

  const bool is_open = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);
  const float active_anim = storage_anim(id, "combo_active", is_open ? 1.0f : 0.0f, 12.0f);
  const ImVec4 border_color = lerp_color(k_line, k_accent, (hover_anim * 0.35f) + (active_anim * 0.55f));

  window->DrawList->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(border_color), 0.0f, 0, 1.0f);
  window->DrawList->AddText(font_regular(), font_regular()->LegacySize, ImVec2(bb.Min.x + 4.0f, bb.GetCenter().y - (ImGui::GetTextLineHeight() * 0.5f)), ImGui::GetColorU32(k_text), preview);

  const ImVec2 arrow_center(bb.Max.x - 10.0f, bb.GetCenter().y + 1.0f);
  window->DrawList->AddTriangleFilled(
    is_open ? ImVec2(arrow_center.x - 5.0f, arrow_center.y + 2.5f) : ImVec2(arrow_center.x - 5.0f, arrow_center.y - 2.5f),
    is_open ? ImVec2(arrow_center.x + 5.0f, arrow_center.y + 2.5f) : ImVec2(arrow_center.x + 5.0f, arrow_center.y - 2.5f),
    is_open ? ImVec2(arrow_center.x, arrow_center.y - 4.5f) : ImVec2(arrow_center.x, arrow_center.y + 4.5f),
    ImGui::GetColorU32(k_text_soft));

  bool changed = false;
  ImGui::SetNextWindowPos(ImVec2(bb.Min.x, bb.Max.y + 1.0f), ImGuiCond_Appearing);
  ImGui::SetNextWindowSize(ImVec2(bb.GetWidth(), 0.0f), ImGuiCond_Appearing);
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 4.0f));
  ImGui::PushStyleColor(ImGuiCol_PopupBg, k_bg_panel);
  ImGui::PushStyleColor(ImGuiCol_Border, k_line);

  if (ImGui::BeginPopupEx(popup_id, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
    for (int index = 0; index < item_count; ++index) {
      const char* item_label = items[index] != nullptr ? items[index] : "";
      ImGui::PushID(index);
      const ImVec2 item_pos = ImGui::GetCursorScreenPos();
      const ImVec2 item_size(bb.GetWidth() - 4.0f, 20.0f);
      const ImRect item_bb(item_pos, ImVec2(item_pos.x + item_size.x, item_pos.y + item_size.y));
      const bool selected = *value == index;
      const bool item_clicked = ImGui::InvisibleButton("##combo_item", item_size);
      const bool item_hovered = ImGui::IsItemHovered();
      const ImVec4 item_color = selected ? with_alpha(k_accent, 0.16f) : item_hovered ? with_alpha(k_accent, 0.08f) : k_bg_panel;

      ImGui::GetWindowDrawList()->AddRectFilled(item_bb.Min, item_bb.Max, ImGui::GetColorU32(item_color), 0.0f);
      ImGui::GetWindowDrawList()->AddText(font_regular(), font_regular()->LegacySize, ImVec2(item_bb.Min.x + 4.0f, item_bb.GetCenter().y - (ImGui::GetTextLineHeight() * 0.5f)), ImGui::GetColorU32(k_text), item_label);

      if (item_clicked) {
        *value = index;
        changed = true;
        ImGui::CloseCurrentPopup();
      }

      ImGui::PopID();
    }

    ImGui::EndPopup();
  }

  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(3);
  cat_bind::bindable_combo_int(label, value, changed, items, item_count);
  return changed;
}

inline bool multi_select_combo(const char* label, uint32_t* value_mask, const char* const items[], const uint32_t item_bits[], const int item_count) {
  if (label == nullptr || value_mask == nullptr || items == nullptr || item_bits == nullptr || item_count <= 0) return false;

  ImGuiWindow* window = ImGui::GetCurrentWindow();
  if (window == nullptr || window->SkipItems) return false;

  field_label(label);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

  std::string preview{};
  int selected_count = 0;
  for (int index = 0; index < item_count; ++index) {
    if ((*value_mask & item_bits[index]) == 0) {
      continue;
    }

    ++selected_count;
    if (!preview.empty()) {
      preview += ", ";
    }
    preview += items[index] != nullptr ? items[index] : "";
  }

  if (preview.empty()) {
    preview = "None";
  } else if (selected_count == item_count) {
    preview = "All";
  }

  const ImGuiID id = window->GetID(label);
  const ImVec2 pos = window->DC.CursorPos;
  const ImVec2 size = ImGui::CalcItemSize(ImVec2(ImGui::CalcItemWidth(), 18.0f), 72.0f, 18.0f);
  const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
  const ImGuiID popup_id = ImHashStr("multi_combo_popup", 0, id);

  ImGui::ItemSize(size);
  if (!ImGui::ItemAdd(bb, id)) return false;

  bool hovered = false, held = false;
  const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
  const float hover_anim = storage_anim(id, "multi_combo_hover", hovered ? 1.0f : 0.0f, 12.0f);
  const ImVec4 frame_color = lerp_color(k_frame, k_frame_hover, hover_anim);

  if (pressed) ImGui::OpenPopupEx(popup_id, ImGuiPopupFlags_None);

  const bool is_open = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);
  const float active_anim = storage_anim(id, "multi_combo_active", is_open ? 1.0f : 0.0f, 12.0f);
  const ImVec4 border_color = lerp_color(k_line, k_accent, (hover_anim * 0.35f) + (active_anim * 0.55f));

  window->DrawList->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(border_color), 0.0f, 0, 1.0f);

  std::string clipped_preview = preview;
  while (!clipped_preview.empty() && ImGui::CalcTextSize(clipped_preview.c_str()).x > bb.GetWidth() - 38.0f) {
    clipped_preview.pop_back();
  }
  if (clipped_preview.size() < preview.size()) {
    clipped_preview += "...";
  }
  window->DrawList->AddText(font_regular(), font_regular()->LegacySize, ImVec2(bb.Min.x + 4.0f, bb.GetCenter().y - (ImGui::GetTextLineHeight() * 0.5f)), ImGui::GetColorU32(k_text), clipped_preview.c_str());

  const ImVec2 arrow_center(bb.Max.x - 10.0f, bb.GetCenter().y + 1.0f);
  window->DrawList->AddTriangleFilled(
    is_open ? ImVec2(arrow_center.x - 5.0f, arrow_center.y + 2.5f) : ImVec2(arrow_center.x - 5.0f, arrow_center.y - 2.5f),
    is_open ? ImVec2(arrow_center.x + 5.0f, arrow_center.y + 2.5f) : ImVec2(arrow_center.x + 5.0f, arrow_center.y - 2.5f),
    is_open ? ImVec2(arrow_center.x, arrow_center.y - 4.5f) : ImVec2(arrow_center.x, arrow_center.y + 4.5f),
    ImGui::GetColorU32(k_text_soft));

  bool changed = false;
  ImGui::SetNextWindowPos(ImVec2(bb.Min.x, bb.Max.y + 3.0f), ImGuiCond_Appearing);
  ImGui::SetNextWindowSize(ImVec2(bb.GetWidth(), 0.0f), ImGuiCond_Appearing);
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 4.0f));
  ImGui::PushStyleColor(ImGuiCol_PopupBg, k_bg_panel);
  ImGui::PushStyleColor(ImGuiCol_Border, k_line);

  if (ImGui::BeginPopupEx(popup_id, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
    for (int index = 0; index < item_count; ++index) {
      const char* item_label = items[index] != nullptr ? items[index] : "";
      ImGui::PushID(index);
      const ImVec2 item_pos = ImGui::GetCursorScreenPos();
      const ImVec2 item_size(bb.GetWidth() - 4.0f, 20.0f);
      const ImRect item_bb(item_pos, ImVec2(item_pos.x + item_size.x, item_pos.y + item_size.y));
      const bool selected = (*value_mask & item_bits[index]) != 0;
      const bool item_clicked = ImGui::InvisibleButton("##multi_combo_item", item_size);
      const bool item_hovered = ImGui::IsItemHovered();
      const ImVec4 item_color = selected ? with_alpha(k_accent, 0.16f) : item_hovered ? with_alpha(k_accent, 0.08f) : k_bg_panel;

      ImGui::GetWindowDrawList()->AddRectFilled(item_bb.Min, item_bb.Max, ImGui::GetColorU32(item_color), 0.0f);
      ImGui::GetWindowDrawList()->AddText(font_regular(), font_regular()->LegacySize, ImVec2(item_bb.Min.x + 4.0f, item_bb.GetCenter().y - (ImGui::GetTextLineHeight() * 0.5f)), ImGui::GetColorU32(k_text), item_label);

      if (item_clicked) {
        *value_mask ^= item_bits[index];
        changed = true;
      }

      ImGui::PopID();
    }

    ImGui::EndPopup();
  }

  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(3);
  return changed;
}

inline bool slider_float(const char* label, float* value, float minimum, float maximum, const char* format) {
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  if (window == nullptr || window->SkipItems || label == nullptr || value == nullptr || format == nullptr) return false;

  ImGuiContext& g = *GImGui;
  field_label(label);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
  const ImGuiID id = window->GetID(label);
  const ImVec2 pos = window->DC.CursorPos;
  const float available_width = ImGui::CalcItemWidth();
  const float track_width = ImMax(120.0f, available_width);
  const ImRect field_bb(pos, ImVec2(pos.x + track_width, pos.y + 24.0f));
  const ImRect total_bb(pos, field_bb.Max);
  ImRect grab_bb{};

  ImGui::ItemSize(total_bb);
  if (!ImGui::ItemAdd(total_bb, id)) return false;

  char value_buffer[32]{};
  ImFormatString(value_buffer, IM_ARRAYSIZE(value_buffer), format, *value);
  const ImVec2 value_size = ImGui::CalcTextSize(value_buffer);
  const float value_width = slider_value_box_width(format, minimum, maximum, *value);
  const ImRect value_bb(ImVec2(field_bb.Max.x - value_width - 4.0f, field_bb.Min.y + 3.0f), ImVec2(field_bb.Max.x - 4.0f, field_bb.Max.y - 3.0f));
  const ImRect track_bb(ImVec2(field_bb.Min.x + 8.0f, field_bb.Min.y + 11.0f), ImVec2(value_bb.Min.x - 8.0f, field_bb.Min.y + 13.0f));

  const bool hovered = ImGui::ItemHoverable(field_bb, id, ImGuiItemFlags_None);
  const bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  if (clicked || g.NavActivateId == id) {
    if (clicked) ImGui::SetKeyOwner(ImGuiKey_MouseLeft, id);
    ImGui::SetActiveID(id, window);
    ImGui::SetFocusID(id, window);
    ImGui::FocusWindow(window);
    g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
  }

  const bool changed = ImGui::SliderBehavior(track_bb, id, ImGuiDataType_Float, value, &minimum, &maximum, format, ImGuiSliderFlags_None, &grab_bb);
  if (changed) ImGui::MarkItemEdited(id);

  const float hover_anim = storage_anim(id, "slider_hover", hovered ? 1.0f : 0.0f, 12.0f);
  const float active_anim = storage_anim(id, "slider_active", g.ActiveId == id ? 1.0f : 0.0f, 12.0f);
  const ImVec4 field_color = lerp_color(k_frame, k_frame_hover, hover_anim * 0.6f);
  const ImVec4 border_color = lerp_color(k_line, k_accent, (hover_anim * 0.35f) + (active_anim * 0.55f));
  const float fill_x = grab_bb.Max.x > track_bb.Min.x ? grab_bb.GetCenter().x : track_bb.Min.x;
  const ImVec2 fill_max(fill_x, track_bb.Max.y);
  const ImRect knob_bb(ImVec2(fill_x - 1.0f, track_bb.Min.y - 5.0f), ImVec2(fill_x + 1.0f, track_bb.Max.y + 5.0f));

  window->DrawList->AddRectFilled(field_bb.Min, field_bb.Max, ImGui::GetColorU32(field_color), 0.0f);
  window->DrawList->AddRect(field_bb.Min, field_bb.Max, ImGui::GetColorU32(border_color), 0.0f, 0, 1.0f);
  window->DrawList->AddRectFilled(track_bb.Min, track_bb.Max, ImGui::GetColorU32(with_alpha(k_text_soft, 0.18f)), 0.0f);
  window->DrawList->AddRectFilled(track_bb.Min, fill_max, ImGui::GetColorU32(k_accent), 0.0f);
  window->DrawList->AddRectFilled(value_bb.Min, value_bb.Max, ImGui::GetColorU32(with_alpha(k_bg_panel, 0.95f)), 0.0f);
  window->DrawList->AddLine(ImVec2(value_bb.Min.x, value_bb.Min.y + 2.0f), ImVec2(value_bb.Min.x, value_bb.Max.y - 2.0f), ImGui::GetColorU32(with_alpha(k_line, 0.85f)), 1.0f);
  window->DrawList->AddRectFilled(knob_bb.Min, knob_bb.Max, ImGui::GetColorU32(lerp_color(k_text_muted, k_text, active_anim * 0.7f + hover_anim * 0.3f)), 0.0f);
  window->DrawList->AddText(font_regular(), font_regular()->LegacySize, ImVec2(value_bb.GetCenter().x - (value_size.x * 0.5f), value_bb.GetCenter().y - (value_size.y * 0.5f)), ImGui::GetColorU32(active_anim > 0.0f ? k_text : k_text_muted), value_buffer);
  cat_bind::bindable_slider_float(label, value, changed, minimum, maximum, format);
  return changed;
}

inline bool slider_int(const char* label, int* value, int minimum, int maximum, const char* format = "%d") {
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  if (window == nullptr || window->SkipItems || label == nullptr || value == nullptr || format == nullptr) return false;

  ImGuiContext& g = *GImGui;
  field_label(label);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
  const ImGuiID id = window->GetID(label);
  const ImVec2 pos = window->DC.CursorPos;
  const float available_width = ImGui::CalcItemWidth();
  const float track_width = ImMax(120.0f, available_width);
  const ImRect field_bb(pos, ImVec2(pos.x + track_width, pos.y + 24.0f));
  const ImRect total_bb(pos, field_bb.Max);
  ImRect grab_bb{};

  ImGui::ItemSize(total_bb);
  if (!ImGui::ItemAdd(total_bb, id)) return false;

  char value_buffer[32]{};
  ImFormatString(value_buffer, IM_ARRAYSIZE(value_buffer), format, *value);
  const ImVec2 value_size = ImGui::CalcTextSize(value_buffer);
  const float value_width = slider_value_box_width(format, minimum, maximum, *value);
  const ImRect value_bb(ImVec2(field_bb.Max.x - value_width - 4.0f, field_bb.Min.y + 3.0f), ImVec2(field_bb.Max.x - 4.0f, field_bb.Max.y - 3.0f));
  const ImRect track_bb(ImVec2(field_bb.Min.x + 8.0f, field_bb.Min.y + 11.0f), ImVec2(value_bb.Min.x - 8.0f, field_bb.Min.y + 13.0f));

  const bool hovered = ImGui::ItemHoverable(field_bb, id, ImGuiItemFlags_None);
  const bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  if (clicked || g.NavActivateId == id) {
    if (clicked) ImGui::SetKeyOwner(ImGuiKey_MouseLeft, id);
    ImGui::SetActiveID(id, window);
    ImGui::SetFocusID(id, window);
    ImGui::FocusWindow(window);
    g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
  }

  const bool changed = ImGui::SliderBehavior(track_bb, id, ImGuiDataType_S32, value, &minimum, &maximum, format, ImGuiSliderFlags_None, &grab_bb);
  if (changed) ImGui::MarkItemEdited(id);

  const float hover_anim = storage_anim(id, "slider_hover", hovered ? 1.0f : 0.0f, 12.0f);
  const float active_anim = storage_anim(id, "slider_active", g.ActiveId == id ? 1.0f : 0.0f, 12.0f);
  const ImVec4 field_color = lerp_color(k_frame, k_frame_hover, hover_anim * 0.6f);
  const ImVec4 border_color = lerp_color(k_line, k_accent, (hover_anim * 0.35f) + (active_anim * 0.55f));
  const float fill_x = grab_bb.Max.x > track_bb.Min.x ? grab_bb.GetCenter().x : track_bb.Min.x;
  const ImVec2 fill_max(fill_x, track_bb.Max.y);
  const ImRect knob_bb(ImVec2(fill_x - 1.0f, track_bb.Min.y - 5.0f), ImVec2(fill_x + 1.0f, track_bb.Max.y + 5.0f));

  window->DrawList->AddRectFilled(field_bb.Min, field_bb.Max, ImGui::GetColorU32(field_color), 0.0f);
  window->DrawList->AddRect(field_bb.Min, field_bb.Max, ImGui::GetColorU32(border_color), 0.0f, 0, 1.0f);
  window->DrawList->AddRectFilled(track_bb.Min, track_bb.Max, ImGui::GetColorU32(with_alpha(k_text_soft, 0.18f)), 0.0f);
  window->DrawList->AddRectFilled(track_bb.Min, fill_max, ImGui::GetColorU32(k_accent), 0.0f);
  window->DrawList->AddRectFilled(value_bb.Min, value_bb.Max, ImGui::GetColorU32(with_alpha(k_bg_panel, 0.95f)), 0.0f);
  window->DrawList->AddLine(ImVec2(value_bb.Min.x, value_bb.Min.y + 2.0f), ImVec2(value_bb.Min.x, value_bb.Max.y - 2.0f), ImGui::GetColorU32(with_alpha(k_line, 0.85f)), 1.0f);
  window->DrawList->AddRectFilled(knob_bb.Min, knob_bb.Max, ImGui::GetColorU32(lerp_color(k_text_muted, k_text, active_anim * 0.7f + hover_anim * 0.3f)), 0.0f);
  window->DrawList->AddText(font_regular(), font_regular()->LegacySize, ImVec2(value_bb.GetCenter().x - (value_size.x * 0.5f), value_bb.GetCenter().y - (value_size.y * 0.5f)), ImGui::GetColorU32(active_anim > 0.0f ? k_text : k_text_muted), value_buffer);
  cat_bind::bindable_slider_int(label, value, changed, minimum, maximum, format);
  return changed;
}

inline bool input_text(const char* label, std::string* value) {
  if (label == nullptr || value == nullptr) return false;

  field_label(label);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
  const std::string item_id = std::string("##") + label;
  ImGui::SetNextItemWidth(ImMax(80.0f, ImGui::GetContentRegionAvail().x - 10.0f));
  const bool hide_text = ImGui::GetActiveID() != ImGui::GetID(item_id.c_str());
  ImGui::PushStyleColor(ImGuiCol_FrameBg, k_frame);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, k_frame_hover);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, k_frame_hover);
  ImGui::PushStyleColor(ImGuiCol_Border, k_line);
  ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, with_alpha(k_accent, 0.30f));
  ImGui::PushStyleColor(ImGuiCol_Text, hide_text ? ImVec4(k_text.x, k_text.y, k_text.z, 0.0f) : k_text);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
  const bool changed = ImGui::InputText(item_id.c_str(), value);
  const bool is_active = ImGui::IsItemActive();
  const bool is_hovered = ImGui::IsItemHovered();
  if (!is_active) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 rect_min = ImGui::GetItemRectMin();
    const ImVec2 rect_max = ImGui::GetItemRectMax();
    const ImRect clip_bb(ImVec2(rect_min.x + 8.0f, rect_min.y + 2.0f), ImVec2(rect_max.x - 8.0f, rect_max.y - 2.0f));
    const ImVec4 preview_color = is_hovered ? k_text : k_text_muted;
    const std::string preview_text = ellipsize_text(value->c_str(), clip_bb.GetWidth());
    draw_list->AddRectFilled(
      ImVec2(rect_min.x + 1.0f, rect_min.y + 1.0f),
      ImVec2(rect_max.x - 1.0f, rect_max.y - 1.0f),
      ImGui::GetColorU32(is_hovered ? k_frame_hover : k_frame),
      0.0f);
    ImGui::PushClipRect(clip_bb.Min, clip_bb.Max, true);
    draw_list->AddText(font_regular(), font_regular()->LegacySize, ImVec2(clip_bb.Min.x, rect_min.y + 4.0f), ImGui::GetColorU32(preview_color), preview_text.c_str());
    ImGui::PopClipRect();
  }
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(6);
  return changed;
}

inline bool input_text(const char* label, char* value, int capacity) {
  if (label == nullptr || value == nullptr || capacity <= 0) return false;

  field_label(label);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
  const std::string item_id = std::string("##") + label;
  ImGui::SetNextItemWidth(ImMax(80.0f, ImGui::GetContentRegionAvail().x - 10.0f));
  const bool hide_text = ImGui::GetActiveID() != ImGui::GetID(item_id.c_str());
  ImGui::PushStyleColor(ImGuiCol_FrameBg, k_frame);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, k_frame_hover);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, k_frame_hover);
  ImGui::PushStyleColor(ImGuiCol_Border, k_line);
  ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, with_alpha(k_accent, 0.30f));
  ImGui::PushStyleColor(ImGuiCol_Text, hide_text ? ImVec4(k_text.x, k_text.y, k_text.z, 0.0f) : k_text);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
  const bool changed = ImGui::InputText(item_id.c_str(), value, capacity);
  const bool is_active = ImGui::IsItemActive();
  const bool is_hovered = ImGui::IsItemHovered();
  if (!is_active) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 rect_min = ImGui::GetItemRectMin();
    const ImVec2 rect_max = ImGui::GetItemRectMax();
    const ImRect clip_bb(ImVec2(rect_min.x + 8.0f, rect_min.y + 2.0f), ImVec2(rect_max.x - 8.0f, rect_max.y - 2.0f));
    const ImVec4 preview_color = is_hovered ? k_text : k_text_muted;
    const std::string preview_text = ellipsize_text(value, clip_bb.GetWidth());
    draw_list->AddRectFilled(
      ImVec2(rect_min.x + 1.0f, rect_min.y + 1.0f),
      ImVec2(rect_max.x - 1.0f, rect_max.y - 1.0f),
      ImGui::GetColorU32(is_hovered ? k_frame_hover : k_frame),
      0.0f);
    ImGui::PushClipRect(clip_bb.Min, clip_bb.Max, true);
    draw_list->AddText(font_regular(), font_regular()->LegacySize, ImVec2(clip_bb.Min.x, rect_min.y + 4.0f), ImGui::GetColorU32(preview_color), preview_text.c_str());
    ImGui::PopClipRect();
  }
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(6);
  return changed;
}

inline bool color_picker(const char* label, float* color) {
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  if (window == nullptr || window->SkipItems || label == nullptr || color == nullptr) return false;

  field_label(label);
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
  const ImGuiID id = window->GetID(label);
  const ImVec2 pos = window->DC.CursorPos;
  const ImVec2 size = ImGui::CalcItemSize(ImVec2(ImGui::CalcItemWidth(), 24.0f), 42.0f, 24.0f);
  const ImRect box_bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));

  ImGui::ItemSize(box_bb);
  if (!ImGui::ItemAdd(box_bb, id)) return false;

  bool hovered = false, held = false;
  const bool pressed = ImGui::ButtonBehavior(box_bb, id, &hovered, &held);
  const std::string popup_id = std::string(label) + "##picker_popup";
  if (pressed) ImGui::OpenPopup(popup_id.c_str());

  const float hover_anim = storage_anim(id, "color_hover", hovered ? 1.0f : 0.0f, 12.0f);
  const bool popup_open = ImGui::IsPopupOpen(popup_id.c_str());
  const float active_anim = storage_anim(id, "color_active", popup_open ? 1.0f : 0.0f, 12.0f);
  const ImVec4 field_color = lerp_color(k_frame, k_frame_hover, hover_anim * 0.55f);
  const ImVec4 outline_color = lerp_color(k_line, k_accent, (hover_anim * 0.35f) + (active_anim * 0.55f));
  char color_text[32]{};
  ImFormatString(color_text, IM_ARRAYSIZE(color_text), "%.2f %.2f %.2f", color[0], color[1], color[2]);

  const ImRect swatch_bb(ImVec2(box_bb.Min.x + 4.0f, box_bb.Min.y + 4.0f), ImVec2(box_bb.Min.x + 28.0f, box_bb.Max.y - 4.0f));
  window->DrawList->AddRectFilled(box_bb.Min, box_bb.Max, ImGui::GetColorU32(field_color), 0.0f);
  window->DrawList->AddRect(box_bb.Min, box_bb.Max, ImGui::GetColorU32(outline_color), 0.0f, 0, 1.0f);
  window->DrawList->AddRectFilled(swatch_bb.Min, swatch_bb.Max, ImGui::ColorConvertFloat4ToU32(ImVec4(color[0], color[1], color[2], color[3])), 0.0f);
  window->DrawList->AddRect(swatch_bb.Min, swatch_bb.Max, ImGui::GetColorU32(k_line), 0.0f, 0, 1.0f);
  window->DrawList->AddText(font_regular(), font_regular()->LegacySize, ImVec2(swatch_bb.Max.x + 8.0f, box_bb.Min.y + 5.0f), ImGui::GetColorU32(k_text_muted), color_text);
  window->DrawList->AddTriangleFilled(
    ImVec2(box_bb.Max.x - 12.0f, box_bb.GetCenter().y - 2.0f),
    ImVec2(box_bb.Max.x - 6.0f, box_bb.GetCenter().y - 2.0f),
    ImVec2(box_bb.Max.x - 9.0f, box_bb.GetCenter().y + 2.0f),
    ImGui::GetColorU32(k_text_soft));

  bool changed = false;
  ImGui::SetNextWindowSize(ImVec2(220.0f, 0.0f), ImGuiCond_Appearing);
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
  ImGui::PushStyleColor(ImGuiCol_PopupBg, k_bg_panel);
  ImGui::PushStyleColor(ImGuiCol_Border, k_line);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, k_frame);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, k_frame_hover);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, k_frame_hover);
  ImGui::PushStyleColor(ImGuiCol_Button, k_frame);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, k_frame_hover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, with_alpha(k_accent, 0.82f));
  ImGui::PushStyleColor(ImGuiCol_Header, k_accent_soft);
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, with_alpha(k_accent, 0.42f));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, with_alpha(k_accent, 0.56f));
  ImGui::PushStyleColor(ImGuiCol_SliderGrab, k_accent);
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, k_text);
  ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, with_alpha(k_accent, 0.30f));

  if (ImGui::BeginPopup(popup_id.c_str())) {
    ImGui::TextUnformatted(label);
    ImGui::Separator();
    changed = ImGui::ColorPicker4(
      "##picker",
      color,
      ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_InputRGB);
    ImGui::EndPopup();
  }

  ImGui::PopStyleColor(14);
  ImGui::PopStyleVar(3);
  return changed;
}

} // namespace cat_menu

static void get_input(SDL_Event* event) {
  cat_bind::handle_input(event);
}

static void set_imgui_theme(void) {
  cat_menu::ensure_fonts();
  ImGuiStyle* style = &ImGui::GetStyle();
  style->WindowPadding = ImVec2(5.0f, 5.0f);
  style->FramePadding = ImVec2(4.0f, 2.0f);
  style->ItemSpacing = ImVec2(5.0f, 4.0f);
  style->ItemInnerSpacing = ImVec2(5.0f, 3.0f);
  style->WindowRounding = 0.0f;
  style->ChildRounding = 0.0f;
  style->FrameRounding = 0.0f;
  style->PopupRounding = 0.0f;
  style->ScrollbarRounding = 0.0f;
  style->GrabRounding = 0.0f;
  style->GrabMinSize = 6.0f;
  style->WindowBorderSize = 0.0f;
  style->ChildBorderSize = 0.0f;
  style->PopupBorderSize = 0.0f;
  style->FrameBorderSize = 0.0f;

  style->Colors[ImGuiCol_WindowBg] = cat_menu::k_bg_outer;
  style->Colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  style->Colors[ImGuiCol_TitleBg] = cat_menu::k_bg_header;
  style->Colors[ImGuiCol_TitleBgActive] = cat_menu::k_bg_header;
  style->Colors[ImGuiCol_Border] = cat_menu::k_line;
  style->Colors[ImGuiCol_Text] = cat_menu::k_text;
  style->Colors[ImGuiCol_TextDisabled] = cat_menu::k_text_muted;
  style->Colors[ImGuiCol_FrameBg] = cat_menu::k_frame;
  style->Colors[ImGuiCol_FrameBgHovered] = cat_menu::k_frame_hover;
  style->Colors[ImGuiCol_FrameBgActive] = cat_menu::lerp_color(cat_menu::k_frame_hover, cat_menu::k_accent, 0.35f);
  style->Colors[ImGuiCol_Button] = cat_menu::k_frame;
  style->Colors[ImGuiCol_ButtonHovered] = cat_menu::k_frame_hover;
  style->Colors[ImGuiCol_ButtonActive] = cat_menu::k_accent;
  style->Colors[ImGuiCol_CheckMark] = cat_menu::k_text;
  style->Colors[ImGuiCol_Header] = cat_menu::k_frame;
  style->Colors[ImGuiCol_HeaderHovered] = cat_menu::k_frame_hover;
  style->Colors[ImGuiCol_HeaderActive] = cat_menu::k_accent;
  style->Colors[ImGuiCol_SliderGrab] = ImVec4(0.68f, 0.87f, 1.00f, 1.00f);
  style->Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.82f, 0.94f, 1.00f, 1.00f);
  style->Colors[ImGuiCol_Separator] = cat_menu::k_line;
  style->Colors[ImGuiCol_ResizeGrip] = cat_menu::k_accent_soft;
  style->Colors[ImGuiCol_ResizeGripHovered] = cat_menu::k_accent;
  style->Colors[ImGuiCol_PopupBg] = cat_menu::k_bg_panel;
  style->Colors[ImGuiCol_ScrollbarBg] = cat_menu::k_bg_panel;
  style->Colors[ImGuiCol_ScrollbarGrab] = cat_menu::k_frame_hover;
  style->Colors[ImGuiCol_ScrollbarGrabHovered] = cat_menu::k_accent;
}

static ImU32 cathook_watermark_rainbow_color() {
  const float hue = std::fabs(std::sin(static_cast<float>(ImGui::GetTime()) / 2.0f));
  return ImGui::ColorConvertFloat4ToU32(ImColor::HSV(hue, 0.85f, 0.90f, 1.0f));
}

static const char* cathook_watermark_version() {
#if defined(GIT_COMMIT_HASH) && defined(GIT_COMMITTER_DATE)
  return "Version: #" GIT_COMMIT_HASH " " GIT_COMMITTER_DATE;
#else
  return "Unknown Version";
#endif
}

static const char* cathook_watermark_type() {
#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE
  return " NOGUI";
#else
  return " GUI";
#endif
}

static void draw_cathook_side_string(ImDrawList* draw_list, float y, ImU32 color, const char* text) {
  constexpr float x = 8.0f;
  const ImVec2 pos(x, y);
  const ImU32 shadow_color = IM_COL32(0, 0, 0, (color >> IM_COL32_A_SHIFT) & 0xff);

  for (int offset = -1; offset < 2; offset += 2) {
    draw_list->AddText(ImVec2(pos.x + static_cast<float>(offset), pos.y), shadow_color, text);
    draw_list->AddText(ImVec2(pos.x, pos.y + static_cast<float>(offset)), shadow_color, text);
  }

  draw_list->AddText(pos, color, text);
}

static void draw_watermark(void) {
  ImDrawList* draw_list = ImGui::GetForegroundDrawList();
  const float line_step = ImGui::GetFontSize() + 1.0f;
  float y = 8.0f;

  draw_cathook_side_string(draw_list, y, cathook_watermark_rainbow_color(), "cathook by pupnoodle");
  y += line_step;
  draw_cathook_side_string(draw_list, y, IM_COL32(255, 255, 255, 255), cathook_watermark_version());
  y += line_step;
  draw_cathook_side_string(draw_list, y, IM_COL32(255, 255, 255, 255), cathook_watermark_type());
  y += line_step;
  draw_cathook_side_string(draw_list, y, IM_COL32(255, 255, 255, 255), "Press 'INSERT' key to open/close cheat menu.");
  y += line_step;
  draw_cathook_side_string(draw_list, y, IM_COL32(255, 255, 255, 255), "Use mouse to navigate in menu.");
}

static void draw_beta_notice(void) {
  constexpr const char* title = "BETA";
  constexpr const char* message = "*some of the features may work badly or straight up not work, please report all issues on githubs issue page.";

  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImDrawList* draw_list = ImGui::GetForegroundDrawList();
  ImFont* title_font = cat_menu::font_regular_large();
  ImFont* message_font = cat_menu::font_regular();
  const float title_size_px = title_font->LegacySize;
  const float message_size_px = message_font->LegacySize;
  const ImVec2 title_size = title_font->CalcTextSizeA(title_size_px, FLT_MAX, 0.0f, title);
  const ImVec2 message_size = message_font->CalcTextSizeA(message_size_px, FLT_MAX, 0.0f, message);
  const float center_x = viewport->Pos.x + (viewport->Size.x * 0.5f);
  const float top_y = viewport->Pos.y + (viewport->Size.y * 0.18f);

  draw_list->AddText(
    title_font,
    title_size_px,
    ImVec2(center_x - (title_size.x * 0.5f), top_y),
    ImGui::GetColorU32(cat_menu::k_accent),
    title);
  draw_list->AddText(
    message_font,
    message_size_px,
    ImVec2(center_x - (message_size.x * 0.5f), top_y + title_size.y + 4.0f),
    ImGui::GetColorU32(cat_menu::k_text_soft),
    message);
}

static void draw_aimbot_content() {
  const char* target_items[] = { "FOV", "Distance", "Least Health", "Most Health" };
  const char* aim_at_items[] = { "Enemies", "Buildings", "MvM robots", "Pumpkins", "Stickies" };
  const char* aim_mode_items[] = { "Plain", "Smooth", "Assistive", "Psilent" };
  const char* projectile_mode_items[] = { "Direct only", "Direct + splash", "Prefer splash", "Splash only" };
  const char* hitbox_items[] = { "Head", "Body", "Pelvis", "Arms", "Legs" };
  const char* projectile_hitbox_items[] = { "Auto", "Head", "Body", "Pelvis", "Arms", "Legs" };
  const uint32_t aim_at_bits[] = {
    Aim::aim_at_enemies,
    Aim::aim_at_buildings,
    Aim::aim_at_mvm_robots,
    Aim::aim_at_pumpkins,
    Aim::aim_at_stickies
  };
  const uint32_t hitbox_bits[] = {
    aim_hitbox_mask_head,
    aim_hitbox_mask_body,
    aim_hitbox_mask_pelvis,
    aim_hitbox_mask_arms,
    aim_hitbox_mask_legs
  };
  const uint32_t projectile_hitbox_bits[] = {
    aim_hitbox_mask_auto,
    aim_hitbox_mask_head,
    aim_hitbox_mask_body,
    aim_hitbox_mask_pelvis,
    aim_hitbox_mask_arms,
    aim_hitbox_mask_legs
  };

  cat_menu::begin_flow_layout("aimbot_layout", 3);
  cat_menu::flow_panel("Aimbot", 0, 200.0f, [&]() {
    cat_menu::checkbox("Enable", &config.aimbot.master);
    cat_menu::checkbox("Auto shoot", &config.aimbot.auto_shoot);
    cat_menu::checkbox("Draw FOV", &config.aimbot.draw_fov);
    cat_menu::checkbox("Shoot through glass", &config.aimbot.shoot_through_glass);
    cat_menu::combo("Aim mode", (int*)&config.aimbot.aim_mode, aim_mode_items, IM_ARRAYSIZE(aim_mode_items));
    cat_menu::slider_float("Aim FOV", &config.aimbot.fov, 0.0f, 180.0f, "%.0f deg");
    cat_menu::slider_float("Smooth factor", &config.aimbot.smooth_factor, 1.0f, 30.0f, "%.1f");
    cat_menu::slider_float("Assist strength", &config.aimbot.assist_strength, 0.0f, 100.0f, "%.0f%%");
  });
  cat_menu::flow_panel("Target selection", 1, 214.0f, [&]() {
    cat_menu::combo("Target", (int*)&config.aimbot.target_type, target_items, IM_ARRAYSIZE(target_items));
    cat_menu::multi_select_combo("Aim at", &config.aimbot.aim_at, aim_at_items, aim_at_bits, IM_ARRAYSIZE(aim_at_items));
    cat_menu::multi_select_combo("Hitscan hitboxes", &config.aimbot.hitscan_hitboxes, hitbox_items, hitbox_bits, IM_ARRAYSIZE(hitbox_items));
    cat_menu::multi_select_combo("Melee hitboxes", &config.aimbot.melee_hitboxes, hitbox_items, hitbox_bits, IM_ARRAYSIZE(hitbox_items));
    cat_menu::checkbox("Melee walk to target", &config.aimbot.melee_walk_to_target);
    cat_menu::multi_select_combo("Projectile hitboxes", &config.aimbot.projectile_hitboxes, projectile_hitbox_items, projectile_hitbox_bits, IM_ARRAYSIZE(projectile_hitbox_items));
    cat_menu::checkbox("Ignore friends", &config.aimbot.ignore_friends);
  });
  cat_menu::flow_panel("Projectile", 1, 296.0f, [&]() {
    cat_menu::combo("Projectile mode", (int*)&config.aimbot.projectile_mode, projectile_mode_items, IM_ARRAYSIZE(projectile_mode_items));
    cat_menu::checkbox("Wall splash", &config.aimbot.projectile_wall_splash);
    cat_menu::checkbox("Seam shot", &config.aimbot.projectile_seam_shot);
    cat_menu::checkbox("Strafe prediction", &config.aimbot.projectile_strafe_prediction);
    cat_menu::checkbox("Splash debug", &config.aimbot.projectile_splash_debug);
    cat_menu::slider_float("Splash radius scale", &config.aimbot.projectile_splash_radius_scale, 0.50f, 1.50f, "%.2fx");
    cat_menu::slider_int("Path steps", &config.aimbot.projectile_path_steps, 2, 64);
    cat_menu::slider_int("Splash samples", &config.aimbot.projectile_splash_samples, 4, 64);
    cat_menu::slider_int("Prediction ticks", &config.aimbot.projectile_prediction_ticks, 8, 180);
    cat_menu::slider_float("Strafe confidence", &config.aimbot.projectile_strafe_confidence, 0.0f, 100.0f, "%.0f%%");
    cat_menu::slider_int("Trace interval", &config.aimbot.projectile_trace_interval, 1, 8);
  });
  cat_menu::flow_panel("Heavy", 2, 92.0f, [&]() {
    cat_menu::checkbox("Heavy auto rev", &config.aimbot.auto_rev);
    cat_menu::checkbox("Heavy auto unrev", &config.aimbot.auto_unrev);
    cat_menu::slider_float("Heavy rev threshold", &config.aimbot.auto_rev_threshold, 200.0f, 1200.0f, "%.0f HU");
  });
  cat_menu::flow_panel("Sniper", 2, 120.0f, [&]() {
    cat_menu::checkbox("Sniper auto scope", &config.aimbot.auto_scope);
    cat_menu::checkbox("Sniper auto unscope", &config.aimbot.auto_unscope);
    cat_menu::slider_float("Auto scope threshold", &config.aimbot.auto_scope_threshold, 800.0f, 2500.0f, "%.0f HU");
    cat_menu::checkbox("Scoped only", &config.aimbot.scoped_only);
    cat_menu::checkbox("Wait for headshot", &config.aimbot.wait_for_headshot);
  });
  cat_menu::end_flow_layout();
}

static void draw_crits_content();

static void draw_combat_tab() {
  enum combat_page_id
  {
    combat_page_aimbot,
    combat_page_crits
  };

  static int combat_subtab = combat_page_aimbot;

  cat_menu::begin_tab_strip("##combat_subtabs", cat_menu::k_subtab_strip_height, false, true, cat_menu::k_tab_strip_padding_x, false);
  if (cat_menu::subtab_button("Aimbot", combat_subtab == combat_page_aimbot)) {
    combat_subtab = combat_page_aimbot;
  }
  ImGui::SameLine(0.0f, 0.0f);
  if (cat_menu::subtab_button("Crits", combat_subtab == combat_page_crits)) {
    combat_subtab = combat_page_crits;
  }
  cat_menu::end_tab_strip();
  ImGui::Dummy(ImVec2(0.0f, 4.0f));

  switch (combat_subtab) {
    case combat_page_aimbot:
      draw_aimbot_content();
      break;
    case combat_page_crits:
      draw_crits_content();
      break;
  }
}

static void draw_crits_content() {
  cat_menu::begin_flow_layout("crits_layout", 2);
  cat_menu::flow_panel("Crits", 0, 176.0f, [&]() {
    cat_menu::checkbox("Force crits", &config.random_crits.force_crits);
    cat_menu::checkbox("Always melee crit", &config.random_crits.always_melee_crit);
    cat_menu::checkbox("Save bucket", &config.random_crits.save_bucket);
    cat_menu::checkbox("Respect bucket", &config.random_crits.respect_bucket);
    cat_menu::checkbox("Advanced stats", &config.random_crits.advanced_stats);
    cat_menu::slider_int("Seed scan", &config.random_crits.seed_scan, 256, 100000);
  });
  cat_menu::end_flow_layout();
}

static void draw_esp_content() {
  const char* mafia_position_items[] = { "Under name", "Left", "Right" };
  const char* box_type_items[] = { "Outline", "Corner", "Filled", "Rounded", "Projected" };
  const char* head_emoji_items[] = { "Emoji 1", "Emoji 2" };

  cat_menu::begin_flow_layout("esp_layout", 3);
  cat_menu::flow_panel("ESP", 0, 126.0f, [&]() {
    cat_menu::checkbox("Enable player ESP", &config.esp.master);
    cat_menu::checkbox("Lerp ESP", &config.esp.lerp);
    cat_menu::slider_float("Lerp speed", &config.esp.lerp_speed, 1.0f, 40.0f, "%.1f");
  });
  cat_menu::flow_panel("Targets", 0, 196.0f, [&]() {
    cat_menu::checkbox("Enemy", &config.esp.player.enemy);
    cat_menu::checkbox("Team", &config.esp.player.team);
    cat_menu::checkbox("Friends", &config.esp.player.friends);
    cat_menu::checkbox("Pickup box", &config.esp.pickup.box);
    cat_menu::combo("Pickup box type", (int*)&config.esp.pickup.box_style, box_type_items, IM_ARRAYSIZE(box_type_items));
    cat_menu::checkbox("Pickup name", &config.esp.pickup.name);
    cat_menu::checkbox("Intel box", &config.esp.intelligence.box);
    cat_menu::combo("Intel box type", (int*)&config.esp.intelligence.box_style, box_type_items, IM_ARRAYSIZE(box_type_items));
    cat_menu::checkbox("Intel name", &config.esp.intelligence.name);
  });
  cat_menu::flow_panel("ESP settings", 1, 288.0f, [&]() {
    cat_menu::checkbox("Box", &config.esp.player.box);
    cat_menu::combo("Box type", (int*)&config.esp.player.box_style, box_type_items, IM_ARRAYSIZE(box_type_items));
    cat_menu::checkbox("Health bar", &config.esp.player.health_bar);
    cat_menu::checkbox("Name", &config.esp.player.name);
    cat_menu::checkbox("Class icon", &config.esp.player.class_icon);
    cat_menu::checkbox("Class icon team", &config.esp.player.class_icon_teammates);
    ImGui::SliderFloat("CI scale", &config.esp.player.class_icon_scale, 0.5f, 5.0f, "%.1f");
    cat_menu::checkbox("Head emoji", &config.esp.player.head_emoji);
    cat_menu::combo("Emoji style", &config.esp.player.head_emoji_style, head_emoji_items, IM_ARRAYSIZE(head_emoji_items));
    cat_menu::checkbox("Emoji teammates", &config.esp.player.head_emoji_teammates);
    ImGui::SliderFloat("Emoji scale", &config.esp.player.head_emoji_scale, 0.5f, 5.0f, "%.1f");
    cat_menu::checkbox("Mafia level", &config.esp.player.mafia_level);
    cat_menu::combo("Mafia position", (int*)&config.esp.player.mafia_level_position, mafia_position_items, IM_ARRAYSIZE(mafia_position_items));
    cat_menu::checkbox("Target flag", &config.esp.player.flags.target_indicator);
    cat_menu::checkbox("Friend flag", &config.esp.player.flags.friend_indicator);
    cat_menu::checkbox("Scoped flag", &config.esp.player.flags.scoped_indicator);
  });
  cat_menu::flow_panel("Strings", 2, 188.0f, [&]() {
    cat_menu::checkbox("Box", &config.esp.buildings.box);
    cat_menu::combo("Box type", (int*)&config.esp.buildings.box_style, box_type_items, IM_ARRAYSIZE(box_type_items));
    cat_menu::checkbox("Health bar", &config.esp.buildings.health_bar);
    cat_menu::checkbox("Name", &config.esp.buildings.name);
    cat_menu::checkbox("Team", &config.esp.buildings.team);
  });
  cat_menu::flow_panel("Colors", 2, 126.0f, [&]() {
    cat_menu::color_picker("Enemy color", config.esp.player.enemy_color.to_arr());
    cat_menu::color_picker("Team color", config.esp.player.team_color.to_arr());
    cat_menu::color_picker("Friend color", config.esp.player.friend_color.to_arr());
  });
  cat_menu::end_flow_layout();
}

static void draw_chams_content() {
  const char* mats[] = {
    "None",
    "Flat",
    "Flat wireframe",
    "Shaded",
    "Shaded wireframe",
    "Fresnel",
    "Fresnel wireframe",
    "Glossy",
    "Glossy wireframe",
    "Additive",
    "Additive wireframe"
  };

  cat_menu::begin_flow_layout("chams_layout", 3);
  cat_menu::flow_panel("Chams", 0, 388.0f, [&]() {
    cat_menu::checkbox("Enable chams", &config.chams.master);
    cat_menu::checkbox("Enemy", &config.chams.player.enemy);
    cat_menu::checkbox("Team", &config.chams.player.team);
    cat_menu::checkbox("Friends", &config.chams.player.friends);
    cat_menu::checkbox("Local", &config.chams.player.local);
    cat_menu::combo("Enemy material", (int*)&config.chams.player.enemy_material_type, mats, IM_ARRAYSIZE(mats));
    cat_menu::combo("Enemy z material", (int*)&config.chams.player.enemy_material_z_type, mats, IM_ARRAYSIZE(mats));
    cat_menu::combo("Team material", (int*)&config.chams.player.team_material_type, mats, IM_ARRAYSIZE(mats));
    cat_menu::combo("Team z material", (int*)&config.chams.player.team_material_z_type, mats, IM_ARRAYSIZE(mats));
    cat_menu::combo("Friend material", (int*)&config.chams.player.friend_material_type, mats, IM_ARRAYSIZE(mats));
    cat_menu::combo("Friend z material", (int*)&config.chams.player.friend_material_z_type, mats, IM_ARRAYSIZE(mats));
    cat_menu::combo("Local material", (int*)&config.chams.player.local_material_type, mats, IM_ARRAYSIZE(mats));
    cat_menu::checkbox("Enemy ignore z", &config.chams.player.enemy_flags.ignore_z);
    cat_menu::checkbox("Team ignore z", &config.chams.player.team_flags.ignore_z);
  });
  cat_menu::flow_panel("Targets", 1, 170.0f, [&]() {
    cat_menu::checkbox("Friend ignore z", &config.chams.player.friends_flags.ignore_z);
    cat_menu::checkbox("Enemy overlay ignore z", &config.chams.player.enemy_overlay_flags.ignore_z);
  });
  cat_menu::flow_panel("Colors", 2, 160.0f, [&]() {
    cat_menu::color_picker("Enemy visible", config.chams.player.enemy_color.to_arr());
    cat_menu::color_picker("Enemy occluded", config.chams.player.enemy_color_z.to_arr());
    cat_menu::color_picker("Team visible", config.chams.player.team_color.to_arr());
    cat_menu::color_picker("Team occluded", config.chams.player.team_color_z.to_arr());
  });
  cat_menu::flow_panel("Colors (NoVis)", 2, 188.0f, [&]() {
    cat_menu::color_picker("Friend visible", config.chams.player.friend_color.to_arr());
    cat_menu::color_picker("Friend occluded", config.chams.player.friend_color_z.to_arr());
    cat_menu::color_picker("Local color", config.chams.player.local_color.to_arr());
    cat_menu::combo("Enemy overlay mat", (int*)&config.chams.player.enemy_overlay_material_type, mats, IM_ARRAYSIZE(mats));
    cat_menu::combo("Enemy overlay z mat", (int*)&config.chams.player.enemy_overlay_material_z_type, mats, IM_ARRAYSIZE(mats));
    cat_menu::color_picker("Enemy overlay", config.chams.player.enemy_overlay_color.to_arr());
    cat_menu::color_picker("Enemy overlay z", config.chams.player.enemy_overlay_color_z.to_arr());
  });
  cat_menu::end_flow_layout();
}

static void draw_glow_content() {
  cat_menu::begin_flow_layout("glow_layout", 3);
  cat_menu::flow_panel("Glow", 0, 170.0f, [&]() {
    cat_menu::checkbox("Enable player glow", &config.glow.master);
    cat_menu::checkbox("Enemy", &config.glow.player.enemy);
    cat_menu::checkbox("Team", &config.glow.player.team);
    cat_menu::checkbox("Friends", &config.glow.player.friends);
    cat_menu::checkbox("Local", &config.glow.player.local);
  });
  cat_menu::flow_panel("Style", 1, 188.0f, [&]() {
    cat_menu::slider_int("Outline size", &config.glow.outline_scale, 0, 10);
    cat_menu::slider_float("Blur strength", &config.glow.blur_scale, 0.0f, 10.0f, "%.1f");
    cat_menu::slider_float("Fade near", &config.glow.start, 0.0f, 2048.0f, "%.0f HU");
    cat_menu::slider_float("Fade far", &config.glow.end, 512.0f, 8192.0f, "%.0f HU");
    if (config.glow.end < config.glow.start) {
      config.glow.end = config.glow.start;
    }
    cat_menu::checkbox("Distance fade", &config.glow.smooth_alpha);
    cat_menu::checkbox("Filled body", &config.glow.filled_body);
  });
  cat_menu::flow_panel("Colors", 2, 244.0f, [&]() {
    cat_menu::color_picker("Enemy visible", config.glow.player.enemy_color.to_arr());
    cat_menu::color_picker("Enemy not visible", config.glow.player.enemy_color_z.to_arr());
    cat_menu::color_picker("Team visible", config.glow.player.team_color.to_arr());
    cat_menu::color_picker("Team not visible", config.glow.player.team_color_z.to_arr());
    cat_menu::color_picker("Friend visible", config.glow.player.friend_color.to_arr());
    cat_menu::color_picker("Friend not visible", config.glow.player.friend_color_z.to_arr());
    cat_menu::color_picker("Local color", config.glow.player.local_color.to_arr());
  });
  cat_menu::end_flow_layout();
}

static void draw_visuals_world_content() {
  cat_menu::begin_flow_layout("visuals_world_layout", 2);
  cat_menu::flow_panel("World", 0, 246.0f, [&]() {
    cat_menu::checkbox("Thirdperson", &config.visuals.thirdperson.enabled);
    cat_menu::checkbox("Thirdperson crosshair", &config.visuals.thirdperson.crosshair);
    cat_menu::checkbox("Thirdperson collision", &config.visuals.thirdperson.collision);
    cat_menu::slider_float("Thirdperson distance", &config.visuals.thirdperson.distance, 0.0f, 400.0f, "%.0f HU");
    cat_menu::slider_float("Thirdperson right", &config.visuals.thirdperson.right, -100.0f, 100.0f, "%.0f HU");
    cat_menu::slider_float("Thirdperson up", &config.visuals.thirdperson.up, -100.0f, 100.0f, "%.0f HU");
    cat_menu::checkbox("Thirdperson scales", &config.visuals.thirdperson.scale);
    cat_menu::checkbox("Override FOV", &config.visuals.override_fov);
    cat_menu::slider_float("Custom FOV", &config.visuals.custom_fov, 30.1f, 150.0f, "%.0f deg");
    cat_menu::checkbox("Override viewmodel FOV", &config.visuals.override_viewmodel_fov);
    cat_menu::slider_float("Viewmodel FOV", &config.visuals.custom_viewmodel_fov, 30.1f, 150.0f, "%.0f deg");
  });
  cat_menu::flow_panel("Removals", 1, 80.0f, [&]() {
    cat_menu::checkbox("Remove scope", &config.visuals.removals.scope);
    cat_menu::checkbox("Remove zoom", &config.visuals.removals.zoom);
  });
  cat_menu::end_flow_layout();
}

static void draw_visuals_ui_content() {
  const char* indicator_items[] = {
    "Random crits",
    "Tickbase",
    "Spectators",
    "Keybinds"
  };
  const uint32_t indicator_bits[] = {
    Visuals::Indicators::random_crits,
    Visuals::Indicators::tickbase,
    Visuals::Indicators::spectators,
    Visuals::Indicators::keybinds
  };

  cat_menu::begin_flow_layout("visuals_ui_layout", 2);
  cat_menu::flow_panel("UI", 0, 144.0f, [&]() {
    cat_menu::checkbox("Disable friend checks", &config.debug.disable_friend_checks);
  });
  cat_menu::flow_panel("Indicators", 1, 176.0f, [&]() {
    cat_menu::multi_select_combo("Visible widgets", &config.visuals.indicators.enabled_mask, indicator_items, indicator_bits, IM_ARRAYSIZE(indicator_items));
    cat_menu::checkbox("Show spectator target", &config.visuals.spectator_list.show_target);
    cat_menu::checkbox("Show spectator modes", &config.visuals.spectator_list.show_modes);
    cat_menu::checkbox("Highlight firstperson", &config.visuals.spectator_list.highlight_firstperson);
    cat_menu::color_picker("Firstperson color", config.visuals.spectator_list.firstperson_color.to_arr());
  });
  cat_menu::flow_panel("Feedback", 1, 188.0f, [&]() {
    cat_menu::checkbox("Hitmarker", &config.visuals.hitmarker.enabled);
    cat_menu::checkbox("Damage text", &config.visuals.hitmarker.damage_text);
    cat_menu::slider_float("Hitmarker duration", &config.visuals.hitmarker.duration, 0.20f, 1.50f, "%.2f s");
    cat_menu::slider_float("Hitmarker size", &config.visuals.hitmarker.size, 4.0f, 16.0f, "%.1f px");
    cat_menu::color_picker("Hitmarker color", config.visuals.hitmarker.color.to_arr());
    cat_menu::color_picker("Crit color", config.visuals.hitmarker.crit_color.to_arr());
    cat_menu::color_picker("Headshot color", config.visuals.hitmarker.headshot_color.to_arr());
  });
  cat_menu::end_flow_layout();
}

static void draw_visuals_tab() {
  enum visuals_page_id
  {
    visuals_page_esp,
    visuals_page_chams,
    visuals_page_glow,
    visuals_page_ui,
    visuals_page_world
  };

  static int visuals_subtab = visuals_page_esp;

  cat_menu::begin_tab_strip("##visuals_subtabs", cat_menu::k_subtab_strip_height, false, true, cat_menu::k_tab_strip_padding_x, false);
  if (cat_menu::subtab_button("ESP", visuals_subtab == visuals_page_esp)) {
    visuals_subtab = visuals_page_esp;
  }
  ImGui::SameLine(0.0f, 0.0f);
  if (cat_menu::subtab_button("Chams", visuals_subtab == visuals_page_chams)) {
    visuals_subtab = visuals_page_chams;
  }
  ImGui::SameLine(0.0f, 0.0f);
  if (cat_menu::subtab_button("Glow", visuals_subtab == visuals_page_glow)) {
    visuals_subtab = visuals_page_glow;
  }
  ImGui::SameLine(0.0f, 0.0f);
  if (cat_menu::subtab_button("UI", visuals_subtab == visuals_page_ui)) {
    visuals_subtab = visuals_page_ui;
  }
  ImGui::SameLine(0.0f, 0.0f);
  if (cat_menu::subtab_button("World", visuals_subtab == visuals_page_world)) {
    visuals_subtab = visuals_page_world;
  }
  cat_menu::end_tab_strip();

  ImGui::Dummy(ImVec2(0.0f, 4.0f));

  switch (visuals_subtab) {
    case visuals_page_esp:
      draw_esp_content();
      break;
    case visuals_page_chams:
      draw_chams_content();
      break;
    case visuals_page_glow:
      draw_glow_content();
      break;
    case visuals_page_ui:
      draw_visuals_ui_content();
      break;
    case visuals_page_world:
      draw_visuals_world_content();
      break;
  }
}

static void draw_movement_content() {
  static const char* auto_strafe_items[] = {
    "Off",
    "Legit",
    "Directional"
  };

  cat_menu::begin_flow_layout("movement_layout", 2);
  cat_menu::flow_panel("Movement", 0, 220.0f, [&]() {
    cat_menu::checkbox("Bhop", &config.misc.movement.bhop);
    cat_menu::combo("Auto strafe", (int*)&config.misc.movement.auto_strafe, auto_strafe_items, IM_ARRAYSIZE(auto_strafe_items));
    cat_menu::slider_float("Strafe turn scale", &config.misc.movement.auto_strafe_turn_scale, 0.0f, 1.0f, "%.2f");
    cat_menu::slider_float("Strafe max delta", &config.misc.movement.auto_strafe_max_delta, 0.0f, 180.0f, "%.0f deg");
    cat_menu::checkbox("No push", &config.misc.movement.no_push);
    cat_menu::checkbox("Taunt slide", &config.misc.movement.taunt_slide);
  });
  cat_menu::end_flow_layout();
}

static void draw_navbot_content() {
  const char* navbot_job_items[] = {
    "Health",
    "Ammo",
    "Capture objective",
    "Push payload",
    "Defend payload",
    "Get flag",
    "Return flag",
    "Escape danger",
    "Hold range on enemy",
    "Melee chase",
    "Sentry snipe",
    "Engineer build",
    "Engineer maintain",
    "Reload weapons",
    "Heal follow"
  };
  const uint32_t navbot_job_bits[] = {
    1u << 0,
    1u << 1,
    1u << 2,
    1u << 3,
    1u << 4,
    1u << 5,
    1u << 6,
    1u << 7,
    1u << 8,
    1u << 9,
    1u << 10,
    1u << 11,
    1u << 12,
    1u << 13,
    1u << 15
  };

  cat_menu::begin_flow_layout("navbot_layout", 2);
  cat_menu::flow_panel("NavBot", 0, 324.0f, [&]() {
    cat_menu::checkbox("Navbot", &config.misc.automation.navbot_enabled);
    cat_menu::checkbox("Draw path", &config.misc.automation.navbot_draw_path);
    cat_menu::checkbox("Don't path during warmup", &config.misc.automation.navbot_dont_path_during_warmup);
    cat_menu::checkbox("Don't path unless match started", &config.misc.automation.navbot_dont_path_unless_match_started);
    cat_menu::checkbox("Warmup only on BLU cp_/pl_", &config.misc.automation.navbot_warmup_only_blu_cp_pl);
    cat_menu::checkbox("Look at path", &config.misc.automation.navbot_look_at_path);
    cat_menu::checkbox("Auto weapon", &config.misc.automation.navbot_auto_weapon);
    cat_menu::slider_float("Look speed", &config.misc.automation.navbot_look_at_path_speed, 45.0f, 1080.0f, "%.0f deg/s");
    cat_menu::slider_float("Crumb blacklist", &config.misc.automation.navbot_crumb_blacklist_seconds, 0.0f, 60.0f, "%.0f s");
    cat_menu::multi_select_combo("Exclude jobs", &config.misc.automation.navbot_excluded_jobs_mask, navbot_job_items, navbot_job_bits, IM_ARRAYSIZE(navbot_job_items));
    cat_menu::checkbox("Debug text", &config.misc.automation.navbot_debug_text);
  });
  cat_menu::end_flow_layout();
}

static void draw_medic_content() {
  const char* heal_target_items[] = {
    "Friends",
    "Ignored",
    "IPC bots"
  };
  const uint32_t heal_target_bits[] = {
    Misc::Automation::medic_heal_target_friends,
    Misc::Automation::medic_heal_target_ignored,
    Misc::Automation::medic_heal_target_ipc_bots
  };

  cat_menu::begin_flow_layout("medic_layout", 2);
  cat_menu::flow_panel("Medic", 0, 184.0f, [&]() {
    cat_menu::checkbox("Autoheal", &config.misc.automation.medic_autoheal);
    cat_menu::checkbox("Autovacc", &config.misc.automation.medic_autovacc);
    cat_menu::checkbox("Autouber", &config.misc.automation.medic_autouber);
    cat_menu::checkbox("Auto Crossbow", &config.misc.automation.medic_auto_crossbow);
    cat_menu::multi_select_combo("Heal targets", &config.misc.automation.medic_heal_targets_mask, heal_target_items, heal_target_bits, IM_ARRAYSIZE(heal_target_items));
    cat_menu::checkbox("Heal only", &config.misc.automation.medic_heal_only);
  });
  cat_menu::end_flow_layout();
}

static void draw_region_selector_panel(const char* list_id) {
  bool region_selector_changed = cat_menu::checkbox("Enable", &config.misc.automation.region_selector);

  const float button_spacing = ImGui::GetStyle().ItemSpacing.x;
  const float button_row_width = ImMax(0.0f, ImGui::GetContentRegionAvail().x - 10.0f);
  const float button_width = ImMax(0.0f, ImFloor((button_row_width - button_spacing) * 0.5f));
  if (cat_menu::accent_button("Allow all", ImVec2(button_width, 22.0f))) {
    config.misc.automation.region_selector_allowed_mask = automation::region_selector::all_region_bits;
    region_selector_changed = true;
  }
  ImGui::SameLine(0.0f, button_spacing);
  if (cat_menu::accent_button("Block all", ImVec2(button_width, 22.0f), true)) {
    config.misc.automation.region_selector_allowed_mask = 0;
    region_selector_changed = true;
  }

  const float list_height = ImMax(1.0f, ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ItemSpacing.y);
  ImGui::BeginChild(list_id, ImVec2(-1.0f, list_height), false, ImGuiWindowFlags_NoBackground);
  for (const auto& data_center : automation::region_selector::data_centers) {
    bool allowed = automation::region_selector::is_region_bit_allowed(data_center.bit);
    if (cat_menu::checkbox(data_center.label, &allowed)) {
      automation::region_selector::set_region_allowed(data_center.bit, allowed);
      region_selector_changed = true;
    }
  }
  ImGui::EndChild();

  if (region_selector_changed) {
    automation::region_selector::refresh_ping_data();
  }
}

static void draw_cat_bot_content() {
  const char* class_items[] = { "Undefined", "Scout", "Sniper", "Soldier", "Demoman", "Medic", "Heavy", "Pyro", "Spy", "Engineer" };
  const char* queue_mode_items[] = {
    "MvM Practice",
    "MvM Mann Up",
    "Ladder 6v6",
    "Ladder 9v9",
    "Ladder 12v12",
    "Casual 6v6",
    "Casual 9v9",
    "Casual 12v12",
    "Event 12v12"
  };
  const char* requeue_action_items[] = {
    "Queue only",
    "Leave + requeue"
  };
  const char* voice_command_spam_items[] = {
    "Off",
    "Random",
    "Medic",
    "Thanks",
    "Nice Shot",
    "Cheers",
    "Jeers",
    "Go Go Go",
    "Move Up",
    "Go Left",
    "Go Right",
    "Yes",
    "No",
    "Incoming",
    "Spy",
    "Sentry Ahead",
    "Need Teleporter",
    "Pootis",
    "Need Sentry",
    "Activate Charge",
    "Help",
    "Battle Cry"
  };

  cat_menu::begin_flow_layout("cat_bot_layout", 3);
  cat_menu::flow_panel("Autojoin and Taunt", 0, 138.0f, [&]() {
    cat_menu::checkbox("Auto class select", &config.misc.automation.auto_class_select);
    cat_menu::combo("Preferred class", (int*)&config.misc.automation.class_selected, class_items, IM_ARRAYSIZE(class_items));
    cat_menu::checkbox("Auto taunt", &config.misc.automation.autotaunt);
    cat_menu::slider_float("Taunt chance", &config.misc.automation.autotaunt_chance, 0.0f, 100.0f, "%.0f%%");
    cat_menu::slider_float("Taunt safety distance", &config.misc.automation.autotaunt_safety_distance, 0.0f, 5000.0f, "%.0f HU");
    cat_menu::slider_int("Taunt weapon slot", &config.misc.automation.autotaunt_weapon_slot, 0, 5);
  });
  cat_menu::flow_panel("Autoqueue", 0, 178.0f, [&]() {
    cat_menu::checkbox("Auto queue", &config.misc.automation.auto_queue);
    cat_menu::checkbox("Auto requeue", &config.misc.automation.auto_requeue);
    cat_menu::checkbox("Requeue on kick", &config.misc.automation.requeue_on_kick);
    cat_menu::combo("Queue mode", &config.misc.automation.auto_queue_mode, queue_mode_items, IM_ARRAYSIZE(queue_mode_items));
    cat_menu::slider_int("RQ if players <=", &config.misc.automation.rq_if_players_lte, 0, 32);
    cat_menu::slider_int("RQ if players >=", &config.misc.automation.rq_if_players_gte, 0, 32);
    cat_menu::checkbox("RQ ignore friends", &config.misc.automation.rq_ignore_friends);
    cat_menu::combo("Requeue action", (int*)&config.misc.automation.requeue_action, requeue_action_items, IM_ARRAYSIZE(requeue_action_items));
  });
  cat_menu::flow_panel("Region selector", 1, 360.0f, [&]() {
    draw_region_selector_panel("##region_selector_list");
  }, false);
  cat_menu::flow_panel("Utilities", 1, 248.0f, [&]() {
    cat_menu::checkbox("Anti AFK", &config.misc.automation.anti_afk);
    cat_menu::checkbox("Anti autobalance", &config.misc.automation.anti_autobalance);
    cat_menu::checkbox("Anti MOTD", &config.misc.automation.anti_motd);
    cat_menu::checkbox("Auto report", &config.misc.automation.auto_report);
    cat_menu::checkbox("Auto vote map", &config.misc.automation.auto_vote_map);
    cat_menu::slider_int("Vote option", &config.misc.automation.auto_vote_map_option, 0, 2);
    cat_menu::checkbox("Noisemaker spam", &config.misc.automation.noisemaker_spam);
    cat_menu::combo("Voice command spam", (int*)&config.misc.automation.voice_command_spam, voice_command_spam_items, IM_ARRAYSIZE(voice_command_spam_items));
    cat_menu::checkbox("Micspam", &config.misc.automation.micspam);
    cat_menu::slider_int("Micspam on", &config.misc.automation.micspam_interval_on_seconds, 1, 600, "%d s");
    cat_menu::slider_int("Micspam off", &config.misc.automation.micspam_interval_off_seconds, 1, 600, "%d s");
  });
  cat_menu::flow_panel("AutoItem", 2, 290.0f, [&]() {
    cat_menu::checkbox("Enable", &config.misc.automation.auto_item);
    cat_menu::slider_int("Interval", &config.misc.automation.auto_item_interval_ms, 1000, 120000, "%d ms");
    cat_menu::checkbox("Weapons", &config.misc.automation.auto_item_weapons);
    cat_menu::input_text("Primary", &config.misc.automation.auto_item_primary);
    cat_menu::input_text("Secondary", &config.misc.automation.auto_item_secondary);
    cat_menu::input_text("Melee", &config.misc.automation.auto_item_melee);
    cat_menu::checkbox("Hats", &config.misc.automation.auto_item_hats);
    cat_menu::input_text("Hat 1", &config.misc.automation.auto_item_hat1);
    cat_menu::input_text("Hat 2", &config.misc.automation.auto_item_hat2);
    cat_menu::input_text("Hat 3", &config.misc.automation.auto_item_hat3);
    cat_menu::checkbox("Noisemaker", &config.misc.automation.auto_item_noisemaker);
    cat_menu::checkbox("Debug", &config.misc.automation.auto_item_debug);
  });
  cat_menu::flow_panel("MvM", 2, 142.0f, [&]() {
    cat_menu::checkbox("Instant respawn", &config.misc.automation.mvm_instant_respawn);
    cat_menu::checkbox("Instant revive", &config.misc.automation.mvm_instant_revive);
    cat_menu::checkbox("Allow inspect", &config.misc.automation.allow_mvm_inspect);
    cat_menu::checkbox("Auto ready up", &config.misc.automation.auto_mvm_ready_up);
    cat_menu::checkbox("Buybot", &config.misc.automation.mvm_buybot);
    cat_menu::slider_int("Buybot max cash", &config.misc.automation.mvm_buybot_max_cash, 0, 50000);
  });
  cat_menu::end_flow_layout();
}

static void draw_chat_content() {
  static const char* chatspam_items[] = {
    "Off",
    "Cathook",
    "LMAOBOX",
    "Custom"
  };
  static const char* killsay_items[] = {
    "Off",
    "Cathook",
    "MLG",
    "Custom"
  };

  cat_menu::begin_flow_layout("chat_layout", 3);
  cat_menu::flow_panel("Chat spam", 0, 166.0f, [&]() {
    cat_menu::combo("Chatspam", (int*)&config.misc.automation.chatspam, chatspam_items, IM_ARRAYSIZE(chatspam_items));
    cat_menu::input_text("Spam file", &config.misc.automation.chatspam_file);
    cat_menu::checkbox("Random order", &config.misc.automation.chatspam_random);
    cat_menu::checkbox("Team chat", &config.misc.automation.chatspam_team);
    cat_menu::slider_int("Spam delay", &config.misc.automation.chatspam_delay_ms, 250, 60000);
  });
  cat_menu::flow_panel("Killsay", 1, 128.0f, [&]() {
    cat_menu::combo("Killsay", (int*)&config.misc.automation.killsay, killsay_items, IM_ARRAYSIZE(killsay_items));
    cat_menu::input_text("Killsay file", &config.misc.automation.killsay_file);
    cat_menu::slider_int("Killsay delay", &config.misc.automation.killsay_delay_ms, 0, 10000);
  });
  cat_menu::end_flow_layout();
}

static void draw_queue_content() {
  const char* class_items[] = { "Undefined", "Scout", "Sniper", "Soldier", "Demoman", "Medic", "Heavy", "Pyro", "Spy", "Engineer" };
  const char* queue_mode_items[] = {
    "MvM Practice",
    "MvM Mann Up",
    "Ladder 6v6",
    "Ladder 9v9",
    "Ladder 12v12",
    "Casual 6v6",
    "Casual 9v9",
    "Casual 12v12",
    "Event 12v12"
  };
  const char* requeue_action_items[] = {
    "Queue only",
    "Leave + requeue"
  };

  cat_menu::begin_flow_layout("queue_layout", 3);
  cat_menu::flow_panel("Class", 0, 104.0f, [&]() {
    cat_menu::checkbox("Auto class select", &config.misc.automation.auto_class_select);
    cat_menu::combo("Preferred class", (int*)&config.misc.automation.class_selected, class_items, IM_ARRAYSIZE(class_items));
  });
  cat_menu::flow_panel("Queue", 1, 210.0f, [&]() {
    cat_menu::checkbox("Auto queue", &config.misc.automation.auto_queue);
    cat_menu::checkbox("Auto requeue", &config.misc.automation.auto_requeue);
    cat_menu::checkbox("Requeue on kick", &config.misc.automation.requeue_on_kick);
    cat_menu::combo("Queue mode", &config.misc.automation.auto_queue_mode, queue_mode_items, IM_ARRAYSIZE(queue_mode_items));
    cat_menu::slider_int("RQ if players <=", &config.misc.automation.rq_if_players_lte, 0, 32);
    cat_menu::slider_int("RQ if players >=", &config.misc.automation.rq_if_players_gte, 0, 32);
    cat_menu::checkbox("RQ ignore friends", &config.misc.automation.rq_ignore_friends);
    cat_menu::combo("Requeue action", (int*)&config.misc.automation.requeue_action, requeue_action_items, IM_ARRAYSIZE(requeue_action_items));
  });
  cat_menu::flow_panel("Region selector", 2, 390.0f, [&]() {
    draw_region_selector_panel("##queue_region_selector_list");
  }, false);
  cat_menu::end_flow_layout();
}

static void draw_automation_utilities_content() {
  const char* voice_command_spam_items[] = {
    "Off",
    "Random",
    "Medic",
    "Thanks",
    "Nice Shot",
    "Cheers",
    "Jeers",
    "Go Go Go",
    "Move Up",
    "Go Left",
    "Go Right",
    "Yes",
    "No",
    "Incoming",
    "Spy",
    "Sentry Ahead",
    "Need Teleporter",
    "Pootis",
    "Need Sentry",
    "Activate Charge",
    "Help",
    "Battle Cry"
  };

  cat_menu::begin_flow_layout("automation_utilities_layout", 3);
  cat_menu::flow_panel("General", 0, 190.0f, [&]() {
    cat_menu::checkbox("Anti AFK", &config.misc.automation.anti_afk);
    cat_menu::checkbox("Anti autobalance", &config.misc.automation.anti_autobalance);
    cat_menu::checkbox("Anti MOTD", &config.misc.automation.anti_motd);
    cat_menu::checkbox("Auto report", &config.misc.automation.auto_report);
    cat_menu::checkbox("Auto vote map", &config.misc.automation.auto_vote_map);
    cat_menu::slider_int("Vote option", &config.misc.automation.auto_vote_map_option, 0, 2);
    cat_menu::checkbox("Custom announcer", &config.misc.automation.custom_announcer);
  });
  cat_menu::flow_panel("Spam", 1, 170.0f, [&]() {
    cat_menu::checkbox("Noisemaker spam", &config.misc.automation.noisemaker_spam);
    cat_menu::combo("Voice command spam", (int*)&config.misc.automation.voice_command_spam, voice_command_spam_items, IM_ARRAYSIZE(voice_command_spam_items));
    cat_menu::checkbox("Micspam", &config.misc.automation.micspam);
    cat_menu::slider_int("Micspam on", &config.misc.automation.micspam_interval_on_seconds, 1, 600, "%d s");
    cat_menu::slider_int("Micspam off", &config.misc.automation.micspam_interval_off_seconds, 1, 600, "%d s");
  });
  cat_menu::flow_panel("Taunt", 2, 128.0f, [&]() {
    cat_menu::checkbox("Auto taunt", &config.misc.automation.autotaunt);
    cat_menu::slider_float("Taunt chance", &config.misc.automation.autotaunt_chance, 0.0f, 100.0f, "%.0f%%");
    cat_menu::slider_float("Taunt safety distance", &config.misc.automation.autotaunt_safety_distance, 0.0f, 5000.0f, "%.0f HU");
    cat_menu::slider_int("Taunt weapon slot", &config.misc.automation.autotaunt_weapon_slot, 0, 5);
  });
  cat_menu::flow_panel("MvM", 2, 142.0f, [&]() {
    cat_menu::checkbox("Instant respawn", &config.misc.automation.mvm_instant_respawn);
    cat_menu::checkbox("Instant revive", &config.misc.automation.mvm_instant_revive);
    cat_menu::checkbox("Allow inspect", &config.misc.automation.allow_mvm_inspect);
    cat_menu::checkbox("Auto ready up", &config.misc.automation.auto_mvm_ready_up);
    cat_menu::checkbox("Buybot", &config.misc.automation.mvm_buybot);
    cat_menu::slider_int("Buybot max cash", &config.misc.automation.mvm_buybot_max_cash, 0, 50000);
  });
  cat_menu::end_flow_layout();
}

static void draw_autoitem_content() {
  cat_menu::begin_flow_layout("autoitem_layout", 3);
  cat_menu::flow_panel("AutoItem", 0, 116.0f, [&]() {
    cat_menu::checkbox("Enable", &config.misc.automation.auto_item);
    cat_menu::slider_int("Interval", &config.misc.automation.auto_item_interval_ms, 1000, 120000, "%d ms");
    cat_menu::checkbox("Debug", &config.misc.automation.auto_item_debug);
  });
  cat_menu::flow_panel("Weapons", 1, 156.0f, [&]() {
    cat_menu::checkbox("Weapons", &config.misc.automation.auto_item_weapons);
    cat_menu::input_text("Primary", &config.misc.automation.auto_item_primary);
    cat_menu::input_text("Secondary", &config.misc.automation.auto_item_secondary);
    cat_menu::input_text("Melee", &config.misc.automation.auto_item_melee);
  });
  cat_menu::flow_panel("Cosmetics", 2, 182.0f, [&]() {
    cat_menu::checkbox("Hats", &config.misc.automation.auto_item_hats);
    cat_menu::input_text("Hat 1", &config.misc.automation.auto_item_hat1);
    cat_menu::input_text("Hat 2", &config.misc.automation.auto_item_hat2);
    cat_menu::input_text("Hat 3", &config.misc.automation.auto_item_hat3);
    cat_menu::checkbox("Noisemaker", &config.misc.automation.auto_item_noisemaker);
  });
  cat_menu::end_flow_layout();
}

static void draw_ipc_content() {
  const bool connected = cat_ipc::client::connected();
  const int peer_id = cat_ipc::client::peer_id();

  cat_menu::begin_flow_layout("ipc_layout", 3);
  cat_menu::flow_panel("Connection", 0, 156.0f, [&]() {
    cat_menu::checkbox("Enable IPC", &config.ipc.enabled);
    cat_menu::checkbox("Auto connect", &config.ipc.auto_connect);
    cat_menu::checkbox("Auto ignore local bots", &config.ipc.auto_ignore_local_bots);

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, connected ? cat_menu::k_accent : cat_menu::k_text_soft);
    if (connected) {
      ImGui::Text("Status: connected as peer %d", peer_id);
    } else {
      ImGui::TextUnformatted("Status: disconnected");
    }
    ImGui::PopStyleColor();
  });
  cat_menu::flow_panel("Actions", 1, 112.0f, [&]() {
    const float button_width = (ImGui::GetContentRegionAvail().x - 6.0f) * 0.5f;
    if (cat_menu::accent_button("Connect", ImVec2(button_width, 22.0f))) {
      config.ipc.enabled = true;
      cat_ipc::client::set_enabled(true);
      cat_ipc::client::start();
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (cat_menu::accent_button("Reconnect", ImVec2(0.0f, 22.0f))) {
      config.ipc.enabled = true;
      cat_ipc::client::shutdown();
      cat_ipc::client::set_enabled(true);
      cat_ipc::client::start();
    }
    if (cat_menu::accent_button("Disconnect", ImVec2(0.0f, 22.0f), true)) {
      config.ipc.enabled = false;
      cat_ipc::client::shutdown();
    }
  });
  cat_menu::flow_panel("Notes", 2, 94.0f, [&]() {
    ImGui::PushStyleColor(ImGuiCol_Text, cat_menu::k_text_soft);
    ImGui::TextUnformatted("Connect uses the catbot shared memory server.");
    ImGui::TextUnformatted("Auto connect retries while IPC is enabled.");
    ImGui::TextUnformatted("Disconnect disables IPC until enabled again.");
    ImGui::PopStyleColor();
  });
  cat_menu::end_flow_layout();
}

static void draw_automation_tab() {
  enum automation_page_id
  {
    automation_page_queue,
    automation_page_utilities,
    automation_page_autoitem,
    automation_page_chat,
    automation_page_medic,
    automation_page_navbot,
    automation_page_ipc
  };

  static int automation_subtab = automation_page_queue;

  cat_menu::begin_tab_strip("##automation_subtabs", cat_menu::k_subtab_strip_height, false, true, cat_menu::k_tab_strip_padding_x, false);
  if (cat_menu::subtab_button("Queue", automation_subtab == automation_page_queue)) {
    automation_subtab = automation_page_queue;
  }
  ImGui::SameLine(0.0f, 0.0f);
  if (cat_menu::subtab_button("Utilities", automation_subtab == automation_page_utilities)) {
    automation_subtab = automation_page_utilities;
  }
  ImGui::SameLine(0.0f, 0.0f);
  if (cat_menu::subtab_button("AutoItem", automation_subtab == automation_page_autoitem)) {
    automation_subtab = automation_page_autoitem;
  }
  ImGui::SameLine(0.0f, 0.0f);
  if (cat_menu::subtab_button("Chat", automation_subtab == automation_page_chat)) {
    automation_subtab = automation_page_chat;
  }
  ImGui::SameLine(0.0f, 0.0f);
  if (cat_menu::subtab_button("Medic", automation_subtab == automation_page_medic)) {
    automation_subtab = automation_page_medic;
  }
  ImGui::SameLine(0.0f, 0.0f);
  if (cat_menu::subtab_button("NavBot", automation_subtab == automation_page_navbot)) {
    automation_subtab = automation_page_navbot;
  }
  ImGui::SameLine(0.0f, 0.0f);
  if (cat_menu::subtab_button("IPC", automation_subtab == automation_page_ipc)) {
    automation_subtab = automation_page_ipc;
  }
  cat_menu::end_tab_strip();
  ImGui::Dummy(ImVec2(0.0f, 4.0f));

  switch (automation_subtab) {
    case automation_page_queue:
      draw_queue_content();
      break;
    case automation_page_utilities:
      draw_automation_utilities_content();
      break;
    case automation_page_autoitem:
      draw_autoitem_content();
      break;
    case automation_page_chat:
      draw_chat_content();
      break;
    case automation_page_medic:
      draw_medic_content();
      break;
    case automation_page_navbot:
      draw_navbot_content();
      break;
    case automation_page_ipc:
      draw_ipc_content();
      break;
  }
}

static void draw_exploits_content() {
  static const char* anti_aim_pitch_items[] = {
    "Off",
    "Up",
    "Down",
    "Zero",
    "Half up",
    "Half down",
    "Jitter",
    "Random"
  };
  static const char* anti_aim_yaw_base_items[] = {
    "View",
    "Target"
  };
  static const char* anti_aim_yaw_items[] = {
    "Off",
    "Forward",
    "Left",
    "Right",
    "Backwards",
    "Jitter",
    "Spin",
    "Random",
    "Sideways"
  };

  cat_menu::begin_flow_layout("exploits_layout", 3);
  cat_menu::flow_panel("Bypasses", 0, 182.0f, [&]() {
    cat_menu::checkbox("Bypass sv_pure", &config.misc.exploits.bypasspure);
    cat_menu::checkbox("Pure bypass", &config.misc.exploits.pure_bypass);
    cat_menu::checkbox("Cheats bypass", &config.misc.exploits.cheats_bypass);
    cat_menu::checkbox("VAC bypass", &config.misc.exploits.vac_bypass);
    cat_menu::checkbox("Network fix", &config.misc.exploits.network_fix);
    cat_menu::checkbox("No engine sleep", &config.misc.exploits.no_engine_sleep);
    cat_menu::checkbox("Null graphics", &config.misc.exploits.null_graphics);
    cat_menu::checkbox("Null render stubs", &config.misc.exploits.null_graphics_render_stubs);
  });
  cat_menu::flow_panel("Tickbase", 1, 224.0f, [&]() {
    cat_menu::checkbox("Tickbase", &config.misc.exploits.tickbase);
    cat_menu::checkbox("Recharge", &config.misc.exploits.tickbase_recharge);
    cat_menu::checkbox("Doubletap", &config.misc.exploits.doubletap);
    cat_menu::slider_int("Doubletap ticks", &config.misc.exploits.doubletap_ticks, 1, 24);
    cat_menu::checkbox("Warp", &config.misc.exploits.warp);
    cat_menu::slider_int("Warp ticks", &config.misc.exploits.warp_ticks, 1, 24);
    cat_menu::checkbox("Fakelag", &config.misc.exploits.fakelag);
    cat_menu::slider_int("Fakelag ticks", &config.misc.exploits.fakelag_ticks, 1, 21);
    cat_menu::checkbox("Antiwarp", &config.misc.exploits.antiwarp);
  });
  cat_menu::flow_panel("Engine", 0, 118.0f, [&]() {
    cat_menu::checkbox("Bones optimization", &config.misc.exploits.setup_bones_optimization);
    cat_menu::checkbox("Equip region unlock", &config.misc.exploits.equip_region_unlock);
    cat_menu::checkbox("Ping reducer", &config.misc.exploits.ping_reducer);
    cat_menu::slider_int("Ping target", &config.misc.exploits.ping_target, 1, 100);
  });
  cat_menu::flow_panel("Anti-aim", 2, 286.0f, [&]() {
    cat_menu::checkbox("Enable", &config.misc.exploits.anti_aim);
    cat_menu::combo("Real pitch", (int*)&config.misc.exploits.anti_aim_real_pitch, anti_aim_pitch_items, IM_ARRAYSIZE(anti_aim_pitch_items));
    cat_menu::combo("Fake pitch", (int*)&config.misc.exploits.anti_aim_fake_pitch, anti_aim_pitch_items, IM_ARRAYSIZE(anti_aim_pitch_items));
    cat_menu::combo("Real base", (int*)&config.misc.exploits.anti_aim_real_yaw_base, anti_aim_yaw_base_items, IM_ARRAYSIZE(anti_aim_yaw_base_items));
    cat_menu::combo("Fake base", (int*)&config.misc.exploits.anti_aim_fake_yaw_base, anti_aim_yaw_base_items, IM_ARRAYSIZE(anti_aim_yaw_base_items));
    cat_menu::combo("Real yaw", (int*)&config.misc.exploits.anti_aim_real_yaw, anti_aim_yaw_items, IM_ARRAYSIZE(anti_aim_yaw_items));
    cat_menu::combo("Fake yaw", (int*)&config.misc.exploits.anti_aim_fake_yaw, anti_aim_yaw_items, IM_ARRAYSIZE(anti_aim_yaw_items));
    cat_menu::slider_float("Real offset", &config.misc.exploits.anti_aim_real_yaw_offset, -180.0f, 180.0f, "%.0f deg");
    cat_menu::slider_float("Fake offset", &config.misc.exploits.anti_aim_fake_yaw_offset, -180.0f, 180.0f, "%.0f deg");
    cat_menu::slider_float("Spin speed", &config.misc.exploits.anti_aim_spin_speed, -180.0f, 180.0f, "%.0f deg");
    cat_menu::checkbox("Anti-overlap", &config.misc.exploits.anti_aim_anti_overlap);
  });
  cat_menu::end_flow_layout();
}

static void draw_misc_content() {
  enum misc_subtab_id
  {
    misc_subtab_collective,
    misc_subtab_exploits
  };

  static int misc_subtab = misc_subtab_collective;

  cat_menu::begin_tab_strip("##misc_subtabs", cat_menu::k_subtab_strip_height, false, true, cat_menu::k_tab_strip_padding_x, false);
  if (cat_menu::subtab_button("Collective", misc_subtab == misc_subtab_collective)) {
    misc_subtab = misc_subtab_collective;
  }
  ImGui::SameLine(0.0f, 0.0f);
  if (cat_menu::subtab_button("Exploits", misc_subtab == misc_subtab_exploits)) {
    misc_subtab = misc_subtab_exploits;
  }
  cat_menu::end_tab_strip();
  ImGui::Dummy(ImVec2(0.0f, 4.0f));

  cat_menu::begin_flow_layout("misc_layout", 2);
  if (misc_subtab == misc_subtab_collective) {
    cat_menu::flow_panel("Collective", 0, 96.0f, [&]() {
      cat_menu::checkbox("Custom announcer", &config.misc.automation.custom_announcer);
    });
  } else {
    static const char* anti_aim_pitch_items[] = {
      "Off",
      "Up",
      "Down",
      "Zero",
      "Half up",
      "Half down",
      "Jitter",
      "Random"
    };
    static const char* anti_aim_yaw_base_items[] = {
      "View",
      "Target"
    };
    static const char* anti_aim_yaw_items[] = {
      "Off",
      "Forward",
      "Left",
      "Right",
      "Backwards",
      "Jitter",
      "Spin",
      "Random",
      "Sideways"
    };

    cat_menu::flow_panel("Exploits", 0, 248.0f, [&]() {
      cat_menu::checkbox("Bypass sv_pure", &config.misc.exploits.bypasspure);
      cat_menu::checkbox("Pure bypass", &config.misc.exploits.pure_bypass);
      cat_menu::checkbox("Cheats bypass", &config.misc.exploits.cheats_bypass);
      cat_menu::checkbox("VAC bypass", &config.misc.exploits.vac_bypass);
      cat_menu::checkbox("Network fix", &config.misc.exploits.network_fix);
      cat_menu::checkbox("Tickbase", &config.misc.exploits.tickbase);
      cat_menu::checkbox("Recharge", &config.misc.exploits.tickbase_recharge);
      cat_menu::checkbox("Doubletap", &config.misc.exploits.doubletap);
      cat_menu::slider_int("Doubletap ticks", &config.misc.exploits.doubletap_ticks, 1, 24);
      cat_menu::checkbox("Warp", &config.misc.exploits.warp);
      cat_menu::slider_int("Warp ticks", &config.misc.exploits.warp_ticks, 1, 24);
      cat_menu::checkbox("Fakelag", &config.misc.exploits.fakelag);
      cat_menu::slider_int("Fakelag ticks", &config.misc.exploits.fakelag_ticks, 1, 21);
      cat_menu::checkbox("Antiwarp", &config.misc.exploits.antiwarp);
      cat_menu::checkbox("Bones optimization", &config.misc.exploits.setup_bones_optimization);
      cat_menu::checkbox("Equip region unlock", &config.misc.exploits.equip_region_unlock);
      cat_menu::checkbox("Ping reducer", &config.misc.exploits.ping_reducer);
      cat_menu::slider_int("Ping target", &config.misc.exploits.ping_target, 1, 100);
      cat_menu::checkbox("No engine sleep", &config.misc.exploits.no_engine_sleep);
      cat_menu::checkbox("Null graphics", &config.misc.exploits.null_graphics);
      cat_menu::checkbox("Null render stubs", &config.misc.exploits.null_graphics_render_stubs);
    });
    cat_menu::flow_panel("Anti-aim", 1, 286.0f, [&]() {
      cat_menu::checkbox("Enable", &config.misc.exploits.anti_aim);
      cat_menu::combo("Real pitch", (int*)&config.misc.exploits.anti_aim_real_pitch, anti_aim_pitch_items, IM_ARRAYSIZE(anti_aim_pitch_items));
      cat_menu::combo("Fake pitch", (int*)&config.misc.exploits.anti_aim_fake_pitch, anti_aim_pitch_items, IM_ARRAYSIZE(anti_aim_pitch_items));
      cat_menu::combo("Real base", (int*)&config.misc.exploits.anti_aim_real_yaw_base, anti_aim_yaw_base_items, IM_ARRAYSIZE(anti_aim_yaw_base_items));
      cat_menu::combo("Fake base", (int*)&config.misc.exploits.anti_aim_fake_yaw_base, anti_aim_yaw_base_items, IM_ARRAYSIZE(anti_aim_yaw_base_items));
      cat_menu::combo("Real yaw", (int*)&config.misc.exploits.anti_aim_real_yaw, anti_aim_yaw_items, IM_ARRAYSIZE(anti_aim_yaw_items));
      cat_menu::combo("Fake yaw", (int*)&config.misc.exploits.anti_aim_fake_yaw, anti_aim_yaw_items, IM_ARRAYSIZE(anti_aim_yaw_items));
      cat_menu::slider_float("Real offset", &config.misc.exploits.anti_aim_real_yaw_offset, -180.0f, 180.0f, "%.0f deg");
      cat_menu::slider_float("Fake offset", &config.misc.exploits.anti_aim_fake_yaw_offset, -180.0f, 180.0f, "%.0f deg");
      cat_menu::slider_float("Spin speed", &config.misc.exploits.anti_aim_spin_speed, -180.0f, 180.0f, "%.0f deg");
      cat_menu::checkbox("Anti-overlap", &config.misc.exploits.anti_aim_anti_overlap);
    });
  }
  cat_menu::end_flow_layout();
}

static void draw_debug_content() {
  cat_menu::begin_flow_layout("debug_layout", 2);
  cat_menu::flow_panel("Debug", 0, 252.0f, [&]() {
    auto& font_names = cat_menu::available_font_names();
    std::vector<const char*> font_name_items{};
    font_name_items.reserve(font_names.size());
    for (const std::string& name : font_names) {
      font_name_items.emplace_back(name.c_str());
    }

    int selected_font = 0;
    if (config.misc.menu.use_custom_font) {
      for (int index = 1; index < static_cast<int>(font_names.size()); ++index) {
        if (font_names[static_cast<size_t>(index)] == config.misc.menu.custom_font) {
          selected_font = index;
          break;
        }
      }
    }

    if (cat_menu::combo("Menu font", &selected_font, font_name_items.data(), static_cast<int>(font_name_items.size()))) {
      config.misc.menu.use_custom_font = selected_font > 0;
      if (selected_font > 0) {
        config.misc.menu.custom_font = font_names[static_cast<size_t>(selected_font)];
      } else {
        config.misc.menu.custom_font.clear();
      }
    }
    cat_menu::checkbox("Draw all entities", &config.debug.debug_render_all_entities);
    cat_menu::checkbox("Show active flag IDs", &config.debug.show_active_flag_ids_of_players);
    cat_menu::checkbox("Disable friend checks", &config.debug.disable_friend_checks);
  });
  cat_menu::end_flow_layout();
}

static void draw_config_content() {
  cathook::core::config_store* config_store = cathook::core::get_config_store();
  if (config_store == nullptr) {
    cat_menu::begin_panel("Configs", ImVec2(0.0f, 0.0f));
    ImGui::TextUnformatted("Config store unavailable");
    cat_menu::end_panel();
    return;
  }

  cat_menu::begin_flow_layout("config_layout", 2);
  cat_menu::flow_panel("Config List", 0, 330.0f, [&]() {
    const std::vector<std::string> configs = config_store->list_files();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##config_list_box", ImVec2(-1.0f, 250.0f), false, ImGuiWindowFlags_NoBackground);
    if (configs.empty()) {
      ImGui::SetCursorPosY(8.0f);
      ImGui::PushStyleColor(ImGuiCol_Text, cat_menu::k_text_soft);
      ImGui::TextUnformatted("No configs found.");
      ImGui::PopStyleColor();
    } else {
      for (int index = 0; index < static_cast<int>(configs.size()); ++index) {
        const bool selected = selected_config == index;
        if (cat_menu::list_row(configs[index].c_str(), selected)) {
          selected_config = index;
          std::strncpy(config_name, configs[index].c_str(), std::size(config_name) - 1);
          config_name[std::size(config_name) - 1] = '\0';
        }
      }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, cat_menu::k_text_soft);
    ImGui::Text("Stored configs: %d", static_cast<int>(configs.size()));
    ImGui::Text("Current: %s", config_store->current_name().c_str());
    ImGui::PopStyleColor();
  });
  cat_menu::flow_panel("Config Options", 1, 186.0f, [&]() {
    cat_menu::input_text("Config name", config_name, static_cast<int>(std::size(config_name)));
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    if (cat_menu::accent_button("Create", ImVec2((ImGui::GetContentRegionAvail().x - 6.0f) * 0.5f, 22.0f))) {
      config_store->import_config(config);
      cat_bind::save(config_store);
      config_store->save_file(config_name);
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (cat_menu::accent_button("Save", ImVec2(0.0f, 22.0f))) {
      config_store->import_config(config);
      cat_bind::save(config_store);
      config_store->save_file(config_name);
    }
    if (cat_menu::accent_button("Load", ImVec2((ImGui::GetContentRegionAvail().x - 6.0f) * 0.5f, 22.0f))) {
      if (config_store->load_file(config_name)) {
        config_store->export_config(config);
        cat_bind::load(config_store);
      }
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (cat_menu::accent_button("Delete", ImVec2(0.0f, 22.0f), true)) {
      config_store->delete_file(config_name);
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, cat_menu::k_text_soft);
    ImGui::TextUnformatted("Actions save the current in-memory config.");
    ImGui::TextUnformatted("Load replaces the current settings from disk.");
    ImGui::PopStyleColor();
  });
  cat_menu::end_flow_layout();
}

static void draw_system_tab() {
  enum system_page_id
  {
    system_page_config,
    system_page_debug
  };

  static int system_subtab = system_page_config;

  cat_menu::begin_tab_strip("##system_subtabs", cat_menu::k_subtab_strip_height, false, true, cat_menu::k_tab_strip_padding_x, false);
  if (cat_menu::subtab_button("Config", system_subtab == system_page_config)) {
    system_subtab = system_page_config;
  }
  ImGui::SameLine(0.0f, 0.0f);
  if (cat_menu::subtab_button("Debug", system_subtab == system_page_debug)) {
    system_subtab = system_page_debug;
  }
  cat_menu::end_tab_strip();
  ImGui::Dummy(ImVec2(0.0f, 4.0f));

  switch (system_subtab) {
    case system_page_config:
      draw_config_content();
      break;
    case system_page_debug:
      draw_debug_content();
      break;
  }
}

static void draw_menu(void) {
  cat_menu::ensure_fonts();
  ImGui::SetNextWindowSize(cat_menu::k_menu_size, ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImVec2(80.0f, 70.0f), ImGuiCond_Once);
  ImGui::SetNextWindowBgAlpha(0.0f);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
  ImGui::PushStyleColor(ImGuiCol_Text, cat_menu::k_text);
  ImGui::PushStyleColor(ImGuiCol_CheckMark, cat_menu::k_text);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, cat_menu::k_frame);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, cat_menu::k_frame_hover);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, cat_menu::k_accent_soft);
  ImGui::PushStyleColor(ImGuiCol_Border, cat_menu::k_line);
  ImGui::PushStyleColor(ImGuiCol_Separator, cat_menu::with_alpha(cat_menu::k_line, 0.9f));
  ImGui::PushStyleColor(ImGuiCol_Header, cat_menu::k_accent_soft);
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, cat_menu::k_frame_hover);
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, cat_menu::k_accent_soft);
  ImGui::PushStyleColor(ImGuiCol_Button, cat_menu::k_frame);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, cat_menu::k_frame_hover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, cat_menu::k_accent_soft);
  ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, cat_menu::k_bg_panel);
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, cat_menu::k_frame_hover);
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, cat_menu::k_line);
  ImGui::PushFont(cat_menu::font_regular());

  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  if (!ImGui::Begin("cathook", nullptr, flags)) {
    ImGui::End();
    ImGui::PopFont();
    ImGui::PopStyleColor(16);
    ImGui::PopStyleVar(6);
    return;
  }

  enum cathook_tab_id
  {
    cathook_tab_combat,
    cathook_tab_visuals,
    cathook_tab_movement,
    cathook_tab_automation,
    cathook_tab_exploits,
    cathook_tab_system
  };

  static int tab = cathook_tab_combat;

  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetWindowPos();
  ImVec2 size = ImGui::GetWindowSize();
  ImVec2 max = ImVec2(pos.x + size.x, pos.y + size.y);

  draw_list->AddRectFilled(pos, max, ImGui::GetColorU32(cat_menu::k_bg_outer), 0.0f);
  draw_list->AddRect(pos, max, ImGui::GetColorU32(cat_menu::k_line), 0.0f, 0, 1.0f);
  draw_list->AddLine(ImVec2(pos.x, pos.y + cat_menu::k_title_height), ImVec2(max.x, pos.y + cat_menu::k_title_height), ImGui::GetColorU32(cat_menu::k_line), 1.0f);

  const char* title = "Main Window";
  const ImVec2 title_size = cat_menu::font_regular()->CalcTextSizeA(cat_menu::font_regular()->LegacySize, FLT_MAX, 0.0f, title);
  draw_list->AddText(cat_menu::font_regular(), cat_menu::font_regular()->LegacySize, ImVec2(pos.x + 6.0f, pos.y + ((cat_menu::k_title_height - title_size.y) * 0.5f)), ImGui::GetColorU32(cat_menu::k_text), title);

  ImGui::SetCursorPos(ImVec2(size.x - 18.0f, (cat_menu::k_title_height - 14.0f) * 0.5f));
  if (cat_menu::accent_button("X##menu_close", ImVec2(14.0f, 14.0f), true)) {
    menu_focused = false;
    if (surface != nullptr) {
      surface->set_cursor_visible(false);
    }
  }

  draw_beta_notice();

  ImGui::SetCursorPos(ImVec2(0.0f, cat_menu::k_title_height));
  cat_menu::begin_tab_strip("##nav", cat_menu::k_tab_strip_height, false, false, cat_menu::k_tab_strip_padding_x);
  if (cat_menu::nav_button("Combat", tab == cathook_tab_combat)) tab = cathook_tab_combat;
  ImGui::SameLine(0.0f, 0.0f);
  if (cat_menu::nav_button("Visuals", tab == cathook_tab_visuals)) tab = cathook_tab_visuals;
  ImGui::SameLine(0.0f, 0.0f);
  if (cat_menu::nav_button("Movement", tab == cathook_tab_movement)) tab = cathook_tab_movement;
  ImGui::SameLine(0.0f, 0.0f);
  if (cat_menu::nav_button("Automation", tab == cathook_tab_automation)) tab = cathook_tab_automation;
  ImGui::SameLine(0.0f, 0.0f);
  if (cat_menu::nav_button("Exploits", tab == cathook_tab_exploits)) tab = cathook_tab_exploits;
  ImGui::SameLine(0.0f, 0.0f);
  if (cat_menu::nav_button("System", tab == cathook_tab_system)) tab = cathook_tab_system;
  cat_menu::end_tab_strip();

  ImGui::SetCursorPos(ImVec2(cat_menu::k_strip_inset_x, cat_menu::k_title_height + cat_menu::k_tab_strip_height));
  ImGui::BeginChild("##content", ImVec2(-cat_menu::k_strip_inset_x, 0.0f), false, ImGuiWindowFlags_NoBackground);

  switch (tab) {
  case cathook_tab_combat:
    draw_combat_tab();
    break;
  case cathook_tab_visuals:
    draw_visuals_tab();
    break;
  case cathook_tab_movement:
    draw_movement_content();
    break;
  case cathook_tab_automation:
    draw_automation_tab();
    break;
  case cathook_tab_exploits:
    draw_exploits_content();
    break;
  case cathook_tab_system:
    draw_system_tab();
    break;
  }

  cat_bind::draw_popup();
  ImGui::EndChild();

  draw_list->AddRect(pos, max, ImGui::GetColorU32(cat_menu::k_line), 0.0f, 0, 1.0f);
  draw_list->AddLine(ImVec2(pos.x, pos.y + cat_menu::k_title_height), ImVec2(max.x, pos.y + cat_menu::k_title_height), ImGui::GetColorU32(cat_menu::k_line), 1.0f);

  ImGui::End();
  ImGui::PopFont();
  ImGui::PopStyleColor(16);
  ImGui::PopStyleVar(6);
}

#endif
