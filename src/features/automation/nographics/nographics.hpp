/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/automation/nographics/nographics.hpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#ifndef NOGRAPHICS_HPP
#define NOGRAPHICS_HPP

namespace nographics
{

inline int hud_throttle_frames = 4;
inline bool aggressive_material_block = true;
inline bool textmode_allow_engine_sleep = false;

void initialize();
void prepare_startup_patches();
void prepare_render_patches();
void on_library_loaded(const char* library_path);
void update();
void shutdown();
bool is_enabled();
bool should_skip_rendering_hooks();
bool is_noshaderapi();
const char* redirect_shaderapi_path(const char* library_path);

}
#endif
