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
#include "features/automation/mvm_queue/mvm_queue.hpp"
#include "features/automation/navbot/navbot_types.hpp"
#include "features/automation/profile_stalker_api.hpp"
#include "features/automation/region_selector/region_selector.hpp"
#include "features/visuals/material_manager.hpp"
#include "features/visuals/groups/visual_groups.hpp"
#include "features/visuals/skybox_changer.hpp"
#include "features/visuals/skin_changer.hpp"
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
#include <functional>
#include <initializer_list>
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

namespace pup_menu
{

}

namespace pup_menu
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

enum combat_subtab_id
{
  combat_subtab_aimbot,
  combat_subtab_weapons,
  combat_subtab_overlay
};

enum visuals_subtab_id
{
  visuals_subtab_profiles,
  visuals_subtab_world,
  visuals_subtab_hud,
  visuals_subtab_models
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
  if (pup_menu::combo("Item", &selected_item, option_labels.data(), static_cast<int>(option_labels.size())) &&
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
  if (pup_menu::combo("War paint", &selected_paintkit, paintkit_labels.data(), static_cast<int>(paintkit_labels.size())) &&
      selected_paintkit >= 0 && static_cast<std::size_t>(selected_paintkit) < paintkits.size()) {
    slot.paintkit = paintkits[static_cast<std::size_t>(selected_paintkit)].definition;
  }
  pup_menu::combo("Wear", &slot.wear, inventory_wear_items, IM_ARRAYSIZE(inventory_wear_items));
  pup_menu::combo("Seed", &slot.seed, inventory_seed_items, IM_ARRAYSIZE(inventory_seed_items));
  pup_menu::combo("Style", &slot.style, inventory_style_items, IM_ARRAYSIZE(inventory_style_items));
  pup_menu::combo("Sheen", &slot.sheen, inventory_sheen_items, IM_ARRAYSIZE(inventory_sheen_items));
  pup_menu::combo("Killstreak", &slot.killstreak, inventory_killstreak_items, IM_ARRAYSIZE(inventory_killstreak_items));
  const auto& effects = inventory_changer::effect_options();
  std::vector<const char*> effect_labels{};
  effect_labels.reserve(effects.size());
  int selected_effect = 0;
  for (std::size_t index = 0; index < effects.size(); ++index) {
    effect_labels.push_back(effects[index].label.c_str());
    if (effects[index].definition == static_cast<std::uint16_t>(slot.unusual)) selected_effect = static_cast<int>(index);
  }
  if (pup_menu::combo("Unusual", &selected_effect, effect_labels.data(), static_cast<int>(effect_labels.size())) &&
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
  if (pup_menu::combo(label, &selected, option_labels.data(), static_cast<int>(option_labels.size())) &&
      selected >= 0 && static_cast<std::size_t>(selected) < options.size()) {
    *definition = options[static_cast<std::size_t>(selected)].definition;
  }
}
#endif

inline bool render_material_layers(const char* title, std::vector<chams_layer>& layers) {
  bool changed = false;
  bool registry_changed = false;
  ImGui::PushID(title);
  ImGui::TextUnformatted(title);
  const std::vector<std::string> names = materials.selectable_names();
  static int selected_material = 0;
  std::vector<const char*> name_items{};
  name_items.reserve(names.size());
  for (const std::string& name : names) name_items.push_back(name.c_str());
  if (!name_items.empty()) {
    selected_material = std::clamp(selected_material, 0, static_cast<int>(name_items.size()) - 1);
    pup_menu::combo("Add material", &selected_material, name_items.data(), static_cast<int>(name_items.size()));
    if (pup_menu::accent_button("Add layer", {-1.0f, 22.0f}, false)) {
      const std::string& name = names[static_cast<std::size_t>(selected_material)];
      if (std::ranges::find_if(layers, [&name](const chams_layer& layer) { return layer.material == name; }) == layers.end()) {
        layers.push_back(chams_layer{.material = name});
        changed = true;
        registry_changed = true;
      }
    }
  }
  struct reorder_payload {
    std::vector<chams_layer>* layers;
    std::size_t index;
  };
  constexpr const char* payload_type = "puphook_chams_layer";
  const std::string layer_panel_key = pup_bind::target_path_component(title != nullptr ? title : "layers");
  std::size_t pending_source = layers.size();
  std::size_t pending_destination = layers.size();
  for (std::size_t index = 0; index < layers.size();) {
    ImGui::PushID(static_cast<int>(index));
    pup_bind::push_panel_label(layer_panel_key);
    pup_bind::push_panel_label("layer_" + pup_bind::target_path_component(layers[index].material));
    bool remove = false;
    if (ImGui::BeginChild("material_layer_card", {0.0f, 0.0f}, ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize)) {
      ImGui::Text("%s", layers[index].material.c_str());
      ImGui::SameLine();
      remove = ImGui::SmallButton("remove");
      pup_menu::color_picker("Color", &layers[index].color);
      pup_menu::slider_float("Start distance", &layers[index].start, 0.0f, 2048.0f, "%.0f HU");
      pup_menu::slider_float("End distance", &layers[index].end, 512.0f, 8192.0f, "%.0f HU");
      if (layers[index].end < layers[index].start) layers[index].end = layers[index].start;
      pup_menu::checkbox("Fade with distance", &layers[index].smooth_alpha);
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
    pup_bind::pop_panel_label();
    pup_bind::pop_panel_label();
    if (remove) {
      layers.erase(layers.begin() + static_cast<std::ptrdiff_t>(index));
      changed = true;
      registry_changed = true;
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
    registry_changed = true;
  }
  if (registry_changed) pup_bind::clear_registered_targets();
  if (layers.empty()) ImGui::TextDisabled("No materials configured.");
  ImGui::PopID();
  return changed;
}

enum automation_subtab_id
{
  automation_subtab_general,
  automation_subtab_queue,
  // automation_subtab_inventory_changer, // Temporarily disabled.
  automation_subtab_chat,
  automation_subtab_items,
  automation_subtab_navbot,
  automation_subtab_medic,
  automation_subtab_ipc
};

inline ImVec4 with_alpha(ImVec4 color, float alpha) {
  color.w *= alpha;
  return color;
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
  return "/opt/puphook/assets/fonts";
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
          puphook::core::log_raw("failed to load menu font: %s\n", custom_font_path_string.c_str());
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

inline bool icon_button(const char* icon, const ImVec2& size = ImVec2(22.0f, 22.0f)) {
  ImFont* icons = font_icons();
  if (icons != nullptr) ImGui::PushFont(icons);
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.10f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.18f));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
  const bool pressed = ImGui::Button(icon, size);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(3);
  if (icons != nullptr) ImGui::PopFont();
  return pressed;
}

inline std::string truncate_ui_text(std::string text, const float max_width) {
  if (ImGui::CalcTextSize(text.c_str()).x <= max_width) return text;
  while (!text.empty() && ImGui::CalcTextSize((text + "...").c_str()).x > max_width) text.pop_back();
  return text.empty() ? "..." : text + "...";
}

inline bool begin_panel(const char* name, const ImVec2& size, ImGuiWindowFlags window_flags = 0) {
  current_panel_layout_stack().push_back({ .header_height = ImGui::GetTextLineHeight() + scaled(8.0f) });
  mono::begin_panel(name, size, window_flags);
  return !ImGui::GetCurrentWindow()->SkipItems;
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
  const bool panel_visible = begin_panel(name, ImVec2(state.column_width, panel_height),
    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  pup_bind::push_panel_label(name != nullptr ? name : "");
  if (panel_visible || pup_bind::registering_targets()) {
    draw_fn();
  }
  pup_bind::pop_panel_label();
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

inline bool checkbox(const char* label, bool* value) {
  if (label == nullptr || value == nullptr) return false;
  if (pup_bind::registering_targets()) {
    pup_bind::bindable_checkbox(label, value, false, false);
    return false;
  }
  const bool changed = mono::toggle(label, value);
  pup_bind::bindable_checkbox(label, value, changed, mono::last_item_interaction().hovered);
  return changed;
}

inline bool combo(const char* label, int* value, const char* const items[], const int item_count) {
  if (label == nullptr || value == nullptr || items == nullptr || item_count <= 0) return false;

  if (pup_bind::registering_targets()) {
    pup_bind::bindable_combo_int(label, value, false, items, item_count, false);
    return false;
  }

  std::vector<std::pair<std::string, int>> mono_items;
  mono_items.reserve(static_cast<size_t>(item_count));
  for (int index = 0; index < item_count; ++index) {
    mono_items.emplace_back(items[index] != nullptr ? items[index] : "", index);
  }
  const bool mono_changed = mono::select_single(label, value, mono_items);
  pup_bind::bindable_combo_int(label, value, mono_changed, items, item_count, mono::last_item_interaction().hovered);
  return mono_changed;
}

inline bool bools_combo(const char* label, std::initializer_list<std::pair<const char*, bool*>> items) {
  if (label == nullptr || items.size() == 0) return false;
  std::vector<std::pair<std::string, bool*>> mono_items;
  mono_items.reserve(items.size());
  for (const auto& item : items) {
    if (item.first == nullptr || item.second == nullptr) continue;
    pup_bind::bindable_checkbox(item.first, item.second, false, false);
    if (!pup_bind::registering_targets()) mono_items.emplace_back(item.first, item.second);
  }
  if (pup_bind::registering_targets() || mono_items.empty()) return false;
  return mono::select_multi(label, mono_items);
}

inline bool multi_select_combo(const char* label, uint32_t* value_mask, const char* const items[], const uint32_t item_bits[], const int item_count) {
  if (label == nullptr || value_mask == nullptr || items == nullptr || item_bits == nullptr || item_count <= 0) return false;

  if (pup_bind::registering_targets()) {
    pup_bind::multi_select_target(value_mask, label, false, false, items, item_bits, item_count);
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
  pup_bind::multi_select_target(value_mask, label, mono_changed, mono::last_item_interaction().hovered, items, item_bits, item_count);
  pup_bind::maybe_open_popup(value_mask, label, mono::last_item_interaction().hovered);
  return mono_changed;
}

inline bool mask_checkbox(const char* label, uint32_t* value_mask, const uint32_t bit) {
  if (label == nullptr || value_mask == nullptr || bit == 0) return false;

  if (pup_bind::registering_targets()) {
    pup_bind::multi_select_target(value_mask, label, false, false, nullptr, nullptr, 0);
    return false;
  }

  bool enabled = (*value_mask & bit) != 0;
  const bool changed = mono::toggle(label, &enabled);
  if (changed) {
    if (enabled) *value_mask |= bit;
    else *value_mask &= ~bit;
  }
  pup_bind::multi_select_target(value_mask, label, changed, mono::last_item_interaction().hovered);
  pup_bind::maybe_open_popup(value_mask, label, mono::last_item_interaction().hovered);
  return changed;
}

inline bool slider_float(const char* label, float* value, float minimum, float maximum, const char* format) {
  if (label == nullptr || value == nullptr) return false;
  if (pup_bind::registering_targets()) {
    pup_bind::bindable_slider_float(label, value, false, minimum, maximum, format, false);
    return false;
  }
  const bool mono_changed = mono::slider_float(label, value, minimum, maximum, format);
  pup_bind::bindable_slider_float(label, value, mono_changed, minimum, maximum, format, mono::last_item_interaction().hovered);
  return mono_changed;
}

inline bool slider_int(const char* label, int* value, int minimum, int maximum, const char* format = "%d") {
  if (label == nullptr || value == nullptr) return false;
  if (pup_bind::registering_targets()) {
    pup_bind::bindable_slider_int(label, value, false, minimum, maximum, format, false);
    return false;
  }
  const bool mono_changed = mono::slider_int(label, value, minimum, maximum, format);
  pup_bind::bindable_slider_int(label, value, mono_changed, minimum, maximum, format, mono::last_item_interaction().hovered);
  return mono_changed;
}

inline bool input_text(const char* label, std::string* value) {
  if (label == nullptr || value == nullptr) return false;

  if (pup_bind::registering_targets()) {
    pup_bind::bindable_string(label, value, false, false);
    return false;
  }

  const bool mono_changed = mono::input_string(label, value);
  pup_bind::bindable_string(label, value, mono_changed, mono::last_item_interaction().hovered);
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

  if (pup_bind::registering_targets()) {
    pup_bind::bindable_color(label, color, false, false);
    return false;
  }

  ImGui::PushID(color);
  mono::rgba8 value{
    static_cast<uint8_t>(std::clamp(color->r * 255.0f, 0.0f, 255.0f)),
    static_cast<uint8_t>(std::clamp(color->g * 255.0f, 0.0f, 255.0f)),
    static_cast<uint8_t>(std::clamp(color->b * 255.0f, 0.0f, 255.0f)),
    static_cast<uint8_t>(std::clamp(color->a * 255.0f, 0.0f, 255.0f))
  };
  const bool changed = mono::color_picker(label, &value, &color->rainbow);
  if (changed) {
    color->r = value.r / 255.0f;
    color->g = value.g / 255.0f;
    color->b = value.b / 255.0f;
    color->a = value.a / 255.0f;
  }
  pup_bind::bindable_color(label, color, changed, mono::last_item_interaction().hovered);
  ImGui::PopID();
  return changed;
}

}

static void get_input(SDL_Event* event) {
  pup_bind::handle_input(event);
}

static void set_imgui_theme(void) {
  pup_menu::ensure_fonts();
  mono::apply_layout();
  const ImVec4 accent = pup_menu::menu_accent();
  mono::apply_colors({accent.x, accent.y, accent.z, accent.w});
}

static const char* puphook_watermark_version() {
#if defined(GIT_COMMIT_HASH) && defined(GIT_COMMITTER_DATE)

  return "Version: #" GIT_COMMIT_HASH " " GIT_COMMITTER_DATE;
#else

  return "Unknown Version";
#endif

}

static const char* puphook_watermark_type() {
#if defined(PUPHOOK_TEXTMODE) && PUPHOOK_TEXTMODE

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
    { "puphook by pupnoodle", accent },
    { puphook_watermark_version(), text },
    { puphook_watermark_type(), text },
    { "Press INSERT OR F11 to open/close menu and Player Manager.", text },
    { "Use mouse to navigate in menu.", text }
  };
  mono::corner_text(lines, { 8.0f, 8.0f }, pup_menu::font_regular());
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
  const char* projectile_modifier_items[] = {
    "Charge weapon", "Cancel charge", "Target dormant", "Lob angles"
  };
  const uint32_t projectile_modifier_bits[] = {
    Aim::projectile_mod_charge_weapon,
    Aim::projectile_mod_cancel_charge,
    Aim::projectile_mod_target_dormant,
    Aim::projectile_mod_lob_angles
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
    "Headshot only",
    "Target dormant"
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
    Aim::hitscan_mod_headshot_only,
    Aim::hitscan_mod_target_dormant
  };

  pup_menu::begin_flow_layout("aimbot_layout", 2);
  pup_menu::flow_panel("Aimbot", 0, 186.0f, [&]() {
    pup_menu::checkbox("Enable", &config.aimbot.master);
    pup_menu::bools_combo("Options", {
      { "Auto shoot", &config.aimbot.auto_shoot },
      { "Shoot through glass", &config.aimbot.shoot_through_glass },
      { "Spread compensation", &config.aimbot.spread_compensation },
      { "Target dormant", &config.aimbot.target_dormant },
      { "Auto resolver", &config.aimbot.resolver }
    });
    pup_menu::combo("Aim mode", (int*)&config.aimbot.aim_mode, aim_mode_items, IM_ARRAYSIZE(aim_mode_items));
    pup_menu::slider_float("Aim FOV", &config.aimbot.fov, 0.0f, 180.0f, "%.0f deg");
    pup_menu::slider_float("Assist strength", &config.aimbot.assist_strength, 0.0f, 100.0f, "%.0f%%");
    pup_menu::slider_int("Resolver yaws", &config.aimbot.resolver_max_yaws, 4, 24);
  });
  pup_menu::flow_panel("Crit Hack", 0, 88.0f, [&]() {
    pup_menu::checkbox("Enable Crit Hack", &config.crithack.enabled);
    pup_menu::bools_combo("Options", {
      { "Force crits", &config.crithack.force_crits },
      { "Always melee crit", &config.crithack.always_melee },
      { "Avoid random crits", &config.crithack.avoid_random }
    });
  });
  pup_menu::flow_panel("Target selection", 1, 254.0f, [&]() {
    pup_menu::combo("Target", (int*)&config.aimbot.target_type, target_items, IM_ARRAYSIZE(target_items));
    pup_menu::multi_select_combo("Aim at", &config.aimbot.aim_at, aim_at_items, aim_at_bits, IM_ARRAYSIZE(aim_at_items));
    pup_menu::multi_select_combo("Hitscan hitboxes", &config.aimbot.hitscan_hitboxes, hitbox_items, hitbox_bits, IM_ARRAYSIZE(hitbox_items));
    pup_menu::multi_select_combo("Melee hitboxes", &config.aimbot.melee_hitboxes, hitbox_items, hitbox_bits, IM_ARRAYSIZE(hitbox_items));
    pup_menu::checkbox("Melee walk to target", &config.aimbot.melee_walk_to_target);
    pup_menu::multi_select_combo("Ignore", &config.aimbot.ignore, ignore_items, ignore_bits, IM_ARRAYSIZE(ignore_items));
    pup_menu::slider_float("Invisible threshold", &config.aimbot.ignore_invisible, 0.0f, 100.0f, "%.0f%%");
    pup_menu::slider_int("Unsimulated ticks", &config.aimbot.ignore_unsimulated_ticks, 0, 21);
    pup_menu::slider_int("Max targets", &config.aimbot.max_targets, 1, 6);
  });
  pup_menu::flow_panel("Hitscan", 0, 188.0f, [&]() {
    pup_menu::multi_select_combo("Modifiers", &config.aimbot.hitscan_modifiers, hitscan_modifier_items, hitscan_modifier_bits, IM_ARRAYSIZE(hitscan_modifier_items));
    pup_menu::slider_float("Tapfire distance", &config.aimbot.tapfire_distance, 250.0f, 2000.0f, "%.0f HU");
    pup_menu::slider_int("Peek ticks", &config.aimbot.peek_ticks, 0, 8);
    pup_menu::slider_float("Multipoint scale", &config.aimbot.multipoint_scale, 0.0f, 100.0f, "%.0f%%");
    pup_menu::slider_float("Bone size subtract", &config.aimbot.bone_size_subtract, 0.0f, 12.0f, "%.1f HU");
    pup_menu::slider_float("Bone size min scale", &config.aimbot.bone_size_min_scale, 0.05f, 1.0f, "%.2f");
  });
  pup_menu::flow_panel("Projectile", 0, 320.0f, [&]() {
    pup_menu::checkbox("Enable", &config.aimbot.projectile_active);
    pup_menu::multi_select_combo("Modifiers", &config.aimbot.projectile_modifiers,
      projectile_modifier_items, projectile_modifier_bits, IM_ARRAYSIZE(projectile_modifier_items));
    pup_menu::combo("Target mode", &config.aimbot.projectile_mode, projectile_mode_items, IM_ARRAYSIZE(projectile_mode_items));
    pup_menu::combo("Prediction", (int*)&config.aimbot.projectile_prediction_mode,
      projectile_prediction_items, IM_ARRAYSIZE(projectile_prediction_items));
    pup_menu::combo("Aim position", &config.aimbot.projectile_aim_pos,
      projectile_position_items, IM_ARRAYSIZE(projectile_position_items));
    pup_menu::combo("Splash policy", &config.aimbot.projectile_splash_policy,
      projectile_splash_items, IM_ARRAYSIZE(projectile_splash_items));
    pup_menu::slider_float("FOV", &config.aimbot.projectile_fov, 0.0f, 180.0f, "%.0f deg");
    pup_menu::slider_int("Max simulation targets", &config.aimbot.projectile_max_sim_targets, 1, 6);
    pup_menu::slider_float("Max simulation time", &config.aimbot.projectile_max_sim_time, 0.25f, 5.0f, "%.2fs");
    pup_menu::slider_int("Multipoint scale", &config.aimbot.projectile_multipoint_scale, 50, 100);
    pup_menu::checkbox("Smooth flamethrowers", &config.aimbot.projectile_smooth_flamethrowers_active);
    pup_menu::slider_float("Flamethrower smooth", &config.aimbot.projectile_smooth_flamethrowers, 1.0f, 100.0f, "%.0f%%");
  });
  pup_menu::end_flow_layout();
}

static void draw_aimbot_draw_content() {
  pup_menu::begin_flow_layout("aimbot_draw_layout", 2);
  pup_menu::flow_panel("FOV circle", 0, 112.0f, [&]() {
    pup_menu::checkbox("Draw FOV", &config.aimbot.draw_fov);
  });
  pup_menu::flow_panel("Debug overlay", 1, 144.0f, [&]() {
    pup_menu::checkbox("Enable", &config.aimbot.debug_overlay);
    pup_menu::slider_float("Position X", &config.aimbot.debug_overlay_x, 0.0f, 1920.0f, "%.0f px");
    pup_menu::slider_float("Position Y", &config.aimbot.debug_overlay_y, 0.0f, 1080.0f, "%.0f px");
  });
  pup_menu::end_flow_layout();
}

static void draw_combat_weapons_content() {
  pup_menu::begin_flow_layout("combat_weapons_layout", 2);
  pup_menu::flow_panel("Sniper", 0, 100.0f, [&]() {
    pup_menu::bools_combo("Scope", {
      { "Automatic scope", &config.aimbot.sniper_auto_scope },
      { "Automatic unscope", &config.aimbot.sniper_auto_unscope }
    });
    pup_menu::slider_float("Scope distance", &config.aimbot.sniper_scope_distance, 250.0f, 4000.0f, "%.0f HU");
    pup_menu::slider_float("Scope cancel delay", &config.aimbot.sniper_scope_cancel_time, 1.0f, 5.0f, "%.1f s");
  });
  pup_menu::flow_panel("Melee", 0, 156.0f, [&]() {
    pup_menu::bools_combo("Options", {
      { "Auto backstab", &config.aimbot.melee_auto_backstab },
      { "Ignore razorback", &config.aimbot.melee_ignore_razorback },
      { "Whip teammates", &config.aimbot.melee_whip_team },
      { "Swing prediction", &config.aimbot.melee_swing_prediction },
      { "Predict lag", &config.aimbot.melee_swing_predict_lag }
    });
    static const char* backstab_ping_items[] = { "Off", "Account ping", "Account ping + confirm" };
    pup_menu::combo("Backstab ping", &config.aimbot.melee_backstab_ping_mode,
      backstab_ping_items, IM_ARRAYSIZE(backstab_ping_items));
    pup_menu::slider_int("Swing ticks", &config.aimbot.melee_swing_ticks, 0, 14);
    static const char* swing_validate_items[] = { "Both", "Swing", "Simulated" };
    pup_menu::combo("Swing validation", &config.aimbot.melee_swing_validate_mode,
      swing_validate_items, IM_ARRAYSIZE(swing_validate_items));
  });
  pup_menu::flow_panel("Heavy", 1, 80.0f, [&]() {
    pup_menu::bools_combo("Rev", {
      { "Heavy auto rev", &config.aimbot.auto_rev },
      { "Heavy auto unrev", &config.aimbot.auto_unrev }
    });
    pup_menu::slider_float("Heavy rev threshold", &config.aimbot.auto_rev_threshold, 200.0f, 1200.0f, "%.0f HU");
  });
  pup_menu::flow_panel("Auto detonate", 1, 116.0f, [&]() {
    pup_menu::bools_combo("Detonate", {
      { "Sticky detonation", &config.auto_detonate.stickies },
      { "Flare airburst", &config.auto_detonate.flares },
      { "Target buildings", &config.auto_detonate.buildings },
      { "Ignore cloaked", &config.auto_detonate.ignore_cloaked },
      { "Don't blow me up", &config.auto_detonate.dont_blow_me_up }
    });
    pup_menu::slider_float("Sticky radius", &config.auto_detonate.sticky_radius, 40.0f, 400.0f, "%.0f HU");
    pup_menu::slider_float("Flare radius", &config.auto_detonate.flare_radius, 40.0f, 400.0f, "%.0f HU");
  });
  pup_menu::flow_panel("Auto reflect", 1, 116.0f, [&]() {
    pup_menu::checkbox("Enable", &config.auto_reflect.enabled);
    pup_menu::bools_combo("Projectiles", {
      { "Rockets", &config.auto_reflect.rockets },
      { "Sentry rockets", &config.auto_reflect.sentry_rockets },
      { "Pipes", &config.auto_reflect.pipes },
      { "Stickies", &config.auto_reflect.stickies },
      { "Flares", &config.auto_reflect.flares },
      { "Arrows", &config.auto_reflect.arrows },
      { "Burning teammates", &config.auto_reflect.burning_teammates }
    });
    pup_menu::slider_float("Range", &config.auto_reflect.range, 40.0f, 400.0f, "%.0f HU");
    pup_menu::slider_float("FOV limit", &config.auto_reflect.fov_limit, 0.0f, 180.0f, "%.0f deg");
  });
  pup_menu::end_flow_layout();
}

static void draw_medic_content();

static void draw_combat_tab(const int combat_subtab) {
  switch (combat_subtab) {
    case pup_menu::combat_subtab_aimbot:
      draw_aimbot_content();
      break;
    case pup_menu::combat_subtab_weapons:
      draw_combat_weapons_content();
      break;
    case pup_menu::combat_subtab_overlay:
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
    preview += puphook::core::players::role_name(role);
  }

  if (!ImGui::BeginCombo("Roles", preview.c_str())) return;
  for (const auto& definition : puphook::core::players::role_definitions())
  {
    if (definition.id == puphook::core::players::default_role) continue;
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
}

static void draw_visual_groups_content_tfwin() {
  visual_groups::ensure_defaults();

  int& selected_group = pup_menu::selected_visual_group();
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
    "Enemy", "Team", "BLU", "RED", "Local", "Friends", "Party", "Priority", "Target", "Dormant", "PUP", "Ignored"
  };
  static const uint32_t condition_bits[] = {
    visual_group::condition_enemy, visual_group::condition_team, visual_group::condition_blu, visual_group::condition_red,
    visual_group::condition_local, visual_group::condition_friends, visual_group::condition_party, visual_group::condition_priority,
    visual_group::condition_target, visual_group::condition_dormant, visual_group::condition_pup, visual_group::condition_ignored
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
  const float profile_width = std::clamp(available_width * 0.34f, pup_menu::scaled(190.0f), pup_menu::scaled(240.0f));
  const float gap = pup_menu::scaled(pup_menu::k_gap);
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

  ImGui::BeginChild("visual_group_manager_tfwin", { profile_width, 0.0f }, ImGuiChildFlags_Border,
    ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::TextUnformatted("Profiles");
  mono::group_separator();
  pup_menu::input_text("New profile", &new_group_name);
  const float content_width = ImMax(1.0f, ImGui::GetContentRegionAvail().x);
  const float item_gap = ImGui::GetStyle().ItemSpacing.x;
  const float half_width = ImMax(1.0f, (content_width - item_gap) * 0.5f);
  if (pup_menu::accent_button("New", { half_width, 22.0f }) &&
      config.visual_groups.groups.size() < visual_group_config::max_groups) {
    visual_group group{};
    group.bind_id = visual_groups::allocate_group_id();
    group.name = new_group_name.empty() ? "New profile" : new_group_name;
    group.targets = visual_group::target_players;
    config.visual_groups.groups.emplace_back(std::move(group));
    selected_group = static_cast<int>(config.visual_groups.groups.size()) - 1;
    config.visual_groups.active_group_mask |= group_active_bit(selected_group);
    pup_bind::clear_registered_targets();
  }
  ImGui::SameLine(0.0f, item_gap);
  if (pup_menu::accent_button("Duplicate", { half_width, 22.0f }) &&
      selected_group >= 0 && selected_group < static_cast<int>(config.visual_groups.groups.size()) &&
      config.visual_groups.groups.size() < visual_group_config::max_groups) {
    visual_group group = config.visual_groups.groups[static_cast<std::size_t>(selected_group)];
    group.bind_id = visual_groups::allocate_group_id();
    group.name += " copy";
    config.visual_groups.groups.emplace_back(std::move(group));
    selected_group = static_cast<int>(config.visual_groups.groups.size()) - 1;
    config.visual_groups.active_group_mask |= group_active_bit(selected_group);
    pup_bind::clear_registered_targets();
  }
  const float action_width = ImMax(1.0f, (content_width - item_gap * 2.0f) / 3.0f);
  if (pup_menu::accent_button("Delete", { action_width, pup_menu::k_button_height }, true)) {
    delete_visual_group(selected_group, &selected_group);
    pup_bind::clear_registered_targets();
  }
  ImGui::SameLine(0.0f, item_gap);
  if (pup_menu::accent_button("Up", { action_width, pup_menu::k_button_height }) && selected_group > 0) {
    visual_groups::move_group(selected_group, selected_group - 1);
    pup_bind::clear_registered_targets();
    --selected_group;
  }
  ImGui::SameLine(0.0f, item_gap);
  if (pup_menu::accent_button("Down", { action_width, pup_menu::k_button_height }) &&
      selected_group + 1 < static_cast<int>(config.visual_groups.groups.size())) {
    visual_groups::move_group(selected_group, selected_group + 1);
    pup_bind::clear_registered_targets();
    ++selected_group;
  }
  rebuild_active_profiles();
  if (active_count > 0) {
    pup_menu::multi_select_combo("Active profiles", &config.visual_groups.active_group_mask,
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
    if (pup_menu::list_row(label.c_str(), selected_group == index, { 0.0f, 28.0f })) {
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
      pup_bind::clear_registered_targets();
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
  pup_bind::push_panel_label("group_" + std::to_string(group.bind_id));

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
    draw_panel_header("Profile");
    pup_menu::input_text("Name", &group.name);
    pup_menu::color_picker("Profile color", &group.color);
    pup_menu::checkbox("Tags use profile color", &group.tags_override_color);
  }
  end_panel();

  if (ImGui::BeginChild("visual_targets_panel", { 0.0f, 0.0f }, inspector_panel_flags)) {
    draw_panel_header("Targets");
    pup_menu::multi_select_combo("Entity target types", &group.targets, target_items, target_bits, IM_ARRAYSIZE(target_items));
  }
  end_panel();

  if (ImGui::BeginChild("visual_conditions_panel", { 0.0f, 0.0f }, inspector_panel_flags)) {
    draw_panel_header("Conditions");
    pup_menu::multi_select_combo("Conditions", &group.conditions, condition_items, condition_bits, IM_ARRAYSIZE(condition_items));
    draw_visual_group_roles(group.roles);
    pup_menu::multi_select_combo("Player filters", &group.players, player_items, player_bits, IM_ARRAYSIZE(player_items));
    pup_menu::multi_select_combo("Building filters", &group.buildings, building_items, building_bits, IM_ARRAYSIZE(building_items));
    pup_menu::multi_select_combo("Projectile filters", &group.projectiles, projectile_items, projectile_bits, IM_ARRAYSIZE(projectile_items));
  }
  end_panel();

  if (ImGui::BeginChild("visual_esp_panel", { 0.0f, 0.0f }, inspector_panel_flags)) {
    draw_panel_header("ESP");
    pup_menu::multi_select_combo("Draw", &group.esp.draw_mask, esp_items, esp_bits, IM_ARRAYSIZE(esp_items));
    pup_menu::mask_checkbox("Emoji head", &group.esp.draw_mask, group_esp_settings::head_emoji);
    pup_menu::mask_checkbox("Mafia level", &group.esp.draw_mask, group_esp_settings::mafia_level);
    pup_menu::checkbox("Override color", &group.esp.override_color);
    pup_menu::color_picker("ESP color", &group.esp.color);
    pup_menu::combo("Box type", (int*)&group.esp.box_style, box_type_items, IM_ARRAYSIZE(box_type_items));
    pup_menu::slider_float("Start drawing", &group.esp.start, 0.0f, 8192.0f, "%.0f HU");
    pup_menu::slider_float("End drawing", &group.esp.end, 0.0f, 8192.0f, "%.0f HU");
    if (group.esp.end < group.esp.start) group.esp.end = group.esp.start;
    pup_menu::checkbox("Draw fade", &group.esp.smooth_alpha);
    int background_alpha = group.esp.background_alpha;
    pup_menu::slider_int("Background alpha", &background_alpha, 0, 255);
    group.esp.background_alpha = static_cast<uint8_t>(std::clamp(background_alpha, 0, 255));
    pup_menu::slider_float("Class icon scale", &group.esp.class_icon_scale, 0.5f, 5.0f, "%.1f");
    pup_menu::slider_float("Emoji scale", &group.esp.head_emoji_scale, 0.5f, 5.0f, "%.1f");
    pup_menu::combo("Emoji style", &group.esp.head_emoji_style, head_emoji_items, IM_ARRAYSIZE(head_emoji_items));
    pup_menu::combo("Mafia position", (int*)&group.esp.mafia_level_position, mafia_position_items, IM_ARRAYSIZE(mafia_position_items));
  }
  end_panel();

  materials.prepare();
  if (ImGui::BeginChild("visual_chams_visible_panel", { 0.0f, 0.0f }, inspector_panel_flags)) {
    draw_panel_header("Visible chams");
    pup_menu::render_material_layers("Visible layers", group.chams.visible);
  }
  end_panel();

  if (ImGui::BeginChild("visual_glow_panel", { 0.0f, 0.0f }, inspector_panel_flags)) {
    draw_panel_header("Glow");
    pup_menu::color_picker("Color", &group.glow.color);
    pup_menu::slider_float("Stencil scale", &group.glow.stencil, 0.0f, 10.0f, "%.1f");
    group.glow.stencil = std::clamp(std::round(group.glow.stencil * 10.0f) / 10.0f, 0.0f, 10.0f);
    pup_menu::slider_float("Blur scale", &group.glow.blur, 0.0f, 100.0f, "%.1f");
    pup_menu::slider_float("Render start", &group.glow.start, 0.0f, 2048.0f, "%.0f HU");
    pup_menu::slider_float("Render end", &group.glow.end, 512.0f, 8192.0f, "%.0f HU");
    if (group.glow.end < group.glow.start) group.glow.end = group.glow.start;
    pup_menu::checkbox("Distance to alpha", &group.glow.smooth_alpha);
  }
  end_panel();

  if (ImGui::BeginChild("visual_chams_occluded_panel", { 0.0f, 0.0f }, inspector_panel_flags)) {
    draw_panel_header("Behind walls");
    pup_menu::render_material_layers("Behind walls layers", group.chams.occluded);
  }
  end_panel();

  if (ImGui::BeginChild("visual_backtrack_panel", { 0.0f, 0.0f }, inspector_panel_flags)) {
    draw_panel_header("Backtrack");
    auto& backtrack = group.backtrack_visuals;
    pup_menu::checkbox("Backtrack", &backtrack.enabled);
    static const char* backtrack_record_items[] = { "Last", "First", "All" };
    ImGui::BeginDisabled(!backtrack.enabled);
    pup_menu::combo("Records", &backtrack.record_mode, backtrack_record_items, IM_ARRAYSIZE(backtrack_record_items));
    pup_menu::checkbox("Ignore z", &backtrack.ignore_z);
    pup_menu::render_material_layers("Backtrack visible layers", backtrack.chams.visible);
    pup_menu::render_material_layers("Backtrack behind walls layers", backtrack.chams.occluded);
    ImGui::TextUnformatted("Backtrack glow");
    pup_menu::color_picker("Backtrack color", &backtrack.glow.color);
    pup_menu::slider_float("Backtrack stencil scale", &backtrack.glow.stencil, 0.0f, 10.0f, "%.1f");
    backtrack.glow.stencil = std::clamp(std::round(backtrack.glow.stencil * 10.0f) / 10.0f, 0.0f, 10.0f);
    pup_menu::slider_float("Backtrack blur scale", &backtrack.glow.blur, 0.0f, 100.0f, "%.1f");
    pup_menu::slider_float("Backtrack render start", &backtrack.glow.start, 0.0f, 2048.0f, "%.0f HU");
    pup_menu::slider_float("Backtrack render end", &backtrack.glow.end, 512.0f, 8192.0f, "%.0f HU");
    if (backtrack.glow.end < backtrack.glow.start) backtrack.glow.end = backtrack.glow.start;
    pup_menu::checkbox("Backtrack distance to alpha", &backtrack.glow.smooth_alpha);
    ImGui::EndDisabled();
  }
  end_panel();

  if (ImGui::BeginChild("visual_advanced_panel", { 0.0f, 0.0f }, inspector_panel_flags)) {
    draw_panel_header("Advanced");
    pup_menu::checkbox("Offscreen arrows", &group.offscreen_arrows);
    pup_menu::slider_int("Arrow offset", &group.offscreen_arrows_offset, 0, 500);
    pup_menu::slider_float("Arrow max distance", &group.offscreen_arrows_max_distance, 0.0f, 8192.0f, "%.0f HU");
    pup_menu::checkbox("Pickup timer", &group.pickup_timer);
    pup_menu::multi_select_combo("Trajectory", &group.trajectory, trajectory_items, trajectory_bits, IM_ARRAYSIZE(trajectory_items));
    pup_menu::multi_select_combo("Sightlines", &group.sightlines, sightline_items, sightline_bits, IM_ARRAYSIZE(sightline_items));
  }
  end_panel();

  pup_bind::pop_panel_label();
  ImGui::EndChild();
}

static void draw_visuals_world_content() {
  static const char* modulation_items[] = {"World", "Sky", "Props", "Particles", "Fog"};
  static const uint32_t modulation_bits[] = {
    Visuals::modulation_world, Visuals::modulation_sky, Visuals::modulation_prop,
    Visuals::modulation_particle, Visuals::modulation_fog
  };
  static const char* world_texture_items[] = {"Default", "Dev", "Camo", "Black", "White", "Gray"};
  static const char* projectile_trail_items[] = {
    "Default", "None", "Rocket", "Critical", "Energy", "Charged", "Fireball", "Teleport", "Fire", "Flame",
    "Sparks", "Flare", "Trail", "Health", "Smoke", "Bubbles", "Halloween", "Monoculus", "Rainbow"
  };
  static const char* beam_items[] = {"Default", "None", "Uber", "Dispenser", "Passtime", "Bombonomicon", "White", "Orange"};
  static const char* charge_items[] = {
    "Default", "None", "Electrocuted", "Halloween", "Fireball", "Teleport", "Burning", "Scorching",
    "Purple energy", "Green energy", "Nebula", "Purple stars", "Green stars", "Sunbeams", "Spellbound",
    "Purple sparks", "Yellow sparks", "Green zap", "Yellow zap", "Plasma", "Frostbite", "Purple souls",
    "Green souls", "Bubbles", "Hearts"
  };
  const auto string_combo = [](const char* label, std::string& value, const char* const* items, const int count) {
    int selected = 0;
    for (int index = 0; index < count; ++index) {
      if (value == items[index]) {
        selected = index;
        break;
      }
    }
    if (pup_menu::combo(label, &selected, items, count)) value = items[selected];
  };
  pup_menu::begin_flow_layout("visuals_world_layout", 2);
  pup_menu::flow_panel("World", 0, 324.0f, [&]() {
    pup_menu::combo("Skybox", &config.visuals.skybox_changer_index,
                    skybox_changer::option_names(), skybox_changer::option_count());
    pup_menu::bools_combo("Thirdperson", {
      { "Thirdperson", &config.visuals.thirdperson.enabled },
      { "Thirdperson crosshair", &config.visuals.thirdperson.crosshair },
      { "Thirdperson collision", &config.visuals.thirdperson.collision },
      { "Thirdperson scales", &config.visuals.thirdperson.scale }
    });
    pup_menu::slider_float("Thirdperson distance", &config.visuals.thirdperson.distance, 0.0f, 400.0f, "%.0f HU");
    pup_menu::slider_float("Thirdperson right", &config.visuals.thirdperson.right, -100.0f, 100.0f, "%.0f HU");
    pup_menu::slider_float("Thirdperson up", &config.visuals.thirdperson.up, -100.0f, 100.0f, "%.0f HU");
    pup_menu::checkbox("Override FOV", &config.visuals.override_fov);
    pup_menu::slider_float("Custom FOV", &config.visuals.custom_fov, 30.1f, 150.0f, "%.0f deg");
    pup_menu::checkbox("Override zoom FOV", &config.visuals.override_zoom_fov);
    pup_menu::slider_float("Zoom FOV", &config.visuals.custom_zoom_fov, 1.0f, 150.0f, "%.0f deg");
    pup_menu::checkbox("Override viewmodel FOV", &config.visuals.override_viewmodel_fov);
    pup_menu::slider_float("Viewmodel FOV", &config.visuals.custom_viewmodel_fov, 30.1f, 150.0f, "%.0f deg");
    pup_menu::bools_combo("ESP", {
      { "ESP lerp", &config.visuals.esp_lerp },
      { "Dormant ESP", &config.visuals.dormant_esp }
    });
  });
  pup_menu::flow_panel("Color modulation", 1, 270.0f, [&]() {
    pup_menu::multi_select_combo("Modulations", &config.visuals.world.modulation_mask,
                                 modulation_items, modulation_bits, IM_ARRAYSIZE(modulation_items));
    pup_menu::color_picker("World color", &config.visuals.world.world_color);
    pup_menu::color_picker("Sky color", &config.visuals.world.sky_color);
    pup_menu::color_picker("Prop color", &config.visuals.world.prop_color);
    pup_menu::color_picker("Particle color", &config.visuals.world.particle_color);
    pup_menu::color_picker("Fog color", &config.visuals.world.fog_color);
    pup_menu::combo("World texture", &config.visuals.world.world_texture,
                    world_texture_items, IM_ARRAYSIZE(world_texture_items));
  });
  pup_menu::flow_panel("Effects", 0, 228.0f, [&]() {
    string_combo("Projectile trail", config.visuals.effects.projectile_trail, projectile_trail_items,
                 IM_ARRAYSIZE(projectile_trail_items));
    string_combo("Medigun beam", config.visuals.effects.medigun_beam, beam_items, IM_ARRAYSIZE(beam_items));
    string_combo("Medigun charge", config.visuals.effects.medigun_charge, charge_items, IM_ARRAYSIZE(charge_items));
    pup_menu::bools_combo("Remove", {
      { "Remove screen overlays", &config.visuals.effects.remove_screen_overlays },
      { "Remove screen effects", &config.visuals.effects.remove_screen_effects }
    });
  });
  pup_menu::flow_panel("Removals", 1, 88.0f, [&]() {
    pup_menu::bools_combo("Removals", {
      { "Remove interpolation", &config.visuals.removals.interpolation },
      { "Remove lerp", &config.visuals.removals.lerp },
      { "Remove scope", &config.visuals.removals.scope },
      { "Remove zoom", &config.visuals.removals.zoom },
      { "Flat scoped sensitivity", &config.visuals.flat_zoom_sensitivity },
      { "Remove disguises", &config.visuals.removals.disguises },
      { "Remove taunts", &config.visuals.removals.taunts },
      { "Remove post-processing", &config.visuals.removals.post_processing },
      { "Remove view punch", &config.visuals.removals.view_punch },
      { "Remove angle forcing", &config.visuals.removals.angle_forcing },
      { "Remove ragdolls", &config.visuals.removals.ragdolls },
      { "Remove gibs", &config.visuals.removals.gibs }
    });
  });
  pup_menu::end_flow_layout();
}

static void draw_visuals_models_content() {
  pup_menu::begin_flow_layout("visuals_models_layout", 2);
  pup_menu::flow_panel("Skin changer", 0, 390.0f, [&]() {
    pup_menu::checkbox("Enable skin changer", &config.visuals.skin_changer.enabled);
    pup_menu::checkbox("Reskin to stock variants", &config.visuals.skin_changer.reskin);

    Player* local = entity_list != nullptr && engine != nullptr && engine->is_in_game()
      ? entity_list->get_localplayer() : nullptr;
    Weapon* weapon = local != nullptr ? local->get_weapon() : nullptr;
    const int skin_key = weapon != nullptr ? skin_changer::key(weapon->get_def_id()) : -1;
    Visuals::SkinChanger::Skin* skin = &config.visuals.skin_changer.defaults;
    Visuals::SkinChanger::Skin override_skin{};
    if (weapon != nullptr) {
      override_skin = skin_changer::get(skin_key);
      skin = &override_skin;
      ImGui::TextUnformatted(("Editing " + std::string(skin_changer::weapon_label(skin_key))).c_str());
      if (override_skin.empty()) {
        ImGui::TextUnformatted("No override; defaults apply.");
      }
    } else {
      ImGui::TextUnformatted("Editing default skins (or hold a weapon)");
    }

    std::vector<const char*> kit_names{};
    std::vector<int> kit_ids{};
    skin_changer::get_kits(weapon != nullptr ? skin_key : -1, kit_names, kit_ids);
    int selected_kit = 0;
    for (std::size_t index = 0; index < kit_ids.size(); ++index) {
      if (kit_ids[index] == skin->paintkit) selected_kit = static_cast<int>(index);
    }
    if (pup_menu::combo("Paint kit", &selected_kit, kit_names.data(), static_cast<int>(kit_names.size())) &&
        selected_kit >= 0 && static_cast<std::size_t>(selected_kit) < kit_ids.size()) {
      skin->paintkit = kit_ids[static_cast<std::size_t>(selected_kit)];
    }
    pup_menu::slider_float("Wear", &skin->wear, 0.0f, 1.0f, "%.2f");
    pup_menu::slider_int("Seed", &skin->seed, 0, 16);
    static const char* skin_quality_items[] = {
      "Auto", "Normal", "Genuine", "Vintage", "Unusual", "Unique", "Strange", "Haunted", "Collector's", "Decorated"
    };
    static const int skin_quality_ids[] = { -1, 0, 1, 3, 5, 6, 11, 13, 14, 15 };
    int selected_quality = 0;
    for (int index = 0; index < IM_ARRAYSIZE(skin_quality_ids); ++index) {
      if (skin_quality_ids[index] == skin->quality) selected_quality = index;
    }
    if (pup_menu::combo("Quality", &selected_quality, skin_quality_items, IM_ARRAYSIZE(skin_quality_items)) &&
        selected_quality >= 0 && selected_quality < IM_ARRAYSIZE(skin_quality_ids)) {
      skin->quality = skin_quality_ids[selected_quality];
    }
    pup_menu::checkbox("Festive", &skin->festive);
    pup_menu::checkbox("Australium", &skin->australium);
    static const char* skin_killstreak_items[] = { "None", "Basic", "Specialized", "Professional" };
    pup_menu::combo("Killstreak", &skin->killstreak, skin_killstreak_items, IM_ARRAYSIZE(skin_killstreak_items));
    static const char* skin_sheen_items[] = {
      "Off", "Team shine", "Deadly daffodil", "Manndarin", "Mean green",
      "Agonizing emerald", "Villainous violet", "Hot rod"
    };
    pup_menu::combo("Sheen", &skin->sheen, skin_sheen_items, IM_ARRAYSIZE(skin_sheen_items));
    static const char* skin_unusual_items[] = {"None", "Hot", "Isotope", "Cool", "Energy Orb"};
    pup_menu::combo("Weapon unusual", &skin->unusual, skin_unusual_items, IM_ARRAYSIZE(skin_unusual_items));
    if (weapon != nullptr) {
      skin_changer::set(skin_key, override_skin);
    }
  });
  pup_menu::end_flow_layout();
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

  pup_menu::begin_flow_layout("visuals_ui_layout", 2);
  pup_menu::flow_panel("Indicators", 0, 228.0f, [&]() {
    pup_menu::multi_select_combo("Visible widgets", &config.visuals.indicators.enabled_mask, indicator_items, indicator_bits, IM_ARRAYSIZE(indicator_items));
    pup_menu::bools_combo("Spectators", {
      { "Show spectator target", &config.visuals.spectator_list.show_target },
      { "Show spectator modes", &config.visuals.spectator_list.show_modes },
      { "Highlight firstperson", &config.visuals.spectator_list.highlight_firstperson }
    });
    pup_menu::color_picker("Firstperson color", &config.visuals.spectator_list.firstperson_color);
  });
  pup_menu::flow_panel("Feedback", 1, 188.0f, [&]() {
    pup_menu::checkbox("Hitmarker", &config.visuals.hitmarker.enabled);
    pup_menu::checkbox("Damage text", &config.visuals.hitmarker.damage_text);
    pup_menu::slider_float("Hitmarker duration", &config.visuals.hitmarker.duration, 0.20f, 1.50f, "%.2f s");
    pup_menu::slider_float("Hitmarker size", &config.visuals.hitmarker.size, 4.0f, 16.0f, "%.1f px");
    pup_menu::color_picker("Hitmarker color", &config.visuals.hitmarker.color);
    pup_menu::color_picker("Crit color", &config.visuals.hitmarker.crit_color);
    pup_menu::color_picker("Headshot color", &config.visuals.hitmarker.headshot_color);
  });
  pup_menu::flow_panel("Casual medal", 0, 124.0f, [&]() {
    pup_menu::checkbox("Guaranteed flip", &config.visuals.casual_medal.guaranteed_flip);
    pup_menu::checkbox("Change displayed rank", &config.visuals.casual_medal.changer);
    pup_menu::slider_int("Displayed rank", &config.visuals.casual_medal.rank, 1, 1200);
  });
  pup_menu::flow_panel("Radar", 1, 356.0f, [&]() {
    pup_menu::checkbox("Enable radar", &config.visuals.radar.enabled);
    pup_menu::slider_float("Radar X", &config.visuals.radar.x, 0.0f, 1920.0f, "%.0f px");
    pup_menu::slider_float("Radar Y", &config.visuals.radar.y, 0.0f, 1080.0f, "%.0f px");
    pup_menu::slider_int("Radar size", &config.visuals.radar.size, 100, 600);
    pup_menu::slider_float("Radar zoom", &config.visuals.radar.zoom, 5.0f, 50.0f, "%.1f");
    pup_menu::slider_int("Icon size", &config.visuals.radar.icon_size, 10, 40);
    pup_menu::checkbox("Class icons", &config.visuals.radar.use_icons);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Draw class icons from assets/textures/atlas.png.\nFalls back to plain dots when the class is unknown.");
    }
    pup_menu::checkbox("Axis lines", &config.visuals.radar.axis_lines);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Full-span horizontal and vertical lines through the centre.\nThe short centre cross is always drawn.");
    }
    pup_menu::slider_int("Range rings", &config.visuals.radar.range_rings, 0, 8);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Evenly spaced distance rings, 0 to turn them off.\nSpacing is in radar pixels, so the rings stay put while zoom\nchanges what distance they represent.");
    }
    pup_menu::bools_combo("Players", {
      { "Show teammates", &config.visuals.radar.show_teammates },
      { "Show enemies", &config.visuals.radar.show_enemies }
    });
  });
  pup_menu::end_flow_layout();
}

static void draw_navbot_content();

static void draw_visuals_tab(const int visuals_subtab) {
  switch (visuals_subtab) {
    case pup_menu::visuals_subtab_profiles:
      draw_visual_groups_content();
      break;
    case pup_menu::visuals_subtab_world:
      draw_visuals_world_content();
      break;
    case pup_menu::visuals_subtab_hud:
      draw_visuals_ui_content();
      break;
    case pup_menu::visuals_subtab_models:
      draw_visuals_models_content();
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

  pup_menu::begin_flow_layout("movement_layout", 2);
  pup_menu::flow_panel("Movement", 0, 220.0f, [&]() {
    pup_menu::checkbox("Bhop", &config.misc.movement.bhop);
    pup_menu::combo("Auto strafe", (int*)&config.misc.movement.auto_strafe, auto_strafe_items, IM_ARRAYSIZE(auto_strafe_items));
    pup_menu::slider_float("Strafe turn scale", &config.misc.movement.auto_strafe_turn_scale, 0.0f, 1.0f, "%.2f");
    pup_menu::slider_float("Strafe max delta", &config.misc.movement.auto_strafe_max_delta, 0.0f, 180.0f, "%.0f deg");
    pup_menu::bools_combo("Jumps", {
      { "Edge jump", &config.misc.movement.edge_jump },
      { "Jumpbug", &config.misc.movement.jumpbug },
      { "Duck jump", &config.misc.movement.duck_jump },
      { "Break jump", &config.misc.movement.break_jump },
      { "Auto reverse jump", &config.misc.movement.auto_reverse_jump }
    });
  });
  pup_menu::flow_panel("Movement extras", 1, 220.0f, [&]() {
    pup_menu::bools_combo("Assist", {
      { "Fast stop", &config.misc.movement.fast_stop },
      { "Fast accelerate", &config.misc.movement.fast_accelerate },
      { "No push", &config.misc.movement.no_push },
      { "Taunt slide", &config.misc.movement.taunt_slide }
    });
    pup_menu::combo("Auto edgebug", (int*)&config.misc.movement.auto_edgebug, auto_edgebug_items, IM_ARRAYSIZE(auto_edgebug_items));
    pup_menu::bools_combo("Moonwalk", {
      { "Moonwalk", &config.misc.movement.moonwalk },
      { "Moonwalk forward", &config.misc.movement.moonwalk_forward },
      { "Moonwalk navbot compat", &config.misc.movement.moonwalk_navbot_compat }
    });
  });
  pup_menu::end_flow_layout();
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

  pup_menu::begin_flow_layout("navbot_layout", 2);
  pup_menu::flow_panel("NavBot", 0, 300.0f, [&]() {
    pup_menu::checkbox("Navbot", &config.misc.automation.navbot_enabled);
    static const char* navbot_behavior_items[] = {
      "Default",
      "MvM automation"
    };
    pup_menu::combo("Behavior", (int*)&config.misc.automation.navbot_behavior,
      navbot_behavior_items, IM_ARRAYSIZE(navbot_behavior_items));
    pup_menu::bools_combo("Draw", {
      { "Draw path", &config.misc.automation.navbot_draw_path },
      { "Draw path boxes", &config.misc.automation.navbot_draw_path_boxes }
    });
    pup_menu::color_picker("Path color", &config.misc.automation.navbot_path_color);
    pup_menu::bools_combo("Options", {
      { "Don't path during warmup", &config.misc.automation.navbot_dont_path_during_warmup },
      { "Dynamic hazards", &config.misc.automation.navbot_hazards }
    });
    static const char* navbot_weapon_selection_items[] = {
      "Off",
      "Auto",
      "Primary",
      "Secondary",
      "Melee"
    };
    pup_menu::combo(
        "Weapon selection",
        (int*)&config.misc.automation.navbot_weapon_selection,
        navbot_weapon_selection_items,
        IM_ARRAYSIZE(navbot_weapon_selection_items));
    pup_menu::combo("Enemy stalk mode", (int*)&config.misc.automation.enemy_stalk_mode, enemy_stalk_mode_items, IM_ARRAYSIZE(enemy_stalk_mode_items));
    pup_menu::slider_float("Melee target range", &config.misc.automation.navbot_melee_target_range, 150.0f, 4000.0f, "%.0f HU");
    pup_menu::slider_float("Crumb blacklist", &config.misc.automation.navbot_crumb_blacklist_seconds, 50.0f, 150.0f, "%.0f s");
    pup_menu::checkbox("Debug text", &config.misc.automation.navbot_debug_text);
  });
  pup_menu::flow_panel("Path Look", 0, 132.0f, [&]() {
    pup_menu::checkbox("Look at path", &config.misc.automation.navbot_look_at_path);
    pup_menu::checkbox("Silent look", &config.misc.automation.navbot_look_at_path_silent);
    pup_menu::slider_int("Slow aim", &config.misc.automation.navbot_look_at_path_speed, 1, 100);
    pup_menu::slider_int("Spin chance", &config.misc.automation.navbot_look_at_path_spin_chance, 0, 100);
  });
  pup_menu::flow_panel("Jobs", 1, 90.0f, [&]() {
    uint32_t navbot_enabled_jobs_mask = navbot_all_job_bits & ~config.misc.automation.navbot_excluded_jobs_mask;
    if (pup_menu::multi_select_combo("Enabled jobs", &navbot_enabled_jobs_mask, navbot_job_items, navbot_job_bits, IM_ARRAYSIZE(navbot_job_items))) {
      config.misc.automation.navbot_excluded_jobs_mask = navbot_all_job_bits & ~navbot_enabled_jobs_mask;
    }
  });
  pup_menu::flow_panel("Followbot", 1, 220.0f, [&]() {
    static const char* followbot_target_items[] = {"Teammates", "Enemies"};
    static const uint32_t followbot_target_bits[] = {
      Misc::Automation::followbot_teammates,
      Misc::Automation::followbot_enemies
    };
    static const char* followbot_preference_items[] = {"Off", "Friends", "Party"};
    static const char* followbot_nav_items[] = {"Off", "Normal", "Normal + Dormant"};
    static const char* followbot_look_items[] = {"Off", "Path", "Copy target", "At target"};
    pup_menu::checkbox("Followbot", &config.misc.automation.followbot_enabled);
    pup_menu::combo("Use nav mesh", (int*)&config.misc.automation.followbot_use_nav,
      followbot_nav_items, IM_ARRAYSIZE(followbot_nav_items));
    pup_menu::multi_select_combo("Targets", &config.misc.automation.followbot_targets,
      followbot_target_items, followbot_target_bits, IM_ARRAYSIZE(followbot_target_items));
    pup_menu::combo("Prefer", (int*)&config.misc.automation.followbot_preference,
      followbot_preference_items, IM_ARRAYSIZE(followbot_preference_items));
    pup_menu::combo("Look at path", (int*)&config.misc.automation.followbot_look,
      followbot_look_items, IM_ARRAYSIZE(followbot_look_items));
    pup_menu::bools_combo("Options", {
      { "Avoid view snap", &config.misc.automation.followbot_look_no_snap },
      { "Ignore AFK targets", &config.misc.automation.followbot_ignore_afk }
    });
    pup_menu::slider_int("Min priority", &config.misc.automation.followbot_min_priority, 0, 10);
    pup_menu::slider_int("Max path nodes", &config.misc.automation.followbot_max_nodes, 50, 500);
    pup_menu::slider_float("Activation distance", &config.misc.automation.followbot_activation_distance, 10.0f, 1200.0f, "%.0f HU");
    pup_menu::slider_float("Follow distance", &config.misc.automation.followbot_follow_distance, 10.0f, 150.0f, "%.0f HU");
    pup_menu::slider_float("Abandon distance", &config.misc.automation.followbot_abandon_distance, 250.0f, 1500.0f, "%.0f HU");
    pup_menu::slider_float("Nav abandon distance", &config.misc.automation.followbot_nav_abandon_distance, 500.0f, 8000.0f, "%.0f HU");
  });
  pup_menu::end_flow_layout();
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

  pup_menu::begin_flow_layout("medic_layout", 2);
  pup_menu::flow_panel("Medic", 0, 184.0f, [&]() {
    pup_menu::bools_combo("Assist", {
      { "Autoheal", &config.misc.automation.medic_autoheal },
      { "Autovacc", &config.misc.automation.medic_autovacc },
      { "Autouber", &config.misc.automation.medic_autouber }
    });
    pup_menu::multi_select_combo("Heal targets", &config.misc.automation.medic_heal_targets_mask, heal_target_items, heal_target_bits, IM_ARRAYSIZE(heal_target_items));
  });
  pup_menu::end_flow_layout();
}

static void draw_region_selector_panel(const char* list_id) {
  bool region_selector_changed = pup_menu::checkbox("Enable", &config.misc.automation.region_selector);

  const float button_spacing = ImGui::GetStyle().ItemSpacing.x;
  const float button_row_width = ImMax(0.0f, ImGui::GetContentRegionAvail().x - 10.0f);
  const float button_width = ImMax(0.0f, ImFloor((button_row_width - button_spacing) * 0.5f));
  if (pup_menu::accent_button("Allow all", ImVec2(button_width, 22.0f))) {
    config.misc.automation.region_selector_allowed_mask = automation::region_selector::all_region_bits;
    region_selector_changed = true;
  }
  ImGui::SameLine(0.0f, button_spacing);
  if (pup_menu::accent_button("Block all", ImVec2(button_width, 22.0f), true)) {
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
      if (pup_menu::checkbox(current_continent.data(), &continent_allowed)) {
        automation::region_selector::set_continent_regions_allowed(current_continent, continent_allowed);
        region_selector_changed = true;
      }
    }
    bool allowed = automation::region_selector::is_region_bit_allowed(data_center.bit);
    if (pup_menu::checkbox(data_center.label, &allowed)) {
      automation::region_selector::set_region_allowed(data_center.bit, allowed);
      region_selector_changed = true;
    }
  }
  ImGui::EndChild();

  if (region_selector_changed) {
    automation::region_selector::refresh_ping_data();
  }
}

static void draw_chat_content() {
  static const char* chatspam_items[] = {
    "Off",
    "Puphook",
    "LMAOBOX",
    "Custom"
  };
  static const char* killsay_items[] = {
    "Off",
    "Puphook",
    "MLG",
    "Custom"
  };

  pup_menu::begin_flow_layout("chat_layout", 2);
  pup_menu::flow_panel("Chat spam", 0, 166.0f, [&]() {
    pup_menu::combo("Chatspam", (int*)&config.misc.automation.chatspam, chatspam_items, IM_ARRAYSIZE(chatspam_items));
    pup_menu::input_text("Spam file", &config.misc.automation.chatspam_file);
    pup_menu::bools_combo("Options", {
      { "Random order", &config.misc.automation.chatspam_random },
      { "Team chat", &config.misc.automation.chatspam_team }
    });
    pup_menu::slider_int("Spam delay", &config.misc.automation.chatspam_delay_ms, 250, 60000);
  });
  pup_menu::flow_panel("Killsay", 1, 128.0f, [&]() {
    pup_menu::combo("Killsay", (int*)&config.misc.automation.killsay, killsay_items, IM_ARRAYSIZE(killsay_items));
    pup_menu::input_text("Killsay file", &config.misc.automation.killsay_file);
    pup_menu::slider_int("Killsay delay", &config.misc.automation.killsay_delay_ms, 0, 10000);
  });
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
  pup_menu::flow_panel("Spam", 0, 200.0f, [&]() {
    pup_menu::checkbox("Noisemaker spam", &config.misc.automation.noisemaker_spam);
    pup_menu::combo("Voice command spam", (int*)&config.misc.automation.voice_command_spam, voice_command_spam_items, IM_ARRAYSIZE(voice_command_spam_items));
    pup_menu::checkbox("Micspam", &config.misc.automation.micspam);
    pup_menu::slider_int("Micspam on", &config.misc.automation.micspam_interval_on_seconds, 1, 600, "%d s");
    pup_menu::slider_int("Micspam off", &config.misc.automation.micspam_interval_off_seconds, 1, 600, "%d s");
    pup_menu::checkbox("Micspam from file", &config.misc.automation.micspam_from_file);
  });
  pup_menu::flow_panel("Taunt", 1, 128.0f, [&]() {
    pup_menu::checkbox("Auto taunt", &config.misc.automation.autotaunt);
    pup_menu::slider_float("Taunt chance", &config.misc.automation.autotaunt_chance, 0.0f, 100.0f, "%.0f%%");
    pup_menu::slider_float("Taunt safety distance", &config.misc.automation.autotaunt_safety_distance, 0.0f, 5000.0f, "%.0f HU");
    pup_menu::slider_int("Taunt weapon slot", &config.misc.automation.autotaunt_weapon_slot, 0, 5);
  });
  pup_menu::end_flow_layout();
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

  pup_menu::begin_flow_layout("queue_layout", 2);
  pup_menu::flow_panel("Queue", 1, 248.0f, [&]() {
    pup_menu::combo("Mode", (int*)&config.misc.automation.queue_mode, queueing_mode_items, IM_ARRAYSIZE(queueing_mode_items));
    if (config.misc.automation.queue_mode == Misc::Automation::queueing_mode::BOOST) {
      pup_menu::checkbox("Enabled", &config.misc.automation.boost_queue_enabled);
      pup_menu::combo("Boost", (int*)&config.misc.automation.boost_queue, boost_queue_mode_items, IM_ARRAYSIZE(boost_queue_mode_items));
      return;
    }
    pup_menu::bools_combo("Queue", {
      { "Auto queue", &config.misc.automation.auto_queue },
      { "Auto requeue", &config.misc.automation.auto_requeue },
      { "Requeue on kick", &config.misc.automation.requeue_on_kick },
      { "Auto casual join", &config.misc.automation.auto_casual_join }
    });
    pup_menu::combo("Queue mode", &config.misc.automation.auto_queue_mode, queue_mode_items, IM_ARRAYSIZE(queue_mode_items));
    pup_menu::slider_int("RQ if players <", &config.misc.automation.rq_if_players_lte, 0, 32);
    pup_menu::slider_int("RQ if players >", &config.misc.automation.rq_if_players_gte, 0, 32);
    pup_menu::slider_int("RQ if IPC bots >", &config.misc.automation.rq_if_ipc_bots_gt, 0, 32);
    pup_menu::bools_combo("Requeue", {
      { "RQ if no navmesh", &config.misc.automation.rq_if_no_navmesh },
      { "RQ ignore friends", &config.misc.automation.rq_ignore_friends }
    });
    pup_menu::combo("Requeue action", (int*)&config.misc.automation.requeue_action, requeue_action_items, IM_ARRAYSIZE(requeue_action_items));
  });
  pup_menu::flow_panel("Region selector", 0, 390.0f, [&]() {
    draw_region_selector_panel("##queue_region_selector_list");
  }, false);
  pup_menu::flow_panel("AutoParty", 0, 252.0f, [&]() {
    pup_menu::checkbox("Enable", &config.misc.automation.autoparty);
    pup_menu::slider_int("Max party size", &config.misc.automation.autoparty_max_party_size, 1, 6);
    pup_menu::input_text("Hosts (Steam32 IDs)", &config.misc.automation.autoparty_party_hosts);
    pup_menu::bools_combo("Options", {
      { "Kick cheaters", &config.misc.automation.autoparty_kick_rage },
      { "Auto leave on offline member", &config.misc.automation.autoparty_auto_leave },
      { "Auto lock", &config.misc.automation.autoparty_auto_lock },
      { "Auto unlock", &config.misc.automation.autoparty_auto_unlock },
      { "Log", &config.misc.automation.autoparty_log },
      { "Message kicks to party", &config.misc.automation.autoparty_message_kicks },
      { "IPC mode", &config.misc.automation.autoparty_ipc_mode }
    });
    pup_menu::slider_int("IPC host count", &config.misc.automation.autoparty_ipc_count, 0, 8);
    pup_menu::slider_int("Run every", &config.misc.automation.autoparty_run_frequency, 5, 300, "%d s");
  });
  const char* class_items[] = { "Undefined", "Scout", "Sniper", "Soldier", "Demoman", "Medic", "Heavy", "Pyro", "Spy", "Engineer" };
  pup_menu::flow_panel("MvM", 1, 268.0f, [&]() {
    pup_menu::bools_combo("MvM", {
      { "Instant respawn", &config.misc.automation.mvm_instant_respawn },
      { "Instant revive", &config.misc.automation.mvm_instant_revive },
      { "Allow inspect", &config.misc.automation.allow_mvm_inspect },
      { "Auto ready up", &config.misc.automation.auto_mvm_ready_up },
      { "Auto abandon Mann Up", &config.misc.automation.auto_mvm_abandon_mannup },
      { "Buybot", &config.misc.automation.mvm_buybot }
    });
    pup_menu::slider_int("Buybot max cash", &config.misc.automation.mvm_buybot_max_cash, 0, 50000);
    pup_menu::checkbox("Buybot auto class", &config.misc.automation.mvm_buybot_auto_class);
    pup_menu::combo("Buybot class", (int*)&config.misc.automation.mvm_buybot_class,
      class_items, IM_ARRAYSIZE(class_items));
    static const char* mvm_chat_command_items[] = { "Off", "Party", "Friends", "Role" };
    pup_menu::combo("Chat commands", (int*)&config.misc.automation.mvm_chat_commands,
      mvm_chat_command_items, IM_ARRAYSIZE(mvm_chat_command_items));
    if (config.misc.automation.mvm_chat_commands == Misc::Automation::mvm_chat_command_mode::ROLE) {
      static const char* chat_role_items[] = { "Ignored", "Cheater", "Friend", "Party", "F2P", "Pup" };
      static const int chat_role_ids[] = { -1, -2, -3, -4, -5, -8 };
      int chat_role_index = 3;
      for (int index = 0; index < IM_ARRAYSIZE(chat_role_ids); ++index) {
        if (chat_role_ids[index] == config.misc.automation.mvm_chat_commands_role) {
          chat_role_index = index;
          break;
        }
      }
      if (pup_menu::combo("Chat command role", &chat_role_index, chat_role_items, IM_ARRAYSIZE(chat_role_items)) &&
          chat_role_index >= 0 && chat_role_index < IM_ARRAYSIZE(chat_role_ids)) {
        config.misc.automation.mvm_chat_commands_role = chat_role_ids[chat_role_index];
      }
    }
  });
  pup_menu::end_flow_layout();
}

static void draw_automation_utilities_content() {
  const char* class_items[] = { "Undefined", "Scout", "Sniper", "Soldier", "Demoman", "Medic", "Heavy", "Pyro", "Spy", "Engineer" };

  pup_menu::begin_flow_layout("automation_utilities_layout", 2);
  pup_menu::flow_panel("Class", 0, 104.0f, [&]() {
    pup_menu::checkbox("Auto class select", &config.misc.automation.auto_class_select);
    pup_menu::combo("Preferred class", (int*)&config.misc.automation.class_selected, class_items, IM_ARRAYSIZE(class_items));
    pup_menu::checkbox("Don't join class during warmup", &config.misc.automation.auto_class_dont_join_during_warmup);
  });
  pup_menu::flow_panel("General", 0, 190.0f, [&]() {
    pup_menu::bools_combo("General", {
      { "Anti AFK", &config.misc.automation.anti_afk },
      { "Anti autobalance", &config.misc.automation.anti_autobalance },
      { "Anti MOTD", &config.misc.automation.anti_motd },
      { "Don't close MOTD during warmup", &config.misc.automation.anti_motd_dont_close_during_warmup },
      { "Auto report", &config.misc.automation.auto_report },
      { "Auto vote map", &config.misc.automation.auto_vote_map }
    });
    pup_menu::slider_int("Vote option", &config.misc.automation.auto_vote_map_option, 0, 2);
    static const char* auto_vote_items[] = {"Defend", "Assist", "Kick", "Kick all"};
    static const uint32_t auto_vote_bits[] = {
      Misc::Automation::auto_vote_defend, Misc::Automation::auto_vote_assist,
      Misc::Automation::auto_vote_kick, Misc::Automation::auto_vote_kick_all};
    pup_menu::multi_select_combo("Auto vote", &config.misc.automation.auto_vote,
                                 auto_vote_items, auto_vote_bits, IM_ARRAYSIZE(auto_vote_items));
    pup_menu::checkbox("Auto vote delay", &config.misc.automation.auto_vote_delay);
    pup_menu::slider_float("Vote delay min", &config.misc.automation.auto_vote_delay_min, 0.0f, 30.0f, "%.1f s");
    pup_menu::slider_float("Vote delay max", &config.misc.automation.auto_vote_delay_max, 0.0f, 30.0f, "%.1f s");
    pup_menu::bools_combo("Extras", {
      { "Killstreak", &config.misc.automation.killstreak },
      { "Custom announcer", &config.misc.automation.custom_announcer }
    });
  });
  static const char* cheat_detection_items[] = {
    "Invalid pitch", "Packet choking", "Aim flick", "Duck speed", "Lag-comp abuse", "Crit manipulation"};
  static const uint32_t cheat_detection_bits[] = {
    Misc::CheatDetection::method_invalid_pitch, Misc::CheatDetection::method_packet_choking,
    Misc::CheatDetection::method_aim_flick, Misc::CheatDetection::method_duck_speed,
    Misc::CheatDetection::method_lagcomp_abuse, Misc::CheatDetection::method_crit_manipulation};
  pup_menu::flow_panel("Cheat detection", 1, 190.0f, [&]() {
    pup_menu::multi_select_combo("Methods", &config.misc.cheat_detection.methods,
                                 cheat_detection_items, cheat_detection_bits, IM_ARRAYSIZE(cheat_detection_items));
    pup_menu::slider_int("Detections to mark", &config.misc.cheat_detection.detections_required, 1, 20);
    pup_menu::slider_float("Min flick", &config.misc.cheat_detection.min_flick, 1.0f, 180.0f, "%.0f deg");
    pup_menu::slider_float("Max noise", &config.misc.cheat_detection.max_noise, 0.0f, 45.0f, "%.1f deg");
    pup_menu::slider_int("Min choking ticks", &config.misc.cheat_detection.min_choking_ticks, 2, 128);
    pup_menu::slider_int("Lag-comp min rewind", &config.misc.cheat_detection.lagcomp_min_delta, 2, 64);
    pup_menu::slider_float("Lag-comp window", &config.misc.cheat_detection.lagcomp_window, 0.1f, 30.0f, "%.1f s");
    pup_menu::slider_int("Lag-comp bursts", &config.misc.cheat_detection.lagcomp_burst_count, 1, 16);
    pup_menu::slider_int("Crit window", &config.misc.cheat_detection.crit_window, 2, 200);
    pup_menu::slider_float("Crit threshold", &config.misc.cheat_detection.crit_threshold, 1.0f, 100.0f, "%.0f%%");
  });
  pup_menu::flow_panel("Profile presence", 0, 280.0f, [&]() {
    pup_menu::checkbox("Enable", &config.misc.automation.stalker_enabled);
    pup_menu::slider_int("Refresh interval", &config.misc.automation.stalker_interval, 5, 300, "%d s");
    ImGui::TextWrapped("Tracks SteamIDs listed in stalk.txt using Steam presence and public server queries.");
    const int tracked = automation::profile_stalker::status_count();
    if (tracked <= 0) {
      ImGui::TextUnformatted("No profiles in stalk.txt");
      return;
    }
    for (int index = 0; index < tracked; ++index) {
      if (index > 0) {
        ImGui::Separator();
      }
      const auto card = automation::profile_stalker::status(index);
      ImGui::TextUnformatted(card.card.empty() ? "Unknown" : card.card.c_str());
    }
  });
  pup_menu::end_flow_layout();
}

#if 0 // Inventory changer UI temporarily disabled.
static void draw_inventory_changer_content() {
  pup_menu::begin_flow_layout("inventory_changer_layout", 2);
  pup_menu::flow_panel("Inventory changer", 0, 238.0f, [&]() {
    pup_menu::checkbox("Enable", &config.misc.inventory_changer.enabled);
    pup_menu::checkbox("Apply to all", &config.misc.inventory_changer.apply_to_all);
    pup_menu::checkbox("Debug", &config.misc.inventory_changer.debug);
    pup_menu::draw_inventory_definition("Crate", &config.misc.inventory_changer.crate,
      inventory_changer::item_category::crate);
    pup_menu::draw_inventory_definition("Key", &config.misc.inventory_changer.key,
      inventory_changer::item_category::key);
    ImGui::TextWrapped("Crates and keys are local inventory redirects; opening still uses the game's normal key/item checks.");
  });
  pup_menu::flow_panel("Weapons", 1, 540.0f, [&]() {
    pup_menu::draw_inventory_slot("Primary", config.misc.inventory_changer.primary, true);
    pup_menu::draw_inventory_slot("Secondary", config.misc.inventory_changer.secondary, true);
    pup_menu::draw_inventory_slot("Melee", config.misc.inventory_changer.melee, true);
  });
  pup_menu::flow_panel("Hats", 0, 540.0f, [&]() {
    pup_menu::draw_inventory_slot("Hat 1", config.misc.inventory_changer.hat1, false);
    pup_menu::draw_inventory_slot("Hat 2", config.misc.inventory_changer.hat2, false);
    pup_menu::draw_inventory_slot("Hat 3", config.misc.inventory_changer.hat3, false);
  });
  pup_menu::flow_panel("Taunt", 1, 100.0f, [&]() {
    const auto& effects = inventory_changer::effect_options();
    std::vector<const char*> effect_labels{};
    effect_labels.reserve(effects.size());
    int selected_effect = 0;
    for (std::size_t index = 0; index < effects.size(); ++index) {
      effect_labels.push_back(effects[index].label.c_str());
      if (effects[index].definition == static_cast<std::uint16_t>(config.misc.inventory_changer.taunt1_unusual)) selected_effect = static_cast<int>(index);
    }
    if (pup_menu::combo("Slot 1 unusual", &selected_effect, effect_labels.data(), static_cast<int>(effect_labels.size())) &&
        selected_effect >= 0 && static_cast<std::size_t>(selected_effect) < effects.size()) {
      config.misc.inventory_changer.taunt1_unusual = effects[static_cast<std::size_t>(selected_effect)].definition;
    }
  });
  pup_menu::end_flow_layout();
}
#endif

static void draw_autoitem_content() {
  pup_menu::begin_flow_layout("autoitem_layout", 2);
  pup_menu::flow_panel("AutoItem", 0, 116.0f, [&]() {
    pup_menu::checkbox("Enable", &config.misc.automation.auto_item);
    pup_menu::slider_int("Interval", &config.misc.automation.auto_item_interval_ms, 1000, 120000, "%d ms");
    pup_menu::checkbox("Debug", &config.misc.automation.auto_item_debug);
  });
  pup_menu::flow_panel("Weapons", 1, 156.0f, [&]() {
    pup_menu::checkbox("Weapons", &config.misc.automation.auto_item_weapons);
    pup_menu::input_text("Primary", &config.misc.automation.auto_item_primary);
    pup_menu::input_text("Secondary", &config.misc.automation.auto_item_secondary);
    pup_menu::input_text("Melee", &config.misc.automation.auto_item_melee);
  });
  pup_menu::flow_panel("Equipment", 1, 182.0f, [&]() {
    pup_menu::checkbox("Equipment", &config.misc.automation.auto_item_equipment);
    pup_menu::input_text("Building", &config.misc.automation.auto_item_building);
    pup_menu::input_text("PDA", &config.misc.automation.auto_item_pda);
    pup_menu::input_text("PDA2", &config.misc.automation.auto_item_pda2);
    pup_menu::input_text("Action", &config.misc.automation.auto_item_action);
    pup_menu::input_text("Taunt", &config.misc.automation.auto_item_taunt);
  });
  pup_menu::flow_panel("Cosmetics", 1, 182.0f, [&]() {
    pup_menu::checkbox("Hats", &config.misc.automation.auto_item_hats);
    pup_menu::input_text("Hat 1", &config.misc.automation.auto_item_hat1);
    pup_menu::input_text("Hat 2", &config.misc.automation.auto_item_hat2);
    pup_menu::input_text("Hat 3", &config.misc.automation.auto_item_hat3);
    pup_menu::checkbox("Noisemaker", &config.misc.automation.auto_item_noisemaker);
  });
  pup_menu::end_flow_layout();
}

static void draw_ipc_content() {
#if defined(PUPHOOK_TEXTMODE) && PUPHOOK_TEXTMODE

  config.ipc.enabled = true;
  config.ipc.auto_connect = true;
  config.ipc.auto_ignore_local_bots = true;
#endif

  const bool connected = pup_ipc::client::connected();
  const int peer_id = pup_ipc::client::peer_id();

  pup_menu::begin_flow_layout("ipc_layout", 2);
  pup_menu::flow_panel("Connection", 0, 156.0f, [&]() {
    pup_menu::bools_combo("Connection", {
      { "Enable IPC", &config.ipc.enabled },
      { "Auto connect", &config.ipc.auto_connect },
      { "Auto ignore local bots", &config.ipc.auto_ignore_local_bots }
    });
#if defined(PUPHOOK_TEXTMODE) && PUPHOOK_TEXTMODE

    config.ipc.enabled = true;
    config.ipc.auto_connect = true;
    config.ipc.auto_ignore_local_bots = true;
#endif

    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, connected ? pup_menu::menu_accent() : pup_menu::k_text_soft);
    if (connected) {
      ImGui::Text("Status: connected as peer %d", peer_id);
    } else {
      ImGui::TextUnformatted("Status: disconnected");
    }
    ImGui::PopStyleColor();
  });
  pup_menu::flow_panel("Actions", 1, 112.0f, [&]() {
    const float button_width = (ImGui::GetContentRegionAvail().x - 6.0f) * 0.5f;
    if (pup_menu::accent_button("Connect", ImVec2(button_width, 22.0f))) {
      config.ipc.enabled = true;
      pup_ipc::client::set_enabled(true);
      pup_ipc::client::start();
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (pup_menu::accent_button("Reconnect", ImVec2(0.0f, 22.0f))) {
      config.ipc.enabled = true;
      pup_ipc::client::shutdown();
      pup_ipc::client::set_enabled(true);
      pup_ipc::client::start();
    }
#if !defined(PUPHOOK_TEXTMODE) || !PUPHOOK_TEXTMODE

    if (pup_menu::accent_button("Disconnect", ImVec2(0.0f, 22.0f), true)) {
      config.ipc.enabled = false;
      pup_ipc::client::shutdown();
    }
#endif

  });
  pup_menu::flow_panel("Notes", 1, 94.0f, [&]() {
    ImGui::PushStyleColor(ImGuiCol_Text, pup_menu::k_text_soft);
    ImGui::TextUnformatted("Connect uses the pupbot shared memory server.");
#if defined(PUPHOOK_TEXTMODE) && PUPHOOK_TEXTMODE

    ImGui::TextUnformatted("Textmode forces IPC on.");
#else

    ImGui::TextUnformatted("Auto connect retries while IPC is enabled.");
    ImGui::TextUnformatted("Disconnect disables IPC until enabled again.");
#endif

    ImGui::PopStyleColor();
  });
  pup_menu::end_flow_layout();
}

static void draw_automation_tab(const int automation_subtab) {
  switch (automation_subtab) {
    case pup_menu::automation_subtab_general: draw_automation_utilities_content(); break;
    case pup_menu::automation_subtab_queue: draw_queue_content(); break;
    case pup_menu::automation_subtab_chat: draw_chat_content(); break;
    case pup_menu::automation_subtab_items: draw_autoitem_content(); break;
    case pup_menu::automation_subtab_navbot: draw_navbot_content(); break;
    case pup_menu::automation_subtab_medic: draw_medic_content(); break;
    case pup_menu::automation_subtab_ipc: draw_ipc_content(); break;
  }
}

static void draw_exploits_content() {
  static const char* backtrack_visualizer_items[] = {
    "Points",
    "Boxes",
    "Projected boxes",
    "Trail",
    "Pulse",
    "Unified (best record)"
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

  pup_menu::begin_flow_layout("exploits_layout", 2);

  pup_menu::flow_panel("Backtrack", 0, 280.0f, [&]() {
    pup_menu::checkbox("Enable", &config.backtrack.enabled);
    pup_menu::slider_int("Window", &config.backtrack.window_ms, 0, 1000, "%d ms");
    pup_menu::slider_float("Fake latency", &config.backtrack.fake_latency_ms, 0.0f, 1000.0f, "%.0f ms");
    pup_menu::checkbox("Fake interp", &config.backtrack.fake_interp);
    ImGui::BeginDisabled(!config.backtrack.fake_interp);
    pup_menu::slider_float("Interpolation", &config.backtrack.interp_ms, 0.0f, 1000.0f, "%.0f ms");
    ImGui::EndDisabled();
    pup_menu::bools_combo("Aim", {
      { "Prefer on shot", &config.backtrack.prefer_on_shot },
      { "Backtrack to crosshair", &config.backtrack.to_crosshair }
    });
    pup_menu::slider_int("Tick offset", &config.backtrack.offset_ticks, -4, 4, "%d ticks");
    pup_menu::checkbox("Visualizer", &config.backtrack.visualizer);
    pup_menu::combo("Visualizer style", (int*)&config.backtrack.visualizer_mode, backtrack_visualizer_items, IM_ARRAYSIZE(backtrack_visualizer_items));
    pup_menu::slider_int("Ticks", &config.backtrack.visualizer_ticks, 1, 80);
  });

  pup_menu::flow_panel("Bypasses", 1, 88.0f, [&]() {
    pup_menu::bools_combo("Bypasses", {
      { "Bypass sv_pure", &config.misc.exploits.bypasspure },
      { "Pure bypass", &config.misc.exploits.pure_bypass },
      { "Cheats bypass", &config.misc.exploits.cheats_bypass },
      { "VAC bypass", &config.misc.exploits.vac_bypass },
      { "Network fix", &config.misc.exploits.network_fix },
      { "No engine sleep", &config.misc.exploits.no_engine_sleep },
      { "Null graphics", &config.misc.exploits.null_graphics }
    });
  });
  pup_menu::flow_panel("Tickbase", 1, 224.0f, [&]() {
    pup_menu::checkbox("Tickbase", &config.misc.exploits.tickbase);
    pup_menu::checkbox("Recharge", &config.misc.exploits.tickbase_recharge);
    pup_menu::checkbox("Doubletap", &config.misc.exploits.doubletap);
    pup_menu::slider_int("Doubletap ticks", &config.misc.exploits.doubletap_ticks, 1, 21);
    pup_menu::checkbox("Warp", &config.misc.exploits.warp);
    pup_menu::slider_int("Warp ticks", &config.misc.exploits.warp_ticks, 1, 21);
    pup_menu::checkbox("Fakelag", &config.misc.exploits.fakelag);
    pup_menu::slider_int("Fakelag ticks", &config.misc.exploits.fakelag_ticks, 1, 21);
    pup_menu::checkbox("Antiwarp", &config.misc.exploits.antiwarp);
  });
  pup_menu::flow_panel("Engine", 0, 118.0f, [&]() {
    pup_menu::bools_combo("Engine", {
      { "Equip region unlock", &config.misc.exploits.equip_region_unlock },
      { "Anti-cheat compat", &config.misc.exploits.anti_cheat_compat },
      { "Ping reducer", &config.misc.exploits.ping_reducer }
    });
    pup_menu::slider_int("Ping target", &config.misc.exploits.ping_target, 1, 100);
  });
  pup_menu::flow_panel("Anti-aim", 1, 286.0f, [&]() {
    pup_menu::checkbox("Enable", &config.misc.exploits.anti_aim);
    pup_menu::combo("Real pitch", (int*)&config.misc.exploits.anti_aim_real_pitch, anti_aim_pitch_items, IM_ARRAYSIZE(anti_aim_pitch_items));
    pup_menu::combo("Fake pitch", (int*)&config.misc.exploits.anti_aim_fake_pitch, anti_aim_pitch_items, IM_ARRAYSIZE(anti_aim_pitch_items));
    pup_menu::combo("Real base", (int*)&config.misc.exploits.anti_aim_real_yaw_base, anti_aim_yaw_base_items, IM_ARRAYSIZE(anti_aim_yaw_base_items));
    pup_menu::combo("Fake base", (int*)&config.misc.exploits.anti_aim_fake_yaw_base, anti_aim_yaw_base_items, IM_ARRAYSIZE(anti_aim_yaw_base_items));
    pup_menu::combo("Real yaw", (int*)&config.misc.exploits.anti_aim_real_yaw, anti_aim_yaw_items, IM_ARRAYSIZE(anti_aim_yaw_items));
    pup_menu::combo("Fake yaw", (int*)&config.misc.exploits.anti_aim_fake_yaw, anti_aim_yaw_items, IM_ARRAYSIZE(anti_aim_yaw_items));
    pup_menu::slider_float("Real offset", &config.misc.exploits.anti_aim_real_yaw_offset, -180.0f, 180.0f, "%.0f deg");
    pup_menu::slider_float("Fake offset", &config.misc.exploits.anti_aim_fake_yaw_offset, -180.0f, 180.0f, "%.0f deg");
    pup_menu::slider_float("Spin speed", &config.misc.exploits.anti_aim_spin_speed, -180.0f, 180.0f, "%.0f deg");
    pup_menu::checkbox("Anti-overlap", &config.misc.exploits.anti_aim_anti_overlap);
  });
  pup_menu::end_flow_layout();
}

static void draw_config_content() {
  puphook::core::config_store* config_store = puphook::core::get_config_store();
  if (config_store == nullptr) {
    pup_menu::begin_panel("Configs", ImVec2(0.0f, 0.0f));
    ImGui::TextUnformatted("Config store unavailable");
    pup_menu::end_panel();
    return;
  }

  pup_menu::begin_flow_layout("config_layout", 2);
  pup_menu::flow_panel("Config List", 0, 330.0f, [&]() {
    const std::vector<std::string> configs = config_store->list_files();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##config_list_box", ImVec2(-1.0f, 250.0f), false, ImGuiWindowFlags_NoBackground);
    if (configs.empty()) {
      ImGui::SetCursorPosY(8.0f);
      ImGui::PushStyleColor(ImGuiCol_Text, pup_menu::k_text_soft);
      ImGui::TextUnformatted("No configs found.");
      ImGui::PopStyleColor();
    } else {
      for (int index = 0; index < static_cast<int>(configs.size()); ++index) {
        const bool selected = selected_config == index;
        if (pup_menu::list_row(configs[index].c_str(), selected)) {
          selected_config = index;
          std::strncpy(config_name, configs[index].c_str(), std::size(config_name) - 1);
          config_name[std::size(config_name) - 1] = '\0';
        }
      }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, pup_menu::k_text_soft);
    ImGui::Text("Stored configs: %d", static_cast<int>(configs.size()));
    ImGui::Text("Current: %s", config_store->current_name().c_str());
    ImGui::PopStyleColor();
  });
  pup_menu::flow_panel("Config Options", 1, 186.0f, [&]() {
    pup_menu::input_text("Config name", config_name, static_cast<int>(std::size(config_name)));
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    if (pup_menu::accent_button("Create", ImVec2((ImGui::GetContentRegionAvail().x - 6.0f) * 0.5f, 22.0f))) {
      config_store->import_config(config);
      if (config_store->save_file(config_name)) {
        pup_bind::save(config_store, config_name);
      }
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (pup_menu::accent_button("Save", ImVec2(0.0f, 22.0f))) {
      config_store->import_config(config);
      if (config_store->save_file(config_name)) {
        pup_bind::save(config_store, config_name);
      }
    }
    if (pup_menu::accent_button("Load", ImVec2((ImGui::GetContentRegionAvail().x - 6.0f) * 0.5f, 22.0f))) {
      if (config_store->load_file(config_name)) {
        config_store->export_config(config);
        reset_insider_settings_session(config);
        pup_bind::load(config_store);
      }
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (pup_menu::accent_button("Delete", ImVec2(0.0f, 22.0f), true)) {
      config_store->delete_file(config_name);
      pup_bind::delete_file(config_store, config_name);
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, pup_menu::k_text_soft);
    ImGui::TextUnformatted("Actions save the current in-memory config.");
    ImGui::TextUnformatted("Load replaces the current settings from disk.");
    ImGui::PopStyleColor();
  });
  pup_menu::end_flow_layout();
}

static void draw_materials_content() {
  materials.prepare();
  static std::string new_material_name{};
  static std::string selected_material{};
  static std::string material_source{};
  static bool selected_locked = false;

  ImGui::BeginChild("material_editor_layout", {0.0f, 0.0f}, ImGuiChildFlags_None);
  const float left_width = std::clamp(ImGui::GetContentRegionAvail().x * 0.34f, pup_menu::scaled(190.0f), pup_menu::scaled(250.0f));
  ImGui::BeginChild("material_manager", {left_width, 0.0f}, ImGuiChildFlags_Border);
  ImGui::TextUnformatted("material manager");
  mono::group_separator();
  pup_menu::input_text("New material", &new_material_name);
  if (pup_menu::accent_button("Create material", {-1.0f, 22.0f}) && materials.add(new_material_name)) {
    selected_material = new_material_name;
    if (const auto definition = materials.find(selected_material)) {
      material_source = definition->vmt;
      selected_locked = definition->locked;
    }
    new_material_name.clear();
  }
  if (pup_menu::accent_button("Reload materials", {-1.0f, 22.0f})) {
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
    if (pup_menu::list_row(definition.name.c_str(), selected_material == definition.name, {0.0f, 26.0f})) {
      selected_material = definition.name;
      material_source = definition.vmt;
      selected_locked = definition.locked;
    }
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::EndChild();

  ImGui::SameLine(0.0f, pup_menu::scaled(pup_menu::k_gap));
  ImGui::BeginChild("material_editor", {0.0f, 0.0f}, ImGuiChildFlags_Border);
  ImGui::TextUnformatted("material editor");
  mono::group_separator();
  if (selected_material.empty()) {
    ImGui::TextDisabled("Select a material to view or edit its VMT.");
  } else {
    ImGui::TextDisabled("%s: %s", selected_locked ? "viewing" : "editing", selected_material.c_str());
    if (!selected_locked) {
      if (pup_menu::accent_button("Save material", {-1.0f, 22.0f})) {
        materials.edit(selected_material, material_source);
      }
      if (pup_menu::accent_button("Delete material", {-1.0f, 22.0f}, true) && materials.remove(selected_material)) {
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
  pup_menu::begin_flow_layout("interface_layout", 1);
  pup_menu::flow_panel("Menu appearance", 0, 252.0f, [&]() {
    pup_menu::color_picker("Theme color", &config.misc.menu.theme_color);
    pup_menu::combo("Menu scale", &config.misc.menu.dpi_scale, pup_menu::k_dpi_scale_labels.data(), static_cast<int>(pup_menu::k_dpi_scale_labels.size()));
  });
  pup_menu::end_flow_layout();
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
  std::lock_guard lock{pup_bind::bind_mutex()};

  static uint32_t selected_id{};
  static pup_bind::bind_entry draft{};
  static int picking_parent{};
  static uint32_t dragging_id{};
  static uint32_t dragging_parent{};

  if (pup_bind::find_entry(selected_id) == nullptr) selected_id = 0;
  if (picking_parent) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragging_id = 0;

  const auto class_items = std::vector<std::pair<std::string, int>>{
    { "Scout", static_cast<int>(tf_class::SCOUT) },
    { "Soldier", static_cast<int>(tf_class::SOLDIER) },
    { "Pyro", static_cast<int>(tf_class::PYRO) },
    { "Demoman", static_cast<int>(tf_class::DEMOMAN) },
    { "Heavy", static_cast<int>(tf_class::HEAVYWEAPONS) },
    { "Engineer", static_cast<int>(tf_class::ENGINEER) },
    { "Medic", static_cast<int>(tf_class::MEDIC) },
    { "Sniper", static_cast<int>(tf_class::SNIPER) },
    { "Spy", static_cast<int>(tf_class::SPY) }
  };
  const auto copy_fields = [](pup_bind::bind_entry& dest, const pup_bind::bind_entry& src) {
    dest.name = src.name;
    dest.condition = src.condition;
    dest.key_mode = src.key_mode;
    dest.key = src.key;
    dest.condition_value = src.condition_value;
    dest.inverted = src.inverted;
    dest.visibility = src.visibility;
    dest.enabled = src.enabled;
  };
  const auto reset_form = [&] {
    selected_id = 0;
    draft = {};
    picking_parent = 0;
  };
  const auto apply_condition_change = [](pup_bind::bind_entry& bind) {
    if (bind.condition == pup_bind::bind_condition::player_class && bind.condition_value == 0) {
      bind.condition_value = static_cast<int>(tf_class::SCOUT);
    } else if (bind.condition != pup_bind::bind_condition::player_class && bind.condition != pup_bind::bind_condition::key) {
      if (bind.condition == pup_bind::bind_condition::item_slot) bind.condition_value = std::clamp(bind.condition_value, 0, 8);
      else if (bind.condition == pup_bind::bind_condition::weapon_type) bind.condition_value = std::clamp(bind.condition_value, 0, 3);
      else bind.condition_value = std::clamp(bind.condition_value, 0, 4);
    }
  };
  const auto draw_condition_fields = [&](pup_bind::bind_entry& bind) {
    bool changed = false;
    int type = static_cast<int>(bind.condition);
    if (mono::select_single("Type", &type, { { "Key", 0 }, { "Class", 1 }, { "Weapon type", 2 }, { "Item slot", 3 }, { "Misc", 4 } })) {
      bind.condition = static_cast<pup_bind::bind_condition>(type);
      bind.condition_value = bind.condition == pup_bind::bind_condition::player_class ? static_cast<int>(tf_class::SCOUT) : 0;
      bind.toggle_state = false;
      bind.was_down = false;
      bind.press_pending = false;
      changed = true;
    }
    apply_condition_change(bind);
    switch (bind.condition) {
    case pup_bind::bind_condition::key:
    {
      int mode = static_cast<int>(bind.key_mode);
      if (mono::select_single("Behavior", &mode, { { "Hold", 0 }, { "Toggle", 1 }, { "Double click", 2 } })) {
        bind.key_mode = static_cast<pup_bind::bind_key_mode>(mode);
        bind.toggle_state = false;
        bind.was_down = false;
        bind.press_pending = false;
        changed = true;
      }
      break;
    }
    case pup_bind::bind_condition::player_class:
      changed = mono::select_single("Class", &bind.condition_value, class_items) || changed;
      break;
    case pup_bind::bind_condition::weapon_type:
      changed = mono::select_single("Weapon type", &bind.condition_value, { { "Hitscan", 0 }, { "Projectile", 1 }, { "Melee", 2 }, { "Throwable", 3 } }) || changed;
      break;
    case pup_bind::bind_condition::item_slot:
    {
      std::vector<std::pair<std::string, int>> slots{};
      for (int slot{}; slot < 9; ++slot) slots.emplace_back(std::to_string(slot + 1), slot);
      changed = mono::select_single("Item slot", &bind.condition_value, slots) || changed;
      break;
    }
    case pup_bind::bind_condition::misc:
      changed = mono::select_single("Misc", &bind.condition_value, { { "Spectated", 0 }, { "Spectated 1st", 1 }, { "Spectated 3rd", 2 }, { "Zoomed", 3 }, { "Aiming", 4 } }) || changed;
      break;
    }
    return changed;
  };

  pup_menu::begin_flow_layout("bind_settings_layout", 1);
  pup_menu::flow_panel("Settings", 0, 92.0f, [&] {
    if (ImGui::BeginTable("bind_window_settings", 3, ImGuiTableFlags_SizingStretchSame)) {
      ImGui::TableNextColumn();
      if (mono::toggle("Bind window", &config.misc.menu.bind_window)) {}
      ImGui::TableNextColumn();
      if (mono::toggle("Bind window title", &config.misc.menu.bind_window_title)) {}
      ImGui::TableNextColumn();
      if (mono::toggle("Menu shows binds", &config.misc.menu.menu_shows_binds)) {}
      ImGui::EndTable();
    }
  });
  pup_menu::end_flow_layout();

  mono::begin_panel("Binds", { 0.0f, 0.0f });
  if (ImGui::BeginTable("bind_form", 2, ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextColumn();
    if (mono::input_string("Name", &draft.name)) {}
    {
      const pup_bind::bind_entry* parent = pup_bind::find_entry(draft.parent_id);
      std::string parent_label = "Parent: ";
      parent_label += picking_parent ? "..." : parent != nullptr ? parent->name : "None";
      if (mono::button(parent_label.c_str(), { -1.0f, 22.0f })) picking_parent = 2;
    }
    draw_condition_fields(draft);

    ImGui::TableNextColumn();
    int invert = draft.inverted ? 1 : 0;
    if (mono::select_single("While", &invert, { { "Active", 0 }, { "Not active", 1 } })) draft.inverted = invert != 0;
    int visibility = static_cast<int>(draft.visibility);
    if (mono::select_single("Visibility", &visibility, { { "Always", 0 }, { "While active", 1 }, { "Hidden", 2 } })) {
      draft.visibility = static_cast<pup_bind::bind_visibility>(visibility);
    }
    if (draft.condition == pup_bind::bind_condition::key) {
      if (draft.waiting) ImGui::TextDisabled("Press a key or mouse button...");
      else pup_menu::input_key("Key", &draft.key);
    }

    const bool modifying = selected_id != 0 && pup_bind::find_entry(selected_id) != nullptr;
    const bool parent_ok = draft.parent_id == 0 || pup_bind::find_entry(draft.parent_id) != nullptr;
    const bool can_create = parent_ok && (draft.condition != pup_bind::bind_condition::key || draft.key != SDLK_UNKNOWN);
    ImGui::BeginDisabled(!can_create);
    if (pup_menu::icon_button(modifying ? ICON_MD_SETTINGS : ICON_MD_ADD, { 28.0f, 28.0f })) {
      if (modifying) {
        if (pup_bind::bind_entry* live = pup_bind::find_entry(selected_id)) {
          copy_fields(*live, draft);
          pup_bind::reparent(selected_id, draft.parent_id);
          pup_bind::mark_dirty();
        }
      } else {
        const uint32_t id = pup_bind::add(draft.name.empty() ? "new bind" : draft.name, draft.parent_id);
        if (pup_bind::bind_entry* live = pup_bind::find_entry(id)) copy_fields(*live, draft);
      }
      reset_form();
    }
    ImGui::SetItemTooltip(modifying ? "Modify bind" : "Create bind");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (pup_menu::icon_button(ICON_MD_CLEAR, { 28.0f, 28.0f })) reset_form();
    ImGui::SetItemTooltip("Clear");
    ImGui::EndTable();
  }

  ImGui::Dummy(ImVec2(0.0f, 6.0f));
  ImGui::TextDisabled("Binds");
  mono::group_separator();

  struct bind_row_hit
  {
    uint32_t id{};
    uint32_t parent{};
    float min_y{};
    float max_y{};
  };
  std::vector<bind_row_hit> hits{};
  bool clicked_bind = false;
  uint32_t delete_id{};

  const std::function<void(uint32_t, int)> draw_tree = [&](const uint32_t parent, const int depth) {
    for (pup_bind::bind_entry& bind : pup_bind::entries()) {
      if (bind.parent_id != parent) continue;
      ImGui::PushID(static_cast<int>(bind.id));
      const float indent = 8.0f + 28.0f * static_cast<float>(std::min(depth, 3));
      const float height = 28.0f;
      const float width = std::max(80.0f, ImGui::GetContentRegionAvail().x - indent);
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
      const ImVec2 row_min = ImGui::GetCursorScreenPos();
      ImDrawList* draw_list = ImGui::GetWindowDrawList();
      const bool selected = selected_id == bind.id;
      const ImVec4 background = selected ? ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered) : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
      draw_list->AddRectFilled(row_min, { row_min.x + width, row_min.y + height }, ImGui::ColorConvertFloat4ToU32(background), 4.0f);
      if (selected) {
        draw_list->AddRect(row_min, { row_min.x + width, row_min.y + height }, ImGui::GetColorU32(ImGuiCol_CheckMark), 4.0f);
      }

      std::string type{};
      std::string info{};
      pup_bind::describe_bind(bind, type, info);
      const float icon_strip = 125.0f;
      const float text_width = std::max(24.0f, width - icon_strip);
      const bool enabled_path = pup_bind::will_be_enabled(bind.id);
      const ImU32 name_color = ImGui::GetColorU32(bind.active ? ImGuiCol_CheckMark : enabled_path ? ImGuiCol_Text : ImGuiCol_TextDisabled);
      const ImU32 detail_color = ImGui::GetColorU32(enabled_path ? ImGuiCol_Text : ImGuiCol_TextDisabled);
      draw_list->AddText({ row_min.x + 8.0f, row_min.y + 7.0f }, name_color, pup_menu::truncate_ui_text(bind.name, text_width / 3.0f - 8.0f).c_str());
      draw_list->AddText({ row_min.x + text_width / 3.0f, row_min.y + 7.0f }, detail_color, type.c_str());
      draw_list->AddText({ row_min.x + text_width * 2.0f / 3.0f, row_min.y + 7.0f }, detail_color, info.c_str());

      ImGui::InvisibleButton("##row", { width - icon_strip, height });
      const bool row_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
      const bool row_right = ImGui::IsItemClicked(ImGuiMouseButton_Right);
      const bool row_hovered = ImGui::IsItemHovered();
      hits.push_back({ bind.id, bind.parent_id, row_min.y, row_min.y + height });

      float icon_x = width - 24.0f;
      const auto place_icon = [&](const char* icon) {
        ImGui::SetCursorScreenPos({ row_min.x + icon_x, row_min.y + 3.0f });
        icon_x -= 25.0f;
        return pup_menu::icon_button(icon, { 22.0f, 22.0f });
      };
      ImGui::SetCursorScreenPos({ row_min.x + width - 125.0f, row_min.y });
      const bool pressed_delete = place_icon(ICON_MD_DELETE);
      if (place_icon(ICON_MD_EDIT)) {
        pup_bind::set_editing(pup_bind::editing() == bind.id ? 0 : bind.id);
      }
      if (place_icon(bind.inverted ? ICON_MD_CODE_OFF : ICON_MD_CODE)) {
        bind.inverted = !bind.inverted;
        if (selected_id == bind.id) draft.inverted = bind.inverted;
        pup_bind::mark_dirty();
      }
      if (place_icon(bind.visibility == pup_bind::bind_visibility::always ? ICON_MD_VISIBILITY : ICON_MD_VISIBILITY_OFF)) {
        bind.visibility = static_cast<pup_bind::bind_visibility>((static_cast<int>(bind.visibility) + 1) % 3);
        if (selected_id == bind.id) draft.visibility = bind.visibility;
        pup_bind::mark_dirty();
      }
      if (place_icon(bind.enabled ? ICON_MD_TOGGLE_ON : ICON_MD_TOGGLE_OFF)) {
        bind.enabled = !bind.enabled;
        if (selected_id == bind.id) draft.enabled = bind.enabled;
        pup_bind::mark_dirty();
      }

      if (row_clicked) {
        clicked_bind = true;
        if (!picking_parent) {
          selected_id = bind.id;
          draft = bind;
        } else {
          picking_parent = 0;
          if (selected_id == 0 || !pup_bind::would_cycle_parent(selected_id, bind.id)) draft.parent_id = bind.id;
          else draft.parent_id = 0;
        }
      } else if (row_right) {
        ImGui::OpenPopup("RightClicked");
      } else if (dragging_id == 0 && row_hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        dragging_id = bind.id;
        dragging_parent = bind.parent_id;
      }

      if (pressed_delete) {
        if ((bind.overrides.empty() && !pup_bind::has_children(bind.id)) || ImGui::GetIO().KeyShift) {
          delete_id = bind.id;
        } else {
          ImGui::OpenPopup("DeleteBind");
        }
      }

      if (ImGui::BeginPopup("RightClicked")) {
        static uint32_t rename_id{};
        static std::string rename{};
        if (rename_id != bind.id) {
          rename_id = bind.id;
          rename = bind.name;
        }
        bool changed = false;
        if (mono::input_string("Name", &rename)) {
          bind.name = rename;
          changed = true;
        }
        changed = draw_condition_fields(bind) || changed;
        if (bind.condition == pup_bind::bind_condition::key && pup_menu::input_key("Key", &bind.key)) changed = true;
        if (changed) {
          if (selected_id == bind.id) copy_fields(draft, bind);
          pup_bind::mark_dirty();
        }
        ImGui::EndPopup();
      }
      if (ImGui::BeginPopupModal("DeleteBind", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Do you really want to delete '%s'%s?", bind.name.c_str(), pup_bind::has_children(bind.id) ? " and all of its children" : "");
        if (mono::button("Yes", { 80.0f, 22.0f })) {
          delete_id = bind.id;
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (mono::button("No", { 80.0f, 22.0f })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }

      ImGui::SetCursorScreenPos({ row_min.x, row_min.y + height + 6.0f });
      ImGui::Dummy(ImVec2(0.0f, 0.0f));
      ImGui::PopID();
      if (depth < 32) draw_tree(bind.id, depth + 1);
    }
  };
  draw_tree(0, 0);

  std::vector<uint32_t> dangling{};
  for (const pup_bind::bind_entry& bind : pup_bind::entries()) {
    if (bind.parent_id != 0 && pup_bind::find_entry(bind.parent_id) == nullptr) dangling.push_back(bind.id);
  }
  if (!dangling.empty()) {
    ImGui::TextDisabled("Dangling");
    for (const uint32_t id : dangling) {
      if (pup_bind::bind_entry* bind = pup_bind::find_entry(id)) bind->parent_id = 0;
    }
    pup_bind::mark_dirty();
  }

  if (dragging_id != 0) {
    const float mouse_y = ImGui::GetIO().MousePos.y;
    for (const bind_row_hit& hit : hits) {
      if (hit.parent == dragging_parent && hit.id != dragging_id && mouse_y >= hit.min_y && mouse_y < hit.max_y) {
        pup_bind::move(dragging_id, hit.id);
        break;
      }
    }
  }

  if (delete_id != 0) {
    if (selected_id == delete_id) reset_form();
    if (pup_bind::editing() == delete_id) pup_bind::set_editing(0);
    pup_bind::remove(delete_id);
  }

  if (picking_parent == 2) picking_parent = 1;
  else if (picking_parent && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !clicked_bind) {
    picking_parent = 0;
    draft.parent_id = 0;
  }

  if (pup_bind::bind_entry* selected = pup_bind::find_entry(selected_id)) {
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::TextDisabled("Feature overrides");
    if (selected->overrides.empty()) {
      ImGui::TextDisabled("No feature values assigned. Click the edit icon, then click a setting.");
    }
    std::string override_to_clear{};
    for (const auto& [target_key, value] : selected->overrides) {
      const pup_bind::target_entry* target = pup_bind::find_target(target_key);
      ImGui::BulletText("%s = %s", target != nullptr ? target->label.c_str() : target_key.c_str(), pup_bind::format_bind_value(value).c_str());
      ImGui::SameLine();
      ImGui::PushID(target_key.c_str());
      if (ImGui::SmallButton("clear")) override_to_clear = target_key;
      ImGui::PopID();
    }
    if (!override_to_clear.empty()) pup_bind::clear_override(selected->id, override_to_clear);
    if (pup_bind::editing() == selected->id) {
      if (mono::button("done editing", { 160.0f, 22.0f })) pup_bind::set_editing(0);
    }
  }
  mono::end_panel();
}

enum puphook_tab_id
{
  puphook_tab_aimbot,
  puphook_tab_automation,
  puphook_tab_exploits,
  puphook_tab_visuals,
  puphook_tab_misc,
  puphook_tab_binds,
  puphook_tab_settings
};

static void draw_settings_content(const puphook_tab_id tab, const int section, const int settings_section) {
  enforce_insider_settings_lock(config);

  switch (tab) {
  case puphook_tab_aimbot:    draw_combat_tab(section); break;
  case puphook_tab_automation: draw_automation_tab(section); break;
  case puphook_tab_exploits:  draw_exploits_content(); break;
  case puphook_tab_visuals:   draw_visuals_tab(section); break;
  case puphook_tab_misc:
    draw_movement_content();
    break;
  case puphook_tab_binds:     draw_binds_content(); break;
  case puphook_tab_settings:  draw_system_tab(settings_section); break;
  }
}

static void warmup_bind_targets()
{
  visual_groups::ensure_defaults();
  if (pup_bind::disabled() || pup_bind::targets_warmed() || ImGui::GetCurrentContext() == nullptr) return;

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

  if (!ImGui::Begin("##puphook_bind_target_warmup", nullptr, flags)) {
    ImGui::End();
    return;
  }

  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);
  pup_bind::target_registration_scope registration{};

  draw_combat_tab(pup_menu::combat_subtab_aimbot);
  draw_combat_tab(pup_menu::combat_subtab_weapons);
  draw_combat_tab(pup_menu::combat_subtab_overlay);
  draw_automation_tab(pup_menu::automation_subtab_general);
  draw_automation_tab(pup_menu::automation_subtab_queue);
  draw_automation_tab(pup_menu::automation_subtab_chat);
  draw_automation_tab(pup_menu::automation_subtab_items);
  draw_automation_tab(pup_menu::automation_subtab_navbot);
  draw_automation_tab(pup_menu::automation_subtab_medic);
  draw_automation_tab(pup_menu::automation_subtab_ipc);
  draw_exploits_content();
  draw_visuals_tab(pup_menu::visuals_subtab_profiles);
  draw_visuals_tab(pup_menu::visuals_subtab_world);
  draw_visuals_tab(pup_menu::visuals_subtab_hud);
  draw_visuals_tab(pup_menu::visuals_subtab_models);
  draw_movement_content();

  int& selected_group = pup_menu::selected_visual_group();
  const int previous_group = selected_group;
  const int group_count = static_cast<int>(config.visual_groups.groups.size());
  for (int index = 0; index < group_count; ++index) {
    selected_group = index;
    ImGui::PushID(index);
    draw_visuals_tab(pup_menu::visuals_subtab_profiles);
    ImGui::PopID();
  }
  selected_group = previous_group;

  ImGui::PopStyleVar();
  ImGui::End();
  pup_bind::targets_warmed() = true;
}

static void draw_menu(void) {
  pup_bind::set_menu_open(menu_focused || player_manager_window_open);
  set_imgui_theme();

  static mono::menu_state menu_state{};
  static mono::menu_state player_manager_state{};
  static puphook_tab_id tab{ puphook_tab_aimbot };
  static int aimbot_section{};
  static int automation_section{};
  static int visuals_section{};
  static int settings_section{};

  const auto navbar_entry = [](const char* const label, const bool active, const char* const icon = nullptr) {
    return mono::navbar_entry(label, active, icon, pup_menu::font_icons());
  };
  const auto subnavbar_entry = [](const char* const label, const bool active) {
    return mono::subnavbar_entry(label, active, nullptr, pup_menu::font_icons());
  };

  if (menu_focused) {
  const std::string editing_name = pup_bind::editing_name();
  const bool visible = mono::begin_menu(
    {
      .id = "monolilth",
      .title = "puphook",
      .preferred_size = pup_menu::k_menu_size,
      .navbar_height = 42.0f,
      .subnavbar_height = 32.0f,
      .show_subnavbar = tab != puphook_tab_exploits && tab != puphook_tab_binds && tab != puphook_tab_misc,
      .viewport_padding = 8.0f,
      .background_alpha = 0.80f
    },
    menu_state,
    [&] {
      auto select = [&](const puphook_tab_id value, const char* const label, const char* const icon) {
        if (navbar_entry(label, tab == value, icon) && tab != value) {
          tab = value;
          menu_state.reset_scroll = true;
        }
      };

      select(puphook_tab_aimbot, "Combat", ICON_MD_GPS_FIXED);
      select(puphook_tab_automation, "Automation", ICON_MD_AUTORENEW);
      select(puphook_tab_exploits, "Exploits", ICON_MD_BOLT);
      select(puphook_tab_visuals, "Visuals", ICON_MD_VISIBILITY);
      select(puphook_tab_misc, "Movement", ICON_MD_DIRECTIONS_RUN);
      select(puphook_tab_binds, "Binds", ICON_MD_VPN_KEY);
      select(puphook_tab_settings, "Settings", ICON_MD_SETTINGS);
    },
    [&] {
      const auto select_section = [&](int& current, const int value, const char* const label) {
        if (subnavbar_entry(label, current == value) && current != value) {
          current = value;
          menu_state.reset_scroll = true;
        }
      };
      switch (tab) {
      case puphook_tab_aimbot:
        select_section(aimbot_section, pup_menu::combat_subtab_aimbot, "Aimbot");
        select_section(aimbot_section, pup_menu::combat_subtab_weapons, "Weapons");
        select_section(aimbot_section, pup_menu::combat_subtab_overlay, "Overlay");
        break;
      case puphook_tab_automation:
        select_section(automation_section, pup_menu::automation_subtab_general, "General");
        select_section(automation_section, pup_menu::automation_subtab_queue, "Queue");
        select_section(automation_section, pup_menu::automation_subtab_chat, "Chat");
        select_section(automation_section, pup_menu::automation_subtab_items, "Items");
        select_section(automation_section, pup_menu::automation_subtab_navbot, "Navbot");
        select_section(automation_section, pup_menu::automation_subtab_medic, "Medic");
        select_section(automation_section, pup_menu::automation_subtab_ipc, "IPC");
        break;
      case puphook_tab_visuals:
        select_section(visuals_section, pup_menu::visuals_subtab_profiles, "Profiles");
        select_section(visuals_section, pup_menu::visuals_subtab_world, "World");
        select_section(visuals_section, pup_menu::visuals_subtab_hud, "HUD");
        select_section(visuals_section, pup_menu::visuals_subtab_models, "Models");
        break;
      case puphook_tab_settings:
        select_section(settings_section, 0, "Configurations");
        select_section(settings_section, 1, "Materials");
        select_section(settings_section, 2, "Interface");
        break;
      case puphook_tab_misc:
      case puphook_tab_exploits:
      case puphook_tab_binds:
        break;
      }
    },
    {
      .label = "editing bind",
      .value = editing_name,
      .action = "done",
      .on_action = [] { pup_bind::set_editing(0); }
    });

  if (visible) {
    draw_settings_content(tab, tab == puphook_tab_visuals ? visuals_section : tab == puphook_tab_aimbot ? aimbot_section :
      tab == puphook_tab_automation ? automation_section : 0, settings_section);
    pup_bind::draw_popup();
  }
  mono::end_menu(visible);
  }

  if (player_manager_window_open) {
    if (!player_manager_state.position_initialized) {
      player_manager_state.position = {
        menu_state.position.x + pup_menu::k_menu_size.x + 24.0f,
        menu_state.position.y
      };
    }
    const bool player_visible = mono::begin_menu(
      {
        .id = "puphook_player_manager##mono_menu",
        .title = "Player Manager",
        .preferred_size = { 720.0f, 540.0f },
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
          pup_bind::set_menu_open(menu_focused);
          if (!menu_focused && surface != nullptr) {
            surface->set_cursor_visible(false);
          }
        }
      });
    if (player_visible) pup_menu::draw_player_window_content();
    mono::end_menu(player_visible);
  }
}
#endif
