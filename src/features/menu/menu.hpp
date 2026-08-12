/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/menu/menu.hpp
 |  Y  |   author: pupnoodle
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
// #include "features/automation/inventory_changer/inventory_changer.hpp" // Temporarily disabled.
#include "features/automation/navbot/navbot_types.hpp"
#include "features/automation/region_selector/region_selector.hpp"
#include "features/visuals/material_manager.hpp"
#include "features/visuals/groups/visual_groups.hpp"
#include "features/visuals/skybox_changer.hpp"
#include "mono/mono.hpp"
#include "mono/icon_definitions.hpp"
#include "mono/material_icons.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_stdlib.h"
#include "core/render/bytes.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <SDL2/SDL_mouse.h>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

inline static SDL_Window* sdl_window = NULL;
inline static bool menu_focused = false;
inline static bool player_manager_window_open = false;
inline static char config_name[64] = "default";
inline static int selected_config = 0;
inline static ImFont* menu_font_regular = nullptr;
inline static ImFont* menu_font_bold_small = nullptr;
inline static ImFont* menu_font_regular_large = nullptr;

namespace cat_menu
{

}

namespace cat_menu
{

constexpr float k_ui_scale{ 1.18f };
constexpr ImVec2 k_menu_size{ 820.0f, 600.0f };
constexpr float k_title_height{ 24.0f };
constexpr float k_gap{ 5.0f };
constexpr float k_button_height{ 24.0f };
constexpr float k_panel_padding_y{ 8.0f };
constexpr float k_content_padding{ 5.0f };
constexpr float k_content_height{ 531.0f };
constexpr std::array<const char*, 5> k_dpi_scale_labels{ "60%", "75%", "100%", "120%", "150%" };
constexpr std::array<float, 5> k_dpi_scale_values{ 0.60f, 0.75f, 1.00f, 1.20f, 1.50f };
inline std::array<ImFont*, k_dpi_scale_values.size()> menu_font_regular_scales{};
inline std::array<ImFont*, k_dpi_scale_values.size()> menu_font_bold_small_scales{};
inline std::array<ImFont*, k_dpi_scale_values.size()> menu_font_regular_large_scales{};
inline std::array<ImFont*, k_dpi_scale_values.size()> menu_font_icon_scales{};

constexpr ImVec4 k_bg_outer{ 0.114f, 0.184f, 0.251f, 0.985f };
constexpr ImVec4 k_bg_header{ 0.114f, 0.184f, 0.251f, 1.000f };
constexpr ImVec4 k_bg_panel{ 0.114f, 0.184f, 0.251f, 0.975f };
constexpr ImVec4 k_bg_panel_header{ 0.114f, 0.184f, 0.251f, 1.000f };
constexpr ImVec4 k_frame{ 0.114f, 0.184f, 0.251f, 1.000f };
constexpr ImVec4 k_frame_hover{ 0.160f, 0.230f, 0.320f, 1.000f };
constexpr ImVec4 k_combo_bg{ 0.132f, 0.205f, 0.283f, 1.000f };
constexpr ImVec4 k_combo_bg_hover{ 0.152f, 0.232f, 0.318f, 1.000f };
constexpr ImVec4 k_line{ 0.267f, 0.392f, 0.596f, 1.000f };
constexpr ImVec4 k_text{ 1.000f, 1.000f, 1.000f, 1.000f };
constexpr ImVec4 k_text_muted{ 0.800f, 0.800f, 0.800f, 1.000f };
constexpr ImVec4 k_text_soft{ 0.667f, 0.667f, 0.667f, 1.000f };
constexpr ImVec4 k_danger{ 0.84f, 0.30f, 0.32f, 1.0f };

inline ImVec4 menu_accent() {
  const RGBA_float color = config.misc.menu.theme_color.resolved();
  return {
    std::clamp(color.r, 0.0f, 1.0f),
    std::clamp(color.g, 0.0f, 1.0f),
    std::clamp(color.b, 0.0f, 1.0f),
    1.0f
  };
}

inline int dpi_scale_index() {
  return std::clamp(config.misc.menu.dpi_scale, 0, static_cast<int>(k_dpi_scale_values.size()) - 1);
}

inline float ui_scale() {
  return k_dpi_scale_values[static_cast<size_t>(dpi_scale_index())];
}

inline float scaled(float value) {
  return value * ui_scale();
}

inline ImVec2 scaled(const ImVec2& value) {
  return ImVec2(value.x * ui_scale(), value.y * ui_scale());
}

inline void preload_menu_font_ascii(ImFont* font, float size) {
  if (font == nullptr) {
    return;
  }

  ImFontBaked* baked = font->GetFontBaked(size);
  if (baked == nullptr) {
    return;
  }

  for (ImWchar c = 32; c <= 126; ++c) {
    baked->FindGlyph(c);
  }
}

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
  visuals_subtab_entity_profiles,
  visuals_subtab_indicators,
  visuals_subtab_map,
  visuals_subtab_other
};

inline bool combo(const char* label, int* value, const char* const items[], int item_count);
inline bool color_picker(const char* label, RGBA_float* color);
inline bool slider_float(const char* label, float* value, float minimum, float maximum, const char* format);
inline bool checkbox(const char* label, bool* value);
inline bool accent_button(const char* label, const ImVec2& size, bool danger);

#if 0 // Inventory changer UI temporarily disabled.
static constexpr const char* inventory_wear_items[] = {
  "Default", "Factory New (0.001)", "Minimal Wear (0.12)", "Field-Tested (0.37)",
  "Well-Worn (0.45)", "Battle-Scarred (0.90)"
};
static constexpr const char* inventory_sheen_items[] = {
  "None", "Team Shine", "Deadly Daffodil", "Manndarin", "Mean Green",
  "Agonizing Emerald", "Villainous Violet", "Hot Rod"
};
static constexpr const char* inventory_killstreak_items[] = { "None", "Basic", "Specialized", "Professional" };
static constexpr const char* inventory_style_items[] = { "Default", "Style 1", "Style 2", "Style 3", "Style 4" };
static constexpr const char* inventory_seed_items[] = {
  "Default", "Seed 1", "Seed 2", "Seed 3", "Seed 4", "Seed 5", "Seed 6", "Seed 7",
  "Seed 8", "Seed 9", "Seed 10", "Seed 11", "Seed 12", "Seed 13", "Seed 14", "Seed 15", "Seed 16"
};
inline void draw_inventory_slot(const char* label, Misc::InventorySlot& slot, const bool weapon) {
  ImGui::PushID(label);
  ImGui::TextUnformatted(label);
  const auto& options = inventory_changer::item_options(
    weapon ? inventory_changer::item_category::weapon : inventory_changer::item_category::wearable);
  std::vector<const char*> option_labels{};
  option_labels.reserve(options.size());
  int selected_item = 0;
  for (std::size_t index = 0; index < options.size(); ++index) {
    option_labels.push_back(options[index].label.c_str());
    if (options[index].definition == static_cast<std::uint16_t>(slot.item)) selected_item = static_cast<int>(index);
  }
  if (cat_menu::combo("Item", &selected_item, option_labels.data(), static_cast<int>(option_labels.size())) &&
      selected_item >= 0 && static_cast<std::size_t>(selected_item) < options.size()) {
    slot.item = options[static_cast<std::size_t>(selected_item)].definition;
  }
  const auto& paintkits = inventory_changer::paintkit_options();
  std::vector<const char*> paintkit_labels{};
  paintkit_labels.reserve(paintkits.size());
  int selected_paintkit = 0;
  for (std::size_t index = 0; index < paintkits.size(); ++index) {
    paintkit_labels.push_back(paintkits[index].label.c_str());
    if (paintkits[index].definition == static_cast<std::uint16_t>(slot.paintkit)) selected_paintkit = static_cast<int>(index);
  }
  if (cat_menu::combo("War paint", &selected_paintkit, paintkit_labels.data(), static_cast<int>(paintkit_labels.size())) &&
      selected_paintkit >= 0 && static_cast<std::size_t>(selected_paintkit) < paintkits.size()) {
    slot.paintkit = paintkits[static_cast<std::size_t>(selected_paintkit)].definition;
  }
  cat_menu::combo("Wear", &slot.wear, inventory_wear_items, IM_ARRAYSIZE(inventory_wear_items));
  cat_menu::combo("Seed", &slot.seed, inventory_seed_items, IM_ARRAYSIZE(inventory_seed_items));
  cat_menu::combo("Style", &slot.style, inventory_style_items, IM_ARRAYSIZE(inventory_style_items));
  cat_menu::combo("Sheen", &slot.sheen, inventory_sheen_items, IM_ARRAYSIZE(inventory_sheen_items));
  cat_menu::combo("Killstreak", &slot.killstreak, inventory_killstreak_items, IM_ARRAYSIZE(inventory_killstreak_items));
  const auto& effects = inventory_changer::effect_options();
  std::vector<const char*> effect_labels{};
  effect_labels.reserve(effects.size());
  int selected_effect = 0;
  for (std::size_t index = 0; index < effects.size(); ++index) {
    effect_labels.push_back(effects[index].label.c_str());
    if (effects[index].definition == static_cast<std::uint16_t>(slot.unusual)) selected_effect = static_cast<int>(index);
  }
  if (cat_menu::combo("Unusual", &selected_effect, effect_labels.data(), static_cast<int>(effect_labels.size())) &&
      selected_effect >= 0 && static_cast<std::size_t>(selected_effect) < effects.size()) {
    slot.unusual = effects[static_cast<std::size_t>(selected_effect)].definition;
  }
  ImGui::PopID();
}

inline void draw_inventory_definition(const char* label, int* definition, inventory_changer::item_category category) {
  const auto& options = inventory_changer::item_options(category);
  std::vector<const char*> option_labels{};
  option_labels.reserve(options.size());
  int selected = 0;
  for (std::size_t index = 0; index < options.size(); ++index) {
    option_labels.push_back(options[index].label.c_str());
    if (options[index].definition == static_cast<std::uint16_t>(*definition)) selected = static_cast<int>(index);
  }
  if (cat_menu::combo(label, &selected, option_labels.data(), static_cast<int>(option_labels.size())) &&
      selected >= 0 && static_cast<std::size_t>(selected) < options.size()) {
    *definition = options[static_cast<std::size_t>(selected)].definition;
  }
}
#endif

inline bool render_material_layers(const char* title, std::vector<chams_layer>& layers) {
  bool changed = false;
  ImGui::PushID(title);
  ImGui::TextUnformatted(title);
  const std::vector<std::string> names = materials.selectable_names();
  static int selected_material = 0;
  std::vector<const char*> name_items{};
  name_items.reserve(names.size());
  for (const std::string& name : names) name_items.push_back(name.c_str());
  if (!name_items.empty()) {
    selected_material = std::clamp(selected_material, 0, static_cast<int>(name_items.size()) - 1);
    cat_menu::combo("Add material", &selected_material, name_items.data(), static_cast<int>(name_items.size()));
    if (cat_menu::accent_button("Add layer", {-1.0f, 22.0f}, false)) {
      const std::string& name = names[static_cast<std::size_t>(selected_material)];
      if (std::ranges::find_if(layers, [&name](const chams_layer& layer) { return layer.material == name; }) == layers.end()) {
        layers.push_back(chams_layer{.material = name});
        changed = true;
      }
    }
  }
  struct reorder_payload {
    std::vector<chams_layer>* layers;
    std::size_t index;
  };
  constexpr const char* payload_type = "cathook_chams_layer";
  std::size_t pending_source = layers.size();
  std::size_t pending_destination = layers.size();
  for (std::size_t index = 0; index < layers.size();) {
    ImGui::PushID(static_cast<int>(index));
    bool remove = false;
    if (ImGui::BeginChild("material_layer_card", {0.0f, 0.0f}, ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize)) {
      ImGui::Text("%s", layers[index].material.c_str());
      ImGui::SameLine();
      remove = ImGui::SmallButton("remove");
      cat_menu::color_picker("Color", &layers[index].color);
      cat_menu::slider_float("Start distance", &layers[index].start, 0.0f, 2048.0f, "%.0f HU");
      cat_menu::slider_float("End distance", &layers[index].end, 512.0f, 8192.0f, "%.0f HU");
      if (layers[index].end < layers[index].start) layers[index].end = layers[index].start;
      cat_menu::checkbox("Fade with distance", &layers[index].smooth_alpha);
    }
    ImGui::EndChild();

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip | ImGuiDragDropFlags_SourceAllowNullID)) {
      const reorder_payload payload{&layers, index};
      ImGui::SetDragDropPayload(payload_type, &payload, sizeof(payload));
      ImGui::TextUnformatted(layers[index].material.c_str());
      ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payload_type, ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
          payload != nullptr && payload->DataSize == sizeof(reorder_payload)) {
        const auto* request = static_cast<const reorder_payload*>(payload->Data);
        if (request->layers == &layers && request->index < layers.size() && request->index != index && payload->IsDelivery()) {
          pending_source = request->index;
          pending_destination = index;
        }
      }
      ImGui::EndDragDropTarget();
    }
    ImGui::PopID();
    if (remove) {
      layers.erase(layers.begin() + static_cast<std::ptrdiff_t>(index));
      changed = true;
    } else {
      ++index;
    }
  }
  if (pending_source < layers.size() && pending_destination < layers.size() && pending_source != pending_destination) {
    chams_layer moved = std::move(layers[pending_source]);
    layers.erase(layers.begin() + static_cast<std::ptrdiff_t>(pending_source));
    const std::size_t destination = std::min(pending_destination, layers.size());
    layers.insert(layers.begin() + static_cast<std::ptrdiff_t>(destination), std::move(moved));
    changed = true;
  }
  if (layers.empty()) ImGui::TextDisabled("No materials configured.");
  ImGui::PopID();
  return changed;
}

enum automation_subtab_id
{
  automation_subtab_general,
  automation_subtab_queue,
  // automation_subtab_inventory_changer, // Temporarily disabled.
  automation_subtab_items_chat,
  automation_subtab_navbot,
  automation_subtab_medic,
  automation_subtab_ipc
};

inline ImVec4 with_alpha(ImVec4 color, float alpha) {
  color.w *= alpha;
  return color;
}

inline std::string ellipsize_text(const char* text, float max_width) {
  if (text == nullptr) {
    return {};
  }

  const std::string source = text;
  if (source.empty() || ImGui::CalcTextSize(source.c_str()).x <= max_width) {
    return source;
  }

  constexpr const char* ellipsis = "...";
  if (ImGui::CalcTextSize(ellipsis).x > max_width) {
    return ellipsis;
  }

  std::size_t low = 0;
  std::size_t high = source.size();
  while (low < high) {
    const std::size_t mid = (low + high + 1) / 2;
    std::string candidate = source.substr(0, mid);
    candidate += ellipsis;
    if (ImGui::CalcTextSize(candidate.c_str()).x <= max_width) {
      low = mid;
    } else {
      high = mid - 1;
    }
  }

  std::string result = source.substr(0, low);
  result += ellipsis;
  return result;
}

inline ImFont* font_regular();
inline ImFont* font_regular() {
  return menu_font_regular ? menu_font_regular : ImGui::GetFont();
}

inline ImFont* font_bold_small() {
  return menu_font_bold_small ? menu_font_bold_small : font_regular();
}

inline ImFont* font_regular_large() {
  return menu_font_regular_large ? menu_font_regular_large : font_regular();
}

inline ImFont* font_icons() {
  return menu_font_icon_scales[static_cast<size_t>(dpi_scale_index())];
}

inline int& selected_visual_group() {
  static int selected = 0;
  return selected;
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
  static float loaded_scale = 0.0f;
  const float scale = ui_scale();
  const int scale_index = dpi_scale_index();
  const bool wants_custom_font = config.misc.menu.use_custom_font && !config.misc.menu.custom_font.empty();
  const std::string wanted_font_name = wants_custom_font ? config.misc.menu.custom_font : std::string{};

  const bool font_selection_changed = loaded_custom_font != wants_custom_font || loaded_font_name != wanted_font_name ||
    menu_font_regular_scales[static_cast<size_t>(scale_index)] == nullptr ||
    menu_font_bold_small_scales[static_cast<size_t>(scale_index)] == nullptr ||
    menu_font_regular_large_scales[static_cast<size_t>(scale_index)] == nullptr ||
    menu_font_icon_scales[static_cast<size_t>(scale_index)] == nullptr;

  if (!font_selection_changed && loaded_scale == scale) {
    menu_font_regular = menu_font_regular_scales[static_cast<size_t>(scale_index)];
    menu_font_bold_small = menu_font_bold_small_scales[static_cast<size_t>(scale_index)];
    menu_font_regular_large = menu_font_regular_large_scales[static_cast<size_t>(scale_index)];
    io.FontDefault = menu_font_regular;
    return;
  }

  if (font_selection_changed) {
    menu_font_regular_scales.fill(nullptr);
    menu_font_bold_small_scales.fill(nullptr);
    menu_font_regular_large_scales.fill(nullptr);
    menu_font_regular = nullptr;
    menu_font_bold_small = nullptr;
    menu_font_regular_large = nullptr;
    menu_font_icon_scales.fill(nullptr);

    io.Fonts->Clear();

    ImFontConfig font_config{};
    font_config.FontDataOwnedByAtlas = false;
    font_config.OversampleH = 4;
    font_config.OversampleV = 4;
    font_config.PixelSnapH = false;

    const std::filesystem::path custom_font_path = assets_font_directory() / std::filesystem::path{ config.misc.menu.custom_font }.filename();
    const std::string custom_font_path_string = custom_font_path.string();
    bool logged_custom_font_error = false;

    for (size_t index = 0; index < k_dpi_scale_values.size(); ++index) {
      const float preset_scale = k_dpi_scale_values[index];

      ImFont* regular = nullptr;
      ImFont* bold_small = nullptr;
      ImFont* regular_large = nullptr;

      if (wants_custom_font) {
        ImFontConfig file_font_config = font_config;
        file_font_config.FontDataOwnedByAtlas = true;
        file_font_config.Flags |= ImFontFlags_NoLoadError;
        regular = io.Fonts->AddFontFromFileTTF(custom_font_path_string.c_str(), 14.0f * preset_scale, &file_font_config);
        bold_small = io.Fonts->AddFontFromFileTTF(custom_font_path_string.c_str(), 14.0f * preset_scale, &file_font_config);
        regular_large = io.Fonts->AddFontFromFileTTF(custom_font_path_string.c_str(), 16.0f * preset_scale, &file_font_config);

        if (regular == nullptr && !logged_custom_font_error) {
          cathook::core::log_raw("failed to load menu font: %s\n", custom_font_path_string.c_str());
          logged_custom_font_error = true;
        }
      }

      if (regular == nullptr) {
        regular = io.Fonts->AddFontFromMemoryTTF(font_medium_bin, sizeof(font_medium_bin), 14.0f * preset_scale, &font_config);
      }
      if (bold_small == nullptr) {
        bold_small = io.Fonts->AddFontFromMemoryTTF(font_bold_bin, sizeof(font_bold_bin), 14.0f * preset_scale, &font_config);
      }
      if (regular_large == nullptr) {
        regular_large = io.Fonts->AddFontFromMemoryTTF(font_bold_bin, sizeof(font_bold_bin), 16.0f * preset_scale, &font_config);
      }

      if (!regular) {
        regular = io.Fonts->AddFontDefault();
      }
      if (!bold_small) {
        bold_small = regular;
      }
      if (!regular_large) {
        regular_large = regular;
      }

      ImFontConfig icon_config{};
      icon_config.FontDataOwnedByAtlas = false;
      icon_config.PixelSnapH = true;
      static constexpr ImWchar icon_ranges[]{ ICON_MIN_MD, ICON_MAX_16_MD, 0 };
      menu_font_icon_scales[index] = io.Fonts->AddFontFromMemoryCompressedTTF(
        MaterialIcons_compressed_data,
        MaterialIcons_compressed_size,
        15.0f * preset_scale,
        &icon_config,
        icon_ranges);

      preload_menu_font_ascii(regular, 14.0f * preset_scale);
      preload_menu_font_ascii(bold_small, 14.0f * preset_scale);
      preload_menu_font_ascii(regular_large, 16.0f * preset_scale);

      menu_font_regular_scales[index] = regular;
      menu_font_bold_small_scales[index] = bold_small;
      menu_font_regular_large_scales[index] = regular_large;
    }

    loaded_custom_font = wants_custom_font;
    loaded_font_name = wanted_font_name;
  }

  menu_font_regular = menu_font_regular_scales[static_cast<size_t>(scale_index)];
  menu_font_bold_small = menu_font_bold_small_scales[static_cast<size_t>(scale_index)];
  menu_font_regular_large = menu_font_regular_large_scales[static_cast<size_t>(scale_index)];

  io.FontDefault = menu_font_regular;
  loaded_scale = scale;
}

inline bool accent_button(const char* label, const ImVec2& size = ImVec2(0.0f, 26.0f), const bool danger = false) {
  ImVec2 button_size = size;
  if (button_size.x == 0.0f) button_size.x = -1.0f;
  button_size.y = k_button_height;
  return mono::button(label, button_size, danger);
}

inline bool list_row(const char* label, bool selected, const ImVec2& size = ImVec2(0.0f, 24.0f)) {
  return mono::list_item(label, selected, size);
}

inline bool begin_panel(const char* name, const ImVec2& size) {
  current_panel_layout_stack().push_back({ .header_height = ImGui::GetTextLineHeight() + scaled(8.0f) });
  mono::begin_panel(name, size);
  return !ImGui::GetCurrentWindow()->SkipItems;
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
  const float gap = scaled(k_gap);
  if (column_count <= 1) {
    state.column_width = ImGui::GetContentRegionAvail().x;
  } else {
    state.column_width =
      (ImGui::GetContentRegionAvail().x - (gap * static_cast<float>(column_count - 1))) / static_cast<float>(column_count);
  }
  state.total_width = (state.column_width * static_cast<float>(column_count)) + (gap * static_cast<float>(column_count - 1));
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
  const float scaled_height = scaled(height);

  const float fallback_height = scaled_height > 0.0f ? scaled_height : scaled(96.0f);
  const float panel_height = auto_fit ? storage->GetFloat(height_key, fallback_height) : scaled_height;
  const float gap = scaled(k_gap);
  const ImVec2 position(
    state.origin.x + (static_cast<float>(column_index) * (state.column_width + gap)),
    state.origin.y + state.column_offsets[static_cast<size_t>(column_index)]);

  ImGui::SetCursorPos(position);
  const bool panel_visible = begin_panel(name, ImVec2(state.column_width, panel_height));
  cat_bind::push_panel_label(name != nullptr ? name : "");
  if (panel_visible || cat_bind::registering_targets()) {
    draw_fn();
  }
  cat_bind::pop_panel_label();
  const float required_height = end_panel_ex();
  const float next_panel_height = auto_fit && panel_visible ? ImMax(scaled(1.0f), ImCeil(required_height)) : panel_height;
  if (auto_fit && panel_visible) {
    storage->SetFloat(height_key, next_panel_height);
  }

  state.column_offsets[static_cast<size_t>(column_index)] += panel_height + gap;
}

inline void end_flow_layout() {
  auto& state = current_flow_layout();
  float height = 0.0f;
  for (float offset : state.column_offsets) {
    height = ImMax(height, offset);
  }

  if (height > 0.0f) {
    height -= scaled(k_gap);
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

  mono::end_panel();
  if (!stack.empty()) {
    stack.pop_back();
  }
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

inline bool checkbox(const char* label, bool* value) {
  if (label == nullptr || value == nullptr) return false;
  if (cat_bind::registering_targets()) {
    cat_bind::bindable_checkbox(label, value, false, false);
    return false;
  }
  const bool changed = mono::toggle(label, value);
  cat_bind::bindable_checkbox(label, value, changed, mono::last_item_interaction().hovered);
  return changed;
}

inline bool combo(const char* label, int* value, const char* const items[], const int item_count) {
  if (label == nullptr || value == nullptr || items == nullptr || item_count <= 0) return false;

  if (cat_bind::registering_targets()) {
    cat_bind::bindable_combo_int(label, value, false, items, item_count, false);
    return false;
  }

  std::vector<std::pair<std::string, int>> mono_items;
  mono_items.reserve(static_cast<size_t>(item_count));
  for (int index = 0; index < item_count; ++index) {
    mono_items.emplace_back(items[index] != nullptr ? items[index] : "", index);
  }
  const bool mono_changed = mono::select_single(label, value, mono_items);
  cat_bind::bindable_combo_int(label, value, mono_changed, items, item_count, mono::last_item_interaction().hovered);
  return mono_changed;
}

inline bool multi_select_combo(const char* label, uint32_t* value_mask, const char* const items[], const uint32_t item_bits[], const int item_count) {
  if (label == nullptr || value_mask == nullptr || items == nullptr || item_bits == nullptr || item_count <= 0) return false;

  if (cat_bind::registering_targets()) {
    cat_bind::multi_select_target(value_mask, label, false, false);
    return false;
  }

  const std::unique_ptr<bool[]> selections{ std::make_unique<bool[]>(static_cast<size_t>(item_count)) };
  for (int index = 0; index < item_count; ++index) {
    selections[static_cast<size_t>(index)] = (*value_mask & item_bits[index]) != 0;
  }
  std::vector<std::pair<std::string, bool *>> mono_items;
  mono_items.reserve(static_cast<size_t>(item_count));
  for (int index = 0; index < item_count; ++index) {
    mono_items.emplace_back(items[index] != nullptr ? items[index] : "", selections.get() + index);
  }
  const bool mono_changed = mono::select_multi(label, mono_items);
  if (mono_changed) {
    for (int index = 0; index < item_count; ++index) {
      if (selections[static_cast<size_t>(index)]) *value_mask |= item_bits[index];
      else *value_mask &= ~item_bits[index];
    }
  }
  cat_bind::multi_select_target(value_mask, label, mono_changed, mono::last_item_interaction().hovered);
  cat_bind::maybe_open_popup(reinterpret_cast<int *>(value_mask), label, mono::last_item_interaction().hovered);
  return mono_changed;
}

inline bool mask_checkbox(const char* label, uint32_t* value_mask, const uint32_t bit) {
  if (label == nullptr || value_mask == nullptr || bit == 0) return false;

  if (cat_bind::registering_targets()) {
    cat_bind::multi_select_target(value_mask, label, false, false);
    return false;
  }

  bool enabled = (*value_mask & bit) != 0;
  const bool changed = mono::toggle(label, &enabled);
  if (changed) {
    if (enabled) *value_mask |= bit;
    else *value_mask &= ~bit;
  }
  cat_bind::multi_select_target(value_mask, label, changed, mono::last_item_interaction().hovered);
  cat_bind::maybe_open_popup(reinterpret_cast<int *>(value_mask), label, mono::last_item_interaction().hovered);
  return changed;
}

inline bool slider_float(const char* label, float* value, float minimum, float maximum, const char* format) {
  if (label == nullptr || value == nullptr) return false;
  if (cat_bind::registering_targets()) {
    cat_bind::bindable_slider_float(label, value, false, minimum, maximum, format, false);
    return false;
  }
  const bool mono_changed = mono::slider_float(label, value, minimum, maximum, format);
  cat_bind::bindable_slider_float(label, value, mono_changed, minimum, maximum, format, mono::last_item_interaction().hovered);
  return mono_changed;
}

inline bool slider_int(const char* label, int* value, int minimum, int maximum, const char* format = "%d") {
  if (label == nullptr || value == nullptr) return false;
  if (cat_bind::registering_targets()) {
    cat_bind::bindable_slider_int(label, value, false, minimum, maximum, format, false);
    return false;
  }
  const bool mono_changed = mono::slider_int(label, value, minimum, maximum, format);
  cat_bind::bindable_slider_int(label, value, mono_changed, minimum, maximum, format, mono::last_item_interaction().hovered);
  return mono_changed;
}

inline bool input_text(const char* label, std::string* value) {
  if (label == nullptr || value == nullptr) return false;

  if (cat_bind::registering_targets()) {
    cat_bind::bindable_string(label, value, false, false);
    return false;
  }

  const bool mono_changed = mono::input_string(label, value);
  cat_bind::bindable_string(label, value, mono_changed, mono::last_item_interaction().hovered);
  return mono_changed;
}

inline bool input_text(const char* label, char* value, int capacity) {
  if (label == nullptr || value == nullptr || capacity <= 0) return false;

  std::string text{ value };
  const bool mono_changed = mono::input_string(label, &text);
  if (mono_changed) {
    std::snprintf(value, static_cast<size_t>(capacity), "%s", text.c_str());
  }
  return mono_changed;
}

inline bool input_key(const char* label, int* value) {
  if (label == nullptr || value == nullptr) return false;
  return mono::input_key(label, value);
}

inline bool color_picker(const char* label, RGBA_float* color) {
  ImGuiWindow* window = ImGui::GetCurrentWindow();
  if (window == nullptr || window->SkipItems || label == nullptr || color == nullptr) return false;

  if (cat_bind::registering_targets()) {
    cat_bind::bindable_color(label, color, false, false);
    checkbox("Rainbow", &color->rainbow);
    return false;
  }

  ImGui::PushID(color);
  const auto display_color = color->to_RGBA();
  mono::rgba8 value{
    static_cast<uint8_t>(display_color.r),
    static_cast<uint8_t>(display_color.g),
    static_cast<uint8_t>(display_color.b),
    static_cast<uint8_t>(display_color.a)
  };
  const bool mono_changed = mono::color_picker(label, &value);
  if (mono_changed) {
    color->r = value.r / 255.0f;
    color->g = value.g / 255.0f;
    color->b = value.b / 255.0f;
    color->a = value.a / 255.0f;
  }
  cat_bind::bindable_color(label, color, mono_changed, mono::last_item_interaction().hovered);
  const bool rainbow_changed = cat_menu::checkbox("Rainbow", &color->rainbow);
  ImGui::PopID();
  return mono_changed || rainbow_changed;
}

}

static void get_input(SDL_Event* event) {
  cat_bind::handle_input(event);
}

static void set_imgui_theme(void) {
  cat_menu::ensure_fonts();
  mono::apply_layout();
  const ImVec4 accent = cat_menu::menu_accent();
  mono::apply_colors({accent.x, accent.y, accent.z, accent.w});
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

static void draw_watermark(void) {
  const RGBA_float watermark_color{ 1.0f, 1.0f, 1.0f, 1.0f, true };
  const RGBA_float rainbow_color = watermark_color.resolved();
  const mono::color accent{ rainbow_color.r, rainbow_color.g, rainbow_color.b, rainbow_color.a };
  const mono::color text{ 1.0f, 1.0f, 1.0f, 1.0f };
  const std::vector<mono::overlay_text_line> lines{
    { "cathook by pupnoodle", accent },
    { cathook_watermark_version(), text },
    { cathook_watermark_type(), text },
    { "Press INSERT OR F11 to open/close menu and Player Manager.", text },
    { "Use mouse to navigate in menu.", text }
  };
  mono::corner_text(lines, { 8.0f, 8.0f }, cat_menu::font_regular());
}

static void draw_beta_notice(void) {
  const ImVec4 accent = cat_menu::menu_accent();
  mono::center_notice(
    "BETA",
    "*some of the features may work badly or straight up not work, please report all issues on githubs issue page.",
    { accent.x, accent.y, accent.z, accent.w },
    { cat_menu::k_text_soft.x, cat_menu::k_text_soft.y, cat_menu::k_text_soft.z, cat_menu::k_text_soft.w },
    cat_menu::font_regular_large(),
    cat_menu::font_regular());
}

static void draw_aimbot_content() {
  const char* target_items[] = { "FOV", "Distance", "Least Health", "Most Health" };
  const char* aim_at_items[] = {
    "Enemies", "Sentries", "Dispensers", "Teleporters", "MvM robots", "NPCs", "Stickies", "Bombs"
  };
  const char* ignore_items[] = {
    "Friends", "IPC bots", "Cloaked", "Invulnerable", "Party", "Unprioritized", "Invisible",
    "Dead ringer", "Vaccinator", "Disguised", "Taunting", "Team", "Sentry busters", "Unsimulated"
  };
  const char* aim_mode_items[] = { "Plain", "Smooth", "Assistive", "Psilent" };
  const char* projectile_mode_items[] = { "FOV to current", "FOV to predicted", "Distance" };
  const char* projectile_prediction_items[] = { "Extrapolation", "Move simulation" };
  const char* projectile_position_items[] = { "Auto", "Feet", "Body", "Head" };
  const char* projectile_splash_items[] = { "Direct", "Balanced", "Splash preferred" };
  const char* projectile_modifier_items[] = { "Charge weapon", "Cancel charge" };
  const uint32_t projectile_modifier_bits[] = {
    Aim::projectile_mod_charge_weapon,
    Aim::projectile_mod_cancel_charge
  };
  const uint32_t aim_at_bits[] = {
    Aim::aim_at_enemies,
    Aim::aim_at_sentries,
    Aim::aim_at_dispensers,
    Aim::aim_at_teleporters,
    Aim::aim_at_mvm_robots,
    Aim::aim_at_npcs,
    Aim::aim_at_stickies,
    Aim::aim_at_bombs
  };
  const uint32_t ignore_bits[] = {
    Aim::ignore_friends,
    Aim::ignore_ipc_bots,
    Aim::ignore_cloaked,
    Aim::ignore_invulnerable,
    Aim::ignore_party,
    Aim::ignore_unprioritized,
    Aim::ignore_invisible_players,
    Aim::ignore_dead_ringer,
    Aim::ignore_vaccinator,
    Aim::ignore_disguised,
    Aim::ignore_taunting,
    Aim::ignore_team,
    Aim::ignore_sentry_busters,
    Aim::ignore_unsimulated
  };
  const char* hitbox_items[] = { "Head", "Body", "Pelvis", "Arms", "Legs" };
  const uint32_t hitbox_bits[] = {
    aim_hitbox_mask_head,
    aim_hitbox_mask_body,
    aim_hitbox_mask_pelvis,
    aim_hitbox_mask_arms,
    aim_hitbox_mask_legs
  };
  const char* hitscan_modifier_items[] = {
    "Wait for headshot",
    "Wait for charge",
    "Body-aim if lethal",
    "Scoped only",
    "Tapfire",
    "Auto rev minigun",
    "Extinguish team",
    "Prefer medics",
    "Headshot only"
  };
  const uint32_t hitscan_modifier_bits[] = {
    Aim::hitscan_mod_wait_for_headshot,
    Aim::hitscan_mod_wait_for_charge,
    Aim::hitscan_mod_body_aim_if_lethal,
    Aim::hitscan_mod_scoped_only,
    Aim::hitscan_mod_tapfire,
    Aim::hitscan_mod_auto_rev,
    Aim::hitscan_mod_extinguish_team,
    Aim::hitscan_mod_prefer_medics,
    Aim::hitscan_mod_headshot_only
  };

  cat_menu::begin_flow_layout("aimbot_layout", 2);
  cat_menu::flow_panel("Aimbot", 0, 226.0f, [&]() {
    cat_menu::checkbox("Enable", &config.aimbot.master);
    cat_menu::checkbox("Auto shoot", &config.aimbot.auto_shoot);
    cat_menu::checkbox("Shoot through glass", &config.aimbot.shoot_through_glass);
    cat_menu::checkbox("Spread compensation", &config.aimbot.spread_compensation);
    cat_menu::checkbox("Auto resolver", &config.aimbot.resolver);
    cat_menu::checkbox("Debug overlay", &config.aimbot.debug_overlay);
    cat_menu::combo("Aim mode", (int*)&config.aimbot.aim_mode, aim_mode_items, IM_ARRAYSIZE(aim_mode_items));
    cat_menu::slider_float("Aim FOV", &config.aimbot.fov, 0.0f, 180.0f, "%.0f deg");
    cat_menu::slider_float("Assist strength", &config.aimbot.assist_strength, 0.0f, 100.0f, "%.0f%%");
    cat_menu::slider_int("Resolver yaws", &config.aimbot.resolver_max_yaws, 4, 24);
  });
  cat_menu::flow_panel("Crit Hack", 0, 116.0f, [&]() {
    cat_menu::checkbox("Enable Crit Hack", &config.crithack.enabled);
    cat_menu::checkbox("Force crits", &config.crithack.force_crits);
    cat_menu::checkbox("Always melee crit", &config.crithack.always_melee);
    cat_menu::checkbox("Avoid random crits", &config.crithack.avoid_random);
  });
  cat_menu::flow_panel("Target selection", 1, 254.0f, [&]() {
    cat_menu::combo("Target", (int*)&config.aimbot.target_type, target_items, IM_ARRAYSIZE(target_items));
    cat_menu::multi_select_combo("Aim at", &config.aimbot.aim_at, aim_at_items, aim_at_bits, IM_ARRAYSIZE(aim_at_items));
    cat_menu::multi_select_combo("Hitscan hitboxes", &config.aimbot.hitscan_hitboxes, hitbox_items, hitbox_bits, IM_ARRAYSIZE(hitbox_items));
    cat_menu::multi_select_combo("Melee hitboxes", &config.aimbot.melee_hitboxes, hitbox_items, hitbox_bits, IM_ARRAYSIZE(hitbox_items));
    cat_menu::checkbox("Melee walk to target", &config.aimbot.melee_walk_to_target);
    cat_menu::multi_select_combo("Ignore", &config.aimbot.ignore, ignore_items, ignore_bits, IM_ARRAYSIZE(ignore_items));
    cat_menu::slider_float("Invisible threshold", &config.aimbot.ignore_invisible, 0.0f, 100.0f, "%.0f%%");
    cat_menu::slider_int("Unsimulated ticks", &config.aimbot.ignore_unsimulated_ticks, 0, 21);
    cat_menu::slider_int("Max targets", &config.aimbot.max_targets, 1, 6);
  });
  cat_menu::flow_panel("Heavy", 1, 92.0f, [&]() {
    cat_menu::checkbox("Heavy auto rev", &config.aimbot.auto_rev);
    cat_menu::checkbox("Heavy auto unrev", &config.aimbot.auto_unrev);
    cat_menu::slider_float("Heavy rev threshold", &config.aimbot.auto_rev_threshold, 200.0f, 1200.0f, "%.0f HU");
  });
  cat_menu::flow_panel("Sniper", 1, 116.0f, [&]() {
    cat_menu::checkbox("Automatic scope", &config.aimbot.sniper_auto_scope);
    cat_menu::checkbox("Automatic unscope", &config.aimbot.sniper_auto_unscope);
    cat_menu::slider_float("Scope distance", &config.aimbot.sniper_scope_distance, 250.0f, 4000.0f, "%.0f HU");
    cat_menu::slider_float("Scope cancel delay", &config.aimbot.sniper_scope_cancel_time, 1.0f, 5.0f, "%.1f s");
  });
  cat_menu::flow_panel("Melee", 1, 140.0f, [&]() {
    cat_menu::checkbox("Auto backstab", &config.aimbot.melee_auto_backstab);
    cat_menu::checkbox("Ignore razorback", &config.aimbot.melee_ignore_razorback);
    cat_menu::checkbox("Swing prediction", &config.aimbot.melee_swing_prediction);
    cat_menu::checkbox("Predict lag", &config.aimbot.melee_swing_predict_lag);
    cat_menu::slider_int("Swing ticks", &config.aimbot.melee_swing_ticks, 0, 14);
    static const char* swing_validate_items[] = { "Both", "Swing", "Simulated" };
    cat_menu::combo("Swing validation", &config.aimbot.melee_swing_validate_mode,
      swing_validate_items, IM_ARRAYSIZE(swing_validate_items));
  });
  cat_menu::flow_panel("Hitscan", 1, 164.0f, [&]() {
    cat_menu::multi_select_combo("Modifiers", &config.aimbot.hitscan_modifiers, hitscan_modifier_items, hitscan_modifier_bits, IM_ARRAYSIZE(hitscan_modifier_items));
    cat_menu::slider_float("Tapfire distance", &config.aimbot.tapfire_distance, 250.0f, 2000.0f, "%.0f HU");
    cat_menu::slider_float("Multipoint scale", &config.aimbot.multipoint_scale, 0.0f, 100.0f, "%.0f%%");
    cat_menu::slider_float("Bone size subtract", &config.aimbot.bone_size_subtract, 0.0f, 12.0f, "%.1f HU");
    cat_menu::slider_float("Bone size min scale", &config.aimbot.bone_size_min_scale, 0.05f, 1.0f, "%.2f");
  });
  cat_menu::flow_panel("Projectile", 0, 320.0f, [&]() {
    cat_menu::checkbox("Enable", &config.aimbot.projectile_active);
    cat_menu::multi_select_combo("Modifiers", &config.aimbot.projectile_modifiers,
      projectile_modifier_items, projectile_modifier_bits, IM_ARRAYSIZE(projectile_modifier_items));
    cat_menu::combo("Target mode", &config.aimbot.projectile_mode, projectile_mode_items, IM_ARRAYSIZE(projectile_mode_items));
    cat_menu::combo("Prediction", (int*)&config.aimbot.projectile_prediction_mode,
      projectile_prediction_items, IM_ARRAYSIZE(projectile_prediction_items));
    cat_menu::combo("Aim position", &config.aimbot.projectile_aim_pos,
      projectile_position_items, IM_ARRAYSIZE(projectile_position_items));
    cat_menu::combo("Splash policy", &config.aimbot.projectile_splash_policy,
      projectile_splash_items, IM_ARRAYSIZE(projectile_splash_items));
    cat_menu::slider_float("FOV", &config.aimbot.projectile_fov, 0.0f, 180.0f, "%.0f deg");
    cat_menu::slider_int("Max simulation targets", &config.aimbot.projectile_max_sim_targets, 1, 6);
    cat_menu::slider_float("Max simulation time", &config.aimbot.projectile_max_sim_time, 0.25f, 5.0f, "%.2fs");
    cat_menu::slider_int("Multipoint scale", &config.aimbot.projectile_multipoint_scale, 50, 100);
    cat_menu::checkbox("Smooth flamethrowers", &config.aimbot.projectile_smooth_flamethrowers_active);
    cat_menu::slider_float("Flamethrower smooth", &config.aimbot.projectile_smooth_flamethrowers, 1.0f, 100.0f, "%.0f%%");
  });
  cat_menu::end_flow_layout();
}

static void draw_aimbot_draw_content() {
  cat_menu::begin_flow_layout("aimbot_draw_layout", 2);
  cat_menu::flow_panel("FOV circle", 0, 112.0f, [&]() {
    cat_menu::checkbox("Draw FOV", &config.aimbot.draw_fov);
  });
  cat_menu::flow_panel("Debug overlay", 1, 144.0f, [&]() {
    cat_menu::checkbox("Enable", &config.aimbot.debug_overlay);
    cat_menu::slider_float("Position X", &config.aimbot.debug_overlay_x, 0.0f, 1920.0f, "%.0f px");
    cat_menu::slider_float("Position Y", &config.aimbot.debug_overlay_y, 0.0f, 1080.0f, "%.0f px");
  });
  cat_menu::end_flow_layout();
}

static void draw_medic_content();

static void draw_combat_tab(const int combat_subtab) {
  switch (combat_subtab) {
    case 0:
      draw_aimbot_content();
      break;
    case 1:
      draw_aimbot_draw_content();
      break;
  }
}

static uint32_t group_active_bit(const int index) {
  return index >= 0 && index < static_cast<int>(visual_group_config::max_groups) ? (1u << index) : 0u;
}

static void draw_visual_group_roles(std::vector<int>& selected_roles)
{
  std::string preview = selected_roles.empty() ? "Any role" : "";
  for (const int role : selected_roles)
  {
    if (!preview.empty()) preview += ", ";
    preview += cathook::core::players::role_name(role);
  }

  if (!ImGui::BeginCombo("Roles", preview.c_str())) return;
  for (const auto& definition : cathook::core::players::role_definitions())
  {
    if (definition.id == cathook::core::players::default_role) continue;
    const bool selected = std::ranges::find(selected_roles, definition.id) != selected_roles.end();
    if (ImGui::Selectable(definition.name, selected))
    {
      if (selected)
      {
        selected_roles.erase(std::remove(selected_roles.begin(), selected_roles.end(), definition.id), selected_roles.end());
      }
      else
      {
        selected_roles.push_back(definition.id);
        std::ranges::sort(selected_roles);
      }
    }
    if (selected) ImGui::SetItemDefaultFocus();
  }
  ImGui::EndCombo();
}

static void delete_visual_group(const int selected_index, int* selected_group) {
  if (selected_index < 0 || selected_group == nullptr || selected_index >= static_cast<int>(config.visual_groups.groups.size())) return;

  uint32_t new_mask = 0;
  for (int index = 0; index < static_cast<int>(config.visual_groups.groups.size()); ++index) {
    if (index == selected_index || (config.visual_groups.active_group_mask & group_active_bit(index)) == 0) continue;
    const int new_index = index < selected_index ? index : index - 1;
    new_mask |= group_active_bit(new_index);
  }

  config.visual_groups.groups.erase(config.visual_groups.groups.begin() + selected_index);
  config.visual_groups.active_group_mask = new_mask;
  if (config.visual_groups.groups.empty()) {
    *selected_group = 0;
  } else {
    *selected_group = std::clamp(selected_index, 0, static_cast<int>(config.visual_groups.groups.size()) - 1);
  }
}

static void draw_visual_groups_content_tfwin();

static void draw_visual_groups_content() {
  draw_visual_groups_content_tfwin();
  return;

  visual_groups::ensure_defaults();

  int& selected_group = cat_menu::selected_visual_group();
  static std::string new_group_name = "New group";

  const char* target_items[] = {
    "Players", "Buildings", "Projectiles", "Ragdolls", "Objective", "NPCs", "Health", "Ammo",
    "Money", "Powerups", "Spellbook", "Bombs", "Gargoyle", "Fake angle", "Viewmodel weapon", "Viewmodel hands"
  };
  const uint32_t target_bits[] = {
    visual_group::target_players, visual_group::target_buildings, visual_group::target_projectiles, visual_group::target_ragdolls,
    visual_group::target_objective, visual_group::target_npcs, visual_group::target_health, visual_group::target_ammo,
    visual_group::target_money, visual_group::target_powerups, visual_group::target_spellbook, visual_group::target_bombs,
    visual_group::target_gargoyle, visual_group::target_fake_angle, visual_group::target_viewmodel_weapon, visual_group::target_viewmodel_hands
  };
  const char* condition_items[] = { "Enemy", "Team", "BLU", "RED", "Local", "Friends", "Party", "Priority", "Target", "Dormant", "CAT", "Ignored" };
  const uint32_t condition_bits[] = {
    visual_group::condition_enemy, visual_group::condition_team, visual_group::condition_blu, visual_group::condition_red,
    visual_group::condition_local, visual_group::condition_friends, visual_group::condition_party, visual_group::condition_priority,
    visual_group::condition_target, visual_group::condition_dormant, visual_group::condition_cat, visual_group::condition_ignored
  };
  const char* player_items[] = {
    "Scout", "Soldier", "Pyro", "Demoman", "Heavy", "Engineer", "Medic", "Sniper", "Spy",
    "Invulnerable", "Crits", "Invisible", "Disguise", "Hurt", "Not invisible"
  };
  const uint32_t player_bits[] = {
    visual_group::player_scout, visual_group::player_soldier, visual_group::player_pyro, visual_group::player_demoman,
    visual_group::player_heavy, visual_group::player_engineer, visual_group::player_medic, visual_group::player_sniper,
    visual_group::player_spy, visual_group::player_invulnerable, visual_group::player_crits, visual_group::player_invisible,
    visual_group::player_disguise, visual_group::player_hurt, visual_group::player_not_invisible
  };
  const char* building_items[] = { "Sentry", "Dispenser", "Teleporter", "Hurt" };
  const uint32_t building_bits[] = {
    visual_group::building_sentry, visual_group::building_dispenser, visual_group::building_teleporter, visual_group::building_hurt
  };
  const char* projectile_items[] = {
    "Rocket", "Sticky", "Pipe", "Arrow", "Heal", "Flare", "Fire", "Repair", "Cleaver", "Milk", "Jarate", "Gas",
    "Bauble", "Baseball", "Energy", "Short circuit", "Meteor", "Lightning", "Fireball", "Bomb", "Bats", "Pumpkin",
    "Monoculus", "Skeleton", "Misc", "Crit", "Mini-crit"
  };
  const uint32_t projectile_bits[] = {
    visual_group::projectile_rocket, visual_group::projectile_sticky, visual_group::projectile_pipe, visual_group::projectile_arrow,
    visual_group::projectile_heal, visual_group::projectile_flare, visual_group::projectile_fire, visual_group::projectile_repair,
    visual_group::projectile_cleaver, visual_group::projectile_milk, visual_group::projectile_jarate, visual_group::projectile_gas,
    visual_group::projectile_bauble, visual_group::projectile_baseball, visual_group::projectile_energy, visual_group::projectile_short_circuit,
    visual_group::projectile_meteor_shower, visual_group::projectile_lightning, visual_group::projectile_fireball, visual_group::projectile_bomb,
    visual_group::projectile_bats, visual_group::projectile_pumpkin, visual_group::projectile_monoculus, visual_group::projectile_skeleton,
    visual_group::projectile_misc, visual_group::projectile_crit, visual_group::projectile_minicrit
  };
  const char* esp_items[] = {
    "Name", "Name bg", "Box", "Distance", "Bones", "Health bar", "Health text", "Class icon", "Class text", "Weapon text",
    "Priority", "Flags", "Ping", "KDR", "Owner", "Level", "Ammo", "Intel timer",
    "Uber", "Uber bar", "Tags", "SteamID", "Conditions", "Latency", "Weapon icon", "Labels", "Buffs", "Debuffs", "Lag compensation", "Ammo bar"
  };
  const uint32_t esp_bits[] = {
    group_esp_settings::name, group_esp_settings::name_background, group_esp_settings::box, group_esp_settings::distance,
    group_esp_settings::bones, group_esp_settings::health_bar, group_esp_settings::health_text, group_esp_settings::class_icon,
    group_esp_settings::class_text, group_esp_settings::weapon_text, group_esp_settings::priority, group_esp_settings::flags,
    group_esp_settings::ping, group_esp_settings::kdr, group_esp_settings::owner,
    group_esp_settings::level, group_esp_settings::ammo_text, group_esp_settings::intel_return_time,
    group_esp_settings::uber, group_esp_settings::uber_bar, group_esp_settings::tags, group_esp_settings::steamid,
    group_esp_settings::conditions, group_esp_settings::latency, group_esp_settings::weapon_icon, group_esp_settings::labels,
    group_esp_settings::buffs, group_esp_settings::debuffs, group_esp_settings::lag_compensation, group_esp_settings::ammo_bar
  };
  const char* box_type_items[] = { "Outline", "Corner", "Filled", "Rounded", "Projected" };
  const char* mafia_position_items[] = { "Under name", "Left", "Right" };
  const char* head_emoji_items[] = { "Emoji 1", "Emoji 2" };
  const char* backtrack_items[] = { "Enabled", "Ignore z", "Last", "First", "Always" };
  const uint32_t backtrack_bits[] = {
    visual_group::backtrack_enabled, visual_group::backtrack_ignore_z, visual_group::backtrack_last,
    visual_group::backtrack_first, visual_group::backtrack_always
  };
  const char* trajectory_items[] = { "Enabled", "Ignore z", "Predict", "Radius", "Trace", "Sphere", "Path" };
  const uint32_t trajectory_bits[] = {
    visual_group::trajectory_enabled, visual_group::trajectory_ignore_z, visual_group::trajectory_predict,
    visual_group::trajectory_radius, visual_group::trajectory_trace, visual_group::trajectory_sphere, visual_group::trajectory_path
  };
  const char* sightline_items[] = { "Enabled", "Ignore z" };
  const uint32_t sightline_bits[] = { visual_group::sightline_enabled, visual_group::sightline_ignore_z };

  if (selected_group >= static_cast<int>(config.visual_groups.groups.size())) {
    selected_group = std::max(0, static_cast<int>(config.visual_groups.groups.size()) - 1);
  }

  std::array<const char*, visual_group_config::max_groups> active_names{};
  std::array<uint32_t, visual_group_config::max_groups> active_bits{};
  const int active_count = static_cast<int>(std::min(config.visual_groups.groups.size(), visual_group_config::max_groups));
  for (int index = 0; index < active_count; ++index) {
    active_names[static_cast<std::size_t>(index)] = config.visual_groups.groups[static_cast<std::size_t>(index)].name.c_str();
    active_bits[static_cast<std::size_t>(index)] = group_active_bit(index);
  }

  cat_menu::begin_flow_layout("visual_groups_layout", 2);
  cat_menu::flow_panel("Groups", 0, 380.0f, [&]() {
    const float content_width = ImMax(1.0f, ImGui::GetContentRegionAvail().x - 10.0f);
    cat_menu::input_text("New group", &new_group_name);
    const float button_spacing = 6.0f;
    const float two_button_width = ImMax(1.0f, (content_width - button_spacing) * 0.5f);
    if (cat_menu::accent_button("Create", ImVec2(two_button_width, 22.0f)) &&
        config.visual_groups.groups.size() < visual_group_config::max_groups) {
      visual_group group{};
      group.name = new_group_name.empty() ? "New group" : new_group_name;
      group.targets = visual_group::target_players;
      config.visual_groups.groups.emplace_back(group);
      selected_group = static_cast<int>(config.visual_groups.groups.size()) - 1;
      config.visual_groups.active_group_mask |= group_active_bit(selected_group);
    }
    ImGui::SameLine(0.0f, button_spacing);
    if (cat_menu::accent_button("Duplicate", ImVec2(two_button_width, 22.0f)) &&
        selected_group >= 0 && selected_group < static_cast<int>(config.visual_groups.groups.size()) &&
        config.visual_groups.groups.size() < visual_group_config::max_groups) {
      visual_group group = config.visual_groups.groups[static_cast<std::size_t>(selected_group)];
      group.name += " copy";
      config.visual_groups.groups.emplace_back(group);
      selected_group = static_cast<int>(config.visual_groups.groups.size()) - 1;
      config.visual_groups.active_group_mask |= group_active_bit(selected_group);
    }
    const float three_button_width = ImMax(1.0f, (content_width - (button_spacing * 2.0f)) / 3.0f);
    if (cat_menu::accent_button("Delete", ImVec2(three_button_width, 22.0f), true)) {
      delete_visual_group(selected_group, &selected_group);
    }
    ImGui::SameLine(0.0f, button_spacing);
    if (cat_menu::accent_button("Up", ImVec2(three_button_width, 22.0f)) && selected_group > 0) {
      visual_groups::move_group(selected_group, selected_group - 1);
      --selected_group;
    }
    ImGui::SameLine(0.0f, button_spacing);
    if (cat_menu::accent_button("Down", ImVec2(three_button_width, 22.0f)) && selected_group + 1 < static_cast<int>(config.visual_groups.groups.size())) {
      visual_groups::move_group(selected_group, selected_group + 1);
      ++selected_group;
    }
    if (active_count > 0) {
      ImGui::PushItemWidth(content_width);
      cat_menu::multi_select_combo("Active", &config.visual_groups.active_group_mask, active_names.data(), active_bits.data(), active_count);
      ImGui::PopItemWidth();
    }
    ImGui::TextDisabled("Drag profiles to change priority (top profile wins first).");
    const float list_height = ImMax(1.0f, ImGui::GetContentRegionAvail().y);
    int reorder_source = -1;
    int reorder_insert = -1;
    ImGui::BeginChild("##visual_group_list", ImVec2(content_width, list_height), true);
    for (int index = 0; index < static_cast<int>(config.visual_groups.groups.size()); ++index) {
      ImGui::PushID(index);
      const bool active = (config.visual_groups.active_group_mask & group_active_bit(index)) != 0;
      std::string label = active ? "* " : "  ";
      label += config.visual_groups.groups[static_cast<std::size_t>(index)].name;
      if (cat_menu::list_row(label.c_str(), selected_group == index, ImVec2(0.0f, 18.0f))) {
        selected_group = index;
      }
      if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
        ImGui::SetDragDropPayload("visual_group", &index, sizeof(index));
        ImGui::TextUnformatted(config.visual_groups.groups[static_cast<std::size_t>(index)].name.c_str());
        ImGui::EndDragDropSource();
      }
      if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("visual_group", ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
          if (payload->DataSize == sizeof(int)) {
            const int source = *static_cast<const int*>(payload->Data);
            if (source >= 0 && source < static_cast<int>(config.visual_groups.groups.size()) && source != index) {
              const ImVec2 row_min = ImGui::GetItemRectMin();
              const ImVec2 row_max = ImGui::GetItemRectMax();
              const bool insert_after = ImGui::GetMousePos().y > (row_min.y + row_max.y) * 0.5f;
              const float insertion_y = insert_after ? row_max.y : row_min.y;
              ImGui::GetWindowDrawList()->AddLine(
                { row_min.x, insertion_y }, { row_max.x, insertion_y },
                ImGui::GetColorU32(ImGuiCol_CheckMark), 1.5f);
              if (payload->IsDelivery()) {
                reorder_source = source;
                reorder_insert = index + (insert_after ? 1 : 0);
              }
            }
          }
        }
        ImGui::EndDragDropTarget();
      }
      ImGui::PopID();
    }
    ImGui::EndChild();

    if (reorder_source >= 0 && reorder_insert >= 0 && reorder_source < static_cast<int>(config.visual_groups.groups.size())) {
      const int old_selected = selected_group;
      int destination = reorder_insert;
      if (reorder_source < destination) --destination;
      if (destination != reorder_source && destination >= 0 && destination < static_cast<int>(config.visual_groups.groups.size())) {
        visual_groups::move_group(reorder_source, destination);
        if (old_selected == reorder_source) {
          selected_group = destination;
        } else if (reorder_source < old_selected && old_selected <= destination) {
          selected_group = old_selected - 1;
        } else if (destination <= old_selected && old_selected < reorder_source) {
          selected_group = old_selected + 1;
        }
      }
    }
  }, false);

  if (config.visual_groups.groups.empty()) {
    cat_menu::end_flow_layout();
    return;
  }

  visual_group& group = config.visual_groups.groups[static_cast<std::size_t>(selected_group)];
  cat_bind::push_panel_label("group_" + std::to_string(selected_group));

  cat_menu::flow_panel("Profile", 0, 142.0f, [&]() {
    cat_menu::input_text("Name", &group.name);
    cat_menu::color_picker("Profile color", &group.color);
    cat_menu::checkbox("Tags use profile color", &group.tags_override_color);
  });
  cat_menu::flow_panel("Targets", 1, 258.0f, [&]() {
    cat_menu::multi_select_combo("Entity target types", &group.targets, target_items, target_bits, IM_ARRAYSIZE(target_items));
  });
  cat_menu::flow_panel("ESP", 0, 436.0f, [&]() {
    cat_menu::multi_select_combo("Draw", &group.esp.draw_mask, esp_items, esp_bits, IM_ARRAYSIZE(esp_items));
    cat_menu::mask_checkbox("Emoji head", &group.esp.draw_mask, group_esp_settings::head_emoji);
    cat_menu::mask_checkbox("Mafia level", &group.esp.draw_mask, group_esp_settings::mafia_level);
    cat_menu::checkbox("Override color", &group.esp.override_color);
    cat_menu::color_picker("ESP color", &group.esp.color);
    cat_menu::combo("Box type", (int*)&group.esp.box_style, box_type_items, IM_ARRAYSIZE(box_type_items));
    cat_menu::slider_float("Start drawing", &group.esp.start, 0.0f, 8192.0f, "%.0f HU");
    cat_menu::slider_float("End drawing", &group.esp.end, 0.0f, 8192.0f, "%.0f HU");
    if (group.esp.end < group.esp.start) group.esp.end = group.esp.start;
    cat_menu::checkbox("Draw fade", &group.esp.smooth_alpha);
    int background_alpha = group.esp.background_alpha;
    cat_menu::slider_int("Background alpha", &background_alpha, 0, 255);
    group.esp.background_alpha = static_cast<uint8_t>(std::clamp(background_alpha, 0, 255));
    cat_menu::slider_float("Class icon scale", &group.esp.class_icon_scale, 0.5f, 5.0f, "%.1f");
    cat_menu::slider_float("Emoji scale", &group.esp.head_emoji_scale, 0.5f, 5.0f, "%.1f");
    cat_menu::combo("Emoji style", &group.esp.head_emoji_style, head_emoji_items, IM_ARRAYSIZE(head_emoji_items));
    cat_menu::combo("Mafia position", (int*)&group.esp.mafia_level_position, mafia_position_items, IM_ARRAYSIZE(mafia_position_items));
  });
  cat_menu::flow_panel("Conditions", 1, 258.0f, [&]() {
    cat_menu::multi_select_combo("Conditions", &group.conditions, condition_items, condition_bits, IM_ARRAYSIZE(condition_items));
    draw_visual_group_roles(group.roles);
    cat_menu::multi_select_combo("Player filters", &group.players, player_items, player_bits, IM_ARRAYSIZE(player_items));
    cat_menu::multi_select_combo("Building filters", &group.buildings, building_items, building_bits, IM_ARRAYSIZE(building_items));
    cat_menu::multi_select_combo("Projectile filters", &group.projectiles, projectile_items, projectile_bits, IM_ARRAYSIZE(projectile_items));
  });
  cat_menu::flow_panel("Advanced", 0, 258.0f, [&]() {
    cat_menu::checkbox("Offscreen arrows", &group.offscreen_arrows);
    cat_menu::slider_int("Arrow offset", &group.offscreen_arrows_offset, 0, 500);
    cat_menu::slider_float("Arrow max distance", &group.offscreen_arrows_max_distance, 0.0f, 8192.0f, "%.0f HU");
    cat_menu::checkbox("Pickup timer", &group.pickup_timer);
    cat_menu::multi_select_combo("Trajectory", &group.trajectory, trajectory_items, trajectory_bits, IM_ARRAYSIZE(trajectory_items));
    cat_menu::multi_select_combo("Sightlines", &group.sightlines, sightline_items, sightline_bits, IM_ARRAYSIZE(sightline_items));
  });
  cat_bind::pop_panel_label();
  cat_menu::end_flow_layout();
}

static void draw_visual_groups_content_tfwin() {
  visual_groups::ensure_defaults();

  int& selected_group = cat_menu::selected_visual_group();
  static std::string new_group_name = "New profile";

  static const char* target_items[] = {
    "Players", "Buildings", "Projectiles", "Ragdolls", "Objective", "NPCs", "Health", "Ammo",
    "Money", "Powerups", "Spellbook", "Bombs", "Gargoyle", "Fake angle", "Viewmodel weapon", "Viewmodel hands"
  };
  static const uint32_t target_bits[] = {
    visual_group::target_players, visual_group::target_buildings, visual_group::target_projectiles, visual_group::target_ragdolls,
    visual_group::target_objective, visual_group::target_npcs, visual_group::target_health, visual_group::target_ammo,
    visual_group::target_money, visual_group::target_powerups, visual_group::target_spellbook, visual_group::target_bombs,
    visual_group::target_gargoyle, visual_group::target_fake_angle, visual_group::target_viewmodel_weapon, visual_group::target_viewmodel_hands
  };
  static const char* condition_items[] = {
    "Enemy", "Team", "BLU", "RED", "Local", "Friends", "Party", "Priority", "Target", "Dormant", "CAT", "Ignored"
  };
  static const uint32_t condition_bits[] = {
    visual_group::condition_enemy, visual_group::condition_team, visual_group::condition_blu, visual_group::condition_red,
    visual_group::condition_local, visual_group::condition_friends, visual_group::condition_party, visual_group::condition_priority,
    visual_group::condition_target, visual_group::condition_dormant, visual_group::condition_cat, visual_group::condition_ignored
  };
  static const char* player_items[] = {
    "Scout", "Soldier", "Pyro", "Demoman", "Heavy", "Engineer", "Medic", "Sniper", "Spy",
    "Invulnerable", "Crits", "Invisible", "Disguise", "Hurt", "Not invisible"
  };
  static const uint32_t player_bits[] = {
    visual_group::player_scout, visual_group::player_soldier, visual_group::player_pyro, visual_group::player_demoman,
    visual_group::player_heavy, visual_group::player_engineer, visual_group::player_medic, visual_group::player_sniper,
    visual_group::player_spy, visual_group::player_invulnerable, visual_group::player_crits, visual_group::player_invisible,
    visual_group::player_disguise, visual_group::player_hurt, visual_group::player_not_invisible
  };
  static const char* building_items[] = { "Sentry", "Dispenser", "Teleporter", "Hurt" };
  static const uint32_t building_bits[] = {
    visual_group::building_sentry, visual_group::building_dispenser, visual_group::building_teleporter, visual_group::building_hurt
  };
  static const char* projectile_items[] = {
    "Rocket", "Sticky", "Pipe", "Arrow", "Heal", "Flare", "Fire", "Repair", "Cleaver", "Milk", "Jarate", "Gas",
    "Bauble", "Baseball", "Energy", "Short circuit", "Meteor", "Lightning", "Fireball", "Bomb", "Bats", "Pumpkin",
    "Monoculus", "Skeleton", "Misc", "Crit", "Mini-crit"
  };
  static const uint32_t projectile_bits[] = {
    visual_group::projectile_rocket, visual_group::projectile_sticky, visual_group::projectile_pipe, visual_group::projectile_arrow,
    visual_group::projectile_heal, visual_group::projectile_flare, visual_group::projectile_fire, visual_group::projectile_repair,
    visual_group::projectile_cleaver, visual_group::projectile_milk, visual_group::projectile_jarate, visual_group::projectile_gas,
    visual_group::projectile_bauble, visual_group::projectile_baseball, visual_group::projectile_energy, visual_group::projectile_short_circuit,
    visual_group::projectile_meteor_shower, visual_group::projectile_lightning, visual_group::projectile_fireball, visual_group::projectile_bomb,
    visual_group::projectile_bats, visual_group::projectile_pumpkin, visual_group::projectile_monoculus, visual_group::projectile_skeleton,
    visual_group::projectile_misc, visual_group::projectile_crit, visual_group::projectile_minicrit
  };
  static const char* esp_items[] = {
    "Name", "Name bg", "Box", "Distance", "Bones", "Health bar", "Health text", "Class icon", "Class text", "Weapon text",
    "Priority", "Flags", "Ping", "KDR", "Owner", "Level", "Ammo", "Intel timer",
    "Uber", "Uber bar", "Tags", "SteamID", "Conditions", "Latency", "Weapon icon", "Labels", "Buffs", "Debuffs", "Lag compensation", "Ammo bar"
  };
  static const uint32_t esp_bits[] = {
    group_esp_settings::name, group_esp_settings::name_background, group_esp_settings::box, group_esp_settings::distance,
    group_esp_settings::bones, group_esp_settings::health_bar, group_esp_settings::health_text, group_esp_settings::class_icon,
    group_esp_settings::class_text, group_esp_settings::weapon_text, group_esp_settings::priority, group_esp_settings::flags,
    group_esp_settings::ping, group_esp_settings::kdr, group_esp_settings::owner,
    group_esp_settings::level, group_esp_settings::ammo_text, group_esp_settings::intel_return_time,
    group_esp_settings::uber, group_esp_settings::uber_bar, group_esp_settings::tags, group_esp_settings::steamid,
    group_esp_settings::conditions, group_esp_settings::latency, group_esp_settings::weapon_icon, group_esp_settings::labels,
    group_esp_settings::buffs, group_esp_settings::debuffs, group_esp_settings::lag_compensation, group_esp_settings::ammo_bar
  };
  static const char* box_type_items[] = { "Outline", "Corner", "Filled", "Rounded", "Projected" };
  static const char* mafia_position_items[] = { "Under name", "Left", "Right" };
  static const char* head_emoji_items[] = { "Emoji 1", "Emoji 2" };
  static const char* backtrack_items[] = { "Enabled", "Ignore z", "Last", "First", "Always" };
  static const uint32_t backtrack_bits[] = {
    visual_group::backtrack_enabled, visual_group::backtrack_ignore_z, visual_group::backtrack_last,
    visual_group::backtrack_first, visual_group::backtrack_always
  };
  static const char* trajectory_items[] = { "Enabled", "Ignore z", "Predict", "Radius", "Trace", "Sphere", "Path" };
  static const uint32_t trajectory_bits[] = {
    visual_group::trajectory_enabled, visual_group::trajectory_ignore_z, visual_group::trajectory_predict,
    visual_group::trajectory_radius, visual_group::trajectory_trace, visual_group::trajectory_sphere, visual_group::trajectory_path
  };
  static const char* sightline_items[] = { "Enabled", "Ignore z" };
  static const uint32_t sightline_bits[] = { visual_group::sightline_enabled, visual_group::sightline_ignore_z };

  if (selected_group >= static_cast<int>(config.visual_groups.groups.size())) {
    selected_group = std::max(0, static_cast<int>(config.visual_groups.groups.size()) - 1);
  }

  const float available_width = ImGui::GetContentRegionAvail().x;
  const float profile_width = std::clamp(available_width * 0.34f, cat_menu::scaled(190.0f), cat_menu::scaled(240.0f));
  const float gap = cat_menu::scaled(cat_menu::k_gap);
  std::array<const char*, visual_group_config::max_groups> active_names{};
  std::array<uint32_t, visual_group_config::max_groups> active_bits{};
  int active_count = 0;
  const auto rebuild_active_profiles = [&] {
    active_names.fill(nullptr);
    active_bits.fill(0);
    active_count = static_cast<int>(std::min(config.visual_groups.groups.size(), visual_group_config::max_groups));
    for (int index = 0; index < active_count; ++index) {
      active_names[static_cast<std::size_t>(index)] = config.visual_groups.groups[static_cast<std::size_t>(index)].name.c_str();
      active_bits[static_cast<std::size_t>(index)] = group_active_bit(index);
    }
  };

  ImGui::BeginChild("visual_group_manager_tfwin", { profile_width, 0.0f }, ImGuiChildFlags_Border);
  ImGui::TextUnformatted("profiles");
  mono::group_separator();
  cat_menu::input_text("New profile", &new_group_name);
  const float content_width = ImMax(1.0f, ImGui::GetContentRegionAvail().x);
  const float item_gap = ImGui::GetStyle().ItemSpacing.x;
  const float half_width = ImMax(1.0f, (content_width - item_gap) * 0.5f);
  if (cat_menu::accent_button("New", { half_width, 22.0f }) &&
      config.visual_groups.groups.size() < visual_group_config::max_groups) {
    visual_group group{};
    group.name = new_group_name.empty() ? "New profile" : new_group_name;
    group.targets = visual_group::target_players;
    config.visual_groups.groups.emplace_back(std::move(group));
    selected_group = static_cast<int>(config.visual_groups.groups.size()) - 1;
    config.visual_groups.active_group_mask |= group_active_bit(selected_group);
  }
  ImGui::SameLine(0.0f, item_gap);
  if (cat_menu::accent_button("Duplicate", { half_width, 22.0f }) &&
      selected_group >= 0 && selected_group < static_cast<int>(config.visual_groups.groups.size()) &&
      config.visual_groups.groups.size() < visual_group_config::max_groups) {
    visual_group group = config.visual_groups.groups[static_cast<std::size_t>(selected_group)];
    group.name += " copy";
    config.visual_groups.groups.emplace_back(std::move(group));
    selected_group = static_cast<int>(config.visual_groups.groups.size()) - 1;
    config.visual_groups.active_group_mask |= group_active_bit(selected_group);
  }
  const float action_width = ImMax(1.0f, (content_width - item_gap * 2.0f) / 3.0f);
  if (cat_menu::accent_button("Delete", { action_width, cat_menu::k_button_height }, true)) {
    delete_visual_group(selected_group, &selected_group);
  }
  ImGui::SameLine(0.0f, item_gap);
  if (cat_menu::accent_button("Up", { action_width, cat_menu::k_button_height }) && selected_group > 0) {
    visual_groups::move_group(selected_group, selected_group - 1);
    --selected_group;
  }
  ImGui::SameLine(0.0f, item_gap);
  if (cat_menu::accent_button("Down", { action_width, cat_menu::k_button_height }) &&
      selected_group + 1 < static_cast<int>(config.visual_groups.groups.size())) {
    visual_groups::move_group(selected_group, selected_group + 1);
    ++selected_group;
  }
  rebuild_active_profiles();
  if (active_count > 0) {
    cat_menu::multi_select_combo("Active profiles", &config.visual_groups.active_group_mask,
      active_names.data(), active_bits.data(), active_count);
  }
  ImGui::TextDisabled("Top profile wins first.");
  int reorder_source = -1;
  int reorder_insert = -1;
  ImGui::BeginChild("visual_group_list_tfwin", { 0.0f, 0.0f }, ImGuiChildFlags_Border);
  for (int index = 0; index < static_cast<int>(config.visual_groups.groups.size()); ++index) {
    ImGui::PushID(index);
    const bool active = (config.visual_groups.active_group_mask & group_active_bit(index)) != 0;
    std::string label = active ? "* " : "  ";
    label += config.visual_groups.groups[static_cast<std::size_t>(index)].name;
    if (cat_menu::list_row(label.c_str(), selected_group == index, { 0.0f, 28.0f })) {
      selected_group = index;
    }
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
      ImGui::SetDragDropPayload("visual_group_tfwin", &index, sizeof(index));
      ImGui::TextUnformatted(config.visual_groups.groups[static_cast<std::size_t>(index)].name.c_str());
      ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("visual_group_tfwin", ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
          payload != nullptr && payload->DataSize == sizeof(int)) {
        const int source = *static_cast<const int*>(payload->Data);
        if (source >= 0 && source < static_cast<int>(config.visual_groups.groups.size()) && source != index) {
          const ImVec2 row_min = ImGui::GetItemRectMin();
          const ImVec2 row_max = ImGui::GetItemRectMax();
          const bool insert_after = ImGui::GetMousePos().y > (row_min.y + row_max.y) * 0.5f;
          const float insertion_y = insert_after ? row_max.y : row_min.y;
          ImGui::GetWindowDrawList()->AddLine({ row_min.x, insertion_y }, { row_max.x, insertion_y },
            ImGui::GetColorU32(ImGuiCol_CheckMark), 1.5f);
          if (payload->IsDelivery()) {
            reorder_source = source;
            reorder_insert = index + (insert_after ? 1 : 0);
          }
        }
      }
      ImGui::EndDragDropTarget();
    }
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::EndChild();

  if (reorder_source >= 0 && reorder_insert >= 0 && reorder_source < static_cast<int>(config.visual_groups.groups.size())) {
    const int old_selected = selected_group;
    int destination = reorder_insert;
    if (reorder_source < destination) --destination;
    if (destination != reorder_source && destination >= 0 && destination < static_cast<int>(config.visual_groups.groups.size())) {
      visual_groups::move_group(reorder_source, destination);
      if (old_selected == reorder_source) selected_group = destination;
      else if (reorder_source < old_selected && old_selected <= destination) --selected_group;
      else if (destination <= old_selected && old_selected < reorder_source) ++selected_group;
    }
  }

  ImGui::SameLine(0.0f, gap);
  ImGui::BeginChild("visual_group_inspector_tfwin", { 0.0f, 0.0f }, ImGuiChildFlags_Border);
  if (config.visual_groups.groups.empty()) {
    ImGui::SetCursorPosY(ImGui::GetContentRegionAvail().y * 0.5f);
    ImGui::TextDisabled("Create a profile to configure entity visuals.");
    ImGui::EndChild();
    return;
  }

  selected_group = std::clamp(selected_group, 0, static_cast<int>(config.visual_groups.groups.size()) - 1);
  visual_group& group = config.visual_groups.groups[static_cast<std::size_t>(selected_group)];
  cat_bind::push_panel_label("group_" + std::to_string(selected_group));

  const auto end_panel = [] {
    ImGui::EndChild();
    ImGui::Dummy({ 0.0f, 6.0f });
  };
  const auto draw_panel_header = [](const char* title) {
    ImGui::TextUnformatted(title);
    mono::group_separator();
  };

  constexpr ImGuiChildFlags inspector_panel_flags =
    ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize;

  if (ImGui::BeginChild("visual_profile_panel", { 0.0f, 0.0f }, inspector_panel_flags)) {
    draw_panel_header("profile inspector");
    cat_menu::input_text("Name", &group.name);
    cat_menu::color_picker("Profile color", &group.color);
    cat_menu::checkbox("Tags use profile color", &group.tags_override_color);
  }
  end_panel();

  if (ImGui::BeginChild("visual_targets_panel", { 0.0f, 0.0f }, inspector_panel_flags)) {
    draw_panel_header("targets");
    cat_menu::multi_select_combo("Entity target types", &group.targets, target_items, target_bits, IM_ARRAYSIZE(target_items));
  }
  end_panel();

  if (ImGui::BeginChild("visual_conditions_panel", { 0.0f, 0.0f }, inspector_panel_flags)) {
    draw_panel_header("conditions");
    cat_menu::multi_select_combo("Conditions", &group.conditions, condition_items, condition_bits, IM_ARRAYSIZE(condition_items));
    draw_visual_group_roles(group.roles);
    cat_menu::multi_select_combo("Player filters", &group.players, player_items, player_bits, IM_ARRAYSIZE(player_items));
    cat_menu::multi_select_combo("Building filters", &group.buildings, building_items, building_bits, IM_ARRAYSIZE(building_items));
    cat_menu::multi_select_combo("Projectile filters", &group.projectiles, projectile_items, projectile_bits, IM_ARRAYSIZE(projectile_items));
  }
  end_panel();

  if (ImGui::BeginChild("visual_esp_panel", { 0.0f, 0.0f }, inspector_panel_flags)) {
    draw_panel_header("esp effects");
    cat_menu::multi_select_combo("Draw", &group.esp.draw_mask, esp_items, esp_bits, IM_ARRAYSIZE(esp_items));
    cat_menu::mask_checkbox("Emoji head", &group.esp.draw_mask, group_esp_settings::head_emoji);
    cat_menu::mask_checkbox("Mafia level", &group.esp.draw_mask, group_esp_settings::mafia_level);
    cat_menu::checkbox("Override color", &group.esp.override_color);
    cat_menu::color_picker("ESP color", &group.esp.color);
    cat_menu::combo("Box type", (int*)&group.esp.box_style, box_type_items, IM_ARRAYSIZE(box_type_items));
    cat_menu::slider_float("Start drawing", &group.esp.start, 0.0f, 8192.0f, "%.0f HU");
    cat_menu::slider_float("End drawing", &group.esp.end, 0.0f, 8192.0f, "%.0f HU");
    if (group.esp.end < group.esp.start) group.esp.end = group.esp.start;
    cat_menu::checkbox("Draw fade", &group.esp.smooth_alpha);
    int background_alpha = group.esp.background_alpha;
    cat_menu::slider_int("Background alpha", &background_alpha, 0, 255);
    group.esp.background_alpha = static_cast<uint8_t>(std::clamp(background_alpha, 0, 255));
    cat_menu::slider_float("Class icon scale", &group.esp.class_icon_scale, 0.5f, 5.0f, "%.1f");
    cat_menu::slider_float("Emoji scale", &group.esp.head_emoji_scale, 0.5f, 5.0f, "%.1f");
    cat_menu::combo("Emoji style", &group.esp.head_emoji_style, head_emoji_items, IM_ARRAYSIZE(head_emoji_items));
    cat_menu::combo("Mafia position", (int*)&group.esp.mafia_level_position, mafia_position_items, IM_ARRAYSIZE(mafia_position_items));
  }
  end_panel();

  materials.prepare();
  if (ImGui::BeginChild("visual_chams_visible_panel", { 0.0f, 0.0f }, inspector_panel_flags)) {
    draw_panel_header("visible chams");
    cat_menu::render_material_layers("Visible layers", group.chams.visible);
  }
  end_panel();

  if (ImGui::BeginChild("visual_glow_panel", { 0.0f, 0.0f }, inspector_panel_flags)) {
    draw_panel_header("glow");
    cat_menu::color_picker("Color", &group.glow.color);
    cat_menu::slider_float("Stencil scale", &group.glow.stencil, 0.0f, 10.0f, "%.1f");
    group.glow.stencil = std::clamp(std::round(group.glow.stencil * 10.0f) / 10.0f, 0.0f, 10.0f);
    cat_menu::slider_float("Blur scale", &group.glow.blur, 0.0f, 100.0f, "%.1f");
    cat_menu::slider_float("Render start", &group.glow.start, 0.0f, 2048.0f, "%.0f HU");
    cat_menu::slider_float("Render end", &group.glow.end, 512.0f, 8192.0f, "%.0f HU");
    if (group.glow.end < group.glow.start) group.glow.end = group.glow.start;
    cat_menu::checkbox("Distance to alpha", &group.glow.smooth_alpha);
  }
  end_panel();

  if (ImGui::BeginChild("visual_chams_occluded_panel", { 0.0f, 0.0f }, inspector_panel_flags)) {
    draw_panel_header("behind walls chams");
    cat_menu::render_material_layers("Behind walls layers", group.chams.occluded);
  }
  end_panel();

  if (ImGui::BeginChild("visual_backtrack_panel", { 0.0f, 0.0f }, inspector_panel_flags)) {
    draw_panel_header("backtrack visuals");
    auto& backtrack = group.backtrack_visuals;
    cat_menu::checkbox("Backtrack", &backtrack.enabled);
    static const char* backtrack_record_items[] = { "Last", "First", "All" };
    ImGui::BeginDisabled(!backtrack.enabled);
    cat_menu::combo("Records", &backtrack.record_mode, backtrack_record_items, IM_ARRAYSIZE(backtrack_record_items));
    cat_menu::checkbox("Ignore z", &backtrack.ignore_z);
    cat_menu::render_material_layers("Backtrack visible layers", backtrack.chams.visible);
    cat_menu::render_material_layers("Backtrack behind walls layers", backtrack.chams.occluded);
    ImGui::TextUnformatted("Backtrack glow");
    cat_menu::color_picker("Backtrack color", &backtrack.glow.color);
    cat_menu::slider_float("Backtrack stencil scale", &backtrack.glow.stencil, 0.0f, 10.0f, "%.1f");
    backtrack.glow.stencil = std::clamp(std::round(backtrack.glow.stencil * 10.0f) / 10.0f, 0.0f, 10.0f);
    cat_menu::slider_float("Backtrack blur scale", &backtrack.glow.blur, 0.0f, 100.0f, "%.1f");
    cat_menu::slider_float("Backtrack render start", &backtrack.glow.start, 0.0f, 2048.0f, "%.0f HU");
    cat_menu::slider_float("Backtrack render end", &backtrack.glow.end, 512.0f, 8192.0f, "%.0f HU");
    if (backtrack.glow.end < backtrack.glow.start) backtrack.glow.end = backtrack.glow.start;
    cat_menu::checkbox("Backtrack distance to alpha", &backtrack.glow.smooth_alpha);
    ImGui::EndDisabled();
  }
  end_panel();

  if (ImGui::BeginChild("visual_advanced_panel", { 0.0f, 0.0f }, inspector_panel_flags)) {
    draw_panel_header("advanced");
    cat_menu::checkbox("Offscreen arrows", &group.offscreen_arrows);
    cat_menu::slider_int("Arrow offset", &group.offscreen_arrows_offset, 0, 500);
    cat_menu::slider_float("Arrow max distance", &group.offscreen_arrows_max_distance, 0.0f, 8192.0f, "%.0f HU");
    cat_menu::checkbox("Pickup timer", &group.pickup_timer);
    cat_menu::multi_select_combo("Trajectory", &group.trajectory, trajectory_items, trajectory_bits, IM_ARRAYSIZE(trajectory_items));
    cat_menu::multi_select_combo("Sightlines", &group.sightlines, sightline_items, sightline_bits, IM_ARRAYSIZE(sightline_items));
  }
  end_panel();

  cat_bind::pop_panel_label();
  ImGui::EndChild();
}

static void draw_visuals_world_content() {
  cat_menu::begin_flow_layout("visuals_world_layout", 2);
  cat_menu::flow_panel("World", 0, 324.0f, [&]() {
    cat_menu::combo("Skybox", &config.visuals.skybox_changer_index,
                    skybox_changer::option_names(), skybox_changer::option_count());
    cat_menu::checkbox("Thirdperson", &config.visuals.thirdperson.enabled);
    cat_menu::checkbox("Thirdperson crosshair", &config.visuals.thirdperson.crosshair);
    cat_menu::checkbox("Thirdperson collision", &config.visuals.thirdperson.collision);
    cat_menu::slider_float("Thirdperson distance", &config.visuals.thirdperson.distance, 0.0f, 400.0f, "%.0f HU");
    cat_menu::slider_float("Thirdperson right", &config.visuals.thirdperson.right, -100.0f, 100.0f, "%.0f HU");
    cat_menu::slider_float("Thirdperson up", &config.visuals.thirdperson.up, -100.0f, 100.0f, "%.0f HU");
    cat_menu::checkbox("Thirdperson scales", &config.visuals.thirdperson.scale);
    cat_menu::checkbox("Override FOV", &config.visuals.override_fov);
    cat_menu::slider_float("Custom FOV", &config.visuals.custom_fov, 30.1f, 150.0f, "%.0f deg");
    cat_menu::checkbox("Override zoom FOV", &config.visuals.override_zoom_fov);
    cat_menu::slider_float("Zoom FOV", &config.visuals.custom_zoom_fov, 1.0f, 150.0f, "%.0f deg");
    cat_menu::checkbox("Override viewmodel FOV", &config.visuals.override_viewmodel_fov);
    cat_menu::slider_float("Viewmodel FOV", &config.visuals.custom_viewmodel_fov, 30.1f, 150.0f, "%.0f deg");
    cat_menu::checkbox("ESP lerp", &config.visuals.esp_lerp);
    cat_menu::checkbox("Dormant ESP", &config.visuals.dormant_esp);
  });
  cat_menu::flow_panel("Removals", 1, 292.0f, [&]() {
    cat_menu::checkbox("Remove scope", &config.visuals.removals.scope);
    cat_menu::checkbox("Remove zoom", &config.visuals.removals.zoom);
    cat_menu::checkbox("Flat scoped sensitivity", &config.visuals.flat_zoom_sensitivity);
    cat_menu::checkbox("Remove arms", &config.visuals.removals.arms);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Drops the draw call for viewmodel arm models.\nThe Yeti cosmetic is excluded - it has \"arms\" in its\npath but is worn in the world, not held.");
    }
    cat_menu::checkbox("Remove hats", &config.visuals.removals.hats);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Drops the draw call for anything under models/player/items/.\nWeapons and player models are left alone.");
    }
    cat_menu::checkbox("Remove cloak", &config.visuals.removals.cloak);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Clears the cloak condition on other players, including the\nuncloak blink. Client-side only and never applied to you.");
    }
    cat_menu::checkbox("Remove disguise", &config.visuals.removals.disguise);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Clears both the disguise and the four-second wind-up,\nso a spy cannot fade into a disguise in front of you.");
    }
    cat_menu::checkbox("Remove taunts", &config.visuals.removals.taunts);
    cat_menu::checkbox("Remove contracker", &config.visuals.removals.contracker);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Stops other players playing the ConTracker inspect animation,\nwhich otherwise freezes their pose and ruins hitbox tracking.");
    }
  });
  cat_menu::end_flow_layout();
}

static void draw_visuals_ui_content() {
  const char* indicator_items[] = {
    "Tickbase",
    "Crit hack",
    "Nospread",
    "Spectators",
    "Keybinds"
  };
  const uint32_t indicator_bits[] = {
    Visuals::Indicators::tickbase,
    Visuals::Indicators::crit_hack,
    Visuals::Indicators::nospread,
    Visuals::Indicators::spectators,
    Visuals::Indicators::keybinds
  };

  cat_menu::begin_flow_layout("visuals_ui_layout", 2);
  cat_menu::flow_panel("Indicators", 0, 228.0f, [&]() {
    cat_menu::multi_select_combo("Visible widgets", &config.visuals.indicators.enabled_mask, indicator_items, indicator_bits, IM_ARRAYSIZE(indicator_items));
    cat_menu::color_picker("Tickbase bar", &config.visuals.indicators.tickbase_bar_color);
    cat_menu::color_picker("Crit hack bar", &config.visuals.indicators.crit_hack_bar_color);
    cat_menu::checkbox("Show spectator target", &config.visuals.spectator_list.show_target);
    cat_menu::checkbox("Show spectator modes", &config.visuals.spectator_list.show_modes);
    cat_menu::checkbox("Highlight firstperson", &config.visuals.spectator_list.highlight_firstperson);
    cat_menu::color_picker("Firstperson color", &config.visuals.spectator_list.firstperson_color);
  });
  cat_menu::flow_panel("Feedback", 1, 188.0f, [&]() {
    cat_menu::checkbox("Hitmarker", &config.visuals.hitmarker.enabled);
    cat_menu::checkbox("Damage text", &config.visuals.hitmarker.damage_text);
    cat_menu::slider_float("Hitmarker duration", &config.visuals.hitmarker.duration, 0.20f, 1.50f, "%.2f s");
    cat_menu::slider_float("Hitmarker size", &config.visuals.hitmarker.size, 4.0f, 16.0f, "%.1f px");
    cat_menu::color_picker("Hitmarker color", &config.visuals.hitmarker.color);
    cat_menu::color_picker("Crit color", &config.visuals.hitmarker.crit_color);
    cat_menu::color_picker("Headshot color", &config.visuals.hitmarker.headshot_color);
  });
  cat_menu::flow_panel("Casual medal", 0, 124.0f, [&]() {
    cat_menu::checkbox("Guaranteed flip", &config.visuals.casual_medal.guaranteed_flip);
    cat_menu::checkbox("Change displayed rank", &config.visuals.casual_medal.changer);
    cat_menu::slider_int("Displayed rank", &config.visuals.casual_medal.rank, 1, 1200);
  });
  cat_menu::flow_panel("Radar", 1, 356.0f, [&]() {
    cat_menu::checkbox("Enable radar", &config.visuals.radar.enabled);
    cat_menu::slider_float("Radar X", &config.visuals.radar.x, 0.0f, 1920.0f, "%.0f px");
    cat_menu::slider_float("Radar Y", &config.visuals.radar.y, 0.0f, 1080.0f, "%.0f px");
    cat_menu::slider_int("Radar size", &config.visuals.radar.size, 100, 600);
    cat_menu::slider_float("Radar zoom", &config.visuals.radar.zoom, 5.0f, 50.0f, "%.1f");
    cat_menu::slider_int("Icon size", &config.visuals.radar.icon_size, 10, 40);
    cat_menu::checkbox("Class icons", &config.visuals.radar.use_icons);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Draw class icons from assets/textures/atlas.png.\nFalls back to plain dots when the class is unknown.");
    }
    cat_menu::checkbox("Axis lines", &config.visuals.radar.axis_lines);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Full-span horizontal and vertical lines through the centre.\nThe short centre cross is always drawn.");
    }
    cat_menu::slider_int("Range rings", &config.visuals.radar.range_rings, 0, 8);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Evenly spaced distance rings, 0 to turn them off.\nSpacing is in radar pixels, so the rings stay put while zoom\nchanges what distance they represent.");
    }
    cat_menu::checkbox("Show teammates", &config.visuals.radar.show_teammates);
    cat_menu::checkbox("Show enemies", &config.visuals.radar.show_enemies);
  });
  cat_menu::end_flow_layout();
}

static void draw_navbot_content();

static void draw_visuals_tab(const int visuals_subtab) {
  switch (visuals_subtab) {
    case cat_menu::visuals_subtab_entity_profiles:
      draw_visual_groups_content();
      break;
    case cat_menu::visuals_subtab_indicators:
      draw_visuals_ui_content();
      break;
    case cat_menu::visuals_subtab_map:
      draw_navbot_content();
      break;
    case cat_menu::visuals_subtab_other:
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
  static const char* auto_edgebug_items[] = {
    "Off",
    "Legit",
    "Strafe",
    "Strafe silent"
  };

  cat_menu::begin_flow_layout("movement_layout", 2);
  cat_menu::flow_panel("Movement", 0, 220.0f, [&]() {
    cat_menu::checkbox("Bhop", &config.misc.movement.bhop);
    cat_menu::combo("Auto strafe", (int*)&config.misc.movement.auto_strafe, auto_strafe_items, IM_ARRAYSIZE(auto_strafe_items));
    cat_menu::slider_float("Strafe turn scale", &config.misc.movement.auto_strafe_turn_scale, 0.0f, 1.0f, "%.2f");
    cat_menu::slider_float("Strafe max delta", &config.misc.movement.auto_strafe_max_delta, 0.0f, 180.0f, "%.0f deg");
    cat_menu::checkbox("Edge jump", &config.misc.movement.edge_jump);
    cat_menu::checkbox("Jumpbug", &config.misc.movement.jumpbug);
    cat_menu::checkbox("Duck jump", &config.misc.movement.duck_jump);
    cat_menu::checkbox("Break jump", &config.misc.movement.break_jump);
    cat_menu::checkbox("Auto reverse jump", &config.misc.movement.auto_reverse_jump);
    cat_menu::checkbox("Fast stop", &config.misc.movement.fast_stop);
    cat_menu::checkbox("Fast accelerate", &config.misc.movement.fast_accelerate);
    cat_menu::combo("Auto edgebug", (int*)&config.misc.movement.auto_edgebug, auto_edgebug_items, IM_ARRAYSIZE(auto_edgebug_items));
    cat_menu::checkbox("No push", &config.misc.movement.no_push);
    cat_menu::checkbox("Taunt slide", &config.misc.movement.taunt_slide);
    cat_menu::checkbox("Moonwalk", &config.misc.movement.moonwalk);
    cat_menu::checkbox("Moonwalk forward", &config.misc.movement.moonwalk_forward);
    cat_menu::checkbox("Moonwalk navbot compat", &config.misc.movement.moonwalk_navbot_compat);
  });
  cat_menu::end_flow_layout();
}

static void draw_navbot_content() {
  static const char* enemy_stalk_mode_items[] = {
    "Default",
    "YOLO"
  };
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
    navbot::goal_type_bit(navbot::goal_type::get_health),
    navbot::goal_type_bit(navbot::goal_type::get_ammo),
    navbot::goal_type_bit(navbot::goal_type::capture_objective),
    navbot::goal_type_bit(navbot::goal_type::push_payload),
    navbot::goal_type_bit(navbot::goal_type::defend_payload),
    navbot::goal_type_bit(navbot::goal_type::get_flag),
    navbot::goal_type_bit(navbot::goal_type::return_flag),
    navbot::goal_type_bit(navbot::goal_type::escape_danger),
    navbot::goal_type_bit(navbot::goal_type::hold_range_on_enemy),
    navbot::goal_type_bit(navbot::goal_type::melee_chase),
    navbot::goal_type_bit(navbot::goal_type::sentry_snipe),
    navbot::goal_type_bit(navbot::goal_type::engineer_build),
    navbot::goal_type_bit(navbot::goal_type::engineer_maintain),
    navbot::goal_type_bit(navbot::goal_type::reload_weapons),
    navbot::goal_type_bit(navbot::goal_type::heal_follow)
  };
  uint32_t navbot_all_job_bits = 0;
  for (uint32_t bit : navbot_job_bits) {
    navbot_all_job_bits |= bit;
  }

  cat_menu::begin_flow_layout("navbot_layout", 2);
  cat_menu::flow_panel("NavBot", 0, 300.0f, [&]() {
    cat_menu::checkbox("Navbot", &config.misc.automation.navbot_enabled);
    static const char* navbot_behavior_items[] = {
      "Default",
      "MvM automation"
    };
    cat_menu::combo("Behavior", (int*)&config.misc.automation.navbot_behavior,
      navbot_behavior_items, IM_ARRAYSIZE(navbot_behavior_items));
    cat_menu::checkbox("Draw path", &config.misc.automation.navbot_draw_path);
    cat_menu::checkbox("Draw path boxes", &config.misc.automation.navbot_draw_path_boxes);
    cat_menu::color_picker("Path color", &config.misc.automation.navbot_path_color);
    cat_menu::checkbox("Don't path during warmup", &config.misc.automation.navbot_dont_path_during_warmup);
    cat_menu::checkbox("Dynamic hazards", &config.misc.automation.navbot_hazards);
    static const char* navbot_weapon_selection_items[] = {
      "Off",
      "Auto",
      "Primary",
      "Secondary",
      "Melee"
    };
    cat_menu::combo(
        "Weapon selection",
        (int*)&config.misc.automation.navbot_weapon_selection,
        navbot_weapon_selection_items,
        IM_ARRAYSIZE(navbot_weapon_selection_items));
    cat_menu::combo("Enemy stalk mode", (int*)&config.misc.automation.enemy_stalk_mode, enemy_stalk_mode_items, IM_ARRAYSIZE(enemy_stalk_mode_items));
    cat_menu::slider_float("Melee target range", &config.misc.automation.navbot_melee_target_range, 150.0f, 4000.0f, "%.0f HU");
    cat_menu::slider_float("Crumb blacklist", &config.misc.automation.navbot_crumb_blacklist_seconds, 50.0f, 150.0f, "%.0f s");
    cat_menu::checkbox("Debug text", &config.misc.automation.navbot_debug_text);
  });
  cat_menu::flow_panel("Path Look", 0, 132.0f, [&]() {
    cat_menu::checkbox("Look at path", &config.misc.automation.navbot_look_at_path);
    cat_menu::checkbox("Silent look", &config.misc.automation.navbot_look_at_path_silent);
    cat_menu::slider_int("Slow aim", &config.misc.automation.navbot_look_at_path_speed, 1, 100);
    cat_menu::slider_int("Spin chance", &config.misc.automation.navbot_look_at_path_spin_chance, 0, 100);
  });
  cat_menu::flow_panel("Jobs", 1, 90.0f, [&]() {
    uint32_t navbot_enabled_jobs_mask = navbot_all_job_bits & ~config.misc.automation.navbot_excluded_jobs_mask;
    if (cat_menu::multi_select_combo("Enabled jobs", &navbot_enabled_jobs_mask, navbot_job_items, navbot_job_bits, IM_ARRAYSIZE(navbot_job_items))) {
      config.misc.automation.navbot_excluded_jobs_mask = navbot_all_job_bits & ~navbot_enabled_jobs_mask;
    }
  });
  cat_menu::flow_panel("Followbot", 1, 220.0f, [&]() {
    static const char* followbot_target_items[] = {"Teammates", "Enemies"};
    static const uint32_t followbot_target_bits[] = {
      Misc::Automation::followbot_teammates,
      Misc::Automation::followbot_enemies
    };
    static const char* followbot_preference_items[] = {"Off", "Friends", "Party"};
    static const char* followbot_nav_items[] = {"Off", "Normal", "Normal + Dormant"};
    static const char* followbot_look_items[] = {"Off", "Path", "Copy target", "At target"};
    cat_menu::checkbox("Followbot", &config.misc.automation.followbot_enabled);
    cat_menu::combo("Use nav mesh", (int*)&config.misc.automation.followbot_use_nav,
      followbot_nav_items, IM_ARRAYSIZE(followbot_nav_items));
    cat_menu::multi_select_combo("Targets", &config.misc.automation.followbot_targets,
      followbot_target_items, followbot_target_bits, IM_ARRAYSIZE(followbot_target_items));
    cat_menu::combo("Prefer", (int*)&config.misc.automation.followbot_preference,
      followbot_preference_items, IM_ARRAYSIZE(followbot_preference_items));
    cat_menu::combo("Look at path", (int*)&config.misc.automation.followbot_look,
      followbot_look_items, IM_ARRAYSIZE(followbot_look_items));
    cat_menu::checkbox("Avoid view snap", &config.misc.automation.followbot_look_no_snap);
    cat_menu::checkbox("Ignore AFK targets", &config.misc.automation.followbot_ignore_afk);
    cat_menu::slider_int("Min priority", &config.misc.automation.followbot_min_priority, 0, 10);
    cat_menu::slider_int("Max path nodes", &config.misc.automation.followbot_max_nodes, 50, 500);
    cat_menu::slider_float("Activation distance", &config.misc.automation.followbot_activation_distance, 10.0f, 1200.0f, "%.0f HU");
    cat_menu::slider_float("Follow distance", &config.misc.automation.followbot_follow_distance, 10.0f, 150.0f, "%.0f HU");
    cat_menu::slider_float("Abandon distance", &config.misc.automation.followbot_abandon_distance, 250.0f, 1500.0f, "%.0f HU");
    cat_menu::slider_float("Nav abandon distance", &config.misc.automation.followbot_nav_abandon_distance, 500.0f, 8000.0f, "%.0f HU");
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
    cat_menu::multi_select_combo("Heal targets", &config.misc.automation.medic_heal_targets_mask, heal_target_items, heal_target_bits, IM_ARRAYSIZE(heal_target_items));
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
  std::string_view current_continent;
  for (const auto& data_center : automation::region_selector::data_centers) {
    if (current_continent != data_center.continent) {
      current_continent = data_center.continent;
      ImGui::SeparatorText(current_continent.data());

      bool continent_allowed = automation::region_selector::are_all_continent_regions_allowed(current_continent);
      if (cat_menu::checkbox(current_continent.data(), &continent_allowed)) {
        automation::region_selector::set_continent_regions_allowed(current_continent, continent_allowed);
        region_selector_changed = true;
      }
    }
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

  cat_menu::begin_flow_layout("cat_bot_layout", 2);
  cat_menu::flow_panel("Autojoin and Taunt", 0, 138.0f, [&]() {
    cat_menu::checkbox("Auto class select", &config.misc.automation.auto_class_select);
    cat_menu::combo("Preferred class", (int*)&config.misc.automation.class_selected, class_items, IM_ARRAYSIZE(class_items));
    cat_menu::checkbox("Don't join class during warmup", &config.misc.automation.auto_class_dont_join_during_warmup);
    cat_menu::checkbox("Auto taunt", &config.misc.automation.autotaunt);
    cat_menu::slider_float("Taunt chance", &config.misc.automation.autotaunt_chance, 0.0f, 100.0f, "%.0f%%");
    cat_menu::slider_float("Taunt safety distance", &config.misc.automation.autotaunt_safety_distance, 0.0f, 5000.0f, "%.0f HU");
    cat_menu::slider_int("Taunt weapon slot", &config.misc.automation.autotaunt_weapon_slot, 0, 5);
  });
  cat_menu::flow_panel("Autoqueue", 0, 216.0f, [&]() {
    cat_menu::checkbox("Auto queue", &config.misc.automation.auto_queue);
    cat_menu::checkbox("Auto requeue", &config.misc.automation.auto_requeue);
    cat_menu::checkbox("Requeue on kick", &config.misc.automation.requeue_on_kick);
    cat_menu::checkbox("Auto casual join", &config.misc.automation.auto_casual_join);
    cat_menu::combo("Queue mode", &config.misc.automation.auto_queue_mode, queue_mode_items, IM_ARRAYSIZE(queue_mode_items));
    cat_menu::slider_int("RQ if players <", &config.misc.automation.rq_if_players_lte, 0, 32);
    cat_menu::slider_int("RQ if players >", &config.misc.automation.rq_if_players_gte, 0, 32);
    cat_menu::slider_int("RQ if IPC bots >", &config.misc.automation.rq_if_ipc_bots_gt, 0, 32);
    cat_menu::checkbox("RQ if no navmesh", &config.misc.automation.rq_if_no_navmesh);
    cat_menu::checkbox("RQ ignore friends", &config.misc.automation.rq_ignore_friends);
    cat_menu::combo("Requeue action", (int*)&config.misc.automation.requeue_action, requeue_action_items, IM_ARRAYSIZE(requeue_action_items));
  });
  cat_menu::flow_panel("Region selector", 0, 360.0f, [&]() {
    draw_region_selector_panel("##region_selector_list");
  }, false);
  cat_menu::flow_panel("Utilities", 1, 278.0f, [&]() {
    cat_menu::checkbox("Anti AFK", &config.misc.automation.anti_afk);
    cat_menu::checkbox("Anti autobalance", &config.misc.automation.anti_autobalance);
    cat_menu::checkbox("Anti MOTD", &config.misc.automation.anti_motd);
    cat_menu::checkbox("Don't close MOTD during warmup", &config.misc.automation.anti_motd_dont_close_during_warmup);
    cat_menu::checkbox("Auto report", &config.misc.automation.auto_report);
    cat_menu::checkbox("Auto vote map", &config.misc.automation.auto_vote_map);
    cat_menu::slider_int("Vote option", &config.misc.automation.auto_vote_map_option, 0, 2);
    cat_menu::checkbox("Noisemaker spam", &config.misc.automation.noisemaker_spam);
    cat_menu::combo("Voice command spam", (int*)&config.misc.automation.voice_command_spam, voice_command_spam_items, IM_ARRAYSIZE(voice_command_spam_items));
    cat_menu::checkbox("Micspam", &config.misc.automation.micspam);
    cat_menu::slider_int("Micspam on", &config.misc.automation.micspam_interval_on_seconds, 1, 600, "%d s");
    cat_menu::slider_int("Micspam off", &config.misc.automation.micspam_interval_off_seconds, 1, 600, "%d s");
    cat_menu::checkbox("Micspam from file", &config.misc.automation.micspam_from_file);
  });
  cat_menu::flow_panel("AutoItem", 1, 290.0f, [&]() {
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
  cat_menu::flow_panel("MvM", 1, 202.0f, [&]() {
    cat_menu::checkbox("Instant respawn", &config.misc.automation.mvm_instant_respawn);
    cat_menu::checkbox("Instant revive", &config.misc.automation.mvm_instant_revive);
    cat_menu::checkbox("Allow inspect", &config.misc.automation.allow_mvm_inspect);
    cat_menu::checkbox("Auto ready up", &config.misc.automation.auto_mvm_ready_up);
    cat_menu::checkbox("Auto abandon Mann Up", &config.misc.automation.auto_mvm_abandon_mannup);
    cat_menu::checkbox("Buybot", &config.misc.automation.mvm_buybot);
    cat_menu::slider_int("Buybot max cash", &config.misc.automation.mvm_buybot_max_cash, 0, 50000);
    cat_menu::checkbox("Buybot auto class", &config.misc.automation.mvm_buybot_auto_class);
    cat_menu::combo("Buybot class", (int*)&config.misc.automation.mvm_buybot_class,
      class_items, IM_ARRAYSIZE(class_items));
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

  cat_menu::begin_flow_layout("chat_layout", 2);
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
  const char* queueing_mode_items[] = {
    "Normal",
    "Boost"
  };
  const char* boost_queue_mode_items[] = {
    "Wait",
    "Instant"
  };

  cat_menu::begin_flow_layout("queue_layout", 2);
  cat_menu::flow_panel("Queue", 1, 248.0f, [&]() {
    cat_menu::combo("Mode", (int*)&config.misc.automation.queue_mode, queueing_mode_items, IM_ARRAYSIZE(queueing_mode_items));
    if (config.misc.automation.queue_mode == Misc::Automation::queueing_mode::BOOST) {
      cat_menu::checkbox("Enabled", &config.misc.automation.boost_queue_enabled);
      cat_menu::combo("Boost", (int*)&config.misc.automation.boost_queue, boost_queue_mode_items, IM_ARRAYSIZE(boost_queue_mode_items));
      return;
    }
    cat_menu::checkbox("Auto queue", &config.misc.automation.auto_queue);
    cat_menu::checkbox("Auto requeue", &config.misc.automation.auto_requeue);
    cat_menu::checkbox("Requeue on kick", &config.misc.automation.requeue_on_kick);
    cat_menu::checkbox("Auto casual join", &config.misc.automation.auto_casual_join);
    cat_menu::combo("Queue mode", &config.misc.automation.auto_queue_mode, queue_mode_items, IM_ARRAYSIZE(queue_mode_items));
    cat_menu::slider_int("RQ if players <", &config.misc.automation.rq_if_players_lte, 0, 32);
    cat_menu::slider_int("RQ if players >", &config.misc.automation.rq_if_players_gte, 0, 32);
    cat_menu::slider_int("RQ if IPC bots >", &config.misc.automation.rq_if_ipc_bots_gt, 0, 32);
    cat_menu::checkbox("RQ if no navmesh", &config.misc.automation.rq_if_no_navmesh);
    cat_menu::checkbox("RQ ignore friends", &config.misc.automation.rq_ignore_friends);
    cat_menu::combo("Requeue action", (int*)&config.misc.automation.requeue_action, requeue_action_items, IM_ARRAYSIZE(requeue_action_items));
  });
  cat_menu::flow_panel("Region selector", 0, 390.0f, [&]() {
    draw_region_selector_panel("##queue_region_selector_list");
  }, false);
  cat_menu::end_flow_layout();
}

static void draw_automation_utilities_content() {
  const char* class_items[] = { "Undefined", "Scout", "Sniper", "Soldier", "Demoman", "Medic", "Heavy", "Pyro", "Spy", "Engineer" };
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

  cat_menu::begin_flow_layout("automation_utilities_layout", 2);
  cat_menu::flow_panel("Class", 0, 104.0f, [&]() {
    cat_menu::checkbox("Auto class select", &config.misc.automation.auto_class_select);
    cat_menu::combo("Preferred class", (int*)&config.misc.automation.class_selected, class_items, IM_ARRAYSIZE(class_items));
    cat_menu::checkbox("Don't join class during warmup", &config.misc.automation.auto_class_dont_join_during_warmup);
  });
  cat_menu::flow_panel("General", 0, 190.0f, [&]() {
    cat_menu::checkbox("Anti AFK", &config.misc.automation.anti_afk);
    cat_menu::checkbox("Anti autobalance", &config.misc.automation.anti_autobalance);
    cat_menu::checkbox("Anti MOTD", &config.misc.automation.anti_motd);
    cat_menu::checkbox("Don't close MOTD during warmup", &config.misc.automation.anti_motd_dont_close_during_warmup);
    cat_menu::checkbox("Auto report", &config.misc.automation.auto_report);
    cat_menu::checkbox("Auto vote map", &config.misc.automation.auto_vote_map);
    cat_menu::slider_int("Vote option", &config.misc.automation.auto_vote_map_option, 0, 2);
    cat_menu::checkbox("Custom announcer", &config.misc.automation.custom_announcer);
  });
  cat_menu::flow_panel("Spam", 1, 200.0f, [&]() {
    cat_menu::checkbox("Noisemaker spam", &config.misc.automation.noisemaker_spam);
    cat_menu::combo("Voice command spam", (int*)&config.misc.automation.voice_command_spam, voice_command_spam_items, IM_ARRAYSIZE(voice_command_spam_items));
    cat_menu::checkbox("Micspam", &config.misc.automation.micspam);
    cat_menu::slider_int("Micspam on", &config.misc.automation.micspam_interval_on_seconds, 1, 600, "%d s");
    cat_menu::slider_int("Micspam off", &config.misc.automation.micspam_interval_off_seconds, 1, 600, "%d s");
    cat_menu::checkbox("Micspam from file", &config.misc.automation.micspam_from_file);
  });
  cat_menu::flow_panel("Taunt", 1, 128.0f, [&]() {
    cat_menu::checkbox("Auto taunt", &config.misc.automation.autotaunt);
    cat_menu::slider_float("Taunt chance", &config.misc.automation.autotaunt_chance, 0.0f, 100.0f, "%.0f%%");
    cat_menu::slider_float("Taunt safety distance", &config.misc.automation.autotaunt_safety_distance, 0.0f, 5000.0f, "%.0f HU");
    cat_menu::slider_int("Taunt weapon slot", &config.misc.automation.autotaunt_weapon_slot, 0, 5);
  });
  cat_menu::flow_panel("MvM", 1, 202.0f, [&]() {
    cat_menu::checkbox("Instant respawn", &config.misc.automation.mvm_instant_respawn);
    cat_menu::checkbox("Instant revive", &config.misc.automation.mvm_instant_revive);
    cat_menu::checkbox("Allow inspect", &config.misc.automation.allow_mvm_inspect);
    cat_menu::checkbox("Auto ready up", &config.misc.automation.auto_mvm_ready_up);
    cat_menu::checkbox("Auto abandon Mann Up", &config.misc.automation.auto_mvm_abandon_mannup);
    cat_menu::checkbox("Buybot", &config.misc.automation.mvm_buybot);
    cat_menu::slider_int("Buybot max cash", &config.misc.automation.mvm_buybot_max_cash, 0, 50000);
    cat_menu::checkbox("Buybot auto class", &config.misc.automation.mvm_buybot_auto_class);
    cat_menu::combo("Buybot class", (int*)&config.misc.automation.mvm_buybot_class,
      class_items, IM_ARRAYSIZE(class_items));
  });
  cat_menu::end_flow_layout();
}

#if 0 // Inventory changer UI temporarily disabled.
static void draw_inventory_changer_content() {
  cat_menu::begin_flow_layout("inventory_changer_layout", 2);
  cat_menu::flow_panel("Inventory changer", 0, 238.0f, [&]() {
    cat_menu::checkbox("Enable", &config.misc.inventory_changer.enabled);
    cat_menu::checkbox("Apply to all", &config.misc.inventory_changer.apply_to_all);
    cat_menu::checkbox("Debug", &config.misc.inventory_changer.debug);
    cat_menu::draw_inventory_definition("Crate", &config.misc.inventory_changer.crate,
      inventory_changer::item_category::crate);
    cat_menu::draw_inventory_definition("Key", &config.misc.inventory_changer.key,
      inventory_changer::item_category::key);
    ImGui::TextWrapped("Crates and keys are local inventory redirects; opening still uses the game's normal key/item checks.");
  });
  cat_menu::flow_panel("Weapons", 1, 540.0f, [&]() {
    cat_menu::draw_inventory_slot("Primary", config.misc.inventory_changer.primary, true);
    cat_menu::draw_inventory_slot("Secondary", config.misc.inventory_changer.secondary, true);
    cat_menu::draw_inventory_slot("Melee", config.misc.inventory_changer.melee, true);
  });
  cat_menu::flow_panel("Hats", 0, 540.0f, [&]() {
    cat_menu::draw_inventory_slot("Hat 1", config.misc.inventory_changer.hat1, false);
    cat_menu::draw_inventory_slot("Hat 2", config.misc.inventory_changer.hat2, false);
    cat_menu::draw_inventory_slot("Hat 3", config.misc.inventory_changer.hat3, false);
  });
  cat_menu::flow_panel("Taunt", 1, 100.0f, [&]() {
    const auto& effects = inventory_changer::effect_options();
    std::vector<const char*> effect_labels{};
    effect_labels.reserve(effects.size());
    int selected_effect = 0;
    for (std::size_t index = 0; index < effects.size(); ++index) {
      effect_labels.push_back(effects[index].label.c_str());
      if (effects[index].definition == static_cast<std::uint16_t>(config.misc.inventory_changer.taunt1_unusual)) selected_effect = static_cast<int>(index);
    }
    if (cat_menu::combo("Slot 1 unusual", &selected_effect, effect_labels.data(), static_cast<int>(effect_labels.size())) &&
        selected_effect >= 0 && static_cast<std::size_t>(selected_effect) < effects.size()) {
      config.misc.inventory_changer.taunt1_unusual = effects[static_cast<std::size_t>(selected_effect)].definition;
    }
  });
  cat_menu::end_flow_layout();
}
#endif

static void draw_autoitem_content() {
  cat_menu::begin_flow_layout("autoitem_layout", 2);
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
  cat_menu::flow_panel("Cosmetics", 1, 182.0f, [&]() {
    cat_menu::checkbox("Hats", &config.misc.automation.auto_item_hats);
    cat_menu::input_text("Hat 1", &config.misc.automation.auto_item_hat1);
    cat_menu::input_text("Hat 2", &config.misc.automation.auto_item_hat2);
    cat_menu::input_text("Hat 3", &config.misc.automation.auto_item_hat3);
    cat_menu::checkbox("Noisemaker", &config.misc.automation.auto_item_noisemaker);
  });
  cat_menu::end_flow_layout();
}

static void draw_ipc_content() {
#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE

  config.ipc.enabled = true;
  config.ipc.auto_connect = true;
  config.ipc.auto_ignore_local_bots = true;
#endif

  const bool connected = cat_ipc::client::connected();
  const int peer_id = cat_ipc::client::peer_id();

  cat_menu::begin_flow_layout("ipc_layout", 2);
  cat_menu::flow_panel("Connection", 0, 156.0f, [&]() {
    cat_menu::checkbox("Enable IPC", &config.ipc.enabled);
    cat_menu::checkbox("Auto connect", &config.ipc.auto_connect);
    cat_menu::checkbox("Auto ignore local bots", &config.ipc.auto_ignore_local_bots);
#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE

    config.ipc.enabled = true;
    config.ipc.auto_connect = true;
    config.ipc.auto_ignore_local_bots = true;
#endif

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, connected ? cat_menu::menu_accent() : cat_menu::k_text_soft);
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
#if !defined(CATHOOK_TEXTMODE) || !CATHOOK_TEXTMODE

    if (cat_menu::accent_button("Disconnect", ImVec2(0.0f, 22.0f), true)) {
      config.ipc.enabled = false;
      cat_ipc::client::shutdown();
    }
#endif

  });
  cat_menu::flow_panel("Notes", 1, 94.0f, [&]() {
    ImGui::PushStyleColor(ImGuiCol_Text, cat_menu::k_text_soft);
    ImGui::TextUnformatted("Connect uses the catbot shared memory server.");
#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE

    ImGui::TextUnformatted("Textmode forces IPC on.");
#else

    ImGui::TextUnformatted("Auto connect retries while IPC is enabled.");
    ImGui::TextUnformatted("Disconnect disables IPC until enabled again.");
#endif

    ImGui::PopStyleColor();
  });
  cat_menu::end_flow_layout();
}

static void draw_automation_tab(const int automation_subtab) {
  switch (automation_subtab) {
    case cat_menu::automation_subtab_general: draw_automation_utilities_content(); break;
    case cat_menu::automation_subtab_queue: draw_queue_content(); break;
    case cat_menu::automation_subtab_items_chat: draw_autoitem_content(); draw_chat_content(); break;
    case cat_menu::automation_subtab_navbot: draw_navbot_content(); break;
    case cat_menu::automation_subtab_medic: draw_medic_content(); break;
    case cat_menu::automation_subtab_ipc: draw_ipc_content(); break;
  }
}

static void draw_exploits_content() {
  static const char* backtrack_visualizer_items[] = {
    "Points",
    "Boxes",
    "Projected boxes",
    "Trail",
    "Pulse"
  };

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

  cat_menu::begin_flow_layout("exploits_layout", 2);

  cat_menu::flow_panel("Backtrack", 0, 280.0f, [&]() {
    cat_menu::checkbox("Enable", &config.backtrack.enabled);
    cat_menu::slider_int("Window", &config.backtrack.window_ms, 0, 1000, "%d ms");
    cat_menu::slider_float("Fake latency", &config.backtrack.fake_latency_ms, 0.0f, 1000.0f, "%.0f ms");
    cat_menu::checkbox("Fake interp", &config.backtrack.fake_interp);
    ImGui::BeginDisabled(!config.backtrack.fake_interp);
    cat_menu::slider_float("Interpolation", &config.backtrack.interp_ms, 0.0f, 1000.0f, "%.0f ms");
    ImGui::EndDisabled();
    cat_menu::checkbox("Prefer on shot", &config.backtrack.prefer_on_shot);
    cat_menu::checkbox("Backtrack to crosshair", &config.backtrack.to_crosshair);
    cat_menu::slider_int("Tick offset", &config.backtrack.offset_ticks, -4, 4, "%d ticks");
    cat_menu::checkbox("Visualizer", &config.backtrack.visualizer);
    cat_menu::combo("Visualizer style", (int*)&config.backtrack.visualizer_mode, backtrack_visualizer_items, IM_ARRAYSIZE(backtrack_visualizer_items));
    cat_menu::slider_int("Ticks", &config.backtrack.visualizer_ticks, 1, 80);
  });

  cat_menu::flow_panel("Bypasses", 1, 224.0f, [&]() {
    cat_menu::checkbox("Bypass sv_pure", &config.misc.exploits.bypasspure);
    cat_menu::checkbox("Pure bypass", &config.misc.exploits.pure_bypass);
    cat_menu::checkbox("Cheats bypass", &config.misc.exploits.cheats_bypass);
    cat_menu::checkbox("VAC bypass", &config.misc.exploits.vac_bypass);
    cat_menu::checkbox("Network fix", &config.misc.exploits.network_fix);
    cat_menu::checkbox("No engine sleep", &config.misc.exploits.no_engine_sleep);
    cat_menu::checkbox("Null graphics", &config.misc.exploits.null_graphics);
  });
  cat_menu::flow_panel("Tickbase", 1, 224.0f, [&]() {
    cat_menu::checkbox("Tickbase", &config.misc.exploits.tickbase);
    cat_menu::checkbox("Recharge", &config.misc.exploits.tickbase_recharge);
    cat_menu::checkbox("Doubletap", &config.misc.exploits.doubletap);
    cat_menu::slider_int("Doubletap ticks", &config.misc.exploits.doubletap_ticks, 1, 21);
    cat_menu::checkbox("Warp", &config.misc.exploits.warp);
    cat_menu::slider_int("Warp ticks", &config.misc.exploits.warp_ticks, 1, 21);
    cat_menu::checkbox("Fakelag", &config.misc.exploits.fakelag);
    cat_menu::slider_int("Fakelag ticks", &config.misc.exploits.fakelag_ticks, 1, 21);
    cat_menu::checkbox("Antiwarp", &config.misc.exploits.antiwarp);
  });
  cat_menu::flow_panel("Engine", 0, 118.0f, [&]() {
    cat_menu::checkbox("Equip region unlock", &config.misc.exploits.equip_region_unlock);
    cat_menu::checkbox("Ping reducer", &config.misc.exploits.ping_reducer);
    cat_menu::slider_int("Ping target", &config.misc.exploits.ping_target, 1, 100);
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
  cat_menu::end_flow_layout();
}

static void draw_misc_content(const int misc_subtab) {
  cat_menu::begin_flow_layout("misc_layout", 2);
  if (misc_subtab == 0) {
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

    cat_menu::flow_panel("Exploits", 0, 332.0f, [&]() {
      cat_menu::checkbox("Bypass sv_pure", &config.misc.exploits.bypasspure);
      cat_menu::checkbox("Pure bypass", &config.misc.exploits.pure_bypass);
      cat_menu::checkbox("Cheats bypass", &config.misc.exploits.cheats_bypass);
      cat_menu::checkbox("VAC bypass", &config.misc.exploits.vac_bypass);
      cat_menu::checkbox("Network fix", &config.misc.exploits.network_fix);
      cat_menu::checkbox("Tickbase", &config.misc.exploits.tickbase);
      cat_menu::checkbox("Recharge", &config.misc.exploits.tickbase_recharge);
      cat_menu::checkbox("Doubletap", &config.misc.exploits.doubletap);
      cat_menu::slider_int("Doubletap ticks", &config.misc.exploits.doubletap_ticks, 1, 21);
      cat_menu::checkbox("Warp", &config.misc.exploits.warp);
      cat_menu::slider_int("Warp ticks", &config.misc.exploits.warp_ticks, 1, 21);
      cat_menu::checkbox("Fakelag", &config.misc.exploits.fakelag);
      cat_menu::slider_int("Fakelag ticks", &config.misc.exploits.fakelag_ticks, 1, 21);
      cat_menu::checkbox("Antiwarp", &config.misc.exploits.antiwarp);
      cat_menu::checkbox("Equip region unlock", &config.misc.exploits.equip_region_unlock);
      cat_menu::checkbox("Ping reducer", &config.misc.exploits.ping_reducer);
      cat_menu::slider_int("Ping target", &config.misc.exploits.ping_target, 1, 100);
      cat_menu::checkbox("No engine sleep", &config.misc.exploits.no_engine_sleep);
      cat_menu::checkbox("Null graphics", &config.misc.exploits.null_graphics);
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
  static int unlock_click_count = 0;

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
    cat_menu::combo("Menu scale", &config.misc.menu.dpi_scale, cat_menu::k_dpi_scale_labels.data(), static_cast<int>(cat_menu::k_dpi_scale_labels.size()));
    cat_menu::checkbox("Draw all entities", &config.debug.debug_render_all_entities);
    cat_menu::checkbox("Show active flag IDs", &config.debug.show_active_flag_ids_of_players);

    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    const char* button_label = config.debug.insider_settings_unlocked ? "Insider Settings: Unlocked" : "Unlock Insider Settings";
    if (cat_menu::accent_button(button_label, ImVec2(-1.0f, 26.0f), false)) {
      if (!config.debug.insider_settings_unlocked) {
        unlock_click_count++;
        if (unlock_click_count >= 5) {
          config.debug.insider_settings_unlocked = true;
          unlock_click_count = 0;
          if (engine != nullptr) {
            engine->client_cmd_unrestricted("play ui/duel_challenge.wav");
          }
        }
      } else {
        config.debug.insider_settings_unlocked = false;
        unlock_click_count = 0;
        enforce_insider_settings_lock(config);
      }
    }

    if (!config.debug.insider_settings_unlocked && unlock_click_count > 0) {
      ImGui::PushStyleColor(ImGuiCol_Text, cat_menu::k_text_soft);
      ImGui::Text("Clicks: %d/5", unlock_click_count);
      ImGui::PopStyleColor();
    }
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
      if (config_store->save_file(config_name)) {
        cat_bind::save(config_store, config_name);
      }
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (cat_menu::accent_button("Save", ImVec2(0.0f, 22.0f))) {
      config_store->import_config(config);
      if (config_store->save_file(config_name)) {
        cat_bind::save(config_store, config_name);
      }
    }
    if (cat_menu::accent_button("Load", ImVec2((ImGui::GetContentRegionAvail().x - 6.0f) * 0.5f, 22.0f))) {
      if (config_store->load_file(config_name)) {
        config_store->export_config(config);
        reset_insider_settings_session(config);
        cat_bind::load(config_store);
      }
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (cat_menu::accent_button("Delete", ImVec2(0.0f, 22.0f), true)) {
      config_store->delete_file(config_name);
      cat_bind::delete_file(config_store, config_name);
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, cat_menu::k_text_soft);
    ImGui::TextUnformatted("Actions save the current in-memory config.");
    ImGui::TextUnformatted("Load replaces the current settings from disk.");
    ImGui::PopStyleColor();
  });
  cat_menu::end_flow_layout();
}

static void draw_materials_content() {
  materials.prepare();
  static std::string new_material_name{};
  static std::string selected_material{};
  static std::string material_source{};
  static bool selected_locked = false;

  ImGui::BeginChild("material_editor_layout", {0.0f, 0.0f}, ImGuiChildFlags_None);
  const float left_width = std::clamp(ImGui::GetContentRegionAvail().x * 0.34f, cat_menu::scaled(190.0f), cat_menu::scaled(250.0f));
  ImGui::BeginChild("material_manager", {left_width, 0.0f}, ImGuiChildFlags_Border);
  ImGui::TextUnformatted("material manager");
  mono::group_separator();
  cat_menu::input_text("New material", &new_material_name);
  if (cat_menu::accent_button("Create material", {-1.0f, 22.0f}) && materials.add(new_material_name)) {
    selected_material = new_material_name;
    if (const auto definition = materials.find(selected_material)) {
      material_source = definition->vmt;
      selected_locked = definition->locked;
    }
    new_material_name.clear();
  }
  if (cat_menu::accent_button("Reload materials", {-1.0f, 22.0f})) {
    materials.reload();
    selected_material.clear();
    material_source.clear();
  }
  ImGui::TextDisabled("%s", materials.directory().string().c_str());
  mono::group_separator();
  std::vector<material_definition> definitions = materials.definitions();
  std::ranges::sort(definitions, [](const material_definition& left, const material_definition& right) {
    if (left.locked != right.locked) return left.locked > right.locked;
    return left.name < right.name;
  });
  ImGui::BeginChild("material_list", {0.0f, 0.0f}, ImGuiChildFlags_Border);
  for (const material_definition& definition : definitions) {
    ImGui::PushID(definition.name.c_str());
    if (cat_menu::list_row(definition.name.c_str(), selected_material == definition.name, {0.0f, 26.0f})) {
      selected_material = definition.name;
      material_source = definition.vmt;
      selected_locked = definition.locked;
    }
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::EndChild();

  ImGui::SameLine(0.0f, cat_menu::scaled(cat_menu::k_gap));
  ImGui::BeginChild("material_editor", {0.0f, 0.0f}, ImGuiChildFlags_Border);
  ImGui::TextUnformatted("material editor");
  mono::group_separator();
  if (selected_material.empty()) {
    ImGui::TextDisabled("Select a material to view or edit its VMT.");
  } else {
    ImGui::TextDisabled("%s: %s", selected_locked ? "viewing" : "editing", selected_material.c_str());
    if (!selected_locked) {
      if (cat_menu::accent_button("Save material", {-1.0f, 22.0f})) {
        materials.edit(selected_material, material_source);
      }
      if (cat_menu::accent_button("Delete material", {-1.0f, 22.0f}, true) && materials.remove(selected_material)) {
        selected_material.clear();
        material_source.clear();
        selected_locked = false;
      }
    }
    ImGui::InputTextMultiline("##material_source", &material_source, {-1.0f, -1.0f}, ImGuiInputTextFlags_AllowTabInput);
  }
  ImGui::EndChild();
  ImGui::EndChild();
}

static void draw_interface_content() {
  cat_menu::begin_flow_layout("interface_layout", 1);
  cat_menu::flow_panel("Menu appearance", 0, 252.0f, [&]() {
    cat_menu::color_picker("Theme color", &config.misc.menu.theme_color);
    cat_menu::combo("Menu scale", &config.misc.menu.dpi_scale, cat_menu::k_dpi_scale_labels.data(), static_cast<int>(cat_menu::k_dpi_scale_labels.size()));
  });
  cat_menu::end_flow_layout();
}

static void draw_system_tab(const int system_subtab) {
  switch (system_subtab) {
    case 0:
      draw_config_content();
      break;
    case 1:
      draw_materials_content();
      break;
    case 2:
      draw_interface_content();
      break;
  }
}
#include "player_window.hpp"

static void draw_binds_content() {
  static uint32_t selected_id{};

  std::lock_guard lock{cat_bind::bind_mutex()};
  if (cat_bind::find_entry(selected_id) == nullptr && !cat_bind::entries().empty()) {
    selected_id = cat_bind::entries().front().id;
  }

  const float left_width = std::max(220.0f, ImGui::GetContentRegionAvail().x * 0.36f);
  mono::begin_transparent_child("bind_tree_column", { left_width, 0.0f });
  if (ImGui::BeginChild("bind_tree", { 0.0f, 0.0f }, ImGuiChildFlags_Border)) {
    ImGui::TextUnformatted("binds");
    mono::group_separator();
    if (mono::button("add root bind", { -1.0f, 0.0f })) {
      selected_id = cat_bind::add_and_capture_key();
    }
    for (const cat_bind::bind_entry& bind : cat_bind::entries()) {
      std::string label = bind.name;
      label += "  [";
      label += bind.condition == cat_bind::bind_condition::key ? get_button_name(bind.key) : cat_bind::condition_label(bind.condition);
      label += "]";
      ImGui::PushID(static_cast<int>(bind.id));
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (bind.parent_id ? 18.0f : 0.0f));
      if (cat_menu::list_row(label.c_str(), selected_id == bind.id, { 0.0f, 24.0f })) {
        selected_id = bind.id;
      }
      ImGui::PopID();
    }
  }
  ImGui::EndChild();
  ImGui::EndChild();
  ImGui::SameLine();

  if (ImGui::BeginChild("bind_editor", { 0.0f, 0.0f }, ImGuiChildFlags_Border)) {
    cat_bind::bind_entry* bind = cat_bind::find_entry(selected_id);
    if (bind == nullptr) {
      ImGui::TextDisabled("Select or create a bind.");
    } else if (bind != nullptr) {
      ImGui::PushID(static_cast<int>(bind->id));
      ImGui::TextUnformatted("bind editor");
      mono::group_separator();
      if (mono::input_string("Name", &bind->name)) cat_bind::mark_dirty();
      const std::vector<std::pair<std::string, int>> conditions{
        { "key", 0 }, { "class", 1 }, { "weapon type", 2 }, { "item slot", 3 }, { "misc", 4 }
      };
      int condition = static_cast<int>(bind->condition);
      if (mono::select_single("Condition", &condition, conditions)) {
        bind->condition = static_cast<cat_bind::bind_condition>(condition);
        bind->condition_value = 0;
        cat_bind::mark_dirty();
      }
      if (bind->condition == cat_bind::bind_condition::key) {
        const std::vector<std::pair<std::string, int>> modes{
          { "hold", 0 }, { "toggle", 1 }, { "double click", 2 }
        };
        int mode = static_cast<int>(bind->key_mode);
        if (mono::select_single("Behavior", &mode, modes)) {
          bind->key_mode = static_cast<cat_bind::bind_key_mode>(mode);
          bind->toggle_state = false;
          bind->was_down = false;
          bind->press_pending = false;
          cat_bind::mark_dirty();
        }
        if (bind->waiting) {
          ImGui::TextDisabled("Press a key or mouse button...");
        } else {
          const int previous_key = bind->key;
          if (cat_menu::input_key("Key", &bind->key) && previous_key != bind->key) {
            bind->toggle_state = false;
            bind->was_down = false;
            bind->press_pending = false;
            cat_bind::mark_dirty();
          }
        }
      } else {
        std::vector<std::pair<std::string, int>> values;
        if (bind->condition == cat_bind::bind_condition::player_class) {
          values = { { "scout", static_cast<int>(tf_class::SCOUT) }, { "sniper", static_cast<int>(tf_class::SNIPER) }, { "soldier", static_cast<int>(tf_class::SOLDIER) }, { "demoman", static_cast<int>(tf_class::DEMOMAN) }, { "medic", static_cast<int>(tf_class::MEDIC) }, { "heavy", static_cast<int>(tf_class::HEAVYWEAPONS) }, { "pyro", static_cast<int>(tf_class::PYRO) }, { "spy", static_cast<int>(tf_class::SPY) }, { "engineer", static_cast<int>(tf_class::ENGINEER) } };
        } else if (bind->condition == cat_bind::bind_condition::weapon_type) {
          values = { { "hitscan", 0 }, { "projectile", 1 }, { "melee", 2 }, { "throwable", 3 } };
        } else if (bind->condition == cat_bind::bind_condition::item_slot) {
          for (int slot{}; slot < 9; ++slot) values.emplace_back(std::to_string(slot + 1), slot);
        } else {
          values = { { "spectated", 0 }, { "spectated 1st", 1 }, { "spectated 3rd", 2 }, { "zoomed", 3 }, { "aiming", 4 } };
        }
        int value = bind->condition_value;
        if (mono::select_single("Value", &value, values)) {
          bind->condition_value = value;
          cat_bind::mark_dirty();
        }
      }
      if (mono::toggle("Enabled", &bind->enabled)) cat_bind::mark_dirty();
      if (mono::toggle("Invert", &bind->inverted)) cat_bind::mark_dirty();
      const std::vector<std::pair<std::string, int>> visibility{
        { "always", 0 }, { "while active", 1 }, { "hidden", 2 }
      };
      int visible = static_cast<int>(bind->visibility);
      if (mono::select_single("Indicator", &visible, visibility)) {
        bind->visibility = static_cast<cat_bind::bind_visibility>(visible);
        cat_bind::mark_dirty();
      }
      std::vector<std::pair<std::string, int>> parents{ { "root", 0 } };
      std::vector<uint32_t> parent_ids{ 0 };
      for (const cat_bind::bind_entry& candidate : cat_bind::entries()) {
        if (candidate.id != bind->id) {
          parents.emplace_back(candidate.name, static_cast<int>(parents.size()));
          parent_ids.push_back(candidate.id);
        }
      }
      int parent_index{};
      for (size_t index{}; index < parent_ids.size(); ++index) if (parent_ids[index] == bind->parent_id) parent_index = static_cast<int>(index);
      if (mono::select_single("Parent", &parent_index, parents)) cat_bind::reparent(bind->id, parent_ids[static_cast<size_t>(parent_index)]);

      ImGui::SeparatorText("Feature overrides");
      if (bind->overrides.empty()) ImGui::TextDisabled("No feature values assigned. Use edit feature values, then click a setting.");
      std::string override_to_clear{};
      for (const auto& [target_key, value] : bind->overrides) {
        const cat_bind::target_entry* target = cat_bind::find_target(target_key);
        ImGui::Text("%s", target != nullptr ? target->label.c_str() : target_key.c_str());
        const std::string value_text = std::visit([](const auto& item) -> std::string {
          using item_type = std::decay_t<decltype(item)>;
          if constexpr (std::is_same_v<item_type, std::string>) return item;
          else if constexpr (std::is_same_v<item_type, RGBA_float>) return "color";
          else return std::to_string(item);
        }, value);
        ImGui::SameLine();
        ImGui::TextDisabled("= %s", value_text.c_str());
        ImGui::SameLine();
        ImGui::PushID(target_key.c_str());
        if (ImGui::SmallButton("clear")) override_to_clear = target_key;
        ImGui::PopID();
      }

      if (!override_to_clear.empty()) cat_bind::clear_override(bind->id, override_to_clear);
      if (cat_bind::editing() == bind->id) {
        if (mono::button("done editing")) cat_bind::set_editing(0);
      } else if (mono::button("edit feature values")) {
        cat_bind::set_editing(bind->id);
      }
      ImGui::SameLine();
      if (mono::button("add child")) selected_id = cat_bind::add_and_capture_key("new bind", bind->id);
      ImGui::SameLine();
      if (mono::button("delete bind", {}, true)) {
        const uint32_t id = bind->id;
        cat_bind::remove(id);
        selected_id = 0;
      }
      ImGui::PopID();
    }
  }
  ImGui::EndChild();
}

enum cathook_tab_id
{
  cathook_tab_aimbot,
  cathook_tab_automation,
  cathook_tab_exploits,
  cathook_tab_visuals,
  cathook_tab_misc,
  cathook_tab_binds,
  cathook_tab_settings
};

static void draw_settings_content(const cathook_tab_id tab, const int section, const int settings_section) {
  enforce_insider_settings_lock(config);

  switch (tab) {
  case cathook_tab_aimbot:    draw_combat_tab(section); break;
  case cathook_tab_automation: draw_automation_tab(section); break;
  case cathook_tab_exploits:  draw_exploits_content(); break;
  case cathook_tab_visuals:   draw_visuals_tab(section); break;
  case cathook_tab_misc:
    if (section == 0) draw_misc_content(0);
    else draw_movement_content();
    break;
  case cathook_tab_binds:     draw_binds_content(); break;
  case cathook_tab_settings:  draw_system_tab(settings_section); break;
  }
}

static void warmup_bind_targets()
{
  if (cat_bind::disabled() || cat_bind::targets_warmed() || ImGui::GetCurrentContext() == nullptr) return;

  const ImVec2 display_size = ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowPos({ 0.0f, 0.0f }, ImGuiCond_Always);
  ImGui::SetNextWindowSize({ std::max(display_size.x, 1.0f), std::max(display_size.y, 1.0f) }, ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.0f);
  constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
    ImGuiWindowFlags_NoBackground |
    ImGuiWindowFlags_NoInputs |
    ImGuiWindowFlags_NoNav |
    ImGuiWindowFlags_NoSavedSettings |
    ImGuiWindowFlags_NoBringToFrontOnFocus;

  if (!ImGui::Begin("##cathook_bind_target_warmup", nullptr, flags)) {
    ImGui::End();
    return;
  }

  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
  cat_bind::target_registration_scope registration{};

  draw_combat_tab(0);
  draw_combat_tab(1);
  draw_automation_tab(cat_menu::automation_subtab_general);
  draw_automation_tab(cat_menu::automation_subtab_queue);
  draw_automation_tab(cat_menu::automation_subtab_items_chat);
  draw_automation_tab(cat_menu::automation_subtab_navbot);
  draw_automation_tab(cat_menu::automation_subtab_medic);
  draw_automation_tab(cat_menu::automation_subtab_ipc);
  draw_exploits_content();
  draw_visuals_tab(cat_menu::visuals_subtab_indicators);
  draw_visuals_tab(cat_menu::visuals_subtab_map);
  draw_visuals_tab(cat_menu::visuals_subtab_other);
  draw_misc_content(0);
  draw_misc_content(1);

  int& selected_group = cat_menu::selected_visual_group();
  const int previous_group = selected_group;
  const int group_count = static_cast<int>(config.visual_groups.groups.size());
  for (int index = 0; index < group_count; ++index) {
    selected_group = index;
    ImGui::PushID(index);
    draw_visuals_tab(cat_menu::visuals_subtab_entity_profiles);
    ImGui::PopID();
  }
  selected_group = previous_group;

  ImGui::PopStyleVar();
  ImGui::End();
  cat_bind::targets_warmed() = true;
}

static void draw_menu(void) {
  cat_bind::set_menu_open(menu_focused || player_manager_window_open);
  set_imgui_theme();

  static mono::menu_state menu_state{};
  static mono::menu_state player_manager_state{};
  static cathook_tab_id tab{ cathook_tab_aimbot };
  static int aimbot_section{};
  static int automation_section{};
  static int visuals_section{};
  static int misc_section{};
  static int settings_section{};

  const auto navbar_entry = [](const char* const label, const bool active, const char* const icon = nullptr) {
    return mono::navbar_entry(label, active, icon, cat_menu::font_icons());
  };
  const auto subnavbar_entry = [](const char* const label, const bool active) {
    return mono::subnavbar_entry(label, active, nullptr, cat_menu::font_icons());
  };

  if (menu_focused) {
  const std::string editing_name = cat_bind::editing_name();
  const bool visible = mono::begin_menu(
    {
      .id = "monolilth",
      .title = "cathook",
      .preferred_size = cat_menu::k_menu_size,
      .navbar_height = 42.0f,
      .subnavbar_height = 32.0f,
      .show_subnavbar = tab != cathook_tab_exploits && tab != cathook_tab_binds,
      .viewport_padding = 8.0f,
      .background_alpha = 0.80f
    },
    menu_state,
    [&] {
      auto select = [&](const cathook_tab_id value, const char* const label, const char* const icon) {
        if (navbar_entry(label, tab == value, icon)) tab = value;
      };

      select(cathook_tab_aimbot, "Aimbot", ICON_MD_GPS_FIXED);
      select(cathook_tab_automation, "Automation", ICON_MD_AUTORENEW);
      select(cathook_tab_exploits, "Exploits", ICON_MD_BOLT);
      select(cathook_tab_visuals, "Visuals", ICON_MD_VISIBILITY);
      select(cathook_tab_misc, "Miscellaneous", ICON_MD_TUNE);
      select(cathook_tab_binds, "Binds", ICON_MD_VPN_KEY);
      select(cathook_tab_settings, "Settings", ICON_MD_SETTINGS);
    },
    [&] {
      switch (tab) {
      case cathook_tab_aimbot:
        if (subnavbar_entry("Main", aimbot_section == 0)) aimbot_section = 0;
        if (subnavbar_entry("Draw", aimbot_section == 1)) aimbot_section = 1;
        break;
      case cathook_tab_automation:
        if (subnavbar_entry("General", automation_section == cat_menu::automation_subtab_general)) automation_section = cat_menu::automation_subtab_general;
        if (subnavbar_entry("Queue", automation_section == cat_menu::automation_subtab_queue)) automation_section = cat_menu::automation_subtab_queue;
        if (subnavbar_entry("Items & chat", automation_section == cat_menu::automation_subtab_items_chat)) automation_section = cat_menu::automation_subtab_items_chat;
        if (subnavbar_entry("Navbot", automation_section == cat_menu::automation_subtab_navbot)) automation_section = cat_menu::automation_subtab_navbot;
        if (subnavbar_entry("Medic", automation_section == cat_menu::automation_subtab_medic)) automation_section = cat_menu::automation_subtab_medic;
        if (subnavbar_entry("IPC", automation_section == cat_menu::automation_subtab_ipc)) automation_section = cat_menu::automation_subtab_ipc;
        break;
      case cathook_tab_visuals:
        if (subnavbar_entry("Entity profiles", visuals_section == 0)) visuals_section = 0;
        if (subnavbar_entry("Indicators", visuals_section == 1)) visuals_section = 1;
        if (subnavbar_entry("Map", visuals_section == 2)) visuals_section = 2;
        if (subnavbar_entry("Other", visuals_section == 3)) visuals_section = 3;
        break;
      case cathook_tab_misc:
        if (subnavbar_entry("General", misc_section == 0)) misc_section = 0;
        if (subnavbar_entry("Movement", misc_section == 1)) misc_section = 1;
        break;
      case cathook_tab_settings:
        if (subnavbar_entry("Configurations", settings_section == 0)) settings_section = 0;
        if (subnavbar_entry("Materials", settings_section == 1)) settings_section = 1;
        if (subnavbar_entry("Interface", settings_section == 2)) settings_section = 2;
        break;
      case cathook_tab_exploits:
      case cathook_tab_binds:
        break;
      }
    },
    {
      .label = "editing bind",
      .value = editing_name,
      .action = "done",
      .on_action = [] { cat_bind::set_editing(0); }
    });

  if (visible) {
    draw_settings_content(tab, tab == cathook_tab_misc ? misc_section :
      tab == cathook_tab_visuals ? visuals_section : tab == cathook_tab_aimbot ? aimbot_section :
      tab == cathook_tab_automation ? automation_section : 0, settings_section);
    cat_bind::draw_popup();
  }
  mono::end_menu(visible);
  }

  if (player_manager_window_open) {
    if (!player_manager_state.position_initialized) {
      player_manager_state.position = {
        menu_state.position.x + cat_menu::k_menu_size.x + 24.0f,
        menu_state.position.y
      };
    }
    const bool player_visible = mono::begin_menu(
      {
        .id = "cathook_player_manager##mono_menu",
        .title = "Player Manager",
        .preferred_size = { 640.0f, 460.0f },
        .show_subnavbar = false,
        .viewport_padding = 8.0f,
        .background_alpha = 0.80f
      },
      player_manager_state,
      [] {},
      [] {},
      {
        .label = "Player Manager",
        .action = "close",
        .on_action = [] {
          player_manager_window_open = false;
          cat_bind::set_menu_open(menu_focused);
          if (!menu_focused && surface != nullptr) {
            surface->set_cursor_visible(false);
          }
        }
      });
    if (player_visible) cat_menu::draw_player_window_content();
    mono::end_menu(player_visible);
  }
}
#endif
