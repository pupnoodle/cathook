/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/features/menu/binds.hpp
 |  Y  |   autor: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#ifndef BINDS_HPP
#define BINDS_HPP

#include "config.hpp"
#include "core/config/config_store.hpp"
#include "imgui/dearimgui.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace cat_bind
{

enum class value_type
{
  boolean,
  integer,
  floating
};

enum class widget_type
{
  checkbox,
  combo_int,
  slider_int,
  slider_float
};

enum class indicator_kind
{
  value,
  aimbot
};

using bind_value = std::variant<bool, int, float>;

struct bind_entry
{
  std::string target_key{};
  std::string label{};
  std::string default_label{};
  void* target = nullptr;
  value_type type = value_type::boolean;
  widget_type widget = widget_type::checkbox;
  button trigger{};
  bool enabled = true;
  bool show_in_indicator = true;
  bool active = false;
  bool has_default = false;
  bool has_override = false;
  bind_value default_value = false;
  bind_value override_value = false;
  int int_min = 0;
  int int_max = 0;
  float float_min = 0.0f;
  float float_max = 0.0f;
  std::string format{};
  std::vector<std::pair<std::string, int>> options{};
};

struct button_entry
{
  std::string target_key{};
  std::string label{};
  std::string default_label{};
  button* target = nullptr;
  indicator_kind kind = indicator_kind::value;
  bool show_in_indicator = true;
};

enum class popup_target_type
{
  value_bind,
  button_bind
};

struct indicator_row
{
  std::string label{};
  std::string key{};
  std::string state{};
  std::string target_key{};
  popup_target_type popup_type = popup_target_type::value_bind;
  bool active = false;
};

inline std::vector<bind_entry>& entries() {
  static std::vector<bind_entry> value{};
  return value;
}

inline std::unordered_map<void*, std::string>& pointer_to_key() {
  static std::unordered_map<void*, std::string> value{};
  return value;
}

inline std::unordered_map<std::string, void*>& key_to_pointer() {
  static std::unordered_map<std::string, void*> value{};
  return value;
}

inline std::vector<button_entry>& button_entries() {
  static std::vector<button_entry> value{};
  return value;
}

inline bool& targets_registered() {
  static bool value = false;
  return value;
}

inline bool& button_targets_registered() {
  static bool value = false;
  return value;
}

inline std::string& popup_target_key() {
  static std::string value{};
  return value;
}

inline bool& popup_open_requested() {
  static bool value = false;
  return value;
}

inline popup_target_type& popup_target_type_value() {
  static popup_target_type value = popup_target_type::value_bind;
  return value;
}

inline std::string& popup_label_target_key() {
  static std::string value{};
  return value;
}

inline std::array<char, 128>& popup_label_buffer() {
  static std::array<char, 128> value{};
  return value;
}

inline bind_entry* find_entry(const std::string& target_key) {
  for (bind_entry& entry : entries()) {
    if (entry.target_key == target_key) {
      return &entry;
    }
  }

  return nullptr;
}

inline button_entry* find_button_entry(const std::string& target_key) {
  for (button_entry& entry : button_entries()) {
    if (entry.target_key == target_key) {
      return &entry;
    }
  }

  return nullptr;
}

inline value_type get_value_type(bool*) { return value_type::boolean; }
inline value_type get_value_type(int*) { return value_type::integer; }
inline value_type get_value_type(float*) { return value_type::floating; }

inline bind_value read_value(void* target, value_type type) {
  switch (type) {
  case value_type::boolean:
    return *static_cast<bool*>(target);
  case value_type::integer:
    return *static_cast<int*>(target);
  case value_type::floating:
    return *static_cast<float*>(target);
  }

  return false;
}

inline void write_value(void* target, value_type type, const bind_value& value) {
  switch (type) {
  case value_type::boolean:
    *static_cast<bool*>(target) = std::get<bool>(value);
    break;
  case value_type::integer:
    *static_cast<int*>(target) = std::get<int>(value);
    break;
  case value_type::floating:
    *static_cast<float*>(target) = std::get<float>(value);
    break;
  }
}

inline const char* mode_label(const button::mode_type mode) {
  switch (mode) {
  case button::mode_type::HOLD:
    return "hold";
  case button::mode_type::TOGGLE:
    return "toggle";
  case button::mode_type::DOUBLE_CLICK:
    return "double";
  }

  return "hold";
}

template <typename value_t>
inline void register_target(const char* target_key, const char* label, value_t* target) {
  pointer_to_key()[target] = target_key;
  key_to_pointer()[target_key] = target;

  if (bind_entry* entry = find_entry(target_key)) {
    entry->target = target;
    if (entry->label.empty() || entry->label == entry->default_label) {
      entry->label = label;
    }
    entry->default_label = label;
    entry->type = get_value_type(target);
  }
}

inline void register_button_target(
  const char* target_key,
  const char* label,
  button* target,
  const indicator_kind kind,
  const bool show_in_indicator = true) {
  if (button_entry* entry = find_button_entry(target_key)) {
    entry->target = target;
    if (entry->label.empty() || entry->label == entry->default_label) {
      entry->label = label;
    }
    entry->default_label = label;
    entry->kind = kind;
    entry->show_in_indicator = show_in_indicator;
    return;
  }

  button_entries().push_back({
    .target_key = target_key,
    .label = label,
    .default_label = label,
    .target = target,
    .kind = kind,
    .show_in_indicator = show_in_indicator
  });
}

inline void register_builtin_targets() {
  if (targets_registered()) {
    return;
  }

  targets_registered() = true;

  register_target("aimbot.master", "Enable", &config.aimbot.master);
  register_target("aimbot.auto_shoot", "Auto shoot", &config.aimbot.auto_shoot);
  register_target("aimbot.draw_fov", "Draw FOV", &config.aimbot.draw_fov);
  register_target("aimbot.shoot_through_glass", "Shoot through glass", &config.aimbot.shoot_through_glass);
  register_target("aimbot.aim_mode", "Aim mode", reinterpret_cast<int*>(&config.aimbot.aim_mode));
  register_target("aimbot.fov", "Aim FOV", &config.aimbot.fov);
  register_target("aimbot.smooth_factor", "Smooth factor", &config.aimbot.smooth_factor);
  register_target("aimbot.assist_strength", "Assist strength", &config.aimbot.assist_strength);
  register_target("aimbot.target_type", "Target", reinterpret_cast<int*>(&config.aimbot.target_type));
  register_target("aimbot.projectile_mode", "Projectile mode", reinterpret_cast<int*>(&config.aimbot.projectile_mode));
  register_target("aimbot.melee_walk_to_target", "Melee walk to target", &config.aimbot.melee_walk_to_target);
  register_target("aimbot.projectile_wall_splash", "Wall splash", &config.aimbot.projectile_wall_splash);
  register_target("aimbot.projectile_seam_shot", "Seam shot", &config.aimbot.projectile_seam_shot);
  register_target("aimbot.projectile_splash_radius_scale", "Splash radius scale", &config.aimbot.projectile_splash_radius_scale);
  register_target("aimbot.projectile_path_steps", "Projectile path steps", &config.aimbot.projectile_path_steps);
  register_target("aimbot.projectile_splash_samples", "Splash samples", &config.aimbot.projectile_splash_samples);
  register_target("aimbot.projectile_prediction_ticks", "Projectile prediction ticks", &config.aimbot.projectile_prediction_ticks);
  register_target("aimbot.projectile_strafe_prediction", "Projectile strafe prediction", &config.aimbot.projectile_strafe_prediction);
  register_target("aimbot.projectile_strafe_confidence", "Projectile strafe confidence", &config.aimbot.projectile_strafe_confidence);
  register_target("aimbot.projectile_trace_interval", "Projectile trace interval", &config.aimbot.projectile_trace_interval);
  register_target("aimbot.projectile_splash_debug", "Projectile splash debug", &config.aimbot.projectile_splash_debug);
  register_target("aimbot.ignore_friends", "Ignore friends", &config.aimbot.ignore_friends);
  register_target("aimbot.auto_rev", "Heavy auto rev", &config.aimbot.auto_rev);
  register_target("aimbot.auto_unrev", "Heavy auto unrev", &config.aimbot.auto_unrev);
  register_target("aimbot.auto_rev_threshold", "Heavy rev threshold", &config.aimbot.auto_rev_threshold);
  register_target("aimbot.auto_scope", "Sniper auto scope", &config.aimbot.auto_scope);
  register_target("aimbot.auto_unscope", "Sniper auto unscope", &config.aimbot.auto_unscope);
  register_target("aimbot.auto_scope_threshold", "Auto scope threshold", &config.aimbot.auto_scope_threshold);
  register_target("aimbot.scoped_only", "Scoped only", &config.aimbot.scoped_only);
  register_target("aimbot.wait_for_headshot", "Wait for headshot", &config.aimbot.wait_for_headshot);

  register_target("esp.master", "Enable", &config.esp.master);
  register_target("esp.player.box", "Box", &config.esp.player.box);
  register_target("esp.player.box_style", "Box type", reinterpret_cast<int*>(&config.esp.player.box_style));
  register_target("esp.player.health_bar", "Health bar", &config.esp.player.health_bar);
  register_target("esp.player.name", "Name", &config.esp.player.name);
  register_target("esp.player.class_icon", "Class icon", &config.esp.player.class_icon);
  register_target("esp.player.class_icon_scale", "CI scale", &config.esp.player.class_icon_scale);
  register_target("esp.player.class_icon_teammates", "Class icon team", &config.esp.player.class_icon_teammates);
  register_target("esp.player.head_emoji", "Head emoji", &config.esp.player.head_emoji);
  register_target("esp.player.head_emoji_scale", "Emoji scale", &config.esp.player.head_emoji_scale);
  register_target("esp.player.head_emoji_style", "Emoji style", &config.esp.player.head_emoji_style);
  register_target("esp.player.head_emoji_teammates", "Emoji team", &config.esp.player.head_emoji_teammates);
  register_target("esp.player.flags.target_indicator", "Target flag", &config.esp.player.flags.target_indicator);
  register_target("esp.player.flags.friend_indicator", "Friend flag", &config.esp.player.flags.friend_indicator);
  register_target("esp.player.flags.scoped_indicator", "Scoped flag", &config.esp.player.flags.scoped_indicator);
  register_target("esp.player.enemy", "Enemy", &config.esp.player.enemy);
  register_target("esp.player.team", "Team", &config.esp.player.team);
  register_target("esp.player.friends", "Friends", &config.esp.player.friends);
  register_target("esp.pickup.box", "Pickup box", &config.esp.pickup.box);
  register_target("esp.pickup.box_style", "Pickup box type", reinterpret_cast<int*>(&config.esp.pickup.box_style));
  register_target("esp.pickup.name", "Pickup name", &config.esp.pickup.name);
  register_target("esp.intelligence.box", "Intel box", &config.esp.intelligence.box);
  register_target("esp.intelligence.box_style", "Intel box type", reinterpret_cast<int*>(&config.esp.intelligence.box_style));
  register_target("esp.intelligence.name", "Intel name", &config.esp.intelligence.name);
  register_target("esp.buildings.box", "Buildings box", &config.esp.buildings.box);
  register_target("esp.buildings.box_style", "Buildings box type", reinterpret_cast<int*>(&config.esp.buildings.box_style));
  register_target("esp.buildings.health_bar", "Buildings health", &config.esp.buildings.health_bar);
  register_target("esp.buildings.name", "Buildings name", &config.esp.buildings.name);
  register_target("esp.buildings.team", "Buildings team", &config.esp.buildings.team);

  register_target("glow.master", "Enable glow", &config.glow.master);
  register_target("glow.outline_scale", "Glow outline size", &config.glow.outline_scale);
  register_target("glow.blur_scale", "Glow blur strength", &config.glow.blur_scale);
  register_target("glow.start", "Glow fade near", &config.glow.start);
  register_target("glow.end", "Glow fade far", &config.glow.end);
  register_target("glow.smooth_alpha", "Glow distance fade", &config.glow.smooth_alpha);
  register_target("glow.filled_body", "Glow filled body", &config.glow.filled_body);
  register_target("glow.player.enemy", "Glow enemy", &config.glow.player.enemy);
  register_target("glow.player.team", "Glow team", &config.glow.player.team);
  register_target("glow.player.friends", "Glow friends", &config.glow.player.friends);
  register_target("glow.player.local", "Glow local", &config.glow.player.local);

  register_target("chams.master", "Enable", &config.chams.master);
  register_target("chams.player.enemy", "Enemy", &config.chams.player.enemy);
  register_target("chams.player.enemy_material_type", "Enemy material", reinterpret_cast<int*>(&config.chams.player.enemy_material_type));
  register_target("chams.player.enemy_material_z_type", "Enemy z material", reinterpret_cast<int*>(&config.chams.player.enemy_material_z_type));
  register_target("chams.player.enemy_flags.ignore_z", "Enemy ignore z", &config.chams.player.enemy_flags.ignore_z);
  register_target("chams.player.team", "Team", &config.chams.player.team);
  register_target("chams.player.team_material_type", "Team material", reinterpret_cast<int*>(&config.chams.player.team_material_type));
  register_target("chams.player.team_material_z_type", "Team z material", reinterpret_cast<int*>(&config.chams.player.team_material_z_type));
  register_target("chams.player.team_flags.ignore_z", "Team ignore z", &config.chams.player.team_flags.ignore_z);
  register_target("chams.player.friends", "Friends", &config.chams.player.friends);
  register_target("chams.player.friend_material_type", "Friend material", reinterpret_cast<int*>(&config.chams.player.friend_material_type));
  register_target("chams.player.friend_material_z_type", "Friend z material", reinterpret_cast<int*>(&config.chams.player.friend_material_z_type));
  register_target("chams.player.friends_flags.ignore_z", "Friend ignore z", &config.chams.player.friends_flags.ignore_z);
  register_target("chams.player.local", "Local", &config.chams.player.local);
  register_target("chams.player.local_material_type", "Local material", reinterpret_cast<int*>(&config.chams.player.local_material_type));
  register_target("chams.player.enemy_overlay_material_type", "Enemy overlay material", reinterpret_cast<int*>(&config.chams.player.enemy_overlay_material_type));
  register_target("chams.player.enemy_overlay_material_z_type", "Enemy overlay z material", reinterpret_cast<int*>(&config.chams.player.enemy_overlay_material_z_type));
  register_target("chams.player.enemy_overlay_flags.ignore_z", "Enemy overlay ignore z", &config.chams.player.enemy_overlay_flags.ignore_z);

  register_target("visuals.removals.scope", "Remove scope", &config.visuals.removals.scope);
  register_target("visuals.removals.zoom", "Remove zoom", &config.visuals.removals.zoom);
  register_target("visuals.thirdperson.enabled", "Thirdperson", &config.visuals.thirdperson.enabled);
  register_target("visuals.thirdperson.crosshair", "Thirdperson crosshair", &config.visuals.thirdperson.crosshair);
  register_target("visuals.thirdperson.distance", "Thirdperson distance", &config.visuals.thirdperson.distance);
  register_target("visuals.thirdperson.right", "Thirdperson right", &config.visuals.thirdperson.right);
  register_target("visuals.thirdperson.up", "Thirdperson up", &config.visuals.thirdperson.up);
  register_target("visuals.thirdperson.scale", "Thirdperson scale", &config.visuals.thirdperson.scale);
  register_target("visuals.thirdperson.collision", "Thirdperson collision", &config.visuals.thirdperson.collision);
  register_target("visuals.override_fov", "Override FOV", &config.visuals.override_fov);
  register_target("visuals.custom_fov", "Custom FOV", &config.visuals.custom_fov);
  register_target("visuals.override_viewmodel_fov", "Override viewmodel FOV", &config.visuals.override_viewmodel_fov);
  register_target("visuals.custom_viewmodel_fov", "Viewmodel FOV", &config.visuals.custom_viewmodel_fov);

  register_target("misc.movement.bhop", "Bhop", &config.misc.movement.bhop);
  register_target("misc.movement.auto_strafe", "Auto strafe", reinterpret_cast<int*>(&config.misc.movement.auto_strafe));
  register_target("misc.movement.auto_strafe_turn_scale", "Strafe turn scale", &config.misc.movement.auto_strafe_turn_scale);
  register_target("misc.movement.auto_strafe_max_delta", "Strafe max delta", &config.misc.movement.auto_strafe_max_delta);
  register_target("misc.movement.no_push", "No push", &config.misc.movement.no_push);
  register_target("misc.movement.taunt_slide", "Taunt slide", &config.misc.movement.taunt_slide);
  register_target("misc.exploits.tickbase", "Tickbase", &config.misc.exploits.tickbase);
  register_target("misc.exploits.tickbase_recharge", "Tickbase recharge", &config.misc.exploits.tickbase_recharge);
  register_target("misc.exploits.doubletap", "Doubletap", &config.misc.exploits.doubletap);
  register_target("misc.exploits.doubletap_ticks", "Doubletap ticks", &config.misc.exploits.doubletap_ticks);
  register_target("misc.exploits.warp", "Warp", &config.misc.exploits.warp);
  register_target("misc.exploits.warp_ticks", "Warp ticks", &config.misc.exploits.warp_ticks);
  register_target("misc.exploits.fakelag", "Fakelag", &config.misc.exploits.fakelag);
  register_target("misc.exploits.fakelag_ticks", "Fakelag ticks", &config.misc.exploits.fakelag_ticks);
  register_target("misc.exploits.anti_aim", "Anti-aim", &config.misc.exploits.anti_aim);
  register_target("misc.exploits.anti_aim_real_pitch", "AA real pitch", reinterpret_cast<int*>(&config.misc.exploits.anti_aim_real_pitch));
  register_target("misc.exploits.anti_aim_fake_pitch", "AA fake pitch", reinterpret_cast<int*>(&config.misc.exploits.anti_aim_fake_pitch));
  register_target("misc.exploits.anti_aim_real_yaw_base", "AA real base", reinterpret_cast<int*>(&config.misc.exploits.anti_aim_real_yaw_base));
  register_target("misc.exploits.anti_aim_fake_yaw_base", "AA fake base", reinterpret_cast<int*>(&config.misc.exploits.anti_aim_fake_yaw_base));
  register_target("misc.exploits.anti_aim_real_yaw", "AA real yaw", reinterpret_cast<int*>(&config.misc.exploits.anti_aim_real_yaw));
  register_target("misc.exploits.anti_aim_fake_yaw", "AA fake yaw", reinterpret_cast<int*>(&config.misc.exploits.anti_aim_fake_yaw));
  register_target("misc.exploits.anti_aim_real_yaw_offset", "AA real offset", &config.misc.exploits.anti_aim_real_yaw_offset);
  register_target("misc.exploits.anti_aim_fake_yaw_offset", "AA fake offset", &config.misc.exploits.anti_aim_fake_yaw_offset);
  register_target("misc.exploits.anti_aim_spin_speed", "AA spin speed", &config.misc.exploits.anti_aim_spin_speed);
  register_target("misc.exploits.anti_aim_anti_overlap", "AA anti-overlap", &config.misc.exploits.anti_aim_anti_overlap);
  register_target("misc.exploits.antiwarp", "Antiwarp", &config.misc.exploits.antiwarp);
  register_target("misc.automation.autotaunt", "Auto taunt", &config.misc.automation.autotaunt);
  register_target("misc.automation.chatspam", "Chatspam", reinterpret_cast<int*>(&config.misc.automation.chatspam));
  register_target("misc.automation.killsay", "Killsay", reinterpret_cast<int*>(&config.misc.automation.killsay));
  register_target("misc.exploits.null_graphics", "Null graphics", &config.misc.exploits.null_graphics);
  register_target("misc.exploits.null_graphics_render_stubs", "Null render stubs", &config.misc.exploits.null_graphics_render_stubs);
  register_target("misc.exploits.experimental_nographic_hooks", "Experimental nographic hooks", &config.misc.exploits.experimental_nographic_hooks);
  register_target("debug.font_height", "Font height", &config.debug.font_height);
  register_target("debug.font_weight", "Font weight", &config.debug.font_weight);

  register_target("misc.automation.navbot_enabled", "Enable navbot", &config.misc.automation.navbot_enabled);
  register_target("misc.automation.navbot_draw_path", "Draw nav path", &config.misc.automation.navbot_draw_path);
  register_target("misc.automation.navbot_dont_path_during_warmup", "Skip path in warmup", &config.misc.automation.navbot_dont_path_during_warmup);
  register_target("misc.automation.navbot_dont_path_unless_match_started", "Path only in match", &config.misc.automation.navbot_dont_path_unless_match_started);
  register_target("misc.automation.navbot_warmup_only_blu_cp_pl", "Warmup BLU path only", &config.misc.automation.navbot_warmup_only_blu_cp_pl);
  register_target("misc.automation.navbot_auto_weapon", "Auto weapon", &config.misc.automation.navbot_auto_weapon);
  register_target("misc.automation.navbot_look_at_path", "Look at path", &config.misc.automation.navbot_look_at_path);
  register_target("misc.automation.navbot_look_at_path_speed", "Navbot look speed", &config.misc.automation.navbot_look_at_path_speed);
  register_target("misc.automation.navbot_crumb_blacklist_seconds", "Crumb blacklist", &config.misc.automation.navbot_crumb_blacklist_seconds);
  register_target("misc.automation.medic_autoheal", "Medic autoheal", &config.misc.automation.medic_autoheal);
  register_target("misc.automation.medic_autovacc", "Medic autovacc", &config.misc.automation.medic_autovacc);
  register_target("misc.automation.medic_autouber", "Medic autouber", &config.misc.automation.medic_autouber);
  register_target("misc.automation.medic_auto_crossbow", "Medic Crossbow", &config.misc.automation.medic_auto_crossbow);
  register_target("misc.automation.medic_heal_targets_mask", "Medic heal targets", reinterpret_cast<int*>(&config.misc.automation.medic_heal_targets_mask));
  register_target("misc.automation.medic_heal_only", "Medic heal only", &config.misc.automation.medic_heal_only);
  register_target("misc.automation.auto_class_select", "Auto class select", &config.misc.automation.auto_class_select);
  register_target("misc.automation.class_selected", "Preferred class", reinterpret_cast<int*>(&config.misc.automation.class_selected));
  register_target("misc.automation.anti_afk", "Anti AFK", &config.misc.automation.anti_afk);
  register_target("misc.automation.anti_autobalance", "Anti autobalance", &config.misc.automation.anti_autobalance);
  register_target("misc.automation.voice_command_spam", "Voice command spam", reinterpret_cast<int*>(&config.misc.automation.voice_command_spam));
  register_target("misc.automation.micspam", "Micspam", &config.misc.automation.micspam);
  register_target("misc.automation.micspam_interval_on_seconds", "Micspam on", &config.misc.automation.micspam_interval_on_seconds);
  register_target("misc.automation.micspam_interval_off_seconds", "Micspam off", &config.misc.automation.micspam_interval_off_seconds);
  register_target("misc.automation.auto_item", "Auto item", &config.misc.automation.auto_item);
  register_target("misc.automation.auto_item_interval_ms", "Auto item interval", &config.misc.automation.auto_item_interval_ms);
  register_target("misc.automation.auto_item_weapons", "Auto item weapons", &config.misc.automation.auto_item_weapons);
  register_target("misc.automation.auto_item_hats", "Auto item hats", &config.misc.automation.auto_item_hats);
  register_target("misc.automation.auto_item_noisemaker", "Auto item noisemaker", &config.misc.automation.auto_item_noisemaker);
  register_target("misc.automation.auto_item_debug", "Auto item debug", &config.misc.automation.auto_item_debug);
  register_target("misc.automation.auto_queue", "Auto queue", &config.misc.automation.auto_queue);
  register_target("misc.automation.auto_report", "Auto report", &config.misc.automation.auto_report);
  register_target("misc.automation.auto_queue_mode", "Queue mode", &config.misc.automation.auto_queue_mode);
  register_target("misc.automation.auto_requeue", "Auto requeue", &config.misc.automation.auto_requeue);
  register_target("misc.automation.requeue_on_kick", "Requeue on kick", &config.misc.automation.requeue_on_kick);
  register_target("misc.automation.auto_casual_join", "Auto casual join", &config.misc.automation.auto_casual_join);
  register_target("misc.automation.rq_if_players_lte", "RQ if players <=", &config.misc.automation.rq_if_players_lte);
  register_target("misc.automation.rq_if_players_gte", "RQ if players >=", &config.misc.automation.rq_if_players_gte);
  register_target("misc.automation.rq_ignore_friends", "RQ ignore friends", &config.misc.automation.rq_ignore_friends);
  register_target("misc.automation.requeue_action", "Requeue action", reinterpret_cast<int*>(&config.misc.automation.requeue_action));
  register_target("misc.automation.anti_motd", "Auto-close MOTD", &config.misc.automation.anti_motd);
}

inline void register_builtin_button_targets() {
  if (button_targets_registered()) {
    return;
  }

  button_targets_registered() = true;

  register_button_target("aimbot.key", "Aimbot", &config.aimbot.key, indicator_kind::aimbot, true);
  register_button_target("misc.exploits.doubletap_key", "Doubletap", &config.misc.exploits.doubletap_key, indicator_kind::value, true);
  register_button_target("misc.exploits.warp_key", "Warp", &config.misc.exploits.warp_key, indicator_kind::value, true);
}

inline bind_entry& create_entry(const std::string& target_key, const char* label, void* target, value_type type) {
  bind_entry entry{};
  entry.target_key = target_key;
  entry.label = label;
  entry.default_label = label;
  entry.target = target;
  entry.type = type;
  entry.default_value = read_value(target, type);
  entry.override_value = entry.default_value;
  entry.has_default = true;
  entry.has_override = true;
  entries().push_back(entry);
  return entries().back();
}

template <typename value_t>
inline bind_entry* ensure_entry(value_t* target, const char* label) {
  register_builtin_targets();
  register_builtin_button_targets();

  const auto it = pointer_to_key().find(target);
  if (it == pointer_to_key().end()) {
    return nullptr;
  }

  bind_entry* entry = find_entry(it->second);
  if (entry == nullptr) {
    entry = &create_entry(it->second, label, target, get_value_type(target));
  }

  entry->target = target;
  entry->label = label;
  entry->type = get_value_type(target);
  return entry;
}

inline void request_popup(const std::string& target_key, popup_target_type target_type) {
  popup_target_key() = target_key;
  popup_target_type_value() = target_type;
  popup_open_requested() = true;
}

inline void close_popup_if_target(const std::string& target_key) {
  if (popup_target_key() != target_key) {
    return;
  }

  popup_target_key().clear();
  popup_label_target_key().clear();
  ImGui::CloseCurrentPopup();
}

inline void remove_indicator_bind(std::string target_key, popup_target_type target_type) {
  if (target_type == popup_target_type::value_bind) {
    auto& value_entries = entries();
    for (auto it = value_entries.begin(); it != value_entries.end();) {
      if (it->target_key == target_key) {
        it = value_entries.erase(it);
      } else {
        ++it;
      }
    }
    close_popup_if_target(target_key);
    return;
  }

  button_entry* entry = find_button_entry(target_key);
  if (entry == nullptr || entry->target == nullptr) {
    return;
  }

  entry->target->button = SDLK_UNKNOWN;
  entry->target->waiting = false;
  entry->target->active = false;
  entry->target->was_down = false;
  entry->target->last_press_time = 0.0f;
  entry->label = entry->default_label;

  close_popup_if_target(target_key);
}

template <typename value_t>
inline void maybe_open_popup(value_t* target, const char* label) {
  if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) || !ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
    return;
  }

  bind_entry* entry = ensure_entry(target, label);
  if (entry == nullptr) {
    return;
  }

  request_popup(entry->target_key, popup_target_type::value_bind);
}

template <typename value_t>
inline void sync_default(value_t* target, bind_entry* entry, bool changed) {
  if (entry == nullptr || target == nullptr) {
    return;
  }

  if (!entry->has_default || changed) {
    entry->default_value = *target;
    entry->has_default = true;
  }

  if (!entry->has_override) {
    entry->override_value = *target;
    entry->has_override = true;
  }
}

inline void bindable_checkbox(const char* label, bool* target, bool changed) {
  bind_entry* entry = ensure_entry(target, label);
  if (entry != nullptr) {
    entry->widget = widget_type::checkbox;
    sync_default(target, entry, changed);
  }
  maybe_open_popup(target, label);
}

inline void bindable_combo_int(const char* label, int* target, bool changed, const char* const items[], int item_count) {
  bind_entry* entry = ensure_entry(target, label);
  if (entry != nullptr) {
    entry->widget = widget_type::combo_int;
    entry->options.clear();
    for (int i = 0; i < item_count; ++i) {
      entry->options.emplace_back(items[i], i);
    }
    sync_default(target, entry, changed);
  }
  maybe_open_popup(target, label);
}

inline void bindable_slider_int(const char* label, int* target, bool changed, int minimum, int maximum, const char* format) {
  bind_entry* entry = ensure_entry(target, label);
  if (entry != nullptr) {
    entry->widget = widget_type::slider_int;
    entry->int_min = minimum;
    entry->int_max = maximum;
    entry->format = format != nullptr ? format : "%d";
    sync_default(target, entry, changed);
  }
  maybe_open_popup(target, label);
}

inline void bindable_slider_float(const char* label, float* target, bool changed, float minimum, float maximum, const char* format) {
  bind_entry* entry = ensure_entry(target, label);
  if (entry != nullptr) {
    entry->widget = widget_type::slider_float;
    entry->float_min = minimum;
    entry->float_max = maximum;
    entry->format = format != nullptr ? format : "%.3f";
    sync_default(target, entry, changed);
  }
  maybe_open_popup(target, label);
}

inline void handle_input(SDL_Event* event) {
  register_builtin_targets();
  register_builtin_button_targets();

  for (bind_entry& entry : entries()) {
    const int previous_button = entry.trigger.button;
    const bool was_waiting = entry.trigger.waiting;
    ImGui::KeybindEvent(event, &entry.trigger.waiting, &entry.trigger.button);
    if (was_waiting && !entry.trigger.waiting && entry.trigger.button != previous_button) {
      entry.trigger.active = false;
      entry.trigger.was_down = false;
      entry.trigger.last_press_time = 0.0f;
    }
  }

  for (button_entry& entry : button_entries()) {
    if (entry.target == nullptr) {
      continue;
    }

    const int previous_button = entry.target->button;
    const bool was_waiting = entry.target->waiting;
    ImGui::KeybindEvent(event, &entry.target->waiting, &entry.target->button);
    if (was_waiting && !entry.target->waiting && entry.target->button != previous_button) {
      entry.target->active = false;
      entry.target->was_down = false;
      entry.target->last_press_time = 0.0f;
    }
  }
}

inline void run() {
  register_builtin_targets();
  register_builtin_button_targets();

  for (bind_entry& entry : entries()) {
    if (entry.target == nullptr) {
      const auto it = key_to_pointer().find(entry.target_key);
      if (it != key_to_pointer().end()) {
        entry.target = it->second;
      }
    }

    if (entry.target == nullptr || !entry.has_default || !entry.has_override) {
      continue;
    }

    const bool bind_set = entry.trigger.button != SDLK_UNKNOWN;
    if (!bind_set) {
      entry.active = false;
      continue;
    }

    entry.active = entry.enabled && is_button_active(entry.trigger);
    write_value(entry.target, entry.type, entry.active ? entry.override_value : entry.default_value);
  }
}

inline void draw_popup() {
  if (popup_open_requested()) {
    ImGui::OpenPopup("bind_popup_context");
    popup_open_requested() = false;
  }

  ImGui::SetNextWindowSize(ImVec2(250.0f, 0.0f), ImGuiCond_Appearing);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7.0f, 7.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.114f, 0.184f, 0.251f, 0.985f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.267f, 0.392f, 0.596f, 1.000f));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.000f, 1.000f, 1.000f, 1.000f));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.114f, 0.184f, 0.251f, 1.000f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.160f, 0.230f, 0.320f, 1.000f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.267f, 0.392f, 0.596f, 1.000f));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.114f, 0.184f, 0.251f, 1.000f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.160f, 0.230f, 0.320f, 1.000f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.267f, 0.392f, 0.596f, 1.000f));
  ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.259f, 0.737f, 0.600f, 1.000f));

  if (!ImGui::BeginPopup("bind_popup_context")) {
    ImGui::PopStyleColor(10);
    ImGui::PopStyleVar(4);
    return;
  }

  const char* mode_names[] = { "Hold", "Toggle", "Double" };
  const bool popup_is_button = popup_target_type_value() == popup_target_type::button_bind;

  bind_entry* entry = popup_is_button ? nullptr : find_entry(popup_target_key());
  button_entry* button_entry = popup_is_button ? find_button_entry(popup_target_key()) : nullptr;
  if (entry == nullptr && button_entry == nullptr) {
    ImGui::TextUnformatted("Bind unavailable");
    ImGui::EndPopup();
    ImGui::PopStyleColor(10);
    ImGui::PopStyleVar(4);
    return;
  }

  button* popup_button = popup_is_button ? button_entry->target : &entry->trigger;
  std::string* popup_label = popup_is_button ? &button_entry->label : &entry->label;
  bool* popup_show_in_indicator = popup_is_button ? &button_entry->show_in_indicator : &entry->show_in_indicator;
  if (popup_button == nullptr || popup_label == nullptr || popup_show_in_indicator == nullptr) {
    ImGui::TextUnformatted("Bind unavailable");
    ImGui::EndPopup();
    ImGui::PopStyleColor(10);
    ImGui::PopStyleVar(4);
    return;
  }

  std::string key_label = "Not bound";
  if (popup_button != nullptr && popup_button->waiting) {
    key_label = "Press a key...";
  } else if (popup_button != nullptr && popup_button->button != SDLK_UNKNOWN) {
    key_label = get_button_name(popup_button->button);
  }

  if (popup_label_target_key() != popup_target_key()) {
    popup_label_target_key() = popup_target_key();
    popup_label_buffer().fill('\0');
    std::snprintf(popup_label_buffer().data(), popup_label_buffer().size(), "%s", popup_label->c_str());
  }

  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.259f, 0.737f, 0.600f, 1.000f));
  ImGui::TextUnformatted("Bind editor");
  ImGui::PopStyleColor();
  ImGui::SameLine();
  ImGui::TextDisabled("right-click feature");
  ImGui::Separator();

  ImGui::TextUnformatted(popup_label->c_str());
  ImGui::TextDisabled("Key");
  if (ImGui::Button(key_label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
    popup_button->waiting = true;
  }
  if (popup_button->waiting) {
    ImGui::TextDisabled("Press any key or mouse button. Escape clears it.");
  }

  if (popup_button->button != SDLK_UNKNOWN && ImGui::Button("Clear key", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
    popup_button->button = SDLK_UNKNOWN;
    reset_button_state(*popup_button);
  }

  if (ImGui::InputText("Name", popup_label_buffer().data(), popup_label_buffer().size())) {
    *popup_label = popup_label_buffer().data();
  }

  int mode = static_cast<int>(popup_button->mode);
  if (ImGui::Combo("Mode", &mode, mode_names, IM_ARRAYSIZE(mode_names))) {
    popup_button->mode = static_cast<button::mode_type>(mode);
    popup_button->active = false;
    popup_button->was_down = false;
    popup_button->last_press_time = 0.0f;
  }

  if (entry != nullptr && entry->type == value_type::boolean) {
    bool value = std::get<bool>(entry->override_value);
    const char* bool_names[] = { "Off", "On" };
    int bool_index = value ? 1 : 0;
    if (ImGui::Combo("Value", &bool_index, bool_names, IM_ARRAYSIZE(bool_names))) {
      entry->override_value = bool_index == 1;
    }
  } else if (entry != nullptr && entry->widget == widget_type::combo_int && !entry->options.empty()) {
    int value = std::get<int>(entry->override_value);
    std::vector<const char*> names{};
    names.reserve(entry->options.size());
    int current_index = 0;
    for (std::size_t i = 0; i < entry->options.size(); ++i) {
      names.push_back(entry->options[i].first.c_str());
      if (entry->options[i].second == value) {
        current_index = static_cast<int>(i);
      }
    }

    if (ImGui::Combo("Value", &current_index, names.data(), static_cast<int>(names.size()))) {
      entry->override_value = entry->options[static_cast<std::size_t>(current_index)].second;
    }
  } else if (entry != nullptr && entry->widget == widget_type::slider_int) {
    int value = std::get<int>(entry->override_value);
    if (ImGui::SliderInt("Value", &value, entry->int_min, entry->int_max, entry->format.c_str())) {
      entry->override_value = value;
    }
  } else if (entry != nullptr && entry->widget == widget_type::slider_float) {
    float value = std::get<float>(entry->override_value);
    if (ImGui::SliderFloat("Value", &value, entry->float_min, entry->float_max, entry->format.c_str())) {
      entry->override_value = value;
    }
  }

  if (entry != nullptr) {
    ImGui::Checkbox("Enabled", &entry->enabled);
  }
  ImGui::Checkbox("Show in indicator", popup_show_in_indicator);

  ImGui::Separator();
  if (ImGui::Button("Remove bind", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
    remove_indicator_bind(popup_target_key(), popup_target_type_value());
  }

  ImGui::EndPopup();
  ImGui::PopStyleColor(10);
  ImGui::PopStyleVar(4);
}

inline const std::vector<bind_entry>& indicator_entries() {
  return entries();
}

inline std::vector<indicator_row> collect_indicator_rows() {
  register_builtin_targets();
  register_builtin_button_targets();

  std::vector<indicator_row> rows{};
  rows.reserve(entries().size() + button_entries().size());

  for (const bind_entry& entry : entries()) {
    if (!entry.enabled || !entry.show_in_indicator || entry.trigger.button == SDLK_UNKNOWN) {
      continue;
    }

    rows.push_back({
      .label = entry.label,
      .key = get_button_name(entry.trigger.button),
      .state = entry.active ? "active" : "idle",
      .target_key = entry.target_key,
      .popup_type = popup_target_type::value_bind,
      .active = entry.active
    });
  }

  for (const button_entry& entry : button_entries()) {
    if (entry.target == nullptr || !entry.show_in_indicator || entry.target->button == SDLK_UNKNOWN) {
      continue;
    }

    bool show_row = false;
    bool active = false;
    const char* state = "idle";

    switch (entry.kind) {
    case indicator_kind::aimbot:
      show_row = true;
      active = entry.target->active;
      state = active ? "active" : "idle";
      break;
    case indicator_kind::value:
      break;
    }

    if (!show_row) {
      continue;
    }

    rows.push_back({
      .label = entry.label,
      .key = get_button_name(entry.target->button),
      .state = state,
      .target_key = entry.target_key,
      .popup_type = popup_target_type::button_bind,
      .active = active
    });
  }

  return rows;
}

inline void save(cathook::core::config_store* store) {
  if (store == nullptr) {
    return;
  }

  store->set_int("binds.count", static_cast<int>(entries().size()));
  for (int index = 0; index < static_cast<int>(entries().size()); ++index) {
    const bind_entry& entry = entries()[static_cast<std::size_t>(index)];
    const std::string prefix = "binds." + std::to_string(index) + ".";
    store->set_string(prefix + "target_key", entry.target_key);
    store->set_string(prefix + "label", entry.label);
    store->set_int(prefix + "type", static_cast<int>(entry.type));
    store->set_int(prefix + "widget", static_cast<int>(entry.widget));
    store->set_int(prefix + "button", entry.trigger.button);
    store->set_int(prefix + "mode", static_cast<int>(entry.trigger.mode));
    store->set_bool(prefix + "enabled", entry.enabled);
    store->set_bool(prefix + "show_in_indicator", entry.show_in_indicator);

    if (entry.type == value_type::boolean) {
      store->set_bool(prefix + "default_bool", std::get<bool>(entry.default_value));
      store->set_bool(prefix + "override_bool", std::get<bool>(entry.override_value));
    } else if (entry.type == value_type::integer) {
      store->set_int(prefix + "default_int", std::get<int>(entry.default_value));
      store->set_int(prefix + "override_int", std::get<int>(entry.override_value));
    } else if (entry.type == value_type::floating) {
      store->set_float(prefix + "default_float", std::get<float>(entry.default_value));
      store->set_float(prefix + "override_float", std::get<float>(entry.override_value));
    }
  }

  store->set_int("button_binds.count", static_cast<int>(button_entries().size()));
  for (int index = 0; index < static_cast<int>(button_entries().size()); ++index) {
    const button_entry& entry = button_entries()[static_cast<std::size_t>(index)];
    const std::string prefix = "button_binds." + std::to_string(index) + ".";
    store->set_string(prefix + "target_key", entry.target_key);
    store->set_string(prefix + "label", entry.label);
    store->set_bool(prefix + "show_in_indicator", entry.show_in_indicator);
  }
}

inline void load(cathook::core::config_store* store) {
  if (store == nullptr) {
    return;
  }

  register_builtin_targets();
  register_builtin_button_targets();
  entries().clear();

  const int count = std::max(0, store->get_int("binds.count", 0));
  for (int index = 0; index < count; ++index) {
    const std::string prefix = "binds." + std::to_string(index) + ".";
    bind_entry entry{};
    entry.target_key = store->get_string(prefix + "target_key", "");
    if (entry.target_key.empty()) {
      continue;
    }

    entry.label = store->get_string(prefix + "label", entry.target_key);
    entry.type = static_cast<value_type>(std::clamp(store->get_int(prefix + "type", 0), 0, 2));
    entry.widget = static_cast<widget_type>(std::clamp(store->get_int(prefix + "widget", 0), 0, 3));
    entry.trigger.button = store->get_int(prefix + "button", SDLK_UNKNOWN);
    entry.trigger.mode = static_cast<button::mode_type>(std::clamp(store->get_int(prefix + "mode", 0), 0, 2));
    entry.enabled = store->get_bool(prefix + "enabled", true);
    entry.show_in_indicator = store->get_bool(prefix + "show_in_indicator", true);
    reset_button_state(entry.trigger);
    entry.target = key_to_pointer().contains(entry.target_key) ? key_to_pointer()[entry.target_key] : nullptr;

    if (entry.type == value_type::boolean) {
      entry.default_value = store->get_bool(prefix + "default_bool", entry.target != nullptr ? *static_cast<bool*>(entry.target) : false);
      entry.override_value = store->get_bool(prefix + "override_bool", std::get<bool>(entry.default_value));
    } else if (entry.type == value_type::integer) {
      entry.default_value = store->get_int(prefix + "default_int", entry.target != nullptr ? *static_cast<int*>(entry.target) : 0);
      entry.override_value = store->get_int(prefix + "override_int", std::get<int>(entry.default_value));
    } else {
      entry.default_value = store->get_float(prefix + "default_float", entry.target != nullptr ? *static_cast<float*>(entry.target) : 0.0f);
      entry.override_value = store->get_float(prefix + "override_float", std::get<float>(entry.default_value));
    }

    entry.has_default = true;
    entry.has_override = true;
    entries().push_back(entry);
  }

  const int button_count = std::max(0, store->get_int("button_binds.count", 0));
  for (int index = 0; index < button_count; ++index) {
    const std::string prefix = "button_binds." + std::to_string(index) + ".";
    const std::string target_key = store->get_string(prefix + "target_key", "");
    if (target_key.empty()) {
      continue;
    }

    button_entry* entry = find_button_entry(target_key);
    if (entry == nullptr) {
      continue;
    }

    entry->label = store->get_string(prefix + "label", entry->label);
    entry->show_in_indicator = store->get_bool(prefix + "show_in_indicator", entry->show_in_indicator);
  }
}

} // namespace cat_bind

#endif
