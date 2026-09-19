const fs = require('fs');
const path = require('path');

const settings_path = path.join(__dirname, '..', 'ch-settings.json');

function default_nonnegative_integer(value, fallback) {
    const text = String(value || '').trim();
    if (!/^[0-9]+$/.test(text))
        return fallback;

    const number = Number.parseInt(text, 10);
    return Number.isSafeInteger(number) ? number : fallback;
}

const default_config = {
    nodiscard: true,
    gdb_crash_reports: false,
    ban_tracker_enabled: false,
    steamwebhelper_cleanup: false,
    max_concurrent_bots: 12,
    steam_boot_concurrency: 12,
    steam_boot_delay_seconds: 3,
    game_start_timeout_seconds: 20,
    chunked_x_display: true,
    chunked_x_display_base: 699,
    chunked_x_display_bots_per_display: 25,
    chunked_x_display_max_displays: 12,
    chunked_x_display_max_clients: 512,
    per_bot_x_display: false,
    per_bot_x_display_base: 1000,
    auto_restart_steam_if_not_logged_within: default_nonnegative_integer(process.env.PUP_STEAM_TIMEOUT_SECONDS, 120)
};

function load_settings() {
    try {
        const settings = JSON.parse(fs.readFileSync(settings_path, 'utf8'));
        const result = {};
        for (const key of Object.keys(default_config)) {
            if (Object.prototype.hasOwnProperty.call(settings, key))
                result[key] = settings[key];
        }
        return result;
    } catch (error) {
        if (error.code !== 'ENOENT')
            console.log(`[WARNING] Failed to load ${settings_path}: ${error.message}`);
        return {};
    }
}

const config = Object.assign({}, default_config, load_settings());

function save_settings() {
    const settings = {};
    for (const key of Object.keys(default_config))
        settings[key] = config[key];

    const temporary_path = settings_path + '.tmp';
    fs.writeFileSync(temporary_path, JSON.stringify(settings, null, 4) + '\n');
    fs.renameSync(temporary_path, settings_path);
}

Object.defineProperty(config, 'save_settings', {
    value: save_settings,
    enumerable: false
});

Object.defineProperty(config, 'settings_path', {
    value: settings_path,
    enumerable: false
});

module.exports = config;
