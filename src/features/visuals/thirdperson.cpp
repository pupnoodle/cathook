/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/visuals/thirdperson.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "features/visuals/thirdperson.hpp"
#include "imgui/imgui.h"
#include "core/math/math.hpp"
#include "features/combat/anti_aim/anti_aim.hpp"
#include "features/automation/navbot/navbot_controller.hpp"
#include "features/menu/config.hpp"
#include "features/visuals/overlay_projection.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/convar_system.hpp"
#include "games/tf2/sdk/interfaces/engine_trace.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "games/tf2/sdk/interfaces/input.hpp"

namespace
{

constexpr int tf_stun_controls = 1 << 1;
constexpr int tf_stun_loser_state = 1 << 6;
constexpr float thirdperson_trace_length = 8192.0f;
constexpr float thirdperson_crosshair_size = 5.0f;

bool was_forcing_taunt_camera = false;

struct render_angle_state
{
  Player* player = nullptr;
  Vec3 original_angles{};
  Vec3 original_local_angles{};
  bool active = false;
};

render_angle_state render_angles{};

[[nodiscard]] Player* get_localplayer()
{
  if (entity_list == nullptr) {
    return nullptr;
  }

  return entity_list->get_localplayer();
}

[[nodiscard]] bool should_keep_game_taunt_camera(Player* localplayer)
{
  if (localplayer == nullptr) {
    return false;
  }

  const bool forced_stun = localplayer->in_cond(TF_COND_STUNNED)
      && (localplayer->get_stun_flags() & (tf_stun_controls | tf_stun_loser_state)) != 0;

  return localplayer->in_cond(TF_COND_TAUNTING)
      || localplayer->in_cond(TF_COND_HALLOWEEN_GHOST_MODE)
      || localplayer->in_cond(TF_COND_HALLOWEEN_KART)
      || forced_stun;
}

[[nodiscard]] bool is_enabled_for(Player* localplayer)
{
  return localplayer != nullptr && localplayer->is_alive() && config.visuals.thirdperson.enabled;
}

void set_cam_ideallag_zero()
{
  if (convar_system == nullptr) {
    return;
  }

  static Convar* cam_ideallag = convar_system->find_var("cam_ideallag");
  if (cam_ideallag != nullptr && cam_ideallag->get_float() != 0.0f) {
    cam_ideallag->set_float(0.0f);
  }
}

void apply_camera_offset(Player* localplayer, view_setup* setup)
{
  Vec3 forward{};
  Vec3 right{};
  Vec3 up{};
  angle_vectors(setup->angles, &forward, &right, &up);

  const float scale = config.visuals.thirdperson.scale ? localplayer->get_model_scale() : 1.0f;

  Vec3 offset{};
  offset += right * (config.visuals.thirdperson.right * scale);
  offset += up * (config.visuals.thirdperson.up * scale);
  offset -= forward * (config.visuals.thirdperson.distance * scale);

  const Vec3 start = localplayer->get_shoot_pos();
  Vec3 end = start + offset;

  if (config.visuals.thirdperson.collision && engine_trace != nullptr) {
    const float hull = 9.0f * scale;
    Vec3 mins{-hull, -hull, -hull};
    Vec3 maxs{hull, hull, hull};
    trace_t trace{};
    engine_trace->trace_hull(const_cast<Vec3*>(&start), &end, &mins, &maxs, MASK_SOLID, &trace);
    end = trace.endpos;
  }

  setup->origin = end;
}

}

namespace thirdperson
{

bool should_draw_local_player()
{
  auto* localplayer = get_localplayer();
  return is_enabled_for(localplayer) || should_keep_game_taunt_camera(localplayer);
}

void update_taunt_camera()
{
  auto* localplayer = get_localplayer();
  const bool force_thirdperson = is_enabled_for(localplayer) || should_keep_game_taunt_camera(localplayer);

  if (localplayer == nullptr) {
    was_forcing_taunt_camera = false;
    return;
  }

  if (input != nullptr) {
    if (force_thirdperson) {
      input->to_thirdperson();
    } else {
      input->to_firstperson();
    }
  }

  localplayer->set_taunt_cam(force_thirdperson);

  was_forcing_taunt_camera = force_thirdperson;
}

void update_camera(view_setup* setup)
{
  if (setup == nullptr) {
    return;
  }

  auto* localplayer = get_localplayer();
  if (!is_enabled_for(localplayer) && !should_keep_game_taunt_camera(localplayer)) {
    return;
  }

  set_cam_ideallag_zero();
  apply_camera_offset(localplayer, setup);
}

void begin_render_angles()
{
  if (render_angles.active) {
    end_render_angles();
  }

  auto* localplayer = get_localplayer();
  if (!is_enabled_for(localplayer) || localplayer->in_cond(TF_COND_TAUNTING)) {
    return;
  }

  const bool has_anti_aim_angles = anti_aim::has_visual_angles();
  const bool has_silent_path_angles = navbot::controller().has_silent_path_look();
  if (!has_anti_aim_angles && !has_silent_path_angles) {
    return;
  }

  render_angles.player = localplayer;
  render_angles.original_angles = localplayer->get_eye_angles();
  render_angles.original_local_angles = localplayer->get_local_eye_angles();
  render_angles.active = true;

  Vec3 visible_angles = has_anti_aim_angles
    ? anti_aim::real_angles()
    : navbot::controller().silent_path_look_angles();
  visible_angles.z = 0.0f;
  localplayer->set_eye_angles(visible_angles);
  localplayer->set_local_eye_angles(visible_angles);
}

void end_render_angles()
{
  if (!render_angles.active) {
    return;
  }

  if (render_angles.player != nullptr) {
    render_angles.player->set_eye_angles(render_angles.original_angles);
    render_angles.player->set_local_eye_angles(render_angles.original_local_angles);
  }

  render_angles = {};
}

void draw_crosshair_imgui()
{
  auto* localplayer = get_localplayer();
  if (!config.visuals.thirdperson.crosshair || !is_enabled_for(localplayer)) {
    return;
  }

  view_setup local_view{};
  if (client == nullptr || !client->get_player_view(local_view)) {
    return;
  }

  Vec3 forward{};
  angle_vectors(local_view.angles, &forward, nullptr, nullptr);

  Vec3 start = localplayer->get_shoot_pos();
  Vec3 end = start + (forward * thirdperson_trace_length);
  Vec3 draw_position = end;

  if (engine_trace != nullptr) {
    trace_t trace{};
    ray_t ray = engine_trace->init_ray(&start, &end);
    trace_filter filter{};
    engine_trace->init_trace_filter(&filter, localplayer);
    engine_trace->trace_ray(&ray, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);
    draw_position = trace.endpos;
  }

  Vec3 screen{};
  if (!overlay_projection::begin_frame() || !overlay_projection::world_to_screen(draw_position, &screen)) {
    return;
  }

  auto* draw_list = ImGui::GetWindowDrawList();
  if (draw_list == nullptr) {
    return;
  }

  const ImVec2 center{screen.x, screen.y};
  draw_list->AddLine(ImVec2(center.x - thirdperson_crosshair_size, center.y),
                     ImVec2(center.x + thirdperson_crosshair_size, center.y),
                     IM_COL32(0, 0, 0, 220),
                     3.0f);
  draw_list->AddLine(ImVec2(center.x, center.y - thirdperson_crosshair_size),
                     ImVec2(center.x, center.y + thirdperson_crosshair_size),
                     IM_COL32(0, 0, 0, 220),
                     3.0f);
  draw_list->AddLine(ImVec2(center.x - thirdperson_crosshair_size, center.y),
                     ImVec2(center.x + thirdperson_crosshair_size, center.y),
                     IM_COL32(255, 255, 255, 255),
                     1.25f);
  draw_list->AddLine(ImVec2(center.x, center.y - thirdperson_crosshair_size),
                     ImVec2(center.x, center.y + thirdperson_crosshair_size),
                     IM_COL32(255, 255, 255, 255),
                     1.25f);
}

}
