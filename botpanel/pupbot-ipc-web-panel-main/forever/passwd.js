const fs = require('fs');

class Passwd {
    constructor() {
        this.users = {};
        this.nameMap = {};
    }
    parse(data) {
        this.users = {};
        this.nameMap = {};
        for (var s of data.split('\n')) {
            var m = /(.+):.:(\d+):(\d+):(?:.+?)?:(.+?):/i.exec(s);
            if (!m) continue;
            var user = {
                name: m[1],
                uid: m[2],
                gid: m[3],
                home: m[4]
            };
            this.users[user.uid] = user;
            this.nameMap[user.name] = user.uid;
        }
    }
    read(callback) {
        var self = this;
        fs.readFile('/etc/passwd', function(error, data) {
            if (error) {
                if (callback)
                    callback(error);
                return;
            }
            self.parse(data.toString());
            if (callback)
                callback(null, self);
        });
    }
    readSync() {
        this.parse(fs.readFileSync('/etc/passwd').toString());
    }
    byName(name) {
        return this.users[this.nameMap[name]];
    }
    byUID(uid) {
        return this.users[uid];
    }
}

class Groups {
    constructor() {
        this.groups = {};
        this.nameMap = {};
    }
    parse(data) {
        this.groups = {};
        this.nameMap = {};
        for (var s of data.split('\n')) {
            var m = /(.+):.:(\d+):/i.exec(s);
            if (!m) continue;
            var group = {
                name: m[1],
                gid: m[2],
            };
            this.groups[group.gid] = group;
            this.nameMap[group.name] = group.gid;
        }
    }
    read(callback) {
        var self = this;
        fs.readFile('/etc/group', function(error, data) {
            if (error) {
                if (callback)
                    callback(error);
                return;
            }
            self.parse(data.toString());
            if (callback)
                callback(null, self);
        });
    }
    readSync() {
        this.parse(fs.readFileSync('/etc/group').toString());
    }
    byName(name) {
        return this.groups[this.nameMap[name]];
    }
    byGID(gid) {
        return this.groups[gid];
    }
}

module.exports = {
    Passwd: Passwd,
    Groups: Groups
};
