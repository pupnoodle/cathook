/*
/^-----^\   data: 2026-03-30
V  o o  V  file: src/features/visuals/esp/esp.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef ESP_HPP
#define ESP_HPP

#include "imgui/imgui.h"

void draw_players_imgui();
void draw_backtrack_visualizer_imgui();
void draw_aimbot_fov_imgui();
void draw_anti_aim_indicator_imgui();
void draw_thirdperson_crosshair_imgui();
void update_player_head_emoji_cache();
void reset_esp_runtime_state();

bool atlas_texture_ready();
void draw_shared_atlas_tile(
  ImDrawList* draw_list,
  int tile_column,
  int tile_row,
  const ImVec2& center,
  float size,
  ImU32 tint = IM_COL32_WHITE);

#endif
