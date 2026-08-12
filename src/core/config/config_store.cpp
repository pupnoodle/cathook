/*
/^-----^\   data: 2026-04-30
V  o o  V  file: src/core/config/config_store.cpp
 |  Y  |   author: pupnoodle
  \ Q /
  / - \
  |    \
  |     \     )
  || (___\====
*/
#include "config_store.hpp"
#include "../logger.hpp"
#include "features/automation/region_selector/region_selector.hpp"
#include "features/visuals/groups/visual_groups.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
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

std::string serialize_int_list(const std::vector<int>& values)
{
    std::string result{};
    for (const int value : values)
    {
        if (!result.empty()) result += ',';
        result += std::to_string(value);
    }
    return result;
}

std::vector<int> parse_int_list(const std::string_view value)
{
    std::vector<int> result{};
    std::size_t start = 0;
    while (start <= value.size())
    {
        const std::size_t end = value.find(',', start);
        const auto token = value.substr(start, end == std::string_view::npos ? value.size() - start : end - start);
        int parsed = 0;
        const auto conversion = std::from_chars(token.data(), token.data() + token.size(), parsed);
        if (conversion.ec == std::errc{} && conversion.ptr == token.data() + token.size())
        {
            result.push_back(parsed);
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    std::ranges::sort(result);
    result.erase(std::ranges::unique(result).begin(), result.end());
    return result;
}

float normalize_stencil_scale(const float value)
{
    return std::clamp(std::round(value * 10.0f) / 10.0f, 0.0f, 10.0f);
}

}

config_store::config_store(std::filesystem::path root_directory)
    : config_store{ std::move(root_directory), "configs" }
{
}

config_store::config_store(std::filesystem::path root_directory, std::filesystem::path config_subdirectory)
    : m_root_directory{ std::move(root_directory) },
      m_config_subdirectory{ std::move(config_subdirectory) }
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

std::vector<std::string> config_store::keys() const
{
    std::vector<std::string> names{};
    names.reserve(m_values.size());

    for (const auto& entry : m_values)
    {
        names.emplace_back(entry.first);
    }

    std::ranges::sort(names);
    return names;
}

const std::string& config_store::current_name() const
{
    return m_current_name;
}

config_store config_store::scoped_store(std::filesystem::path config_subdirectory) const
{
    return config_store{ m_root_directory, std::move(config_subdirectory) };
}

void config_store::import_config(const Config& config)
{
    m_values.clear();

#if 0 // Inventory changer config temporarily disabled.
    const auto save_inventory_slot = [this](const std::string& prefix, const Misc::InventorySlot& slot) {
        set_int(prefix + ".item", slot.item);
        set_int(prefix + ".paintkit", slot.paintkit);
        set_int(prefix + ".wear", slot.wear);
        set_int(prefix + ".seed", slot.seed);
        set_int(prefix + ".style", slot.style);
        set_int(prefix + ".sheen", slot.sheen);
        set_int(prefix + ".killstreak", slot.killstreak);
        set_int(prefix + ".unusual", slot.unusual);
    };
#endif

    const bool spectator_indicator_enabled = (config.visuals.indicators.enabled_mask & Visuals::Indicators::spectators) != 0;
    const bool keybind_indicator_enabled = (config.visuals.indicators.enabled_mask & Visuals::Indicators::keybinds) != 0;

    set_bool("aimbot.master", config.aimbot.master);
    set_bool("aimbot.auto_shoot", config.aimbot.auto_shoot);
    set_int("aimbot.target_type", static_cast<int>(config.aimbot.target_type));
    set_int("aimbot.aim_at", static_cast<int>(config.aimbot.aim_at));
    set_int("aimbot.aim_at_version", 2);
    set_int("aimbot.aim_mode", static_cast<int>(config.aimbot.aim_mode));
    set_float("aimbot.fov", config.aimbot.fov);
    set_float("aimbot.assist_strength", config.aimbot.assist_strength);
    set_bool("aimbot.draw_fov", config.aimbot.draw_fov);
    set_bool("aimbot.shoot_through_glass", config.aimbot.shoot_through_glass);
    set_bool("aimbot.spread_compensation", config.aimbot.spread_compensation);
    set_bool("aimbot.resolver", config.aimbot.resolver);
    set_int("aimbot.resolver_max_yaws", config.aimbot.resolver_max_yaws);
    set_bool("aimbot.debug_overlay", config.aimbot.debug_overlay);
    set_float("aimbot.debug_overlay_x", config.aimbot.debug_overlay_x);
    set_float("aimbot.debug_overlay_y", config.aimbot.debug_overlay_y);
    set_int("aimbot.hitscan_hitboxes", static_cast<int>(config.aimbot.hitscan_hitboxes));
    set_int("aimbot.melee_hitboxes", static_cast<int>(config.aimbot.melee_hitboxes));
    set_bool("aimbot.melee_walk_to_target", config.aimbot.melee_walk_to_target);
    set_bool("aimbot.melee_auto_backstab", config.aimbot.melee_auto_backstab);
    set_bool("aimbot.melee_ignore_razorback", config.aimbot.melee_ignore_razorback);
    set_bool("aimbot.melee_swing_prediction", config.aimbot.melee_swing_prediction);
    set_int("aimbot.melee_swing_ticks", config.aimbot.melee_swing_ticks);
    set_bool("aimbot.melee_swing_predict_lag", config.aimbot.melee_swing_predict_lag);
    set_int("aimbot.melee_swing_validate_mode", config.aimbot.melee_swing_validate_mode);
    set_bool("aimbot.sniper_auto_scope", config.aimbot.sniper_auto_scope);
    set_bool("aimbot.sniper_auto_unscope", config.aimbot.sniper_auto_unscope);
    set_float("aimbot.sniper_scope_distance", config.aimbot.sniper_scope_distance);
    set_float("aimbot.sniper_scope_cancel_time", config.aimbot.sniper_scope_cancel_time);
    set_bool("aimbot.auto_rev", config.aimbot.auto_rev);
    set_bool("aimbot.auto_unrev", config.aimbot.auto_unrev);
    set_float("aimbot.auto_rev_threshold", config.aimbot.auto_rev_threshold);
    set_int("aimbot.hitscan_modifiers", static_cast<int>(config.aimbot.hitscan_modifiers));
    set_int("aimbot.hitscan_modifier_version", 5);
    set_float("aimbot.tapfire_distance", config.aimbot.tapfire_distance);
    set_float("aimbot.ignore_invisible", config.aimbot.ignore_invisible);
    set_int("aimbot.ignore_unsimulated_ticks", config.aimbot.ignore_unsimulated_ticks);
    set_float("aimbot.multipoint_scale", config.aimbot.multipoint_scale);
    set_float("aimbot.bone_size_subtract", config.aimbot.bone_size_subtract);
    set_float("aimbot.bone_size_min_scale", config.aimbot.bone_size_min_scale);
    set_bool("aimbot.projectile_active", config.aimbot.projectile_active);
    set_int("aimbot.projectile_modifiers", static_cast<int>(config.aimbot.projectile_modifiers));
    set_int("aimbot.projectile_mode", config.aimbot.projectile_mode);
    set_int("aimbot.projectile_prediction_mode", static_cast<int>(config.aimbot.projectile_prediction_mode));
    set_int("aimbot.projectile_aim_pos", config.aimbot.projectile_aim_pos);
    set_float("aimbot.projectile_fov", config.aimbot.projectile_fov);
    set_int("aimbot.projectile_max_sim_targets", config.aimbot.projectile_max_sim_targets);
    set_float("aimbot.projectile_max_sim_time", config.aimbot.projectile_max_sim_time);
    set_int("aimbot.projectile_multipoint_scale", config.aimbot.projectile_multipoint_scale);
    set_int("aimbot.projectile_splash_policy", config.aimbot.projectile_splash_policy);
    set_bool("aimbot.projectile_smooth_flamethrowers_active", config.aimbot.projectile_smooth_flamethrowers_active);
    set_float("aimbot.projectile_smooth_flamethrowers", config.aimbot.projectile_smooth_flamethrowers);
    set_int("aimbot.ignore", static_cast<int>(config.aimbot.ignore));
    set_int("aimbot.ignore_version", 2);
    set_int("aimbot.max_targets", config.aimbot.max_targets);
    set_bool("backtrack.enabled", config.backtrack.enabled);
    set_float("backtrack.fake_latency_ms", config.backtrack.fake_latency_ms);
    set_float("backtrack.interp_ms", config.backtrack.interp_ms);
    set_bool("backtrack.fake_interp", config.backtrack.fake_interp);
    set_int("backtrack.window_ms", config.backtrack.window_ms);
    set_bool("backtrack.prefer_on_shot", config.backtrack.prefer_on_shot);
    set_bool("backtrack.to_crosshair", config.backtrack.to_crosshair);
    set_int("backtrack.offset_ticks", config.backtrack.offset_ticks);
    set_bool("backtrack.visualizer", config.backtrack.visualizer);
    set_int("backtrack.visualizer_ticks", config.backtrack.visualizer_ticks);
    set_int("backtrack.visualizer_mode", static_cast<int>(config.backtrack.visualizer_mode));
#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE

    set_bool("ipc.enabled", true);
    set_bool("ipc.auto_connect", true);
    set_bool("ipc.auto_ignore_local_bots", true);
#else

    set_bool("ipc.enabled", config.ipc.enabled);
    set_bool("ipc.auto_connect", config.ipc.auto_connect);
    set_bool("ipc.auto_ignore_local_bots", config.ipc.auto_ignore_local_bots);
#endif

    const std::size_t visual_group_count = std::min(config.visual_groups.groups.size(), visual_group_config::max_groups);
    set_int("visuals.groups.count", static_cast<int>(visual_group_count));
    set_int("visuals.groups.active_mask", static_cast<int>(config.visual_groups.active_group_mask));
    for (std::size_t index = 0; index < visual_group_count; ++index)
    {
        const visual_group& group = config.visual_groups.groups[index];
        const std::string prefix = "visuals.groups." + std::to_string(index) + ".";
        set_string(prefix + "name", group.name);
        set_color(prefix + "color", group.color);
        set_bool(prefix + "tags_override_color", group.tags_override_color);
        set_int(prefix + "targets", static_cast<int>(group.targets));
        set_int(prefix + "conditions", static_cast<int>(group.conditions));
        set_string(prefix + "roles", serialize_int_list(group.roles));
        set_int(prefix + "players", static_cast<int>(group.players));
        set_int(prefix + "buildings", static_cast<int>(group.buildings));
        set_int(prefix + "projectiles", static_cast<int>(group.projectiles));
        set_int(prefix + "esp.draw_mask", static_cast<int>(group.esp.draw_mask));
        set_bool(prefix + "esp.override_color", group.esp.override_color);
        set_color(prefix + "esp.color", group.esp.color);
        set_int(prefix + "esp.box_style", static_cast<int>(group.esp.box_style));
        set_float(prefix + "esp.start", group.esp.start);
        set_float(prefix + "esp.end", group.esp.end);
        set_bool(prefix + "esp.smooth_alpha", group.esp.smooth_alpha);
        set_int(prefix + "esp.background_alpha", group.esp.background_alpha);
        set_float(prefix + "esp.class_icon_scale", group.esp.class_icon_scale);
        set_float(prefix + "esp.head_emoji_scale", group.esp.head_emoji_scale);
        set_int(prefix + "esp.head_emoji_style", group.esp.head_emoji_style);
        set_int(prefix + "esp.mafia_level_position", static_cast<int>(group.esp.mafia_level_position));
        set_int(prefix + "chams.visible.count", static_cast<int>(group.chams.visible.size()));
        for (std::size_t layer_index = 0; layer_index < group.chams.visible.size(); ++layer_index)
        {
            const chams_layer& layer = group.chams.visible[layer_index];
            const std::string layer_prefix = prefix + "chams.visible." + std::to_string(layer_index) + ".";
            set_string(layer_prefix + "material", layer.material);
            set_color(layer_prefix + "color", layer.color);
            set_float(layer_prefix + "start", layer.start);
            set_float(layer_prefix + "end", layer.end);
            set_bool(layer_prefix + "smooth_alpha", layer.smooth_alpha);
        }
        set_int(prefix + "chams.occluded.count", static_cast<int>(group.chams.occluded.size()));
        for (std::size_t layer_index = 0; layer_index < group.chams.occluded.size(); ++layer_index)
        {
            const chams_layer& layer = group.chams.occluded[layer_index];
            const std::string layer_prefix = prefix + "chams.occluded." + std::to_string(layer_index) + ".";
            set_string(layer_prefix + "material", layer.material);
            set_color(layer_prefix + "color", layer.color);
            set_float(layer_prefix + "start", layer.start);
            set_float(layer_prefix + "end", layer.end);
            set_bool(layer_prefix + "smooth_alpha", layer.smooth_alpha);
        }
        set_color(prefix + "glow.color", group.glow.color);
        set_float(prefix + "glow.stencil", normalize_stencil_scale(group.glow.stencil));
        set_float(prefix + "glow.blur", group.glow.blur);
        set_float(prefix + "glow.start", group.glow.start);
        set_float(prefix + "glow.end", group.glow.end);
        set_bool(prefix + "glow.smooth_alpha", group.glow.smooth_alpha);
        set_bool(prefix + "backtrack_visuals.enabled", group.backtrack_visuals.enabled);
        set_int(prefix + "backtrack_visuals.record_mode", group.backtrack_visuals.record_mode);
        set_bool(prefix + "backtrack_visuals.ignore_z", group.backtrack_visuals.ignore_z);
        const auto save_backtrack_layers = [&](const char* side, const std::vector<chams_layer>& layers) {
            set_int(prefix + "backtrack_visuals.chams." + side + ".count", static_cast<int>(layers.size()));
            for (std::size_t layer_index = 0; layer_index < layers.size(); ++layer_index)
            {
                const chams_layer& layer = layers[layer_index];
                const std::string layer_prefix = prefix + "backtrack_visuals.chams." + side + "." + std::to_string(layer_index) + ".";
                set_string(layer_prefix + "material", layer.material);
                set_color(layer_prefix + "color", layer.color);
                set_float(layer_prefix + "start", layer.start);
                set_float(layer_prefix + "end", layer.end);
                set_bool(layer_prefix + "smooth_alpha", layer.smooth_alpha);
            }
        };
        save_backtrack_layers("visible", group.backtrack_visuals.chams.visible);
        save_backtrack_layers("occluded", group.backtrack_visuals.chams.occluded);
        set_color(prefix + "backtrack_visuals.glow.color", group.backtrack_visuals.glow.color);
        set_float(prefix + "backtrack_visuals.glow.stencil", normalize_stencil_scale(group.backtrack_visuals.glow.stencil));
        set_float(prefix + "backtrack_visuals.glow.blur", group.backtrack_visuals.glow.blur);
        set_float(prefix + "backtrack_visuals.glow.start", group.backtrack_visuals.glow.start);
        set_float(prefix + "backtrack_visuals.glow.end", group.backtrack_visuals.glow.end);
        set_bool(prefix + "backtrack_visuals.glow.smooth_alpha", group.backtrack_visuals.glow.smooth_alpha);
        set_bool(prefix + "offscreen_arrows", group.offscreen_arrows);
        set_int(prefix + "offscreen_arrows_offset", group.offscreen_arrows_offset);
        set_float(prefix + "offscreen_arrows_max_distance", group.offscreen_arrows_max_distance);
        set_bool(prefix + "pickup_timer", group.pickup_timer);
        set_int(prefix + "backtrack", static_cast<int>(group.backtrack));
        set_int(prefix + "trajectory", static_cast<int>(group.trajectory));
        set_int(prefix + "sightlines", static_cast<int>(group.sightlines));
    }

    set_bool("visuals.removals.scope", config.visuals.removals.scope);
    set_bool("visuals.removals.zoom", config.visuals.removals.zoom);
    set_bool("visuals.removals.arms", config.visuals.removals.arms);
    set_bool("visuals.removals.hats", config.visuals.removals.hats);
    set_bool("visuals.removals.cloak", config.visuals.removals.cloak);
    set_bool("visuals.removals.disguise", config.visuals.removals.disguise);
    set_bool("visuals.removals.taunts", config.visuals.removals.taunts);
    set_bool("visuals.removals.contracker", config.visuals.removals.contracker);
    set_bool("visuals.casual_medal.guaranteed_flip", config.visuals.casual_medal.guaranteed_flip);
    set_bool("visuals.casual_medal.changer", config.visuals.casual_medal.changer);
    set_int("visuals.casual_medal.rank", config.visuals.casual_medal.rank);
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
    set_float("visuals.indicators.keybinds_x", config.visuals.indicators.keybinds_x);
    set_float("visuals.indicators.keybinds_y", config.visuals.indicators.keybinds_y);
    set_float("visuals.indicators.crit_hack_x", config.visuals.indicators.crit_hack_x);
    set_float("visuals.indicators.crit_hack_y", config.visuals.indicators.crit_hack_y);
    set_float("visuals.indicators.nospread_x", config.visuals.indicators.nospread_x);
    set_float("visuals.indicators.nospread_y", config.visuals.indicators.nospread_y);
    set_color("visuals.indicators.tickbase_bar_color", config.visuals.indicators.tickbase_bar_color);
    set_color("visuals.indicators.crit_hack_bar_color", config.visuals.indicators.crit_hack_bar_color);
    set_bool("visuals.spectator_list.enabled", spectator_indicator_enabled);
    set_bool("visuals.spectator_list.show_target", config.visuals.spectator_list.show_target);
    set_bool("visuals.spectator_list.show_modes", config.visuals.spectator_list.show_modes);
    set_bool("visuals.spectator_list.highlight_firstperson", config.visuals.spectator_list.highlight_firstperson);
    set_float("visuals.spectator_list.x", config.visuals.spectator_list.x);
    set_float("visuals.spectator_list.y", config.visuals.spectator_list.y);
    set_color("visuals.spectator_list.firstperson_color", config.visuals.spectator_list.firstperson_color);
    set_bool("visuals.radar.enabled", config.visuals.radar.enabled);
    set_float("visuals.radar.x", config.visuals.radar.x);
    set_float("visuals.radar.y", config.visuals.radar.y);
    set_int("visuals.radar.size", config.visuals.radar.size);
    set_float("visuals.radar.zoom", config.visuals.radar.zoom);
    set_int("visuals.radar.icon_size", config.visuals.radar.icon_size);
    set_bool("visuals.radar.show_teammates", config.visuals.radar.show_teammates);
    set_bool("visuals.radar.show_enemies", config.visuals.radar.show_enemies);
    set_bool("visuals.radar.use_icons", config.visuals.radar.use_icons);
    set_bool("visuals.radar.axis_lines", config.visuals.radar.axis_lines);
    set_int("visuals.radar.range_rings", config.visuals.radar.range_rings);
    set_int("visuals.skybox_changer_index", config.visuals.skybox_changer_index);
    set_bool("visuals.override_fov", config.visuals.override_fov);
    set_float("visuals.custom_fov", config.visuals.custom_fov);
    set_bool("visuals.override_zoom_fov", config.visuals.override_zoom_fov);
    set_float("visuals.custom_zoom_fov", config.visuals.custom_zoom_fov);
    set_bool("visuals.flat_zoom_sensitivity", config.visuals.flat_zoom_sensitivity);
    set_bool("visuals.override_viewmodel_fov", config.visuals.override_viewmodel_fov);
    set_float("visuals.custom_viewmodel_fov", config.visuals.custom_viewmodel_fov);
    set_bool("visuals.esp_lerp", config.visuals.esp_lerp);
    set_bool("visuals.dormant_esp", config.visuals.dormant_esp);

    set_bool("misc.movement.bhop", config.misc.movement.bhop);
    set_int("misc.movement.auto_strafe", static_cast<int>(config.misc.movement.auto_strafe));
    set_float("misc.movement.auto_strafe_turn_scale", config.misc.movement.auto_strafe_turn_scale);
    set_float("misc.movement.auto_strafe_max_delta", config.misc.movement.auto_strafe_max_delta);
    set_bool("misc.movement.edge_jump", config.misc.movement.edge_jump);
    set_bool("misc.movement.jumpbug", config.misc.movement.jumpbug);
    set_bool("misc.movement.duck_jump", config.misc.movement.duck_jump);
    set_bool("misc.movement.break_jump", config.misc.movement.break_jump);
    set_bool("misc.movement.auto_reverse_jump", config.misc.movement.auto_reverse_jump);
    set_bool("misc.movement.fast_stop", config.misc.movement.fast_stop);
    set_bool("misc.movement.fast_accelerate", config.misc.movement.fast_accelerate);
    set_int("misc.movement.auto_edgebug", static_cast<int>(config.misc.movement.auto_edgebug));
    set_bool("misc.movement.no_push", config.misc.movement.no_push);
    set_bool("misc.movement.taunt_slide", config.misc.movement.taunt_slide);
    set_bool("misc.movement.moonwalk", config.misc.movement.moonwalk);
    set_bool("misc.movement.moonwalk_forward", config.misc.movement.moonwalk_forward);
    set_bool("misc.movement.moonwalk_navbot_compat", config.misc.movement.moonwalk_navbot_compat);
    set_bool("misc.exploits.bypasspure", config.misc.exploits.bypasspure);
    set_bool("misc.exploits.pure_bypass", config.misc.exploits.pure_bypass);
    set_bool("misc.exploits.cheats_bypass", config.misc.exploits.cheats_bypass);
    set_bool("misc.exploits.vac_bypass", config.misc.exploits.vac_bypass);
    set_bool("misc.exploits.network_fix", config.misc.exploits.network_fix);
    set_bool("misc.exploits.tickbase", config.misc.exploits.tickbase);
    set_bool("misc.exploits.tickbase_recharge", config.misc.exploits.tickbase_recharge);
    set_bool("misc.exploits.doubletap", config.misc.exploits.doubletap);
    set_int("misc.exploits.doubletap_ticks", config.misc.exploits.doubletap_ticks);
    set_bool("misc.exploits.warp", config.misc.exploits.warp);
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
    set_bool("misc.exploits.equip_region_unlock", config.misc.exploits.equip_region_unlock);
    set_bool("misc.exploits.ping_reducer", config.misc.exploits.ping_reducer);
    set_int("misc.exploits.ping_target", config.misc.exploits.ping_target);
    set_bool("misc.exploits.no_engine_sleep", config.misc.exploits.no_engine_sleep);
    set_bool("misc.exploits.null_graphics", config.misc.exploits.null_graphics);
#if 0 // Inventory changer config temporarily disabled.
    set_bool("misc.inventory_changer.enabled", config.misc.inventory_changer.enabled);
    set_bool("misc.inventory_changer.apply_to_all", config.misc.inventory_changer.apply_to_all);
    set_bool("misc.inventory_changer.debug", config.misc.inventory_changer.debug);
    set_string("misc.inventory_changer.redirects", config.misc.inventory_changer.redirects);
    set_string("misc.inventory_changer.boxes", config.misc.inventory_changer.boxes);
    set_string("misc.inventory_changer.attributes", config.misc.inventory_changer.attributes);
    save_inventory_slot("misc.inventory_changer.primary", config.misc.inventory_changer.primary);
    save_inventory_slot("misc.inventory_changer.secondary", config.misc.inventory_changer.secondary);
    save_inventory_slot("misc.inventory_changer.melee", config.misc.inventory_changer.melee);
    save_inventory_slot("misc.inventory_changer.hat1", config.misc.inventory_changer.hat1);
    save_inventory_slot("misc.inventory_changer.hat2", config.misc.inventory_changer.hat2);
    save_inventory_slot("misc.inventory_changer.hat3", config.misc.inventory_changer.hat3);
    set_int("misc.inventory_changer.taunt1_unusual", config.misc.inventory_changer.taunt1_unusual);
    set_int("misc.inventory_changer.crate", config.misc.inventory_changer.crate);
    set_int("misc.inventory_changer.key", config.misc.inventory_changer.key);
#endif
    set_bool("misc.exploits.keybind_indicator", keybind_indicator_enabled);
    set_float("misc.exploits.keybind_indicator_x", config.visuals.indicators.keybinds_x);
    set_float("misc.exploits.keybind_indicator_y", config.visuals.indicators.keybinds_y);
    set_bool("misc.menu.use_custom_font", config.misc.menu.use_custom_font);
    set_string("misc.menu.custom_font", config.misc.menu.custom_font);
    set_int("misc.menu.dpi_scale", config.misc.menu.dpi_scale);
    set_color("misc.menu.theme_color", config.misc.menu.theme_color);
    set_bool("misc.automation.auto_class_select", config.misc.automation.auto_class_select);
    set_int("misc.automation.class_selected", static_cast<int>(config.misc.automation.class_selected));
    set_bool("misc.automation.auto_class_dont_join_during_warmup", config.misc.automation.auto_class_dont_join_during_warmup);
    set_bool("misc.automation.anti_afk", config.misc.automation.anti_afk);
    set_bool("misc.automation.anti_autobalance", config.misc.automation.anti_autobalance);
    set_bool("misc.automation.anti_motd", config.misc.automation.anti_motd);
    set_bool("misc.automation.anti_motd_dont_close_during_warmup", config.misc.automation.anti_motd_dont_close_during_warmup);
    set_bool("misc.automation.auto_vote_map", config.misc.automation.auto_vote_map);
    set_int("misc.automation.auto_vote_map_option", config.misc.automation.auto_vote_map_option);
    set_bool("misc.automation.noisemaker_spam", config.misc.automation.noisemaker_spam);
    set_int("misc.automation.voice_command_spam", static_cast<int>(config.misc.automation.voice_command_spam));
    set_bool("misc.automation.micspam", config.misc.automation.micspam);
    set_int("misc.automation.micspam_interval_on_seconds", config.misc.automation.micspam_interval_on_seconds);
    set_int("misc.automation.micspam_interval_off_seconds", config.misc.automation.micspam_interval_off_seconds);
    set_bool("misc.automation.micspam_from_file", config.misc.automation.micspam_from_file);
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
    set_bool("misc.automation.auto_mvm_abandon_mannup", config.misc.automation.auto_mvm_abandon_mannup);
    set_bool("misc.automation.mvm_buybot", config.misc.automation.mvm_buybot);
    set_int("misc.automation.mvm_buybot_max_cash", config.misc.automation.mvm_buybot_max_cash);
    set_bool("misc.automation.mvm_buybot_auto_class", config.misc.automation.mvm_buybot_auto_class);
    set_int("misc.automation.mvm_buybot_class", static_cast<int>(config.misc.automation.mvm_buybot_class));
    set_bool("misc.automation.medic_autoheal", config.misc.automation.medic_autoheal);
    set_bool("misc.automation.medic_autovacc", config.misc.automation.medic_autovacc);
    set_bool("misc.automation.medic_autouber", config.misc.automation.medic_autouber);
    set_int("misc.automation.medic_heal_targets_mask", static_cast<int>(config.misc.automation.medic_heal_targets_mask));
    set_bool("misc.automation.auto_report", config.misc.automation.auto_report);
    set_bool("misc.automation.auto_queue", config.misc.automation.auto_queue);
    set_bool("misc.automation.auto_requeue", config.misc.automation.auto_requeue);
    set_bool("misc.automation.requeue_on_kick", config.misc.automation.requeue_on_kick);
    set_int("misc.automation.queue_mode", static_cast<int>(config.misc.automation.queue_mode));
    set_bool("misc.automation.boost_queue_enabled", config.misc.automation.boost_queue_enabled);
    set_int("misc.automation.boost_queue", static_cast<int>(config.misc.automation.boost_queue));
    set_bool("misc.automation.auto_casual_join", config.misc.automation.auto_casual_join);
    set_int("misc.automation.auto_queue_mode", config.misc.automation.auto_queue_mode);
    set_int("misc.automation.rq_if_players_lte", config.misc.automation.rq_if_players_lte);
    set_int("misc.automation.rq_if_players_gte", config.misc.automation.rq_if_players_gte);
    set_int("misc.automation.rq_if_ipc_bots_gt", config.misc.automation.rq_if_ipc_bots_gt);
    set_bool("misc.automation.rq_if_no_navmesh", config.misc.automation.rq_if_no_navmesh);
    set_bool("misc.automation.rq_ignore_friends", config.misc.automation.rq_ignore_friends);
    set_int("misc.automation.requeue_action", static_cast<int>(config.misc.automation.requeue_action));
    set_bool("misc.automation.region_selector", config.misc.automation.region_selector);
    set_string(
        "misc.automation.region_selector_allowed_mask",
        std::to_string(config.misc.automation.region_selector_allowed_mask));
    set_bool("misc.automation.navbot_enabled", config.misc.automation.navbot_enabled);
    set_int("misc.automation.navbot_behavior", static_cast<int>(config.misc.automation.navbot_behavior));
    set_bool("misc.automation.navbot_draw_path", config.misc.automation.navbot_draw_path);
    set_bool("misc.automation.navbot_draw_path_boxes", config.misc.automation.navbot_draw_path_boxes);
    set_color("misc.automation.navbot_path_color", config.misc.automation.navbot_path_color);
    set_bool("misc.automation.navbot_dont_path_during_warmup", config.misc.automation.navbot_dont_path_during_warmup);
    set_bool("misc.automation.navbot_hazards", config.misc.automation.navbot_hazards);
    set_bool("misc.automation.navbot_look_at_path", config.misc.automation.navbot_look_at_path);
    set_bool("misc.automation.navbot_look_at_path_silent", config.misc.automation.navbot_look_at_path_silent);
    set_int("misc.automation.navbot_weapon_selection", static_cast<int>(config.misc.automation.navbot_weapon_selection));
    set_int("misc.automation.enemy_stalk_mode", static_cast<int>(config.misc.automation.enemy_stalk_mode));
    set_float("misc.automation.navbot_melee_target_range", config.misc.automation.navbot_melee_target_range);
    set_int("misc.automation.navbot_look_at_path_speed", config.misc.automation.navbot_look_at_path_speed);
    set_int("misc.automation.navbot_look_at_path_spin_chance", config.misc.automation.navbot_look_at_path_spin_chance);
    set_float("misc.automation.navbot_crumb_blacklist_seconds", config.misc.automation.navbot_crumb_blacklist_seconds);
    set_bool("misc.automation.navbot_debug_text", config.misc.automation.navbot_debug_text);
    set_int("misc.automation.navbot_excluded_jobs_mask", static_cast<int>(config.misc.automation.navbot_excluded_jobs_mask));
    set_bool("misc.automation.followbot_enabled", config.misc.automation.followbot_enabled);
    set_int("misc.automation.followbot_use_nav", static_cast<int>(config.misc.automation.followbot_use_nav));
    set_int("misc.automation.followbot_targets", static_cast<int>(config.misc.automation.followbot_targets));
    set_int("misc.automation.followbot_preference", static_cast<int>(config.misc.automation.followbot_preference));
    set_int("misc.automation.followbot_look", static_cast<int>(config.misc.automation.followbot_look));
    set_bool("misc.automation.followbot_look_no_snap", config.misc.automation.followbot_look_no_snap);
    set_bool("misc.automation.followbot_ignore_afk", config.misc.automation.followbot_ignore_afk);
    set_int("misc.automation.followbot_min_priority", config.misc.automation.followbot_min_priority);
    set_int("misc.automation.followbot_max_nodes", config.misc.automation.followbot_max_nodes);
    set_float("misc.automation.followbot_activation_distance", config.misc.automation.followbot_activation_distance);
    set_float("misc.automation.followbot_follow_distance", config.misc.automation.followbot_follow_distance);
    set_float("misc.automation.followbot_abandon_distance", config.misc.automation.followbot_abandon_distance);
    set_float("misc.automation.followbot_nav_abandon_distance", config.misc.automation.followbot_nav_abandon_distance);
    set_bool("crithack.enabled", config.crithack.enabled);
    set_bool("crithack.force_crits", config.crithack.force_crits);
    set_bool("crithack.always_melee", config.crithack.always_melee);
    set_bool("crithack.avoid_random", config.crithack.avoid_random);
    set_int("debug.font_height", config.debug.font_height);
    set_int("debug.font_weight", config.debug.font_weight);
    set_bool("debug.render_all_entities", config.debug.debug_render_all_entities);
    set_bool("debug.show_active_flag_ids_of_players", config.debug.show_active_flag_ids_of_players);
}

void config_store::export_config(Config& config) const
{
#if 0 // Inventory changer config temporarily disabled.
    const auto load_inventory_slot = [this](const std::string& prefix, Misc::InventorySlot& slot) {
        slot.item = get_int(prefix + ".item", slot.item);
        slot.paintkit = get_int(prefix + ".paintkit", slot.paintkit);
        slot.wear = get_int(prefix + ".wear", slot.wear);
        slot.seed = get_int(prefix + ".seed", slot.seed);
        slot.style = get_int(prefix + ".style", slot.style);
        slot.sheen = get_int(prefix + ".sheen", slot.sheen);
        slot.killstreak = get_int(prefix + ".killstreak", slot.killstreak);
        slot.unusual = get_int(prefix + ".unusual", slot.unusual);
    };
#endif
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
    {
        const uint32_t saved_aim_at = static_cast<uint32_t>(get_int(
            "aimbot.aim_at", static_cast<int>(config.aimbot.aim_at)));
        if (has_key("aimbot.aim_at") && get_int("aimbot.aim_at_version", 0) < 2) {
            uint32_t migrated = 0;
            if ((saved_aim_at & (1u << 0)) != 0) migrated |= Aim::aim_at_enemies;
            if ((saved_aim_at & (1u << 1)) != 0) migrated |= Aim::aim_at_buildings;
            if ((saved_aim_at & (1u << 2)) != 0) migrated |= Aim::aim_at_mvm_robots;
            config.aimbot.aim_at = migrated;
        } else {
            config.aimbot.aim_at = saved_aim_at & Aim::aim_at_all;
        }
    }
    const int legacy_aim_mode = get_bool("aimbot.silent", config.aimbot.aim_mode == Aim::AimMode::PSILENT)
      ? static_cast<int>(Aim::AimMode::PSILENT)
      : static_cast<int>(Aim::AimMode::PLAIN);
    int saved_aim_mode = get_int("aimbot.aim_mode", legacy_aim_mode);
    if (!has_key("aimbot.aim_mode") && has_key("aimbot.projectile_aim_mode")) {
        const int legacy_projectile_mode = std::clamp(get_int("aimbot.projectile_aim_mode", 2), 0, 2);
        saved_aim_mode = legacy_projectile_mode == 2
            ? static_cast<int>(Aim::AimMode::PSILENT)
            : static_cast<int>(Aim::AimMode::PLAIN);
    }
    if (saved_aim_mode < static_cast<int>(Aim::AimMode::PLAIN) ||
        saved_aim_mode > static_cast<int>(Aim::AimMode::PSILENT)) {
        saved_aim_mode = static_cast<int>(Aim::AimMode::PLAIN);
    }
    config.aimbot.aim_mode = static_cast<Aim::AimMode>(saved_aim_mode);
    config.aimbot.fov = get_float("aimbot.fov", config.aimbot.fov);
    config.aimbot.assist_strength = std::clamp(
        get_float("aimbot.assist_strength", config.aimbot.assist_strength),
        0.0f,
        100.0f);
    config.aimbot.draw_fov = get_bool("aimbot.draw_fov", config.aimbot.draw_fov);
    config.aimbot.shoot_through_glass = get_bool("aimbot.shoot_through_glass", config.aimbot.shoot_through_glass);
    config.aimbot.spread_compensation = get_bool("aimbot.spread_compensation", config.aimbot.spread_compensation);
    config.aimbot.resolver = get_bool("aimbot.resolver", config.aimbot.resolver);
    config.aimbot.resolver_max_yaws = std::clamp(
        get_int("aimbot.resolver_max_yaws", config.aimbot.resolver_max_yaws),
        4,
        24);
    config.aimbot.debug_overlay = get_bool("aimbot.debug_overlay", config.aimbot.debug_overlay);
    config.aimbot.debug_overlay_x = get_float("aimbot.debug_overlay_x", config.aimbot.debug_overlay_x);
    config.aimbot.debug_overlay_y = get_float("aimbot.debug_overlay_y", config.aimbot.debug_overlay_y);
    config.aimbot.hitscan_hitboxes = static_cast<uint32_t>(get_int("aimbot.hitscan_hitboxes", static_cast<int>(config.aimbot.hitscan_hitboxes)));
    config.aimbot.melee_hitboxes = static_cast<uint32_t>(get_int("aimbot.melee_hitboxes", static_cast<int>(config.aimbot.melee_hitboxes)));
    config.aimbot.melee_walk_to_target = get_bool("aimbot.melee_walk_to_target", config.aimbot.melee_walk_to_target);
    config.aimbot.melee_auto_backstab = get_bool("aimbot.melee_auto_backstab", config.aimbot.melee_auto_backstab);
    config.aimbot.melee_ignore_razorback = get_bool("aimbot.melee_ignore_razorback", config.aimbot.melee_ignore_razorback);
    config.aimbot.melee_swing_prediction = get_bool("aimbot.melee_swing_prediction", config.aimbot.melee_swing_prediction);
    config.aimbot.melee_swing_ticks = std::clamp(
        get_int("aimbot.melee_swing_ticks", config.aimbot.melee_swing_ticks), 0, 14);
    config.aimbot.melee_swing_predict_lag = get_bool("aimbot.melee_swing_predict_lag", config.aimbot.melee_swing_predict_lag);
    config.aimbot.melee_swing_validate_mode = std::clamp(
        get_int("aimbot.melee_swing_validate_mode", config.aimbot.melee_swing_validate_mode), 0, 2);
    const bool legacy_scoped_only = get_bool(
        "aimbot.scoped_only",
        get_bool("aimbot.sniper_scoped_only", false));
    const bool legacy_auto_scope = get_bool("aimbot.auto_scope", false);
    const bool legacy_auto_unscope = get_bool("aimbot.auto_unscope", false);
    config.aimbot.sniper_auto_scope = get_bool(
        "aimbot.sniper_auto_scope",
        legacy_scoped_only || legacy_auto_scope || config.aimbot.sniper_auto_scope);
    config.aimbot.sniper_auto_unscope = get_bool(
        "aimbot.sniper_auto_unscope",
        legacy_auto_unscope || config.aimbot.sniper_auto_unscope);
    config.aimbot.sniper_scope_distance = std::clamp(
        get_float("aimbot.sniper_scope_distance", config.aimbot.sniper_scope_distance),
        250.0f,
        4000.0f);
    config.aimbot.sniper_scope_cancel_time = std::clamp(
        get_float("aimbot.sniper_scope_cancel_time", config.aimbot.sniper_scope_cancel_time),
        1.0f,
        5.0f);
    config.aimbot.auto_rev = get_bool("aimbot.auto_rev", config.aimbot.auto_rev);
    config.aimbot.auto_unrev = get_bool("aimbot.auto_unrev", config.aimbot.auto_unrev);
    config.aimbot.auto_rev_threshold = get_float("aimbot.auto_rev_threshold", config.aimbot.auto_rev_threshold);
    {
        const bool has_legacy_modifiers = has_key("aimbot.wait_for_headshot") ||
            has_key("aimbot.wait_for_charge") ||
            has_key("aimbot.body_aim_if_lethal");
        uint32_t legacy = config.aimbot.hitscan_modifiers;
        if (has_legacy_modifiers) {
            legacy = 0;
        }
        if (get_bool("aimbot.wait_for_headshot", false))   legacy |= Aim::hitscan_mod_wait_for_headshot;
        if (get_bool("aimbot.wait_for_charge", false))     legacy |= Aim::hitscan_mod_wait_for_charge;
        if (get_bool("aimbot.body_aim_if_lethal", false))  legacy |= Aim::hitscan_mod_body_aim_if_lethal;
        if (legacy_scoped_only)                             legacy |= Aim::hitscan_mod_scoped_only;
        const bool has_saved_modifiers = has_key("aimbot.hitscan_modifiers");
        const uint32_t saved_modifiers = static_cast<uint32_t>(get_int(
            "aimbot.hitscan_modifiers",
            static_cast<int>(config.aimbot.hitscan_modifiers)));
        if (!has_saved_modifiers) {
            config.aimbot.hitscan_modifiers = legacy & Aim::hitscan_mod_all;
        } else if (get_int("aimbot.hitscan_modifier_version", 0) < 2) {
            uint32_t migrated = 0;
            if ((saved_modifiers & (1u << 1)) != 0) migrated |= Aim::hitscan_mod_wait_for_headshot;
            if ((saved_modifiers & (1u << 2)) != 0) migrated |= Aim::hitscan_mod_wait_for_charge;
            if ((saved_modifiers & (1u << 3)) != 0) migrated |= Aim::hitscan_mod_body_aim_if_lethal;
            if (legacy_scoped_only) migrated |= Aim::hitscan_mod_scoped_only;
            config.aimbot.hitscan_modifiers = migrated;
        } else if (get_int("aimbot.hitscan_modifier_version", 0) < 3) {
            config.aimbot.hitscan_modifiers = saved_modifiers & Aim::hitscan_mod_all;
            if (legacy_scoped_only) {
                config.aimbot.hitscan_modifiers |= Aim::hitscan_mod_scoped_only;
            }
        } else {
            config.aimbot.hitscan_modifiers = saved_modifiers & Aim::hitscan_mod_all;
        }
    }
    config.aimbot.tapfire_distance = std::clamp(
        get_float("aimbot.tapfire_distance", config.aimbot.tapfire_distance), 250.0f, 2000.0f);
    config.aimbot.ignore_invisible = std::clamp(
        get_float("aimbot.ignore_invisible", config.aimbot.ignore_invisible), 0.0f, 100.0f);
    config.aimbot.ignore_unsimulated_ticks = std::clamp(
        get_int("aimbot.ignore_unsimulated_ticks", config.aimbot.ignore_unsimulated_ticks), 0, 21);
    config.aimbot.multipoint_scale = std::clamp(
        get_float("aimbot.multipoint_scale", config.aimbot.multipoint_scale),
        0.0f,
        100.0f);
    config.aimbot.bone_size_subtract = std::clamp(
        get_float("aimbot.bone_size_subtract", config.aimbot.bone_size_subtract),
        0.0f,
        12.0f);
    config.aimbot.bone_size_min_scale = std::clamp(
        get_float("aimbot.bone_size_min_scale", config.aimbot.bone_size_min_scale),
        0.05f,
        1.0f);
    config.aimbot.projectile_active = get_bool("aimbot.projectile_active", config.aimbot.projectile_active);
    config.aimbot.projectile_modifiers = static_cast<uint32_t>(
        std::clamp(get_int("aimbot.projectile_modifiers", static_cast<int>(config.aimbot.projectile_modifiers)),
          0, static_cast<int>(Aim::projectile_mod_all))) & Aim::projectile_mod_all;
    config.aimbot.projectile_mode = std::clamp(get_int("aimbot.projectile_mode", config.aimbot.projectile_mode), 0, 2);
    config.aimbot.projectile_prediction_mode = static_cast<Aim::ProjectilePredictionMode>(std::clamp(
        get_int("aimbot.projectile_prediction_mode", static_cast<int>(config.aimbot.projectile_prediction_mode)), 0, 1));
    config.aimbot.projectile_aim_pos = std::clamp(get_int("aimbot.projectile_aim_pos", config.aimbot.projectile_aim_pos), 0, 3);
    config.aimbot.projectile_fov = std::clamp(get_float("aimbot.projectile_fov", config.aimbot.projectile_fov), 0.0f, 180.0f);
    config.aimbot.projectile_max_sim_targets = std::clamp(
        get_int("aimbot.projectile_max_sim_targets", config.aimbot.projectile_max_sim_targets), 1, 6);
    config.aimbot.projectile_max_sim_time = std::clamp(
        get_float("aimbot.projectile_max_sim_time", config.aimbot.projectile_max_sim_time), 0.25f, 5.0f);
    config.aimbot.projectile_multipoint_scale = std::clamp(
        get_int("aimbot.projectile_multipoint_scale", config.aimbot.projectile_multipoint_scale), 50, 100);
    config.aimbot.projectile_splash_policy = std::clamp(
        get_int("aimbot.projectile_splash_policy", config.aimbot.projectile_splash_policy), 0, 2);
    config.aimbot.projectile_smooth_flamethrowers_active = get_bool(
        "aimbot.projectile_smooth_flamethrowers_active", config.aimbot.projectile_smooth_flamethrowers_active);
    config.aimbot.projectile_smooth_flamethrowers = std::clamp(
        get_float("aimbot.projectile_smooth_flamethrowers", config.aimbot.projectile_smooth_flamethrowers), 1.0f, 100.0f);
    uint32_t ignore_default = config.aimbot.ignore;
    if (get_bool("aimbot.ignore_friends", true)) {
        ignore_default |= Aim::ignore_friends;
    } else {
        ignore_default &= ~Aim::ignore_friends;
    }
    if (get_int("aimbot.ipc_bot_mode", get_bool("aimbot.ignore_ipc_bots", true) ? 1 : 0) == 1) {
        ignore_default |= Aim::ignore_ipc_bots;
    } else {
        ignore_default &= ~Aim::ignore_ipc_bots;
    }
    if (get_int("aimbot.ignore_version", 0) < 2) {

        ignore_default &= Aim::ignore_friends | Aim::ignore_ipc_bots |
            Aim::ignore_cloaked | Aim::ignore_invulnerable;
    }
    config.aimbot.ignore = static_cast<std::uint32_t>(
        std::clamp(get_int("aimbot.ignore", static_cast<int>(ignore_default)), 0, static_cast<int>(Aim::ignore_all)));
    config.aimbot.max_targets = std::clamp(get_int("aimbot.max_targets", config.aimbot.max_targets), 1, 6);
    config.backtrack.enabled = get_bool("backtrack.enabled", config.backtrack.enabled);
    config.backtrack.fake_latency_ms = std::clamp(
        get_float("backtrack.fake_latency_ms", config.backtrack.fake_latency_ms),
        0.0f,
        1000.0f);
    config.backtrack.interp_ms = std::clamp(
        get_float("backtrack.interp_ms", config.backtrack.interp_ms),
        0.0f,
        1000.0f);
    config.backtrack.fake_interp = get_bool("backtrack.fake_interp", config.backtrack.fake_interp);
    config.backtrack.window_ms = std::clamp(
        get_int("backtrack.window_ms", config.backtrack.window_ms),
        0,
        1000);
    config.backtrack.prefer_on_shot = get_bool("backtrack.prefer_on_shot", config.backtrack.prefer_on_shot);
    config.backtrack.to_crosshair = get_bool("backtrack.to_crosshair", config.backtrack.to_crosshair);
    config.backtrack.offset_ticks = std::clamp(
        get_int("backtrack.offset_ticks", config.backtrack.offset_ticks), -4, 4);
    config.backtrack.visualizer = get_bool("backtrack.visualizer", config.backtrack.visualizer);
    config.backtrack.visualizer_ticks = std::clamp(
        get_int("backtrack.visualizer_ticks", config.backtrack.visualizer_ticks),
        1,
        80);
    config.backtrack.visualizer_mode = static_cast<backtrack_config::visualizer_style>(std::clamp(
        get_int("backtrack.visualizer_mode", static_cast<int>(config.backtrack.visualizer_mode)),
        0,
        4));
    config.ipc.enabled = get_bool("ipc.enabled", config.ipc.enabled);
    config.ipc.auto_connect = get_bool("ipc.auto_connect", config.ipc.auto_connect);
    config.ipc.auto_ignore_local_bots = get_bool("ipc.auto_ignore_local_bots", config.ipc.auto_ignore_local_bots);
#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE

    config.ipc.enabled = true;
    config.ipc.auto_connect = true;
    config.ipc.auto_ignore_local_bots = true;
#endif

    config.visual_groups.groups.clear();
    const int visual_group_count = std::clamp(get_int("visuals.groups.count", 0), 0, static_cast<int>(visual_group_config::max_groups));
    config.visual_groups.groups.reserve(static_cast<std::size_t>(visual_group_count));
    for (int index = 0; index < visual_group_count; ++index)
    {
        const std::string prefix = "visuals.groups." + std::to_string(index) + ".";
        visual_group group{};
        group.name = get_string(prefix + "name", group.name);
        group.color = get_color(prefix + "color", group.color);
        group.tags_override_color = get_bool(prefix + "tags_override_color", group.tags_override_color);
        group.targets = static_cast<uint32_t>(std::max(0, get_int(prefix + "targets", static_cast<int>(group.targets))));
        group.conditions = static_cast<uint32_t>(std::max(0, get_int(prefix + "conditions", static_cast<int>(group.conditions))));
        group.roles = parse_int_list(get_string(prefix + "roles", {}));
        group.players = static_cast<uint32_t>(std::max(0, get_int(prefix + "players", static_cast<int>(group.players))));
        group.buildings = static_cast<uint32_t>(std::max(0, get_int(prefix + "buildings", static_cast<int>(group.buildings))));
        group.projectiles = static_cast<uint32_t>(std::max(0, get_int(prefix + "projectiles", static_cast<int>(group.projectiles))));

        group.esp.draw_mask = static_cast<uint32_t>(get_int(prefix + "esp.draw_mask", static_cast<int>(group.esp.draw_mask)));
        group.esp.override_color = get_bool(prefix + "esp.override_color", group.esp.override_color);
        group.esp.color = get_color(prefix + "esp.color", group.esp.color);
        group.esp.box_style = static_cast<esp_box_type>(std::clamp(get_int(prefix + "esp.box_style", static_cast<int>(group.esp.box_style)), 0, 4));
        group.esp.start = std::clamp(get_float(prefix + "esp.start", group.esp.start), 0.0f, 8192.0f);
        group.esp.end = std::clamp(get_float(prefix + "esp.end", group.esp.end), 0.0f, 8192.0f);
        if (group.esp.end < group.esp.start) {
            group.esp.end = group.esp.start;
        }
        group.esp.smooth_alpha = get_bool(prefix + "esp.smooth_alpha", group.esp.smooth_alpha);
        group.esp.background_alpha = static_cast<uint8_t>(std::clamp(get_int(prefix + "esp.background_alpha", group.esp.background_alpha), 0, 255));
        group.esp.class_icon_scale = std::clamp(get_float(prefix + "esp.class_icon_scale", group.esp.class_icon_scale), 0.5f, 5.0f);
        group.esp.head_emoji_scale = std::clamp(get_float(prefix + "esp.head_emoji_scale", group.esp.head_emoji_scale), 0.5f, 5.0f);
        group.esp.head_emoji_style = std::clamp(get_int(prefix + "esp.head_emoji_style", group.esp.head_emoji_style), 0, 1);
        group.esp.mafia_level_position = static_cast<mafia_level_position>(std::clamp(get_int(prefix + "esp.mafia_level_position", static_cast<int>(group.esp.mafia_level_position)), 0, 2));
        const auto load_layers = [&](const char* side, std::vector<chams_layer>& layers) {
            const std::string side_prefix = prefix + "chams." + side + ".";
            const int count = std::clamp(get_int(side_prefix + "count", static_cast<int>(layers.size())), 0, 64);
            layers.clear();
            layers.reserve(static_cast<std::size_t>(count));
            for (int layer_index = 0; layer_index < count; ++layer_index)
            {
                chams_layer layer{};
                const std::string layer_prefix = side_prefix + std::to_string(layer_index) + ".";
                layer.material = get_string(layer_prefix + "material", layer.material);
                layer.color = get_color(layer_prefix + "color", layer.color);
                layer.start = std::clamp(get_float(layer_prefix + "start", layer.start), 0.0f, 8192.0f);
                layer.end = std::clamp(get_float(layer_prefix + "end", layer.end), 0.0f, 8192.0f);
                if (layer.end < layer.start) layer.end = layer.start;
                layer.smooth_alpha = get_bool(layer_prefix + "smooth_alpha", layer.smooth_alpha);
                layers.emplace_back(std::move(layer));
            }
        };
        load_layers("visible", group.chams.visible);
        load_layers("occluded", group.chams.occluded);
        group.glow.color = get_color(prefix + "glow.color", group.glow.color);
        group.glow.stencil = normalize_stencil_scale(get_float(prefix + "glow.stencil", group.glow.stencil));
        group.glow.blur = std::clamp(get_float(prefix + "glow.blur", group.glow.blur), 0.0f, 100.0f);
        group.glow.start = std::clamp(get_float(prefix + "glow.start", group.glow.start), 0.0f, 2048.0f);
        group.glow.end = std::clamp(get_float(prefix + "glow.end", group.glow.end), 0.0f, 8192.0f);
        if (group.glow.end < group.glow.start) group.glow.end = group.glow.start;
        group.glow.smooth_alpha = get_bool(prefix + "glow.smooth_alpha", group.glow.smooth_alpha);
        group.backtrack_visuals.enabled = get_bool(prefix + "backtrack_visuals.enabled", group.backtrack_visuals.enabled);
        group.backtrack_visuals.record_mode = std::clamp(
            get_int(prefix + "backtrack_visuals.record_mode", group.backtrack_visuals.record_mode), 0, 2);
        group.backtrack_visuals.ignore_z = get_bool(prefix + "backtrack_visuals.ignore_z", group.backtrack_visuals.ignore_z);
        const auto load_backtrack_layers = [&](const char* side, std::vector<chams_layer>& layers) {
            const std::string side_prefix = prefix + "backtrack_visuals.chams." + side + ".";
            const int count = std::clamp(get_int(side_prefix + "count", static_cast<int>(layers.size())), 0, 64);
            layers.clear();
            layers.reserve(static_cast<std::size_t>(count));
            for (int layer_index = 0; layer_index < count; ++layer_index)
            {
                chams_layer layer{};
                const std::string layer_prefix = side_prefix + std::to_string(layer_index) + ".";
                layer.material = get_string(layer_prefix + "material", layer.material);
                layer.color = get_color(layer_prefix + "color", layer.color);
                layer.start = std::clamp(get_float(layer_prefix + "start", layer.start), 0.0f, 8192.0f);
                layer.end = std::clamp(get_float(layer_prefix + "end", layer.end), 0.0f, 8192.0f);
                if (layer.end < layer.start) layer.end = layer.start;
                layer.smooth_alpha = get_bool(layer_prefix + "smooth_alpha", layer.smooth_alpha);
                layers.emplace_back(std::move(layer));
            }
        };
        load_backtrack_layers("visible", group.backtrack_visuals.chams.visible);
        load_backtrack_layers("occluded", group.backtrack_visuals.chams.occluded);
        group.backtrack_visuals.glow.color = get_color(prefix + "backtrack_visuals.glow.color", group.backtrack_visuals.glow.color);
        group.backtrack_visuals.glow.stencil = normalize_stencil_scale(
            get_float(prefix + "backtrack_visuals.glow.stencil", group.backtrack_visuals.glow.stencil));
        group.backtrack_visuals.glow.blur = std::clamp(
            get_float(prefix + "backtrack_visuals.glow.blur", group.backtrack_visuals.glow.blur), 0.0f, 100.0f);
        group.backtrack_visuals.glow.start = std::clamp(
            get_float(prefix + "backtrack_visuals.glow.start", group.backtrack_visuals.glow.start), 0.0f, 2048.0f);
        group.backtrack_visuals.glow.end = std::clamp(
            get_float(prefix + "backtrack_visuals.glow.end", group.backtrack_visuals.glow.end), 0.0f, 8192.0f);
        if (group.backtrack_visuals.glow.end < group.backtrack_visuals.glow.start) {
            group.backtrack_visuals.glow.end = group.backtrack_visuals.glow.start;
        }
        group.backtrack_visuals.glow.smooth_alpha = get_bool(
            prefix + "backtrack_visuals.glow.smooth_alpha", group.backtrack_visuals.glow.smooth_alpha);
        group.backtrack = static_cast<uint32_t>(std::max(0, get_int(prefix + "backtrack", static_cast<int>(group.backtrack))));
        group.trajectory = static_cast<uint32_t>(std::max(0, get_int(prefix + "trajectory", static_cast<int>(group.trajectory))));
        group.sightlines = static_cast<uint32_t>(std::max(0, get_int(prefix + "sightlines", static_cast<int>(group.sightlines))));
        config.visual_groups.groups.emplace_back(group);
    }
    config.visual_groups.active_group_mask = static_cast<uint32_t>(std::max(0, get_int("visuals.groups.active_mask", static_cast<int>(config.visual_groups.active_group_mask))));
    if (config.visual_groups.groups.empty()) {
        visual_groups::ensure_defaults();
    } else {
        const uint32_t valid_mask = config.visual_groups.groups.size() >= visual_group_config::max_groups ? 0xFFFFFFFFu : ((1u << config.visual_groups.groups.size()) - 1u);
        config.visual_groups.active_group_mask &= valid_mask;
    }

    config.visuals.removals.scope = get_bool("visuals.removals.scope", config.visuals.removals.scope);
    config.visuals.removals.zoom = get_bool("visuals.removals.zoom", config.visuals.removals.zoom);
    config.visuals.removals.arms = get_bool("visuals.removals.arms", config.visuals.removals.arms);
    config.visuals.removals.hats = get_bool("visuals.removals.hats", config.visuals.removals.hats);
    config.visuals.removals.cloak = get_bool("visuals.removals.cloak", config.visuals.removals.cloak);
    config.visuals.removals.disguise = get_bool("visuals.removals.disguise", config.visuals.removals.disguise);
    config.visuals.removals.taunts = get_bool("visuals.removals.taunts", config.visuals.removals.taunts);
    config.visuals.removals.contracker = get_bool("visuals.removals.contracker", config.visuals.removals.contracker);
    config.visuals.casual_medal.guaranteed_flip = get_bool(
        "visuals.casual_medal.guaranteed_flip", config.visuals.casual_medal.guaranteed_flip);
    config.visuals.casual_medal.changer = get_bool(
        "visuals.casual_medal.changer", config.visuals.casual_medal.changer);
    config.visuals.casual_medal.rank = std::clamp(
        get_int("visuals.casual_medal.rank", config.visuals.casual_medal.rank), 1, 1200);
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
    config.visuals.indicators.keybinds_x = get_float("visuals.indicators.keybinds_x", get_float("misc.exploits.keybind_indicator_x", config.visuals.indicators.x));
    config.visuals.indicators.keybinds_y = get_float("visuals.indicators.keybinds_y", get_float("misc.exploits.keybind_indicator_y", config.visuals.indicators.y + 92.0f));
    config.visuals.indicators.crit_hack_x = get_float("visuals.indicators.crit_hack_x", config.visuals.indicators.crit_hack_x);
    config.visuals.indicators.crit_hack_y = get_float("visuals.indicators.crit_hack_y", config.visuals.indicators.crit_hack_y);
    config.visuals.indicators.nospread_x = get_float("visuals.indicators.nospread_x", config.visuals.indicators.nospread_x);
    config.visuals.indicators.nospread_y = get_float("visuals.indicators.nospread_y", config.visuals.indicators.nospread_y);
    config.visuals.indicators.tickbase_bar_color = get_color(
        "visuals.indicators.tickbase_bar_color",
        config.visuals.indicators.tickbase_bar_color);
    config.visuals.indicators.crit_hack_bar_color = get_color(
        "visuals.indicators.crit_hack_bar_color",
        config.visuals.indicators.crit_hack_bar_color);
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
    config.visuals.radar.enabled = get_bool("visuals.radar.enabled", config.visuals.radar.enabled);
    config.visuals.radar.x = get_float("visuals.radar.x", config.visuals.radar.x);
    config.visuals.radar.y = get_float("visuals.radar.y", config.visuals.radar.y);
    config.visuals.radar.size = std::clamp(
        get_int("visuals.radar.size", config.visuals.radar.size), 100, 600);
    config.visuals.radar.zoom = std::clamp(
        get_float("visuals.radar.zoom", config.visuals.radar.zoom), 5.0f, 50.0f);
    config.visuals.radar.icon_size = std::clamp(
        get_int("visuals.radar.icon_size", config.visuals.radar.icon_size), 10, 40);
    config.visuals.radar.show_teammates = get_bool("visuals.radar.show_teammates", config.visuals.radar.show_teammates);
    config.visuals.radar.show_enemies = get_bool("visuals.radar.show_enemies", config.visuals.radar.show_enemies);
    config.visuals.radar.use_icons = get_bool("visuals.radar.use_icons", config.visuals.radar.use_icons);
    config.visuals.radar.axis_lines = get_bool("visuals.radar.axis_lines", config.visuals.radar.axis_lines);
    config.visuals.radar.range_rings = std::clamp(
        get_int("visuals.radar.range_rings", config.visuals.radar.range_rings), 0, 8);
    config.visuals.skybox_changer_index = std::clamp(
        get_int("visuals.skybox_changer_index", config.visuals.skybox_changer_index), 0, 31);
    config.visuals.override_fov = get_bool("visuals.override_fov", config.visuals.override_fov);
    config.visuals.custom_fov = get_float("visuals.custom_fov", config.visuals.custom_fov);
    config.visuals.override_zoom_fov = get_bool("visuals.override_zoom_fov", config.visuals.override_zoom_fov);
    config.visuals.custom_zoom_fov = std::clamp(
        get_float("visuals.custom_zoom_fov", config.visuals.custom_zoom_fov), 1.0f, 150.0f);
    config.visuals.flat_zoom_sensitivity = get_bool(
        "visuals.flat_zoom_sensitivity", config.visuals.flat_zoom_sensitivity);
    config.visuals.override_viewmodel_fov = get_bool("visuals.override_viewmodel_fov", config.visuals.override_viewmodel_fov);
    config.visuals.custom_viewmodel_fov = get_float("visuals.custom_viewmodel_fov", config.visuals.custom_viewmodel_fov);
    config.visuals.esp_lerp = get_bool("visuals.esp_lerp", config.visuals.esp_lerp);
    config.visuals.dormant_esp = get_bool("visuals.dormant_esp", config.visuals.dormant_esp);

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
    config.misc.movement.edge_jump = get_bool("misc.movement.edge_jump", config.misc.movement.edge_jump);
    config.misc.movement.jumpbug = get_bool("misc.movement.jumpbug", config.misc.movement.jumpbug);
    config.misc.movement.duck_jump = get_bool("misc.movement.duck_jump", config.misc.movement.duck_jump);
    config.misc.movement.break_jump = get_bool("misc.movement.break_jump", config.misc.movement.break_jump);
    config.misc.movement.auto_reverse_jump = get_bool("misc.movement.auto_reverse_jump", config.misc.movement.auto_reverse_jump);
    config.misc.movement.fast_stop = get_bool("misc.movement.fast_stop", config.misc.movement.fast_stop);
    config.misc.movement.fast_accelerate = get_bool("misc.movement.fast_accelerate", config.misc.movement.fast_accelerate);
    config.misc.movement.auto_edgebug = static_cast<Misc::Movement::auto_edgebug_mode>(std::clamp(
        get_int("misc.movement.auto_edgebug", static_cast<int>(config.misc.movement.auto_edgebug)),
        0,
        3));
    config.misc.movement.no_push = get_bool("misc.movement.no_push", config.misc.movement.no_push);
    config.misc.movement.taunt_slide = get_bool("misc.movement.taunt_slide", config.misc.movement.taunt_slide);
    config.misc.movement.moonwalk = get_bool("misc.movement.moonwalk", config.misc.movement.moonwalk);
    config.misc.movement.moonwalk_forward = get_bool("misc.movement.moonwalk_forward", config.misc.movement.moonwalk_forward);
    config.misc.movement.moonwalk_navbot_compat = get_bool("misc.movement.moonwalk_navbot_compat", config.misc.movement.moonwalk_navbot_compat);
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
    config.misc.exploits.doubletap_ticks = std::clamp(
        get_int("misc.exploits.doubletap_ticks", config.misc.exploits.doubletap_ticks),
        1,
        21);
    config.misc.exploits.warp = get_bool("misc.exploits.warp", config.misc.exploits.warp);
    config.misc.exploits.warp_ticks = std::clamp(
        get_int("misc.exploits.warp_ticks", config.misc.exploits.warp_ticks),
        1,
        21);
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
#if 0 // Inventory changer config temporarily disabled.
    config.misc.inventory_changer.enabled = get_bool(
        "misc.inventory_changer.enabled", config.misc.inventory_changer.enabled);
    config.misc.inventory_changer.apply_to_all = get_bool(
        "misc.inventory_changer.apply_to_all", config.misc.inventory_changer.apply_to_all);
    config.misc.inventory_changer.debug = get_bool(
        "misc.inventory_changer.debug", config.misc.inventory_changer.debug);
    config.misc.inventory_changer.redirects = get_string(
        "misc.inventory_changer.redirects", config.misc.inventory_changer.redirects);
    config.misc.inventory_changer.boxes = get_string(
        "misc.inventory_changer.boxes", config.misc.inventory_changer.boxes);
    config.misc.inventory_changer.attributes = get_string(
        "misc.inventory_changer.attributes", config.misc.inventory_changer.attributes);
    load_inventory_slot("misc.inventory_changer.primary", config.misc.inventory_changer.primary);
    load_inventory_slot("misc.inventory_changer.secondary", config.misc.inventory_changer.secondary);
    load_inventory_slot("misc.inventory_changer.melee", config.misc.inventory_changer.melee);
    load_inventory_slot("misc.inventory_changer.hat1", config.misc.inventory_changer.hat1);
    load_inventory_slot("misc.inventory_changer.hat2", config.misc.inventory_changer.hat2);
    load_inventory_slot("misc.inventory_changer.hat3", config.misc.inventory_changer.hat3);
    config.misc.inventory_changer.taunt1_unusual = get_int(
        "misc.inventory_changer.taunt1_unusual", config.misc.inventory_changer.taunt1_unusual);
    config.misc.inventory_changer.crate = get_int(
        "misc.inventory_changer.crate", config.misc.inventory_changer.crate);
    config.misc.inventory_changer.key = get_int(
        "misc.inventory_changer.key", config.misc.inventory_changer.key);
#endif
#if defined(CATHOOK_TEXTMODE) && CATHOOK_TEXTMODE

    config.misc.exploits.null_graphics = true;
#endif

    config.misc.exploits.legacy_tickbase_indicator = false;
    config.misc.exploits.keybind_indicator = (config.visuals.indicators.enabled_mask & Visuals::Indicators::keybinds) != 0;
    config.misc.exploits.legacy_tickbase_indicator_x = config.visuals.indicators.legacy_ticks_x;
    config.misc.exploits.legacy_tickbase_indicator_y = config.visuals.indicators.legacy_ticks_y;
    config.misc.exploits.keybind_indicator_x = config.visuals.indicators.keybinds_x;
    config.misc.exploits.keybind_indicator_y = config.visuals.indicators.keybinds_y;
    config.misc.menu.use_custom_font = get_bool("misc.menu.use_custom_font", config.misc.menu.use_custom_font);
    config.misc.menu.custom_font = get_string("misc.menu.custom_font", config.misc.menu.custom_font);
    config.misc.menu.dpi_scale = std::clamp(get_int("misc.menu.dpi_scale", config.misc.menu.dpi_scale), 0, 4);
    config.misc.menu.theme_color = get_color("misc.menu.theme_color", config.misc.menu.theme_color);
    config.misc.automation.auto_class_select = get_bool("misc.automation.auto_class_select", config.misc.automation.auto_class_select);
    config.misc.automation.class_selected = static_cast<tf_class>(std::clamp(
        get_int("misc.automation.class_selected", static_cast<int>(config.misc.automation.class_selected)),
        0,
        9));
    config.misc.automation.auto_class_dont_join_during_warmup = get_bool(
        "misc.automation.auto_class_dont_join_during_warmup",
        config.misc.automation.auto_class_dont_join_during_warmup);
    config.misc.automation.anti_afk = get_bool("misc.automation.anti_afk", config.misc.automation.anti_afk);
    config.misc.automation.anti_autobalance = get_bool("misc.automation.anti_autobalance", config.misc.automation.anti_autobalance);
    config.misc.automation.anti_motd = get_bool("misc.automation.anti_motd", config.misc.automation.anti_motd);
    config.misc.automation.anti_motd_dont_close_during_warmup = get_bool(
        "misc.automation.anti_motd_dont_close_during_warmup",
        config.misc.automation.anti_motd_dont_close_during_warmup);
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
    config.misc.automation.micspam_from_file = get_bool(
        "misc.automation.micspam_from_file", config.misc.automation.micspam_from_file);
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
    config.misc.automation.auto_mvm_abandon_mannup = get_bool(
        "misc.automation.auto_mvm_abandon_mannup",
        config.misc.automation.auto_mvm_abandon_mannup);
    config.misc.automation.mvm_buybot = get_bool(
        "misc.automation.mvm_buybot",
        config.misc.automation.mvm_buybot);
    config.misc.automation.mvm_buybot_max_cash = std::clamp(
        get_int("misc.automation.mvm_buybot_max_cash", config.misc.automation.mvm_buybot_max_cash),
        0,
        50000);
    config.misc.automation.mvm_buybot_auto_class = get_bool(
        "misc.automation.mvm_buybot_auto_class",
        config.misc.automation.mvm_buybot_auto_class);
    config.misc.automation.mvm_buybot_class = static_cast<tf_class>(std::clamp(
        get_int("misc.automation.mvm_buybot_class", static_cast<int>(config.misc.automation.mvm_buybot_class)),
        1,
        9));
    config.misc.automation.medic_autoheal = get_bool("misc.automation.medic_autoheal", config.misc.automation.medic_autoheal);
    config.misc.automation.medic_autovacc = get_bool("misc.automation.medic_autovacc", config.misc.automation.medic_autovacc);
    config.misc.automation.medic_autouber = get_bool("misc.automation.medic_autouber", config.misc.automation.medic_autouber);
    config.misc.automation.medic_heal_targets_mask = static_cast<uint32_t>(std::clamp(
        get_int("misc.automation.medic_heal_targets_mask", static_cast<int>(config.misc.automation.medic_heal_targets_mask)),
        0,
        static_cast<int>(Misc::Automation::medic_heal_target_default)));
    config.misc.automation.auto_report = get_bool("misc.automation.auto_report", config.misc.automation.auto_report);
    config.misc.automation.auto_queue = get_bool("misc.automation.auto_queue", config.misc.automation.auto_queue);
    config.misc.automation.auto_requeue = get_bool("misc.automation.auto_requeue", config.misc.automation.auto_requeue);
    config.misc.automation.requeue_on_kick = get_bool("misc.automation.requeue_on_kick", config.misc.automation.requeue_on_kick);
    config.misc.automation.queue_mode = static_cast<Misc::Automation::queueing_mode>(std::clamp(
        get_int("misc.automation.queue_mode", static_cast<int>(config.misc.automation.queue_mode)),
        0,
        1));
    config.misc.automation.boost_queue_enabled = get_bool(
        "misc.automation.boost_queue_enabled",
        config.misc.automation.boost_queue_enabled);
    config.misc.automation.boost_queue = static_cast<Misc::Automation::boost_queue_mode>(std::clamp(
        get_int("misc.automation.boost_queue", static_cast<int>(config.misc.automation.boost_queue)),
        0,
        1));
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
    config.misc.automation.rq_if_ipc_bots_gt = std::clamp(
        get_int("misc.automation.rq_if_ipc_bots_gt", config.misc.automation.rq_if_ipc_bots_gt),
        0,
        32);
    config.misc.automation.rq_if_no_navmesh = get_bool(
        "misc.automation.rq_if_no_navmesh",
        config.misc.automation.rq_if_no_navmesh);
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
    config.misc.automation.navbot_behavior = static_cast<Misc::Automation::navbot_mode>(std::clamp(
        get_int("misc.automation.navbot_behavior", static_cast<int>(config.misc.automation.navbot_behavior)),
        0,
        1));
    config.misc.automation.navbot_draw_path = get_bool("misc.automation.navbot_draw_path", config.misc.automation.navbot_draw_path);
    config.misc.automation.navbot_draw_path_boxes = get_bool(
        "misc.automation.navbot_draw_path_boxes",
        config.misc.automation.navbot_draw_path_boxes);
    config.misc.automation.navbot_path_color = get_color(
        "misc.automation.navbot_path_color",
        config.misc.automation.navbot_path_color);
    const bool legacy_navbot_warmup_block = std::clamp(
        get_int("misc.automation.navbot_block_during_enum", 0), 0, 2) != 0;
    config.misc.automation.navbot_dont_path_during_warmup = get_bool(
        "misc.automation.navbot_dont_path_during_warmup",
        legacy_navbot_warmup_block);
    config.misc.automation.navbot_hazards = get_bool(
        "misc.automation.navbot_hazards",
        config.misc.automation.navbot_hazards);
    config.misc.automation.navbot_look_at_path = get_bool("misc.automation.navbot_look_at_path", config.misc.automation.navbot_look_at_path);
    config.misc.automation.navbot_look_at_path_silent = get_bool(
        "misc.automation.navbot_look_at_path_silent",
        config.misc.automation.navbot_look_at_path_silent);
    config.misc.automation.navbot_weapon_selection = static_cast<Misc::Automation::navbot_weapon_mode>(std::clamp(
        get_int("misc.automation.navbot_weapon_selection", static_cast<int>(config.misc.automation.navbot_weapon_selection)),
        0,
        4));
    config.misc.automation.enemy_stalk_mode = static_cast<Misc::Automation::navbot_enemy_stalk_mode>(std::clamp(
        get_int("misc.automation.enemy_stalk_mode", static_cast<int>(config.misc.automation.enemy_stalk_mode)),
        0,
        1));
    config.misc.automation.navbot_melee_target_range = std::clamp(
        get_float("misc.automation.navbot_melee_target_range", config.misc.automation.navbot_melee_target_range),
        150.0f,
        4000.0f);
    config.misc.automation.navbot_look_at_path_speed = std::clamp(
        get_int("misc.automation.navbot_look_at_path_speed", config.misc.automation.navbot_look_at_path_speed),
        1,
        100);
    config.misc.automation.navbot_look_at_path_spin_chance = std::clamp(
        get_int("misc.automation.navbot_look_at_path_spin_chance", config.misc.automation.navbot_look_at_path_spin_chance),
        0,
        100);
    config.misc.automation.navbot_crumb_blacklist_seconds = std::clamp(
        get_float("misc.automation.navbot_crumb_blacklist_seconds", config.misc.automation.navbot_crumb_blacklist_seconds),
        50.0f,
        150.0f);
    config.misc.automation.navbot_debug_text = get_bool("misc.automation.navbot_debug_text", config.misc.automation.navbot_debug_text);
    config.misc.automation.navbot_excluded_jobs_mask = static_cast<uint32_t>(
        get_int("misc.automation.navbot_excluded_jobs_mask", static_cast<int>(config.misc.automation.navbot_excluded_jobs_mask)));
    config.misc.automation.followbot_enabled = get_bool("misc.automation.followbot_enabled", config.misc.automation.followbot_enabled);
    config.misc.automation.followbot_use_nav = static_cast<Misc::Automation::followbot_nav_mode>(std::clamp(
        get_int("misc.automation.followbot_use_nav", static_cast<int>(config.misc.automation.followbot_use_nav)), 0, 2));
    config.misc.automation.followbot_targets = static_cast<uint32_t>(get_int(
        "misc.automation.followbot_targets", static_cast<int>(config.misc.automation.followbot_targets))) &
        (Misc::Automation::followbot_teammates | Misc::Automation::followbot_enemies);
    config.misc.automation.followbot_preference = static_cast<Misc::Automation::followbot_prefer>(std::clamp(
        get_int("misc.automation.followbot_preference", static_cast<int>(config.misc.automation.followbot_preference)), 0, 2));
    config.misc.automation.followbot_look = static_cast<Misc::Automation::followbot_look_mode>(std::clamp(
        get_int("misc.automation.followbot_look", static_cast<int>(config.misc.automation.followbot_look)), 0, 3));
    config.misc.automation.followbot_look_no_snap = get_bool("misc.automation.followbot_look_no_snap", config.misc.automation.followbot_look_no_snap);
    config.misc.automation.followbot_ignore_afk = get_bool("misc.automation.followbot_ignore_afk", config.misc.automation.followbot_ignore_afk);
    config.misc.automation.followbot_min_priority = std::clamp(
        get_int("misc.automation.followbot_min_priority", config.misc.automation.followbot_min_priority), 0, 10);
    config.misc.automation.followbot_max_nodes = std::clamp(
        get_int("misc.automation.followbot_max_nodes", config.misc.automation.followbot_max_nodes), 50, 500);
    config.misc.automation.followbot_activation_distance = std::clamp(
        get_float("misc.automation.followbot_activation_distance", config.misc.automation.followbot_activation_distance), 10.0f, 1200.0f);
    config.misc.automation.followbot_follow_distance = std::clamp(
        get_float("misc.automation.followbot_follow_distance", config.misc.automation.followbot_follow_distance), 10.0f, 150.0f);
    config.misc.automation.followbot_abandon_distance = std::clamp(
        get_float("misc.automation.followbot_abandon_distance", config.misc.automation.followbot_abandon_distance), 250.0f, 1500.0f);
    config.misc.automation.followbot_nav_abandon_distance = std::clamp(
        get_float("misc.automation.followbot_nav_abandon_distance", config.misc.automation.followbot_nav_abandon_distance), 500.0f, 8000.0f);
    config.crithack.enabled = get_bool("crithack.enabled", config.crithack.enabled);
    const bool legacy_force_crits = get_bool("random_crits.force_crits", config.crithack.force_crits);
    config.crithack.force_crits = get_bool("crithack.force_crits", legacy_force_crits);
    const bool legacy_always_melee = get_bool("random_crits.always_melee_crit", config.crithack.always_melee);
    config.crithack.always_melee = get_bool("crithack.always_melee", legacy_always_melee);
    const bool legacy_avoid_random = get_bool("random_crits.save_bucket", config.crithack.avoid_random);
    config.crithack.avoid_random = get_bool("crithack.avoid_random", legacy_avoid_random);
    config.debug.font_height = get_int("debug.font_height", config.debug.font_height);
    config.debug.font_weight = get_int("debug.font_weight", config.debug.font_weight);
    config.debug.debug_render_all_entities = get_bool("debug.render_all_entities", config.debug.debug_render_all_entities);
    config.debug.show_active_flag_ids_of_players = get_bool("debug.show_active_flag_ids_of_players", config.debug.show_active_flag_ids_of_players);
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
           << value.r << ',' << value.g << ',' << value.b << ',' << value.a << ',' << (value.rainbow ? "true" : "false");
    m_values[std::move(key)] = stream.str();
}

void config_store::erase_key(const std::string_view key)
{
    m_values.erase(std::string{ key });
}

void config_store::erase_prefix(const std::string_view prefix)
{
    for (auto it = m_values.begin(); it != m_values.end();)
    {
        if (it->first.starts_with(prefix))
        {
            it = m_values.erase(it);
            continue;
        }

        ++it;
    }
}

bool config_store::has_key(const std::string_view key) const
{
    return m_values.contains(std::string{ key });
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
    return m_root_directory / m_config_subdirectory;
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
    bool rainbow = false;
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
            cursor = value.size();
            break;
        }

        cursor = separator + 1;
    }

    if (channel_index != channels.size())
    {
        return std::nullopt;
    }

    if (cursor < value.size())
    {
        const auto token = trim(std::string{ value.substr(cursor) });
        if (token == "true" || token == "1") rainbow = true;
        else if (token != "false" && token != "0") return std::nullopt;
    }

    return RGBA_float{ channels[0], channels[1], channels[2], channels[3], rainbow };
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
        reset_insider_settings_session(config);
        log_line("loaded default config");
        return;
    }

    store->import_config(config);
    if (store->save_file("default"))
    {
        log_line("created default config");
    }
}

}
