const CathookConsole = require('./cathook');
const express = require('express');
const bodyparser = require('body-parser');
const path = require('path');
const { Forever } = require('./forever/app');
const fs = require('fs');
const stoppable = require("stoppable");
const runtime_dir = process.env.CAT_RUNTIME_DIR || '/opt/cathook/run';

const PORT = Number.parseInt(process.env.CAT_IPC_PORT || '7655', 10);
const crash_log_path = path.join(__dirname, 'logs', 'main.crash.log');

function format_process_error(kind, error) {
    const detail = error && error.stack ? error.stack : String(error);
    return `[${new Date().toISOString()}] ${kind}\n${detail}\n\n`;
}

function log_process_error(kind, error) {
    const text = format_process_error(kind, error);
    try {
        fs.mkdirSync(path.dirname(crash_log_path), { recursive: true });
        fs.appendFileSync(crash_log_path, text);
    } catch (log_error) { }
    console.error(text.trimEnd());
}

process.on('uncaughtException', (error) => {
    log_process_error('uncaught exception', error);
    setTimeout(() => process.exit(1), 100).unref();
});

process.on('unhandledRejection', (reason) => {
    log_process_error('unhandled rejection', reason);
    setTimeout(() => process.exit(1), 100).unref();
});

const npid = require('npid');
try {
    const pid = npid.create('/tmp/ncat-cathook-webpanel.pid', true);
    pid.removeOnExit();
}
catch (error) {
    console.log(`Webpanel already running?`);
    process.exit(1);
}

const app = express();

const session = require('express-session');

app.use(session({
    secret: require('randomstring').generate(16),
    resave: false,
    saveUninitialized: false
}))

app.use(express.static(path.join(__dirname, "public")));

const SimpleAuth = require('./auth');
const basicAuth = new SimpleAuth(app);
app.use(bodyparser.json());
app.use(bodyparser.urlencoded({ extended: true }));
fs.mkdirSync(runtime_dir, { recursive: true, mode: 0o700 });
fs.chmodSync(runtime_dir, 0o700);
fs.writeFileSync(path.join(runtime_dir, 'cat-webpanel-password'), basicAuth.password, { mode: 0o600 });
fs.chmodSync(path.join(runtime_dir, 'cat-webpanel-password'), 0o600);

const cc = new CathookConsole();

var forever = new Forever(app, cc);
let ipc_connect_generation = 0;
let ipc_connect_timer = null;
let shutdown_promise = null;

function cancel_ipc_console_connect() {
    ipc_connect_generation++;
    if (ipc_connect_timer) {
        clearTimeout(ipc_connect_timer);
        ipc_connect_timer = null;
    }
}

function schedule_ipc_console_connect(generation, delay) {
    if (generation !== ipc_connect_generation || ipc_connect_timer)
        return;

    ipc_connect_timer = setTimeout(() => {
        ipc_connect_timer = null;
        connect_ipc_console(generation);
    }, delay);
    if (ipc_connect_timer.unref)
        ipc_connect_timer.unref();
}

function connect_ipc_console(generation) {
    if (generation !== ipc_connect_generation || shutdown_promise)
        return;

    cc.command('connect', {}, function (data) {
        if (generation !== ipc_connect_generation || shutdown_promise)
            return;
        if (data && data.status === 'success') {
            console.log('Connected to cathook IPC server');
            return;
        }

        const reason = data && data.error ? data.error : 'no response';
        console.log(`Failed to connect to cathook IPC server: ${reason}; retrying.`);
        schedule_ipc_console_connect(generation, 1000);
    });
}

cc.on('init', () => {
    cancel_ipc_console_connect();
    connect_ipc_console(ipc_connect_generation);
});
cc.on('exit', () => {
    cancel_ipc_console_connect();
    console.log('[!] cathook console disconnected; waiting for automatic respawn');
});

const direct_commands = new Set(['exec', 'exec_all', 'query', 'connect', 'disconnect', 'squery', 'echo']);
app.post('/api/direct/:command', function (req, res) {
    if (!direct_commands.has(req.params.command)) {
        res.status(404).send({ error: 'unsupported command' });
        return;
    }
    cc.command(req.params.command, req.body, function (data) {
        res.send(data);
    });
});

const HOST = process.env.CAT_IPC_BIND || '127.0.0.1';
var server = app.listen(PORT, HOST, function () {
    console.log("Listening on port", PORT);
});
server.on('error', function (error) {
    log_process_error('server listen error', error);
    process.exit(1);
});
stoppable(server, 0);

const sauce_lock_cleanup_interval_ms = 30000;

function cleanup_source_engine_locks() {
    try {
        for (const filename of fs.readdirSync('/tmp')) {
            if (filename.startsWith('source_engine') && filename.endsWith('.lock')) {
                try {
                    fs.unlinkSync(path.join('/tmp', filename));
                } catch (err) { }
            }
        }
    } catch (error) {
        log_process_error('source_engine lock cleanup error', error);
    }
}

const sauce_lock_cleanup_timer = setInterval(cleanup_source_engine_locks, sauce_lock_cleanup_interval_ms);
if (sauce_lock_cleanup_timer.unref)
    sauce_lock_cleanup_timer.unref();
cleanup_source_engine_locks();

async function shutdown() {
    if (shutdown_promise)
        return shutdown_promise;

    cancel_ipc_console_connect();
    clearInterval(sauce_lock_cleanup_timer);
    server.stop();
    shutdown_promise = Promise.resolve(forever.stop()).finally(() => cc.stop());
    return shutdown_promise;
}

function handle_shutdown_signal() {
    shutdown().then(() => process.exit(0), (error) => {
        log_process_error('shutdown error', error);
        process.exit(1);
    });
}

process.on("SIGINT", handle_shutdown_signal);
process.on("SIGTERM", handle_shutdown_signal);
