const child_process = require('child_process');
const EventEmitter = require('events');
const extend = require('extend');

const PUPHOOK_ROOT = process.env.PUPHOOK_ROOT || '/opt/puphook';
const CONSOLE_PATH = `${PUPHOOK_ROOT}/ipc/bin/console`;
const IPC_COMMAND_TIMEOUT_BASE = Number.parseInt(process.env.PUP_IPC_COMMAND_TIMEOUT_SECONDS || '30', 10) * 1000;
const IPC_COMMAND_TIMEOUT_MAX = Number.parseInt(process.env.PUP_IPC_COMMAND_TIMEOUT_MAX_SECONDS || '120', 10) * 1000;
const IPC_RESPAWN_DELAY_MS = Number.parseInt(process.env.PUP_IPC_CONSOLE_RESPAWN_MS || '2000', 10);
const MAX_CONSECUTIVE_TIMEOUTS = Math.max(1, Number.parseInt(process.env.PUP_IPC_MAX_CONSECUTIVE_TIMEOUTS || '3', 10));

class PuphookConsole extends EventEmitter {
    constructor() {
        super();
        this.setMaxListeners(512);
        this.init = false;
        this.next_cmdid = 1;
        this.pending_commands = [];
        this.command_in_flight = false;
        this.in_flight_entry = null;
        this.respawn_timer = null;
        this.respawning = false;
        this.stopping = false;
        this.stdout_buffer = '';
        this.consecutive_timeouts = 0;
        this.spawn_process();
        this.on('data', (data) => {
            if (!data)
                return;
            if (data.init) {
                this.init = true;
                this.emit('init');
            }
            if (data.cmdid !== undefined) {
                this.consecutive_timeouts = 0;
            }
        });
    }

    ipc_command_timeout_ms() {
        const queued = this.pending_commands.length + (this.command_in_flight ? 1 : 0);
        const scaled = IPC_COMMAND_TIMEOUT_BASE + queued * 2000;
        return Math.min(IPC_COMMAND_TIMEOUT_MAX, scaled);
    }

    spawn_process() {
        var self = this;
        this.init = false;
        this.stdout_buffer = '';
        this.process = child_process.spawn(CONSOLE_PATH);
        this.process.on('error', function (error) {
            self.init = false;
            console.log('[!] failed to start puphook console:', error.message);
            if (!self.stopping)
                self.schedule_respawn();
        });
        this.process.on('exit', function (code) {
            self.init = false;
            self.fail_in_flight_command('puphook console exited');
            console.log('[!] puphook console exited with code', code);
            self.emit('exit');
            if (!self.respawning && !self.stopping)
                self.schedule_respawn();
        });
        this.process.stdin.on('error', function (error) {
            console.log('[!] puphook console stdin error:', error.message);
        });
        this.process.stdout.on('data', function (data) {
            self.stdout_buffer += data.toString();
            var newline_index = self.stdout_buffer.indexOf('\n');
            while (newline_index !== -1) {
                const line = self.stdout_buffer.slice(0, newline_index);
                self.stdout_buffer = self.stdout_buffer.slice(newline_index + 1);
                newline_index = self.stdout_buffer.indexOf('\n');
                if (!line)
                    continue;

                try {
                    const clean_line = line.replace(/[\uFFFD\uFFFE\uFFFF]/g, '');
                    const parsed = JSON.parse(clean_line);
                    self.emit('data', parsed);
                } catch (e) {
                    console.log('Error parsing IPC data:', e.message);
                    console.log('Raw buffer length:', line.length);
                    self.emit('data', null);
                }
            }
        });
    }

    schedule_respawn() {
        if (this.stopping || this.respawn_timer)
            return;

        this.respawn_timer = setTimeout(() => {
            this.respawn_timer = null;
            this.respawn();
        }, IPC_RESPAWN_DELAY_MS);
        if (this.respawn_timer.unref)
            this.respawn_timer.unref();
    }

    respawn(reason = 'puphook console respawning') {
        if (this.stopping)
            return;
        this.respawning = true;
        this.consecutive_timeouts = 0;
        this.settle_entry(this.in_flight_entry, { status: 'error', error: reason });
        if (this.process) {
            try {
                this.process.removeAllListeners();
                this.process.kill();
            } catch (error) { }
            this.process = null;
        }

        this.fail_queued_commands('puphook console respawning');
        this.spawn_process();
        this.respawning = false;
        this.flush_command_queue();
    }

    fail_in_flight_command(reason) {
        this.settle_entry(this.in_flight_entry, { status: 'error', error: reason });
    }

    fail_queued_commands(reason) {
        while (this.pending_commands.length) {
            const entry = this.pending_commands.shift();
            if (entry && entry.callback)
                entry.callback({ status: 'error', error: reason });
        }
    }

    flush_command_queue() {
        if (this.stopping || this.respawning || this.command_in_flight || !this.pending_commands.length)
            return;
        if (!this.process || !this.process.stdin || this.process.stdin.destroyed) {
            this.fail_queued_commands('puphook console is not running');
            return;
        }

        const entry = this.pending_commands.shift();
        this.command_in_flight = true;
        this.in_flight_entry = entry;
        const payload = extend({}, entry.data || {}, { command: entry.cmd });
        let callback_timeout = null;
        if (entry.callback) {
            const cmdid = String(this.next_cmdid++);
            payload.cmdid = cmdid;
            const handler = (response) => {
                if (!response || response.cmdid !== cmdid)
                    return;
                this.settle_entry(entry, response, handler);
            };
            entry.handler = handler;
            this.on('data', handler);
            callback_timeout = setTimeout(() => {
                console.log('[!] puphook console command timed out (cmdid', cmdid + ')');
                this.consecutive_timeouts += 1;
                this.settle_entry(entry, { status: 'error', error: 'puphook console command timed out' }, entry.handler);
                if (this.consecutive_timeouts >= MAX_CONSECUTIVE_TIMEOUTS) {
                    console.log('[!] puphook console unresponsive after', this.consecutive_timeouts, 'timeouts; respawning');
                    this.respawn('puphook console unresponsive');
                }
            }, this.ipc_command_timeout_ms());
            entry.timeout = callback_timeout;
            if (callback_timeout.unref)
                callback_timeout.unref();
        } else {
            this.command_in_flight = false;
        }

        try {
            this.process.stdin.write(JSON.stringify(payload) + '\n');
            if (!entry.callback)
                this.flush_command_queue();
        } catch (error) {
            this.settle_entry(entry, { status: 'error', error: error.message }, entry.handler);
            console.log('[!] puphook console write failed:', error.message);
            this.flush_command_queue();
        }
    }

    settle_entry(entry, response, handler) {
        if (!entry || entry.settled)
            return false;
        entry.settled = true;
        if (handler)
            this.removeListener('data', handler);
        if (entry.timeout)
            clearTimeout(entry.timeout);
        if (this.in_flight_entry === entry) {
            this.command_in_flight = false;
            this.in_flight_entry = null;
        }
        if (entry.callback)
            entry.callback(response);
        this.flush_command_queue();
        return true;
    }

    command(cmd, data, callback) {
        if (this.stopping) {
            if (callback)
                callback({ status: 'error', error: 'puphook console stopping' });
            return;
        }
        this.pending_commands.push({ cmd: cmd, data: data, callback: callback });
        this.flush_command_queue();
    }

    stop() {
        this.stopping = true;
        if (this.respawn_timer) {
            clearTimeout(this.respawn_timer);
            this.respawn_timer = null;
        }
        this.settle_entry(this.in_flight_entry, { status: 'error', error: 'puphook console stopping' });
        this.fail_queued_commands('puphook console stopping');
        if (this.process) {
            try {
                this.process.kill('SIGTERM');
            } catch (error) { }
            this.process = null;
        }
    }
}

module.exports = PuphookConsole;
