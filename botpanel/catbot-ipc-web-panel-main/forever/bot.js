const EventEmitter = require('events');
const child_process = require('child_process');
const crypto = require('crypto');

const timestamp = require('time-stamp');
const fs = require('fs');
const os = require('os');
const path = require("path");
const { Tail } = require("tail");

const accounts = require('./acc.js');
const config = require('./config');
const steam_id = require('../steam_id');

function positive_integer(value, fallback) {
    const number = Number.parseInt(String(value || '').trim(), 10);
    return Number.isSafeInteger(number) && number > 0 ? number : fallback;
}

function display_number_from_text(value) {
    const match = String(value || '').match(/^:([0-9]+)$/);
    if (!match)
        return 0;

    const number = Number.parseInt(match[1], 10);
    return Number.isSafeInteger(number) && number > 0 ? number : 0;
}

const CATHOOK_ROOT = process.env.CATHOOK_ROOT || '/opt/cathook';
const DEFAULT_SHARED_TF2_PATH = process.env.CAT_TF2_PATH || '/opt/steamapps/common/Team Fortress 2';
const VISIBLE_WINDOWS = process.env.CAT_VISIBLE_WINDOWS === '1';
const BOT_DISPLAY = process.env.DISPLAY || process.env.CAT_DEFAULT_DISPLAY || ':699';
const BOT_XAUTHORITY = process.env.XAUTHORITY || path.join(process.env.HOME || '', '.Xauthority');
const XPRA_LOG = process.env.CAT_XPRA_LOG || '/tmp/cat-catbot-xpra.log';
const TEXTMODE_GAME = process.env.CAT_TEXTMODE_GAME !== '0';
const BOT_TF2_OVERLAY_ENABLED = process.env.CAT_BOT_TF2_OVERLAY !== '0';
const STEAM_TXTMODE_ENABLED = process.env.CAT_STEAM_TXTMODE === '1'
    || (process.env.CAT_STEAM_TXTMODE !== '0' && TEXTMODE_GAME);
const STEAM_TXTMODE_HIDE_WINDOWS = process.env.CAT_STEAM_TXTMODE_HIDE_WINDOWS === '0' ? '0' : '1';
const STEAM_TXTMODE_DROP_DRAWS = process.env.CAT_STEAM_TXTMODE_DROP_DRAWS === '0' ? '0' : '1';
const STEAM_TXTMODE_TRIM_WEBHELPER = process.env.CAT_STEAM_TXTMODE_TRIM_WEBHELPER === '1' ? '1' : '0';
const STEAM_TXTMODE_SYNTHETIC_HARDWARE = process.env.CAT_STEAM_TXTMODE_SYNTHETIC_HARDWARE === '0'
    ? '0' : (STEAM_TXTMODE_ENABLED ? '1' : '0');
const STEAM_TXTMODE_HARDWARE_RANDOMIZE = process.env.CAT_STEAM_TXTMODE_HARDWARE_RANDOMIZE === '1'
    || (process.env.CAT_STEAM_TXTMODE_HARDWARE_RANDOMIZE !== '0' && STEAM_TXTMODE_SYNTHETIC_HARDWARE === '1');
const STEAM_TXTMODE_HARDWARE_SEED = process.env.CAT_STEAM_TXTMODE_HARDWARE_SEED
    || (STEAM_TXTMODE_HARDWARE_RANDOMIZE
        ? `session-${crypto.randomBytes(16).toString('hex')}` : '');
const steam_txtmode_frame_interval_value = Number.parseInt(process.env.CAT_STEAM_TXTMODE_FRAME_INTERVAL_US || '100000', 10);
const STEAM_TXTMODE_FRAME_INTERVAL_US = Number.isSafeInteger(steam_txtmode_frame_interval_value)
    && steam_txtmode_frame_interval_value >= 0
    && steam_txtmode_frame_interval_value <= 10000000
    ? String(steam_txtmode_frame_interval_value)
    : '100000';
const STEAM_VGUI_TARGET_VERSION = process.env.CAT_STEAM_VGUI_TARGET_VERSION || '1689034492';
const STEAM_VGUI_REQUIRED = process.env.CAT_STEAM_VGUI === '1' || process.env.CAT_STEAM_VGUI !== '0';
const SKIP_DBUS_RUN_SESSION = process.env.CAT_SKIP_DBUS_RUN_SESSION === '1'
    || (process.env.CAT_SKIP_DBUS_RUN_SESSION !== '0' && TEXTMODE_GAME);
const GDB_CRASH_REPORTS = process.env.CAT_GDB_CRASH_REPORTS === '1'
    || (process.env.CAT_GDB_CRASH_REPORTS !== '0' && config.gdb_crash_reports === true);
const steam_window_options_default = VISIBLE_WINDOWS
    ? ''
    : '-silent -cef-disable-gpu -cef-disable-gpu-compositing -cef-force-occlusion'
      + ' -cef-disable-site-isolation -cef-disable-hang-timeouts -cef-disable-renderer-restart'
      + ' -cef-disable-breakpad -cef-disable-logging -cef-disable-js-logging -cef-disable-hevc'
      + ' -disablehighdpi -nominidumps -nobreakpad -skipstreamingdrivers';
const steam_window_options = process.env.CAT_STEAM_WINDOW_OPTIONS || steam_window_options_default;
const steam_shim_loop_sleep = process.env.CAT_STM_LOOP_SLEEP === '0' || process.env.CAT_STM_STEAM_LOOP_SLEEP === '0' ? '0' : '1';
const steam_shim_loop_sleep_us_value = Number.parseInt(process.env.CAT_STM_LOOP_SLEEP_US || process.env.CAT_STM_STEAM_LOOP_SLEEP_US || '100000', 10);
const steam_shim_loop_sleep_us = Number.isSafeInteger(steam_shim_loop_sleep_us_value) && steam_shim_loop_sleep_us_value > 0 ? steam_shim_loop_sleep_us_value : 100000;
const game_window_options_default = VISIBLE_WINDOWS
    ? '-gl -sw -w 1280 -h 720'
    : (TEXTMODE_GAME ? '-silent -sw -w 1 -h 480' : '-gl -silent -sw -w 1 -h 480');
const GAME_WINDOW_OPTIONS = process.env.CAT_GAME_WINDOW_OPTIONS || game_window_options_default;
const GAME_MODE_OPTIONS = TEXTMODE_GAME
    ? '-noshaderapi -nomouse -nosound'
    : '';
const textmode_allocator_assignments = TEXTMODE_GAME
    ? 'MIMALLOC_ARENA_EAGER_COMMIT=0 MIMALLOC_EAGER_COMMIT_DELAY=0 MIMALLOC_PURGE_DELAY=0 MIMALLOC_RESET_DELAY=0 MIMALLOC_ALLOW_LARGE_OS_PAGES=0'
    : '';
const SHARED_STEAM_ROOT = '/opt/catbot-shared-steam';
const SHARED_STEAMAPPS = '/opt/steamapps';
const STEAM_OVERLAY = process.env.CAT_STEAM_OVERLAY === '1' || process.env.CAT_STEAM_OVERLAY !== '0';
const STEAM_OVERLAY_ROOT = process.env.CAT_STEAM_OVERLAY_DIR || '/opt/cathook/steam-overlays';
const STEAM_OVERLAY_PRIVATE_DIRS = ['appcache', 'config', 'logs', 'steamapps', 'steamapps_old', 'userdata'];
const CATHOOK_ATTACH_DELAY_SECONDS = Number.parseInt(process.env.CATHOOK_ATTACH_DELAY_SECONDS || '0', 10);
const TF2_LAUNCH_MODE = (process.env.CAT_TF2_LAUNCH_MODE || 'direct').toLowerCase();
const PER_BOT_X_DISPLAY = process.env.CAT_PER_BOT_X_DISPLAY === '1' || config.per_bot_x_display === true;
const PER_BOT_X_DISPLAY_BASE_VALUE = Number.parseInt(process.env.CAT_PER_BOT_X_DISPLAY_BASE || String(config.per_bot_x_display_base || '1000'), 10);
const PER_BOT_X_DISPLAY_BASE = (Number.isSafeInteger(PER_BOT_X_DISPLAY_BASE_VALUE) && PER_BOT_X_DISPLAY_BASE_VALUE > 0) ? PER_BOT_X_DISPLAY_BASE_VALUE : 1000;
const PER_BOT_X_SCREEN = process.env.CAT_PER_BOT_X_SCREEN || process.env.CAT_X_SCREEN || '1x1x24';
const CHUNKED_X_DISPLAY_ENV = process.env.CAT_CHUNKED_X_DISPLAY;
const CHUNKED_X_DISPLAY = !PER_BOT_X_DISPLAY && !VISIBLE_WINDOWS && (CHUNKED_X_DISPLAY_ENV === '1' || (CHUNKED_X_DISPLAY_ENV !== '0' && config.chunked_x_display !== false));
const CHUNKED_X_DISPLAY_BASE = positive_integer(process.env.CAT_CHUNKED_X_DISPLAY_BASE || config.chunked_x_display_base, display_number_from_text(BOT_DISPLAY) || 699);
const CHUNKED_X_DISPLAY_BOTS_PER_DISPLAY = positive_integer(process.env.CAT_CHUNKED_X_DISPLAY_BOTS_PER_DISPLAY || config.chunked_x_display_bots_per_display, 25);
const CHUNKED_X_DISPLAY_MAX_DISPLAYS = positive_integer(process.env.CAT_CHUNKED_X_DISPLAY_MAX_DISPLAYS || config.chunked_x_display_max_displays, 12);
const CHUNKED_X_DISPLAY_MAX_CLIENTS = positive_integer(process.env.CAT_CHUNKED_X_DISPLAY_MAX_CLIENTS || process.env.CAT_XVFB_MAX_CLIENTS || config.chunked_x_display_max_clients, 512);
const CHUNKED_X_DISPLAY_PID_DIR = process.env.CAT_CHUNKED_X_DISPLAY_PID_DIR || '/tmp/cat-catbot-xvfb-pool';
const HEADLESS_STEAM_GRAPHICS = !VISIBLE_WINDOWS && process.env.CAT_STEAM_HEADLESS_GRAPHICS !== '0';
const HEADLESS_STEAM_GRAPHICS_FIREJAIL_ENV = HEADLESS_STEAM_GRAPHICS ? '--env=LIBGL_ALWAYS_SOFTWARE=1 --env=GALLIUM_DRIVER=llvmpipe --env=MESA_LOADER_DRIVER_OVERRIDE=llvmpipe --env=__GLX_VENDOR_LIBRARY_NAME=mesa --env=VK_ICD_FILENAMES=/dev/null --env=DISABLE_VK_LAYER_VALVE_steam_overlay_1=1 --env=DISABLE_VK_LAYER_VALVE_steam_fossilize_1=1' : '';
const HEADLESS_STEAM_GRAPHICS_ASSIGNMENTS = HEADLESS_STEAM_GRAPHICS ? 'LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe MESA_LOADER_DRIVER_OVERRIDE=llvmpipe __GLX_VENDOR_LIBRARY_NAME=mesa VK_ICD_FILENAMES=/dev/null DISABLE_VK_LAYER_VALVE_steam_overlay_1=1 DISABLE_VK_LAYER_VALVE_steam_fossilize_1=1' : '';
const STEAM_NOGRAPHICS_RENDERER_ENV = STEAM_TXTMODE_ENABLED
    ? '--env=LP_NUM_THREADS=1 --env=GALLIUM_NUM_THREADS=1 --env=vblank_mode=0'
    : '';
const game_port_base_value = Number.parseInt(process.env.CAT_GAME_PORT_BASE || String(config.game_port_base || '30000'), 10);
const game_port_base = (Number.isSafeInteger(game_port_base_value) && game_port_base_value >= 1024) ? game_port_base_value : 30000;
const game_port_stride_value = Number.parseInt(process.env.CAT_GAME_PORT_STRIDE || String(config.game_port_stride || '10'), 10);
const game_port_stride = (Number.isSafeInteger(game_port_stride_value) && game_port_stride_value >= 10) ? game_port_stride_value : 10;

function game_port_options(botid) {
    const bot_index = Number.isSafeInteger(botid) && botid >= 0 ? botid : 0;
    const bot_port_base = game_port_base + bot_index * game_port_stride;
    const tv_port = bot_port_base + 1;
    const client_port_min = bot_port_base + 2;
    const client_port_max = bot_port_base + game_port_stride - 1;

    if (bot_port_base < 1024 || client_port_max > 65535) {
        throw new Error(`Invalid TF2 port range for bot ${botid}: ${bot_port_base}-${client_port_max}`);
    }

    return `-tv_port ${tv_port} +tv_port ${tv_port} -port ${bot_port_base} +port ${bot_port_base} +clientport ${client_port_min}-${client_port_max}`;
}

const LAUNCH_OPTIONS_STEAM = `firejail --dns=1.1.1.1 %NETWORK% --noprofile --private="%HOME%" --private-tmp --private-dev --read-write=/opt/cathook/ipc --name=%JAILNAME% --env=PULSE_SERVER="unix:/tmp/pulse.sock" --env=DISPLAY=%DISPLAY% --env=XAUTHORITY=%XAUTHORITY% --env=TMPDIR=/tmp --env=TMP=/tmp --env=TEMP=/tmp --env=XDG_RUNTIME_DIR=/tmp/xdg-runtime --env=NO_AT_BRIDGE=1 --env=GTK_USE_PORTAL=0 --env=GIO_USE_VFS=local --env=CAT_SKIP_DBUS_RUN_SESSION=${SKIP_DBUS_RUN_SESSION ? '1' : '0'} --env=CAT_STEAM_TXTMODE=${STEAM_TXTMODE_ENABLED ? '1' : '0'} --env=CAT_STEAM_TXTMODE_HIDE_WINDOWS=${STEAM_TXTMODE_HIDE_WINDOWS} --env=CAT_STEAM_TXTMODE_DROP_DRAWS=${STEAM_TXTMODE_DROP_DRAWS} --env=CAT_STEAM_TXTMODE_TRIM_WEBHELPER=${STEAM_TXTMODE_TRIM_WEBHELPER} --env=CAT_STEAM_TXTMODE_FRAME_INTERVAL_US=${STEAM_TXTMODE_FRAME_INTERVAL_US} --env=CAT_STEAM_TXTMODE_HARDWARE_SEED=%HARDWARE_SEED% ${HEADLESS_STEAM_GRAPHICS_FIREJAIL_ENV} ${STEAM_NOGRAPHICS_RENDERER_ENV} --env=LD_LIBRARY_PATH=%STEAM_LD_LIBRARY_PATH% --env=LD_PRELOAD= --env=CAT_STEAM_TXTMODE_PRELOAD=%LD_PRELOAD% --env=CAT_STM_LOOP_SLEEP=%CAT_STM_LOOP_SLEEP% --env=CAT_STM_LOOP_SLEEP_US=%CAT_STM_LOOP_SLEEP_US% sh -lc 'mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"; if [ "$CAT_SKIP_DBUS_RUN_SESSION" = 1 ]; then exec env DBUS_SESSION_BUS_ADDRESS="unix:path=/tmp/cat-disabled-dbus" "$@"; elif command -v dbus-run-session >/dev/null 2>&1; then exec dbus-run-session -- "$@"; else exec "$@"; fi' steam-session %STEAM% %STEAM_VGUI_ARG% ${steam_window_options} -login %LOGIN% %PASSWORD%`
const LAUNCH_OPTIONS_STEAM_RESET = 'firejail --net=none --noprofile --private="%HOME%" --private-dev --read-write=/opt/cathook/ipc --env=LD_LIBRARY_PATH=%STEAM_LD_LIBRARY_PATH% %STEAM% --reset'
const LAUNCH_OPTIONS_STEAM_WITH_HARDWARE = LAUNCH_OPTIONS_STEAM.replace(
    '--env=CAT_STEAM_TXTMODE_FRAME_INTERVAL_US=',
    `--env=CAT_STEAM_TXTMODE_SYNTHETIC_HARDWARE=${STEAM_TXTMODE_SYNTHETIC_HARDWARE} --env=CAT_STEAM_TXTMODE_FRAME_INTERVAL_US=`,
);
const LAUNCH_OPTIONS_GAME = `firejail --join=%JAILNAME% bash -c 'cd "%GAMEPATH%" && %RUNTIME_PREFIX% ${HEADLESS_STEAM_GRAPHICS_ASSIGNMENTS} ${textmode_allocator_assignments} SteamAppId=440 SteamGameId=440 SteamOverlayGameId=440 SteamEnv=1 CATHOOK_ROOT="%CATHOOK_ROOT%" CATHOOK_ROOT_DIR="%CATHOOK_ROOT%" CATHOOK_AUTO_ATTACH=1 CATHOOK_ATTACH_DELAY_SECONDS=%CATHOOK_ATTACH_DELAY_SECONDS% CAT_BOT_ID="%BOT_ID%" CAT_BOT_NAME="%BOT_NAME%" CAT_STEAM_TXTMODE_HARDWARE_SEED="%HARDWARE_SEED%" CAT_STEAMID32=%STEAMID32% DBUS_SESSION_BUS_ADDRESS="unix:path=/tmp/cat-disabled-dbus" LD_PRELOAD=%LD_PRELOAD% DISPLAY=%DISPLAY% XAUTHORITY="%XAUTHORITY%" PULSE_SERVER="unix:/tmp/pulse.sock" %GAME_BINARY% -steam -game tf ${GAME_WINDOW_OPTIONS} -novid -nojoy -nomessagebox -nominidumps -nohltv -nobreakpad -noquicktime -precachefontchars -particles 1 -snoforceformat -softparticlesdefaultoff ${GAME_MODE_OPTIONS} -forcenovsync +volume 0 -noqueuedpacketprocessing -limitvsconst -nocrashdialog -noipx -threads 1 %GAME_PORT_OPTIONS% -nosteamcontroller -low +fps_max 30'`
const LAUNCH_OPTIONS_GAME_WITH_HARDWARE = LAUNCH_OPTIONS_GAME.replace(
    'CAT_BOT_ID="%BOT_ID%"',
    `CAT_BOT_ID="%BOT_ID%" CAT_STEAM_TXTMODE_SYNTHETIC_HARDWARE=${STEAM_TXTMODE_SYNTHETIC_HARDWARE}`,
);
const LAUNCH_OPTIONS_GAME_STEAM = `firejail --join=%JAILNAME% bash -c '${HEADLESS_STEAM_GRAPHICS_ASSIGNMENTS} ${textmode_allocator_assignments} DISPLAY=%DISPLAY% XAUTHORITY="%XAUTHORITY%" PULSE_SERVER="unix:/tmp/pulse.sock" %STEAM% -applaunch 440'`
const GAME_LIBRARY_PATH = './bin:./bin/linux64:./tf/bin:./tf/bin/linux64:./platform:./platform/bin:./platform/bin/linux64:.';

const STEAM_CLIENT_INITIALIZED_PATTERNS = [
    'Desktop state changed:',
    'Caching cursor image',
    'reaping pid:',
    'System startup time:'
];
const steam_client_initialized_game_delay_default_seconds = 0;
const STEAM_CLIENT_INITIALIZED_GAME_DELAY_SECONDS_VALUE = Number.parseInt(process.env.CAT_STEAM_CLIENT_INITIALIZED_GAME_DELAY_SECONDS || String(steam_client_initialized_game_delay_default_seconds), 10);
const steam_client_initialized_game_delay = (Number.isFinite(STEAM_CLIENT_INITIALIZED_GAME_DELAY_SECONDS_VALUE) ? Math.max(0, STEAM_CLIENT_INITIALIZED_GAME_DELAY_SECONDS_VALUE) : steam_client_initialized_game_delay_default_seconds) * 1000;

const TIMEOUT_START_GAME = positive_integer(
    process.env.CAT_GAME_START_TIMEOUT_SECONDS || config.game_start_timeout_seconds,
    20
) * 1000;

const TIMEOUT_IPC_STATE = Number.parseInt(process.env.CAT_IPC_TIMEOUT_SECONDS || '300', 10) * 1000;
const ipc_heartbeat_stale_timeout = Number.parseInt(process.env.CAT_IPC_STALE_SECONDS || '45', 10) * 1000;
const ipc_identity_timeout = Number.parseInt(process.env.CAT_IPC_IDENTITY_TIMEOUT_SECONDS || '60', 10) * 1000;
const runtime_kill_grace_time = Number.parseInt(process.env.CAT_RUNTIME_KILL_GRACE_SECONDS || '8', 10) * 1000;
const AUTO_RESTART_MAX_FAILURES = positive_integer(process.env.CAT_AUTO_RESTART_MAX_FAILURES, 3);
const AUTO_RESTART_WINDOW_MS = positive_integer(process.env.CAT_AUTO_RESTART_WINDOW_SECONDS, 600) * 1000;
const AUTO_RESTART_STABLE_MS = positive_integer(process.env.CAT_AUTO_RESTART_STABLE_SECONDS, 60) * 1000;
const AUTO_RESTART_DELAYS_MS = [10000, 30000, 90000];

const TIMEOUT_STEAM_ASSUME_READY = Number.parseInt(process.env.CAT_STEAM_READY_SECONDS || '0', 10) * 1000;
const steam_logged_in_game_delay_default_seconds = 0;
const STEAM_LOGGED_IN_GAME_DELAY_SECONDS_VALUE = Number.parseInt(process.env.CAT_STEAM_LOGGED_IN_GAME_DELAY_SECONDS || String(steam_logged_in_game_delay_default_seconds), 10);
const steam_logged_in_game_delay = (Number.isFinite(STEAM_LOGGED_IN_GAME_DELAY_SECONDS_VALUE) ? Math.max(0, STEAM_LOGGED_IN_GAME_DELAY_SECONDS_VALUE) : steam_logged_in_game_delay_default_seconds) * 1000;
const STEAMWEBHELPER_CLEANUP_ENABLED = process.env.CAT_STEAMWEBHELPER_CLEANUP === '1' || config.steamwebhelper_cleanup === true;
const STEAMWEBHELPER_CLEANUP_DELAY_SECONDS_VALUE = Number.parseInt(process.env.CAT_STEAMWEBHELPER_CLEANUP_SECONDS || '10', 10);
const STEAMWEBHELPER_CLEANUP_DELAY = (Number.isFinite(STEAMWEBHELPER_CLEANUP_DELAY_SECONDS_VALUE) ? Math.max(0, STEAMWEBHELPER_CLEANUP_DELAY_SECONDS_VALUE) : 10) * 1000;
const STEAM_LOGIN_UI_TIMEOUT_SECONDS_VALUE = Number.parseInt(process.env.CAT_STEAM_LOGIN_UI_TIMEOUT_SECONDS || '0', 10);
const STEAM_LOGIN_UI_TIMEOUT = (Number.isFinite(STEAM_LOGIN_UI_TIMEOUT_SECONDS_VALUE) ? Math.max(0, STEAM_LOGIN_UI_TIMEOUT_SECONDS_VALUE) : 0) * 1000;

const BOT_START_DELAY_SECONDS_VALUE = Number.parseInt(process.env.CAT_BOT_START_DELAY_SECONDS || '30', 10);
const DELAY_START_TIME = (Number.isFinite(BOT_START_DELAY_SECONDS_VALUE) ? Math.max(0, BOT_START_DELAY_SECONDS_VALUE) : 30) * 1000;
const STEAM_BOOT_DELAY_SECONDS_VALUE = Number.parseInt(process.env.CAT_STEAM_BOOT_DELAY_SECONDS || String(config.steam_boot_delay_seconds || '8'), 10);
const STEAM_BOOT_DELAY = (Number.isFinite(STEAM_BOOT_DELAY_SECONDS_VALUE) ? Math.max(0, STEAM_BOOT_DELAY_SECONDS_VALUE) : 8) * 1000;
const STEAM_STARTUP_HARD_TIMEOUT_SECONDS_VALUE = Number.parseInt(process.env.CAT_STEAM_STARTUP_HARD_TIMEOUT_SECONDS || '900', 10);
const STEAM_STARTUP_HARD_TIMEOUT = (Number.isFinite(STEAM_STARTUP_HARD_TIMEOUT_SECONDS_VALUE) ? Math.max(0, STEAM_STARTUP_HARD_TIMEOUT_SECONDS_VALUE) : 900) * 1000;
const GAME_STARTUP_FATAL_PATTERNS = [
    'AppFramework : Unable to load module engine.so!',
    'Unable to load interface VCvarQuery001 from engine.so',

    'version `GLIBC_',
    'error while loading shared libraries:',
    'cannot open shared object file'
];
const STEAM_STARTUP_FATAL_PATTERNS = [
    'Error: Couldn\'t set up the Steam Runtime.',
    'LD_LIBRARY_PATH: unbound variable'
];
const x_client_cap_patterns = [
    /Maximum number of clients reached/i
];
const steam_auth_log_scan_tail_lines = 400;

const steam_session_log_clear_names = [
    'bootstrap_log.txt',
    'cef_log.txt',
    'connection_log.txt',
    'console_log.txt',
    'content_log.txt',
    'steamui_login.txt',
    'steamui_system.txt',
    'steamui_html.txt',
    'webhelper.txt',
    'webhelper_js.txt',
    'systemdisplaymanager.txt',
    'transport_steamui.txt',
    'configstore_log.txt',
    'cloud_log.txt',
    'workshop_log.txt',
    'timedtrial_log.txt',
    'remote_connections.txt',
    'gameprocess_log.txt',
    'shader_log.txt'
];
const steam_invalid_password_patterns = [
    /LogonFailure Invalid Password/i,
    /ConnectionDisconnected\(.*Invalid Password/i,
    /SteamServerConnectFailure_t Invalid Password/i,
    /SetLoginState:.*Invalid Password/i
];
const steam_login_error_5_patterns = [
    /login error\s*:?\s*5\b/i,
    /login error\s*:?\s*e5\b/i
];
const steam_account_disabled_e43_patterns = [
    /\be43\b/i,
    /\b(?:e|eresult|result|error|code|status)\s*[:=]?\s*43\b/i,
    /\b43\b[^\n]{0,80}(?:disabled|account|login|logon|auth)/i,
    /(?:login|logon|auth|account|error|eresult|result)[^\n]{0,80}\be43\b/i,
    /\be43\b[^\n]{0,80}(?:login|logon|auth|account|error|disabled)/i
];
let navmesh_sync_done = false;
let cathook_configs_ready = false;

const STATE = {
    INITIALIZING: 0,
    INITIALIZED: 1,
    PREPARING: 2,
    STARTING: 3,
    WAITING: 4,
    RUNNING: 5,
    RESTARTING: 6,
    STOPPING: 7,
    NO_ACCOUNT: 8,
    INVALID_PASSWORD_E5: 9,
    ACCOUNT_DISABLED_E43: 10,
    PENDING: 11,
    RESTART_PAUSED: 12,
}

function makeid(length) {
    var result = '';
    var characters = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
    var charactersLength = characters.length;
    for (var i = 0; i < length; i++) {
        result += characters.charAt(Math.floor(Math.random() * charactersLength));
    }
    return result;
}

function clearSourceLockFiles() {
    var files = fs.readdirSync('/tmp/');
    files.forEach((str, index, arr) => {
        if (str.startsWith("source_engine") && str.endsWith(".lock"))
            fs.unlink(`/tmp/${str}`, (err) => {
                if (err)
                    console.log("[ERROR] Failed to delete a source engine lock!");
            });
    });
}

function shell_quote(value) {
    return "'" + String(value).replace(/'/g, "'\\''") + "'";
}

function log_file_tail(file_path, line_count) {
    try {
        const stat = fs.statSync(file_path);
        if (!stat.isFile())
            return '';

        const tail_size = Math.min(stat.size, 65536);
        const fd = fs.openSync(file_path, 'r');
        const buffer = Buffer.allocUnsafe(tail_size);
        try {
            fs.readSync(fd, buffer, 0, tail_size, stat.size - tail_size);
        } finally {
            fs.closeSync(fd);
        }
        const lines = buffer.toString('utf8').trimEnd().split(/\r?\n/);
        return lines.slice(-line_count).join('\n');
    } catch (error) {
        return `failed to read ${file_path}: ${error.message}`;
    }
}

function read_file_tail(file_path, max_bytes) {
    try {
        const stat = fs.statSync(file_path);
        if (!stat.isFile())
            return '';

        const tail_size = Math.min(stat.size, max_bytes);
        const fd = fs.openSync(file_path, 'r');
        const buffer = Buffer.allocUnsafe(tail_size);
        try {
            fs.readSync(fd, buffer, 0, tail_size, stat.size - tail_size);
        } finally {
            fs.closeSync(fd);
        }
        return buffer.toString('utf8');
    } catch (error) {
        return '';
    }
}

function bash_double_quote_escape(value) {
    return String(value).replace(/(["\\$`])/g, '\\$1');
}

function tf2_install_ready(tf2_path) {
    return fs.existsSync(path.join(tf2_path, 'tf_linux64'));
}

function steamapps_tf2_ready(steamapps_path) {
    return steamapps_path && tf2_install_ready(path.join(steamapps_path, 'common/Team Fortress 2'));
}

function steam_root_ready(steam_path) {
    return steam_path &&
        fs.existsSync(path.join(steam_path, 'steam.sh')) &&
        fs.existsSync(path.join(steam_path, 'ubuntu12_32/steam')) &&
        fs.existsSync(path.join(steam_path, 'ubuntu12_32/steam-runtime/run.sh'));
}

function steam_installed_version(steam_path) {
    if (!steam_path)
        return '';

    const package_dir = path.join(steam_path, 'package');
    try {
        const package_files = fs.readdirSync(package_dir)
            .filter((name) => name.startsWith('steam_client_ubuntu12'))
            .sort();
        for (const package_file of package_files) {
            const text = fs.readFileSync(path.join(package_dir, package_file), 'utf8');
            const match = text.match(/"version"\s+"([^"]+)"/);
            if (match)
                return match[1];
        }
    } catch (error) { }

    return '';
}

function steam_client_initialized_from_log(text) {
    return STEAM_CLIENT_INITIALIZED_PATTERNS.some((pattern) => text.includes(pattern));
}

function escape_regex(text) {
    return String(text).replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function game_startup_log_has_fatal_error(text) {
    return GAME_STARTUP_FATAL_PATTERNS.some((pattern) => text.includes(pattern));
}

function steam_startup_log_has_fatal_error(text) {
    return STEAM_STARTUP_FATAL_PATTERNS.some((pattern) => text.includes(pattern));
}

function steam_log_has_x_client_cap_error(text) {
    return x_client_cap_patterns.some((pattern) => pattern.test(text));
}

function steam_log_has_invalid_password(text) {
    return steam_invalid_password_patterns.some((pattern) => pattern.test(text));
}

function steam_log_has_login_error_5(text) {
    return steam_login_error_5_patterns.some((pattern) => pattern.test(text));
}

function steam_log_has_account_disabled_e43(text) {
    return steam_account_disabled_e43_patterns.some((pattern) => pattern.test(text));
}

function steam_webhelper_browser_stalled(text) {
    if (text.includes('Timed out waiting for webhelper init'))
        return true;

    if (text.includes('LockMgrMutex failed') || text.includes('pthreads futex robust_list is corrupt'))
        return true;

    if (!text.includes('CreateResponse') && !text.includes('BrowserReady')) {
        const restart_count = (text.match(/Restart webhelper process/g) || []).length;
        if (restart_count >= 2)
            return true;
    }

    const create_browser_position = text.lastIndexOf('CreateBrowser');
    if (create_browser_position < 0)
        return false;

    const latest_browser_text = text.slice(create_browser_position);
    if (latest_browser_text.includes('CreateResponse') || latest_browser_text.includes('BrowserReady'))
        return false;

    const retry_count = (latest_browser_text.match(/RetryCreateBrowser/g) || []).length;
    return retry_count >= 20;
}

function command_succeeds(command, args) {
    if (command === 'which' && args && args.length)
        return command_in_path(args[0]);
    if (command === 'mountpoint' && args && args[0] === '-q' && args[1])
        return is_mountpoint(args[1]);

    try {
        child_process.execFileSync(command, args, { stdio: 'ignore' });
        return true;
    } catch (error) {
        return false;
    }
}

function command_in_path(command) {
    const path_value = process.env.PATH || '';
    for (const directory of path_value.split(':')) {
        if (!directory)
            continue;
        try {
            fs.accessSync(path.join(directory, command), fs.constants.X_OK);
            return true;
        } catch (error) { }
    }
    return false;
}

function is_mountpoint(target_path) {
    try {
        const real_path = fs.realpathSync(target_path);
        const mountinfo = fs.readFileSync('/proc/self/mountinfo', 'utf8');
        for (const line of mountinfo.split('\n')) {
            const fields = line.split(' ');
            if (fields.length > 4 && fields[4] === real_path)
                return true;
        }
    } catch (error) { }
    return false;
}

let xvfb_available_cached = null;
function xvfb_available() {
    if (xvfb_available_cached === null)
        xvfb_available_cached = command_succeeds('which', ['Xvfb']);
    return xvfb_available_cached;
}

function read_x_lock_pid(display_num) {
    try {
        const text = fs.readFileSync(`/tmp/.X${display_num}-lock`, 'utf8').trim();
        const pid = Number.parseInt(text, 10);
        return Number.isFinite(pid) && pid > 0 ? pid : 0;
    } catch (error) {
        return 0;
    }
}

function pid_alive(pid) {
    if (!pid)
        return false;
    try {
        process.kill(pid, 0);
        return true;
    } catch (error) {
        return error.code === 'EPERM';
    }
}

const x_display_pool = new Map();

function x_display_pid_path(display_num) {
    return path.join(CHUNKED_X_DISPLAY_PID_DIR, `X${display_num}.pid`);
}

function x_socket_path(display_num) {
    return `/tmp/.X11-unix/X${display_num}`;
}

function wait_for_x_socket(display_num, timeout_ms) {
    return fs.existsSync(x_socket_path(display_num));
}

function chunked_x_display_entry(display_num) {
    let entry = x_display_pool.get(display_num);
    if (entry)
        return entry;

    entry = {
        display_num: display_num,
        display: `:${display_num}`,
        proc: null,
        adopted: false,
        unavailable: false,
        users: new Set(),
        log_stream: null
    };
    x_display_pool.set(display_num, entry);
    return entry;
}

function ensure_chunked_x_display_entry(entry, bot) {
    if (entry.unavailable)
        return false;

    if (entry.proc && pid_alive(entry.proc.pid))
        return true;

    const existing_pid = read_x_lock_pid(entry.display_num);
    if (existing_pid && pid_alive(existing_pid)) {
        entry.adopted = true;
        return true;
    }
    if (existing_pid) {
        try { fs.unlinkSync(`/tmp/.X${entry.display_num}-lock`); } catch (error) { }
    }

    try {
        fs.mkdirSync('./logs', { recursive: true });
        fs.mkdirSync(CHUNKED_X_DISPLAY_PID_DIR, { recursive: true });
        const xvfb_log_path = `./logs/chunked-xvfb-${entry.display_num}.log`;
        entry.log_stream = fs.createWriteStream(xvfb_log_path, { flags: 'a' });
        entry.proc = child_process.spawn('Xvfb', [
            entry.display,
            '-screen', '0', PER_BOT_X_SCREEN,
            '-maxclients', String(CHUNKED_X_DISPLAY_MAX_CLIENTS),
            '-nolisten', 'tcp',
            '-ac',
            '-noreset'
        ], {
            uid: USER.uid,
            env: { PATH: process.env.PATH, HOME: USER.home },
            stdio: ['ignore', 'pipe', 'pipe'],
            detached: false
        });
        fs.writeFileSync(x_display_pid_path(entry.display_num), String(entry.proc.pid) + '\n');
        entry.proc.stdout.pipe(entry.log_stream);
        entry.proc.stderr.pipe(entry.log_stream);
        entry.proc.on('error', (error) => {
            bot.log(`[ERROR] Xvfb ${entry.display} error: ${error.message}`);
        });
        entry.proc.on('exit', (code, signal) => {
            bot.log(`Xvfb ${entry.display} exited code=${code} signal=${signal}`);
            entry.proc = null;
            entry.adopted = false;
            if (entry.log_stream) {
                try { entry.log_stream.end(); } catch (error) { }
                entry.log_stream = null;
            }
            try { fs.unlinkSync(x_display_pid_path(entry.display_num)); } catch (error) { }
        });
    } catch (error) {
        bot.log(`[ERROR] Failed to spawn Xvfb on ${entry.display}: ${error.message}`);
        entry.proc = null;
        return false;
    }

    if (!wait_for_x_socket(entry.display_num, 3000))
        bot.log(`[WARN] X socket ${x_socket_path(entry.display_num)} did not appear within 3s; Steam may fail to connect.`);

    bot.log(`Spawned chunked Xvfb on ${entry.display} pid=${entry.proc.pid} bots_per_display=${CHUNKED_X_DISPLAY_BOTS_PER_DISPLAY}`);
    return true;
}

function stop_chunked_x_display_entry(entry) {
    if (!entry || !entry.proc)
        return;

    const pid = entry.proc.pid;
    try { entry.proc.kill('SIGTERM'); } catch (error) { }
    setTimeout(() => {
        try { process.kill(pid, 0); } catch (error) { return; }
        try { process.kill(pid, 'SIGKILL'); } catch (error) { }
    }, 2000);
    entry.proc = null;
    entry.adopted = false;
    try { fs.unlinkSync(x_display_pid_path(entry.display_num)); } catch (error) { }
}

function preload_value(primary_library) {
    const extra_preload = (process.env.STEAM_LD_PRELOAD || '')
        .split(':')
        .filter((entry) => entry && path.basename(entry) !== 'libcatsteamtxtmode.so');
    return [primary_library, ...extra_preload].filter(Boolean).join(':');
}

function steam_txtmode_library() {
    const library_name = 'libcatsteamtxtmode.so';
    const candidates = [
        process.env.CAT_STEAM_TXTMODE_LIBRARY,
        path.join(CATHOOK_ROOT, 'botpanel/cat-steamtxtmode/bin/lib64/libcatsteamtxtmode.so'),
        path.join(CATHOOK_ROOT, 'lib64/libcatsteamtxtmode.so'),
        '/usr/local/lib/libcatsteamtxtmode.so',
        '/usr/lib/libcatsteamtxtmode.so',
        '/usr/lib64/libcatsteamtxtmode.so',
        '/usr/lib/x86_64-linux-gnu/libcatsteamtxtmode.so',
        library_name
    ];
    for (const candidate of candidates) {
        if (candidate && (candidate === library_name || fs.existsSync(candidate)))
            return candidate;
    }
    return library_name;
}

function steam_txtmode_library64() {
    const library_name = 'libcatsteamtxtmode.so';
    const candidates = [
        process.env.CAT_STEAM_TXTMODE_LIBRARY64,
        path.join(CATHOOK_ROOT, 'botpanel/cat-steamtxtmode/bin/libx64/libcatsteamtxtmode.so'),
        '/usr/local/lib/libcatsteamtxtmode.so',
        '/usr/lib/x86_64-linux-gnu/libcatsteamtxtmode.so',
        '/usr/lib64/libcatsteamtxtmode.so',
        library_name
    ];
    for (const candidate of candidates) {
        if (candidate && (candidate === library_name || fs.existsSync(candidate)))
            return candidate;
    }
    return library_name;
}

function steam_preload_value() {
    const library_name = steam_txtmode_library();
    const library_basenames = new Set([path.basename(library_name)]);
    const extra_preload = (process.env.STEAM_LD_PRELOAD || '')
        .split(':')
        .filter((entry) => entry && !library_basenames.has(path.basename(entry)));
    if (!STEAM_TXTMODE_ENABLED)
        return extra_preload.join(':');

    return [library_name, ...extra_preload].join(':');
}

function cathook_textmode_library() {
    const candidates = [
        path.join(CATHOOK_ROOT, 'bin/libcathooktextmode.so'),
        '/opt/cathook/bin/libcathooktextmode.so',
        path.join(CATHOOK_ROOT, 'bin/libcathook-textmode.so'),
        '/opt/cathook/bin/libcathook-textmode.so'
    ];

    for (const candidate of unique_paths(candidates)) {
        if (fs.existsSync(candidate))
            return candidate;
    }

    return candidates[0];
}

function cathook_graphical_library() {
    const candidates = [
        path.join(CATHOOK_ROOT, 'bin/libcathook.so'),
        '/opt/cathook/bin/libcathook.so'
    ];

    for (const candidate of unique_paths(candidates)) {
        if (fs.existsSync(candidate))
            return candidate;
    }

    return candidates[0];
}

function cathook_game_library() {
    return TEXTMODE_GAME ? cathook_textmode_library() : cathook_graphical_library();
}

function path_is_inside(child_path, parent_path) {
    const relative_path = path.relative(parent_path, child_path);
    return Boolean(relative_path) && !relative_path.startsWith('..') && !path.isAbsolute(relative_path);
}

function unique_paths(paths) {
    return [...new Set(paths.filter(Boolean))];
}

const tf2_videoconfig_linux_template = [
    '"videoconfig"',
    '{',
    '\t"setting.cpu_level"\t\t"0"',
    '\t"setting.gpu_level"\t\t"0"',
    '\t"setting.mat_antialias"\t\t"0"',
    '\t"setting.mat_aaquality"\t\t"0"',
    '\t"setting.mat_forceaniso"\t\t"0"',
    '\t"setting.mat_vsync"\t\t"0"',
    '\t"setting.mat_triplebuffered"\t\t"0"',
    '\t"setting.mat_grain_scale_override"\t\t"-1.000000"',
    '\t"setting.gpu_mem_level"\t\t"0"',
    '\t"setting.mem_level"\t\t"0"',
    '\t"setting.mat_queue_mode"\t\t"0"',
    '\t"setting.csm_quality_level"\t\t"0"',
    '\t"setting.mat_software_aa_strength"\t\t"0"',
    '\t"setting.mat_motion_blur_enabled"\t\t"0"',
    '\t"setting.fullscreen"\t\t"0"',
    '\t"setting.defaultres"\t\t"1"',
    '\t"setting.defaultresheight"\t\t"480"',
    '\t"setting.aspectratiomode"\t\t"0"',
    '\t"setting.nowindowborder"\t\t"1"',
    '\t"mat_hdr_level"\t\t"0"',
    '\t"mat_colorcorrection"\t\t"0"',
    '\t"VendorID"\t\t"0"',
    '\t"DeviceID"\t\t"0"',
    '\t"DXLevel_V1"\t\t"90"',
    '\t"AutoConfigVersion"\t\t"1"',
    '\t"ScreenDisplayIndex"\t\t"0"',
    '\t"ScreenWidth"\t\t"1"',
    '\t"ScreenHeight"\t\t"480"',
    '\t"ScreenWindowed"\t\t"1"',
    '\t"ScreenNoBorder"\t\t"1"',
    '\t"ScreenMSAA"\t\t"0"',
    '\t"ScreenMSAAQuality"\t\t"0"',
    '\t"MotionBlur"\t\t"0"',
    '\t"ShadowDepthTexture"\t\t"0"',
    '\t"VRModeAdapter"\t\t"-1"',
    '\t"ScreenMonitorGamma"\t\t"2.200000"',
    '\t"mat_forceaniso"\t\t"0"',
    '\t"mat_picmip"\t\t"2"',
    '\t"mat_trilinear"\t\t"0"',
    '\t"mat_vsync"\t\t"0"',
    '\t"mat_forcehardwaresync"\t\t"0"',
    '\t"mat_parallaxmap"\t\t"0"',
    '\t"mat_reducefillrate"\t\t"1"',
    '\t"r_lightmap_bicubic"\t\t"0"',
    '\t"r_shadowrendertotexture"\t\t"0"',
    '\t"r_rootlod"\t\t"2"',
    '\t"r_waterforceexpensive"\t\t"0"',
    '\t"r_waterforcereflectentities"\t\t"0"',
    '\t"mat_antialias"\t\t"0"',
    '\t"mat_aaquality"\t\t"0"',
    '\t"mat_specular"\t\t"0"',
    '\t"mat_bumpmap"\t\t"0"',
    '}',
    ''
].join('\n');

function shared_tf2_videoconfig_path(tf2_path) {
    return path.join(tf2_path || DEFAULT_SHARED_TF2_PATH, 'tf', 'videoconfig_linux.cfg');
}

function bot_videoconfig_linux_path(bot_home) {
    return path.join(bot_home, 'videoconfig_linux.cfg');
}

function videoconfig_linux_is_valid(text) {
    return /^\s*"videoconfig"\s*\{/.test(text) && !text.includes('}\nsetting.');
}

function bot_tf2_overlay_path(bot_home) {
    return path.join(bot_home, 'cat_tf2');
}

function ensure_symlink(link_path, target_path) {
    const resolved_target = path.resolve(target_path);
    try {
        if (fs.existsSync(link_path)) {
            const status = fs.lstatSync(link_path);
            if (status.isSymbolicLink()) {
                const current_target = fs.readlinkSync(link_path);
                if (path.resolve(path.dirname(link_path), current_target) === resolved_target)
                    return true;
                fs.unlinkSync(link_path);
            } else {
                return true;
            }
        }

        fs.symlinkSync(resolved_target, link_path);
        return true;
    } catch (error) {
        return false;
    }
}

function ensure_videoconfig_linux_at(videoconfig_path, log_fn) {
    if (!videoconfig_path)
        return false;

    let needs_write = false;
    try {
        if (!fs.existsSync(videoconfig_path)) {
            needs_write = true;
        } else {
            const stat = fs.statSync(videoconfig_path);
            if (stat.isSymbolicLink() || stat.size === 0) {
                needs_write = true;
            } else {
                needs_write = !videoconfig_linux_is_valid(fs.readFileSync(videoconfig_path, 'utf8'));
            }
        }
    } catch (error) {
        if (log_fn)
            log_fn(`[ERROR] Failed to inspect videoconfig_linux.cfg: ${error.message}`);
        return false;
    }

    if (!needs_write)
        return true;

    try {
        fs.mkdirSync(path.dirname(videoconfig_path), { recursive: true });
        if (fs.existsSync(videoconfig_path)) {
            const status = fs.lstatSync(videoconfig_path);
            if (status.isSymbolicLink())
                fs.unlinkSync(videoconfig_path);
        }
        const temporary_path = `${videoconfig_path}.tmp`;
        fs.writeFileSync(temporary_path, tf2_videoconfig_linux_template, { mode: 0o644 });
        fs.renameSync(temporary_path, videoconfig_path);
        fs.chownSync(videoconfig_path, USER.uid, USER.uid);
        if (log_fn)
            log_fn(`Prepared videoconfig_linux.cfg at ${videoconfig_path}`);
        return true;
    } catch (error) {
        if (log_fn)
            log_fn(`[ERROR] Failed to write videoconfig_linux.cfg: ${error.message}`);
        return false;
    }
}

function ensure_bot_videoconfig_linux(bot_home, log_fn) {
    if (!bot_home)
        return false;

    return ensure_videoconfig_linux_at(bot_videoconfig_linux_path(bot_home), log_fn);
}

function ensure_bot_tf2_overlay(bot_home, shared_tf2_path, log_fn) {
    if (!bot_home || !shared_tf2_path || !tf2_install_ready(shared_tf2_path))
        return null;

    const overlay_path = bot_tf2_overlay_path(bot_home);
    if (tf2_install_ready(overlay_path)) {
        ensure_videoconfig_linux_at(path.join(overlay_path, 'tf', 'videoconfig_linux.cfg'), log_fn);
        return overlay_path;
    }

    const overlay_tf_path = path.join(overlay_path, 'tf');
    const shared_tf_path = path.join(shared_tf2_path, 'tf');
    const overlay_existed = false;

    try {
        fs.mkdirSync(overlay_path, { recursive: true });
        for (const entry_name of fs.readdirSync(shared_tf2_path)) {
            if (entry_name === 'cat_tf2')
                continue;

            const shared_entry_path = path.join(shared_tf2_path, entry_name);
            const overlay_entry_path = path.join(overlay_path, entry_name);
            if (entry_name === 'tf') {
                fs.mkdirSync(overlay_tf_path, { recursive: true });
                for (const tf_entry_name of fs.readdirSync(shared_tf_path)) {
                    if (tf_entry_name === 'videoconfig_linux.cfg')
                        continue;

                    const shared_tf_entry_path = path.join(shared_tf_path, tf_entry_name);
                    const overlay_tf_entry_path = path.join(overlay_tf_path, tf_entry_name);
                    ensure_symlink(overlay_tf_entry_path, shared_tf_entry_path);
                }
                continue;
            }

            ensure_symlink(overlay_entry_path, shared_entry_path);
        }
    } catch (error) {
        if (log_fn)
            log_fn(`[ERROR] Failed to prepare bot TF2 overlay at ${overlay_path}: ${error.message}`);
        return null;
    }

    if (!ensure_videoconfig_linux_at(path.join(overlay_tf_path, 'videoconfig_linux.cfg'), log_fn))
        return null;

    if (!overlay_existed && tf2_install_ready(overlay_path) && log_fn)
        log_fn(`Bot TF2 overlay ready at ${overlay_path} (shared install=${shared_tf2_path})`);

    return tf2_install_ready(overlay_path) ? overlay_path : null;
}

function ensure_shared_tf2_videoconfig(tf2_path, log_fn) {
    if (!tf2_path)
        return false;

    const videoconfig_path = shared_tf2_videoconfig_path(tf2_path);
    let needs_write = false;
    try {
        if (!fs.existsSync(videoconfig_path)) {
            needs_write = true;
        } else {
            const stat = fs.statSync(videoconfig_path);
            if (stat.size === 0) {
                needs_write = true;
            } else {
                needs_write = !videoconfig_linux_is_valid(fs.readFileSync(videoconfig_path, 'utf8'));
            }
        }
    } catch (error) {
        if (log_fn)
            log_fn(`[ERROR] Failed to inspect shared videoconfig_linux.cfg: ${error.message}`);
        return false;
    }

    if (!needs_write)
        return true;

    try {
        fs.mkdirSync(path.dirname(videoconfig_path), { recursive: true });
        try {
            fs.chmodSync(videoconfig_path, 0o644);
        } catch (error) {
        }
        const temporary_path = `${videoconfig_path}.tmp`;
        fs.writeFileSync(temporary_path, tf2_videoconfig_linux_template, { mode: 0o644 });
        fs.renameSync(temporary_path, videoconfig_path);
        if (log_fn)
            log_fn(`[WARN] Repaired shared videoconfig_linux.cfg at ${videoconfig_path}`);
        return true;
    } catch (error) {
        if (log_fn)
            log_fn(`[ERROR] Failed to write shared videoconfig_linux.cfg: ${error.message}`);
        return false;
    }
}

function close_steam_log_stream(bot) {
    if (!bot.logSteam)
        return;

    const steam_process = bot.procFirejailSteam;
    if (steam_process) {
        if (steam_process.stdout)
            steam_process.stdout.unpipe(bot.logSteam);
        if (steam_process.stderr)
            steam_process.stderr.unpipe(bot.logSteam);
    }

    bot.logSteam.end();
    bot.logSteam = null;
}

function vdf_escape(value) {
    return String(value).replace(/\\/g, '\\\\').replace(/"/g, '\\"');
}

function set_vdf_path(root, keys, value) {
    let object = root;
    for (let index = 0; index < keys.length - 1; ++index) {
        const key = keys[index];
        if (!object[key] || typeof object[key] !== 'object')
            object[key] = {};
        object = object[key];
    }
    object[keys[keys.length - 1]] = value;
}

function parse_simple_vdf(text) {
    const tokens = [];
    const pattern = /"((?:\\.|[^"\\])*)"|[{}]/g;
    let match = null;
    while ((match = pattern.exec(text)) !== null)
        tokens.push(match[0][0] === '"' ? match[1].replace(/\\"/g, '"').replace(/\\\\/g, '\\') : match[0]);

    let index = 0;
    function parse_object() {
        const object = {};
        while (index < tokens.length && tokens[index] !== '}') {
            const key = tokens[index++];
            if (tokens[index] === '{') {
                ++index;
                object[key] = parse_object();
                if (tokens[index] === '}')
                    ++index;
            } else {
                object[key] = tokens[index++] || '';
            }
        }
        return object;
    }

    return parse_object();
}

function write_simple_vdf_value(lines, key, value, depth) {
    const indent = '\t'.repeat(depth);
    if (value && typeof value === 'object') {
        lines.push(`${indent}"${vdf_escape(key)}"`);
        lines.push(`${indent}{`);
        for (const entry_key of Object.keys(value))
            write_simple_vdf_value(lines, entry_key, value[entry_key], depth + 1);
        lines.push(`${indent}}`);
    } else {
        lines.push(`${indent}"${vdf_escape(key)}"\t\t"${vdf_escape(value)}"`);
    }
}

function stringify_simple_vdf(root) {
    const lines = [];
    for (const key of Object.keys(root))
        write_simple_vdf_value(lines, key, root[key], 0);
    return lines.join('\n') + '\n';
}

function read_proc_stat(pid) {
    try {
        const stat = fs.readFileSync(`/proc/${pid}/stat`, 'utf8');
        const end_comm = stat.lastIndexOf(')');
        if (end_comm < 0)
            return null;

        const comm = stat.slice(stat.indexOf('(') + 1, end_comm);
        const fields = stat.slice(end_comm + 2).trim().split(/\s+/);
        const info = {
            pid: pid,
            comm: comm,
            ppid: Number.parseInt(fields[1], 10),
            starttime: Number.parseInt(fields[19], 10) || 0
        };
        let cmdline = null;
        let nspids = null;
        Object.defineProperty(info, 'cmdline', {
            enumerable: true,
            get: function() {
                if (cmdline === null)
                    cmdline = read_proc_cmdline(pid);
                return cmdline;
            }
        });
        Object.defineProperty(info, 'nspids', {
            enumerable: true,
            get: function() {
                if (nspids === null)
                    nspids = read_proc_nspids(pid);
                return nspids;
            }
        });
        return info;
    } catch (error) {
        return null;
    }
}

function max_concurrent_bots() {
    const value = Number.parseInt(config.max_concurrent_bots, 10);
    if (!Number.isSafeInteger(value) || value < 1)
        return 1;

    return value;
}

function max_steam_boots() {
    const value = Number.parseInt(process.env.CAT_STEAM_BOOT_CONCURRENCY || String(config.steam_boot_concurrency || '1'), 10);
    if (!Number.isSafeInteger(value) || value < 1)
        return 1;

    return value;
}

function start_delay_allows_launch(time) {
    if (!DELAY_START_TIME)
        return true;

    return module.exports.lastStartTime + DELAY_START_TIME < time;
}

function steam_boot_delay_allows_launch(time) {
    if (!STEAM_BOOT_DELAY)
        return true;

    return module.exports.lastSteamBootTime + STEAM_BOOT_DELAY < time;
}

function steam_login_timeout_seconds() {
    const value = Number.parseInt(config.auto_restart_steam_if_not_logged_within, 10);
    if (!Number.isSafeInteger(value) || value < 0)
        return 70;

    return value;
}

function steam_login_timeout() {
    return steam_login_timeout_seconds() * 1000;
}

function steam_login_timeout_description() {
    const seconds = steam_login_timeout_seconds();
    return seconds > 0 ? `${seconds} seconds` : 'login timeout disabled';
}

function read_proc_cmdline(pid) {
    try {
        return fs.readFileSync(`/proc/${pid}/cmdline`, 'utf8').replace(/\0/g, ' ').trim();
    } catch (error) {
        return '';
    }
}

function read_proc_nspids(pid) {
    try {
        const status = fs.readFileSync(`/proc/${pid}/status`, 'utf8');
        const line = status.split('\n').find((entry) => entry.startsWith('NSpid:'));
        if (!line)
            return [pid];

        const pids = line.slice(6).trim().split(/\s+/)
            .map((value) => Number.parseInt(value, 10))
            .filter((value) => Number.isSafeInteger(value) && value > 0);
        return pids.length ? pids : [pid];
    } catch (error) {
        return [pid];
    }
}

const PROCESS_TABLE_CACHE_MS = Number.parseInt(process.env.CAT_PROCESS_TABLE_CACHE_MS || '1500', 10);
let process_table_cache_ms = PROCESS_TABLE_CACHE_MS;
let cached_process_table = null;
let cached_process_table_time = 0;
let cached_children_by_parent = null;

function invalidate_process_table_cache() {
    cached_process_table = null;
    cached_process_table_time = 0;
    cached_children_by_parent = null;
}

function set_process_table_cache_ms(value) {
    if (Number.isFinite(value) && value > 0)
        process_table_cache_ms = value;
}

function read_process_table(force_refresh) {
    const now = Date.now();
    if (!force_refresh && cached_process_table && (now - cached_process_table_time) < process_table_cache_ms)
        return cached_process_table;

    const processes = new Map();
    try {
        for (const entry of fs.readdirSync('/proc')) {
            if (!/^\d+$/.test(entry))
                continue;

            const pid = Number.parseInt(entry, 10);
            const info = read_proc_stat(pid);
            if (info)
                processes.set(pid, info);
        }
    } catch (error) { }

    cached_process_table = processes;
    cached_process_table_time = now;
    cached_children_by_parent = null;
    return processes;
}

function process_table_or_current(processes) {
    return processes || read_process_table();
}

async function refresh_process_table() {
    const now = Date.now();
    if (process_table_cache && now - process_table_cache_time < process_table_cache_ms)
        return process_table_cache;

    const processes = new Map();
    const entries = await fs.promises.readdir('/proc');
    let count = 0;
    for (const entry of entries) {
        if (!/^\d+$/.test(entry))
            continue;
        const pid = Number.parseInt(entry, 10);
        const info = read_proc_stat(pid);
        if (info)
            processes.set(pid, info);
        if (++count % 32 === 0)
            await new Promise(resolve => setImmediate(resolve));
    }
    if (process_table_cache_time <= now) {
        process_table_cache = processes;
        process_table_cache_time = now;
    }
    return process_table_cache;
}

function process_command_executable(info) {
    if (!info)
        return '';

    const command = info.cmdline || '';
    const first_arg = command.split(/\s+/)[0] || '';
    return path.basename(first_arg || info.comm || '');
}

function is_game_process(info, game_binary) {
    const executable = process_command_executable(info);
    return executable === game_binary || info.comm === game_binary || executable === 'tf_linux64' || info.comm === 'tf_linux64';
}

function process_has_pid(info, pid) {
    return Boolean(info && pid > 0 && (info.pid === pid || (info.nspids || []).includes(pid)));
}

function add_process_pids_to_map(process_map, info, value) {
    if (!info)
        return;

    process_map.set(info.pid, value);
    for (const pid of info.nspids || [])
        process_map.set(pid, value);
}

function adopted_process(pid) {
    return {
        pid: pid,
        kill: function(signal) {
            process.kill(pid, signal);
        }
    };
}

function collect_descendant_pids(root_pid, processes) {
    return collect_descendant_pids_from_children(root_pid, build_process_children_by_parent(processes));
}

function build_process_children_by_parent(processes) {
    if (processes === cached_process_table && cached_children_by_parent)
        return cached_children_by_parent;

    const children_by_parent = new Map();
    for (const info of processes.values()) {
        if (!children_by_parent.has(info.ppid))
            children_by_parent.set(info.ppid, []);
        children_by_parent.get(info.ppid).push(info.pid);
    }

    if (processes === cached_process_table)
        cached_children_by_parent = children_by_parent;

    return children_by_parent;
}

function collect_descendant_pids_from_children(root_pid, children_by_parent) {
    const descendants = [];
    const stack = [root_pid];
    while (stack.length) {
        const parent_pid = stack.pop();
        for (const child_pid of children_by_parent.get(parent_pid) || []) {
            descendants.push(child_pid);
            stack.push(child_pid);
        }
    }

    return descendants;
}

function firejail_command_has_marker(command, option, value) {
    const marker_pattern = new RegExp(`(^|\\s)${escape_regex(`${option}=${value}`)}(?=\\s|$)`);
    return marker_pattern.test(command);
}

function find_firejail_root_by_marker(processes, option, value) {
    const candidates = [];
    for (const info of processes.values()) {
        const command = info.cmdline || '';
        if (info.comm === 'firejail' && firejail_command_has_marker(command, option, value))
            candidates.push(info);
    }

    const candidate_pids = new Set(candidates.map((info) => info.pid));
    const roots = candidates.filter((info) => !candidate_pids.has(info.ppid));
    const result = roots.length ? roots : candidates;
    result.sort((left, right) => (left.starttime - right.starttime) || (left.pid - right.pid));
    return result[0] || null;
}

function kill_process_tree(root_pid, signal) {
    if (!root_pid || root_pid <= 0)
        return 0;

    const pids = collect_process_tree_pids(root_pid);
    return kill_pids(pids, signal);
}

function collect_process_tree_pids(root_pid) {
    if (!root_pid || root_pid <= 0)
        return [];

    const processes = read_process_table();
    const pids = collect_descendant_pids(root_pid, processes).reverse();
    pids.push(root_pid);
    return pids;
}

function kill_pids(pids, signal) {
    var killed_count = 0;
    for (const pid of pids) {
        try {
            process.kill(pid, signal);
            killed_count++;
        } catch (error) { }
    }

    return killed_count;
}

function is_steamwebhelper_process(info) {
    if (!info)
        return false;

    const command = info.cmdline || '';
    const executable = path.basename(command.split(/\s+/)[0] || '');
    return info.comm === 'steamwebhelper' || executable.includes('steamwebhelper') || command.includes('/steamwebhelper');
}

function find_main_steamwebhelper(steam_launcher_pid, processes) {
    processes = process_table_or_current(processes);
    const steam_tree = new Set(collect_descendant_pids(steam_launcher_pid, processes));
    steam_tree.add(steam_launcher_pid);

    const helpers = [...steam_tree]
        .map((pid) => processes.get(pid))
        .filter(is_steamwebhelper_process);
    const helper_pids = new Set(helpers.map((info) => info.pid));
    const root_helpers = helpers.filter((info) => !helper_pids.has(info.ppid));
    const candidates = root_helpers.length ? root_helpers : helpers;
    candidates.sort((left, right) => (left.starttime - right.starttime) || (left.pid - right.pid));

    if (!candidates.length)
        return { main: null, child_pids: [], helper_pids: [], helper_count: 0 };

    return {
        main: candidates[0],
        child_pids: collect_descendant_pids(candidates[0].pid, processes),
        helper_pids: helpers.map((info) => info.pid),
        helper_count: helpers.length
    };
}

function copy_directory_contents(source_path, target_path) {
    fs.mkdirSync(target_path, { recursive: true });
    for (const entry of fs.readdirSync(source_path, { withFileTypes: true })) {
        const source_entry = path.join(source_path, entry.name);
        const target_entry = path.join(target_path, entry.name);
        const source_stat = fs.statSync(source_entry);

        if (source_stat.isDirectory()) {
            copy_directory_contents(source_entry, target_entry);
        } else if (source_stat.isFile()) {
            fs.copyFileSync(source_entry, target_entry);
        }
    }
}

function copy_navmesh_files(source_path, target_path) {
    if (!source_path || !fs.existsSync(source_path))
        return 0;

    try {
        if (fs.existsSync(target_path) && fs.realpathSync(source_path) === fs.realpathSync(target_path))
            return 0;
    } catch (error) { }

    fs.mkdirSync(target_path, { recursive: true });

    var copied_count = 0;
    for (const entry of fs.readdirSync(source_path, { withFileTypes: true })) {
        if (!entry.isFile() || path.extname(entry.name).toLowerCase() !== '.nav')
            continue;

        const source_entry = path.join(source_path, entry.name);
        const target_entry = path.join(target_path, entry.name);
        fs.copyFileSync(source_entry, target_entry);
        fs.chmodSync(target_entry, 0o755);
        copied_count++;
    }

    return copied_count;
}

function count_navmesh_files(directory_path) {
    try {
        if (!directory_path || !fs.existsSync(directory_path))
            return 0;

        return fs.readdirSync(directory_path, { withFileTypes: true })
            .filter((entry) => entry.isFile() && path.extname(entry.name).toLowerCase() === '.nav')
            .length;
    } catch (error) {
        return 0;
    }
}

function bundled_cathook_configs_path() {
    return path.resolve(__dirname, '..', '..', '..', 'opt', 'cathook', 'configs');
}

function copy_missing_config_files(source_path, target_path) {
    if (!source_path || !fs.existsSync(source_path))
        return 0;

    fs.mkdirSync(target_path, { recursive: true });

    var copied_count = 0;
    for (const entry of fs.readdirSync(source_path, { withFileTypes: true })) {
        if (!entry.isFile())
            continue;

        const source_entry = path.join(source_path, entry.name);
        const target_entry = path.join(target_path, entry.name);
        if (fs.existsSync(target_entry))
            continue;

        fs.copyFileSync(source_entry, target_entry);
        fs.chmodSync(target_entry, 0o644);
        copied_count++;
    }

    return copied_count;
}

function ensure_cathook_config_access(log) {
    if (cathook_configs_ready)
        return;

    const config_path = path.join(CATHOOK_ROOT, 'configs');
    const log_path = path.join(CATHOOK_ROOT, 'logs');
    fs.mkdirSync(config_path, { recursive: true });
    fs.mkdirSync(log_path, { recursive: true });

    const source_path = process.env.CATHOOK_CONFIG_SOURCE || bundled_cathook_configs_path();
    const copied_count = copy_missing_config_files(source_path, config_path);
    chown_tree(config_path, USER.uid, USER.uid);
    chown_tree(log_path, USER.uid, USER.uid);

    if (copied_count > 0 && log)
        log(`Installed ${copied_count} bundled cathook config file(s) into ${config_path}`);

    cathook_configs_ready = true;
}

function rm_path_sync(target_path, options = {}) {
    const force = Boolean(options.force);
    const recursive = Boolean(options.recursive);

    if (typeof fs.rmSync === 'function') {
        fs.rmSync(target_path, options);
        return;
    }

    let status = null;
    try {
        status = fs.lstatSync(target_path);
    } catch (error) {
        if (force && error.code === 'ENOENT')
            return;
        throw error;
    }

    if (status.isDirectory() && !status.isSymbolicLink()) {
        if (!recursive)
            fs.rmdirSync(target_path);
        else {
            for (const entry of fs.readdirSync(target_path))
                rm_path_sync(path.join(target_path, entry), { recursive: true, force: force });
            fs.rmdirSync(target_path);
        }
        return;
    }

    try {
        fs.unlinkSync(target_path);
    } catch (error) {
        if (force && error.code === 'ENOENT')
            return;
        throw error;
    }
}

function copy_steam_seed(source_path, target_path, is_root = true) {
    const skip_entries = new Set(['appcache', 'config', 'logs', 'steamapps', 'steamapps_old', 'userdata']);

    fs.mkdirSync(target_path, { recursive: true });
    for (const entry of fs.readdirSync(source_path)) {
        if (is_root && skip_entries.has(entry))
            continue;

        const source_entry = path.join(source_path, entry);
        const target_entry = path.join(target_path, entry);
        let source_stat = null;
        try {
            source_stat = fs.lstatSync(source_entry);
        } catch (error) {
            if (error.code === 'ENOENT')
                continue;
            throw error;
        }

        rm_path_sync(target_entry, { recursive: true, force: true });
        try {
            if (source_stat.isSymbolicLink()) {
                fs.symlinkSync(fs.readlinkSync(source_entry), target_entry);
            } else if (source_stat.isDirectory()) {
                copy_steam_seed(source_entry, target_entry, false);
            } else if (source_stat.isFile()) {
                fs.copyFileSync(source_entry, target_entry);
                fs.chmodSync(target_entry, source_stat.mode & 0o777);
            }
        } catch (error) {
            if (error.code !== 'ENOENT')
                throw error;
        }
    }
}

function ensure_exact_symlink(link_path, target_path) {
    try {
        const status = fs.lstatSync(link_path);
        if (status.isSymbolicLink() && fs.readlinkSync(link_path) === target_path)
            return;
        rm_path_sync(link_path, { recursive: true, force: true });
    } catch (error) {
        if (error.code !== 'ENOENT')
            throw error;
    }

    fs.symlinkSync(target_path, link_path);
}

function ensure_shared_symlink(shared_path, source_path, log_fn) {
    const resolved_source_path = fs.realpathSync(source_path);
    if (resolved_source_path === shared_path)
        return true;

    if (command_succeeds('mountpoint', ['-q', shared_path])) {
        child_process.execFileSync('umount', [shared_path]);
        if (log_fn)
            log_fn(`Unmounted bind-mounted shared path ${shared_path}`);
    }

    try {
        const status = fs.lstatSync(shared_path);
        if (status.isSymbolicLink()) {
            const current_target = fs.realpathSync(shared_path);
            if (current_target === resolved_source_path)
                return true;
            fs.unlinkSync(shared_path);
        } else {
            rm_path_sync(shared_path, { recursive: true, force: true });
            if (log_fn)
                log_fn(`Removed stale generated shared path ${shared_path}`);
        }
    } catch (error) {
        if (error.code !== 'ENOENT')
            throw error;
    }

    fs.mkdirSync(path.dirname(shared_path), { recursive: true });
    fs.symlinkSync(resolved_source_path, shared_path);
    if (fs.realpathSync(shared_path) !== resolved_source_path)
        throw new Error(`shared symlink verification failed: ${shared_path} -> ${resolved_source_path}`);
    if (log_fn)
        log_fn(`Shared path ready, source=${resolved_source_path}, target=${shared_path}, mode=symlink`);
    return true;
}

function ensure_shared_steam_copy(source_path, shared_path, log_fn) {
    const resolved_source_path = fs.realpathSync(source_path);
    if (resolved_source_path === shared_path)
        return true;

    if (command_succeeds('mountpoint', ['-q', shared_path])) {
        child_process.execFileSync('umount', [shared_path]);
        if (log_fn)
            log_fn(`Unmounted bind-mounted Steam root ${shared_path}`);
    }

    try {
        const status = fs.lstatSync(shared_path);
        if (status.isSymbolicLink())
            fs.unlinkSync(shared_path);
        else if (!status.isDirectory())
            fs.unlinkSync(shared_path);
    } catch (error) {
        if (error.code !== 'ENOENT')
            throw error;
    }

    fs.mkdirSync(shared_path, { recursive: true });
    // This copies the Steam runtime/client files only. copy_steam_seed skips
    // steamapps, userdata, config, caches, and logs, so TF2 is never copied.
    copy_steam_seed(resolved_source_path, shared_path);
    ensure_exact_symlink(path.join(shared_path, 'steamapps'), path.join(resolved_source_path, 'steamapps'));
    if (!steam_root_ready(shared_path))
        throw new Error(`shared Steam client copy is incomplete at ${shared_path}`);
    if (log_fn)
        log_fn(`Shared Steam client ready, copied=${shared_path}, TF2 library=${resolved_source_path}/steamapps`);
    return true;
}

function ensure_local_directory(directory_path, uid, gid) {
    try {
        const status = fs.lstatSync(directory_path);
        if (status.isSymbolicLink() || !status.isDirectory())
            rm_path_sync(directory_path, { recursive: true, force: true });
    } catch (error) {
        if (error.code !== 'ENOENT')
            throw error;
    }

    fs.mkdirSync(directory_path, { recursive: true });
    chown_tree(directory_path, uid, gid);
}

function steam_overlay_paths(bot_name) {
    const base = path.join(STEAM_OVERLAY_ROOT, bot_name);
    return {
        base,
        upper: path.join(base, 'upper'),
        work: path.join(base, 'work'),
        merged: path.join(base, 'merged')
    };
}

function steam_overlay_mounted(merged) {
    let real_merged = merged;
    try {
        real_merged = fs.realpathSync(merged);
    } catch (error) { }
    try {
        const mounts = fs.readFileSync('/proc/mounts', 'utf8');
        return mounts.split('\n').some((line) => {
            const parts = line.split(' ');
            return parts[0] === 'overlay' && parts[1] === real_merged;
        });
    } catch (error) {
        return false;
    }
}

function mount_steam_overlay(lower_path, bot_name) {
    const paths = steam_overlay_paths(bot_name);
    fs.mkdirSync(paths.upper, { recursive: true });
    fs.mkdirSync(paths.work, { recursive: true });
    fs.mkdirSync(paths.merged, { recursive: true });
    try { fs.chownSync(paths.upper, USER.uid, USER.uid); } catch (error) { }

    if (!steam_overlay_mounted(paths.merged)) {
        const options = `lowerdir=${lower_path},upperdir=${paths.upper},workdir=${paths.work}`;
        const result = child_process.spawnSync('mount', ['-t', 'overlay', 'overlay', '-o', options, paths.merged]);
        if (result.status !== 0)
            throw new Error(`overlay mount failed for ${bot_name}: ${(result.stderr || '').toString().trim() || 'status ' + result.status}`);

        for (const entry of STEAM_OVERLAY_PRIVATE_DIRS) {
            const entry_path = path.join(paths.merged, entry);
            rm_path_sync(entry_path, { recursive: true, force: true });
            fs.mkdirSync(entry_path, { recursive: true });
            try { fs.chownSync(entry_path, USER.uid, USER.uid); } catch (error) { }
        }
    }

    return paths.merged;
}

function umount_steam_overlay(bot_name) {
    const paths = steam_overlay_paths(bot_name);
    if (steam_overlay_mounted(paths.merged)) {
        let result = child_process.spawnSync('umount', [paths.merged]);
        if (result.status !== 0)
            child_process.spawnSync('umount', ['-l', paths.merged]);
    }
    rm_path_sync(paths.upper, { recursive: true, force: true });
    rm_path_sync(paths.work, { recursive: true, force: true });
}

function chown_tree(target_path, uid, gid) {
    try {
        fs.chownSync(target_path, uid, gid);
        if (!fs.lstatSync(target_path).isDirectory())
            return;

        for (const entry of fs.readdirSync(target_path)) {
            chown_tree(path.join(target_path, entry), uid, gid);
        }
    } catch (error) { }
}

function ensure_directory_not_symlink(directory_path) {
    try {
        const status = fs.lstatSync(directory_path);
        if (status.isSymbolicLink() || !status.isDirectory())
            rm_path_sync(directory_path, { recursive: true, force: true });
    } catch (error) {
        if (error.code !== 'ENOENT')
            throw error;
    }

    fs.mkdirSync(directory_path, { recursive: true });
}

function get_default_network_interface() {
    if (process.env.CATHOOK_NET_INTERFACE)
        return process.env.CATHOOK_NET_INTERFACE;

    try {
        const route_table = fs.readFileSync('/proc/net/route', 'utf8').trim().split('\n').slice(1);
        for (const line of route_table) {
            const fields = line.trim().split(/\s+/);
            if (fields.length > 7 && fields[1] === '00000000' && fields[7] === '00000000')
                return fields[0];
        }
        for (const line of route_table) {
            const fields = line.trim().split(/\s+/);
            if (fields.length > 1 && fields[1] === '00000000')
                return fields[0];
        }
    } catch (error) { }

    return '';
}

function firejail_network_works(interface_name) {
    try {
        child_process.execFileSync('firejail', [
            '--quiet',
            '--noprofile',
            `--net=${interface_name}`,
            'bash',
            '-c',
            'ping -q -c 1 -W 1 1.1.1.1 >/dev/null 2>&1'
        ], { stdio: 'ignore' });
        return true;
    } catch (error) {
        return false;
    }
}

if (!process.env.SUDO_USER) {
    console.error('[ERROR] Could not find $SUDO_USER');
    process.exit(1);
}

const sudo_uid = Number.parseInt(process.env.SUDO_UID || '', 10);
const USER = {
    name: process.env.SUDO_USER,
    uid: Number.isSafeInteger(sudo_uid) ? sudo_uid : 1000,
    home: process.env.CAT_PANEL_USER_HOME || path.join('/home', process.env.SUDO_USER),
    interface: get_default_network_interface(),
    SUPPORTS_FJ_NET: process.env.CAT_USE_FIREJAIL_NET === '1'
};
if (!USER.interface) {
    USER.SUPPORTS_FJ_NET = false;
} else {
    if (USER.SUPPORTS_FJ_NET && !firejail_network_works(USER.interface))
        USER.SUPPORTS_FJ_NET = false;
}

console.log('Main user name: ' + USER.name);
console.log('Visible windows: ' + (VISIBLE_WINDOWS ? 'yes' : 'no') + ', display: ' + BOT_DISPLAY + ', game_mode: ' + (TEXTMODE_GAME ? 'textmode' : 'graphical'));
console.log('Bot runtime version: steam_log_triggered_client_init_v1');

class Bot extends EventEmitter {
    constructor(botid) {
        super();
        var self = this;
        this.state = STATE.INITIALIZING;

        this.name = "b" + botid;
        this.botid = botid;
        this.home = path.join(__dirname, "..", "..", "user_instances", this.name)

        this.stopped = false;
        this.account = null;
        this.account_generation = 0;
        this.restarts = 0;

        this.log(`Initializing, folder = ${self.name}`);
        ensure_cathook_config_access(this.log.bind(this));

        this.procFirejailSteam = null;
        this.procFirejailGame = null;
        this.procXvfb = null;
        this.botDisplay = null;
        this.botXvfbAdopted = false;
        this.chunked_x_display_entry = null;
        this.network_namespace_ready = USER.SUPPORTS_FJ_NET;
        this.network_namespace_preparing = false;
        this.network_namespace_failed = false;

        // Start timestamp
        this.startTime = null;

        this.ipcState = null;
        this.ipcLastHeartbeat = 0;
        this.ipcID = -1;
        this.ipcObservedAt = 0;
        this.time_ipc_identity_missing = 0;
        this.time_ipc_peer_missing = 0;
        this.ipc_peer_restart_deferred = false;
        this.manager = null;

        this.gameStarted = 0;
        this.gamePid = -1;
        this.gamePreloadLibrary = null;
        this.xauthorityPath = BOT_XAUTHORITY;
        this.gdbSnapshotRunning = false;
        this.time_steamwebhelper_cleanup = 0;
        this.steamwebhelper_cleanup_done = false;
        this.steamwebhelper_frozen_pid = -1;
        this.steamClientInitialized = false;
        this.game_kill_generation = 0;
        this.steam_kill_generation = 0;
        this.game_kill_requested_pid = 0;
        this.steam_kill_requested_pid = 0;
        this.lastRestartReason = '';
        this.restart_failure_times = [];
        this.restart_not_before = 0;
        this.restart_paused = false;
        this.restart_pause_reason = '';
        this.restart_healthy_since = 0;
        this.lastGameKillReason = '';
        this.terminal_auth_state = 0;
        this.clear_steam_webhelper_cache_before_start = false;
        this.steam_log_paths_cache = [];
        this.steam_log_paths_cache_time = 0;
        this.existing_steam_log_paths_cache = [];
        this.existing_steam_log_paths_cache_time = 0;

        this.logSteam = null;
        this.logGame = null;

        this.nativeSteam = fs.existsSync("/usr/bin/steam-native");

        this.spawnOptions = {
            shell: 'bash',
            uid: USER.uid,
            env: {
                PATH: process.env.PATH,
                HOME: USER.home
            }
        }

        this.on('ipc-data', function (obj) {
            if (!self.procFirejailGame)
                return;
            if (self.shouldRestart)
                return;
            if (self.state != STATE.RUNNING && self.state != STATE.WAITING)
                return;
            var id = obj.id;
            var data = obj.data;

            // There is no point in storing the same ipc data again. Removing this will actually cause IPC data to be set while the bot is not running.
            // This is only really a problem between tf2 crash/exit and IPC server auto removal
            if (data.heartbeat == self.ipcLastHeartbeat)
                return;
            self.ipcLastHeartbeat = data.heartbeat;

            self.ipcID = Number(id);
            if (!self.ipcState) {
                self.log(`Assigned IPC ID ${id}`);
                self.schedule_steamwebhelper_cleanup();
            }
            self.ipcObservedAt = Date.now();
            self.ipcState = data;
        });

        this.state = STATE.INITIALIZED;

        this.shouldRun = false;
        this.shouldRestart = false;
        this.isSteamWorking = false;
        this.time_steamWorking = 0;
        this.time_steam_launch_started = 0;
        this.time_steam_login_timeout_started = 0;
        this.time_steamAssumeReady = 0;
        this.time_steamLoggedIn = 0;
        this.time_game_launch = 0;
        this.time_steam_client_initialized_game_launch = 0;
        this.time_gameCheck = 0;
        this.time_ipcState = 0;
        this.time_steam_log_scan = 0;
        this.time_steamStatusLog = 0;
        this.time_steam_boot_status_log = 0;
        this.shouldResetSteam = false;
        this.steamReadyLogged = false;
        this.steam_quick_exit_count = 0;
    }

    log(message) {
        console.log(`[${timestamp('HH:mm:ss')}][${this.name}][${this.state}] ${message}`);
    }

    ensure_network_namespace() {
        if (USER.SUPPORTS_FJ_NET)
            return true;
        if (this.network_namespace_ready)
            return true;
        if (this.network_namespace_failed)
            return false;
        if (fs.existsSync(`/var/run/netns/catbotns${this.botid}`)) {
            this.network_namespace_ready = true;
            return true;
        }
        if (this.network_namespace_preparing)
            return false;

        this.network_namespace_preparing = true;
        this.log('Preparing network namespace');
        const ns_process = child_process.spawn('./scripts/ns-inet', [String(this.botid)], {
            stdio: ['ignore', 'ignore', 'pipe']
        });
        ns_process.stderr.on('data', (data) => {
            const text = String(data || '').trim();
            if (text)
                this.log(`[ERROR] ns-inet: ${text}`);
        });
        ns_process.on('error', (error) => {
            this.network_namespace_preparing = false;
            this.network_namespace_failed = true;
            this.shouldRun = false;
            this.log(`[ERROR] Failed to prepare network namespace: ${error.message}`);
        });
        ns_process.on('exit', (code, signal) => {
            this.network_namespace_preparing = false;
            if (code === 0) {
                this.network_namespace_ready = true;
                this.log('Network namespace ready');
            } else {
                this.network_namespace_failed = true;
                this.shouldRun = false;
                this.log(`[ERROR] Network namespace setup failed code=${code} signal=${signal}`);
            }
        });
        return false;
    }

    shouldSetupSteamapps() {
        try {
            const status = fs.lstatSync(this.steamApps);
            if (!status.isSymbolicLink())
                return true;

            const target_path = fs.readlinkSync(this.steamApps);
            return target_path !== '/opt/steamapps' && target_path !== '/opt/steamapps/';
        } catch (error) {
            return error.code === 'ENOENT';
        }
    }

    setupSteamapps() {
        fs.mkdirSync(path.dirname(this.steamApps), { recursive: true });
        try {
            const status = fs.lstatSync(this.steamApps);
            if (status.isSymbolicLink()) {
                fs.unlinkSync(this.steamApps);
            } else {
                // This is a generated per-bot Steam library. Never rename a
                // copied TF2 tree to steamapps_old: that multiplies disk use.
                rm_path_sync(this.steamApps, { recursive: true, force: true });
            }
        } catch (error) {
            if (error.code !== 'ENOENT')
                throw error;
        }

        fs.symlinkSync(SHARED_STEAMAPPS, this.steamApps);
        return true;
    }

    botSteamPath(steam_path) {
        try {
            const real_steam_path = fs.realpathSync(steam_path);
            if (real_steam_path === this.home || path_is_inside(real_steam_path, this.home))
                return real_steam_path;
            if (real_steam_path === USER.home || path_is_inside(real_steam_path, USER.home))
                return path.resolve(this.home, path.relative(USER.home, real_steam_path));
        } catch (error) { }

        return steam_path;
    }

    steamInstallCandidates() {
        return [
            path.join(this.home, '.steam/steam'),
            path.join(this.home, '.local/share/Steam'),
            path.join(this.home, '.steam/debian-installation')
        ];
    }

    hostSteamInstallCandidates() {
        return [
            path.join(USER.home, '.steam/steam'),
            path.join(USER.home, '.local/share/Steam'),
            path.join(USER.home, '.steam/debian-installation')
        ];
    }

    hostSteamappsCandidates() {
        return unique_paths([
            process.env.CAT_STEAMAPPS_PATH,
            process.env.CAT_STEAM_ROOT ? path.join(process.env.CAT_STEAM_ROOT, 'steamapps') : null,
            path.join(USER.home, '.steam/steam/steamapps'),
            path.join(USER.home, '.local/share/Steam/steamapps'),
            path.join(USER.home, '.steam/debian-installation/steamapps')
        ]);
    }

    hostSteamInstallSource() {
        return this.hostSteamInstallCandidates().find(steam_root_ready) || null;
    }

    steamLaunchRoot() {
        return this.steamInstallCandidates().find(steam_root_ready) || null;
    }

    botHomeLost() {
        try {
            return !fs.existsSync(this.home);
        } catch (error) {
            return true;
        }
    }

    warnIfSteamVguiDowngradeMissing(steam_path) {
        if (!STEAM_VGUI_REQUIRED || !steam_path)
            return;

        const real_steam_path = (() => {
            try {
                return fs.realpathSync(steam_path);
            } catch (error) {
                return steam_path;
            }
        })();
        if (this.warnedSteamVguiPath === real_steam_path)
            return;

        this.warnedSteamVguiPath = real_steam_path;
        const version = steam_installed_version(steam_path);
        if (version === STEAM_VGUI_TARGET_VERSION)
            return;

        this.log(`[WARN] Steam is launching with -vgui, but installed Steam package version is ${version || 'unknown'}; expected downgraded VGUI version ${STEAM_VGUI_TARGET_VERSION}. Run scripts/downgrade_steam_vgui.sh --force before starting bots.`);
    }

    hostSteamappsSource() {
        const candidates = this.hostSteamappsCandidates();
        return candidates.find(steamapps_tf2_ready) || candidates.find((steamapps_path) => steamapps_path && fs.existsSync(steamapps_path)) || null;
    }

    ensureSharedSteamapps() {
        const source_path = this.hostSteamappsSource();
        if (!source_path) {
            this.log(`Shared steamapps unavailable: host Steam steamapps directory was not found.`);
            return false;
        }

        let real_source_path = source_path;
        try {
            real_source_path = fs.realpathSync(source_path);
        } catch (error) { }

        return ensure_shared_symlink(SHARED_STEAMAPPS, real_source_path, this.log.bind(this));
    }

    ensureSharedSteamRoot(source_path) {
        if (!source_path)
            return false;

        let real_source_path = source_path;
        try {
            real_source_path = fs.realpathSync(source_path);
        } catch (error) { }

        return ensure_shared_steam_copy(real_source_path, SHARED_STEAM_ROOT, this.log.bind(this));
    }

    tf2InstallCandidates() {
        const steam_roots = unique_paths([
            this.steamPath,
            ...this.steamInstallCandidates(),
            ...this.hostSteamInstallCandidates().map((steam_path) => this.botSteamPath(steam_path))
        ]);

        return unique_paths([
            process.env.TF2_PATH,
            path.join(SHARED_STEAMAPPS, 'common/Team Fortress 2'),
            ...steam_roots.map((steam_path) => path.join(steam_path, 'steamapps/common/Team Fortress 2')),
            path.join(this.home, '.steam/steam/steamapps/common/Team Fortress 2'),
            path.join(this.home, '.local/share/Steam/steamapps/common/Team Fortress 2'),
            path.join(this.home, '.steam/debian-installation/steamapps/common/Team Fortress 2')
        ]);
    }

    navmesh_source_candidates() {
        return unique_paths([
            this.tf2Path ? path.join(this.tf2Path, 'tf/maps') : null,
            ...this.tf2InstallCandidates().map((tf2_path) => path.join(tf2_path, 'tf/maps')),
            ...this.hostSteamappsCandidates().map((steamapps_path) => path.join(steamapps_path, 'common/Team Fortress 2/tf/maps')),
            path.join(SHARED_STEAMAPPS, 'common/Team Fortress 2/tf/maps')
        ]);
    }

    sync_navmeshes() {
        if (navmesh_sync_done)
            return;

        const target_path = path.join(CATHOOK_ROOT, 'navmeshes');
        var copied_count = 0;
        var source_count = 0;
        for (const source_path of this.navmesh_source_candidates()) {
            try {
                const source_copied_count = copy_navmesh_files(source_path, target_path);
                if (source_copied_count > 0)
                    source_count++;
                copied_count += source_copied_count;
            } catch (error) {
                this.log(`Failed to copy navmeshes from ${source_path} to ${target_path}: ${error.message}`);
            }
        }

        navmesh_sync_done = true;
        if (copied_count > 0)
            this.log(`Navmeshes ready, copied=${copied_count}, sources=${source_count}, target=${target_path}`);
        else if (count_navmesh_files(target_path) > 0)
            this.log(`Navmeshes ready, target=${target_path}`);
        else
            this.log(`No navmesh files found to copy into ${target_path}`);
    }

    prepareSteamInstall() {
        const source_path = this.hostSteamInstallSource();
        if (!source_path)
            return;

        if (!this.ensureSharedSteamRoot(source_path))
            return;

        const target_path = this.botSteamPath(source_path);
        const steam_config_path = path.join(this.home, '.steam');
        const local_entries = new Set(['appcache', 'config', 'logs', 'package', 'registry.vdf', 'userdata']);
        const skip_entries = new Set([...local_entries, 'steamapps', 'steamapps_old']);

        fs.mkdirSync(target_path, { recursive: true });
        fs.chownSync(target_path, USER.uid, USER.uid);

        for (const entry of local_entries) {
            const entry_path = path.join(target_path, entry);
            if (entry.endsWith('.vdf')) {
                try {
                    const status = fs.lstatSync(entry_path);
                    if (status.isSymbolicLink())
                        rm_path_sync(entry_path, { force: true });
                } catch (error) {
                    if (error.code !== 'ENOENT')
                        throw error;
                }
                continue;
            }

            ensure_local_directory(entry_path, USER.uid, USER.uid);
        }

        for (const entry of fs.readdirSync(source_path)) {
            if (skip_entries.has(entry))
                continue;

            const source_entry_path = path.join(SHARED_STEAM_ROOT, entry);
            const target_entry_path = path.join(target_path, entry);
            ensure_exact_symlink(target_entry_path, source_entry_path);
        }

        if (!steam_root_ready(target_path))
            this.log(`[ERROR] Bot Steam root is incomplete at ${target_path}`);
        else
            this.log(`Bot Steam root ready, source=${source_path}, target=${target_path}, mode=shared_links`);

        fs.mkdirSync(steam_config_path, { recursive: true });
        rm_path_sync(path.join(target_path, '.crash'), { force: true });
        ensure_exact_symlink(path.join(steam_config_path, 'bin32'), path.join('debian-installation', 'ubuntu12_32'));
        ensure_exact_symlink(path.join(steam_config_path, 'bin64'), path.join('debian-installation', 'ubuntu12_64'));
        ensure_exact_symlink(path.join(steam_config_path, 'bin'), 'bin32');
        ensure_exact_symlink(path.join(steam_config_path, 'sdk32'), path.join('debian-installation', 'linux32'));
        for (const link_name of ['steam', 'root']) {
            const link_path = path.join(steam_config_path, link_name);
            if (path.resolve(link_path) === path.resolve(target_path)) {
                try {
                    if (!fs.lstatSync(link_path).isSymbolicLink())
                        continue;
                } catch (error) { }
            }

            const link_target = path.relative(steam_config_path, target_path) || '.';
            ensure_exact_symlink(link_path, link_target);
        }
        chown_tree(steam_config_path, USER.uid, USER.uid);
    }

    prepareSteamOverlay(source_path, target_path) {
        let lower_path = source_path;
        try { lower_path = fs.realpathSync(source_path); } catch (error) { }

        let merged;
        try {
            merged = mount_steam_overlay(lower_path, this.name);
        } catch (error) {
            this.log(`[ERROR] Steam overlay mount failed, falling back to copy seed: ${error.message}`);
            return false;
        }

        try {
            const status = fs.lstatSync(target_path);
            if (!status.isSymbolicLink() || fs.readlinkSync(target_path) !== merged) {
                this.log(`Reclaiming previous Steam copy at ${target_path}`);
                rm_path_sync(target_path, { recursive: true, force: true });
            }
        } catch (error) {
            if (error.code !== 'ENOENT')
                throw error;
        }

        fs.mkdirSync(path.dirname(target_path), { recursive: true });
        if (!fs.existsSync(target_path))
            fs.symlinkSync(merged, target_path);

        if (!steam_root_ready(target_path)) {
            this.log(`[ERROR] Steam overlay is incomplete at ${target_path}; falling back to copy seed.`);
            try { umount_steam_overlay(this.name); } catch (error) { }
            try { rm_path_sync(target_path, { recursive: true, force: true }); } catch (error) { }
            return false;
        }

        if (!this.loggedSteamOverlay) {
            this.loggedSteamOverlay = true;
            this.log(`Steam client mounted via overlay: lower=${lower_path} merged=${merged} (per-bot writes in upper layer)`);
        }
        return true;
    }

    steamid32FromLoginUsers() {
        if (!this.account || !this.account.login)
            return null;

        const wanted_login = String(this.account.login).toLowerCase();
        const login_users_paths = unique_paths([
            this.steamPath ? path.join(this.steamPath, 'config/loginusers.vdf') : null,
            ...this.steamInstallCandidates().map((steam_path) => path.join(steam_path, 'config/loginusers.vdf'))
        ]);

        for (const login_users_path of login_users_paths) {
            let text = '';
            try {
                text = fs.readFileSync(login_users_path, 'utf8');
            } catch (error) {
                continue;
            }

            const user_blocks = text.matchAll(/"(\d{17})"\s*\{([\s\S]*?)\n\s*\}/g);
            for (const match of user_blocks) {
                const steamid64 = match[1];
                const block = match[2] || '';
                const account_name = block.match(/"AccountName"\s+"([^"]+)"/);
                if (!account_name || account_name[1].toLowerCase() !== wanted_login)
                    continue;

                return steam_id.steamid64_to_account_id32(steamid64);
            }
        }

        return null;
    }

    steamid32FromSteamLogs() {
        const log_paths = this.steamLogPaths().filter((log_path) => [
            'connection_log.txt',
            'console_log.txt',
            'steamui_login.txt',
            'webhelper_js.txt'
        ].includes(path.basename(log_path)));

        let best_account_id32 = null;
        let best_position = -1;
        for (const log_path of log_paths) {
            let text = '';
            try {
                text = read_file_tail(log_path, 262144);
            } catch (error) {
                continue;
            }

            const login_ok_position = Math.max(
                text.lastIndexOf('RecvMsgClientLogOnResponse()'),
                text.lastIndexOf('Received logon success response'),
                text.lastIndexOf('OnLoginStateChange')
            );
            if (login_ok_position === -1)
                continue;

            const search_text = text.slice(Math.max(0, login_ok_position - 4096), login_ok_position + 4096);
            const direct_match = search_text.match(/RecvMsgClientLogOnResponse\(\)\s*:\s*\[U:1:(\d+)\]\s*'OK'/i);
            if (direct_match) {
                const account_id32 = Number.parseInt(direct_match[1], 10);
                if (Number.isFinite(account_id32) && account_id32 > 0 && login_ok_position > best_position) {
                    best_position = login_ok_position;
                    best_account_id32 = account_id32;
                }
                continue;
            }

            const matches = [...new Set([...search_text.matchAll(/\[U:1:(\d+)\]/g)]
                .map((match) => Number.parseInt(match[1], 10))
                .filter((account_id32) => Number.isFinite(account_id32) && account_id32 > 0))];
            if (matches.length !== 1)
                continue;

            if (login_ok_position > best_position) {
                best_position = login_ok_position;
                best_account_id32 = matches[0];
            }
        }

        return best_account_id32;
    }

    steamid32FromSteamState() {
        return this.steamid32FromLoginUsers() || this.steamid32FromSteamLogs();
    }

    steamLiveLoggedIn() {
        const login = this.account && this.account.login ? escape_regex(this.account.login) : null;
        const login_state_ready = login ? new RegExp(`OnLoginStateChange\\s+${login}\\s+5\\s+`, 'i') : null;
        const ready_patterns = [
            /Logged in OK/i,
            /Logged into Steam/i,
            /Steam signed in/i,
            /Steam client ready/i,
            /Refresh complete/i,
            /RecvMsgClientLogOnResponse\(\)\s*:\s*\[U:1:\d+\]\s*'OK'/i,
            /\[Logged On,[^\]]*\]\s*\[U:1:\d+\]\s*RecvMsgClientLogOnResponse\(\)\s*:\s*processing complete/i,
        ];

        for (const log_path of this.existingSteamLogPaths()) {
            try {
                const log_text = read_file_tail(log_path, 262144);
                if ((login_state_ready && login_state_ready.test(log_text)) ||
                    ready_patterns.some((pattern) => pattern.test(log_text))) {
                    return true;
                }
            } catch (error) { }
        }

        return false;
    }

    steamLoggedIn() {
        return this.steamLiveLoggedIn() && !!this.steamid32FromSteamState();
    }

    sandboxHomePath(host_path) {
        if (path_is_inside(host_path, this.home))
            return path.join(USER.home, path.relative(this.home, host_path));

        return host_path;
    }

    steamLaunchCommand() {
        if (this.nativeSteam)
            return 'steam-native';

        const steam_script_candidates = unique_paths([
            ...this.steamInstallCandidates().map((steam_path) => path.join(steam_path, 'steam.sh')),
            path.join(this.home, '.steam/debian-installation/steam.sh'),
            path.join(this.home, '.steam/steam/steam.sh')
        ]);

        for (const steam_script of steam_script_candidates) {
            if (fs.existsSync(steam_script))
                return shell_quote(this.sandboxHomePath(steam_script));
        }

        this.log('[ERROR] Bot-local steam.sh was not found; falling back to system steam wrapper.');
        return 'steam';
    }

    steamLocalConfigPaths(steamid32) {
        const paths = this.steamInstallCandidates().map((steam_path) => steamid32 ? path.join(steam_path, 'userdata', String(steamid32), 'config/localconfig.vdf') : null);
        for (const steam_path of this.steamInstallCandidates()) {
            const userdata_path = path.join(steam_path, 'userdata');
            try {
                for (const user_id of fs.readdirSync(userdata_path)) {
                    if (/^\d+$/.test(user_id))
                        paths.push(path.join(userdata_path, user_id, 'config/localconfig.vdf'));
                }
            } catch (error) { }
        }
        return unique_paths(paths);
    }

    setSteamTf2LaunchOptions(steamid32, launch_options) {
        let updated_count = 0;
        for (const config_path of this.steamLocalConfigPaths(steamid32)) {
            const root = fs.existsSync(config_path) ? parse_simple_vdf(fs.readFileSync(config_path, 'utf8')) : { UserLocalConfigStore: {} };
            set_vdf_path(root, ['UserLocalConfigStore', 'Software', 'Valve', 'Steam', 'apps', '440', 'LaunchOptions'], launch_options);
            fs.mkdirSync(path.dirname(config_path), { recursive: true });
            fs.writeFileSync(config_path, stringify_simple_vdf(root));
            this.log(`Updated Steam TF2 launch options in ${config_path}`);
            ++updated_count;
        }

        if (!updated_count) {
            this.log('[ERROR] Could not find Steam localconfig.vdf to set TF2 launch options.');
            return false;
        }

        this.log(`Updated Steam TF2 launch options paths=${updated_count}`);
        return true;
    }

    using_chunked_x_display() {
        return !!this.chunked_x_display_entry && !!this.botDisplay;
    }

    release_chunked_x_display() {
        const entry = this.chunked_x_display_entry;
        if (!entry)
            return;

        entry.users.delete(this.name);
        if (this.botDisplay === entry.display)
            this.botDisplay = null;
        this.chunked_x_display_entry = null;
        this.botXvfbAdopted = false;
        if (entry.users.size === 0 && entry.proc)
            stop_chunked_x_display_entry(entry);
    }

    ensure_chunked_x_display() {
        if (this.chunked_x_display_entry && this.botDisplay && !this.chunked_x_display_entry.unavailable)
            return true;
        if (this.chunked_x_display_entry && this.chunked_x_display_entry.unavailable)
            this.release_chunked_x_display();
        if (!CHUNKED_X_DISPLAY)
            return false;
        if (!xvfb_available()) {
            this.log('[WARN] chunked_x_display is enabled but Xvfb is not installed; falling back to shared display.');
            return false;
        }

        const preferred_index = Math.floor(this.botid / CHUNKED_X_DISPLAY_BOTS_PER_DISPLAY);
        for (let offset = 0; offset < CHUNKED_X_DISPLAY_MAX_DISPLAYS; ++offset) {
            const display_index = (preferred_index + offset) % CHUNKED_X_DISPLAY_MAX_DISPLAYS;
            const display_num = CHUNKED_X_DISPLAY_BASE + display_index;
            const entry = chunked_x_display_entry(display_num);
            if (entry.unavailable || entry.users.size >= CHUNKED_X_DISPLAY_BOTS_PER_DISPLAY)
                continue;
            if (!ensure_chunked_x_display_entry(entry, this))
                continue;

            entry.users.add(this.name);
            this.chunked_x_display_entry = entry;
            this.botDisplay = entry.display;
            this.botXvfbAdopted = true;
            this.log(`Using chunked X display ${entry.display} users=${entry.users.size}/${CHUNKED_X_DISPLAY_BOTS_PER_DISPLAY}`);
            return true;
        }

        this.log(`[ERROR] No chunked X display slot available for ${this.name}; max_displays=${CHUNKED_X_DISPLAY_MAX_DISPLAYS} bots_per_display=${CHUNKED_X_DISPLAY_BOTS_PER_DISPLAY}.`);
        return false;
    }

    using_per_bot_x_display() {
        return !!this.procXvfb || (this.botXvfbAdopted && !this.chunked_x_display_entry);
    }

    using_bot_x_display() {
        return this.using_chunked_x_display() || this.using_per_bot_x_display();
    }

    ensure_bot_x_display() {
        if (this.ensure_chunked_x_display())
            return true;

        return this.ensure_per_bot_x_display();
    }

    ensure_per_bot_x_display() {
        if (this.using_per_bot_x_display() && this.botDisplay)
            return true;
        if (!PER_BOT_X_DISPLAY)
            return false;
        if (VISIBLE_WINDOWS) {
            this.log('Per-bot X display disabled because CAT_VISIBLE_WINDOWS=1; using shared display.');
            return false;
        }
        if (!xvfb_available()) {
            this.log('[WARN] per_bot_x_display is enabled but Xvfb is not installed; falling back to shared display.');
            return false;
        }

        const display_num = PER_BOT_X_DISPLAY_BASE + this.botid;
        const lock_path = `/tmp/.X${display_num}-lock`;
        const existing_pid = read_x_lock_pid(display_num);
        if (existing_pid && pid_alive(existing_pid)) {
            this.log(`Adopting existing X server on :${display_num} (pid ${existing_pid})`);
            this.botDisplay = `:${display_num}`;
            this.botXvfbAdopted = true;
            return true;
        }
        if (existing_pid) {
            try { fs.unlinkSync(lock_path); } catch (error) { }
        }

        try {
            fs.mkdirSync('./logs', { recursive: true });
            const xvfb_log_path = `./logs/${this.name}.xvfb.log`;
            const xvfb_log = fs.createWriteStream(xvfb_log_path, { flags: 'a' });
            this.procXvfb = child_process.spawn('Xvfb', [
                `:${display_num}`,
                '-screen', '0', PER_BOT_X_SCREEN,
                '-nolisten', 'tcp',
                '-ac',
                '-noreset'
            ], {
                uid: USER.uid,
                env: { PATH: process.env.PATH, HOME: USER.home },
                stdio: ['ignore', 'pipe', 'pipe'],
                detached: false
            });
            this.procXvfb.stdout.pipe(xvfb_log);
            this.procXvfb.stderr.pipe(xvfb_log);
            this.procXvfb.on('error', (error) => {
                this.log(`[ERROR] Xvfb :${display_num} error: ${error.message}`);
            });
            this.procXvfb.on('exit', (code, signal) => {
                this.log(`Xvfb :${display_num} exited code=${code} signal=${signal}`);
                this.procXvfb = null;
                this.botXvfbAdopted = false;
                this.botDisplay = null;
            });
        } catch (error) {
            this.log(`[ERROR] Failed to spawn Xvfb on :${display_num}: ${error.message}; falling back to shared display.`);
            this.procXvfb = null;
            return false;
        }

        this.botDisplay = `:${display_num}`;
        this.log(`Spawned per-bot Xvfb on ${this.botDisplay} (pid ${this.procXvfb.pid})`);

        const x_socket_path = `/tmp/.X11-unix/X${display_num}`;
        if (!fs.existsSync(x_socket_path))
            this.log(`[WARN] X socket ${x_socket_path} did not appear within 3s; Steam may fail to connect.`);

        return true;
    }

    killXvfb() {
        this.release_chunked_x_display();
        if (this.procXvfb) {
            const pid = this.procXvfb.pid;
            try { this.procXvfb.kill('SIGTERM'); } catch (error) { }
            setTimeout(() => {
                try { process.kill(pid, 0); } catch (error) { return; }
                try { process.kill(pid, 'SIGKILL'); } catch (error) { }
            }, 2000);
        }
        this.procXvfb = null;
        this.botXvfbAdopted = false;
        this.botDisplay = null;
    }

    ensure_xauthority() {
        if (!BOT_XAUTHORITY || !fs.existsSync(BOT_XAUTHORITY)) {
            this.xauthorityPath = '';
            if (VISIBLE_WINDOWS)
                this.log(`Visible windows requested but XAUTHORITY is missing: ${BOT_XAUTHORITY}`);
            return this.xauthorityPath;
        }

        const target_path = path.join(this.home, '.Xauthority');
        try {
            fs.mkdirSync(this.home, { recursive: true });
            fs.copyFileSync(BOT_XAUTHORITY, target_path);
            fs.chownSync(target_path, USER.uid, USER.uid);
            this.xauthorityPath = this.sandboxHomePath(target_path);
        } catch (error) {
            this.xauthorityPath = '';
            this.log(`Failed to copy XAUTHORITY for display ${BOT_DISPLAY}: ${error.message}`);
        }

        return this.xauthorityPath;
    }

    steamSdk64Source() {
        const steam_paths = unique_paths([this.steamPath, ...this.steamInstallCandidates(), ...this.hostSteamInstallCandidates()]);
        for (const steam_path of steam_paths) {
            for (const sdk_dir_name of ['ubuntu12_64', 'linux64']) {
                const sdk_dir = path.join(steam_path, sdk_dir_name);
                if (fs.existsSync(path.join(sdk_dir, 'steamclient.so')))
                    return fs.realpathSync(sdk_dir);
            }
        }

        return null;
    }

    repairSteamSdk64() {
        const source_path = this.steamSdk64Source();
        const steam_dir = path.join(this.home, '.steam');
        const target_path = path.join(this.home, '.steam/sdk64');
        if (!source_path) {
            this.log(`[ERROR] Could not find 64-bit steamclient.so for ${target_path}`);
            return false;
        }

        try {
            ensure_directory_not_symlink(steam_dir);
            rm_path_sync(target_path, { recursive: true, force: true });
            copy_directory_contents(source_path, target_path);
            chown_tree(target_path, USER.uid, USER.uid);
            if (!fs.existsSync(path.join(target_path, 'steamclient.so')))
                throw new Error(`steamclient.so missing after copy from ${source_path}`);
            this.log(`Steam sdk64 ready, source=${source_path}, target=${target_path}, mode=copy`);
            return true;
        } catch (error) {
            this.log(`[ERROR] Failed to prepare Steam sdk64: ${error.message}`);
            return false;
        }
    }

    steamLogPaths() {
        const now = Date.now();
        if (this.steam_log_paths_cache_time && now - this.steam_log_paths_cache_time < 5000)
            return this.steam_log_paths_cache;

        var log_paths = ['./logs/' + this.name + '.steam.log'];

        for (var steam_root of this.steamInstallCandidates()) {
            log_paths.push(
                path.join(steam_root, 'error.log'),
                path.join(steam_root, 'logs/bootstrap_log.txt'),
                path.join(steam_root, 'logs/cef_log.txt'),
                path.join(steam_root, 'logs/connection_log.txt'),
                path.join(steam_root, 'logs/content_log.txt'),
                path.join(steam_root, 'logs/systemdisplaymanager.txt')
            );

            const logs_dir = path.join(steam_root, 'logs');
            try {
                for (var entry of fs.readdirSync(logs_dir)) {
                    if (entry.endsWith('.log') || entry.endsWith('.txt'))
                        log_paths.push(path.join(logs_dir, entry));
                }
            } catch (error) { }
        }

        this.steam_log_paths_cache = unique_paths(log_paths);
        this.steam_log_paths_cache_time = now;
        return this.steam_log_paths_cache;
    }

    steam_log_paths_for_session_clear() {
        const paths = [...this.steamLogPaths()];
        for (const steam_root of this.steamInstallCandidates()) {
            paths.push(path.join(steam_root, 'error.log'));
            const logs_dir = path.join(steam_root, 'logs');
            for (const name of steam_session_log_clear_names)
                paths.push(path.join(logs_dir, name));
        }

        return unique_paths(paths);
    }

    existingSteamLogPaths() {
        const now = Date.now();
        if (this.existing_steam_log_paths_cache_time && now - this.existing_steam_log_paths_cache_time < 5000)
            return this.existing_steam_log_paths_cache;

        this.existing_steam_log_paths_cache = this.steamLogPaths().filter((log_path) => {
            try {
                return fs.existsSync(log_path) && fs.statSync(log_path).isFile();
            } catch (error) {
                return false;
            }
        });
        this.existing_steam_log_paths_cache_time = now;
        return this.existing_steam_log_paths_cache;
    }

    clearSteamStartupLogs() {
        this.steam_log_paths_cache_time = 0;
        this.existing_steam_log_paths_cache_time = 0;
        for (const steam_root of this.steamInstallCandidates()) {
            try {
                fs.mkdirSync(path.join(steam_root, 'logs'), { recursive: true });
            } catch (error) { }
        }

        for (const log_path of this.steam_log_paths_for_session_clear()) {
            try {
                if (!fs.existsSync(log_path))
                    continue;

                const st = fs.lstatSync(log_path);
                if (st.isDirectory())
                    continue;

                try {
                    fs.unlinkSync(log_path);
                } catch (unlink_error) {
                    fs.writeFileSync(log_path, '', { encoding: 'utf8' });
                }
            } catch (error) { }
        }
    }

    steam_webhelper_cache_paths() {
        const home_real_path = fs.existsSync(this.home) ? fs.realpathSync(this.home) : this.home;
        const cache_paths = [];

        for (const steam_root of this.steamInstallCandidates()) {
            cache_paths.push(path.join(steam_root, 'config/htmlcache'));
            cache_paths.push(path.join(steam_root, 'config/cefdata'));
            cache_paths.push(path.join(steam_root, 'config/overlayhtmlcache'));
            cache_paths.push(path.join(steam_root, 'appcache/httpcache'));
            cache_paths.push(path.join(steam_root, 'appcache/cefdata'));
        }

        const removable_cache_paths = [];
        for (const cache_path of unique_paths(cache_paths)) {
            const resolved_cache_path = path.resolve(cache_path);
            if (resolved_cache_path !== path.resolve(this.home) && !path_is_inside(resolved_cache_path, path.resolve(this.home)))
                continue;

            try {
                const cache_status = fs.lstatSync(cache_path);
                if (cache_status.isSymbolicLink()) {
                    removable_cache_paths.push(cache_path);
                    continue;
                }

                const cache_real_path = fs.realpathSync(cache_path);
                if (cache_real_path === home_real_path || path_is_inside(cache_real_path, home_real_path))
                    removable_cache_paths.push(cache_real_path);
            } catch (error) { }
        }

        return unique_paths(removable_cache_paths);
    }

    clear_steam_webhelper_cache() {
        const removed_paths = new Set();

        for (const cache_path of this.steam_webhelper_cache_paths()) {
            try {
                if (removed_paths.has(cache_path))
                    continue;

                rm_path_sync(cache_path, { recursive: true, force: true });
                removed_paths.add(cache_path);
            } catch (error) {
                this.log(`Failed to clear Steam webhelper cache ${cache_path}: ${error.message}`);
            }
        }

        if (removed_paths.size)
            this.log(`Cleared Steam webhelper cache paths: ${[...removed_paths].join(', ')}`);
    }

    logSteamTails(prefix, line_count) {
        const logs = this.existingSteamLogPaths();
        if (!logs.length) {
            this.log(`${prefix}: no Steam log files found under bot home yet.`);
            return;
        }

        this.log(`${prefix}: found Steam logs: ${logs.join(', ')}`);
        for (var log_path of logs) {
            const tail = log_file_tail(log_path, line_count);
            if (tail)
                this.log(`${prefix} ${log_path}:\n${tail}`);
        }
    }

    steamFatalStartupLogPath() {
        for (var log_path of this.existingSteamLogPaths()) {
            try {
                const log_text = read_file_tail(log_path, 262144);
                if (steam_startup_log_has_fatal_error(log_text))
                    return log_path;
            } catch (error) { }
        }

        return null;
    }

    steam_x_client_cap_log_path() {
        for (var log_path of this.existingSteamLogPaths()) {
            try {
                const log_text = read_file_tail(log_path, 262144);
                if (steam_log_has_x_client_cap_error(log_text))
                    return log_path;
            } catch (error) { }
        }

        return null;
    }

    steam_invalid_password_log_path() {
        for (var log_path of this.existingSteamLogPaths()) {
            if (!['connection_log.txt', 'console_log.txt', 'steamui_login.txt'].includes(path.basename(log_path)))
                continue;

            try {
                const log_text = log_file_tail(log_path, steam_auth_log_scan_tail_lines);
                if (steam_log_has_invalid_password(log_text))
                    return log_path;
            } catch (error) { }
        }

        return null;
    }

    steam_login_error_5_log_path() {
        for (var log_path of this.existingSteamLogPaths()) {
            if (!['connection_log.txt', 'console_log.txt', 'steamui_login.txt', 'webhelper_js.txt'].includes(path.basename(log_path)))
                continue;

            try {
                const log_text = log_file_tail(log_path, steam_auth_log_scan_tail_lines);
                if (steam_log_has_login_error_5(log_text))
                    return log_path;
            } catch (error) { }
        }

        return null;
    }

    steam_account_disabled_e43_log_path() {
        for (var log_path of this.existingSteamLogPaths()) {
            const basename = path.basename(log_path);
            if (!['connection_log.txt', 'console_log.txt', 'steamui_login.txt', 'webhelper_js.txt', `${this.name}.steam.log`].includes(basename))
                continue;

            try {
                const log_text = log_file_tail(log_path, steam_auth_log_scan_tail_lines);
                if (steam_log_has_account_disabled_e43(log_text))
                    return log_path;
            } catch (error) { }
        }

        return null;
    }

    log_single_steam_tail(prefix, log_path) {
        const tail = log_file_tail(log_path, 12);
        if (tail)
            this.log(`${prefix} ${log_path}:\n${tail}`);
    }

    mark_terminal_auth_error(state, status_text, log_path) {
        this.log(`[ERROR] Steam auth failed; marking bot ${status_text}. log=${log_path}`);
        this.log_single_steam_tail(`Steam ${status_text} log tail`, log_path);
        this.terminal_auth_state = state;
        this.shouldRun = false;
        this.shouldRestart = false;
        this.state = state;
        this.time_steamWorking = 0;
        this.killGame();
        this.killSteam();
        this.force_kill_runtime_processes(1000);
    }

    restart_after_auth_relogin(status_text, log_path) {
        this.log(`[ERROR] Steam ${status_text} detected; restarting Steam immediately. log=${log_path}`);
        this.log_single_steam_tail(`Steam ${status_text} log tail`, log_path);
        this.request_restart(`Steam ${status_text}`);
        this.time_steamWorking = 0;
        this.killGame();
        this.killSteam();
        this.force_kill_runtime_processes(1000);
    }

    restart_after_x_client_cap(log_path) {
        const entry = this.chunked_x_display_entry;
        if (!entry)
            return false;

        entry.unavailable = true;
        const display = entry.display;
        this.log(`[ERROR] X display ${display} hit Maximum number of clients reached; moving this bot to another chunked X display. log=${log_path}`);
        this.log_single_steam_tail('Steam X client cap log tail', log_path);
        this.release_chunked_x_display();
        this.request_restart('X display client limit reached');
        this.time_steamWorking = 0;
        this.killGame();
        this.killSteam();
        this.force_kill_runtime_processes(1000);
        return true;
    }

    steam_webhelper_stall_log_path() {
        for (var log_path of this.existingSteamLogPaths()) {
            if (!['steamui_html.txt', 'webhelper.txt'].includes(path.basename(log_path)))
                continue;

            try {
                const log_text = read_file_tail(log_path, 262144);
                if (steam_webhelper_browser_stalled(log_text))
                    return log_path;
            } catch (error) { }
        }

        return null;
    }

    steam_login_ui_stall_log_path(time) {
        if (!STEAM_LOGIN_UI_TIMEOUT || !this.time_steam_login_timeout_started)
            return null;

        if (time < this.time_steam_login_timeout_started + STEAM_LOGIN_UI_TIMEOUT)
            return null;

        const log_paths = this.existingSteamLogPaths();
        if (log_paths.some((log_path) => path.basename(log_path) === 'steamui_login.txt'))
            return null;

        return log_paths.find((log_path) => path.basename(log_path) === 'webhelper.txt')
            || log_paths.find((log_path) => path.basename(log_path) === 'steamui_system.txt')
            || log_paths.find((log_path) => path.basename(log_path) === 'bootstrap_log.txt')
            || null;
    }

    markSteamReady(preferredSteamPath) {
        if (!this.ensure_account_loaded())
            return false;

        const candidates = unique_paths([preferredSteamPath, ...this.steamInstallCandidates()]);
        const steam_path = candidates.find(steam_root_ready)
            || candidates.find((candidate) => candidate && fs.existsSync(candidate))
            || path.join(this.home, '.steam/steam');

        this.steamPath = this.botSteamPath(steam_path);
        this.steamApps = path.join(this.steamPath, 'steamapps');
        this.warnIfSteamVguiDowngradeMissing(this.steamPath);

        if (!this.steamLoggedIn()) {
            if (!this.time_steamStatusLog || Date.now() > this.time_steamStatusLog) {
                this.log(`Steam client is initialized, but ${this.account.login} is not logged in yet; waiting before launching TF2.`);
                this.time_steamStatusLog = Date.now() + 10000;
            }
            return false;
        }

        if (!this.time_steamLoggedIn) {
            this.time_steamLoggedIn = Date.now();
            if (steam_logged_in_game_delay > 0) {
                const delay_until = this.time_steamLoggedIn + steam_logged_in_game_delay;
                this.time_steam_client_initialized_game_launch = Math.max(this.time_steam_client_initialized_game_launch || 0, delay_until);
                this.log(`Steam logged in; delaying game launch for ${steam_logged_in_game_delay / 1000} seconds to let SteamAPI settle.`);
            }
        }

        this.ensureSharedSteamapps();
        if (this.shouldSetupSteamapps())
            this.setupSteamapps();
        this.tf2Path = this.tf2InstallCandidates().find(tf2_install_ready) || path.join(this.steamApps, 'common/Team Fortress 2');
        this.sync_navmeshes();

        if (!this.repairSteamSdk64())
            return false;

        this.isSteamWorking = true;

        if (!this.steamReadyLogged) {
            this.log(`Steam ready, steam_path=${this.steamPath}, steamapps=${this.steamApps}, tf2_path=${this.tf2Path}`);
            this.steamReadyLogged = true;
        }

        return true;
    }

    mark_steam_client_initialized(preferredSteamPath, log_text) {
        if (!this.steamClientInitialized) {
            const first_line = String(log_text || '').split(/\r?\n/).find((line) => line.trim()) || 'Steam client activity marker';
            this.log(`Steam client initialized marker seen: ${first_line.trim()}`);
            if (steam_client_initialized_game_delay > 0) {
                this.time_steam_client_initialized_game_launch = Date.now() + steam_client_initialized_game_delay;
                this.log(`Delaying game launch for ${steam_client_initialized_game_delay / 1000} seconds after Steam client initialized marker.`);
            }
        }

        this.steamClientInitialized = true;
        if (!this.isSteamWorking && this.steamLoggedIn())
            return this.markSteamReady(preferredSteamPath);

        return true;
    }

    hostGameLaunchPath() {
        if (BOT_TF2_OVERLAY_ENABLED && this.tf2Path) {
            const overlay_path = bot_tf2_overlay_path(this.home);
            if (tf2_install_ready(overlay_path))
                return overlay_path;
        }

        return this.tf2Path;
    }

    gameLaunchPath() {
        return this.sandboxHomePath(this.hostGameLaunchPath());
    }

    gameBinary() {
        return './tf_linux64';
    }

    steamRuntimeScript() {
        const runtime_dirs = ['ubuntu12_32/steam-runtime/run.sh', 'ubuntu12_64/steam-runtime/run.sh', 'linux64/steam-runtime/run.sh'];
        for (var runtime_dir of runtime_dirs) {
            const host_runtime_path = path.join(this.steamPath, runtime_dir);
            if (fs.existsSync(host_runtime_path))
                return path.join(USER.home, path.relative(this.home, host_runtime_path));
        }

        return null;
    }

    gameRuntimePrefix() {
        const runtime_script = this.nativeSteam ? null : this.steamRuntimeScript();
        if (runtime_script)
            return `LD_LIBRARY_PATH="$("${bash_double_quote_escape(runtime_script)}" printenv LD_LIBRARY_PATH 2>/dev/null):${GAME_LIBRARY_PATH}"`;

        return `LD_LIBRARY_PATH="\${LD_LIBRARY_PATH:-}:${GAME_LIBRARY_PATH}"`;
    }

    game_dependency_check_command(game_launch_path) {
        const runtime_script = this.nativeSteam ? null : this.steamRuntimeScript();
        const runtime_path_command = runtime_script
            ? `"${bash_double_quote_escape(runtime_script)}" printenv LD_LIBRARY_PATH 2>/dev/null`
            : 'printf %s "${LD_LIBRARY_PATH:-}"';
        const escaped_game_path = bash_double_quote_escape(game_launch_path);

        return [
            `cd "${escaped_game_path}"`,
            `game_ld_path="$(${runtime_path_command}):${GAME_LIBRARY_PATH}"`,
            'LD_LIBRARY_PATH="$game_ld_path" ldd ./tf_linux64 ./bin/linux64/engine.so'
        ].join(' && ');
    }

    validateGameDependencies(game_launch_path) {
        try {
            const ldd_output = child_process.execFileSync('bash', ['-lc', this.game_dependency_check_command(game_launch_path)], {
                encoding: 'utf8',
                env: Object.assign({}, process.env, this.spawnOptions.env)
            });
            const missing_libraries = [...ldd_output.matchAll(/^\s*(\S+)\s+=>\s+not found\s*$/gm)].map((match) => match[1]);
            if (!missing_libraries.length)
                return true;

            this.log(`[ERROR] TF2 dependency check failed, missing=${[...new Set(missing_libraries)].join(', ')}`);
            this.log('Install missing runtime libraries with ./install-deps or botpanel/start, then restart the panel.');
            return false;
        } catch (error) {
            this.log(`[ERROR] TF2 dependency check failed: ${error.message}`);
            return false;
        }
    }

    kill_stale_bot_runtime_processes() {
        const bot_name = escape_regex(this.name);
        const bot_namespace = escape_regex(`catbotns${this.botid}`);
        const matchers = [
            new RegExp(`(^|\\s)--name=${bot_name}(\\s|$)`),
            new RegExp(`(^|\\s)--netns=${bot_namespace}(\\s|$)`),
            new RegExp(`(^|\\s)--join=${bot_name}(\\s|$)`),
            new RegExp(`user_instances/${bot_name}(/|\\s|$)`)
        ];
        const stale_pids = [];
        for (const info of read_process_table().values()) {
            if (info.pid === process.pid)
                continue;
            const command = info.cmdline || '';
            if (matchers.some((matcher) => matcher.test(command)))
                stale_pids.push(info.pid);
        }

        const killed_count = kill_pids([...new Set(stale_pids)].sort((left, right) => right - left), 'SIGKILL');
        if (killed_count)
            this.log(`Force-killed stale bot runtime processes before Steam launch count=${killed_count}`);
    }

    steam_ready_log_patterns() {
        return [
            'System startup time:',
            'Logged in OK',
            'logged in OK',
            'Logged into Steam',
            'logged into Steam',
            'Steam client ready',
            'Steam signed in',
            'Refresh complete'
        ];
    }

    steam_log_preferred_path(log_path) {
        return path.basename(log_path) === 'error.log' ? path.dirname(log_path) : null;
    }

    scan_steam_startup_logs() {
        const result = {
            ready_log_path: null,
            client_initialized_log_path: null,
            account_disabled_e43_log_path: null,
            login_error_5_log_path: null,
            invalid_password_log_path: null,
            x_client_cap_log_path: null,
            fatal_startup_log_path: null,
            webhelper_stall_log_path: null
        };
        const ready_patterns = this.steam_ready_log_patterns();
        const auth_log_names = new Set(['connection_log.txt', 'console_log.txt', 'steamui_login.txt', 'webhelper_js.txt', `${this.name}.steam.log`]);
        const webhelper_log_names = new Set(['steamui_html.txt', 'webhelper.txt']);

        for (const log_path of this.existingSteamLogPaths()) {
            let log_text_large = null;
            let log_text_small = null;
            const read_large = () => {
                if (log_text_large === null)
                    log_text_large = read_file_tail(log_path, 262144);
                return log_text_large;
            };
            const read_small = () => {
                if (log_text_small === null)
                    log_text_small = log_file_tail(log_path, steam_auth_log_scan_tail_lines);
                return log_text_small;
            };
            const basename = path.basename(log_path);

            try {
                const large_text = read_large();
                if (!result.client_initialized_log_path && steam_client_initialized_from_log(large_text))
                    result.client_initialized_log_path = log_path;
                if (!result.ready_log_path && ready_patterns.some((pattern) => large_text.includes(pattern)))
                    result.ready_log_path = log_path;
                if (!result.fatal_startup_log_path && steam_startup_log_has_fatal_error(large_text))
                    result.fatal_startup_log_path = log_path;
                if (!result.x_client_cap_log_path && steam_log_has_x_client_cap_error(large_text))
                    result.x_client_cap_log_path = log_path;
                if (!result.webhelper_stall_log_path && webhelper_log_names.has(basename) && steam_webhelper_browser_stalled(large_text))
                    result.webhelper_stall_log_path = log_path;

                if (auth_log_names.has(basename)) {
                    const small_text = read_small();
                    if (!result.invalid_password_log_path && steam_log_has_invalid_password(small_text))
                        result.invalid_password_log_path = log_path;
                    if (!result.login_error_5_log_path && steam_log_has_login_error_5(small_text))
                        result.login_error_5_log_path = log_path;
                    if (!result.account_disabled_e43_log_path && steam_log_has_account_disabled_e43(small_text))
                        result.account_disabled_e43_log_path = log_path;
                }
            } catch (error) { }
        }

        return result;
    }

    apply_steam_startup_scan(scan_result) {
        if (!scan_result)
            return false;

        if (scan_result.client_initialized_log_path) {
            const log_text = read_file_tail(scan_result.client_initialized_log_path, 262144);
            this.mark_steam_client_initialized(this.steam_log_preferred_path(scan_result.client_initialized_log_path), log_text);
            if (this.isSteamWorking)
                return true;
        }

        if (scan_result.ready_log_path) {
            this.markSteamReady(this.steam_log_preferred_path(scan_result.ready_log_path));
            if (this.isSteamWorking)
                return true;
        }

        if (scan_result.account_disabled_e43_log_path) {
            this.mark_terminal_auth_error(STATE.ACCOUNT_DISABLED_E43, 'ACCOUNT DISABLED E43', scan_result.account_disabled_e43_log_path);
            return true;
        }

        if (scan_result.login_error_5_log_path) {
            this.restart_after_auth_relogin('login error 5', scan_result.login_error_5_log_path);
            return true;
        }

        if (scan_result.invalid_password_log_path) {
            this.restart_after_auth_relogin('invalid password E5', scan_result.invalid_password_log_path);
            return true;
        }

        if (scan_result.x_client_cap_log_path && this.restart_after_x_client_cap(scan_result.x_client_cap_log_path))
            return true;

        if (scan_result.fatal_startup_log_path) {
            this.log(`[ERROR] Steam startup fatal error detected in ${scan_result.fatal_startup_log_path}; stopping this bot instead of waiting with ${steam_login_timeout_description()}.`);
            this.logSteamTails('Steam fatal startup log tail', 12);
            this.shouldRun = false;
            this.shouldRestart = false;
            this.killSteam();
            return true;
        }

        if (scan_result.webhelper_stall_log_path) {
            this.log(`[ERROR] Steam webhelper browser startup stalled in ${scan_result.webhelper_stall_log_path}; restarting Steam with a fresh webhelper cache.`);
            this.log_single_steam_tail('Steam webhelper stall log tail', scan_result.webhelper_stall_log_path);
            this.clear_steam_webhelper_cache_before_start = true;
            this.request_restart('Steam webhelper startup stalled');
            this.time_steamWorking = 0;
            return true;
        }

        return false;
    }

    pollSteamReady() {
        return this.apply_steam_startup_scan(this.scan_steam_startup_logs());
    }

    spawnSteam() {
        var self = this;
        if (self.procFirejailSteam) {
            self.log('[ERROR] Steam is already running!');
            return false;
        }
        if (!self.ensure_account_loaded())
            return false;

        if (!fs.existsSync(self.home)) {
            fs.mkdirSync(self.home);
            fs.chownSync(self.home, USER.uid, USER.uid);
        }
        self.kill_stale_bot_runtime_processes();
        self.prepareSteamInstall();
        if (self.clear_steam_webhelper_cache_before_start) {
            self.clear_steam_webhelper_cache();
            self.clear_steam_webhelper_cache_before_start = false;
        }
        self.clearSteamStartupLogs();
        if (!self.ensure_bot_x_display() && CHUNKED_X_DISPLAY) {
            self.log('[ERROR] Chunked X display is enabled but no display slot could be prepared; Steam launch skipped.');
            self.request_restart('chunked X display unavailable');
            self.time_steamWorking = 0;
            return false;
        }
        const using_bot_x = self.using_bot_x_display();
        const display_value = (using_bot_x && self.botDisplay) ? self.botDisplay : BOT_DISPLAY;
        const xauthority_path = using_bot_x ? '' : self.ensure_xauthority();
        if (using_bot_x)
            self.xauthorityPath = '';

        var steambin = this.steamLaunchCommand();
        self.warnIfSteamVguiDowngradeMissing(self.steamLaunchRoot());
        self.time_steam_launch_started = Date.now();
        const steam_preload = steam_preload_value();
        if (!STEAM_TXTMODE_ENABLED)
            self.log('cat-steamtxtmode preload disabled (set CAT_STEAM_TXTMODE=1 to re-enable)');

        self.procFirejailSteam = child_process.spawn(([this.shouldResetSteam, this.shouldResetSteam = 0][0]
            ? LAUNCH_OPTIONS_STEAM_RESET
            : LAUNCH_OPTIONS_STEAM_WITH_HARDWARE)
            // Username
            .replace("%LOGIN%", shell_quote(self.account.login))
            // Password
            .replace("%PASSWORD%", shell_quote(self.account.password))
            // Name of the firejail jail
            .replace("%JAILNAME%", shell_quote(self.name))
            .replace("%STEAM_LD_LIBRARY_PATH%", shell_quote(process.env.LD_LIBRARY_PATH || ''))
            .replace("%LD_PRELOAD%", shell_quote(steam_preload))
            .replace("%HARDWARE_SEED%", shell_quote(STEAM_TXTMODE_HARDWARE_SEED))
            .replace("%CAT_STM_LOOP_SLEEP%", shell_quote(steam_shim_loop_sleep))
            .replace("%CAT_STM_LOOP_SLEEP_US%", shell_quote(String(steam_shim_loop_sleep_us)))
            .replace("%STEAM_VGUI_ARG%", STEAM_VGUI_REQUIRED ? '-vgui' : '')
            // XOrg Display
            .replace("%DISPLAY%", shell_quote(display_value))
            .replace("%XAUTHORITY%", shell_quote(xauthority_path))
            // Network
            .replace("%NETWORK%", USER.SUPPORTS_FJ_NET ? `--net=${USER.interface}` : `--netns=catbotns${this.botid}`)
            // Home folder
            .replace("%HOME%", self.home.replace(/"/g, '\\"'))
            .replace("%STEAM%", steambin),
            self.spawnOptions);
        const steam_process = self.procFirejailSteam;
        steam_process.on('error', (error) => {
            if (self.procFirejailSteam !== steam_process)
                return;
            self.log(`[ERROR] Failed to launch Steam/firejail: ${error.message}`);
            self.shouldRun = false;
            self.shouldRestart = false;
            self.isSteamWorking = false;
            self.steamClientInitialized = false;
            delete self.procFirejailSteam;
        });
        steam_process.stdout.on('error', (error) => {
            self.log(`[ERROR] Steam stdout error: ${error.message}`);
        });
        steam_process.stderr.on('error', (error) => {
            self.log(`[ERROR] Steam stderr error: ${error.message}`);
        });
        self.logSteam = fs.createWriteStream('./logs/' + self.name + '.steam.log');
        self.logSteam.on('error', (err) => { self.log(`error on logSteam pipe: ${err}`) });
        steam_process.stdout.pipe(self.logSteam);

        var tail_steam_err_logs = [];
        var steam_log_listener_paths = new Set();
        var steam_path = path.join(this.home, ".steam/steam");

        function processErrorLogs(text) {
            if (steam_client_initialized_from_log(text))
                self.mark_steam_client_initialized(steam_path, text);

            if (text.includes("System startup time:")) {
                self.markSteamReady(steam_path);

                for (var i = 0; i < tail_steam_err_logs.length; i++) {
                    if (tail_steam_err_logs[i]) {
                        tail_steam_err_logs[i].unwatch();
                    }
                }
                tail_steam_err_logs = [];
            }
            if (RegExp("Failed to load .*\.so: cannot open shared object file: .*").test(text)) {
                this.request_restart('Steam shared library load failed');
                this.shouldResetSteam = true;
            }
        }

        function registerSteamErrorLogListeners() {
            var registered_count = 0;
            const log_paths = [
                ...self.steamInstallCandidates().map((steam_root) => path.join(steam_root, 'error.log')),
                XPRA_LOG
            ];

            for (const log_path of unique_paths(log_paths)) {
                if (steam_log_listener_paths.has(log_path))
                    continue;

                try {
                    const tail = new Tail(log_path);
                    tail.on('line', (data) => {
                        processErrorLogs.bind(this)(data);
                    });
                    tail.on('error', (error) => {
                        self.log(`[ERROR] Steam log tail failed for ${log_path}: ${error.message}`);
                    });
                    tail_steam_err_logs.push(tail);
                    steam_log_listener_paths.add(log_path);
                    registered_count++;
                } catch (error) { }
            }

            return registered_count;
        }

        registerSteamErrorLogListeners.bind(this)();

        steam_process.stderr.on("data", (data) => {
            var text = data.toString();
            processErrorLogs.bind(this)(text);
        });

        steam_process.stdout.on("data", (data) => {
            var text = data.toString();
            if (steam_client_initialized_from_log(text))
                self.mark_steam_client_initialized(steam_path, text);

            // Extend time if we are downloading updates.
            if (text.includes(" Downloading update (")) {
                const timeout = steam_login_timeout();
                self.time_steam_login_timeout_started = Date.now();
                self.time_steamWorking = timeout ? self.time_steam_login_timeout_started + timeout : 0;
            }
            if (text.includes("Error: You are missing the following 32-bit libraries, and Steam may not run:")
                || text.includes("Error: Couldn't set up the Steam Runtime. Are you running low on disk space?")) {
                this.request_restart('Steam runtime dependency check failed');
                this.shouldResetSteam = true;
            }
            if (text.includes("Running Steam on"))
                registerSteamErrorLogListeners.bind(this)();
        });
        steam_process.stderr.pipe(self.logSteam);
        steam_process.on('exit', (code, signal) => self.handleSteamExit(steam_process, code, signal));
        steam_process.on('exit', () => {
            for (var i = 0; i < tail_steam_err_logs.length; i++) {
                if (tail_steam_err_logs[i]) {
                    tail_steam_err_logs[i].unwatch();
                    tail_steam_err_logs[i] = null;
                }
            }
            tail_steam_err_logs = [];
            steam_log_listener_paths.clear();
        });
        self.log(`Launched ${steambin} (${steam_process.pid})`);
        self.log(`Steam log capture: ./logs/${self.name}.steam.log plus ${self.steamInstallCandidates().map((steam_path) => path.join(steam_path, 'logs')).join(', ')}`);
        self.emit('start-steam', steam_process.pid);
        return true;
    }

    spawnGame() {
        var self = this;
        if (!self.ensure_account_loaded())
            return false;

        try {
            for (const entry of fs.readdirSync(this.home)) {
                if (/^\.gl.{6}$/.test(entry))
                    try { fs.unlinkSync(path.join(this.home, entry)); } catch (error) { }
            }
        } catch (error) { }

        var filename = path.join(this.home, `.gl${makeid(6)}`);
        const source_library = cathook_game_library();
        fs.copyFileSync(source_library, filename);
        fs.chmodSync(filename, 0o755);
        fs.chownSync(filename, USER.uid, USER.uid);
        self.gamePreloadLibrary = filename;

        clearSourceLockFiles();
        if (!self.repairSteamSdk64()) {
            self.request_restart('Steam SDK64 repair failed');
            return;
        }

        if (BOT_TF2_OVERLAY_ENABLED) {
            ensure_bot_tf2_overlay(self.home, self.tf2Path, self.log.bind(self));
        } else {
            ensure_bot_videoconfig_linux(self.home, self.log.bind(self));
            ensure_shared_tf2_videoconfig(self.tf2Path, self.log.bind(self));
        }

        const host_game_launch_path = self.hostGameLaunchPath();
        const game_launch_path = self.gameLaunchPath();
        const game_binary = self.gameBinary();
        if (!fs.existsSync(path.join(host_game_launch_path, 'tf_linux64'))) {
            self.log(`[ERROR] Missing tf_linux64 in ${host_game_launch_path} (sandbox=${game_launch_path})`);
            self.request_restart('TF2 binary missing');
            return;
        }
        if (!self.validateGameDependencies(host_game_launch_path)) {
            self.shouldRun = false;
            self.shouldRestart = false;
            self.removeGamePreloadLibrary();
            return false;
        }

        const game_preload = preload_value(self.sandboxHomePath(filename));
        const steamid32 = self.steamid32FromSteamState() || '';
        if (!steamid32) {
            self.log(`SteamID32 for ${self.account.login} is not available from Steam state yet; delaying TF2 launch until Steam is logged in.`);
            self.isSteamWorking = false;
            self.steamClientInitialized = false;
            self.removeGamePreloadLibrary();
            return false;
        }

        self.log(`Resolved SteamID32 ${steamid32} for ${self.account.login}`);
        const game_launch_options = TF2_LAUNCH_MODE === 'steam'
            ? LAUNCH_OPTIONS_GAME_STEAM
            : LAUNCH_OPTIONS_GAME_WITH_HARDWARE;
        const game_port_launch_options = game_port_options(self.botid);
        if (TF2_LAUNCH_MODE === 'steam') {
            const steam_tf2_launch_options = [
                `SteamAppId=440`,
                `SteamGameId=440`,
                `SteamOverlayGameId=440`,
                `SteamEnv=1`,
                `CATHOOK_ROOT="${bash_double_quote_escape(CATHOOK_ROOT)}"`,
                `CATHOOK_ROOT_DIR="${bash_double_quote_escape(CATHOOK_ROOT)}"`,
                `CATHOOK_AUTO_ATTACH=1`,
                `CATHOOK_ATTACH_DELAY_SECONDS=${CATHOOK_ATTACH_DELAY_SECONDS}`,
                `CAT_BOT_ID="${bash_double_quote_escape(String(self.botid))}"`,
                `CAT_BOT_NAME="${bash_double_quote_escape(self.name)}"`,
                `CAT_STEAM_TXTMODE_HARDWARE_SEED="${bash_double_quote_escape(STEAM_TXTMODE_HARDWARE_SEED)}"`,
                `CAT_STEAMID32=${steamid32}`,
                `LD_PRELOAD="${bash_double_quote_escape(game_preload)}"`,
                textmode_allocator_assignments,
                `%command%`,
                `-steam -game tf ${GAME_WINDOW_OPTIONS} -novid -nojoy -noipx -nomessagebox -nominidumps -nohltv -nobreakpad -reuse -noquicktime -precachefontchars -particles 1 -snoforceformat -softparticlesdefaultoff ${GAME_MODE_OPTIONS} -forcenovsync +fps_max 30 ${game_port_launch_options}`
            ].join(' ');

            if (!self.setSteamTf2LaunchOptions(steamid32, steam_tf2_launch_options)) {
                self.removeGamePreloadLibrary();
                return false;
            }
        }
        self.log(`Launching TF2 mode=${TF2_LAUNCH_MODE} from ${game_launch_path} binary=${game_binary} source_library=${source_library} attach_delay_seconds=${CATHOOK_ATTACH_DELAY_SECONDS} preload=${game_preload}`);
        self.procFirejailGame = child_process.spawn(game_launch_options.replace("%GAMEPATH%", bash_double_quote_escape(game_launch_path))
            .replace("%RUNTIME_PREFIX%", self.gameRuntimePrefix())
            .replace("%GAME_BINARY%", game_binary)
            .replace("%STEAM%", self.steamLaunchCommand())
            .replace(/%CATHOOK_ROOT%/g, bash_double_quote_escape(CATHOOK_ROOT))
            .replace("%CATHOOK_ATTACH_DELAY_SECONDS%", String(CATHOOK_ATTACH_DELAY_SECONDS))
            .replace("%BOT_ID%", bash_double_quote_escape(String(self.botid)))
            .replace("%BOT_NAME%", bash_double_quote_escape(self.name))
            .replace("%STEAMID32%", steamid32)
            .replace("%HARDWARE_SEED%", bash_double_quote_escape(STEAM_TXTMODE_HARDWARE_SEED))
            .replace("%GAME_PORT_OPTIONS%", game_port_launch_options)
            // Firejail jail name used by this users steam
            .replace("%JAILNAME%", self.name)
            // cathook
            .replace("%LD_PRELOAD%", `"${game_preload}"`)
            // XORG display
            .replace("%DISPLAY%", self.botDisplay || BOT_DISPLAY)
            .replace("%XAUTHORITY%", bash_double_quote_escape(self.xauthorityPath || '')),
            [], self.spawnOptions);
        const game_process = self.procFirejailGame;
        game_process.on('error', (error) => {
            if (self.procFirejailGame !== game_process)
                return;
            self.log(`[ERROR] Failed to launch TF2/firejail: ${error.message}`);
            self.request_restart('TF2/firejail spawn failed');
            self.removeGamePreloadLibrary();
            delete self.procFirejailGame;
        });
        game_process.stdout.on('error', (error) => {
            self.log(`[ERROR] Game stdout error: ${error.message}`);
        });
        game_process.stderr.on('error', (error) => {
            self.log(`[ERROR] Game stderr error: ${error.message}`);
        });
        self.logGame = fs.createWriteStream('./logs/' + self.name + '.game.log');
        self.logGame.on('error', (err) => { self.log(`error on logGame pipe: ${err}`) });
        game_process.stdout.pipe(self.logGame);
        game_process.stderr.pipe(self.logGame);
        game_process.on('exit', (code, signal) => self.handleGameExit(game_process, code, signal));
        this.restarts++;
        return true;
    }

    handleSteamExit(steam_process, code, signal) {
        if (this.procFirejailSteam !== steam_process) {
            this.log(`Ignored stale Steam exit pid=${steam_process ? steam_process.pid : 0}`);
            return;
        }
        const launcher_pid = steam_process.pid;
        close_steam_log_stream(this);
        this.log(`Steam (${launcher_pid}) exited with code ${code}, signal ${signal}`);
        const steam_runtime = this.time_steam_launch_started ? Date.now() - this.time_steam_launch_started : 0;
        const steam_log_tail = log_file_tail('./logs/' + this.name + '.steam.log', 25);
        if (steam_log_tail)
            this.log(`Steam log tail:\n${steam_log_tail}`);
        const account_disabled_e43_log_path = this.steam_account_disabled_e43_log_path();
        const login_error_5_log_path = this.steam_login_error_5_log_path();
        const invalid_password_log_path = this.steam_invalid_password_log_path();
        const x_client_cap_log_path = this.steam_x_client_cap_log_path();
        if (!this.isSteamWorking && account_disabled_e43_log_path) {
            this.mark_terminal_auth_error(STATE.ACCOUNT_DISABLED_E43, 'ACCOUNT DISABLED E43', account_disabled_e43_log_path);
        }
        else if (!this.isSteamWorking && login_error_5_log_path) {
            this.restart_after_auth_relogin('login error 5', login_error_5_log_path);
        }
        else if (!this.isSteamWorking && invalid_password_log_path) {
            this.restart_after_auth_relogin('invalid password E5', invalid_password_log_path);
        }
        else if (!this.isSteamWorking && x_client_cap_log_path) {
            if (!this.restart_after_x_client_cap(x_client_cap_log_path)) {
                this.log(`[ERROR] X display ${this.botDisplay || BOT_DISPLAY} hit Maximum number of clients reached but no chunked display slot is assigned. log=${x_client_cap_log_path}`);
                this.request_restart('X display client limit reached without replacement');
                this.time_steamWorking = 0;
            }
        }
        else if (!this.isSteamWorking && code !== 0 && steam_startup_log_has_fatal_error(steam_log_tail)) {
            this.log('[ERROR] Steam exited during startup with a fatal runtime setup error; stopping this bot instead of relaunching in a loop.');
            this.log('Run ./install-deps and check the bot Steam runtime/logs before restarting this bot.');
            this.shouldRun = false;
            this.shouldRestart = false;
        }
        else if (!this.isSteamWorking && !this.steamClientInitialized && code === 0 && signal === null && steam_runtime > 0 && steam_runtime < 10000) {
            this.steam_quick_exit_count++;
            this.log(`[ERROR] Steam exited cleanly after ${Math.ceil(steam_runtime / 1000)} seconds before login/readiness; treating it as failed startup instead of respawning immediately. quick_exit_count=${this.steam_quick_exit_count}`);
            this.request_restart('Steam exited before readiness');
            this.time_steamWorking = 0;
        }
        else if (this.shouldRun && !this.isSteamWorking) {
            this.log(`[ERROR] Steam exited before login/readiness after ${Math.max(1, Math.ceil(steam_runtime / 1000))} seconds; restarting instead of leaving bot in STARTING.`);
            this.request_restart('Steam exited before readiness');
            this.time_steamWorking = 0;
        }
        this.emit('exit-steam');

        this.isSteamWorking = false;
        this.steamClientInitialized = false;
        this.time_steam_launch_started = 0;
        this.time_steam_login_timeout_started = 0;
        this.time_steamLoggedIn = 0;
        this.time_steam_client_initialized_game_launch = 0;
        this.time_steamwebhelper_cleanup = 0;
        this.time_steam_log_scan = 0;
        this.steamwebhelper_cleanup_done = false;
        this.steamwebhelper_frozen_pid = -1;

        delete this.procFirejailSteam;
        this.steam_kill_requested_pid = 0;
    }
    handleGameExit(game_process, code, signal) {
        if (this.procFirejailGame !== game_process) {
            this.log(`Ignored stale game exit pid=${game_process ? game_process.pid : 0}`);
            return;
        }
        const launcher_pid = game_process.pid;
        const game_pid = this.gamePid;
        this.log(`Game (${launcher_pid}) exited with code ${code}, signal ${signal}`);
        const game_log_tail = log_file_tail('./logs/' + this.name + '.game.log', 25);
        if (game_log_tail)
            this.log(`Game log tail:\n${game_log_tail}`);
        this.writeGameExitDiagnostics(code, signal, launcher_pid, game_pid, game_log_tail);
        if (!this.ipcState && !this.gameStarted && game_startup_log_has_fatal_error(game_log_tail)) {
            this.log('[ERROR] TF2 exited during startup with a fatal loader/runtime error; stopping this bot instead of restarting in a loop.');
            this.log('Check the game log and the deployed preload with ldd/readelf, then deploy a library compatible with this host runtime before restarting this bot.');
            this.shouldRun = false;
            this.shouldRestart = false;
        }
        const requested_game_exit = this.game_kill_requested_pid === launcher_pid;
        const crash_signal = signal !== null && !['SIGINT', 'SIGTERM', 'SIGKILL'].includes(signal);
        const crashed = !requested_game_exit && ((code !== null && code !== 0) || crash_signal);
        // A launcher that exits before the first IPC heartbeat leaves Steam
        // looking ready while the update loop still owns the bot.  Without a
        // restart request, the next update immediately calls spawnGame again,
        // producing an unbounded STARTING loop (for example firejail
        // "cannot find sandbox").  Route every unexpected pre-IPC exit
        // through the same bounded restart circuit used by runtime failures.
        const startup_exit = !requested_game_exit && !this.ipcState && !this.gameStarted && crashed;
        if (startup_exit) {
            this.log(`[ERROR] TF2 exited before IPC startup (code=${code}, signal=${signal}); applying bounded restart backoff.`);
            this.request_restart('TF2 exited before IPC startup');
        }
        if (crashed && game_pid > 0 && GDB_CRASH_REPORTS)
            this.runGdbCrashReport(game_pid, code, signal);
        else
            this.removeGamePreloadLibrary();
        this.clear_ipc_state();
        this.gameStarted = 0;
        this.gamePid = -1;
        this.time_steamLoggedIn = 0;
        this.time_game_launch = 0;
        this.time_steam_client_initialized_game_launch = 0;
        this.time_gameCheck = 0;
        this.time_ipcState = 0;
        this.time_steamwebhelper_cleanup = 0;
        this.time_steam_log_scan = 0;
        this.steamwebhelper_cleanup_done = false;
        if (this.shouldRun && this.procFirejailSteam)
            this.state = STATE.STARTING;
        delete this.procFirejailGame;
        this.game_kill_requested_pid = 0;
    }

    clear_ipc_state() {
        this.ipcState = null;
        this.ipcID = -1;
        this.ipcLastHeartbeat = 0;
        this.ipcObservedAt = 0;
        this.time_ipc_identity_missing = 0;
        this.time_ipc_peer_missing = 0;
    }

    ipc_heartbeat_stale(time) {
        if (!this.ipcState || !this.ipcState.heartbeat || !ipc_heartbeat_stale_timeout)
            return false;

        if (!this.auto_restart_allowed())
            return false;

        if (this.manager && !this.manager.ipc_queries_healthy())
            return false;

        return time - this.ipcState.heartbeat * 1000 > ipc_heartbeat_stale_timeout;
    }

    clear_ipc_peer_missing() {
        this.time_ipc_peer_missing = 0;
        this.ipc_peer_restart_deferred = false;
    }

    mark_ipc_peer_missing(time, manager) {
        if (!this.ipcState || this.ipcID < 0 || this.shouldRestart)
            return;

        if (!this.auto_restart_allowed())
            return;

        if (manager && !manager.ipc_queries_healthy())
            return;

        if (!this.time_ipc_peer_missing)
            this.time_ipc_peer_missing = time;

        const grace_ms = manager ? manager.ipc_peer_missing_grace_ms() : 15000;
        if (time - this.time_ipc_peer_missing > grace_ms) {
            if (!this.request_restart(`IPC peer ${this.ipcID} disappeared from server query`))
                this.ipc_peer_restart_deferred = true;
        }
    }

    steam_boot_in_progress() {
        return this.state === STATE.STARTING && !!this.procFirejailSteam && !this.steamClientInitialized;
    }

    start_slot_in_use() {
        return this.state === STATE.STARTING && !!this.procFirejailSteam;
    }

    ipc_identity_missing() {
        if (!this.auto_restart_allowed())
            return false;

        return !this.ipcState || !this.ipcState.friendid || this.ipcState.friendid === 0;
    }

    request_restart(reason, bypass_restart_budget, start_if_stopped) {
        if (!this.shouldRun && !start_if_stopped)
            return false;

        // A restart request can be observed repeatedly while a failed child is
        // exiting.  Count that failure once, then delay the next launch.  A
        // manual/API restart is an explicit operator recovery and clears the
        // circuit breaker instead of being charged to it.
        if (this.shouldRestart)
            return true;

        const now = Date.now();
        if (!bypass_restart_budget && this.manager && !this.manager.allow_restart(this, reason))
            return false;

        if (bypass_restart_budget) {
            this.restart_failure_times = [];
            this.restart_not_before = 0;
            this.restart_paused = false;
            this.restart_pause_reason = '';
        } else {
            if (this.restart_paused) {
                this.log(`[WARN] Automatic restart remains paused: ${this.restart_pause_reason}`);
                return false;
            }

            this.restart_failure_times = this.restart_failure_times.filter((time) => now - time < AUTO_RESTART_WINDOW_MS);
            if (this.restart_failure_times.length + 1 >= AUTO_RESTART_MAX_FAILURES) {
                this.restart_paused = true;
                this.restart_pause_reason = `${AUTO_RESTART_MAX_FAILURES} failures within ${AUTO_RESTART_WINDOW_MS / 1000}s; last=${reason || 'unknown'}`;
                this.shouldRestart = false;
                this.shouldRun = false;
                this.state = STATE.RESTART_PAUSED;
                this.log(`[ERROR] Automatic restart circuit opened: ${this.restart_pause_reason}. Use Restart to resume this bot.`);
                return false;
            }

            const failure_number = this.restart_failure_times.length;
            const delay = AUTO_RESTART_DELAYS_MS[Math.min(failure_number, AUTO_RESTART_DELAYS_MS.length - 1)];
            this.restart_failure_times.push(now);
            this.restart_not_before = now + delay;
        }

        this.ipc_peer_restart_deferred = false;
        this.lastRestartReason = reason || 'unspecified restart';
        const delayed_seconds = this.restart_not_before > now
            ? Math.ceil((this.restart_not_before - now) / 1000)
            : 0;
        this.log(`Restart requested: ${reason}${delayed_seconds ? `; retry_in_seconds=${delayed_seconds}` : ''}`);
        this.clear_ipc_state();
        this.terminal_auth_state = 0;
        this.shouldRun = true;
        this.shouldRestart = true;
        return true;
    }

    auto_restart_allowed() {
        return Boolean(this.shouldRun &&
            !this.shouldRestart &&
            !this.terminal_auth_state &&
            this.state === STATE.RUNNING &&
            this.procFirejailGame &&
            this.ipcState &&
            (!this.manager || this.manager.can_auto_restart_bot(this)));
    }

    auto_restart(reason) {
        if (!this.auto_restart_allowed())
            return false;

        return this.request_restart(reason || 'automatic restart');
    }

    mark_restart_healthy(time) {
        if (!this.restart_failure_times.length)
            return;

        if (!this.restart_healthy_since)
            this.restart_healthy_since = time;
        if (time - this.restart_healthy_since < AUTO_RESTART_STABLE_MS)
            return;

        this.log(`Restart circuit recovered after ${AUTO_RESTART_STABLE_MS / 1000}s of healthy runtime.`);
        this.restart_failure_times = [];
        this.restart_not_before = 0;
        this.restart_healthy_since = 0;
    }

    reset() {
        this.procFirejailSteam = null;
        this.procFirejailGame = null;
        this.isSteamWorking = false;
        this.time_steamWorking = 0;
        this.time_steam_launch_started = 0;
        this.time_steam_login_timeout_started = 0;
        this.time_steamAssumeReady = 0;
        this.time_steamLoggedIn = 0;
        this.time_game_launch = 0;
        this.time_steam_client_initialized_game_launch = 0;
        this.time_gameCheck = 0;
        this.time_ipcState = 0;
        this.time_ipc_identity_missing = 0;
        this.time_ipc_peer_missing = 0;
        this.time_steamwebhelper_cleanup = 0;
        this.time_steam_log_scan = 0;
        this.time_steamStatusLog = 0;
        this.time_steam_boot_status_log = 0;
        this.shouldRestart = false;
        this.steamReadyLogged = false;
        this.steamClientInitialized = false;
        this.steam_quick_exit_count = 0;
        this.steamwebhelper_cleanup_done = false;
        this.steamwebhelper_frozen_pid = -1;
        this.restart_healthy_since = 0;
        this.game_kill_requested_pid = 0;
        this.steam_kill_requested_pid = 0;
        this.lastRestartReason = '';
        this.lastGameKillReason = '';
        this.terminal_auth_state = 0;
        this.gamePid = -1;
        this.gameStarted = 0;
        this.startTime = null;
        this.removeGamePreloadLibrary();
        // Needs to be reset here because resetting it in handleGameExit is not enough
        this.clear_ipc_state();
    }

    schedule_forced_runtime_kill(kind, root_pid, generation) {
        if (!root_pid || root_pid <= 0 || !runtime_kill_grace_time)
            return;

        const pid_starttimes = new Map();
        for (const pid of collect_process_tree_pids(root_pid)) {
            const info = read_proc_stat(pid);
            if (info)
                pid_starttimes.set(pid, info.starttime);
        }
        setTimeout(() => {
            const current_generation = kind === 'game' ? this.game_kill_generation : this.steam_kill_generation;
            if (current_generation === generation) {
                const stale_pids = [];
                for (const [pid, starttime] of pid_starttimes.entries()) {
                    const info = read_proc_stat(pid);
                    if (info && info.starttime === starttime)
                        stale_pids.push(pid);
                }

                const killed_count = kill_pids(stale_pids, 'SIGKILL');
                if (killed_count)
                    this.log(`Force-killed stuck ${kind} process tree pid=${root_pid} count=${killed_count}`);
            }
        }, runtime_kill_grace_time);
    }

    killSteam() {
        if (this.procFirejailSteam) {
            const pid = this.procFirejailSteam.pid;
            if (this.steam_kill_requested_pid === pid)
                return;
            this.steam_kill_requested_pid = pid;
            this.log('Killing steam');
            this.resume_steamwebhelper();
            const generation = ++this.steam_kill_generation;
            try {
                this.procFirejailSteam.kill("SIGINT");
            } catch (error) { }
            this.schedule_forced_runtime_kill('steam', pid, generation);
        }
    }
    killGame(reason) {
        if (this.procFirejailGame) {
            const pid = this.procFirejailGame.pid;
            if (this.game_kill_requested_pid === pid)
                return;
            this.game_kill_requested_pid = pid;
            this.lastGameKillReason = reason || this.lastRestartReason || 'panel requested game kill';
            this.log(`Killing game: ${this.lastGameKillReason}`);
            const generation = ++this.game_kill_generation;
            try {
                this.procFirejailGame.kill("SIGINT");
            } catch (error) { }
            this.schedule_forced_runtime_kill('game', pid, generation);
        }
    }

    force_kill_runtime_processes(delay_ms) {
        const steam_pid = this.procFirejailSteam ? this.procFirejailSteam.pid : 0;
        const game_pid = this.procFirejailGame ? this.procFirejailGame.pid : 0;
        const run = () => {
            const killed_game = kill_process_tree(game_pid, 'SIGKILL');
            const killed_steam = kill_process_tree(steam_pid, 'SIGKILL');
            if (killed_game || killed_steam)
                this.log(`Force-killed runtime processes game=${killed_game} steam=${killed_steam}`);
        };

        if (delay_ms && delay_ms > 0)
            setTimeout(run, delay_ms);
        else
            run();
    }

    advance_account_generation(reason) {
        this.account_generation++;
        this.log(`Advancing to account generation ${this.account_generation}: ${reason}`);
        this.request_restart(`account generation advanced: ${reason}`, true, true);
        this.account = null;
        this.clear_ipc_state();
        this.killGame();
        this.killSteam();
        this.force_kill_runtime_processes(1000);
    }

    gdbLogPath() {
        return './logs/' + this.name + '.gdb.log';
    }

    appendGdbLog(text) {
        fs.mkdirSync('./logs', { recursive: true });
        fs.appendFileSync(this.gdbLogPath(), text);
    }

    writeGameExitDiagnostics(code, signal, launcher_pid, game_pid, game_log_tail) {
        let ipc_snapshot = 'null';
        try {
            ipc_snapshot = JSON.stringify(this.ipcState || null);
        } catch (error) {
            ipc_snapshot = `[unserializable ipcState: ${error.message}]`;
        }

        this.appendGdbLog([
            '',
            `========== ${new Date().toISOString()} game exit ==========`,
            `bot=${this.name}`,
            `state=${this.state}`,
            `launcher_pid=${launcher_pid}`,
            `game_pid=${game_pid}`,
            `code=${code}`,
            `signal=${signal}`,
            `restart_reason=${this.lastRestartReason || 'none'}`,
            `game_kill_reason=${this.lastGameKillReason || 'none'}`,
            `ipc_id=${this.ipcID}`,
            `ipc_snapshot=${ipc_snapshot}`,
            '[game log tail]',
            game_log_tail || 'unavailable',
            '=======================================',
            ''
        ].join('\n'));
    }

    removeGamePreloadLibrary(preload_library = this.gamePreloadLibrary) {
        if (!preload_library)
            return;

        if (preload_library === this.gamePreloadLibrary)
            this.gamePreloadLibrary = null;

        try {
            fs.unlinkSync(preload_library);
            this.log(`Removed temp cathook preload ${preload_library}`);
        } catch (error) {
            if (error.code === 'EACCES' || error.code === 'EPERM') {
                try {
                    fs.chownSync(preload_library, USER.uid, USER.uid);
                    fs.chmodSync(preload_library, 0o755);
                    fs.unlinkSync(preload_library);
                    this.log(`Fixed permissions and removed temp cathook preload ${preload_library}`);
                    return;
                } catch (repair_error) {
                    this.log(`[ERROR] Failed to repair temp cathook preload permissions ${preload_library}: ${repair_error.message}`);
                }
            }
            if (error.code !== 'ENOENT')
                this.log(`[ERROR] Failed to remove temp cathook preload ${preload_library}: ${error.message}`);
        }
    }

    runGdbCrashReport(pid, code, signal) {
        if (!pid || pid <= 0)
            return;
        const preload_library = this.gamePreloadLibrary;
        this.gamePreloadLibrary = null;
        if (this.gdbSnapshotRunning) {
            this.appendGdbLog(`\n[${new Date().toISOString()}] skipped gdb crash report pid=${pid}; previous report still running\n`);
            this.removeGamePreloadLibrary(preload_library);
            return;
        }

        this.gdbSnapshotRunning = true;
        this.log(`Writing gdb crash report pid=${pid} log=${this.gdbLogPath()}`);
        this.appendGdbLog(`\n========== ${new Date().toISOString()} crash pid=${pid} code=${code} signal=${signal} ==========\n`);

        const core_path = `/tmp/${this.name}.${pid}.core`;
        const binary_path = path.join(this.hostGameLaunchPath(), this.gameBinary());
        const script = [
            'set -u',
            'has_core=0',
            `echo '[coredumpctl info]'`,
            `coredumpctl info ${pid} 2>&1 || true`,
            `echo '[coredumpctl dump]'`,
            `rm -f ${shell_quote(core_path)}`,
            `if coredumpctl dump ${pid} --output=${shell_quote(core_path)} >/dev/null 2>&1 && [ -s ${shell_quote(core_path)} ]; then`,
            '  has_core=1',
            `  gdb -n -q --batch ${shell_quote(binary_path)} ${shell_quote(core_path)} -ex 'set pagination off' -ex 'info threads' -ex 'info sharedlibrary' -ex 'thread apply all bt' 2>&1 || true`,
            `  rm -f ${shell_quote(core_path)}`,
            'else',
            `  echo 'no core dump available for pid ${pid}; live gdb attach skipped to avoid pausing a running/restarting game'`,
            'fi',
            'if [ "$has_core" = "1" ]; then exit 0; fi',
            'exit 2'
        ].join('\n');
        const gdb = child_process.spawn('sh', ['-lc', script], { uid: 0, gid: 0 });

        gdb.stdout.on('data', (data) => this.appendGdbLog(data.toString()));
        gdb.stderr.on('data', (data) => this.appendGdbLog(data.toString()));
        gdb.on('error', (error) => {
            this.appendGdbLog(`\n[gdb error] ${error.message}\n`);
            this.gdbSnapshotRunning = false;
            this.removeGamePreloadLibrary(preload_library);
        });
        gdb.on('exit', (code, signal) => {
            this.appendGdbLog(`\n[gdb exit] code=${code} signal=${signal}\n`);
            this.gdbSnapshotRunning = false;
            this.removeGamePreloadLibrary(preload_library);
        });
    }

    schedule_steamwebhelper_cleanup() {
        if (!STEAMWEBHELPER_CLEANUP_ENABLED || this.steamwebhelper_cleanup_done || this.time_steamwebhelper_cleanup)
            return;

        this.time_steamwebhelper_cleanup = Date.now() + STEAMWEBHELPER_CLEANUP_DELAY;
        this.log(`Steam webhelper cleanup scheduled in ${STEAMWEBHELPER_CLEANUP_DELAY / 1000} seconds.`);
    }

    run_steamwebhelper_cleanup_if_ready(time) {
        if (!STEAMWEBHELPER_CLEANUP_ENABLED || this.steamwebhelper_cleanup_done || !this.ipcState)
            return;

        if (!this.procFirejailSteam) {
            this.steamwebhelper_cleanup_done = true;
            return;
        }

        if (!this.time_steamwebhelper_cleanup) {
            this.schedule_steamwebhelper_cleanup();
            return;
        }

        if (time < this.time_steamwebhelper_cleanup)
            return;

        this.freeze_steamwebhelper_and_kill_children();
        this.steamwebhelper_cleanup_done = true;
        this.time_steamwebhelper_cleanup = 0;
    }

    freeze_steamwebhelper_and_kill_children() {
        const result = find_main_steamwebhelper(this.procFirejailSteam.pid);
        if (!result.main) {
            this.log('Steam webhelper cleanup skipped: no steamwebhelper process found in Steam process tree.');
            return;
        }

        try {
            process.kill(result.main.pid, 'SIGSTOP');
            this.steamwebhelper_frozen_pid = result.main.pid;
            this.log(`Froze main steamwebhelper pid=${result.main.pid} helpers_in_tree=${result.helper_count}.`);
        } catch (error) {
            this.log(`[ERROR] Failed to freeze main steamwebhelper pid=${result.main.pid}: ${error.message}`);
            return;
        }

        var killed_count = 0;
        const helper_pids = new Set(result.child_pids);
        for (const helper_pid of result.helper_pids)
            helper_pids.add(helper_pid);
        helper_pids.delete(result.main.pid);

        const cleanup_pids = [...helper_pids].reverse();
        for (const child_pid of cleanup_pids) {
            if (child_pid === this.gamePid)
                continue;

            try {
                process.kill(child_pid, 'SIGKILL');
                killed_count++;
            } catch (error) { }
        }

        this.log(`Killed ${killed_count} steamwebhelper helper processes after IPC stayed connected.`);
    }

    resume_steamwebhelper() {
        if (this.steamwebhelper_frozen_pid <= 0)
            return;

        const frozen_pid = this.steamwebhelper_frozen_pid;
        this.steamwebhelper_frozen_pid = -1;
        try {
            process.kill(frozen_pid, 'SIGCONT');
            this.log(`Resumed frozen steamwebhelper pid=${frozen_pid}.`);
        } catch (error) { }
    }

    gameCheck(processes, children_by_parent) {
        let game_process = this.findGameProcess(processes, children_by_parent);
        if (!game_process) {
            const fresh_processes = read_process_table(true);
            game_process = this.findGameProcess(
                fresh_processes,
                build_process_children_by_parent(fresh_processes)
            );
        }
        if (!game_process) {
            if (this.ipcState && this.ipcState.pid) {
                this.gamePid = this.ipcState.pid;
                this.startTime = this.ipcState.starttime || this.startTime;
                this.log(`Found game from IPC (${this.gamePid})`);
                this.emit('start-game', this.procFirejailGame.pid);
                clearSourceLockFiles();
                return true;
            }

            this.log('[ERROR] Could not find running game!');
            return false;
        }

        this.gamePid = game_process.pid;
        this.startTime = game_process.starttime;
        this.log(`Found game (${game_process.pid})`);
        this.emit('start-game', this.procFirejailGame.pid);
        clearSourceLockFiles();
        return true;
    }

    findGameProcess(processes, children_by_parent) {
        if (!this.procFirejailGame)
            return null;

        processes = process_table_or_current(processes);
        children_by_parent = children_by_parent || build_process_children_by_parent(processes);
        const game_binary = this.gameBinary();
        const descendant_pids = collect_descendant_pids_from_children(this.procFirejailGame.pid, children_by_parent);
        const candidates = descendant_pids
            .map((pid) => processes.get(pid))
            .filter((info) => is_game_process(info, game_binary));

        candidates.sort((left, right) => (right.starttime - left.starttime) || (right.pid - left.pid));
        return candidates[0] || null;
    }

    ensure_account_loaded() {
        if (this.account)
            return true;

        this.account = accounts.get(this.botid, this.account_generation);
        if (!this.account) {
            this.state = STATE.NO_ACCOUNT;
            this.shouldRun = false;
            this.shouldRestart = false;
            return false;
        }
        if (this.state == STATE.NO_ACCOUNT)
            this.state = STATE.INITIALIZED;
        return true;
    }

    adopt_runtime_processes(processes, children_by_parent) {
        processes = process_table_or_current(processes);
        children_by_parent = children_by_parent || build_process_children_by_parent(processes);

        const steam_root = this.procFirejailSteam || find_firejail_root_by_marker(processes, '--name', this.name);
        const game_root = this.procFirejailGame || find_firejail_root_by_marker(processes, '--join', this.name);
        if (!steam_root && !game_root)
            return false;

        if (steam_root && !this.procFirejailSteam) {
            if (!this.ensure_account_loaded())
                return false;

            this.procFirejailSteam = adopted_process(steam_root.pid);
            this.time_steamWorking = 0;
            this.time_steam_launch_started = 0;
            this.time_steam_login_timeout_started = 0;
            this.time_steamAssumeReady = 0;
            this.time_steamStatusLog = 0;
            if (game_root) {
                this.isSteamWorking = true;
                this.steamClientInitialized = true;
            } else {
                this.isSteamWorking = false;
                this.steamClientInitialized = false;
                this.prepareSteamInstall();
            }
            this.log(`Adopted Steam runtime (${steam_root.pid})`);
        }

        if (game_root && !this.procFirejailGame) {
            this.procFirejailGame = adopted_process(game_root.pid);
            const game_process = this.findGameProcess(processes, children_by_parent);
            if (game_process) {
                this.gamePid = game_process.pid;
                this.startTime = game_process.starttime;
            }
            this.time_gameCheck = 0;
            this.time_ipcState = 0;
            this.gameStarted = Date.now();
            this.log(`Adopted game runtime (${game_root.pid})`);
        }

        if (this.procFirejailGame) {
            this.state = STATE.RUNNING;
        } else if (this.procFirejailSteam) {
            this.state = STATE.STARTING;
        }

        this.shouldRun = true;
        this.shouldRestart = false;
        return true;
    }

    kill_existing_runtime_processes(processes, children_by_parent) {
        processes = process_table_or_current(processes);
        children_by_parent = children_by_parent || build_process_children_by_parent(processes);

        const roots = [];
        const steam_root = this.procFirejailSteam || find_firejail_root_by_marker(processes, '--name', this.name);
        const game_root = this.procFirejailGame || find_firejail_root_by_marker(processes, '--join', this.name);
        if (steam_root)
            roots.push(steam_root.pid || steam_root);
        if (game_root)
            roots.push(game_root.pid || game_root);

        const unique_roots = [...new Set(roots)].filter((pid) => pid && pid > 0);
        let killed_count = 0;
        for (const root_pid of unique_roots) {
            const pids = collect_descendant_pids_from_children(root_pid, children_by_parent).reverse();
            pids.push(root_pid);
            killed_count += kill_pids(pids, 'SIGTERM');
            setTimeout(() => {
                kill_process_tree(root_pid, 'SIGKILL');
            }, 2000).unref();
        }

        const sandbox_shutdown = command_succeeds('firejail', [`--shutdown=${this.name}`]);
        if (!unique_roots.length && !sandbox_shutdown)
            return false;

        this.shouldRun = false;
        this.shouldRestart = false;
        this.clear_ipc_state();
        this.procFirejailSteam = null;
        this.procFirejailGame = null;
        this.gamePid = -1;
        this.gameStarted = 0;
        this.time_steamWorking = 0;
        this.time_steam_login_timeout_started = 0;
        this.time_steamLoggedIn = 0;
        this.state = STATE.INITIALIZED;
        this.log(`Killed existing runtime process trees roots=${unique_roots.join(',') || 'none'} count=${killed_count} sandbox_shutdown=${sandbox_shutdown}`);
        return true;
    }

    owns_process_pid(pid, processes, children_by_parent) {
        return Boolean(this.find_owned_process_by_pid(pid, processes, children_by_parent));
    }

    find_owned_process_by_pid(pid, processes, children_by_parent) {
        if (!this.procFirejailGame || !pid || pid <= 0)
            return null;

        processes = process_table_or_current(processes);
        children_by_parent = children_by_parent || build_process_children_by_parent(processes);
        const root_process = processes.get(this.procFirejailGame.pid);
        if (process_has_pid(root_process, pid))
            return root_process;

        for (const descendant_pid of collect_descendant_pids_from_children(this.procFirejailGame.pid, children_by_parent)) {
            const info = processes.get(descendant_pid);
            if (process_has_pid(info, pid))
                return info;
        }

        return null;
    }

    index_owned_processes(processes, children_by_parent, process_map) {
        if (!this.procFirejailGame)
            return;

        processes = process_table_or_current(processes);
        children_by_parent = children_by_parent || build_process_children_by_parent(processes);
        add_process_pids_to_map(process_map, processes.get(this.procFirejailGame.pid), this);
        for (const descendant_pid of collect_descendant_pids_from_children(this.procFirejailGame.pid, children_by_parent))
            add_process_pids_to_map(process_map, processes.get(descendant_pid), this);
    }

    ipc_peer_match_score(id, data, processes, owned_process_bot, children_by_parent) {
        if (!data)
            return 0;

        if (this.shouldRestart || this.state == STATE.STOPPING || this.state == STATE.RESTARTING)
            return 0;

        if (this.ipcID == id)
            return 120;

        if (this.ipcState)
            return 0;

        if (owned_process_bot !== undefined) {
            if (owned_process_bot === this)
                return 100;
        } else if (data.pid && this.owns_process_pid(data.pid, processes, children_by_parent)) {
            return 100;
        }

        if (this.startTime && data.starttime && this.startTime == data.starttime)
            return 80;

        if (data.name && data.name === this.name)
            return 60;

        return 0;
    }

    accept_ipc_peer(id, data, processes, children_by_parent) {
        if (!data)
            return;

        this.time_ipc_peer_missing = 0;
        const peer_process = this.find_owned_process_by_pid(data.pid, processes, children_by_parent);
        if (peer_process) {
            this.gamePid = peer_process.pid;
            if (!this.startTime)
                this.startTime = peer_process.starttime;
        } else {
            if (!this.startTime && data.starttime)
                this.startTime = data.starttime;
            if (data.pid)
                this.gamePid = data.pid;
        }

        if (this.adopt_runtime_processes(processes, children_by_parent)) {
            this.ipcID = Number(id);
            if (!this.ipcState) {
                this.log(`Assigned IPC ID ${id}`);
                this.schedule_steamwebhelper_cleanup();
            }
            this.ipcLastHeartbeat = data.heartbeat || 0;
            this.ipcObservedAt = Date.now();
            this.ipcState = data;
            return;
        }

        this.emit('ipc-data', {
            id: id,
            data: data
        });
    }

    refresh_steam_login_timeout(time) {
        const timeout = steam_login_timeout();
        if (!timeout) {
            this.time_steamWorking = 0;
            return;
        }

        if (!this.time_steam_login_timeout_started)
            this.time_steam_login_timeout_started = time;

        this.time_steamWorking = this.time_steam_login_timeout_started + timeout;
    }

    // Apply current state
    update(processes, children_by_parent) {
        var time = Date.now();
        if (this.restart_paused) {
            this.state = STATE.RESTART_PAUSED;
            return;
        }
        if (this.ipc_peer_restart_deferred && this.manager)
            this.request_restart(`IPC peer ${this.ipcID} disappeared from server query (deferred)`);
        if (this.shouldRun && !this.shouldRestart) {
            this.adopt_runtime_processes(processes, children_by_parent);
            if (this.procFirejailSteam) {
                if (this.botHomeLost()) {
                    if (!this.warnedBotHomeLost) {
                        this.warnedBotHomeLost = true;
                        this.log(`[ERROR] Bot home ${this.home} vanished while Steam was running (moved or trashed out from under the sandbox); the panel can no longer read this bot's Steam logs. Restarting to recreate a host-visible home.`);
                    }
                    this.request_restart('bot home disappeared');
                    this.time_steamWorking = 0;
                    return;
                }
                this.warnedBotHomeLost = false;
                if (!this.isSteamWorking) {
                    this.refresh_steam_login_timeout(time);
                    const scan_steam_logs = !this.time_steam_log_scan || time > this.time_steam_log_scan;
                    if (scan_steam_logs) {
                        this.time_steam_log_scan = time + 2000;
                        if (this.apply_steam_startup_scan(this.scan_steam_startup_logs()))
                            return;

                        const stalled_login_ui_log_path = this.steam_login_ui_stall_log_path(time);
                        if (stalled_login_ui_log_path) {
                            this.log(`[ERROR] Steam login UI did not appear within ${STEAM_LOGIN_UI_TIMEOUT / 1000} seconds; restarting Steam with a fresh webhelper cache. last_log=${stalled_login_ui_log_path}`);
                            this.log_single_steam_tail('Steam login UI stall log tail', stalled_login_ui_log_path);
                            this.clear_steam_webhelper_cache_before_start = true;
                            this.request_restart('Steam login UI stalled');
                            this.time_steamWorking = 0;
                            return;
                        }
                    }

                    if (!this.time_steamStatusLog || time > this.time_steamStatusLog) {
                        const timeout_status = this.time_steamWorking
                            ? `remaining_seconds=${Math.max(0, Math.ceil((this.time_steamWorking - time) / 1000))}`
                            : 'login_timeout=disabled';
                        this.log(`Waiting for Steam login/readiness, ${timeout_status}`);
                        const logs = this.existingSteamLogPaths();
                        this.log(logs.length ? `Visible Steam logs: count=${logs.length} sample=${logs.slice(0, 6).join(', ')}` : 'Visible Steam logs: none yet');
                        this.time_steamStatusLog = time + 10000;
                    }

                    if (this.time_steamAssumeReady && time > this.time_steamAssumeReady) {
                        this.log('Steam readiness marker was not found; continuing because CAT_STEAM_READY_SECONDS fallback is enabled.');
                        this.markSteamReady();
                        return;
                    }

                    if (this.time_steamWorking && time > this.time_steamWorking) {
                        this.log('Steam login/readiness timed out.');
                        this.logSteamTails('Steam startup log tail', 12);
                        this.request_restart('Steam login/readiness timeout');
                        this.time_steamWorking = 0;
                        return;
                    }

                    if (STEAM_STARTUP_HARD_TIMEOUT && this.time_steam_launch_started && time - this.time_steam_launch_started > STEAM_STARTUP_HARD_TIMEOUT) {
                        this.log(`Steam startup hard timeout after ${Math.floor((time - this.time_steam_launch_started) / 1000)} seconds; restarting to avoid permanent STARTING state.`);
                        this.logSteamTails('Steam startup hard-timeout log tail', 12);
                        this.request_restart('Steam startup hard timeout');
                        this.time_steamWorking = 0;
                    }
                    return;
                }
                else {
                    if (!this.procFirejailGame) {
                        if (!this.steamClientInitialized && (!this.time_steam_log_scan || time > this.time_steam_log_scan)) {
                            this.time_steam_log_scan = time + 2000;
                            const scan_result = this.scan_steam_startup_logs();
                            if (scan_result.client_initialized_log_path) {
                                const log_text = read_file_tail(scan_result.client_initialized_log_path, 262144);
                                this.mark_steam_client_initialized(this.steam_log_preferred_path(scan_result.client_initialized_log_path), log_text);
                            } else if (scan_result.ready_log_path) {
                                this.markSteamReady(this.steam_log_preferred_path(scan_result.ready_log_path));
                            }
                        }
                        if (!this.steamClientInitialized) {
                            if (!this.time_steamStatusLog || time > this.time_steamStatusLog) {
                                this.log('Steam ready; waiting for client initialization log marker before launching game.');
                                this.time_steamStatusLog = time + 10000;
                            }
                            return;
                        }

                        if (this.time_steam_client_initialized_game_launch && time < this.time_steam_client_initialized_game_launch) {
                            if (!this.time_steamStatusLog || time > this.time_steamStatusLog) {
                                const remaining = Math.ceil((this.time_steam_client_initialized_game_launch - time) / 1000);
                                this.log(`Steam client initialized; delaying game launch, remaining_seconds=${remaining}`);
                                this.time_steamStatusLog = time + 10000;
                            }
                            return;
                        }

                        this.time_steam_client_initialized_game_launch = 0;
                        this.time_game_launch = 0;
                        if (this.spawnGame()) {
                            this.state = STATE.WAITING;
                            this.time_gameCheck = time + TIMEOUT_START_GAME;
                        }
                    }
                    else {
                        if (this.time_gameCheck) {
                            if (time > this.time_gameCheck) {
                                if (!this.gameCheck(processes, children_by_parent)) {
                                    this.request_restart('TF2 process was not found after launch');
                                    this.time_gameCheck = Number.MAX_SAFE_INTEGER;
                                }
                                else {
                                    this.time_gameCheck = 0;
                                    this.time_ipcState = time + TIMEOUT_IPC_STATE;
                                }
                            }
                        }
                        else {
                            if (this.ipcState) {
                                if (this.ipc_heartbeat_stale(time)) {
                                    const stale_seconds = Math.floor((time - this.ipcState.heartbeat * 1000) / 1000);
                                    this.request_restart(`IPC heartbeat stale for ${stale_seconds} seconds`);
                                    return;
                                }
                                if (this.ipc_identity_missing()) {
                                    if (this.manager && !this.manager.ipc_queries_healthy()) {
                                        this.time_ipc_identity_missing = 0;
                                    } else if (!this.time_ipc_identity_missing) {
                                        this.time_ipc_identity_missing = time;
                                    } else if (ipc_identity_timeout && time - this.time_ipc_identity_missing > ipc_identity_timeout) {
                                        this.request_restart(`IPC identity missing for ${Math.floor((time - this.time_ipc_identity_missing) / 1000)} seconds`);
                                        return;
                                    }
                                } else {
                                    this.time_ipc_identity_missing = 0;
                                }
                                this.time_ipcState = 0;
                                if (this.state != STATE.RUNNING) {
                                    this.state = STATE.RUNNING;
                                    this.gameStarted = time;
                                }
                                this.mark_restart_healthy(time);
                                this.run_steamwebhelper_cleanup_if_ready(time);
                            } else if (this.time_ipcState && time > this.time_ipcState) {
                                this.killGame('IPC state timeout before first heartbeat');
                                this.time_ipcState = 0;
                            }

                        }

                    }
                }
            }
            else {
                if (this.procFirejailGame) {
                    this.killGame('bot shouldRun disabled while game is running');
                }
                else {
                    if (!this.ensure_account_loaded()) {
                        this.log(`Preparing to restart with account generation ${this.account_generation}...`);
                        return;
                    }
                    const manager_allows_start = !this.manager || this.manager.can_bot_begin_steam_boot(this, time);
                    if (this.account && manager_allows_start) {
                        if (!this.ensure_network_namespace()) {
                            this.state = STATE.PREPARING;
                            return;
                        }
                        this.state = STATE.STARTING;
                        this.reset();
                        if (this.spawnSteam()) {
                            if (this.manager)
                                this.manager.notify_steam_boot_granted(this, time);
                            else {
                                module.exports.lastStartTime = time;
                                module.exports.lastSteamBootTime = time;
                            }
                            module.exports.currentlyStartingGames++;
                            module.exports.currentlyBootingSteam++;
                            this.time_steam_login_timeout_started = time;
                            this.refresh_steam_login_timeout(time);
                            this.time_steamAssumeReady = TIMEOUT_STEAM_ASSUME_READY ? time + TIMEOUT_STEAM_ASSUME_READY : 0;
                        }
                    } else if (this.account && (!this.time_steam_boot_status_log || time > this.time_steam_boot_status_log)) {
                        if (!manager_allows_start && this.state !== STATE.PENDING)
                            this.state = STATE.PENDING;
                        if (!manager_allows_start && this.manager) {
                            const queue_index = this.manager.start_queue_index(this);
                            const queue_head = this.manager.start_lane.length ? this.manager.start_lane[0].name : 'none';
                            const blocked_by = 'start slots full';
                            this.log(`Waiting for start lane position ${queue_index + 1}/${this.manager.start_lane.length}, head=${queue_head}, ${blocked_by}, active=${this.manager.count_active_starts()}/${max_concurrent_bots()}`);
                        } else if (!this.account)
                            this.log('Waiting for account');
                        this.time_steam_boot_status_log = time + 10000;
                    }
                }
            }
        }
        else {
            if (!this.manager || this.manager.can_bot_begin_restart(this)) {
                if (this.procFirejailGame) {
                    this.killGame('restart requested while game is running');
                }
                if (this.procFirejailSteam) {
                    this.killSteam();
                }
                this.state = this.terminal_auth_state || STATE.STOPPING;
                if (!this.procFirejailSteam && !this.procFirejailGame) {
                    if (this.shouldRun && this.restart_not_before > time) {
                        this.state = STATE.RESTARTING;
                        return;
                    }
                    this.state = this.terminal_auth_state || (this.shouldRun ? STATE.PENDING : STATE.INITIALIZED);
                    this.shouldRestart = false;
                    this.account = null;
                }
            } else {
                if (!this.time_steam_boot_status_log || time > this.time_steam_boot_status_log) {
                    this.log('Restart queued; waiting for higher-id bots to finish restarting first');
                    this.time_steam_boot_status_log = time + 10000;
                }
                this.state = this.terminal_auth_state || STATE.INITIALIZED;
            }
        }
    }

    restart() {
        this.request_restart('manual/API restart', true, true);
    }
    stop() {
        this.shouldRun = false;
    }
    terminate(processes, children_by_parent) {
        processes = processes || read_process_table(true);
        children_by_parent = children_by_parent || build_process_children_by_parent(processes);
        this.kill_existing_runtime_processes(processes, children_by_parent);
        this.shouldRun = false;
        this.shouldRestart = false;
        this.clear_ipc_state();
        this.killGame('manual/API terminate');
        this.killSteam();
        this.force_kill_runtime_processes(1000);
    }
    full_stop() {
        this.stop();
        const fully_stopped = !(this.procFirejailGame || this.procFirejailSteam);
        // Only tear down the per-bot Xvfb once Steam/game are gone, otherwise Steam loses its display mid-shutdown.
        if (fully_stopped) {
            this.killXvfb();
            if (STEAM_OVERLAY) {
                try {
                    umount_steam_overlay(this.name);
                    this.loggedSteamOverlay = false;
                } catch (error) {
                    this.log(`[WARN] Steam overlay teardown failed: ${error.message}`);
                }
            }
        }
        // Delete the network namespace for this bot
        if (!USER.SUPPORTS_FJ_NET && fs.existsSync(`/var/run/netns/catbotns${this.botid}`))
            child_process.execSync(`./scripts/ns-delete ${this.botid}`)
        return fully_stopped;
    }
}

module.exports.bot = Bot;
module.exports.currentlyStartingGames = 0;
module.exports.currentlyBootingSteam = 0;
module.exports.lastStartTime = 0;
module.exports.lastSteamBootTime = 0;
module.exports.start_wave_delay_ms = DELAY_START_TIME;
module.exports.steam_boot_delay_ms = STEAM_BOOT_DELAY;
module.exports.max_steam_boots = max_steam_boots;
module.exports.read_process_table = read_process_table;
module.exports.refresh_process_table = refresh_process_table;
module.exports.build_process_children_by_parent = build_process_children_by_parent;
module.exports.invalidate_process_table_cache = invalidate_process_table_cache;
module.exports.set_process_table_cache_ms = set_process_table_cache_ms;
module.exports.states = STATE;
Object.defineProperty(module.exports, 'MAX_CONCURRENT_BOTS', {
    get: function() { return max_concurrent_bots(); },
    set: function(value) { config.max_concurrent_bots = value; }
});
