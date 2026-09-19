const EventEmitter = require('events');
const fs = require('fs');

class Process extends EventEmitter {
    constructor(pid, uid, name) {
        super();
        this.pid = pid;
        this.uid = uid;
        this.name = name;
    }
}

class ProcEvents extends EventEmitter {
    constructor() {
        super();
        this.cache = {};
        this.init = 2;
        this.interval = 0;
        this.pending = {};
        this.updating = false;
        this.setMaxListeners(256);
    }
    start() {
        this.interval = setInterval(this.update.bind(this), 500);
    }
    stop() {
        clearInterval(this.interval);
    }
    storeProcessData(pid) {
        var self = this;
        this.pending[pid] = true;
        fs.readFile(`/proc/${pid}/status`, function(err, data) {
            delete self.pending[pid];
            if (err) {
                return;
            };
            var name = '';
            var uid = '';
            var lines = data.toString().split('\n');
            lines.forEach(function(line) {
                var match_name = /Name:\t(.+)$/i.exec(line);
                var match_uid = /Uid:\t\d+\t(\d+)/.exec(line);
                if (match_name) name = match_name[1];
                if (match_uid) uid = match_uid[1];
            });
            self.cache[pid] = new Process(pid, uid, name);
            if (!self.init) {
                self.emit('birth', self.cache[pid]);
            }
        });
    }
    update() {
        var self = this;
        if (this.updating) {
            return;
        }
        this.updating = true;
        if (this.init) {
            if (!--this.init) {
                self.emit('init');
            }
        };
        fs.readdir('/proc', { withFileTypes: true }, function(err, entries) {
            self.updating = false;
            if (err) {
                return;
            }
            const live = {};
            entries.forEach(function(entry) {
                var pid = entry.name;
                if (!entry.isDirectory() || !/^\d+$/.test(pid)) {
                    return;
                }
                live[pid] = true;
                if (!self.cache[pid] && !self.pending[pid]) {
                    self.storeProcessData(pid);
                }
            });
            for (var key in self.cache) {
                if (live[key]) {
                    continue;
                }
                try {
                    self.emit('death', self.cache[key]);
                    self.cache[key].emit('exit');
                    delete self.cache[key];
                } catch (e) {
                    console.log(e);
                }
            }
            self.emit('update');
        });
    }
    find(name, uid) {
        var self = this;
        var result = [];
        for (var key in self.cache) {
            if (self.cache[key].uid == uid && self.cache[key].name == name) {
                result.push(self.cache[key]);
            }
        }
        return result;
    }
}

module.exports = new ProcEvents();
