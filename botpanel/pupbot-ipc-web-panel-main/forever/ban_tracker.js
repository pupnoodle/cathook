const http = require('http');
const https = require('https');
const config = require('./config');
const steam_id = require('../steam_id');

const check_interval_ms = 15000;
const missing_recheck_ms = 5000;
const request_timeout_ms = 10000;

function now_seconds() {
    return Math.floor(Date.now() / 1000);
}

function timestamp() {
    return new Date().toTimeString().slice(0, 8);
}

function log(message) {
    console.log(`[${timestamp()}][Ban Tracker] ${message}`);
}

function request_url(url, headers, redirect_count) {
    return new Promise((resolve, reject) => {
        const parsed_url = new URL(url);
        const transport = parsed_url.protocol === 'http:' ? http : https;
        const req = transport.get(parsed_url, { headers: headers || {} }, (response) => {
            var body = '';
            response.setEncoding('utf8');
            response.on('data', (chunk) => {
                body += chunk;
            });
            response.on('end', () => {
                if (response.statusCode >= 300 && response.statusCode < 400 && response.headers.location) {
                    if ((redirect_count || 0) >= 3) {
                        reject(new Error('too many redirects'));
                        return;
                    }

                    request_url(new URL(response.headers.location, parsed_url).toString(), headers, (redirect_count || 0) + 1)
                        .then(resolve)
                        .catch(reject);
                    return;
                }

                resolve({
                    status_code: response.statusCode,
                    body: body
                });
            });
        });

        req.setTimeout(request_timeout_ms, () => {
            req.destroy(new Error('request timed out'));
        });
        req.on('error', reject);
    });
}

async function request_json(url) {
    const response = await request_url(url);
    if (response.status_code >= 400)
        throw new Error(`http ${response.status_code}`);

    return JSON.parse(response.body);
}

async function request_text(url) {
    const response = await request_url(url, {
        'User-Agent': 'pupbot-ipc-web-panel'
    });
    if (response.status_code === 429 || response.status_code >= 500)
        throw new Error(`http ${response.status_code}`);

    return String(response.body || '');
}

function result_ok(source) {
    return {
        source: source,
        banned: false,
        missing: false,
        inconclusive: false
    };
}

function result_inconclusive(source, error) {
    return {
        source: source,
        banned: false,
        missing: false,
        inconclusive: true,
        error: error ? error.message : 'unknown error'
    };
}

function parse_bans_response(body, steamid64) {
    const players = body && body.players;
    if (!Array.isArray(players))
        throw new Error('bad GetPlayerBans response');

    const player = players.find((item) => String(item.SteamId || item.steamid || '') === steamid64) || players[0];
    if (!player)
        return result_ok('api');

    if (Number(player.DaysSinceLastBan) === 0 || Number(player.NumberOfDaysSinceLastBan) === 0) {
        return {
            source: 'api',
            banned: true,
            missing: false,
            inconclusive: false,
            reason: '0 days since last ban'
        };
    }

    return result_ok('api');
}

function parse_summary_response(body, steamid64, result) {
    const players = body && body.response && body.response.players;
    if (!Array.isArray(players))
        throw new Error('bad GetPlayerSummaries response');

    if (!players.some((item) => String(item.steamid || '') === steamid64)) {
        return {
            source: 'api',
            banned: result.banned,
            missing: true,
            inconclusive: false,
            reason: 'profile not found'
        };
    }

    return result;
}

function parse_profile_html(body) {
    if (/0\s+day\(s\)\s+since\s+last\s+ban/i.test(body)) {
        return {
            source: 'html',
            banned: true,
            missing: false,
            inconclusive: false,
            reason: '0 days since last ban'
        };
    }

    if (/The specified profile could not be found\./i.test(body)) {
        return {
            source: 'html',
            banned: false,
            missing: true,
            inconclusive: false,
            reason: 'profile not found'
        };
    }

    return result_ok('html');
}

class BanTracker {
    constructor(manager) {
        this.manager = manager;
        this.round_robin_index = 0;
        this.next_check_time = 0;
        this.check_running = false;
        this.pending_recheck = null;
        this.status_by_bot = new Map();
    }

    status_for_bot(bot) {
        this.ensure_status_for_bot(bot);
        return this.status_by_bot.get(bot.name);
    }

    update() {
        if (!config.ban_tracker_enabled)
            return;
        if (this.check_running)
            return;
        if (Date.now() < this.next_check_time)
            return;

        const bot = this.pick_bot_to_check();
        if (!bot) {
            this.next_check_time = Date.now() + check_interval_ms;
            return;
        }

        this.check_running = true;
        this.check_bot(bot)
            .catch((error) => {
                this.set_status(bot, {
                    status: 'error',
                    reason: error.message,
                    checked_at: now_seconds()
                });
                log(`${bot.name} check failed: ${error.message}`);
            })
            .finally(() => {
                this.check_running = false;
            });
    }

    ensure_status_for_bot(bot) {
        const identity = this.identity_for_bot(bot);
        const current = this.status_by_bot.get(bot.name);
        if (current && current.identity === identity)
            return current;

        const status = {
            identity: identity,
            status: identity ? 'unchecked' : 'waiting_for_id',
            reason: '',
            checked_at: 0
        };
        this.status_by_bot.set(bot.name, status);
        return status;
    }

    identity_for_bot(bot) {
        const account_id = bot.ipcState && bot.ipcState.friendid ? bot.ipcState.friendid : 0;
        const steamid64 = steam_id.account_id32_to_steamid64(account_id);
        if (!steamid64)
            return null;
        return `${bot.name}:${bot.account_generation}:${steamid64}`;
    }

    set_status(bot, values) {
        const identity = this.identity_for_bot(bot);
        this.status_by_bot.set(bot.name, Object.assign({
            identity: identity,
            status: identity ? 'unchecked' : 'waiting_for_id',
            reason: '',
            checked_at: 0
        }, values));
    }

    watched_bot(bot) {
        return Boolean(bot && bot.ipcState && steam_id.account_id32_to_steamid64(bot.ipcState.friendid));
    }

    pick_bot_to_check() {
        if (this.pending_recheck) {
            const bot = this.manager.bot(this.pending_recheck.bot_name);
            if (bot && this.identity_for_bot(bot) === this.pending_recheck.identity && this.watched_bot(bot))
                return bot;
            this.pending_recheck = null;
        }

        const bots = this.manager.bots;
        if (!bots.length)
            return null;

        for (var offset = 0; offset < bots.length; offset++) {
            const index = (this.round_robin_index + offset) % bots.length;
            const bot = bots[index];
            if (!this.watched_bot(bot)) {
                this.ensure_status_for_bot(bot);
                continue;
            }

            this.round_robin_index = (index + 1) % bots.length;
            return bot;
        }

        return null;
    }

    async check_bot(bot) {
        const account_id = bot.ipcState.friendid;
        const steamid64 = steam_id.account_id32_to_steamid64(account_id);
        const profile_url = steam_id.profile_url_from_account_id32(account_id);
        const identity = this.identity_for_bot(bot);
        const was_recheck = this.pending_recheck && this.pending_recheck.identity === identity;

        log(`Checking ${bot.name} steamid64=${steamid64}${was_recheck ? ' recheck=yes' : ''}`);
        const result = await this.check_steam_account(steamid64, profile_url);

        if (result.inconclusive) {
            this.pending_recheck = null;
            this.next_check_time = Date.now() + check_interval_ms;
            this.set_status(bot, {
                status: 'error',
                reason: result.error || 'inconclusive',
                checked_at: now_seconds(),
                steamid64: steamid64,
                source: result.source
            });
            return;
        }

        if (result.banned) {
            this.confirm_and_restart(bot, steamid64, result.reason || 'recent ban');
            this.next_check_time = Date.now() + check_interval_ms;
            return;
        }

        if (result.missing && !was_recheck) {
            this.pending_recheck = {
                bot_name: bot.name,
                identity: identity
            };
            this.next_check_time = Date.now() + missing_recheck_ms;
            this.set_status(bot, {
                status: 'suspicious',
                reason: result.reason || 'profile not found',
                checked_at: now_seconds(),
                steamid64: steamid64,
                source: result.source
            });
            return;
        }

        if (result.missing && was_recheck) {
            this.pending_recheck = null;
            this.confirm_and_restart(bot, steamid64, result.reason || 'profile not found');
            this.next_check_time = Date.now() + check_interval_ms;
            return;
        }

        this.pending_recheck = null;
        this.next_check_time = Date.now() + check_interval_ms;
        this.set_status(bot, {
            status: 'ok',
            reason: '',
            checked_at: now_seconds(),
            steamid64: steamid64,
            source: result.source
        });
    }

    async check_steam_account(steamid64, profile_url) {
        const api_key = process.env.PUP_STEAM_WEB_API_KEY || '';
        if (api_key) {
            const api_result = await this.check_steam_api(api_key, steamid64).catch((error) => result_inconclusive('api', error));
            if (!api_result.inconclusive)
                return api_result;
            log(`API check failed for ${steamid64}: ${api_result.error}; falling back to profile HTML`);
        }

        return this.check_profile_html(profile_url);
    }

    async check_steam_api(api_key, steamid64) {
        const encoded_key = encodeURIComponent(api_key);
        const encoded_steamid = encodeURIComponent(steamid64);
        const bans_url = `https://api.steampowered.com/ISteamUser/GetPlayerBans/v1/?key=${encoded_key}&steamids=${encoded_steamid}`;
        const summary_url = `https://api.steampowered.com/ISteamUser/GetPlayerSummaries/v2/?key=${encoded_key}&steamids=${encoded_steamid}`;

        const bans = await request_json(bans_url);
        const ban_result = parse_bans_response(bans, steamid64);
        if (ban_result.banned)
            return ban_result;

        const summary = await request_json(summary_url);
        return parse_summary_response(summary, steamid64, ban_result);
    }

    async check_profile_html(profile_url) {
        const body = await request_text(profile_url);
        return parse_profile_html(body);
    }

    confirm_and_restart(bot, steamid64, reason) {
        this.set_status(bot, {
            status: 'confirmed',
            reason: reason,
            checked_at: now_seconds(),
            steamid64: steamid64
        });
        log(`${bot.name} confirmed ${reason}; advancing account generation`);
        bot.advance_account_generation(reason);
    }
}

module.exports = BanTracker;
