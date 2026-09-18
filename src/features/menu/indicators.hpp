#ifndef INDICATORS_HPP
#define INDICATORS_HPP
#include "config.hpp"
#include "binds.hpp"
#include "menu.hpp"
#include "features/combat/random_crits/crit_hack.hpp"
#include "features/combat/aimbot/aimbot_debug.hpp"
#include "features/combat/aimbot/aim_utils.hpp"
#include "features/combat/tickbase/tickbase.hpp"
#include "features/visuals/spectator_list.hpp"
#include "games/tf2/sdk/entities/player.hpp"
#include "games/tf2/sdk/interfaces/engine.hpp"
#include "games/tf2/sdk/interfaces/entity_list.hpp"
#include "mono/entity_esp.hpp"
#include "mono/window_chrome.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace cat_indicator
{

enum class section_kind
{
  tickbase,
  keybinds,
  spectators,
  aimbot_debug,
  crit_hack,
  nospread
};

struct section_spec
{
  section_kind kind = section_kind::keybinds;
  ImVec2 position{};
};

inline bool should_draw_overlay()
{
  if (menu_focused) {
    return true;
  }
  if (engine != nullptr && !engine->is_in_game()) {
    return false;
  }
  if (entity_list == nullptr) {
    return true;
  }
  Player* localplayer = entity_list->get_localplayer();
  return localplayer == nullptr || localplayer->is_alive();
}

inline bool has_indicator(const uint32_t flag)
{
  return (config.visuals.indicators.enabled_mask & flag) != 0;
}

inline auto collect_keybind_rows() -> std::vector<cat_bind::indicator_row>
{

  return cat_bind::collect_indicator_rows();
}

inline auto collect_spectator_rows(Player** target_player_out) -> std::vector<spectator_list::spectator_entry>
{
  return spectator_list::collect_spectators(target_player_out);
}

inline auto build_sections() -> std::vector<section_spec>
{
  std::vector<section_spec> sections{};
  sections.reserve(6);

  if (has_indicator(Visuals::Indicators::crit_hack)) {
    sections.push_back({ .kind = section_kind::crit_hack, .position = { config.visuals.indicators.crit_hack_x, config.visuals.indicators.crit_hack_y } });
  }
  if (has_indicator(Visuals::Indicators::tickbase)) {
    sections.push_back({ .kind = section_kind::tickbase, .position = { config.visuals.indicators.legacy_ticks_x, config.visuals.indicators.legacy_ticks_y } });
  }
  if (has_indicator(Visuals::Indicators::nospread)) {
    sections.push_back({ .kind = section_kind::nospread, .position = { config.visuals.indicators.nospread_x, config.visuals.indicators.nospread_y } });
  }
  if (has_indicator(Visuals::Indicators::spectators)) {
    Player* target_player = nullptr;
    if (!collect_spectator_rows(&target_player).empty()) {
      sections.push_back({ .kind = section_kind::spectators, .position = { config.visuals.spectator_list.x, config.visuals.spectator_list.y } });
    }
  }
  if (config.misc.menu.bind_window || menu_focused || has_indicator(Visuals::Indicators::keybinds)) {
    if (!collect_keybind_rows().empty()) {
      sections.push_back({ .kind = section_kind::keybinds, .position = { config.visuals.indicators.keybinds_x, config.visuals.indicators.keybinds_y } });
    }
  }
  if (config.aimbot.debug_overlay) {
    const aimbot_debug_state& state = aimbot_debug_get_state();
    if (menu_focused || state.active) {
      sections.push_back({ .kind = section_kind::aimbot_debug, .position = { config.aimbot.debug_overlay_x, config.aimbot.debug_overlay_y } });
    }
  }
  return sections;
}

struct section_position_refs
{
  float* x = nullptr;
  float* y = nullptr;
};

inline auto section_drag_position(const section_kind kind) -> section_position_refs
{
  switch (kind) {
  case section_kind::crit_hack:
    return { &config.visuals.indicators.crit_hack_x, &config.visuals.indicators.crit_hack_y };
  case section_kind::nospread:
    return { &config.visuals.indicators.nospread_x, &config.visuals.indicators.nospread_y };
  case section_kind::tickbase:
    return { &config.visuals.indicators.legacy_ticks_x, &config.visuals.indicators.legacy_ticks_y };
  case section_kind::keybinds:
    return { &config.visuals.indicators.keybinds_x, &config.visuals.indicators.keybinds_y };
  case section_kind::spectators:
    return { &config.visuals.spectator_list.x, &config.visuals.spectator_list.y };
  case section_kind::aimbot_debug:
    return { &config.aimbot.debug_overlay_x, &config.aimbot.debug_overlay_y };
  }
  return {};
}

inline auto format_float(const float value, const char* format) -> std::string
{
  char buffer[64]{};
  std::snprintf(buffer, sizeof(buffer), format, value);
  return buffer;
}

inline auto bool_text(const bool value) -> const char*
{
  return value ? "yes" : "no";
}

inline auto format_optional_float(const float value, const char* format) -> std::string
{
  if (!std::isfinite(value) || value >= FLT_MAX * 0.5f) {
    return "n/a";
  }
  return format_float(value, format);
}

inline auto format_reject_summary(const aimbot_reject_debug& reject) -> std::string
{
  if (reject.reason == aimbot_reject_reason::none) {
    return "none";
  }
  std::string text = "#" + std::to_string(reject.entity_index) + " ";
  text += aimbot_debug_reject_reason_name(reject.reason);
  if (reject.hitbox >= 0) text += " hb " + std::to_string(reject.hitbox);
  if (reject.team >= 0) text += " t" + std::to_string(reject.team);
  if (reject.health > 0) text += " hp " + std::to_string(reject.health);
  return text;
}

inline auto format_reject_detail(const aimbot_reject_debug& reject) -> std::string
{
  if (reject.reason == aimbot_reject_reason::none) {
    return "none";
  }
  std::string text = "fov " + format_optional_float(reject.fov, "%.2f") + "/" + format_optional_float(reject.fov_limit, "%.2f");
  text += " d " + format_optional_float(reject.distance, "%.0f");
  text += " vis " + std::string(bool_text(reject.visible));
  if (reject.trace_entity_index >= 0 || reject.trace_hitbox >= 0) {
    text += " tr " + std::to_string(reject.trace_entity_index) + " hb " + std::to_string(reject.trace_hitbox);
    text += " fr " + format_float(reject.trace_fraction, "%.3f");
    text += " c " + std::to_string(reject.trace_contents);
  }
  if (reject.preferred) text += " pref";
  if (reject.current) text += " cur";
  if (reject.backtrack) text += " bt";
  return text;
}

inline auto format_reject_trace_path(const aimbot_reject_debug& reject) -> std::string
{
  if (reject.reason != aimbot_reject_reason::trace_blocked) {
    return "n/a";
  }
  return "s " + format_float(reject.trace_start.x, "%.0f") + "," + format_float(reject.trace_start.y, "%.0f") + "," + format_float(reject.trace_start.z, "%.0f") +
    " p " + format_float(reject.trace_point.x, "%.0f") + "," + format_float(reject.trace_point.y, "%.0f") + "," + format_float(reject.trace_point.z, "%.0f") +
    " e " + format_float(reject.trace_end.x, "%.0f") + "," + format_float(reject.trace_end.y, "%.0f") + "," + format_float(reject.trace_end.z, "%.0f");
}

struct owned_indicator_rows final
{
  std::deque<std::string> values{};
  std::vector<mono::indicator_row> rows{};
};

inline auto build_meter_rows(const std::string_view label, std::string status, std::string value, const bool active) -> owned_indicator_rows
{
  owned_indicator_rows result{};
  result.rows.reserve(1);
  result.values.push_back(std::move(status));
  result.values.push_back(std::move(value));
  result.rows.push_back({ label, result.values[0], result.values[1], active });
  return result;
}

inline auto build_aimbot_debug_rows() -> owned_indicator_rows
{
  const aimbot_debug_state& state = aimbot_debug_get_state();
  const bool active = state.attack_ready;
  owned_indicator_rows result{};
  result.rows.reserve(30);
  const auto add = [&](const char* label, std::string value) {
    result.values.push_back(std::move(value));
    result.rows.push_back({ label, "", result.values.back(), active });
  };
  add("reason", aimbot_debug_reason_name(state.reason));
  add("target", std::to_string(state.selected_entity_index) + " hb " + std::to_string(state.selected_hitbox) + " hp " + std::to_string(state.selected_health));
  add("target id", "team " + std::to_string(state.selected_team) + " ref " + std::to_string(state.selected_handle) + (state.selected_backtrack ? " bt" : " cur"));
  add("aim point", format_float(state.selected_aim_position.x, "%.0f") + "," + format_float(state.selected_aim_position.y, "%.0f") + "," + format_float(state.selected_aim_position.z, "%.0f"));
  add("target sim", format_float(state.selected_simulation_time, "%.3f"));
  add("velocity", format_float(state.target_velocity.x, "%.0f") + "," + format_float(state.target_velocity.y, "%.0f") + "," + format_float(state.target_velocity.z, "%.0f") + " s" + format_float(state.target_speed, "%.0f"));
  add("lead", std::string(bool_text(state.pose_timing_valid)) + " " + format_float(state.pose_lead_seconds * 1000.0f, "%.1f") + " ms / " + format_float(state.pose_lead_distance, "%.1f") + " hu");
  add("target/cmd tick", std::to_string(state.pose_target_tick) + " / " + std::to_string(state.pose_command_tick) + (state.compensation_applied ? " moved" : " raw"));
  add("trace", std::to_string(state.trace_entity_index) + " hb " + std::to_string(state.trace_hitbox));
  add("final trace", std::string(bool_text(state.final_trace_hit)) + " fr " + format_float(state.final_trace_fraction, "%.3f") + " ent " + std::to_string(state.trace_entity_index) + " hb " + std::to_string(state.trace_hitbox) + " c " + std::to_string(state.final_trace_contents));
  add("trace end", format_float(state.final_trace_end.x, "%.0f") + "," + format_float(state.final_trace_end.y, "%.0f") + "," + format_float(state.final_trace_end.z, "%.0f"));
  add("candidates", std::to_string(state.candidates_visible) + "/" + std::to_string(state.candidates_total) + " reject " + std::to_string(state.candidates_rejected));
  add("last reject", format_reject_summary(state.last_reject));
  add("last detail", format_reject_detail(state.last_reject));
  add("best reject", format_reject_summary(state.best_reject));
  add("best detail", format_reject_detail(state.best_reject));
  add("best trace s/p/e", format_reject_trace_path(state.best_reject));
  add("skip ig/fr/ip/cl/tm/in/de/type", std::to_string(state.skipped_ignored) + "/" + std::to_string(state.skipped_friends) + "/" + std::to_string(state.skipped_ipc) + "/" + std::to_string(state.skipped_cloaked) + "/" + std::to_string(state.skipped_team) + "/" + std::to_string(state.skipped_invulnerable) + "/" + std::to_string(state.skipped_dead) + "/" + std::to_string(state.skipped_type));
  add("resolver", std::string(state.resolver_active ? "on " : "off ") + aimbot_debug_resolver_mode_name(state.resolver_mode) + " y" + std::to_string(state.resolver_candidates));
  add("res angle", format_float(state.resolver_yaw, "%.1f") + " / " + format_float(state.resolver_pitch, "%.1f"));
  add("res hit/miss", std::to_string(state.resolver_hits) + " / " + std::to_string(state.resolver_misses));
  add("scope/head", std::string(bool_text(state.scoped_ready)) + " / " + bool_text(state.headshot_ready));
  add("gates a/c/t/s/p", std::string(bool_text(state.attack_gate_ready)) + " / " + bool_text(state.charge_ready) + " / " + bool_text(state.trace_ready) + " / " + bool_text(state.settled) + " / " + bool_text(state.primary_ready));
  add("spread", format_float(state.spread, "%.4f") + (state.spread_compensated ? " comp" : ""));
  add("pellet", std::to_string(state.pellet_index) + " / " + std::to_string(state.pellet_count));
  add("tick/fov", std::to_string(state.tick_count) + " / " + format_float(state.fov, "%.2f"));
  add("bt err/gap", format_float(state.backtrack_timing_error * 1000.0f, "%.0f") + " / " + format_float(state.backtrack_capture_gap * 1000.0f, "%.0f") + " ms");
  add("pose", "i" + std::to_string(state.pose.target_index) + " " + bool_text(state.pose.valid) + " b" + std::to_string(state.pose.bone_count) + " g" + std::to_string(state.pose.generation) + " fail " + aimbot_debug_reject_reason_name(state.pose.failure));
  add("pose frame", std::to_string(state.pose.pose_frame) + " / " + std::to_string(state.pose.current_frame) + (state.pose.signature_reused ? " reuse" : " new"));
  add("pose time", format_float(state.pose.simulation_time, "%.3f") + " / " + format_float(state.pose.setup_time, "%.3f") + " / " + format_float(state.pose.cache_time, "%.3f"));
  add("pose age", format_float(state.pose.simulation_age * 1000.0f, "%.1f") + " ms");
  add("bone cache", std::string(bool_text(state.pose.getter_ready)) + " / " + bool_text(state.pose.updater_ready) + " / " + bool_text(state.pose.cache_updated) + " h " + std::to_string(static_cast<unsigned long long>(state.pose.cache_handle)));
  add("net out/in", format_float(state.outgoing_latency * 1000.0f, "%.1f") + " / " + format_float(state.incoming_latency * 1000.0f, "%.1f") + " ms");
  add("interp/lerp", format_float(state.interpolation * 1000.0f, "%.1f") + " / " + format_float(state.fake_interpolation * 1000.0f, "%.1f") + " ms " + std::to_string(state.lerp_ticks) + "t");
  add("correct", format_float(state.timing_correct * 1000.0f, "%.1f") + " ms");
  return result;
}

struct keybind_panel_rows final
{
  std::vector<cat_bind::indicator_row> source{};
  std::vector<mono::indicator_row> rows{};
};

inline auto build_keybind_rows() -> keybind_panel_rows
{
  keybind_panel_rows result{};
  result.source = collect_keybind_rows();
  result.rows.reserve(result.source.size());
  for (const cat_bind::indicator_row& row : result.source) {
    result.rows.push_back({ row.label, row.key, row.state, row.active });
  }
  return result;
}

inline auto section_id(const section_kind kind) -> const char*
{
  switch (kind) {
  case section_kind::tickbase: return "mono_indicator_tickbase";
  case section_kind::keybinds: return "mono_indicator_keybinds";
  case section_kind::spectators: return "mono_indicator_spectators";
  case section_kind::aimbot_debug: return "mono_indicator_aimbot_debug";
  case section_kind::crit_hack: return "mono_indicator_crit_hack";
  case section_kind::nospread: return "mono_indicator_nospread";
  }
  return "mono_indicator";
}

inline auto indicator_color(const int red, const int green, const int blue) -> mono::color
{
  return {
    static_cast<float>(red) / 255.0f,
    static_cast<float>(green) / 255.0f,
    static_cast<float>(blue) / 255.0f,
    1.0f
  };
}

inline auto progress_indicator_id(const section_kind kind) -> const char*
{
  switch (kind) {
  case section_kind::tickbase: return "##mono_indicator_tickbase_progress";
  case section_kind::crit_hack: return "##mono_indicator_crit_hack_progress";
  case section_kind::nospread: return "##mono_indicator_nospread_progress";
  default: return "##mono_indicator_progress";
  }
}

inline void draw_progress_indicator(
  const section_kind kind,
  const std::string_view label,
  const float progress,
  const std::string_view state,
  const mono::color state_color)
{
  const section_position_refs refs = section_drag_position(kind);
  if (refs.x == nullptr || refs.y == nullptr) return;

  constexpr ImVec2 bar_size{ 180.0f, 15.0f };
  const float label_height = ImGui::GetFontSize() + 3.0f;
  ImVec2 position{ *refs.x, *refs.y };
  if (menu_focused) {
    std::string_view drag_label = label;
    if (kind == section_kind::tickbase) drag_label = "tick base";
    else if (kind == section_kind::crit_hack) drag_label = "crits";
    const ImVec2 dragged = mono::drag_overlay(
      section_id(kind),
      drag_label,
      position,
      { bar_size.x, label_height + bar_size.y });
    position = dragged;
    *refs.x = position.x;
    *refs.y = position.y;
  }

  const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
  mono::progress_indicator(
    progress_indicator_id(kind),
    position,
    bar_size,
    progress,
    label,
    state,
    state_color,
    { accent.x, accent.y, accent.z, accent.w });
}

inline void draw_tickbase_indicator()
{
  if (!config.misc.exploits.tickbase) {
    return;
  }

  const tickbase::indicator_state state = tickbase::get_indicator_state();
  const int maximum = std::max(1, state.max_processing_ticks);
  const int processing = std::clamp(state.processing_ticks, 0, maximum);

  static float charge_fraction{};
  const float charge_target = static_cast<float>(processing) / static_cast<float>(maximum);
  charge_fraction = std::clamp(charge_fraction + (charge_target - charge_fraction) * 0.10f, 0.0f, 1.0f);

  const bool ready = config.misc.exploits.tickbase
    && state.available_shift_ticks >= 8
    && !state.recharging;
  const bool charged = config.misc.exploits.tickbase && processing >= maximum && !state.recharging;

  std::string status = "NO CHARGE";
  mono::color status_color = indicator_color(255, 63, 52);
  if (ready) {
    status = "READY";
    status_color = indicator_color(11, 232, 129);
  } else if (state.shifting) {
    status = state.doubletap ? "DOUBLETAP" : state.warp ? "WARP" : "SHIFT";
    status_color = indicator_color(255, 168, 1);
  } else if (charged) {
    status = "CHARGED";
    status_color = indicator_color(11, 232, 129);
  } else if (state.recharging) {
    status = "CHARGING";
    status_color = indicator_color(255, 168, 1);
  }

  draw_progress_indicator(
    section_kind::tickbase,
    "TICKS " + std::to_string(processing) + "/" + std::to_string(maximum),
    std::clamp(charge_fraction + (processing > 0 ? 1.0f / 150.0f : 0.0f), 0.0f, 1.0f),
    status,
    status_color);
}

inline void draw_crit_hack_indicator()
{
  Player* local = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  Weapon* weapon = local != nullptr ? local->get_weapon() : nullptr;
  if (local == nullptr || weapon == nullptr || !local->is_alive() || local->is_dormant()) {
    draw_progress_indicator(section_kind::crit_hack, "CRITS", 0.0f, "UNAVAILABLE", indicator_color(255, 63, 52));
    return;
  }

  if (!crit_hack::weapon_can_crit(weapon, true)) {
    draw_progress_indicator(section_kind::crit_hack, "CRITS", 0.0f, "UNAVAILABLE", indicator_color(255, 63, 52));
    return;
  }

  const crit_hack::crit_stats_t stats = crit_hack::get_stats();
  const float bucket_progress = stats.bucket_cap > 0.0f
    ? std::clamp(stats.bucket / stats.bucket_cap, 0.0f, 1.0f)
    : 0.0f;
  const float progress = stats.potential > 0 && stats.available >= stats.potential ? 1.0f : bucket_progress;
  const float current_time = global_vars != nullptr ? global_vars->curtime : 0.0f;
  static Convar* tf_weapon_criticals_nopred = nullptr;
  if (tf_weapon_criticals_nopred == nullptr && convar_system != nullptr) {
    tf_weapon_criticals_nopred = convar_system->find_var("tf_weapon_criticals_nopred");
  }
  const float rapid_check_time = tf_weapon_criticals_nopred != nullptr && tf_weapon_criticals_nopred->get_int() != 0
    ? weapon->last_crit_check_time()
    : weapon->last_rapid_fire_crit_check_time();
  const float wait_time = rapid_check_time + 1.0f - local->get_tickbase() * 0.015f;
  const bool waiting_for_crit = weapon->is_rapid_fire() && wait_time > 0.0f;

  std::string status = "NOT READY";
  mono::color status_color = indicator_color(255, 63, 52);
  if (!weapon->are_random_crits_enabled()) {
    status = "RANDOM OFF";
  } else if (local->is_crit_boosted()) {
    status = "BOOSTED";
    status_color = indicator_color(11, 232, 129);
  } else if (stats.banned && !weapon->is_melee()) {
    status = stats.damage_till_flip > 0 ? "FLIP " + std::to_string(stats.damage_till_flip) : "BANNED";
  } else if (weapon->crit_time() > current_time) {
    status = "STREAM " + format_float(weapon->crit_time() - current_time, "%.1f");
    status_color = indicator_color(11, 232, 129);
  } else {
    const bool crit_ready = !stats.banned && !waiting_for_crit && stats.available > 0;
    if (crit_ready) {
      status = config.crithack.force_crits ? "FORCED" : "READY";
      status_color = indicator_color(11, 232, 129);
    } else if (waiting_for_crit) {
      status = "WAIT " + format_float(wait_time, "%.2f");
      status_color = indicator_color(255, 168, 1);
    } else if (stats.available <= 0 && stats.damage_till_crit > 0) {
      status = "DMG " + std::to_string(stats.damage_till_crit);
    } else {
      status = "NOT READY";
    }
  }

  if (stats.queue == crit_hack::queue_state::waiting_for_seed) {
    status = "QUEUE " + std::to_string(std::max(0, stats.queued_ticks));
    status_color = indicator_color(255, 168, 1);
  } else if (stats.queue == crit_hack::queue_state::blocked) {
    status = "BLOCKED";
  }

  draw_progress_indicator(
    section_kind::crit_hack,
    "CRITS " + std::to_string(std::max(0, stats.available)) + "/" + std::to_string(std::max(0, stats.potential)),
    progress,
    status,
    status_color);
}

inline void draw_nospread_indicator()
{
  const mono::color muted = indicator_color(140, 140, 140);
  if (!config.aimbot.spread_compensation) {
    draw_progress_indicator(section_kind::nospread, "NOSPREAD", 0.0f, "OFF", muted);
    return;
  }

  Player* local = entity_list != nullptr ? entity_list->get_localplayer() : nullptr;
  Weapon* weapon = local != nullptr ? local->get_weapon() : nullptr;
  if (local == nullptr || weapon == nullptr || !local->is_alive() || local->is_dormant()) {
    draw_progress_indicator(section_kind::nospread, "NOSPREAD", 0.0f, "WAIT", muted);
    return;
  }

  const bool ready = !weapon->is_melee() && weapon->get_hitscan_spread() > 0.00001f;
  draw_progress_indicator(
    section_kind::nospread,
    "NOSPREAD",
    ready ? 1.0f : 0.0f,
    ready ? "READY" : "NONE",
    ready ? indicator_color(11, 232, 129) : muted);
}

inline void draw_spectator_indicator()
{

  if (ImGui::IsPopupOpen("bind_popup_context") || cat_bind::popup_open_requested()) return;

  Player* target_player = nullptr;
  const std::vector<spectator_list::spectator_entry> spectators = collect_spectator_rows(&target_player);
  if (spectators.empty()) return;

  const float scale = mono::window_scale();
  const float header_height = mono::window_header_height();
  const float width = 260.0f * scale;
  const float row_height = 32.0f * scale;
  const float height = header_height + 24.0f * scale + row_height * static_cast<float>(spectators.size()) + 6.0f * scale;
  const section_position_refs refs = section_drag_position(section_kind::spectators);
  if (refs.x == nullptr || refs.y == nullptr) return;

  ImGui::SetNextWindowPos({ *refs.x, *refs.y }, ImGuiCond_Always);
  ImGui::SetNextWindowSize({ width, height }, ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f });
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
    | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
    | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar;
  if (!menu_focused) flags |= ImGuiWindowFlags_NoInputs;

  if (ImGui::Begin(section_id(section_kind::spectators), nullptr, flags)) {
    const ImVec2 position = ImGui::GetWindowPos();
    const ImVec2 end{ position.x + width, position.y + height };
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(position, end, ImGui::GetColorU32(ImGuiCol_WindowBg), 4.0f * scale);
    mono::draw_window_header(*draw_list, position, width, { "spectators" });
    draw_list->AddRect({ position.x + 0.5f * scale, position.y + 0.5f * scale },
      { end.x - 0.5f * scale, end.y - 0.5f * scale },
      ImGui::GetColorU32(ImGuiCol_CheckMark), 4.0f * scale, 0, scale);

    if (config.visuals.spectator_list.show_target && target_player != nullptr && engine != nullptr) {
      std::string target = "watching you";
      if (entity_list == nullptr || target_player != entity_list->get_localplayer()) {
        player_info info{};
        if (engine->get_player_info(target_player->get_index(), &info)) target = "watching " + std::string{ info.name };
      }
      draw_list->AddText({ position.x + 10.0f * scale, position.y + header_height + 4.0f * scale },
        ImGui::GetColorU32(ImGuiCol_TextDisabled), target.c_str());
    }

    for (size_t index{}; index < spectators.size(); ++index) {
      const spectator_list::spectator_entry& spectator = spectators[index];
      const float y = position.y + header_height + 22.0f * scale + row_height * static_cast<float>(index);
      ImU32 name_color = ImGui::GetColorU32(ImGuiCol_Text);
      if (config.visuals.spectator_list.highlight_firstperson && spectator.firstperson) {
        RGBA_float color = config.visuals.spectator_list.firstperson_color;
        const RGBA rgba = color.to_RGBA();
        name_color = IM_COL32(rgba.r, rgba.g, rgba.b, rgba.a);
      }
      draw_list->AddText({ position.x + 10.0f * scale, y + 3.0f * scale }, name_color, spectator.name.c_str());
      if (config.visuals.spectator_list.show_modes) {
        const char* mode = spectator.firstperson ? "1ST" : "3RD";
        const ImVec2 mode_size = ImGui::CalcTextSize(mode);
        draw_list->AddText({ end.x - 10.0f * scale - mode_size.x, y + 3.0f * scale },
          spectator.firstperson ? name_color : ImGui::GetColorU32(ImGuiCol_TextDisabled), mode);
      }
    }

    static ImVec2 drag_offset{};
    static bool dragging{};
    const ImVec2 header_end{ end.x, position.y + header_height };
    if (menu_focused && ImGui::IsMouseHoveringRect(position, header_end) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      dragging = true;
      const ImVec2 mouse = ImGui::GetMousePos();
      drag_offset = { mouse.x - position.x, mouse.y - position.y };
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragging = false;
    if (dragging) {
      const ImVec2 mouse = ImGui::GetMousePos();
      ImGui::SetWindowPos({ mouse.x - drag_offset.x, mouse.y - drag_offset.y });
    }
    *refs.x = ImGui::GetWindowPos().x;
    *refs.y = ImGui::GetWindowPos().y;
    ImGui::Dummy({ width, height });
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}

}

static void draw_game_indicators()
{
  using namespace cat_indicator;
  if (config.misc.automation.stalker_enabled && ImGui::GetCurrentContext() != nullptr) {
    const int tracked = automation::profile_stalker::status_count();
    if (tracked > 0) {
      std::vector<automation::profile_stalker::presence_card> cards{};
      cards.reserve(static_cast<std::size_t>(tracked));
      for (int index = 0; index < tracked; ++index) {
        cards.push_back(automation::profile_stalker::status(index));
      }
      std::vector<mono::indicator_row> rows{};
      rows.reserve(cards.size());
      for (const auto& card : cards) {
        rows.push_back({
          .name = card.name,
          .type = card.heading,
          .value = card.summary,
          .active = card.in_server
        });
      }
      const ImVec2 position = mono::indicator_panel(
        "profile_presence",
        "presence",
        rows,
        { config.misc.automation.stalker_overlay_x, config.misc.automation.stalker_overlay_y },
        cat_menu::font_regular(),
        menu_focused);
      config.misc.automation.stalker_overlay_x = position.x;
      config.misc.automation.stalker_overlay_y = position.y;
    }
  }
  if (!should_draw_overlay()) return;

  const std::vector<section_spec> sections = build_sections();
  for (const section_spec& section : sections) {
    const section_position_refs refs = section_drag_position(section.kind);
    if (section.kind == section_kind::keybinds) {
      keybind_panel_rows panel = build_keybind_rows();
      mono::indicator_row_callback on_row = [source = std::move(panel.source)](const size_t index, const mono::indicator_row&) {
        if (index < source.size() && !source[index].target_key.empty()) {
          cat_bind::request_popup(source[index].target_key, source[index].popup_type);
        }
      };
      const ImVec2 position = mono::indicator_panel(
        section_id(section.kind), config.misc.menu.bind_window_title ? "keybinds" : "", panel.rows, section.position, cat_menu::font_regular(), menu_focused,
        menu_focused ? std::move(on_row) : mono::indicator_row_callback{});
      if (refs.x != nullptr && refs.y != nullptr) { *refs.x = position.x; *refs.y = position.y; }
      continue;
    }
    if (section.kind == section_kind::spectators) {
      draw_spectator_indicator();
      continue;
    }

    if (section.kind == section_kind::tickbase) {
      draw_tickbase_indicator();
      continue;
    }
    if (section.kind == section_kind::crit_hack) {
      draw_crit_hack_indicator();
      continue;
    }
    if (section.kind == section_kind::nospread) {
      draw_nospread_indicator();
      continue;
    }

    owned_indicator_rows panel = build_aimbot_debug_rows();
    const ImVec2 position = mono::indicator_panel(section_id(section.kind), "aimbot debug", panel.rows, section.position, cat_menu::font_regular(), menu_focused);
    if (refs.x != nullptr && refs.y != nullptr) { *refs.x = position.x; *refs.y = position.y; }
  }
}
#endif
