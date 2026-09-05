const fs = require('fs');
const Bot = require('./bot');
const BanTracker = require('./ban_tracker');
const steam_id = require('../steam_id');

class BotManager {
    constructor(cc) {
        var self = this;
        try {
            fs.mkdirSync('logs');
        } catch (e) { }
        this.bots = [];
        this.cc = cc;
        this.quota = 0;
        this.wanted_quota = 0;
        this.lastQuery = {};
        this.ban_tracker = new BanTracker(this);
        this.updateTimeout = null;
        this.ipc_query_timeout = null;
        this.query_in_progress = false;
        this.stopping = false;
        this.recover_existing_until = 0;
        this.list_json = '{"quota":0,"count":0,"bots":{}}';
        this.state_json = '{"bots":{}}';
        this.snapshot_dirty = true;
        this.last_snapshot_rebuild = 0;
        this.last_ipc_query_success = 0;
        this.consecutive_ipc_failures = 0;
        this.ipc_discovery_query = true;
        this.restart_window_start = Date.now();
        this.restart_window_count = 0;
        this.start_queue = [];
        this.start_lane = [];
        this.granted_starts_this_tick = 0;
        this.last_start_wave_time = 0;
        this.last_steam_boot_time = 0;
        this.update_cursor = 0;
        this.update_budget_ms = Number.parseInt(process.env.CAT_MANAGER_UPDATE_BUDGET_MS || '12', 10);
        if (!Number.isSafeInteger(this.update_budget_ms) || this.update_budget_ms < 5)
            this.update_budget_ms = 12;
        this.update_batch_limit = Number.parseInt(process.env.CAT_MANAGER_UPDATE_BATCH || '8', 10);
        if (!Number.isSafeInteger(this.update_batch_limit) || this.update_batch_limit < 1)
            this.update_batch_limit = 8;
        this.update_slice_yield_ms = Number.parseInt(process.env.CAT_MANAGER_UPDATE_SLICE_YIELD_MS || '25', 10);
        if (!Number.isSafeInteger(this.update_slice_yield_ms) || this.update_slice_yield_ms < 1)
            this.update_slice_yield_ms = 25;
        this.quota_creation_timeout = null;
        this.quota_creation_in_progress = false;
        this.quota_creation_batch_limit = Number.parseInt(process.env.CAT_BOT_CREATE_BATCH || '8', 10);
        if (!Number.isSafeInteger(this.quota_creation_batch_limit) || this.quota_creation_batch_limit < 1)
            this.quota_creation_batch_limit = 8;
        this.snapshot_interval_ms = Number.parseInt(process.env.CAT_MANAGER_SNAPSHOT_INTERVAL_MS || '1000', 10);
        if (!Number.isSafeInteger(this.snapshot_interval_ms) || this.snapshot_interval_ms < 100)
            this.snapshot_interval_ms = 1000;
        this.schedule_update(1000);
        this.schedule_ipc_query(this.ipc_query_interval_ms());
    }

    schedule_update(delay) {
        if (this.updateTimeout)
            return;

        this.updateTimeout = setTimeout(() => {
            this.updateTimeout = null;
            this.update();
        }, delay);
    }

    request_update(delay) {
        if (this.updateTimeout) {
            clearTimeout(this.updateTimeout);
            this.updateTimeout = null;
        }
        this.schedule_update(delay);
    }

    schedule_quota_creation(delay) {
        if (this.stopping || this.quota_creation_timeout || this.quota_creation_in_progress || this.bots.length >= this.quota)
            return;

        this.quota_creation_timeout = setTimeout(() => {
            this.quota_creation_timeout = null;
            this.run_quota_creation();
        }, delay);
    }

    schedule_ipc_query(delay) {
        if (this.ipc_query_timeout)
            return;

        this.ipc_query_timeout = setTimeout(() => {
            this.ipc_query_timeout = null;
            this.run_ipc_query();
        }, delay);
    }

    update_interval_ms() {
        const count = this.bots.length;
        if (count > 60)
            return 4000;
        if (count > 30)
            return 2500;
        return 1500;
    }

    ipc_query_interval_ms() {
        const count = this.bots.length;
        if (count > 60)
            return 8000;
        if (count > 30)
            return 5000;
        return 3000;
    }

    process_table_cache_ms() {
        const count = this.bots.length;
        if (count > 60)
            return 15000;
        if (count > 30)
            return 10000;
        return Number.parseInt(process.env.CAT_PROCESS_TABLE_CACHE_MS || '1500', 10);
    }

    ipc_queries_healthy() {
        if (this.consecutive_ipc_failures > 0)
            return false;
        if (!this.last_ipc_query_success)
            return false;
        return Date.now() - this.last_ipc_query_success < this.ipc_query_interval_ms() * 3;
    }

    ipc_peer_missing_grace_ms() {
        return Math.max(15000, 5000 + this.bots.length * 250);
    }

    restart_budget_per_minute() {
        return Math.max(4, Math.ceil(this.bots.length / 8));
    }

    startup_pressure_active() {
        return this.count_active_starts() > 0 ||
            this.count_active_steam_boots() > 0 ||
            this.start_lane.length > 0;
    }

    can_auto_restart_bot(bot) {
        if (!bot)
            return false;

        if (!this.ipc_queries_healthy())
            return false;

        if (this.startup_pressure_active())
            return false;

        return true;
    }

    allow_restart(bot, reason) {
        const now = Date.now();
        if (now - this.restart_window_start > 60000) {
            this.restart_window_start = now;
            this.restart_window_count = 0;
        }

        if (this.restart_window_count >= this.restart_budget_per_minute()) {
            if (!bot.restart_budget_deferred) {
                bot.restart_budget_deferred = true;
                console.log(`[WARN] Restart budget reached; deferring ${bot.name}: ${reason}`);
            }
            return false;
        }

        bot.restart_budget_deferred = false;
        this.restart_window_count++;
        return true;
    }

    sort_bots_by_id_desc() {
        this.bots.sort((left, right) => right.botid - left.botid);
    }

    refresh_start_lane() {
        const lane = [];
        for (const bot of this.bots) {
            if (!bot.shouldRun || bot.shouldRestart)
                continue;
            if (bot.procFirejailSteam || bot.procFirejailGame)
                continue;
            if (bot.terminal_auth_state)
                continue;
            if (!bot.account)
                continue;
            if (bot.state === Bot.states.NO_ACCOUNT)
                continue;
            lane.push(bot);
        }
        lane.sort((left, right) => right.botid - left.botid);
        this.start_lane = lane;
        this.start_queue = lane;
    }

    higher_bot_blocks_steam_start(bot) {
        for (const other of this.bots) {
            if (other.botid <= bot.botid)
                continue;
            if (!other.shouldRun)
                continue;
            if (other.terminal_auth_state)
                continue;
            if (other.shouldRestart)
                return true;
            if (other.procFirejailSteam || other.procFirejailGame)
                continue;
            return true;
        }
        return false;
    }

    can_bot_begin_restart(bot) {
        return bot.shouldRestart;
    }

    count_active_starts() {
        let count = 0;
        for (const bot of this.bots) {
            if (bot.start_slot_in_use())
                count++;
        }
        return count;
    }

    count_active_steam_boots() {
        let count = 0;
        for (const bot of this.bots) {
            if (bot.steam_boot_in_progress())
                count++;
        }
        return count;
    }

    start_queue_index(bot) {
        return this.start_lane.indexOf(bot);
    }

    start_wave_delay_elapsed(time) {
        if (!Bot.start_wave_delay_ms)
            return true;
        if (!this.last_start_wave_time)
            return true;
        return time >= this.last_start_wave_time + Bot.start_wave_delay_ms;
    }

    steam_boot_delay_elapsed(time) {
        if (!Bot.steam_boot_delay_ms)
            return true;
        if (!this.last_steam_boot_time)
            return true;
        return time >= this.last_steam_boot_time + Bot.steam_boot_delay_ms;
    }

    can_bot_adopt_existing(bot) {
        let head = null;
        for (const candidate of this.bots) {
            if (candidate.procFirejailSteam || candidate.procFirejailGame)
                continue;
            if (!head || candidate.botid > head.botid)
                head = candidate;
        }
        return head === bot;
    }

    can_bot_begin_steam_boot(bot, time) {
        const rank = this.start_lane.indexOf(bot);
        if (rank < 0)
            return false;

        const active_starts = this.count_active_starts();
        const active_steam_boots = this.count_active_steam_boots();
        const pending = this.granted_starts_this_tick;
        const start_slots = Bot.MAX_CONCURRENT_BOTS - active_starts - pending;
        const steam_slots = Bot.max_steam_boots() - active_steam_boots - pending;
        const available = Math.min(start_slots, steam_slots);
        if (available <= 0)
            return false;

        if (rank < pending)
            return false;

        if (rank >= pending + available)
            return false;

        if (active_starts + pending === 0) {
            if (rank !== 0)
                return false;
            if (!this.start_wave_delay_elapsed(time))
                return false;
        }

        if (pending > 0 && !this.steam_boot_delay_elapsed(time))
            return false;

        return true;
    }

    notify_steam_boot_granted(bot, time) {
        if (this.count_active_starts() + this.granted_starts_this_tick === 0)
            this.last_start_wave_time = time;

        this.last_steam_boot_time = time;
        this.granted_starts_this_tick++;
        Bot.lastStartTime = time;
        Bot.lastSteamBootTime = time;
    }

    rebuild_snapshots() {
        const list = {
            quota: this.quota,
            count: this.bots.length,
            bots: {}
        };
        const state = { bots: {} };

        for (const bot of this.bots) {
            list.bots[bot.name] = {
                user: bot.user || bot.name,
                state: bot.state,
                ipcID: bot.ipcID
            };

            const steamid32 = bot.ipcState && bot.ipcState.friendid ? String(bot.ipcState.friendid) : null;
            const steamid64 = steamid32 ? steam_id.account_id32_to_steamid64(steamid32) : null;
            state.bots[bot.name] = {
                ipc: bot.ipcState,
                ipc_observed_at: bot.ipcObservedAt || 0,
                restarts: bot.restarts,
                restart_paused: Boolean(bot.restart_paused),
                restart_pause_reason: bot.restart_pause_reason || null,
                restart_not_before: bot.restart_not_before || 0,
                restart_failures: bot.restart_failure_times ? bot.restart_failure_times.length : 0,
                ipcID: bot.ipcID,
                state: bot.state,
                started: bot.gameStarted,
                pid: bot.game,
                steamid32: steamid32,
                steamid64: steamid64,
                profile_url: steamid64 ? `https://steamcommunity.com/profiles/${steamid64}` : null,
                ban_tracker: this.ban_tracker.status_for_bot(bot)
            };
        }

        this.list_json = JSON.stringify(list);
        this.state_json = JSON.stringify(state);
        this.last_snapshot_rebuild = Date.now();
        this.snapshot_dirty = false;
    }

    maybe_rebuild_snapshots(force) {
        if (!force && !this.snapshot_dirty)
            return;
        if (!force && Date.now() - this.last_snapshot_rebuild < this.snapshot_interval_ms)
            return;
        this.rebuild_snapshots();
    }

    get_list_json() {
        return this.list_json;
    }

    get_state_json() {
        return this.state_json;
    }

    log_exception(context, error) {
        const stack = error && error.stack ? error.stack : String(error);
        console.log(`[ERROR] ${context}: ${stack}`);
    }

    stop_failed_bot(bot, context, error) {
        this.log_exception(`${context} for ${bot ? bot.name : 'unknown bot'}`, error);
        if (bot)
            bot.update_failed_at = Date.now();
    }

    build_ipc_query_args() {
        const assigned_ids = [];
        let unassigned_count = 0;

        for (const bot of this.bots) {
            if (bot.ipcID >= 0)
                assigned_ids.push(bot.ipcID);
            else
                unassigned_count++;
        }

        if (this.ipc_discovery_query || unassigned_count > 0 || assigned_ids.length === 0)
            return { skipEmpty: true };

        return { ids: assigned_ids };
    }

    apply_ipc_query_result(data, query_time) {
        if (!data || data.status !== 'success' || !data.result)
            return false;

        this.last_ipc_query_success = Date.now();
        this.consecutive_ipc_failures = 0;
        this.lastQuery = data;

        const bots_by_ipc_id = new Map();
        for (const bot of this.bots) {
            if (bot.ipcID >= 0)
                bots_by_ipc_id.set(String(bot.ipcID), bot);
        }

        let process_table = null;
        let children_by_parent = null;
        let bots_by_owned_pid = null;
        const ensure_process_context = () => {
            if (process_table)
                return;

            Bot.set_process_table_cache_ms(this.process_table_cache_ms());
            process_table = Bot.read_process_table();
            children_by_parent = Bot.build_process_children_by_parent(process_table);
            bots_by_owned_pid = new Map();
            for (const bot of this.bots)
                bot.index_owned_processes(process_table, children_by_parent, bots_by_owned_pid);
        };

        for (var q in data.result) {
            const peer = data.result[q];
            let best_bot = null;
            let best_score = 0;
            const assigned_bot = bots_by_ipc_id.get(String(q));
            if (assigned_bot) {
                best_bot = assigned_bot;
                best_score = assigned_bot.ipc_peer_match_score(q, peer, process_table, undefined, children_by_parent);
            }
            if (!best_bot || best_score <= 0) {
                ensure_process_context();
                const owner_bot = peer && peer.pid ? bots_by_owned_pid.get(peer.pid) : null;
                if (owner_bot) {
                    best_bot = owner_bot;
                    best_score = owner_bot.ipc_peer_match_score(q, peer, process_table, owner_bot, children_by_parent);
                }
            }
            if (!best_bot || best_score <= 0) {
                ensure_process_context();
                const owner_bot = peer && peer.pid ? bots_by_owned_pid.get(peer.pid) : null;
                for (var b of this.bots) {
                    const score = b.ipc_peer_match_score(q, peer, process_table, owner_bot || null, children_by_parent);
                    if (score > best_score) {
                        best_bot = b;
                        best_score = score;
                    }
                }
            }

            if (best_bot) {
                if (!process_table)
                    best_bot.emit('ipc-data', { id: q, data: peer });
                else
                    best_bot.accept_ipc_peer(q, peer, process_table, children_by_parent);
            }
        }

        if (this.ipc_queries_healthy()) {
            for (const bot of this.bots) {
                const assigned_peer = bot.ipcID >= 0 ? data.result[String(bot.ipcID)] : null;
                if (bot.ipcID >= 0 && (!assigned_peer || !assigned_peer.pid || !assigned_peer.heartbeat))
                    bot.mark_ipc_peer_missing(query_time, this);
                else if (assigned_peer)
                    bot.clear_ipc_peer_missing();
            }
        }

        this.ipc_discovery_query = !this.ipc_discovery_query;
        this.rebuild_snapshots();
        return true;
    }

    record_ipc_query_failure(reason) {
        this.consecutive_ipc_failures++;
        console.log(`[WARN] IPC query failed (${this.consecutive_ipc_failures} consecutive): ${reason || 'unknown'}`);
        for (const bot of this.bots)
            bot.clear_ipc_peer_missing();
    }

    run_ipc_query() {
        var self = this;

        if (this.stopping || !this.bots.length) {
            if (!this.stopping)
                this.schedule_ipc_query(this.ipc_query_interval_ms());
            return;
        }

        if (this.query_in_progress) {
            this.schedule_ipc_query(1000);
            return;
        }

        const query_args = this.build_ipc_query_args();

        this.query_in_progress = true;
        try {
            this.cc.command('query', query_args, function (data) {
                self.query_in_progress = false;
                try {
                    const query_time = Date.now();
                    if (!self.apply_ipc_query_result(data, query_time))
                        self.record_ipc_query_failure(data && data.error ? data.error : 'bad IPC query response');
                } catch (error) {
                    self.record_ipc_query_failure(error.message);
                    self.log_exception('IPC query callback failed', error);
                }
                if (!self.stopping || self.bots.length)
                    self.schedule_ipc_query(self.ipc_query_interval_ms());
            });
        } catch (error) {
            self.query_in_progress = false;
            self.record_ipc_query_failure(error.message);
            self.log_exception('IPC query command failed', error);
            if (!self.stopping || self.bots.length)
                self.schedule_ipc_query(self.ipc_query_interval_ms());
        }
    }

    async update() {
        if (this.update_in_progress)
            return;
        this.update_in_progress = true;
        try {
            await this.update_bots();
        } catch (error) {
            this.log_exception('bot update failed', error);
            this.schedule_update(1000);
        } finally {
            this.update_in_progress = false;
        }
    }

    async update_bots() {
        var self = this;
        Bot.currentlyStartingGames = 0;
        Bot.currentlyBootingSteam = 0;

        try {
            this.enforceQuota();
        } catch (error) {
            this.log_exception('failed to enforce bot quota during update', error);
            this.quota = this.bots.length;
            this.wanted_quota = this.bots.length;
        }

        if (this.bots.length < this.quota)
            this.schedule_quota_creation(10);

        if (!this.quota && !this.bots.length) {
            this.rebuild_snapshots();
            if (!this.stopping)
                self.schedule_update(5000);
            return;
        }

        Bot.set_process_table_cache_ms(this.process_table_cache_ms());
        const process_table = await Bot.refresh_process_table();
        const children_by_parent = Bot.build_process_children_by_parent(process_table);
        this.granted_starts_this_tick = 0;
        this.refresh_start_lane();

        const update_start = Date.now();
        const total_bots = this.bots.length;
        let processed_bots = 0;
        let completed_cycle = true;
        if (this.update_cursor >= total_bots)
            this.update_cursor = 0;

        for (let offset = 0; offset < total_bots; offset++) {
            const i = (this.update_cursor + offset) % total_bots;
            const b = this.bots[i];
            if (!b)
                continue;
            if (b.start_slot_in_use())
                Bot.currentlyStartingGames++;
            if (b.steam_boot_in_progress())
                Bot.currentlyBootingSteam++;
            if (i + 1 > this.quota) {
                let stopped = false;
                try {
                    stopped = b.full_stop();
                } catch (error) {
                    this.stop_failed_bot(b, 'bot full stop failed', error);
                }
                if (stopped) {
                    const remove_index = this.bots.indexOf(b);
                    if (remove_index >= 0)
                        this.bots.splice(remove_index, 1);
                    continue;
                }
            }
            try {
                b.update(process_table, children_by_parent);
            } catch (error) {
                this.stop_failed_bot(b, 'bot update failed', error);
            }
            processed_bots++;
            if (processed_bots >= this.update_batch_limit || Date.now() - update_start >= this.update_budget_ms) {
                this.update_cursor = (i + 1) % total_bots;
                completed_cycle = this.update_cursor === 0;
                break;
            }
        }

        if (completed_cycle)
            this.update_cursor = 0;

        this.snapshot_dirty = true;
        this.maybe_rebuild_snapshots(completed_cycle);
        if (completed_cycle) {
            try {
                this.ban_tracker.update();
            } catch (error) {
                this.log_exception('ban tracker update failed', error);
            }
        }

        if (!this.stopping || self.bots.length)
            self.schedule_update(completed_cycle ? this.update_interval_ms() : this.update_slice_yield_ms);
    }

    enforceQuota() {
        if (this.bots.length < this.quota)
            this.schedule_quota_creation(10);
        this.sort_bots_by_id_desc();
        return true;
    }

    run_quota_creation() {
        if (this.stopping || this.quota_creation_in_progress)
            return;

        this.quota_creation_in_progress = true;
        let created_count = 0;
        try {
            while (this.bots.length < this.quota && created_count < this.quota_creation_batch_limit) {
                const bot = new Bot.bot(this.next_bot_id_for_fill());
                bot.manager = this;
                bot.shouldRun = false;
                bot.shouldRestart = false;
                this.bots.push(bot);
                created_count++;
            }
            if (created_count)
                this.sort_bots_by_id_desc();
        } catch (error) {
            this.log_exception(`failed to create bot b${this.bots.length}`, error);
            this.quota = this.bots.length;
            this.wanted_quota = this.bots.length;
        } finally {
            this.quota_creation_in_progress = false;
        }

        this.snapshot_dirty = true;
        this.maybe_rebuild_snapshots(false);
        if (this.bots.length < this.quota)
            this.schedule_quota_creation(10);
        this.request_update(this.update_slice_yield_ms);
    }

    next_bot_id_for_fill() {
        const used_ids = new Set();
        for (const bot of this.bots)
            used_ids.add(bot.botid);

        for (let botid = this.quota - 1; botid >= 0; botid--) {
            if (!used_ids.has(botid))
                return botid;
        }

        let botid = 0;
        while (used_ids.has(botid))
            botid++;

        return botid;
    }

    next_bot_id() {
        return this.next_bot_id_for_fill();
    }

    bot(name) {
        for (var bot of this.bots) {
            if (bot.name == name) return bot;
        }
        return null;
    }

    setQuota(quota) {
        quota = String(quota).trim();
        if (!/^[0-9]+$/.test(quota)) {
            return false;
        }
        quota = Number.parseInt(quota, 10);
        if (!Number.isSafeInteger(quota))
            return false;

        this.wanted_quota = quota;
        this.quota = quota;
        this.recover_existing_until = 0;
        this.enforceQuota();
        this.snapshot_dirty = true;
        this.maybe_rebuild_snapshots(true);
        this.request_update(this.update_slice_yield_ms);
        this.schedule_ipc_query(0);
        return true;
    }

    getJSONStatus() {
        var result = {};
        return result;
    }

    stop() {
        this.stopping = true;
    }
}

module.exports = BotManager;
