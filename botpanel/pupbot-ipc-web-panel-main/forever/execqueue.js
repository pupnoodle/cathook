class ExecQueue {
    constructor(rate, max_queue = 64) {
        this.rate = 0;
        this.queue = [];
        this.max_queue = max_queue;
        this.on_drop = null;
        this.interval = setInterval(this.exec.bind(this), rate);
    }
    exec() {
        var a = (this.queue.shift());
        if (a) a();
    }
    push(callback) {
        if (this.queue.length >= this.max_queue) {
            const dropped = this.queue.shift();
            if (dropped && this.on_drop)
                this.on_drop(dropped);
        }
        this.queue.push(callback);
    }
}

module.exports = ExecQueue;
