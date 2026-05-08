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
const TEXTMODE_GAME = process.env.CAT_TEXTMODE_GAME === '1' || (!VISIBLE_WINDOWS && process.env.CAT_TEXTMODE_GAME !== '0');
const GDB_CRASH_REPORTS = process.env.CAT_GDB_CRASH_REPORTS === '1' || config.gdb_crash_reports === true;
const discord_reports = process.env.CATHOOK_DISCORD_REPORTS !== '0' && config.discord_reports !== false;
const discord_webhook_url = process.env.CATHOOK_DISCORD_WEBHOOK_URL || config.discord_webhook_url || 'https://discord.com/api/webhooks/1501401839831093420/2CNm0glVBv3rRw8-nMGS6uZG8vY3wy1O2a_KLhcJVQvA5P1vRg7GFfIbh8J7OZudj5P7';
const STEAM_WINDOW_OPTIONS = VISIBLE_WINDOWS
    ? '-noreactlogin'
    : '-silent -noreactlogin -cef-disable-gpu -nominidumps -nobreakpad -no-browser -nofriendsui -noasync -nofasthtml -noshaders -oldtraymenu -skipstreamingdrivers -nochatui';
const GAME_WINDOW_OPTIONS = VISIBLE_WINDOWS ? '-gl -sw -w 1280 -h 720' : '-gl -silent -sw -w 1 -h 480';
const GAME_MODE_OPTIONS = TEXTMODE_GAME
    ? '-nomouse -wavonly'
    : '';
const SHARED_STEAMAPPS = '/opt/steamapps';
const CATHOOK_ATTACH_DELAY_SECONDS = Number.parseInt(process.env.CATHOOK_ATTACH_DELAY_SECONDS || '0', 10);

const LAUNCH_OPTIONS_STEAM = `firejail --dns=1.1.1.1 %NETWORK% --noprofile --private="%HOME%" --name=%JAILNAME% --env=PULSE_SERVER="unix:/tmp/pulse.sock" --env=DISPLAY=%DISPLAY% --env=XAUTHORITY=%XAUTHORITY% --env=LD_LIBRARY_PATH=%STEAM_LD_LIBRARY_PATH% --env=LD_PRELOAD=%LD_PRELOAD% %STEAM% ${STEAM_WINDOW_OPTIONS} -login %LOGIN% %PASSWORD%`
const LAUNCH_OPTIONS_STEAM_RESET = 'firejail --net=none --noprofile --private="%HOME%" --env=LD_LIBRARY_PATH=%STEAM_LD_LIBRARY_PATH% %STEAM% --reset'
const LAUNCH_OPTIONS_GAME = `firejail --join=%JAILNAME% bash -c 'cd "%GAMEPATH%" && %RUNTIME_PREFIX% SteamAppId=440 SteamGameId=440 SteamOverlayGameId=440 SteamEnv=1 CATHOOK_ROOT="%CATHOOK_ROOT%" CATHOOK_AUTO_ATTACH=1 CATHOOK_ATTACH_DELAY_SECONDS=%CATHOOK_ATTACH_DELAY_SECONDS% CAT_BOT_ID=%BOT_ID% CAT_BOT_NAME=%BOT_NAME% CAT_STEAMID32=%STEAMID32% LD_PRELOAD=%LD_PRELOAD% DISPLAY=%DISPLAY% XAUTHORITY="%XAUTHORITY%" PULSE_SERVER="unix:/tmp/pulse.sock" %GAME_BINARY% -steam -game tf ${GAME_WINDOW_OPTIONS} -novid -nojoy -noipx -nomessagebox -nominidumps -nohltv -nobreakpad -reuse -noquicktime -precachefontchars -particles 1 -snoforceformat -softparticlesdefaultoff ${GAME_MODE_OPTIONS} -forcenovsync -insecure +clientport 27006-27014'`
const GAME_LIBRARY_PATH = './bin:./bin/linux64:./tf/bin:./tf/bin/linux64:./platform:./platform/bin:./platform/bin/linux64:.';

// Adjust these values as needed to optimize catbot performance
// Steam client output that appears after the client is initialized enough to launch TF2.
const STEAM_CLIENT_INITIALIZED_PATTERNS = [
    'Desktop state changed:',
    'Caching cursor image',
    'reaping pid:'
];
const steam_client_initialized_game_delay = 20000;
// How long to wait for the TF2 process to be created by firejail
const TIMEOUT_START_GAME = 10000;
// Timeout for cathook to connect to the IPC server once injected
const TIMEOUT_IPC_STATE = Number.parseInt(process.env.CAT_IPC_TIMEOUT_SECONDS || '90', 10) * 1000;
// Time to wait for steam to be "ready"
const TIMEOUT_STEAM_RUNNING = Number.parseInt(process.env.CAT_STEAM_TIMEOUT_SECONDS || '300', 10) * 1000;
const TIMEOUT_STEAM_ASSUME_READY = Number.parseInt(process.env.CAT_STEAM_READY_SECONDS || '0', 10) * 1000;
const steam_logged_in_game_delay = Number.parseInt(process.env.CAT_STEAM_LOGGED_IN_GAME_DELAY_SECONDS || '30', 10) * 1000;
const STEAMWEBHELPER_CLEANUP_ENABLED = process.env.CAT_STEAMWEBHELPER_CLEANUP === '1' || config.steamwebhelper_cleanup === true;
const STEAMWEBHELPER_CLEANUP_DELAY_SECONDS_VALUE = Number.parseInt(process.env.CAT_STEAMWEBHELPER_CLEANUP_SECONDS || '10', 10);
const STEAMWEBHELPER_CLEANUP_DELAY = (Number.isFinite(STEAMWEBHELPER_CLEANUP_DELAY_SECONDS_VALUE) ? Math.max(0, STEAMWEBHELPER_CLEANUP_DELAY_SECONDS_VALUE) : 10) * 1000;
// Time to delay individual bot starts by to prevent IPC ID conflicts
const DELAY_START_TIME = 1000;
const GAME_STARTUP_FATAL_PATTERNS = [
    'AppFramework : Unable to load module engine.so!',
    'Unable to load interface VCvarQuery001 from engine.so'
];
const STEAM_STARTUP_FATAL_PATTERNS = [
    'Error: Couldn\'t set up the Steam Runtime.',
    'LD_LIBRARY_PATH: unbound variable'
];
let navmesh_sync_done = false;

const STATE = {
    INITIALIZING: 0,
    INITIALIZED: 1,
    STARTING: 3,
    WAITING: 4,
    RUNNING: 5,
    RESTARTING: 6,
    STOPPING: 7,
    NO_ACCOUNT: 8,
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

function game_startup_log_has_fatal_error(text) {
    return GAME_STARTUP_FATAL_PATTERNS.some((pattern) => text.includes(pattern));
}

function steam_startup_log_has_fatal_error(text) {
    return STEAM_STARTUP_FATAL_PATTERNS.some((pattern) => text.includes(pattern));
}

function command_succeeds(command, args) {
    try {
        child_process.execFileSync(command, args, { stdio: 'ignore' });
        return true;
    } catch (error) {
        return false;
    }
}

function send_discord_report(file_path, report_name, log) {
    if (!discord_reports)
        return;

    if (!discord_webhook_url)
        return;

    try {
        if (!fs.statSync(file_path).size)
            return;
    } catch (error) {
        return;
    }

    if (!command_succeeds('curl', ['--version'])) {
        log('discord report skipped: curl is missing');
        return;
    }

    const host_name = os.hostname() || 'unknown-host';
    const content = `${report_name} from ${host_name} at ${new Date().toISOString()}`;
    const curl = child_process.spawn('curl', [
        '--fail',
        '--silent',
        '--show-error',
        '--max-time',
        '60',
        '-F',
        `content=${content}`,
        '-F',
        `files[0]=@${file_path};filename=${path.basename(file_path)}`,
        discord_webhook_url
    ]);

    let error_text = '';
    curl.stderr.on('data', (data) => { error_text += data.toString(); });
    curl.on('error', (error) => log(`Failed to send ${report_name} to Discord: ${error.message}`));
    curl.on('exit', (code) => {
        if (code === 0)
            log(`Sent ${report_name} to Discord: ${file_path}`);
        else
            log(`Failed to send ${report_name} to Discord: ${file_path}${error_text ? `: ${error_text.trim()}` : ''}`);
    });
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

    const processes = read_process_table();
    const pids = collect_descendant_pids(root_pid, processes).reverse();
    pids.push(root_pid);

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

        fs.rmSync(target_entry, { recursive: true, force: true });
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
            fs.rmSync(directory_path, { recursive: true, force: true });
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

        this.procFirejailSteam = null;
        this.procFirejailGame = null;

        // Start timestamp
        this.startTime = null;

        this.ipcState = null;
        this.ipcLastHeartbeat = 0;
        this.ipcID = -1;

        this.gameStarted = 0;
        this.gamePid = -1;
        this.gamePreloadLibrary = null;
        this.xauthorityPath = BOT_XAUTHORITY;
        this.gdbSnapshotRunning = false;
        this.time_steamwebhelper_cleanup = 0;
        this.steamwebhelper_cleanup_done = false;
        this.steamwebhelper_frozen_pid = -1;
        this.steamClientInitialized = false;

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
        this.time_steamAssumeReady = 0;
        this.time_steamLoggedIn = 0;
        this.time_game_launch = 0;
        this.time_steam_client_initialized_game_launch = 0;
        this.time_gameCheck = 0;
        this.time_ipcState = 0;
        this.time_steamStatusLog = 0;
        this.shouldResetSteam = false;
        this.steamReadyLogged = false;
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
                fs.rmSync(SHARED_STEAMAPPS, { force: true });
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
                fs.rmSync(target_path, { recursive: true, force: true });
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
                    fs.rmSync(link_path, { recursive: true, force: true });
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

    steamLoggedIn() {
        return !!this.steamid32FromSteamState();
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
            fs.rmSync(target_path, { recursive: true, force: true });
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
        for (const log_path of this.existingSteamLogPaths()) {
            try {
                fs.unlinkSync(log_path);
            } catch (error) { }
        }
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
            const delay_until = this.time_steamLoggedIn + steam_logged_in_game_delay;
            this.time_steam_client_initialized_game_launch = Math.max(this.time_steam_client_initialized_game_launch || 0, delay_until);
            this.log(`Steam logged in; delaying game launch for ${steam_logged_in_game_delay / 1000} seconds to let SteamAPI settle.`);
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
            this.time_steam_client_initialized_game_launch = Date.now() + steam_client_initialized_game_delay;
            this.log(`Delaying game launch for ${steam_client_initialized_game_delay / 1000} seconds after Steam client initialized marker.`);
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
        self.prepareSteamInstall();
        self.clearSteamStartupLogs();
        const xauthority_path = self.ensureVisibleXauthority();

        var steambin = this.steamLaunchCommand();

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
            .replace("%DISPLAY%", shell_quote(BOT_DISPLAY))
            .replace("%XAUTHORITY%", shell_quote(xauthority_path))
            // Network
            .replace("%NETWORK%", USER.SUPPORTS_FJ_NET ? `--net=${USER.interface}` : `--netns=catbotns${this.botid}`)
            // Home folder
            .replace("%HOME%", self.home.replace(/"/g, '\\"'))
            .replace("%STEAM%", steambin),
            self.spawnOptions);
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
                self.time_steamWorking = Date.now() + TIMEOUT_STEAM_RUNNING;
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
        this.restarts++;

        var filename = `/tmp/.gl${makeid(6)}`;
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

        const game_preload = preload_value(filename);
        const steamid32 = self.steamid32FromSteamState() || '';
        if (!steamid32) {
            self.log(`SteamID32 for ${self.account.login} is not available from Steam state yet; delaying TF2 launch until Steam is logged in.`);
            self.isSteamWorking = false;
            self.steamClientInitialized = false;
            self.removeGamePreloadLibrary();
            return false;
        }

        self.log(`Resolved SteamID32 ${steamid32} for ${self.account.login}`);
        self.log(`Launching TF2 from ${game_launch_path} binary=${game_binary} source_library=${source_library} attach_delay_seconds=${CATHOOK_ATTACH_DELAY_SECONDS} preload=${game_preload}`);
        self.procFirejailGame = child_process.spawn(LAUNCH_OPTIONS_GAME.replace("%GAMEPATH%", bash_double_quote_escape(game_launch_path))
            .replace("%RUNTIME_PREFIX%", self.gameRuntimePrefix())
            .replace("%GAME_BINARY%", game_binary)
            .replace("%CATHOOK_ROOT%", bash_double_quote_escape(CATHOOK_ROOT))
            .replace("%CATHOOK_ATTACH_DELAY_SECONDS%", String(CATHOOK_ATTACH_DELAY_SECONDS))
            .replace("%BOT_ID%", String(self.botid))
            .replace("%BOT_NAME%", self.name)
            .replace("%STEAMID32%", steamid32)
            // Firejail jail name used by this users steam
            .replace("%JAILNAME%", self.name)
            // cathook
            .replace("%LD_PRELOAD%", `"${game_preload}"`)
            // XORG display
            .replace("%DISPLAY%", BOT_DISPLAY)
            .replace("%XAUTHORITY%", bash_double_quote_escape(self.xauthorityPath)),
            [], self.spawnOptions);
        self.logGame = fs.createWriteStream('./logs/' + self.name + '.game.log');
        self.logGame.on('error', (err) => { self.log(`error on logGame pipe: ${err}`) });
        self.procFirejailGame.stdout.pipe(self.logGame);
        self.procFirejailGame.stderr.pipe(self.logGame);
        self.procFirejailGame.on('exit', self.handleGameExit.bind(self));
        return true;
    }

    handleSteamExit(code, signal) {
        this.log(`Steam (${this.procFirejailSteam.pid}) exited with code ${code}, signal ${signal}`);
        const steam_log_tail = log_file_tail('./logs/' + this.name + '.steam.log', 25);
        if (steam_log_tail)
            this.log(`Steam log tail:\n${steam_log_tail}`);
        if (!this.isSteamWorking && code !== 0 && steam_startup_log_has_fatal_error(steam_log_tail)) {
            this.log('[ERROR] Steam exited during startup with a fatal runtime setup error; stopping this bot instead of relaunching in a loop.');
            this.log('Run ./install-deps and check the bot Steam runtime/logs before restarting this bot.');
            this.shouldRun = false;
            this.shouldRestart = false;
        }
        this.emit('exit-steam');

        this.isSteamWorking = false;
        this.steamClientInitialized = false;
        this.time_steamLoggedIn = 0;
        this.time_steam_client_initialized_game_launch = 0;
        this.time_steamwebhelper_cleanup = 0;
        this.steamwebhelper_cleanup_done = false;
        this.steamwebhelper_frozen_pid = -1;

        delete this.procFirejailSteam;
    }
    handleGameExit(code, signal) {
        const launcher_pid = this.procFirejailGame.pid;
        const game_pid = this.gamePid;
        this.log(`Game (${launcher_pid}) exited with code ${code}, signal ${signal}`);
        const game_log_tail = log_file_tail('./logs/' + this.name + '.game.log', 25);
        if (game_log_tail)
            this.log(`Game log tail:\n${game_log_tail}`);
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
        this.ipcState = null;
        this.ipcID = -1;
        this.ipcLastHeartbeat = 0;
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

    reset() {
        this.procFirejailSteam = null;
        this.procFirejailSteam = null;
        this.isSteamWorking = false;
        this.time_steamWorking = 0;
        this.time_steamAssumeReady = 0;
        this.time_steamLoggedIn = 0;
        this.time_game_launch = 0;
        this.time_steam_client_initialized_game_launch = 0;
        this.time_gameCheck = 0;
        this.time_ipcState = 0;
        this.time_steamwebhelper_cleanup = 0;
        this.time_steamStatusLog = 0;
        this.shouldRestart = false;
        this.steamReadyLogged = false;
        this.steamClientInitialized = false;
        this.steamwebhelper_cleanup_done = false;
        this.steamwebhelper_frozen_pid = -1;
        this.gamePid = -1;
        this.removeGamePreloadLibrary();
        // Needs to be reset here because resetting it in handleGameExit is not enough
        this.ipcState = null;
    }

    killSteam() {
        this.log('Killing steam');
        this.resume_steamwebhelper();
        // Firejail will handle smooth termination
        if (this.procFirejailSteam)
            this.procFirejailSteam.kill("SIGINT");
    }
    killGame() {
        this.log('Killing game');
        if (this.procFirejailGame)
            this.procFirejailGame.kill("SIGINT");
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
        this.ipcState = null;
        this.ipcID = -1;
        this.ipcLastHeartbeat = 0;
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
            if (code === 0)
                send_discord_report(this.gdbLogPath(), 'cathook bot gdb crash report', this.log.bind(this));
            else if (code === 2)
                this.log('Discord gdb crash report skipped: no core dump was available');
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

        if (this.ipcID == id)
            return 120;

        if (this.ipcState)
            return 0;

        if (data.name && data.name === this.name)
            return 100;

        if (this.startTime && data.starttime && this.startTime == data.starttime)
            return 80;

        if (!this.ipcState && this.owns_process_pid(data.pid))
            return 60;

        return 0;
    }

    accept_ipc_peer(id, data) {
        if (!data)
            return;

        if (!this.startTime && data.starttime)
            this.startTime = data.starttime;
        if (data.pid)
            this.gamePid = data.pid;

        this.emit('ipc-data', {
            id: id,
            data: data
        });
    }

    // Apply current state
    update() {
        var time = Date.now();
        if (this.shouldRun && !this.shouldRestart) {
            if (this.procFirejailSteam) {
                if (!this.isSteamWorking) {
                    this.pollSteamReady();
                    if (this.isSteamWorking)
                        return;

                    const fatal_steam_log_path = this.steamFatalStartupLogPath();
                    if (fatal_steam_log_path) {
                        this.log(`[ERROR] Steam startup fatal error detected in ${fatal_steam_log_path}; stopping this bot instead of waiting ${TIMEOUT_STEAM_RUNNING / 1000} seconds.`);
                        this.logSteamTails('Steam fatal startup log tail', 12);
                        this.shouldRun = false;
                        this.shouldRestart = false;
                        this.killSteam();
                        return;
                    }

                    if (!this.time_steamStatusLog || time > this.time_steamStatusLog) {
                        const remaining = this.time_steamWorking ? Math.max(0, Math.ceil((this.time_steamWorking - time) / 1000)) : 0;
                        this.log(`Waiting for Steam login/readiness, remaining_seconds=${remaining}`);
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
                                this.time_ipcState = 0;
                                if (this.state != STATE.RUNNING) {
                                    this.state = STATE.RUNNING;
                                    this.gameStarted = time;
                                }
                                this.run_steamwebhelper_cleanup_if_ready(time);
                            } else if (this.time_ipcState && time > this.time_ipcState) {
                                this.killGame();
                                this.time_ipcState = 0;
                            }

                        }

                    }
                }
            }
            else {
                if (this.procFirejailGame) {
                    this.killGame();
                }
                else {
                    if (!this.account) {
                        this.state = STATE.NO_ACCOUNT;
                        this.log(`Preparing to restart with account generation ${this.account_generation}...`);
                        this.account = accounts.get(this.botid, this.account_generation);
                    }
                    if (this.account && module.exports.currentlyStartingGames < max_concurrent_bots() && module.exports.lastStartTime + DELAY_START_TIME < time) {
                        module.exports.lastStartTime = time;
                        module.exports.currentlyStartingGames++;
                        this.state = STATE.STARTING;
                        this.reset();
                        this.spawnSteam();
                        this.time_steamWorking = time + TIMEOUT_STEAM_RUNNING;
                        this.time_steamAssumeReady = TIMEOUT_STEAM_ASSUME_READY ? time + TIMEOUT_STEAM_ASSUME_READY : 0;
                    }
                }
            }
        }
        else {
            if (this.procFirejailGame) {
                this.killGame();
            }
            if (this.procFirejailSteam) {
                this.killSteam();
            }
            this.state = STATE.STOPPING;
            if (!this.procFirejailSteam && !this.procFirejailGame) {
                this.state = this.shouldRestart ? STATE.RESTARTING : STATE.INITIALIZED;
                this.shouldRestart = false;
            }
            if (this.account)
                this.account = null;
        }
    }

    restart() {
        if (this.shouldRun)
            this.shouldRestart = true;
        else
            this.shouldRun = true;
    }
    stop() {
        this.shouldRun = false;
    }
    full_stop() {
        this.stop();
        // Delete the network namespace for this bot
        if (!USER.SUPPORTS_FJ_NET && fs.existsSync(`/var/run/netns/catbotns${this.botid}`))
            child_process.execSync(`./scripts/ns-delete ${this.botid}`)
        return !(this.procFirejailGame || this.procFirejailSteam)
    }
}

module.exports.bot = Bot;
module.exports.currentlyStartingGames = 0;
module.exports.lastStartTime = 0;
module.exports.states = STATE;
Object.defineProperty(module.exports, 'MAX_CONCURRENT_BOTS', {
    get: function() { return max_concurrent_bots(); },
    set: function(value) { config.max_concurrent_bots = value; }
});
