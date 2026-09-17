const ExecQueue = require('./execqueue');
const child_process = require('child_process');
const fs = require('fs');

class InjectManager {
    constructor() {
        this.injectQueue = new ExecQueue(5000);
    }
    injectInternal(pid, callback) {
        console.log(`[INJ] Injecting into ${pid}`);
        var cp = child_process.spawn('bash', ['attach.sh', `${parseInt(pid)}`], { uid: 0, gid: 0 });
        cp.stdout.on('data', (data) => {
            data.toString().trimEnd().split(/\r?\n/).forEach((line) => {
                if (line)
                    console.log(`[INJ:${pid}] ${line}`);
            });
        });
        cp.stderr.on('data', (data) => {
            data.toString().trimEnd().split(/\r?\n/).forEach((line) => {
                if (line)
                    console.log(`[INJ:${pid}] ${line}`);
            });
        });
        var to = setTimeout(function() {
            console.log(`[INJ] Injecting into ${pid} timed out`);
            cp.removeAllListeners('exit');
            cp.kill('SIGKILL');
            callback(new Error('timed out'));
        }, 7500);
        cp.on('exit', function(code, signal) {
            clearTimeout(to);
            if (callback) {
                if (code === 0) {
                    console.log(`[INJ] Injection into process ${pid} successful`);
                    callback(null);
                } else {
                    const reason = code !== null ? `exit code ${code}` : `signal ${signal}`;
                    console.log(`[INJ] Injection into process ${pid} failed with ${reason}`);
                    callback(new Error(reason));
                }
            }
        });
    }
    inject(pid, callback) {
        const max_queue = 32;
        if (this.injectQueue.queue.length >= max_queue) {
            const dropped = this.injectQueue.queue.splice(0, this.injectQueue.queue.length - max_queue + 1);
            dropped.forEach((cb) => cb(new Error('injection queue overflow; request dropped')));
        }
        this.injectQueue.push(this.injectInternal.bind(this, pid, callback));
    }
    injected(pid) {
        try {
            var maps = fs.readFileSync(`/proc/${pid}/maps`).toString();
            return maps.split('\n').some((line) => {
                const name = line.trim().split(/\s+/).pop();
                if (!name)
                    return false;
                const base = name.substring(name.lastIndexOf('/') + 1);
                return base.indexOf('cathook') === 0;
            });
        } catch (error) {
            return false;
        }
    }
}

module.exports = new InjectManager();
