/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/config/config_store.cpp
 |  Y  |   autor: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/

#include "config_store.hpp"

#include "../logger.hpp"

#include "features/automation/region_selector/region_selector.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>

namespace cathook::core
{

namespace
{

std::unique_ptr<config_store> g_config_store{};

std::uint64_t parse_uint64(const std::string_view value, const std::uint64_t fallback)
{
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
    {
        return fallback;
    }

    return parsed;
}

} // namespace

config_store::config_store(std::filesystem::path root_directory)
    : m_root_directory{ std::move(root_directory) }
{
    std::error_code error{};
    std::filesystem::create_directories(config_directory(), error);
}

bool config_store::load_file(const std::string_view name)
{
    std::ifstream input{ config_path(name) };
    if (!input.is_open())
    {
        return false;
    }

    m_values.clear();

    std::string line{};
    while (std::getline(input, line))
    {
        line = trim(std::move(line));
        if (line.empty() || line.starts_with('#'))
        {
            continue;
        }

        const std::size_t separator{ line.find('=') };
        if (separator == std::string::npos)
        {
            continue;
        }

        std::string key{ trim(line.substr(0, separator)) };
        std::string value{ trim(line.substr(separator + 1)) };
        if (!key.empty())
        {
            m_values[std::move(key)] = std::move(value);
        }
    }

    m_current_name = std::string{ name };
    return true;
}

bool config_store::save_file(const std::string_view name)
{
    std::error_code error{};
    std::filesystem::create_directories(config_directory(), error);

    std::ofstream output{ config_path(name), std::ios::trunc };
    if (!output.is_open())
    {
        return false;
    }

    std::vector<std::pair<std::string, std::string>> ordered_values{};
    ordered_values.reserve(m_values.size());

    for (const auto& [key, value] : m_values)
    {
        ordered_values.emplace_back(key, value);
    }

    std::ranges::sort(ordered_values, [](const auto& left, const auto& right)
    {
        return left.first < right.first;
    });

    for (const auto& [key, value] : ordered_values)
    {
        output << key << '=' << value << '\n';
    }

    m_current_name = std::string{ name };
    return output.good();
}

bool config_store::delete_file(const std::string_view name)
{
    const bool removed{ std::filesystem::remove(config_path(name)) };
    if (removed && m_current_name == name)
    {
        m_current_name = "default";
    }

    return removed;
}

std::vector<std::string> config_store::list_files() const
{
    std::vector<std::string> names{};

    if (!std::filesystem::exists(config_directory()))
    {
        return names;
    }

    for (const auto& entry : std::filesystem::directory_iterator{ config_directory() })
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".cat")
        {
            continue;
        }

        names.emplace_back(entry.path().stem().string());
    }

    std::ranges::sort(names);
    return names;
}

const std::string& config_store::current_name() const
{
    return m_current_name;
}

void config_store::import_config(const Config& config)
{
    const bool spectator_indicator_enabled = (config.visuals.indicators.enabled_mask & Visuals::Indicators::spectators) != 0;
    const bool keybind_indicator_enabled = (config.visuals.indicators.enabled_mask & Visuals::Indicators::keybinds) != 0;

    set_bool("aimbot.master", config.aimbot.master);
    set_bool("aimbot.auto_shoot", config.aimbot.auto_shoot);
    set_int("aimbot.target_type", static_cast<int>(config.aimbot.target_type));
    set_int("aimbot.aim_at", static_cast<int>(config.aimbot.aim_at));
    set_int("aimbot.aim_mode", static_cast<int>(config.aimbot.aim_mode));
    set_int("aimbot.key", config.aimbot.key.button);
    set_int("aimbot.key_mode", static_cast<int>(config.aimbot.key.mode));
    set_float("aimbot.fov", config.aimbot.fov);
    set_float("aimbot.smooth_factor", config.aimbot.smooth_factor);
    set_float("aimbot.assist_strength", config.aimbot.assist_strength);
    set_bool("aimbot.draw_fov", config.aimbot.draw_fov);
    set_bool("aimbot.shoot_through_glass", config.aimbot.shoot_through_glass);
    set_int("aimbot.hitscan_hitboxes", static_cast<int>(config.aimbot.hitscan_hitboxes));
    set_int("aimbot.melee_hitboxes", static_cast<int>(config.aimbot.melee_hitboxes));
    set_bool("aimbot.melee_walk_to_target", config.aimbot.melee_walk_to_target);
    set_int("aimbot.projectile_mode", static_cast<int>(config.aimbot.projectile_mode));
    set_int("aimbot.projectile_hitboxes", static_cast<int>(config.aimbot.projectile_hitboxes));
    set_bool("aimbot.projectile_wall_splash", config.aimbot.projectile_wall_splash);
    set_bool("aimbot.projectile_seam_shot", config.aimbot.projectile_seam_shot);
    set_float("aimbot.projectile_splash_radius_scale", config.aimbot.projectile_splash_radius_scale);
    set_int("aimbot.projectile_path_steps", config.aimbot.projectile_path_steps);
    set_int("aimbot.projectile_splash_samples", config.aimbot.projectile_splash_samples);
    set_int("aimbot.projectile_prediction_ticks", config.aimbot.projectile_prediction_ticks);
    set_bool("aimbot.projectile_strafe_prediction", config.aimbot.projectile_strafe_prediction);
    set_float("aimbot.projectile_strafe_confidence", config.aimbot.projectile_strafe_confidence);
    set_int("aimbot.projectile_trace_interval", config.aimbot.projectile_trace_interval);
    set_bool("aimbot.projectile_splash_debug", config.aimbot.projectile_splash_debug);
    set_bool("aimbot.auto_scope", config.aimbot.auto_scope);
    set_bool("aimbot.auto_unscope", config.aimbot.auto_unscope);
    set_float("aimbot.auto_scope_threshold", config.aimbot.auto_scope_threshold);
    set_bool("aimbot.auto_rev", config.aimbot.auto_rev);
    set_bool("aimbot.auto_unrev", config.aimbot.auto_unrev);
    set_float("aimbot.auto_rev_threshold", config.aimbot.auto_rev_threshold);
    set_bool("aimbot.scoped_only", config.aimbot.scoped_only);
    set_bool("aimbot.wait_for_headshot", config.aimbot.wait_for_headshot);
    set_bool("aimbot.ignore_friends", config.aimbot.ignore_friends);
    set_bool("ipc.enabled", config.ipc.enabled);
    set_bool("ipc.auto_connect", config.ipc.auto_connect);
    set_bool("ipc.auto_ignore_local_bots", config.ipc.auto_ignore_local_bots);
    set_bool("random_crits.force_crits", config.random_crits.force_crits);
    set_bool("random_crits.always_melee_crit", config.random_crits.always_melee_crit);
    set_bool("random_crits.save_bucket", config.random_crits.save_bucket);
    set_bool("random_crits.respect_bucket", config.random_crits.respect_bucket);
    set_bool("random_crits.advanced_stats", config.random_crits.advanced_stats);
    set_int("random_crits.seed_scan", config.random_crits.seed_scan);

    set_bool("esp.master", config.esp.master);
    set_bool("esp.lerp", config.esp.lerp);
    set_float("esp.lerp_speed", config.esp.lerp_speed);
    set_color("esp.player.enemy_color", config.esp.player.enemy_color);
    set_color("esp.player.team_color", config.esp.player.team_color);
    set_color("esp.player.friend_color", config.esp.player.friend_color);
    set_bool("esp.player.box", config.esp.player.box);
    set_int("esp.player.box_style", static_cast<int>(config.esp.player.box_style));
    set_bool("esp.player.health_bar", config.esp.player.health_bar);
    set_bool("esp.player.name", config.esp.player.name);
    set_bool("esp.player.class_icon", config.esp.player.class_icon);
    set_float("esp.player.class_icon_scale", config.esp.player.class_icon_scale);
    set_bool("esp.player.class_icon_teammates", config.esp.player.class_icon_teammates);
    set_bool("esp.player.head_emoji", config.esp.player.head_emoji);
    set_float("esp.player.head_emoji_scale", config.esp.player.head_emoji_scale);
    set_int("esp.player.head_emoji_style", config.esp.player.head_emoji_style);
    set_bool("esp.player.head_emoji_teammates", config.esp.player.head_emoji_teammates);
    set_bool("esp.player.flags.target_indicator", config.esp.player.flags.target_indicator);
    set_bool("esp.player.flags.friend_indicator", config.esp.player.flags.friend_indicator);
    set_bool("esp.player.flags.scoped_indicator", config.esp.player.flags.scoped_indicator);
    set_bool("esp.player.enemy", config.esp.player.enemy);
    set_bool("esp.player.team", config.esp.player.team);
    set_bool("esp.player.friends", config.esp.player.friends);
    set_bool("esp.pickup.box", config.esp.pickup.box);
    set_int("esp.pickup.box_style", static_cast<int>(config.esp.pickup.box_style));
    set_bool("esp.pickup.name", config.esp.pickup.name);
    set_bool("esp.intelligence.box", config.esp.intelligence.box);
    set_int("esp.intelligence.box_style", static_cast<int>(config.esp.intelligence.box_style));
    set_bool("esp.intelligence.name", config.esp.intelligence.name);
    set_bool("esp.buildings.box", config.esp.buildings.box);
    set_int("esp.buildings.box_style", static_cast<int>(config.esp.buildings.box_style));
    set_bool("esp.buildings.health_bar", config.esp.buildings.health_bar);
    set_bool("esp.buildings.name", config.esp.buildings.name);
    set_bool("esp.buildings.team", config.esp.buildings.team);

    set_bool("glow.master", config.glow.master);
    set_int("glow.outline_scale", config.glow.outline_scale);
    set_float("glow.blur_scale", config.glow.blur_scale);
    set_float("glow.start", config.glow.start);
    set_float("glow.end", config.glow.end);
    set_bool("glow.smooth_alpha", config.glow.smooth_alpha);
    set_bool("glow.filled_body", config.glow.filled_body);
    set_bool("glow.player.enemy", config.glow.player.enemy);
    set_bool("glow.player.team", config.glow.player.team);
    set_bool("glow.player.friends", config.glow.player.friends);
    set_bool("glow.player.local", config.glow.player.local);
    set_color("glow.player.enemy_color", config.glow.player.enemy_color);
    set_color("glow.player.enemy_color_z", config.glow.player.enemy_color_z);
    set_color("glow.player.team_color", config.glow.player.team_color);
    set_color("glow.player.team_color_z", config.glow.player.team_color_z);
    set_color("glow.player.friend_color", config.glow.player.friend_color);
    set_color("glow.player.friend_color_z", config.glow.player.friend_color_z);
    set_color("glow.player.local_color", config.glow.player.local_color);

    set_bool("chams.master", config.chams.master);
    set_bool("chams.player.enemy", config.chams.player.enemy);
    set_color("chams.player.enemy_color", config.chams.player.enemy_color);
    set_int("chams.player.enemy_material_type", static_cast<int>(config.chams.player.enemy_material_type));
    set_color("chams.player.enemy_color_z", config.chams.player.enemy_color_z);
    set_int("chams.player.enemy_material_z_type", static_cast<int>(config.chams.player.enemy_material_z_type));
    set_bool("chams.player.enemy_flags.ignore_z", config.chams.player.enemy_flags.ignore_z);
    set_color("chams.player.enemy_overlay_color", config.chams.player.enemy_overlay_color);
    set_int("chams.player.enemy_overlay_material_type", static_cast<int>(config.chams.player.enemy_overlay_material_type));
    set_color("chams.player.enemy_overlay_color_z", config.chams.player.enemy_overlay_color_z);
    set_int("chams.player.enemy_overlay_material_z_type", static_cast<int>(config.chams.player.enemy_overlay_material_z_type));
    set_bool("chams.player.enemy_overlay_flags.ignore_z", config.chams.player.enemy_overlay_flags.ignore_z);
    set_bool("chams.player.team", config.chams.player.team);
    set_color("chams.player.team_color", config.chams.player.team_color);
    set_int("chams.player.team_material_type", static_cast<int>(config.chams.player.team_material_type));
    set_color("chams.player.team_color_z", config.chams.player.team_color_z);
    set_int("chams.player.team_material_z_type", static_cast<int>(config.chams.player.team_material_z_type));
    set_bool("chams.player.team_flags.ignore_z", config.chams.player.team_flags.ignore_z);
    set_bool("chams.player.friends", config.chams.player.friends);
    set_color("chams.player.friend_color", config.chams.player.friend_color);
    set_int("chams.player.friend_material_type", static_cast<int>(config.chams.player.friend_material_type));
    set_color("chams.player.friend_color_z", config.chams.player.friend_color_z);
    set_int("chams.player.friend_material_z_type", static_cast<int>(config.chams.player.friend_material_z_type));
    set_bool("chams.player.friends_flags.ignore_z", config.chams.player.friends_flags.ignore_z);
    set_bool("chams.player.local", config.chams.player.local);
    set_color("chams.player.local_color", config.chams.player.local_color);
    set_int("chams.player.local_material_type", static_cast<int>(config.chams.player.local_material_type));

    set_bool("visuals.removals.scope", config.visuals.removals.scope);
    set_bool("visuals.removals.zoom", config.visuals.removals.zoom);
    set_bool("visuals.thirdperson.enabled", config.visuals.thirdperson.enabled);
    set_bool("visuals.thirdperson.crosshair", config.visuals.thirdperson.crosshair);
    set_float("visuals.thirdperson.distance", config.visuals.thirdperson.distance);
    set_float("visuals.thirdperson.right", config.visuals.thirdperson.right);
    set_float("visuals.thirdperson.up", config.visuals.thirdperson.up);
    set_bool("visuals.thirdperson.scale", config.visuals.thirdperson.scale);
    set_bool("visuals.thirdperson.collision", config.visuals.thirdperson.collision);
    set_bool("visuals.hitmarker.enabled", config.visuals.hitmarker.enabled);
    set_bool("visuals.hitmarker.damage_text", config.visuals.hitmarker.damage_text);
    set_float("visuals.hitmarker.duration", config.visuals.hitmarker.duration);
    set_float("visuals.hitmarker.size", config.visuals.hitmarker.size);
    set_color("visuals.hitmarker.color", config.visuals.hitmarker.color);
    set_color("visuals.hitmarker.crit_color", config.visuals.hitmarker.crit_color);
    set_color("visuals.hitmarker.headshot_color", config.visuals.hitmarker.headshot_color);
    set_int("visuals.indicators.enabled_mask", static_cast<int>(config.visuals.indicators.enabled_mask));
    set_float("visuals.indicators.x", config.visuals.indicators.x);
    set_float("visuals.indicators.y", config.visuals.indicators.y);
    set_float("visuals.indicators.ticks_x", config.visuals.indicators.legacy_ticks_x);
    set_float("visuals.indicators.ticks_y", config.visuals.indicators.legacy_ticks_y);
    set_float("visuals.indicators.random_crits_x", config.visuals.indicators.random_crits_x);
    set_float("visuals.indicators.random_crits_y", config.visuals.indicators.random_crits_y);
    set_float("visuals.indicators.keybinds_x", config.visuals.indicators.keybinds_x);
    set_float("visuals.indicators.keybinds_y", config.visuals.indicators.keybinds_y);
    set_bool("visuals.spectator_list.enabled", spectator_indicator_enabled);
    set_bool("visuals.spectator_list.show_target", config.visuals.spectator_list.show_target);
    set_bool("visuals.spectator_list.show_modes", config.visuals.spectator_list.show_modes);
    set_bool("visuals.spectator_list.highlight_firstperson", config.visuals.spectator_list.highlight_firstperson);
    set_float("visuals.spectator_list.x", config.visuals.spectator_list.x);
    set_float("visuals.spectator_list.y", config.visuals.spectator_list.y);
    set_color("visuals.spectator_list.firstperson_color", config.visuals.spectator_list.firstperson_color);
    set_bool("visuals.override_fov", config.visuals.override_fov);
    set_float("visuals.custom_fov", config.visuals.custom_fov);
    set_bool("visuals.override_viewmodel_fov", config.visuals.override_viewmodel_fov);
    set_float("visuals.custom_viewmodel_fov", config.visuals.custom_viewmodel_fov);

    set_bool("misc.movement.bhop", config.misc.movement.bhop);
    set_int("misc.movement.auto_strafe", static_cast<int>(config.misc.movement.auto_strafe));
    set_float("misc.movement.auto_strafe_turn_scale", config.misc.movement.auto_strafe_turn_scale);
    set_float("misc.movement.auto_strafe_max_delta", config.misc.movement.auto_strafe_max_delta);
    set_bool("misc.movement.no_push", config.misc.movement.no_push);
    set_bool("misc.movement.taunt_slide", config.misc.movement.taunt_slide);
    set_bool("misc.exploits.bypasspure", config.misc.exploits.bypasspure);
    set_bool("misc.exploits.pure_bypass", config.misc.exploits.pure_bypass);
    set_bool("misc.exploits.cheats_bypass", config.misc.exploits.cheats_bypass);
    set_bool("misc.exploits.vac_bypass", config.misc.exploits.vac_bypass);
    set_bool("misc.exploits.network_fix", config.misc.exploits.network_fix);
    set_bool("misc.exploits.tickbase", config.misc.exploits.tickbase);
    set_bool("misc.exploits.tickbase_recharge", config.misc.exploits.tickbase_recharge);
    set_bool("misc.exploits.doubletap", config.misc.exploits.doubletap);
    set_int("misc.exploits.doubletap_key", config.misc.exploits.doubletap_key.button);
    set_int("misc.exploits.doubletap_key_mode", static_cast<int>(config.misc.exploits.doubletap_key.mode));
    set_int("misc.exploits.doubletap_ticks", config.misc.exploits.doubletap_ticks);
    set_bool("misc.exploits.warp", config.misc.exploits.warp);
    set_int("misc.exploits.warp_key", config.misc.exploits.warp_key.button);
    set_int("misc.exploits.warp_key_mode", static_cast<int>(config.misc.exploits.warp_key.mode));
    set_int("misc.exploits.warp_ticks", config.misc.exploits.warp_ticks);
    set_bool("misc.exploits.fakelag", config.misc.exploits.fakelag);
    set_int("misc.exploits.fakelag_ticks", config.misc.exploits.fakelag_ticks);
    set_bool("misc.exploits.anti_aim", config.misc.exploits.anti_aim);
    set_int("misc.exploits.anti_aim_real_pitch", static_cast<int>(config.misc.exploits.anti_aim_real_pitch));
    set_int("misc.exploits.anti_aim_fake_pitch", static_cast<int>(config.misc.exploits.anti_aim_fake_pitch));
    set_int("misc.exploits.anti_aim_real_yaw_base", static_cast<int>(config.misc.exploits.anti_aim_real_yaw_base));
    set_int("misc.exploits.anti_aim_fake_yaw_base", static_cast<int>(config.misc.exploits.anti_aim_fake_yaw_base));
    set_int("misc.exploits.anti_aim_real_yaw", static_cast<int>(config.misc.exploits.anti_aim_real_yaw));
    set_int("misc.exploits.anti_aim_fake_yaw", static_cast<int>(config.misc.exploits.anti_aim_fake_yaw));
    set_float("misc.exploits.anti_aim_real_yaw_offset", config.misc.exploits.anti_aim_real_yaw_offset);
    set_float("misc.exploits.anti_aim_fake_yaw_offset", config.misc.exploits.anti_aim_fake_yaw_offset);
    set_float("misc.exploits.anti_aim_spin_speed", config.misc.exploits.anti_aim_spin_speed);
    set_bool("misc.exploits.anti_aim_anti_overlap", config.misc.exploits.anti_aim_anti_overlap);
    set_bool("misc.exploits.antiwarp", config.misc.exploits.antiwarp);
    set_bool("misc.exploits.setup_bones_optimization", config.misc.exploits.setup_bones_optimization);
    set_bool("misc.exploits.equip_region_unlock", config.misc.exploits.equip_region_unlock);
    set_bool("misc.exploits.ping_reducer", config.misc.exploits.ping_reducer);
    set_int("misc.exploits.ping_target", config.misc.exploits.ping_target);
    set_bool("misc.exploits.no_engine_sleep", config.misc.exploits.no_engine_sleep);
    set_bool("misc.exploits.null_graphics", config.misc.exploits.null_graphics);
    set_bool("misc.exploits.null_graphics_render_stubs", config.misc.exploits.null_graphics_render_stubs);
    set_bool("misc.exploits.experimental_nographic_hooks", config.misc.exploits.experimental_nographic_hooks);
    set_bool("misc.exploits.keybind_indicator", keybind_indicator_enabled);
    set_float("misc.exploits.keybind_indicator_x", config.visuals.indicators.keybinds_x);
    set_float("misc.exploits.keybind_indicator_y", config.visuals.indicators.keybinds_y);
    set_bool("misc.menu.use_custom_font", config.misc.menu.use_custom_font);
    set_string("misc.menu.custom_font", config.misc.menu.custom_font);
    set_bool("misc.automation.auto_class_select", config.misc.automation.auto_class_select);
    set_int("misc.automation.class_selected", static_cast<int>(config.misc.automation.class_selected));
    set_bool("misc.automation.anti_afk", config.misc.automation.anti_afk);
    set_bool("misc.automation.anti_autobalance", config.misc.automation.anti_autobalance);
    set_bool("misc.automation.anti_motd", config.misc.automation.anti_motd);
    set_bool("misc.automation.auto_vote_map", config.misc.automation.auto_vote_map);
    set_int("misc.automation.auto_vote_map_option", config.misc.automation.auto_vote_map_option);
    set_bool("misc.automation.noisemaker_spam", config.misc.automation.noisemaker_spam);
    set_int("misc.automation.voice_command_spam", static_cast<int>(config.misc.automation.voice_command_spam));
    set_bool("misc.automation.micspam", config.misc.automation.micspam);
    set_int("misc.automation.micspam_interval_on_seconds", config.misc.automation.micspam_interval_on_seconds);
    set_int("misc.automation.micspam_interval_off_seconds", config.misc.automation.micspam_interval_off_seconds);
    set_bool("misc.automation.auto_item", config.misc.automation.auto_item);
    set_int("misc.automation.auto_item_interval_ms", config.misc.automation.auto_item_interval_ms);
    set_bool("misc.automation.auto_item_weapons", config.misc.automation.auto_item_weapons);
    set_string("misc.automation.auto_item_primary", config.misc.automation.auto_item_primary);
    set_string("misc.automation.auto_item_secondary", config.misc.automation.auto_item_secondary);
    set_string("misc.automation.auto_item_melee", config.misc.automation.auto_item_melee);
    set_bool("misc.automation.auto_item_hats", config.misc.automation.auto_item_hats);
    set_string("misc.automation.auto_item_hat1", config.misc.automation.auto_item_hat1);
    set_string("misc.automation.auto_item_hat2", config.misc.automation.auto_item_hat2);
    set_string("misc.automation.auto_item_hat3", config.misc.automation.auto_item_hat3);
    set_bool("misc.automation.auto_item_noisemaker", config.misc.automation.auto_item_noisemaker);
    set_bool("misc.automation.auto_item_debug", config.misc.automation.auto_item_debug);
    set_bool("misc.automation.autotaunt", config.misc.automation.autotaunt);
    set_float("misc.automation.autotaunt_chance", config.misc.automation.autotaunt_chance);
    set_float("misc.automation.autotaunt_safety_distance", config.misc.automation.autotaunt_safety_distance);
    set_int("misc.automation.autotaunt_weapon_slot", config.misc.automation.autotaunt_weapon_slot);
    set_int("misc.automation.chatspam", static_cast<int>(config.misc.automation.chatspam));
    set_bool("misc.automation.chatspam_random", config.misc.automation.chatspam_random);
    set_bool("misc.automation.chatspam_team", config.misc.automation.chatspam_team);
    set_int("misc.automation.chatspam_delay_ms", config.misc.automation.chatspam_delay_ms);
    set_string("misc.automation.chatspam_file", config.misc.automation.chatspam_file);
    set_int("misc.automation.killsay", static_cast<int>(config.misc.automation.killsay));
    set_int("misc.automation.killsay_delay_ms", config.misc.automation.killsay_delay_ms);
    set_string("misc.automation.killsay_file", config.misc.automation.killsay_file);
    set_bool("misc.automation.custom_announcer", config.misc.automation.custom_announcer);
    set_bool("misc.automation.mvm_instant_respawn", config.misc.automation.mvm_instant_respawn);
    set_bool("misc.automation.mvm_instant_revive", config.misc.automation.mvm_instant_revive);
    set_bool("misc.automation.allow_mvm_inspect", config.misc.automation.allow_mvm_inspect);
    set_bool("misc.automation.auto_mvm_ready_up", config.misc.automation.auto_mvm_ready_up);
    set_bool("misc.automation.mvm_buybot", config.misc.automation.mvm_buybot);
    set_int("misc.automation.mvm_buybot_max_cash", config.misc.automation.mvm_buybot_max_cash);
    set_bool("misc.automation.medic_autoheal", config.misc.automation.medic_autoheal);
    set_bool("misc.automation.medic_autovacc", config.misc.automation.medic_autovacc);
    set_bool("misc.automation.medic_autouber", config.misc.automation.medic_autouber);
    set_bool("misc.automation.medic_auto_crossbow", config.misc.automation.medic_auto_crossbow);
    set_int("misc.automation.medic_heal_targets_mask", static_cast<int>(config.misc.automation.medic_heal_targets_mask));
    set_bool("misc.automation.medic_heal_only", config.misc.automation.medic_heal_only);
    set_bool("misc.automation.auto_report", config.misc.automation.auto_report);
    set_bool("misc.automation.auto_queue", config.misc.automation.auto_queue);
    set_bool("misc.automation.auto_requeue", config.misc.automation.auto_requeue);
    set_bool("misc.automation.requeue_on_kick", config.misc.automation.requeue_on_kick);
    set_bool("misc.automation.auto_casual_join", config.misc.automation.auto_casual_join);
    set_int("misc.automation.auto_queue_mode", config.misc.automation.auto_queue_mode);
    set_int("misc.automation.rq_if_players_lte", config.misc.automation.rq_if_players_lte);
    set_int("misc.automation.rq_if_players_gte", config.misc.automation.rq_if_players_gte);
    set_bool("misc.automation.rq_ignore_friends", config.misc.automation.rq_ignore_friends);
    set_int("misc.automation.requeue_action", static_cast<int>(config.misc.automation.requeue_action));
    set_bool("misc.automation.region_selector", config.misc.automation.region_selector);
    set_string(
        "misc.automation.region_selector_allowed_mask",
        std::to_string(config.misc.automation.region_selector_allowed_mask));
    set_bool("misc.automation.navbot_enabled", config.misc.automation.navbot_enabled);
    set_bool("misc.automation.navbot_draw_path", config.misc.automation.navbot_draw_path);
    set_bool("misc.automation.navbot_dont_path_during_warmup", config.misc.automation.navbot_dont_path_during_warmup);
    set_bool("misc.automation.navbot_dont_path_unless_match_started", config.misc.automation.navbot_dont_path_unless_match_started);
    set_bool("misc.automation.navbot_warmup_only_blu_cp_pl", config.misc.automation.navbot_warmup_only_blu_cp_pl);
    set_bool("misc.automation.navbot_look_at_path", config.misc.automation.navbot_look_at_path);
    set_bool("misc.automation.navbot_auto_weapon", config.misc.automation.navbot_auto_weapon);
    set_float("misc.automation.navbot_look_at_path_speed", config.misc.automation.navbot_look_at_path_speed);
    set_float("misc.automation.navbot_crumb_blacklist_seconds", config.misc.automation.navbot_crumb_blacklist_seconds);
    set_bool("misc.automation.navbot_debug_text", config.misc.automation.navbot_debug_text);
    set_int("misc.automation.navbot_excluded_jobs_mask", static_cast<int>(config.misc.automation.navbot_excluded_jobs_mask));
    set_int("debug.font_height", config.debug.font_height);
    set_int("debug.font_weight", config.debug.font_weight);
    set_bool("debug.render_all_entities", config.debug.debug_render_all_entities);
    set_bool("debug.show_active_flag_ids_of_players", config.debug.show_active_flag_ids_of_players);
    set_bool("debug.disable_friend_checks", config.debug.disable_friend_checks);
}

void config_store::export_config(Config& config) const
{
    int legacy_indicator_mask{ 0 };
    if (get_bool("misc.exploits.tickbase_indicator", config.misc.exploits.legacy_tickbase_indicator))
    {
        legacy_indicator_mask |= static_cast<int>(Visuals::Indicators::tickbase);
    }
    if (get_bool("misc.exploits.keybind_indicator", config.misc.exploits.keybind_indicator))
    {
        legacy_indicator_mask |= static_cast<int>(Visuals::Indicators::keybinds);
    }
    if (get_bool("visuals.spectator_list.enabled", config.visuals.spectator_list.enabled))
    {
        legacy_indicator_mask |= static_cast<int>(Visuals::Indicators::spectators);
    }
    config.aimbot.master = get_bool("aimbot.master", config.aimbot.master);
    config.aimbot.auto_shoot = get_bool("aimbot.auto_shoot", config.aimbot.auto_shoot);
    config.aimbot.target_type = static_cast<Aim::TargetType>(get_int("aimbot.target_type", static_cast<int>(config.aimbot.target_type)));
    config.aimbot.aim_at = static_cast<uint32_t>(get_int("aimbot.aim_at", static_cast<int>(config.aimbot.aim_at))) & Aim::aim_at_all;
    const int legacy_aim_mode = get_bool("aimbot.silent", config.aimbot.aim_mode == Aim::AimMode::PSILENT)
      ? static_cast<int>(Aim::AimMode::PSILENT)
      : static_cast<int>(Aim::AimMode::PLAIN);
    config.aimbot.aim_mode = static_cast<Aim::AimMode>(get_int("aimbot.aim_mode", legacy_aim_mode));
    config.aimbot.key.button = get_int("aimbot.key", config.aimbot.key.button);
    config.aimbot.key.mode = static_cast<button::mode_type>(std::clamp(
        get_int("aimbot.key_mode", static_cast<int>(config.aimbot.key.mode)),
        0,
        2));
    reset_button_state(config.aimbot.key);
    config.aimbot.fov = get_float("aimbot.fov", config.aimbot.fov);
    config.aimbot.smooth_factor = get_float("aimbot.smooth_factor", config.aimbot.smooth_factor);
    config.aimbot.assist_strength = std::clamp(
        get_float("aimbot.assist_strength", config.aimbot.assist_strength),
        0.0f,
        100.0f);
    config.aimbot.draw_fov = get_bool("aimbot.draw_fov", config.aimbot.draw_fov);
    config.aimbot.shoot_through_glass = get_bool("aimbot.shoot_through_glass", config.aimbot.shoot_through_glass);
    config.aimbot.hitscan_hitboxes = static_cast<uint32_t>(get_int("aimbot.hitscan_hitboxes", static_cast<int>(config.aimbot.hitscan_hitboxes)));
    config.aimbot.melee_hitboxes = static_cast<uint32_t>(get_int("aimbot.melee_hitboxes", static_cast<int>(config.aimbot.melee_hitboxes)));
    config.aimbot.melee_walk_to_target = get_bool("aimbot.melee_walk_to_target", config.aimbot.melee_walk_to_target);
    config.aimbot.projectile_mode = static_cast<Aim::ProjectileMode>(std::clamp(
        get_int("aimbot.projectile_mode", static_cast<int>(config.aimbot.projectile_mode)),
        0,
        3));
    config.aimbot.projectile_hitboxes = static_cast<uint32_t>(get_int("aimbot.projectile_hitboxes", static_cast<int>(config.aimbot.projectile_hitboxes)));
    config.aimbot.projectile_wall_splash = get_bool("aimbot.projectile_wall_splash", config.aimbot.projectile_wall_splash);
    config.aimbot.projectile_seam_shot = get_bool("aimbot.projectile_seam_shot", config.aimbot.projectile_seam_shot);
    config.aimbot.projectile_splash_radius_scale = get_float("aimbot.projectile_splash_radius_scale", config.aimbot.projectile_splash_radius_scale);
    config.aimbot.projectile_path_steps = std::clamp(
        get_int("aimbot.projectile_path_steps", config.aimbot.projectile_path_steps),
        2,
        64);
    config.aimbot.projectile_splash_samples = std::clamp(
        get_int("aimbot.projectile_splash_samples", config.aimbot.projectile_splash_samples),
        4,
        64);
    config.aimbot.projectile_prediction_ticks = std::clamp(
        get_int("aimbot.projectile_prediction_ticks", config.aimbot.projectile_prediction_ticks),
        8,
        180);
    config.aimbot.projectile_strafe_prediction = get_bool(
        "aimbot.projectile_strafe_prediction",
        config.aimbot.projectile_strafe_prediction);
    config.aimbot.projectile_strafe_confidence = std::clamp(
        get_float("aimbot.projectile_strafe_confidence", config.aimbot.projectile_strafe_confidence),
        0.0f,
        100.0f);
    config.aimbot.projectile_trace_interval = std::clamp(
        get_int("aimbot.projectile_trace_interval", config.aimbot.projectile_trace_interval),
        1,
        8);
    config.aimbot.projectile_splash_debug = get_bool(
        "aimbot.projectile_splash_debug",
        config.aimbot.projectile_splash_debug);
    config.aimbot.auto_scope = get_bool("aimbot.auto_scope", config.aimbot.auto_scope);
    config.aimbot.auto_unscope = get_bool("aimbot.auto_unscope", config.aimbot.auto_unscope);
    config.aimbot.auto_scope_threshold = get_float(
        "aimbot.auto_scope_threshold",
        get_float("aimbot.auto_scope_min_distance", config.aimbot.auto_scope_threshold));
    config.aimbot.auto_rev = get_bool("aimbot.auto_rev", config.aimbot.auto_rev);
    config.aimbot.auto_unrev = get_bool("aimbot.auto_unrev", config.aimbot.auto_unrev);
    config.aimbot.auto_rev_threshold = get_float("aimbot.auto_rev_threshold", config.aimbot.auto_rev_threshold);
    config.aimbot.scoped_only = get_bool("aimbot.scoped_only", config.aimbot.scoped_only);
    config.aimbot.wait_for_headshot = get_bool("aimbot.wait_for_headshot", config.aimbot.wait_for_headshot);
    config.aimbot.ignore_friends = get_bool("aimbot.ignore_friends", config.aimbot.ignore_friends);
    config.ipc.enabled = get_bool("ipc.enabled", config.ipc.enabled);
    config.ipc.auto_connect = get_bool("ipc.auto_connect", config.ipc.auto_connect);
    config.ipc.auto_ignore_local_bots = get_bool("ipc.auto_ignore_local_bots", config.ipc.auto_ignore_local_bots);
    config.random_crits.force_crits = get_bool(
        "random_crits.force_crits",
        get_bool("crithack.force_crits", config.random_crits.force_crits));
    config.random_crits.always_melee_crit = get_bool(
        "random_crits.always_melee_crit",
        get_bool("crithack.always_melee_crit", config.random_crits.always_melee_crit));
    config.random_crits.save_bucket = get_bool(
        "random_crits.save_bucket",
        get_bool("crithack.save_bucket", config.random_crits.save_bucket));
    config.random_crits.respect_bucket = get_bool(
        "random_crits.respect_bucket",
        get_bool("crithack.respect_bucket", config.random_crits.respect_bucket));
    config.random_crits.advanced_stats = get_bool(
        "random_crits.advanced_stats",
        get_bool("crithack.advanced_stats", config.random_crits.advanced_stats));
    config.random_crits.seed_scan = std::clamp(
        get_int("random_crits.seed_scan", get_int("crithack.seed_scan", config.random_crits.seed_scan)),
        256,
        8192);

    config.esp.master = get_bool("esp.master", config.esp.master);
    config.esp.lerp = get_bool("esp.lerp", config.esp.lerp);
    config.esp.lerp_speed = std::clamp(get_float("esp.lerp_speed", config.esp.lerp_speed), 1.0f, 40.0f);
    config.esp.player.enemy_color = get_color("esp.player.enemy_color", config.esp.player.enemy_color);
    config.esp.player.team_color = get_color("esp.player.team_color", config.esp.player.team_color);
    config.esp.player.friend_color = get_color("esp.player.friend_color", config.esp.player.friend_color);
    config.esp.player.box = get_bool("esp.player.box", config.esp.player.box);
    config.esp.player.box_style = static_cast<Esp::box_type>(std::clamp(
        get_int("esp.player.box_style", static_cast<int>(config.esp.player.box_style)),
        0,
        4));
    config.esp.player.health_bar = get_bool("esp.player.health_bar", config.esp.player.health_bar);
    config.esp.player.name = get_bool("esp.player.name", config.esp.player.name);
    config.esp.player.class_icon = get_bool("esp.player.class_icon", config.esp.player.class_icon);
    config.esp.player.class_icon_scale = get_float("esp.player.class_icon_scale", config.esp.player.class_icon_scale);
    config.esp.player.class_icon_teammates = get_bool("esp.player.class_icon_teammates", config.esp.player.class_icon_teammates);
    config.esp.player.head_emoji = get_bool("esp.player.head_emoji", config.esp.player.head_emoji);
    config.esp.player.head_emoji_scale = get_float("esp.player.head_emoji_scale", config.esp.player.head_emoji_scale);
    config.esp.player.head_emoji_style = std::clamp(get_int("esp.player.head_emoji_style", config.esp.player.head_emoji_style), 0, 1);
    config.esp.player.head_emoji_teammates = get_bool("esp.player.head_emoji_teammates", config.esp.player.head_emoji_teammates);
    config.esp.player.flags.target_indicator = get_bool("esp.player.flags.target_indicator", config.esp.player.flags.target_indicator);
    config.esp.player.flags.friend_indicator = get_bool("esp.player.flags.friend_indicator", config.esp.player.flags.friend_indicator);
    config.esp.player.flags.scoped_indicator = get_bool("esp.player.flags.scoped_indicator", config.esp.player.flags.scoped_indicator);
    config.esp.player.enemy = get_bool("esp.player.enemy", config.esp.player.enemy);
    config.esp.player.team = get_bool("esp.player.team", config.esp.player.team);
    config.esp.player.friends = get_bool("esp.player.friends", config.esp.player.friends);
    config.esp.pickup.box = get_bool("esp.pickup.box", config.esp.pickup.box);
    config.esp.pickup.box_style = static_cast<Esp::box_type>(std::clamp(
        get_int("esp.pickup.box_style", static_cast<int>(config.esp.pickup.box_style)),
        0,
        4));
    config.esp.pickup.name = get_bool("esp.pickup.name", config.esp.pickup.name);
    config.esp.intelligence.box = get_bool("esp.intelligence.box", config.esp.intelligence.box);
    config.esp.intelligence.box_style = static_cast<Esp::box_type>(std::clamp(
        get_int("esp.intelligence.box_style", static_cast<int>(config.esp.intelligence.box_style)),
        0,
        4));
    config.esp.intelligence.name = get_bool("esp.intelligence.name", config.esp.intelligence.name);
    config.esp.buildings.box = get_bool("esp.buildings.box", config.esp.buildings.box);
    config.esp.buildings.box_style = static_cast<Esp::box_type>(std::clamp(
        get_int("esp.buildings.box_style", static_cast<int>(config.esp.buildings.box_style)),
        0,
        4));
    config.esp.buildings.health_bar = get_bool("esp.buildings.health_bar", config.esp.buildings.health_bar);
    config.esp.buildings.name = get_bool("esp.buildings.name", config.esp.buildings.name);
    config.esp.buildings.team = get_bool("esp.buildings.team", config.esp.buildings.team);

    config.glow.master = get_bool("glow.master", get_bool("esp.glow.enabled", config.glow.master));
    config.glow.outline_scale = std::clamp(
        get_int("glow.outline_scale", get_int("esp.glow.stencil", config.glow.outline_scale)),
        0,
        10);
    config.glow.blur_scale = std::clamp(
        get_float("glow.blur_scale", get_float("esp.glow.blur", config.glow.blur_scale)),
        0.0f,
        10.0f);
    config.glow.start = std::clamp(
        get_float("glow.start", get_float("esp.glow.start", config.glow.start)),
        0.0f,
        8192.0f);
    config.glow.end = std::clamp(
        get_float("glow.end", get_float("esp.glow.end", config.glow.end)),
        0.0f,
        8192.0f);
    if (config.glow.end < config.glow.start) {
        config.glow.end = config.glow.start;
    }
    config.glow.smooth_alpha = get_bool("glow.smooth_alpha", get_bool("esp.glow.smooth_alpha", config.glow.smooth_alpha));
    config.glow.filled_body = get_bool("glow.filled_body", config.glow.filled_body);
    config.glow.player.enemy = get_bool("glow.player.enemy", config.esp.player.enemy);
    config.glow.player.team = get_bool("glow.player.team", config.esp.player.team);
    config.glow.player.friends = get_bool("glow.player.friends", config.esp.player.friends);
    config.glow.player.local = get_bool("glow.player.local", config.glow.player.local);
    config.glow.player.enemy_color = get_color("glow.player.enemy_color", config.esp.player.enemy_color);
    config.glow.player.enemy_color_z = get_color("glow.player.enemy_color_z", config.glow.player.enemy_color);
    config.glow.player.team_color = get_color("glow.player.team_color", config.esp.player.team_color);
    config.glow.player.team_color_z = get_color("glow.player.team_color_z", config.glow.player.team_color);
    config.glow.player.friend_color = get_color("glow.player.friend_color", config.esp.player.friend_color);
    config.glow.player.friend_color_z = get_color("glow.player.friend_color_z", config.glow.player.friend_color);
    config.glow.player.local_color = get_color("glow.player.local_color", config.glow.player.local_color);

    config.chams.master = get_bool("chams.master", config.chams.master);
    config.chams.player.enemy = get_bool("chams.player.enemy", config.chams.player.enemy);
    config.chams.player.enemy_color = get_color("chams.player.enemy_color", config.chams.player.enemy_color);
    config.chams.player.enemy_material_type = static_cast<Chams::Player::material_type>(
        get_int("chams.player.enemy_material_type", static_cast<int>(config.chams.player.enemy_material_type)));
    config.chams.player.enemy_color_z = get_color("chams.player.enemy_color_z", config.chams.player.enemy_color_z);
    config.chams.player.enemy_material_z_type = static_cast<Chams::Player::material_type>(
        get_int("chams.player.enemy_material_z_type", static_cast<int>(config.chams.player.enemy_material_z_type)));
    config.chams.player.enemy_flags.ignore_z = get_bool("chams.player.enemy_flags.ignore_z", config.chams.player.enemy_flags.ignore_z);
    config.chams.player.enemy_overlay_color = get_color("chams.player.enemy_overlay_color", config.chams.player.enemy_overlay_color);
    config.chams.player.enemy_overlay_material_type = static_cast<Chams::Player::material_type>(
        get_int("chams.player.enemy_overlay_material_type", static_cast<int>(config.chams.player.enemy_overlay_material_type)));
    config.chams.player.enemy_overlay_color_z = get_color("chams.player.enemy_overlay_color_z", config.chams.player.enemy_overlay_color_z);
    config.chams.player.enemy_overlay_material_z_type = static_cast<Chams::Player::material_type>(
        get_int("chams.player.enemy_overlay_material_z_type", static_cast<int>(config.chams.player.enemy_overlay_material_z_type)));
    config.chams.player.enemy_overlay_flags.ignore_z = get_bool("chams.player.enemy_overlay_flags.ignore_z", config.chams.player.enemy_overlay_flags.ignore_z);
    config.chams.player.team = get_bool("chams.player.team", config.chams.player.team);
    config.chams.player.team_color = get_color("chams.player.team_color", config.chams.player.team_color);
    config.chams.player.team_material_type = static_cast<Chams::Player::material_type>(
        get_int("chams.player.team_material_type", static_cast<int>(config.chams.player.team_material_type)));
    config.chams.player.team_color_z = get_color("chams.player.team_color_z", config.chams.player.team_color_z);
    config.chams.player.team_material_z_type = static_cast<Chams::Player::material_type>(
        get_int("chams.player.team_material_z_type", static_cast<int>(config.chams.player.team_material_z_type)));
    config.chams.player.team_flags.ignore_z = get_bool("chams.player.team_flags.ignore_z", config.chams.player.team_flags.ignore_z);
    config.chams.player.friends = get_bool("chams.player.friends", config.chams.player.friends);
    config.chams.player.friend_color = get_color("chams.player.friend_color", config.chams.player.friend_color);
    config.chams.player.friend_material_type = static_cast<Chams::Player::material_type>(
        get_int("chams.player.friend_material_type", static_cast<int>(config.chams.player.friend_material_type)));
    config.chams.player.friend_color_z = get_color("chams.player.friend_color_z", config.chams.player.friend_color_z);
    config.chams.player.friend_material_z_type = static_cast<Chams::Player::material_type>(
        get_int("chams.player.friend_material_z_type", static_cast<int>(config.chams.player.friend_material_z_type)));
    config.chams.player.friends_flags.ignore_z = get_bool("chams.player.friends_flags.ignore_z", config.chams.player.friends_flags.ignore_z);
    config.chams.player.local = get_bool("chams.player.local", config.chams.player.local);
    config.chams.player.local_color = get_color("chams.player.local_color", config.chams.player.local_color);
    config.chams.player.local_material_type = static_cast<Chams::Player::material_type>(
        get_int("chams.player.local_material_type", static_cast<int>(config.chams.player.local_material_type)));

    config.visuals.removals.scope = get_bool("visuals.removals.scope", config.visuals.removals.scope);
    config.visuals.removals.zoom = get_bool("visuals.removals.zoom", config.visuals.removals.zoom);
    config.visuals.thirdperson.enabled = get_bool("visuals.thirdperson.enabled", config.visuals.thirdperson.enabled);
    config.visuals.thirdperson.crosshair = get_bool("visuals.thirdperson.crosshair", config.visuals.thirdperson.crosshair);
    config.visuals.thirdperson.distance = get_float("visuals.thirdperson.distance", config.visuals.thirdperson.distance);
    config.visuals.thirdperson.right = get_float("visuals.thirdperson.right", config.visuals.thirdperson.right);
    config.visuals.thirdperson.up = get_float("visuals.thirdperson.up", config.visuals.thirdperson.up);
    config.visuals.thirdperson.scale = get_bool("visuals.thirdperson.scale", config.visuals.thirdperson.scale);
    config.visuals.thirdperson.collision = get_bool("visuals.thirdperson.collision", config.visuals.thirdperson.collision);
    config.visuals.hitmarker.enabled = get_bool("visuals.hitmarker.enabled", config.visuals.hitmarker.enabled);
    config.visuals.hitmarker.damage_text = get_bool("visuals.hitmarker.damage_text", config.visuals.hitmarker.damage_text);
    config.visuals.hitmarker.duration = get_float("visuals.hitmarker.duration", config.visuals.hitmarker.duration);
    config.visuals.hitmarker.size = get_float("visuals.hitmarker.size", config.visuals.hitmarker.size);
    config.visuals.hitmarker.color = get_color("visuals.hitmarker.color", config.visuals.hitmarker.color);
    config.visuals.hitmarker.crit_color = get_color("visuals.hitmarker.crit_color", config.visuals.hitmarker.crit_color);
    config.visuals.hitmarker.headshot_color = get_color("visuals.hitmarker.headshot_color", config.visuals.hitmarker.headshot_color);
    const int indicator_mask = get_int("visuals.indicators.enabled_mask", legacy_indicator_mask > 0 ? legacy_indicator_mask : static_cast<int>(config.visuals.indicators.enabled_mask));
    config.visuals.indicators.enabled_mask = static_cast<uint32_t>(std::max(0, indicator_mask)) & ~Visuals::Indicators::legacy_ticks;
    config.visuals.indicators.x = get_float("visuals.indicators.x", get_float("misc.exploits.tickbase_indicator_x", config.visuals.indicators.x));
    config.visuals.indicators.y = get_float("visuals.indicators.y", get_float("misc.exploits.tickbase_indicator_y", config.visuals.indicators.y));
    config.visuals.indicators.legacy_ticks_x = get_float("visuals.indicators.ticks_x", get_float("misc.exploits.tickbase_indicator_x", config.visuals.indicators.x));
    config.visuals.indicators.legacy_ticks_y = get_float("visuals.indicators.ticks_y", get_float("misc.exploits.tickbase_indicator_y", config.visuals.indicators.y));
    config.visuals.indicators.random_crits_x = get_float(
        "visuals.indicators.random_crits_x",
        get_float("visuals.indicators.crit_hack_x", config.visuals.indicators.x));
    config.visuals.indicators.random_crits_y = get_float(
        "visuals.indicators.random_crits_y",
        get_float("visuals.indicators.crit_hack_y", config.visuals.indicators.y + 46.0f));
    config.visuals.indicators.keybinds_x = get_float("visuals.indicators.keybinds_x", get_float("misc.exploits.keybind_indicator_x", config.visuals.indicators.x));
    config.visuals.indicators.keybinds_y = get_float("visuals.indicators.keybinds_y", get_float("misc.exploits.keybind_indicator_y", config.visuals.indicators.y + 92.0f));
    config.visuals.spectator_list.enabled = (config.visuals.indicators.enabled_mask & Visuals::Indicators::spectators) != 0;
    config.visuals.spectator_list.show_target = get_bool("visuals.spectator_list.show_target", config.visuals.spectator_list.show_target);
    config.visuals.spectator_list.show_modes = get_bool("visuals.spectator_list.show_modes", config.visuals.spectator_list.show_modes);
    config.visuals.spectator_list.highlight_firstperson = get_bool(
        "visuals.spectator_list.highlight_firstperson",
        config.visuals.spectator_list.highlight_firstperson);
    config.visuals.spectator_list.x = get_float("visuals.spectator_list.x", config.visuals.indicators.x);
    config.visuals.spectator_list.y = get_float("visuals.spectator_list.y", config.visuals.indicators.y + 138.0f);
    config.visuals.spectator_list.firstperson_color = get_color(
        "visuals.spectator_list.firstperson_color",
        config.visuals.spectator_list.firstperson_color);
    config.visuals.override_fov = get_bool("visuals.override_fov", config.visuals.override_fov);
    config.visuals.custom_fov = get_float("visuals.custom_fov", config.visuals.custom_fov);
    config.visuals.override_viewmodel_fov = get_bool("visuals.override_viewmodel_fov", config.visuals.override_viewmodel_fov);
    config.visuals.custom_viewmodel_fov = get_float("visuals.custom_viewmodel_fov", config.visuals.custom_viewmodel_fov);

    config.misc.movement.bhop = get_bool("misc.movement.bhop", config.misc.movement.bhop);
    config.misc.movement.auto_strafe = static_cast<Misc::Movement::auto_strafe_mode>(std::clamp(
        get_int("misc.movement.auto_strafe", static_cast<int>(config.misc.movement.auto_strafe)),
        0,
        2));
    config.misc.movement.auto_strafe_turn_scale = std::clamp(
        get_float("misc.movement.auto_strafe_turn_scale", config.misc.movement.auto_strafe_turn_scale),
        0.0f,
        1.0f);
    config.misc.movement.auto_strafe_max_delta = std::clamp(
        get_float("misc.movement.auto_strafe_max_delta", config.misc.movement.auto_strafe_max_delta),
        0.0f,
        180.0f);
    config.misc.movement.no_push = get_bool("misc.movement.no_push", config.misc.movement.no_push);
    config.misc.movement.taunt_slide = get_bool("misc.movement.taunt_slide", config.misc.movement.taunt_slide);
    config.misc.exploits.bypasspure = get_bool("misc.exploits.bypasspure", config.misc.exploits.bypasspure);
    config.misc.exploits.pure_bypass = get_bool("misc.exploits.pure_bypass", config.misc.exploits.pure_bypass);
    config.misc.exploits.cheats_bypass = get_bool("misc.exploits.cheats_bypass", config.misc.exploits.cheats_bypass);
    config.misc.exploits.vac_bypass = get_bool("misc.exploits.vac_bypass", config.misc.exploits.vac_bypass);
    config.misc.exploits.network_fix = get_bool("misc.exploits.network_fix", config.misc.exploits.network_fix);
    config.misc.exploits.tickbase = get_bool("misc.exploits.tickbase", config.misc.exploits.tickbase);
    config.misc.exploits.tickbase_recharge = get_bool(
        "misc.exploits.tickbase_recharge",
        config.misc.exploits.tickbase_recharge);
    config.misc.exploits.doubletap = get_bool("misc.exploits.doubletap", config.misc.exploits.doubletap);
    config.misc.exploits.doubletap_key.button = get_int(
        "misc.exploits.doubletap_key",
        config.misc.exploits.doubletap_key.button);
    config.misc.exploits.doubletap_key.mode = static_cast<button::mode_type>(std::clamp(
        get_int("misc.exploits.doubletap_key_mode", static_cast<int>(config.misc.exploits.doubletap_key.mode)),
        0,
        2));
    config.misc.exploits.doubletap_ticks = std::clamp(
        get_int("misc.exploits.doubletap_ticks", config.misc.exploits.doubletap_ticks),
        1,
        24);
    config.misc.exploits.warp = get_bool("misc.exploits.warp", config.misc.exploits.warp);
    config.misc.exploits.warp_key.button = get_int("misc.exploits.warp_key", config.misc.exploits.warp_key.button);
    config.misc.exploits.warp_key.mode = static_cast<button::mode_type>(std::clamp(
        get_int("misc.exploits.warp_key_mode", static_cast<int>(config.misc.exploits.warp_key.mode)),
        0,
        2));
    config.misc.exploits.warp_ticks = std::clamp(
        get_int("misc.exploits.warp_ticks", config.misc.exploits.warp_ticks),
        1,
        24);
    config.misc.exploits.fakelag = get_bool("misc.exploits.fakelag", config.misc.exploits.fakelag);
    config.misc.exploits.fakelag_ticks = std::clamp(
        get_int("misc.exploits.fakelag_ticks", config.misc.exploits.fakelag_ticks),
        1,
        21);
    config.misc.exploits.anti_aim = get_bool("misc.exploits.anti_aim", config.misc.exploits.anti_aim);
    config.misc.exploits.anti_aim_real_pitch = static_cast<Misc::Exploits::anti_aim_pitch_mode>(std::clamp(
        get_int("misc.exploits.anti_aim_real_pitch", static_cast<int>(config.misc.exploits.anti_aim_real_pitch)),
        0,
        7));
    config.misc.exploits.anti_aim_fake_pitch = static_cast<Misc::Exploits::anti_aim_pitch_mode>(std::clamp(
        get_int("misc.exploits.anti_aim_fake_pitch", static_cast<int>(config.misc.exploits.anti_aim_fake_pitch)),
        0,
        7));
    config.misc.exploits.anti_aim_real_yaw_base = static_cast<Misc::Exploits::anti_aim_yaw_base>(std::clamp(
        get_int("misc.exploits.anti_aim_real_yaw_base", static_cast<int>(config.misc.exploits.anti_aim_real_yaw_base)),
        0,
        1));
    config.misc.exploits.anti_aim_fake_yaw_base = static_cast<Misc::Exploits::anti_aim_yaw_base>(std::clamp(
        get_int("misc.exploits.anti_aim_fake_yaw_base", static_cast<int>(config.misc.exploits.anti_aim_fake_yaw_base)),
        0,
        1));
    config.misc.exploits.anti_aim_real_yaw = static_cast<Misc::Exploits::anti_aim_yaw_mode>(std::clamp(
        get_int("misc.exploits.anti_aim_real_yaw", static_cast<int>(config.misc.exploits.anti_aim_real_yaw)),
        0,
        8));
    config.misc.exploits.anti_aim_fake_yaw = static_cast<Misc::Exploits::anti_aim_yaw_mode>(std::clamp(
        get_int("misc.exploits.anti_aim_fake_yaw", static_cast<int>(config.misc.exploits.anti_aim_fake_yaw)),
        0,
        8));
    config.misc.exploits.anti_aim_real_yaw_offset = std::clamp(
        get_float("misc.exploits.anti_aim_real_yaw_offset", config.misc.exploits.anti_aim_real_yaw_offset),
        -180.0f,
        180.0f);
    config.misc.exploits.anti_aim_fake_yaw_offset = std::clamp(
        get_float("misc.exploits.anti_aim_fake_yaw_offset", config.misc.exploits.anti_aim_fake_yaw_offset),
        -180.0f,
        180.0f);
    config.misc.exploits.anti_aim_spin_speed = std::clamp(
        get_float("misc.exploits.anti_aim_spin_speed", config.misc.exploits.anti_aim_spin_speed),
        -180.0f,
        180.0f);
    config.misc.exploits.anti_aim_anti_overlap = get_bool(
        "misc.exploits.anti_aim_anti_overlap",
        config.misc.exploits.anti_aim_anti_overlap);
    config.misc.exploits.antiwarp = get_bool("misc.exploits.antiwarp", config.misc.exploits.antiwarp);
    config.misc.exploits.setup_bones_optimization = get_bool(
        "misc.exploits.setup_bones_optimization",
        config.misc.exploits.setup_bones_optimization);
    config.misc.exploits.equip_region_unlock = get_bool(
        "misc.exploits.equip_region_unlock",
        config.misc.exploits.equip_region_unlock);
    config.misc.exploits.ping_reducer = get_bool("misc.exploits.ping_reducer", config.misc.exploits.ping_reducer);
    config.misc.exploits.ping_target = std::clamp(
        get_int("misc.exploits.ping_target", config.misc.exploits.ping_target),
        1,
        100);
    config.misc.exploits.no_engine_sleep = get_bool("misc.exploits.no_engine_sleep", config.misc.exploits.no_engine_sleep);
    config.misc.exploits.null_graphics = get_bool("misc.exploits.null_graphics", config.misc.exploits.null_graphics);
    config.misc.exploits.null_graphics_render_stubs = get_bool(
        "misc.exploits.null_graphics_render_stubs",
        config.misc.exploits.null_graphics_render_stubs);
    config.misc.exploits.experimental_nographic_hooks = get_bool(
        "misc.exploits.experimental_nographic_hooks",
        config.misc.exploits.experimental_nographic_hooks);
#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE
    config.misc.exploits.null_graphics = true;
    config.misc.exploits.null_graphics_render_stubs = true;
#endif
    config.misc.exploits.legacy_tickbase_indicator = false;
    config.misc.exploits.keybind_indicator = (config.visuals.indicators.enabled_mask & Visuals::Indicators::keybinds) != 0;
    config.misc.exploits.legacy_tickbase_indicator_x = config.visuals.indicators.legacy_ticks_x;
    config.misc.exploits.legacy_tickbase_indicator_y = config.visuals.indicators.legacy_ticks_y;
    config.misc.exploits.keybind_indicator_x = config.visuals.indicators.keybinds_x;
    config.misc.exploits.keybind_indicator_y = config.visuals.indicators.keybinds_y;
    config.misc.menu.use_custom_font = get_bool("misc.menu.use_custom_font", config.misc.menu.use_custom_font);
    config.misc.menu.custom_font = get_string("misc.menu.custom_font", config.misc.menu.custom_font);
    config.misc.automation.auto_class_select = get_bool("misc.automation.auto_class_select", config.misc.automation.auto_class_select);
    config.misc.automation.class_selected = static_cast<tf_class>(std::clamp(
        get_int("misc.automation.class_selected", static_cast<int>(config.misc.automation.class_selected)),
        0,
        9));
    config.misc.automation.anti_afk = get_bool("misc.automation.anti_afk", config.misc.automation.anti_afk);
    config.misc.automation.anti_autobalance = get_bool("misc.automation.anti_autobalance", config.misc.automation.anti_autobalance);
    config.misc.automation.anti_motd = get_bool("misc.automation.anti_motd", config.misc.automation.anti_motd);
    config.misc.automation.auto_vote_map = get_bool("misc.automation.auto_vote_map", config.misc.automation.auto_vote_map);
    config.misc.automation.auto_vote_map_option = std::clamp(
        get_int("misc.automation.auto_vote_map_option", config.misc.automation.auto_vote_map_option),
        0,
        2);
    config.misc.automation.noisemaker_spam = get_bool("misc.automation.noisemaker_spam", config.misc.automation.noisemaker_spam);
    config.misc.automation.voice_command_spam = static_cast<Misc::Automation::voice_command_spam_mode>(std::clamp(
        get_int("misc.automation.voice_command_spam", static_cast<int>(config.misc.automation.voice_command_spam)),
        0,
        21));
    config.misc.automation.micspam = get_bool(
        "misc.automation.micspam",
        get_bool("cat-bot.micspam.enable", config.misc.automation.micspam));
    config.misc.automation.micspam_interval_on_seconds = std::clamp(
        get_int(
            "misc.automation.micspam_interval_on_seconds",
            get_int("cat-bot.micspam.interval-on", config.misc.automation.micspam_interval_on_seconds)),
        1,
        600);
    config.misc.automation.micspam_interval_off_seconds = std::clamp(
        get_int(
            "misc.automation.micspam_interval_off_seconds",
            get_int("cat-bot.micspam.interval-off", config.misc.automation.micspam_interval_off_seconds)),
        1,
        600);
    config.misc.automation.auto_item = get_bool(
        "misc.automation.auto_item",
        get_bool("auto-item.enable", config.misc.automation.auto_item));
    config.misc.automation.auto_item_interval_ms = std::clamp(
        get_int("misc.automation.auto_item_interval_ms", get_int("auto-item.time", config.misc.automation.auto_item_interval_ms)),
        1000,
        120000);
    config.misc.automation.auto_item_weapons = get_bool(
        "misc.automation.auto_item_weapons",
        get_bool("auto-item.weapons", config.misc.automation.auto_item_weapons));
    config.misc.automation.auto_item_primary = get_string(
        "misc.automation.auto_item_primary",
        get_string("auto-item.weapons.primary", config.misc.automation.auto_item_primary));
    config.misc.automation.auto_item_secondary = get_string(
        "misc.automation.auto_item_secondary",
        get_string("auto-item.weapons.secondary", config.misc.automation.auto_item_secondary));
    config.misc.automation.auto_item_melee = get_string(
        "misc.automation.auto_item_melee",
        get_string("auto-item.weapons.melee", config.misc.automation.auto_item_melee));
    config.misc.automation.auto_item_hats = get_bool(
        "misc.automation.auto_item_hats",
        get_bool("auto-item.hats", config.misc.automation.auto_item_hats));
    config.misc.automation.auto_item_hat1 = get_string(
        "misc.automation.auto_item_hat1",
        std::to_string(get_int("auto-item.hats.1", 940)));
    config.misc.automation.auto_item_hat2 = get_string(
        "misc.automation.auto_item_hat2",
        std::to_string(get_int("auto-item.hats.2", 941)));
    config.misc.automation.auto_item_hat3 = get_string(
        "misc.automation.auto_item_hat3",
        std::to_string(get_int("auto-item.hats.3", 302)));
    config.misc.automation.auto_item_noisemaker = get_bool(
        "misc.automation.auto_item_noisemaker",
        get_bool("misc.auto-noisemaker", config.misc.automation.auto_item_noisemaker));
    config.misc.automation.auto_item_debug = get_bool(
        "misc.automation.auto_item_debug",
        get_bool("auto-item.debug", config.misc.automation.auto_item_debug));
    config.misc.automation.autotaunt = get_bool("misc.automation.autotaunt", config.misc.automation.autotaunt);
    config.misc.automation.autotaunt_chance = std::clamp(
        get_float("misc.automation.autotaunt_chance", config.misc.automation.autotaunt_chance),
        0.0f,
        100.0f);
    config.misc.automation.autotaunt_safety_distance = std::clamp(
        get_float("misc.automation.autotaunt_safety_distance", config.misc.automation.autotaunt_safety_distance),
        0.0f,
        5000.0f);
    config.misc.automation.autotaunt_weapon_slot = std::clamp(
        get_int("misc.automation.autotaunt_weapon_slot", config.misc.automation.autotaunt_weapon_slot),
        0,
        5);
    config.misc.automation.chatspam = static_cast<Misc::Automation::chatspam_source>(std::clamp(
        get_int("misc.automation.chatspam", static_cast<int>(config.misc.automation.chatspam)),
        0,
        3));
    config.misc.automation.chatspam_random = get_bool("misc.automation.chatspam_random", config.misc.automation.chatspam_random);
    config.misc.automation.chatspam_team = get_bool("misc.automation.chatspam_team", config.misc.automation.chatspam_team);
    config.misc.automation.chatspam_delay_ms = std::clamp(
        get_int("misc.automation.chatspam_delay_ms", config.misc.automation.chatspam_delay_ms),
        250,
        60000);
    config.misc.automation.chatspam_file = get_string("misc.automation.chatspam_file", config.misc.automation.chatspam_file);
    config.misc.automation.killsay = static_cast<Misc::Automation::killsay_mode>(std::clamp(
        get_int("misc.automation.killsay", static_cast<int>(config.misc.automation.killsay)),
        0,
        3));
    config.misc.automation.killsay_delay_ms = std::clamp(
        get_int("misc.automation.killsay_delay_ms", config.misc.automation.killsay_delay_ms),
        0,
        10000);
    config.misc.automation.killsay_file = get_string("misc.automation.killsay_file", config.misc.automation.killsay_file);
    config.misc.automation.custom_announcer = get_bool("misc.automation.custom_announcer", config.misc.automation.custom_announcer);
    config.misc.automation.mvm_instant_respawn = get_bool(
        "misc.automation.mvm_instant_respawn",
        config.misc.automation.mvm_instant_respawn);
    config.misc.automation.mvm_instant_revive = get_bool(
        "misc.automation.mvm_instant_revive",
        config.misc.automation.mvm_instant_revive);
    config.misc.automation.allow_mvm_inspect = get_bool(
        "misc.automation.allow_mvm_inspect",
        config.misc.automation.allow_mvm_inspect);
    config.misc.automation.auto_mvm_ready_up = get_bool(
        "misc.automation.auto_mvm_ready_up",
        config.misc.automation.auto_mvm_ready_up);
    config.misc.automation.mvm_buybot = get_bool(
        "misc.automation.mvm_buybot",
        config.misc.automation.mvm_buybot);
    config.misc.automation.mvm_buybot_max_cash = std::clamp(
        get_int("misc.automation.mvm_buybot_max_cash", config.misc.automation.mvm_buybot_max_cash),
        0,
        50000);
    config.misc.automation.medic_autoheal = get_bool("misc.automation.medic_autoheal", config.misc.automation.medic_autoheal);
    config.misc.automation.medic_autovacc = get_bool("misc.automation.medic_autovacc", config.misc.automation.medic_autovacc);
    config.misc.automation.medic_autouber = get_bool("misc.automation.medic_autouber", config.misc.automation.medic_autouber);
    config.misc.automation.medic_auto_crossbow = get_bool("misc.automation.medic_auto_crossbow", config.misc.automation.medic_auto_crossbow);
    config.misc.automation.medic_heal_targets_mask = static_cast<uint32_t>(std::clamp(
        get_int("misc.automation.medic_heal_targets_mask", static_cast<int>(config.misc.automation.medic_heal_targets_mask)),
        0,
        static_cast<int>(Misc::Automation::medic_heal_target_default)));
    config.misc.automation.medic_heal_only = get_bool("misc.automation.medic_heal_only", config.misc.automation.medic_heal_only);
    config.misc.automation.auto_report = get_bool("misc.automation.auto_report", config.misc.automation.auto_report);
    config.misc.automation.auto_queue = get_bool("misc.automation.auto_queue", config.misc.automation.auto_queue);
    config.misc.automation.auto_requeue = get_bool("misc.automation.auto_requeue", config.misc.automation.auto_requeue);
    config.misc.automation.requeue_on_kick = get_bool("misc.automation.requeue_on_kick", config.misc.automation.requeue_on_kick);
    config.misc.automation.auto_casual_join = get_bool(
        "misc.automation.auto_casual_join",
        config.misc.automation.auto_casual_join);
    config.misc.automation.auto_queue_mode = std::clamp(
        get_int("misc.automation.auto_queue_mode", config.misc.automation.auto_queue_mode),
        0,
        8);
    config.misc.automation.rq_if_players_lte = std::clamp(
        get_int("misc.automation.rq_if_players_lte", config.misc.automation.rq_if_players_lte),
        0,
        32);
    config.misc.automation.rq_if_players_gte = std::clamp(
        get_int("misc.automation.rq_if_players_gte", config.misc.automation.rq_if_players_gte),
        0,
        32);
    config.misc.automation.rq_ignore_friends = get_bool(
        "misc.automation.rq_ignore_friends",
        config.misc.automation.rq_ignore_friends);
    config.misc.automation.requeue_action = static_cast<Misc::Automation::requeue_action_mode>(std::clamp(
        get_int("misc.automation.requeue_action", static_cast<int>(config.misc.automation.requeue_action)),
        0,
        1));
    config.misc.automation.region_selector = get_bool(
        "misc.automation.region_selector",
        config.misc.automation.region_selector);
    config.misc.automation.region_selector_allowed_mask = parse_uint64(
        get_string(
            "misc.automation.region_selector_allowed_mask",
            std::to_string(config.misc.automation.region_selector_allowed_mask)),
        config.misc.automation.region_selector_allowed_mask) & automation::region_selector::all_region_bits;
    config.misc.automation.navbot_enabled = get_bool("misc.automation.navbot_enabled", config.misc.automation.navbot_enabled);
    config.misc.automation.navbot_draw_path = get_bool("misc.automation.navbot_draw_path", config.misc.automation.navbot_draw_path);
    config.misc.automation.navbot_dont_path_during_warmup = get_bool(
        "misc.automation.navbot_dont_path_during_warmup",
        config.misc.automation.navbot_dont_path_during_warmup);
    config.misc.automation.navbot_dont_path_unless_match_started = get_bool(
        "misc.automation.navbot_dont_path_unless_match_started",
        config.misc.automation.navbot_dont_path_unless_match_started);
    config.misc.automation.navbot_warmup_only_blu_cp_pl = get_bool(
        "misc.automation.navbot_warmup_only_blu_cp_pl",
        config.misc.automation.navbot_warmup_only_blu_cp_pl);
    config.misc.automation.navbot_look_at_path = get_bool("misc.automation.navbot_look_at_path", config.misc.automation.navbot_look_at_path);
    config.misc.automation.navbot_auto_weapon = get_bool("misc.automation.navbot_auto_weapon", config.misc.automation.navbot_auto_weapon);
    config.misc.automation.navbot_look_at_path_speed = get_float("misc.automation.navbot_look_at_path_speed", config.misc.automation.navbot_look_at_path_speed);
    config.misc.automation.navbot_crumb_blacklist_seconds = std::clamp(
        get_float("misc.automation.navbot_crumb_blacklist_seconds", config.misc.automation.navbot_crumb_blacklist_seconds),
        50.0f,
        150.0f);
    config.misc.automation.navbot_debug_text = get_bool("misc.automation.navbot_debug_text", config.misc.automation.navbot_debug_text);
    config.misc.automation.navbot_excluded_jobs_mask = static_cast<uint32_t>(
        get_int("misc.automation.navbot_excluded_jobs_mask", static_cast<int>(config.misc.automation.navbot_excluded_jobs_mask)));
    config.debug.font_height = get_int("debug.font_height", config.debug.font_height);
    config.debug.font_weight = get_int("debug.font_weight", config.debug.font_weight);
    config.debug.debug_render_all_entities = get_bool("debug.render_all_entities", config.debug.debug_render_all_entities);
    config.debug.show_active_flag_ids_of_players = get_bool("debug.show_active_flag_ids_of_players", config.debug.show_active_flag_ids_of_players);
    config.debug.disable_friend_checks = get_bool("debug.disable_friend_checks", config.debug.disable_friend_checks);
}

void config_store::set_bool(std::string key, const bool value)
{
    m_values[std::move(key)] = value ? "true" : "false";
}

void config_store::set_int(std::string key, const int value)
{
    m_values[std::move(key)] = std::to_string(value);
}

void config_store::set_float(std::string key, const float value)
{
    std::ostringstream stream{};
    stream << std::fixed << std::setprecision(2) << value;
    m_values[std::move(key)] = stream.str();
}

void config_store::set_string(std::string key, const std::string_view value)
{
    m_values[std::move(key)] = std::string{ value };
}

void config_store::set_color(std::string key, const RGBA_float& value)
{
    std::ostringstream stream{};
    stream << std::fixed << std::setprecision(3)
           << value.r << ',' << value.g << ',' << value.b << ',' << value.a;
    m_values[std::move(key)] = stream.str();
}

bool config_store::get_bool(const std::string_view key, const bool fallback) const
{
    const auto found{ m_values.find(std::string{ key }) };
    if (found == m_values.end())
    {
        return fallback;
    }

    return found->second == "true";
}

int config_store::get_int(const std::string_view key, const int fallback) const
{
    const auto found{ m_values.find(std::string{ key }) };
    if (found == m_values.end())
    {
        return fallback;
    }

    int parsed_value{ fallback };
    const auto result{ std::from_chars(found->second.data(), found->second.data() + found->second.size(), parsed_value) };
    return result.ec == std::errc{} ? parsed_value : fallback;
}

float config_store::get_float(const std::string_view key, const float fallback) const
{
    const auto found{ m_values.find(std::string{ key }) };
    if (found == m_values.end())
    {
        return fallback;
    }

    float parsed_value{ fallback };
    const auto result{ std::from_chars(found->second.data(), found->second.data() + found->second.size(), parsed_value) };
    return result.ec == std::errc{} ? parsed_value : fallback;
}

std::string config_store::get_string(const std::string_view key, const std::string_view fallback) const
{
    const auto found{ m_values.find(std::string{ key }) };
    if (found == m_values.end())
    {
        return std::string{ fallback };
    }

    return found->second;
}

RGBA_float config_store::get_color(const std::string_view key, RGBA_float fallback) const
{
    const auto found{ m_values.find(std::string{ key }) };
    if (found == m_values.end())
    {
        return fallback;
    }

    if (const auto parsed{ parse_color(found->second) })
    {
        return *parsed;
    }

    return fallback;
}

std::filesystem::path config_store::config_directory() const
{
    return m_root_directory / "configs";
}

std::filesystem::path config_store::config_path(const std::string_view name) const
{
    return config_directory() / (std::string{ name } + ".cat");
}

std::string config_store::trim(std::string value)
{
    const auto is_space = [](const unsigned char character)
    {
        return std::isspace(character) != 0;
    };

    const auto start{ std::find_if_not(value.begin(), value.end(), is_space) };
    const auto finish{ std::find_if_not(value.rbegin(), value.rend(), is_space).base() };

    if (start >= finish)
    {
        return {};
    }

    return { start, finish };
}

std::optional<RGBA_float> config_store::parse_color(const std::string_view value)
{
    std::array<float, 4> channels{};
    std::size_t channel_index{};
    std::size_t cursor{};

    while (cursor <= value.size() && channel_index < channels.size())
    {
        const std::size_t separator{ value.find(',', cursor) };
        const std::size_t length
        {
            separator == std::string_view::npos ? value.size() - cursor : separator - cursor
        };

        const std::string component{ trim(std::string{ value.substr(cursor, length) }) };
        if (component.empty())
        {
            return std::nullopt;
        }

        float parsed{};
        const auto result{ std::from_chars(component.data(), component.data() + component.size(), parsed) };
        if (result.ec != std::errc{})
        {
            return std::nullopt;
        }

        channels[channel_index++] = parsed;
        if (separator == std::string_view::npos)
        {
            break;
        }

        cursor = separator + 1;
    }

    if (channel_index != channels.size())
    {
        return std::nullopt;
    }

    return RGBA_float{ channels[0], channels[1], channels[2], channels[3] };
}

void initialize_config_store(const std::filesystem::path& root_directory)
{
    if (g_config_store)
    {
        return;
    }

    g_config_store = std::make_unique<config_store>(root_directory);
}

void shutdown_config_store()
{
    g_config_store.reset();
}

config_store* get_config_store()
{
    return g_config_store.get();
}

void load_default_config(Config& config)
{
    config_store* store{ get_config_store() };
    if (!store)
    {
        return;
    }

    if (store->load_file("default"))
    {
        store->export_config(config);
        log_line("loaded default config");
        return;
    }

    store->import_config(config);
    if (store->save_file("default"))
    {
        log_line("created default config");
    }
}

} // namespace cathook::core
