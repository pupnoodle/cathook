const EventEmitter = require('events');
const child_process = require('child_process');

const timestamp = require('time-stamp');
const fs = require('fs');
const os = require('os');
const path = require("path");
const { Tail } = require("tail");

const accounts = require('./acc.js');
const config = require('./config');
const steam_id = require('../steam_id');

const CATHOOK_ROOT = process.env.CATHOOK_ROOT || '/opt/cathook';
const VISIBLE_WINDOWS = process.env.CAT_VISIBLE_WINDOWS === '1';
const BOT_DISPLAY = process.env.DISPLAY || process.env.CAT_DEFAULT_DISPLAY || ':699';
const BOT_XAUTHORITY = VISIBLE_WINDOWS ? (process.env.XAUTHORITY || path.join(process.env.HOME || '', '.Xauthority')) : '';
const XPRA_LOG = process.env.CAT_XPRA_LOG || '/tmp/cat-catbot-xpra.log';
const TEXTMODE_GAME = process.env.CAT_TEXTMODE_GAME !== '0';
const GDB_CRASH_REPORTS = process.env.CAT_GDB_CRASH_REPORTS === '1' || config.gdb_crash_reports === true;
const steam_window_options_default = VISIBLE_WINDOWS
    ? '-noreactlogin'
    : '-silent -noreactlogin -cef-disable-gpu -nominidumps -nobreakpad -skipstreamingdrivers';
const steam_window_options = process.env.CAT_STEAM_WINDOW_OPTIONS || steam_window_options_default;
const game_window_options_default = VISIBLE_WINDOWS ? '-gl -sw -w 1280 -h 720' : '-gl -silent -sw -w 640 -h 480';
const GAME_WINDOW_OPTIONS = process.env.CAT_GAME_WINDOW_OPTIONS || game_window_options_default;
const GAME_MODE_OPTIONS = TEXTMODE_GAME
    ? '-nomouse -wavonly'
    : '';
const SHARED_STEAMAPPS = '/opt/steamapps';
const CATHOOK_ATTACH_DELAY_SECONDS = Number.parseInt(process.env.CATHOOK_ATTACH_DELAY_SECONDS || '0', 10);
const TF2_LAUNCH_MODE = (process.env.CAT_TF2_LAUNCH_MODE || 'direct').toLowerCase();
const PER_BOT_X_DISPLAY = process.env.CAT_PER_BOT_X_DISPLAY === '1' || config.per_bot_x_display === true;
const PER_BOT_X_DISPLAY_BASE_VALUE = Number.parseInt(process.env.CAT_PER_BOT_X_DISPLAY_BASE || String(config.per_bot_x_display_base || '1000'), 10);
const PER_BOT_X_DISPLAY_BASE = (Number.isSafeInteger(PER_BOT_X_DISPLAY_BASE_VALUE) && PER_BOT_X_DISPLAY_BASE_VALUE > 0) ? PER_BOT_X_DISPLAY_BASE_VALUE : 1000;
const PER_BOT_X_SCREEN = process.env.CAT_PER_BOT_X_SCREEN || '1280x720x24';

const LAUNCH_OPTIONS_STEAM = `firejail --dns=1.1.1.1 %NETWORK% --noprofile --private="%HOME%" --private-tmp --name=%JAILNAME% --env=PULSE_SERVER="unix:/tmp/pulse.sock" --env=DISPLAY=%DISPLAY% --env=XAUTHORITY=%XAUTHORITY% --env=TMPDIR=/tmp --env=TMP=/tmp --env=TEMP=/tmp --env=XDG_RUNTIME_DIR=/tmp/xdg-runtime --env=LD_LIBRARY_PATH=%STEAM_LD_LIBRARY_PATH% --env=LD_PRELOAD=%LD_PRELOAD% sh -lc 'mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"; if command -v dbus-run-session >/dev/null 2>&1; then exec dbus-run-session -- "$@"; else exec "$@"; fi' steam-session %STEAM% ${steam_window_options} -login %LOGIN% %PASSWORD%`
const LAUNCH_OPTIONS_STEAM_RESET = 'firejail --net=none --noprofile --private="%HOME%" --env=LD_LIBRARY_PATH=%STEAM_LD_LIBRARY_PATH% %STEAM% --reset'
const LAUNCH_OPTIONS_GAME = `firejail --join=%JAILNAME% bash -c 'cd "%GAMEPATH%" && %RUNTIME_PREFIX% SteamAppId=440 SteamGameId=440 SteamOverlayGameId=440 SteamEnv=1 CATHOOK_ROOT="%CATHOOK_ROOT%" CATHOOK_ROOT_DIR="%CATHOOK_ROOT%" CATHOOK_AUTO_ATTACH=1 CATHOOK_ATTACH_DELAY_SECONDS=%CATHOOK_ATTACH_DELAY_SECONDS% CAT_BOT_ID="%BOT_ID%" CAT_BOT_NAME="%BOT_NAME%" CAT_STEAMID32=%STEAMID32% LD_PRELOAD=%LD_PRELOAD% DISPLAY=%DISPLAY% XAUTHORITY="%XAUTHORITY%" PULSE_SERVER="unix:/tmp/pulse.sock" %GAME_BINARY% -steam -game tf ${GAME_WINDOW_OPTIONS} -novid -nojoy -nomessagebox -nominidumps -nohltv -nobreakpad -noquicktime -precachefontchars -particles 1 -snoforceformat -softparticlesdefaultoff ${GAME_MODE_OPTIONS} -forcenovsync -insecure +clientport 27006-27014'`
const LAUNCH_OPTIONS_GAME_STEAM = `firejail --join=%JAILNAME% bash -c 'DISPLAY=%DISPLAY% XAUTHORITY="%XAUTHORITY%" PULSE_SERVER="unix:/tmp/pulse.sock" %STEAM% -applaunch 440'`
const GAME_LIBRARY_PATH = './bin:./bin/linux64:./tf/bin:./tf/bin/linux64:./platform:./platform/bin:./platform/bin/linux64:.';

// Adjust these values as needed to optimize catbot performance
// Steam client output that appears after the client is initialized enough to launch TF2.
const STEAM_CLIENT_INITIALIZED_PATTERNS = [
    'Desktop state changed:',
    'Caching cursor image',
    'reaping pid:'
];
const steam_client_initialized_game_delay_default_seconds = 0;
const STEAM_CLIENT_INITIALIZED_GAME_DELAY_SECONDS_VALUE = Number.parseInt(process.env.CAT_STEAM_CLIENT_INITIALIZED_GAME_DELAY_SECONDS || String(steam_client_initialized_game_delay_default_seconds), 10);
const steam_client_initialized_game_delay = (Number.isFinite(STEAM_CLIENT_INITIALIZED_GAME_DELAY_SECONDS_VALUE) ? Math.max(0, STEAM_CLIENT_INITIALIZED_GAME_DELAY_SECONDS_VALUE) : steam_client_initialized_game_delay_default_seconds) * 1000;
// How long to wait for the TF2 process to be created by firejail
const TIMEOUT_START_GAME = 10000;
// Timeout for cathook to connect to the IPC server once injected
const TIMEOUT_IPC_STATE = Number.parseInt(process.env.CAT_IPC_TIMEOUT_SECONDS || '90', 10) * 1000;
const ipc_heartbeat_stale_timeout = Number.parseInt(process.env.CAT_IPC_STALE_SECONDS || '45', 10) * 1000;
const ipc_identity_timeout = Number.parseInt(process.env.CAT_IPC_IDENTITY_TIMEOUT_SECONDS || '60', 10) * 1000;
const runtime_kill_grace_time = Number.parseInt(process.env.CAT_RUNTIME_KILL_GRACE_SECONDS || '8', 10) * 1000;
// Time to wait for Steam to log in is configured in ch-settings.json. 0 disables it.
const TIMEOUT_STEAM_ASSUME_READY = Number.parseInt(process.env.CAT_STEAM_READY_SECONDS || '0', 10) * 1000;
const steam_logged_in_game_delay_default_seconds = 0;
const STEAM_LOGGED_IN_GAME_DELAY_SECONDS_VALUE = Number.parseInt(process.env.CAT_STEAM_LOGGED_IN_GAME_DELAY_SECONDS || String(steam_logged_in_game_delay_default_seconds), 10);
const steam_logged_in_game_delay = (Number.isFinite(STEAM_LOGGED_IN_GAME_DELAY_SECONDS_VALUE) ? Math.max(0, STEAM_LOGGED_IN_GAME_DELAY_SECONDS_VALUE) : steam_logged_in_game_delay_default_seconds) * 1000;
const STEAMWEBHELPER_CLEANUP_ENABLED = process.env.CAT_STEAMWEBHELPER_CLEANUP === '1' || config.steamwebhelper_cleanup === true;
const STEAMWEBHELPER_CLEANUP_DELAY_SECONDS_VALUE = Number.parseInt(process.env.CAT_STEAMWEBHELPER_CLEANUP_SECONDS || '10', 10);
const STEAMWEBHELPER_CLEANUP_DELAY = (Number.isFinite(STEAMWEBHELPER_CLEANUP_DELAY_SECONDS_VALUE) ? Math.max(0, STEAMWEBHELPER_CLEANUP_DELAY_SECONDS_VALUE) : 10) * 1000;
const STEAM_LOGIN_UI_TIMEOUT_SECONDS_VALUE = Number.parseInt(process.env.CAT_STEAM_LOGIN_UI_TIMEOUT_SECONDS || '0', 10);
const STEAM_LOGIN_UI_TIMEOUT = (Number.isFinite(STEAM_LOGIN_UI_TIMEOUT_SECONDS_VALUE) ? Math.max(0, STEAM_LOGIN_UI_TIMEOUT_SECONDS_VALUE) : 0) * 1000;
// Time to delay between bot start waves. Max concurrent starts controls wave size.
const BOT_START_DELAY_SECONDS_VALUE = Number.parseInt(process.env.CAT_BOT_START_DELAY_SECONDS || '30', 10);
const DELAY_START_TIME = (Number.isFinite(BOT_START_DELAY_SECONDS_VALUE) ? Math.max(0, BOT_START_DELAY_SECONDS_VALUE) : 30) * 1000;
const STEAM_BOOT_CONCURRENCY_VALUE = Number.parseInt(process.env.CAT_STEAM_BOOT_CONCURRENCY || String(config.steam_boot_concurrency || '1'), 10);
const STEAM_BOOT_DELAY_SECONDS_VALUE = Number.parseInt(process.env.CAT_STEAM_BOOT_DELAY_SECONDS || String(config.steam_boot_delay_seconds || '8'), 10);
const STEAM_BOOT_DELAY = (Number.isFinite(STEAM_BOOT_DELAY_SECONDS_VALUE) ? Math.max(0, STEAM_BOOT_DELAY_SECONDS_VALUE) : 8) * 1000;
const GAME_STARTUP_FATAL_PATTERNS = [
    'AppFramework : Unable to load module engine.so!',
    'Unable to load interface VCvarQuery001 from engine.so'
];
const STEAM_STARTUP_FATAL_PATTERNS = [
    'Error: Couldn\'t set up the Steam Runtime.',
    'LD_LIBRARY_PATH: unbound variable'
];
const steam_auth_log_scan_tail_lines = 400;
// Cleared on every Steam launch; must cover auth heuristics and common session logs.
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
    /(?:login|logon|auth|account|error|eresult|result)[^\n]{0,80}\be43\b/i,
    /\be43\b[^\n]{0,80}(?:login|logon|auth|account|error|disabled)/i
];
let navmesh_sync_done = false;
let cathook_configs_ready = false;

const STATE = {
    INITIALIZING: 0,
    INITIALIZED: 1,
    STARTING: 3,
    WAITING: 4,
    RUNNING: 5,
    RESTARTING: 6,
    STOPPING: 7,
    NO_ACCOUNT: 8,
    INVALID_PASSWORD_E5: 9,
    ACCOUNT_DISABLED_E43: 10,
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
        if (!fs.existsSync(file_path))
            return '';

        const lines = fs.readFileSync(file_path, 'utf8').trimEnd().split(/\r?\n/);
        return lines.slice(-line_count).join('\n');
    } catch (error) {
        return `failed to read ${file_path}: ${error.message}`;
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
    try {
        child_process.execFileSync(command, args, { stdio: 'ignore' });
        return true;
    } catch (error) {
        return false;
    }
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


function preload_value(primary_library) {
    const extra_preload = process.env.STEAM_LD_PRELOAD || '';
    return extra_preload ? `${primary_library}:${extra_preload}` : primary_library;
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
        return {
            pid: pid,
            comm: comm,
            ppid: Number.parseInt(fields[1], 10),
            starttime: Number.parseInt(fields[19], 10) || 0,
            cmdline: read_proc_cmdline(pid)
        };
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
    if (!Number.isSafeInteger(STEAM_BOOT_CONCURRENCY_VALUE) || STEAM_BOOT_CONCURRENCY_VALUE < 1)
        return 1;

    return STEAM_BOOT_CONCURRENCY_VALUE;
}

function start_delay_allows_launch(time) {
    if (!DELAY_START_TIME)
        return true;

    if (module.exports.currentlyStartingGames > 0)
        return true;

    return module.exports.lastStartTime + DELAY_START_TIME < time;
}

function steam_boot_delay_allows_launch(time) {
    if (!STEAM_BOOT_DELAY)
        return true;

    if (module.exports.currentlyBootingSteam > 0 && module.exports.currentlyBootingSteam < max_steam_boots())
        return true;

    return module.exports.lastSteamBootTime + STEAM_BOOT_DELAY < time;
}

function steam_login_timeout_seconds() {
    const value = Number.parseInt(config.auto_restart_steam_if_not_logged_within, 10);
    if (!Number.isSafeInteger(value) || value < 0)
        return 300;

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

function read_process_table() {
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

    return processes;
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

function collect_descendant_pids(root_pid, processes) {
    const children_by_parent = new Map();
    for (const info of processes.values()) {
        if (!children_by_parent.has(info.ppid))
            children_by_parent.set(info.ppid, []);
        children_by_parent.get(info.ppid).push(info.pid);
    }

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

function find_main_steamwebhelper(steam_launcher_pid) {
    const processes = read_process_table();
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
        return { main: null, child_pids: [], helper_count: 0 };

    return {
        main: candidates[0],
        child_pids: collect_descendant_pids(candidates[0].pid, processes),
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
        const route = child_process.execFileSync('ip', ['-o', '-4', 'route', 'get', process.env.CATHOOK_NET_PROBE_HOST || '1.1.1.1'], { encoding: 'utf8' }).trim();
        const fields = route.split(/\s+/);
        const dev_index = fields.indexOf('dev');

        if (dev_index !== -1 && dev_index + 1 < fields.length)
            return fields[dev_index + 1];
    } catch (error) { }

    try {
        const default_route = child_process.execFileSync('ip', ['-o', '-4', 'route', 'show', 'default'], { encoding: 'utf8' }).trim();
        const fields = default_route.split(/\s+/);
        const dev_index = fields.indexOf('dev');

        if (dev_index !== -1 && dev_index + 1 < fields.length)
            return fields[dev_index + 1];
    } catch (error) { }

    try {
        return child_process.execSync("route -n | grep '^0\\.0\\.0\\.0' | grep -o '[^ ]*$' | head -n 1", { encoding: 'utf8' }).trim();
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

const USER = { name: process.env.SUDO_USER, uid: Number.parseInt(child_process.execSync("id -u " + process.env.SUDO_USER).toString().trim()), home: child_process.execSync(`printf ~${process.env.SUDO_USER}`).toString(), interface: get_default_network_interface(), SUPPORTS_FJ_NET: true };
if (!USER.interface) {
    USER.SUPPORTS_FJ_NET = false;
} else {
    if (!firejail_network_works(USER.interface))
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

        // Create a network namespace for this bot
        if (!USER.SUPPORTS_FJ_NET)
            child_process.execSync("./scripts/ns-inet " + this.botid)

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

        // Start timestamp
        this.startTime = null;

        this.ipcState = null;
        this.ipcLastHeartbeat = 0;
        this.ipcID = -1;
        this.time_ipc_identity_missing = 0;
        this.time_ipc_peer_missing = 0;

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
        this.lastRestartReason = '';
        this.lastGameKillReason = '';
        this.terminal_auth_state = 0;
        this.clear_steam_webhelper_cache_before_start = false;

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

            self.ipcID = id;
            if (!self.ipcState) {
                self.log(`Assigned IPC ID ${id}`);
                self.schedule_steamwebhelper_cleanup();
            }
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
        this.time_steamStatusLog = 0;
        this.time_steam_boot_status_log = 0;
        this.shouldResetSteam = false;
        this.steamReadyLogged = false;
        this.steam_quick_exit_count = 0;
    }

    log(message) {
        console.log(`[${timestamp('HH:mm:ss')}][${this.name}][${this.state}] ${message}`);
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
                let backup_path = path.resolve(this.steamApps, '..', 'steamapps_old');
                if (fs.existsSync(backup_path))
                    backup_path = path.resolve(this.steamApps, '..', `steamapps_old_${Date.now()}`);
                fs.renameSync(this.steamApps, backup_path);
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

        if (real_source_path === SHARED_STEAMAPPS)
            return true;

        if (command_succeeds('mountpoint', ['-q', SHARED_STEAMAPPS])) {
            if (steamapps_tf2_ready(SHARED_STEAMAPPS))
                return true;

            child_process.execFileSync('umount', [SHARED_STEAMAPPS]);
            this.log(`Unmounted incomplete ${SHARED_STEAMAPPS}`);
        }

        try {
            const status = fs.lstatSync(SHARED_STEAMAPPS);
            if (status.isSymbolicLink()) {
                const current_target = fs.readlinkSync(SHARED_STEAMAPPS);
                rm_path_sync(SHARED_STEAMAPPS, { force: true });
                this.log(`Replacing ${SHARED_STEAMAPPS} symlink (${current_target}) with a bind mount to ${real_source_path}`);
            } else if (steamapps_tf2_ready(SHARED_STEAMAPPS)) {
                return true;
            } else {
                const backup_path = `${SHARED_STEAMAPPS}.backup.${Math.floor(Date.now() / 1000)}`;
                fs.renameSync(SHARED_STEAMAPPS, backup_path);
                this.log(`Moved incomplete ${SHARED_STEAMAPPS} to ${backup_path}`);
            }
        } catch (error) {
            if (error.code !== 'ENOENT')
                throw error;
        }

        fs.mkdirSync(SHARED_STEAMAPPS, { recursive: true });
        child_process.execFileSync('mount', ['--bind', real_source_path, SHARED_STEAMAPPS]);
        this.log(`Shared steamapps ready, source=${real_source_path}, target=${SHARED_STEAMAPPS}, mode=bind`);
        return true;
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

        const target_path = this.botSteamPath(source_path);
        const steam_config_path = path.join(this.home, '.steam');
        if (!steam_root_ready(target_path)) {
            if (fs.existsSync(target_path)) {
                this.log(`Replacing incomplete bot Steam install at ${target_path}`);
                rm_path_sync(target_path, { recursive: true, force: true });
            }
            this.log(`Seeding Steam client into bot home from ${source_path} to ${target_path}`);
            copy_steam_seed(source_path, target_path);
            chown_tree(target_path, USER.uid, USER.uid);
            if (!steam_root_ready(target_path))
                this.log(`[ERROR] Seeded Steam install is still incomplete at ${target_path}`);
        }

        fs.mkdirSync(steam_config_path, { recursive: true });
        for (const link_name of ['steam', 'root']) {
            const link_path = path.join(steam_config_path, link_name);
            if (path.resolve(link_path) === path.resolve(target_path)) {
                try {
                    if (!fs.lstatSync(link_path).isSymbolicLink())
                        continue;
                } catch (error) { }
            }

            const link_target = path.relative(steam_config_path, target_path) || '.';
            try {
                const status = fs.lstatSync(link_path);
                if (!status.isSymbolicLink() || fs.readlinkSync(link_path) !== link_target)
                    rm_path_sync(link_path, { recursive: true, force: true });
            } catch (error) {
                if (error.code !== 'ENOENT')
                    throw error;
            }

            if (!fs.existsSync(link_path))
                fs.symlinkSync(link_target, link_path);
        }
        chown_tree(steam_config_path, USER.uid, USER.uid);
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
                text = fs.readFileSync(log_path, 'utf8');
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
            const matches = [...search_text.matchAll(/\[U:1:(\d+)\]/g)]
                .map((match) => Number.parseInt(match[1], 10))
                .filter((account_id32) => Number.isFinite(account_id32) && account_id32 > 0);
            if (!matches.length)
                continue;

            if (login_ok_position > best_position) {
                best_position = login_ok_position;
                best_account_id32 = matches[matches.length - 1];
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
                const log_text = fs.readFileSync(log_path, 'utf8');
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

    using_per_bot_x_display() {
        return !!this.procXvfb || this.botXvfbAdopted;
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

        // Brief poll so Steam doesn't race against the X socket appearing.
        const x_socket_path = `/tmp/.X11-unix/X${display_num}`;
        const deadline = Date.now() + 3000;
        while (Date.now() < deadline) {
            if (fs.existsSync(x_socket_path))
                break;
            try { child_process.execFileSync('sleep', ['0.05'], { stdio: 'ignore' }); } catch (error) { break; }
        }
        if (!fs.existsSync(x_socket_path))
            this.log(`[WARN] X socket ${x_socket_path} did not appear within 3s; Steam may fail to connect.`);

        return true;
    }

    killXvfb() {
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

    ensureVisibleXauthority() {
        this.xauthorityPath = BOT_XAUTHORITY;

        if (!VISIBLE_WINDOWS)
            return this.xauthorityPath;

        if (!BOT_XAUTHORITY || !fs.existsSync(BOT_XAUTHORITY)) {
            this.log(`Visible windows requested but XAUTHORITY is missing: ${BOT_XAUTHORITY}`);
            return this.xauthorityPath;
        }

        const target_path = path.join(this.home, '.Xauthority');
        try {
            fs.mkdirSync(this.home, { recursive: true });
            fs.copyFileSync(BOT_XAUTHORITY, target_path);
            fs.chownSync(target_path, USER.uid, USER.uid);
            this.xauthorityPath = target_path;
        } catch (error) {
            this.log(`Failed to copy XAUTHORITY for visible windows: ${error.message}`);
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

        return unique_paths(log_paths);
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
        return this.steamLogPaths().filter((log_path) => {
            try {
                return fs.existsSync(log_path) && fs.statSync(log_path).isFile();
            } catch (error) {
                return false;
            }
        });
    }

    clearSteamStartupLogs() {
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
                const log_text = fs.readFileSync(log_path, 'utf8');
                if (steam_startup_log_has_fatal_error(log_text))
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
            if (!['connection_log.txt', 'console_log.txt', 'steamui_login.txt', 'webhelper_js.txt'].includes(path.basename(log_path)))
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

    restart_after_login_error_5(log_path) {
        this.log(`[ERROR] Steam login error 5 detected; restarting Steam immediately. log=${log_path}`);
        this.log_single_steam_tail('Steam login error 5 log tail', log_path);
        this.shouldRestart = true;
        this.time_steamWorking = 0;
        this.killGame();
        this.killSteam();
        this.force_kill_runtime_processes(1000);
    }

    steam_webhelper_stall_log_path() {
        for (var log_path of this.existingSteamLogPaths()) {
            if (!['steamui_html.txt', 'webhelper.txt'].includes(path.basename(log_path)))
                continue;

            try {
                const log_text = fs.readFileSync(log_path, 'utf8');
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
        const candidates = unique_paths([preferredSteamPath, ...this.steamInstallCandidates()]);
        const steam_path = candidates.find(steam_root_ready)
            || candidates.find((candidate) => candidate && fs.existsSync(candidate))
            || path.join(this.home, '.steam/steam');

        this.steamPath = this.botSteamPath(steam_path);
        this.steamApps = path.join(this.steamPath, 'steamapps');

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

    gameLaunchPath() {
        const bot_relative_path = path.relative(this.home, this.tf2Path);
        if (path_is_inside(this.tf2Path, this.home))
            return path.join(USER.home, bot_relative_path);

        return this.tf2Path;
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

    pollSteamReady() {
        const ready_patterns = [
            'System startup time:',
            'Logged in OK',
            'logged in OK',
            'Logged into Steam',
            'logged into Steam',
            'Steam client ready',
            'Steam signed in',
            'Refresh complete'
        ];

        for (var logPath of this.steamLogPaths()) {
            try {
                const log_text = fs.existsSync(logPath) ? fs.readFileSync(logPath, 'utf8') : '';
                if (steam_client_initialized_from_log(log_text)) {
                    this.mark_steam_client_initialized(path.basename(logPath) === 'error.log' ? path.dirname(logPath) : null, log_text);
                    return true;
                }
                if (ready_patterns.some((pattern) => log_text.includes(pattern))) {
                    this.markSteamReady(path.basename(logPath) === 'error.log' ? path.dirname(logPath) : null);
                    return true;
                }
            } catch (error) { }
        }

        return false;
    }

    spawnSteam() {
        var self = this;
        if (self.procFirejailSteam) {
            self.log('[ERROR] Steam is already running!');
            return;
        }

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
        self.ensure_per_bot_x_display();
        const using_per_bot_x = self.using_per_bot_x_display();
        const display_value = (using_per_bot_x && self.botDisplay) ? self.botDisplay : BOT_DISPLAY;
        const xauthority_path = using_per_bot_x ? '' : self.ensureVisibleXauthority();
        if (using_per_bot_x)
            self.xauthorityPath = '';

        var steambin = this.steamLaunchCommand();
        self.time_steam_launch_started = Date.now();

        self.procFirejailSteam = child_process.spawn(([this.shouldResetSteam, this.shouldResetSteam = 0][0] ? LAUNCH_OPTIONS_STEAM_RESET : LAUNCH_OPTIONS_STEAM)
            // Username
            .replace("%LOGIN%", shell_quote(self.account.login))
            // Password
            .replace("%PASSWORD%", shell_quote(self.account.password))
            // Name of the firejail jail
            .replace("%JAILNAME%", shell_quote(self.name))
            .replace("%STEAM_LD_LIBRARY_PATH%", shell_quote(process.env.LD_LIBRARY_PATH || ''))
            .replace("%LD_PRELOAD%", shell_quote(process.env.STEAM_LD_PRELOAD || ''))
            // XOrg Display
            .replace("%DISPLAY%", shell_quote(display_value))
            .replace("%XAUTHORITY%", shell_quote(xauthority_path))
            // Network
            .replace("%NETWORK%", USER.SUPPORTS_FJ_NET ? `--net=${USER.interface}` : `--netns=catbotns${this.botid}`)
            // Home folder
            .replace("%HOME%", self.home.replace(/"/g, '\\"'))
            .replace("%STEAM%", steambin),
            self.spawnOptions);
        self.procFirejailSteam.on('error', (error) => {
            self.log(`[ERROR] Failed to launch Steam/firejail: ${error.message}`);
            self.shouldRun = false;
            self.shouldRestart = false;
            self.isSteamWorking = false;
            self.steamClientInitialized = false;
            delete self.procFirejailSteam;
        });
        self.procFirejailSteam.stdout.on('error', (error) => {
            self.log(`[ERROR] Steam stdout error: ${error.message}`);
        });
        self.procFirejailSteam.stderr.on('error', (error) => {
            self.log(`[ERROR] Steam stderr error: ${error.message}`);
        });
        self.logSteam = fs.createWriteStream('./logs/' + self.name + '.steam.log');
        self.logSteam.on('error', (err) => { self.log(`error on logSteam pipe: ${err}`) });
        self.procFirejailSteam.stdout.pipe(self.logSteam);

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
                this.shouldRestart = true;
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

        self.procFirejailSteam.stderr.on("data", (data) => {
            var text = data.toString();
            processErrorLogs.bind(this)(text);
        });

        self.procFirejailSteam.stdout.on("data", (data) => {
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
                this.shouldRestart = true;
                this.shouldResetSteam = true;
            }
            if (text.includes("Running Steam on"))
                registerSteamErrorLogListeners.bind(this)();
        });
        self.procFirejailSteam.stderr.pipe(self.logSteam);
        self.procFirejailSteam.on('exit', self.handleSteamExit.bind(self));
        self.procFirejailSteam.on('exit', () => {
            for (var i = 0; i < tail_steam_err_logs.length; i++) {
                if (tail_steam_err_logs[i]) {
                    tail_steam_err_logs[i].unwatch();
                    tail_steam_err_logs[i] = null;
                }
            }
            tail_steam_err_logs = [];
            steam_log_listener_paths.clear();
        });
        self.log(`Launched ${steambin} (${self.procFirejailSteam.pid})`);
        self.log(`Steam log capture: ./logs/${self.name}.steam.log plus ${self.steamInstallCandidates().map((steam_path) => path.join(steam_path, 'logs')).join(', ')}`);
        self.emit('start-steam', self.procFirejailSteam.pid);
    }

    spawnGame() {
        var self = this;

        var filename = path.join(this.home, `.gl${makeid(6)}`);
        const source_library = cathook_game_library();
        fs.copyFileSync(source_library, filename);
        fs.chmodSync(filename, 0o755);
        fs.chownSync(filename, USER.uid, USER.uid);
        self.gamePreloadLibrary = filename;

        clearSourceLockFiles();
        if (!self.repairSteamSdk64()) {
            self.shouldRestart = true;
            return;
        }

        const game_launch_path = self.gameLaunchPath();
        const game_binary = self.gameBinary();
        if (!fs.existsSync(path.join(self.tf2Path, 'tf_linux64'))) {
            self.log(`[ERROR] Missing tf_linux64 in ${self.tf2Path}`);
            self.shouldRestart = true;
            return;
        }
        if (!self.validateGameDependencies(game_launch_path)) {
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
        const game_launch_options = TF2_LAUNCH_MODE === 'steam' ? LAUNCH_OPTIONS_GAME_STEAM : LAUNCH_OPTIONS_GAME;
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
                `CAT_STEAMID32=${steamid32}`,
                `LD_PRELOAD="${bash_double_quote_escape(game_preload)}"`,
                `%command%`,
                `-steam -game tf ${GAME_WINDOW_OPTIONS} -novid -nojoy -noipx -nomessagebox -nominidumps -nohltv -nobreakpad -reuse -noquicktime -precachefontchars -particles 1 -snoforceformat -softparticlesdefaultoff ${GAME_MODE_OPTIONS} -forcenovsync -insecure +clientport 27006-27014`
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
            // Firejail jail name used by this users steam
            .replace("%JAILNAME%", self.name)
            // cathook
            .replace("%LD_PRELOAD%", `"${game_preload}"`)
            // XORG display
            .replace("%DISPLAY%", self.botDisplay || BOT_DISPLAY)
            .replace("%XAUTHORITY%", bash_double_quote_escape(self.xauthorityPath || '')),
            [], self.spawnOptions);
        self.procFirejailGame.on('error', (error) => {
            self.log(`[ERROR] Failed to launch TF2/firejail: ${error.message}`);
            self.shouldRestart = true;
            self.removeGamePreloadLibrary();
            delete self.procFirejailGame;
        });
        self.procFirejailGame.stdout.on('error', (error) => {
            self.log(`[ERROR] Game stdout error: ${error.message}`);
        });
        self.procFirejailGame.stderr.on('error', (error) => {
            self.log(`[ERROR] Game stderr error: ${error.message}`);
        });
        self.logGame = fs.createWriteStream('./logs/' + self.name + '.game.log');
        self.logGame.on('error', (err) => { self.log(`error on logGame pipe: ${err}`) });
        self.procFirejailGame.stdout.pipe(self.logGame);
        self.procFirejailGame.stderr.pipe(self.logGame);
        self.procFirejailGame.on('exit', self.handleGameExit.bind(self));
        this.restarts++;
        return true;
    }

    handleSteamExit(code, signal) {
        const steam_process = this.procFirejailSteam;
        const launcher_pid = steam_process ? steam_process.pid : 0;
        this.log(`Steam (${launcher_pid}) exited with code ${code}, signal ${signal}`);
        const steam_runtime = this.time_steam_launch_started ? Date.now() - this.time_steam_launch_started : 0;
        const steam_log_tail = log_file_tail('./logs/' + this.name + '.steam.log', 25);
        if (steam_log_tail)
            this.log(`Steam log tail:\n${steam_log_tail}`);
        const account_disabled_e43_log_path = this.steam_account_disabled_e43_log_path();
        const login_error_5_log_path = this.steam_login_error_5_log_path();
        const invalid_password_log_path = this.steam_invalid_password_log_path();
        if (!this.isSteamWorking && account_disabled_e43_log_path) {
            this.mark_terminal_auth_error(STATE.ACCOUNT_DISABLED_E43, 'ACCOUNT DISABLED E43', account_disabled_e43_log_path);
        }
        else if (!this.isSteamWorking && login_error_5_log_path) {
            this.restart_after_login_error_5(login_error_5_log_path);
        }
        else if (!this.isSteamWorking && invalid_password_log_path) {
            this.mark_terminal_auth_error(STATE.INVALID_PASSWORD_E5, 'INVALID PASSWORD E5', invalid_password_log_path);
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
            this.shouldRestart = true;
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
        this.steamwebhelper_cleanup_done = false;
        this.steamwebhelper_frozen_pid = -1;

        delete this.procFirejailSteam;
    }
    handleGameExit(code, signal) {
        const game_process = this.procFirejailGame;
        const launcher_pid = game_process ? game_process.pid : 0;
        const game_pid = this.gamePid;
        this.log(`Game (${launcher_pid}) exited with code ${code}, signal ${signal}`);
        const game_log_tail = log_file_tail('./logs/' + this.name + '.game.log', 25);
        if (game_log_tail)
            this.log(`Game log tail:\n${game_log_tail}`);
        this.writeGameExitDiagnostics(code, signal, launcher_pid, game_pid, game_log_tail);
        if (!this.ipcState && !this.gameStarted && game_startup_log_has_fatal_error(game_log_tail)) {
            this.log('[ERROR] TF2 exited during startup after failing to load engine.so; stopping this bot instead of restarting in a loop.');
            this.log('Check missing libraries with ldd on tf_linux64 and bin/linux64/engine.so, then run ./install-deps.');
            this.shouldRun = false;
            this.shouldRestart = false;
        }
        const crashed = (code !== null && code !== 0) || signal !== null;
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
        this.steamwebhelper_cleanup_done = false;
        if (this.shouldRun && this.procFirejailSteam)
            this.state = STATE.STARTING;
        delete this.procFirejailGame;
    }

    clear_ipc_state() {
        this.ipcState = null;
        this.ipcID = -1;
        this.ipcLastHeartbeat = 0;
        this.time_ipc_identity_missing = 0;
        this.time_ipc_peer_missing = 0;
    }

    ipc_heartbeat_stale(time) {
        if (!this.ipcState || !this.ipcState.heartbeat || !ipc_heartbeat_stale_timeout)
            return false;

        return time - this.ipcState.heartbeat * 1000 > ipc_heartbeat_stale_timeout;
    }

    ipc_identity_missing() {
        return !this.ipcState || !this.ipcState.friendid || this.ipcState.friendid === 0;
    }

    mark_ipc_peer_missing(time) {
        if (!this.ipcState || this.ipcID < 0 || this.shouldRestart)
            return;

        if (!this.time_ipc_peer_missing)
            this.time_ipc_peer_missing = time;

        if (time - this.time_ipc_peer_missing > 5000)
            this.request_restart(`IPC peer ${this.ipcID} disappeared from server query`);
    }

    steam_boot_in_progress() {
        return this.state === STATE.STARTING && !!this.procFirejailSteam && !this.steamClientInitialized;
    }

    request_restart(reason) {
        this.lastRestartReason = reason || 'unspecified restart';
        this.log(`Restart requested: ${reason}`);
        this.clear_ipc_state();
        this.terminal_auth_state = 0;
        if (this.shouldRun)
            this.shouldRestart = true;
        else
            this.shouldRun = true;
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
        this.time_steamStatusLog = 0;
        this.time_steam_boot_status_log = 0;
        this.shouldRestart = false;
        this.steamReadyLogged = false;
        this.steamClientInitialized = false;
        this.steam_quick_exit_count = 0;
        this.steamwebhelper_cleanup_done = false;
        this.steamwebhelper_frozen_pid = -1;
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
        this.log('Killing steam');
        this.resume_steamwebhelper();
        // Firejail will handle smooth termination
        if (this.procFirejailSteam) {
            const pid = this.procFirejailSteam.pid;
            const generation = ++this.steam_kill_generation;
            try {
                this.procFirejailSteam.kill("SIGINT");
            } catch (error) { }
            this.schedule_forced_runtime_kill('steam', pid, generation);
        }
    }
    killGame(reason) {
        this.lastGameKillReason = reason || this.lastRestartReason || 'panel requested game kill';
        this.log(`Killing game: ${this.lastGameKillReason}`);
        if (this.procFirejailGame) {
            const pid = this.procFirejailGame.pid;
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
        this.shouldRun = true;
        this.shouldRestart = true;
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

    removeGamePreloadLibrary() {
        if (!this.gamePreloadLibrary)
            return;

        const preload_library = this.gamePreloadLibrary;
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
        if (this.gdbSnapshotRunning) {
            this.appendGdbLog(`\n[${new Date().toISOString()}] skipped gdb crash report pid=${pid}; previous report still running\n`);
            this.removeGamePreloadLibrary();
            return;
        }

        this.gdbSnapshotRunning = true;
        this.log(`Writing gdb crash report pid=${pid} log=${this.gdbLogPath()}`);
        this.appendGdbLog(`\n========== ${new Date().toISOString()} crash pid=${pid} code=${code} signal=${signal} ==========\n`);

        const core_path = `/tmp/${this.name}.${pid}.core`;
        const binary_path = path.join(this.gameLaunchPath(), this.gameBinary());
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
            this.removeGamePreloadLibrary();
        });
        gdb.on('exit', (code, signal) => {
            this.appendGdbLog(`\n[gdb exit] code=${code} signal=${signal}\n`);
            this.gdbSnapshotRunning = false;
            this.removeGamePreloadLibrary();
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
        const child_pids = [...result.child_pids].reverse();
        for (const child_pid of child_pids) {
            if (child_pid === this.gamePid)
                continue;

            try {
                process.kill(child_pid, 'SIGKILL');
                killed_count++;
            } catch (error) { }
        }

        this.log(`Killed ${killed_count} steamwebhelper child processes after IPC stayed connected.`);
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

    gameCheck() {
        const game_process = this.findGameProcess();
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

    findGameProcess() {
        if (!this.procFirejailGame)
            return null;

        const processes = read_process_table();
        const game_binary = this.gameBinary();
        const descendant_pids = collect_descendant_pids(this.procFirejailGame.pid, processes);
        const candidates = descendant_pids
            .map((pid) => processes.get(pid))
            .filter((info) => is_game_process(info, game_binary));

        candidates.sort((left, right) => (right.starttime - left.starttime) || (right.pid - left.pid));
        return candidates[0] || null;
    }

    owns_process_pid(pid) {
        if (!this.procFirejailGame || !pid || pid <= 0)
            return false;

        if (pid === this.procFirejailGame.pid)
            return true;

        const processes = read_process_table();
        return collect_descendant_pids(this.procFirejailGame.pid, processes).includes(pid);
    }

    ipc_peer_match_score(id, data) {
        if (!data)
            return 0;

        if (this.shouldRestart || this.state == STATE.STOPPING || this.state == STATE.RESTARTING)
            return 0;

        if (this.ipcID == id)
            return 120;

        if (this.ipcState)
            return 0;

        if (data.pid && this.owns_process_pid(data.pid))
            return 100;

        if (this.startTime && data.starttime && this.startTime == data.starttime)
            return 80;

        if (data.name && data.name === this.name)
            return 60;

        return 0;
    }

    accept_ipc_peer(id, data) {
        if (!data)
            return;

        this.time_ipc_peer_missing = 0;
        if (!this.startTime && data.starttime)
            this.startTime = data.starttime;
        if (data.pid)
            this.gamePid = data.pid;

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
    update() {
        var time = Date.now();
        if (this.shouldRun && !this.shouldRestart) {
            if (this.procFirejailSteam) {
                if (!this.isSteamWorking) {
                    this.refresh_steam_login_timeout(time);
                    this.pollSteamReady();
                    if (this.isSteamWorking)
                        return;

                    const account_disabled_e43_log_path = this.steam_account_disabled_e43_log_path();
                    if (account_disabled_e43_log_path) {
                        this.mark_terminal_auth_error(STATE.ACCOUNT_DISABLED_E43, 'ACCOUNT DISABLED E43', account_disabled_e43_log_path);
                        return;
                    }

                    const login_error_5_log_path = this.steam_login_error_5_log_path();
                    if (login_error_5_log_path) {
                        this.restart_after_login_error_5(login_error_5_log_path);
                        return;
                    }

                    const invalid_password_log_path = this.steam_invalid_password_log_path();
                    if (invalid_password_log_path) {
                        this.mark_terminal_auth_error(STATE.INVALID_PASSWORD_E5, 'INVALID PASSWORD E5', invalid_password_log_path);
                        return;
                    }

                    const fatal_steam_log_path = this.steamFatalStartupLogPath();
                    if (fatal_steam_log_path) {
                        this.log(`[ERROR] Steam startup fatal error detected in ${fatal_steam_log_path}; stopping this bot instead of waiting with ${steam_login_timeout_description()}.`);
                        this.logSteamTails('Steam fatal startup log tail', 12);
                        this.shouldRun = false;
                        this.shouldRestart = false;
                        this.killSteam();
                        return;
                    }

                    const stalled_webhelper_log_path = this.steam_webhelper_stall_log_path();
                    if (stalled_webhelper_log_path) {
                        this.log(`[ERROR] Steam webhelper browser startup stalled in ${stalled_webhelper_log_path}; restarting Steam with a fresh webhelper cache.`);
                        this.log_single_steam_tail('Steam webhelper stall log tail', stalled_webhelper_log_path);
                        this.clear_steam_webhelper_cache_before_start = true;
                        this.shouldRestart = true;
                        this.time_steamWorking = 0;
                        return;
                    }

                    const stalled_login_ui_log_path = this.steam_login_ui_stall_log_path(time);
                    if (stalled_login_ui_log_path) {
                        this.log(`[ERROR] Steam login UI did not appear within ${STEAM_LOGIN_UI_TIMEOUT / 1000} seconds; restarting Steam with a fresh webhelper cache. last_log=${stalled_login_ui_log_path}`);
                        this.log_single_steam_tail('Steam login UI stall log tail', stalled_login_ui_log_path);
                        this.clear_steam_webhelper_cache_before_start = true;
                        this.shouldRestart = true;
                        this.time_steamWorking = 0;
                        return;
                    }

                    if (!this.time_steamStatusLog || time > this.time_steamStatusLog) {
                        const timeout_status = this.time_steamWorking
                            ? `remaining_seconds=${Math.max(0, Math.ceil((this.time_steamWorking - time) / 1000))}`
                            : 'login_timeout=disabled';
                        this.log(`Waiting for Steam login/readiness, ${timeout_status}`);
                        const logs = this.existingSteamLogPaths();
                        this.log(logs.length ? `Visible Steam logs: ${logs.join(', ')}` : 'Visible Steam logs: none yet');
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
                        this.shouldRestart = true;
                        this.time_steamWorking = 0;
                    }
                    return;
                }
                else {
                    if (!this.procFirejailGame) {
                        this.pollSteamReady();
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
                                if (!this.gameCheck()) {
                                    this.shouldRestart = true;
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
                                    if (!this.time_ipc_identity_missing)
                                        this.time_ipc_identity_missing = time;
                                    if (ipc_identity_timeout && time - this.time_ipc_identity_missing > ipc_identity_timeout) {
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
                    if (!this.account) {
                        this.log(`Preparing to restart with account generation ${this.account_generation}...`);
                        this.account = accounts.get(this.botid, this.account_generation);
                        if (!this.account) {
                            this.state = STATE.NO_ACCOUNT;
                            return;
                        }
                        if (this.state == STATE.NO_ACCOUNT)
                            this.state = STATE.INITIALIZED;
                    }
                    const start_slots_available = module.exports.currentlyStartingGames < max_concurrent_bots();
                    const start_delay_elapsed = start_delay_allows_launch(time);
                    const steam_boot_slots_available = module.exports.currentlyBootingSteam < max_steam_boots();
                    const steam_boot_delay_elapsed = steam_boot_delay_allows_launch(time);
                    if (this.account && start_slots_available && steam_boot_slots_available && start_delay_elapsed && steam_boot_delay_elapsed) {
                        module.exports.lastStartTime = time;
                        module.exports.lastSteamBootTime = time;
                        this.state = STATE.STARTING;
                        this.reset();
                        this.spawnSteam();
                        module.exports.currentlyStartingGames++;
                        module.exports.currentlyBootingSteam++;
                        this.time_steam_login_timeout_started = time;
                        this.refresh_steam_login_timeout(time);
                        this.time_steamAssumeReady = TIMEOUT_STEAM_ASSUME_READY ? time + TIMEOUT_STEAM_ASSUME_READY : 0;
                    } else if (this.account && start_slots_available && (!this.time_steam_boot_status_log || time > this.time_steam_boot_status_log)) {
                        this.log(`Waiting for Steam boot slot, active_boots=${module.exports.currentlyBootingSteam}/${max_steam_boots()}`);
                        this.time_steam_boot_status_log = time + 10000;
                    }
                }
            }
        }
        else {
            if (this.procFirejailGame) {
                this.killGame('restart requested while game is running');
            }
            if (this.procFirejailSteam) {
                this.killSteam();
            }
            this.state = this.terminal_auth_state || STATE.STOPPING;
            if (!this.procFirejailSteam && !this.procFirejailGame) {
                this.state = this.terminal_auth_state || (this.shouldRestart ? STATE.RESTARTING : STATE.INITIALIZED);
                this.shouldRestart = false;
            }
            if (this.account)
                this.account = null;
        }
    }

    restart() {
        this.request_restart('manual/API restart');
    }
    stop() {
        this.shouldRun = false;
    }
    full_stop() {
        this.stop();
        const fully_stopped = !(this.procFirejailGame || this.procFirejailSteam);
        // Only tear down the per-bot Xvfb once Steam/game are gone, otherwise Steam loses its display mid-shutdown.
        if (fully_stopped)
            this.killXvfb();
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
module.exports.states = STATE;
Object.defineProperty(module.exports, 'MAX_CONCURRENT_BOTS', {
    get: function() { return max_concurrent_bots(); },
    set: function(value) { config.max_concurrent_bots = value; }
});
