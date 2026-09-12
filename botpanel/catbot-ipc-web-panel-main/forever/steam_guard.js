const crypto = require('crypto');

const CODE_ALPHABET = '23456789BCDFGHJKMNPQRTVWXY';
const CODE_PERIOD_SECONDS = 30;
const TIME_QUERY_URL = 'https://api.steampowered.com/ITwoFactorService/QueryTime/v0001';
const TIME_REALIGN_MS = 60 * 60 * 1000;
const TIME_RETRY_MS = 60 * 1000;

let time_offset_seconds = 0;
let time_next_align = 0;
let time_align_pending = null;

function decode_secret(secret) {
    if (Buffer.isBuffer(secret))
        return secret;
    const text = String(secret || '').trim();
    if (/^[0-9a-f]{40}$/i.test(text))
        return Buffer.from(text, 'hex');
    return Buffer.from(text, 'base64');
}

function generate_auth_code(shared_secret, time_seconds) {
    const key = decode_secret(shared_secret);
    if (!key.length)
        throw new Error('shared_secret is empty');

    const counter = Buffer.alloc(8);
    counter.writeBigUInt64BE(BigInt(Math.floor(time_seconds / CODE_PERIOD_SECONDS)));
    const hmac = crypto.createHmac('sha1', key).update(counter).digest();
    const offset = hmac[19] & 0x0f;
    let code_point = hmac.readUInt32BE(offset) & 0x7fffffff;

    let code = '';
    for (let i = 0; i < 5; i++) {
        code += CODE_ALPHABET[code_point % CODE_ALPHABET.length];
        code_point = Math.floor(code_point / CODE_ALPHABET.length);
    }
    return code;
}

// Signature the mobile app sends with UpdateAuthSessionWithMobileConfirmation
// (steam-session LoginApprover.approveAuthSession).
function mobile_confirmation_signature(shared_secret, version, client_id, steamid) {
    const data = Buffer.alloc(2 + 8 + 8);
    data.writeUInt16LE(version, 0);
    data.writeBigUInt64LE(BigInt(client_id), 2);
    data.writeBigUInt64LE(BigInt(steamid), 10);
    return crypto.createHmac('sha256', decode_secret(shared_secret)).update(data).digest();
}

async function align_steam_time(fetch_impl) {
    const started = Date.now();
    const response = await (fetch_impl || fetch)(TIME_QUERY_URL, {
        method: 'POST',
        body: new URLSearchParams({ steamid: '0' }),
        signal: AbortSignal.timeout(10000)
    });
    if (!response.ok)
        throw new Error(`QueryTime HTTP ${response.status}`);
    const body = await response.json();
    const server_time = Number(body && body.response && body.response.server_time);
    if (!Number.isFinite(server_time) || server_time <= 0)
        throw new Error('QueryTime response has no server_time');

    time_offset_seconds = server_time - Math.floor((started + Date.now()) / 2000);
    return time_offset_seconds;
}

// Local clock corrected by Steam's clock; codes are only valid for 30 seconds.
async function steam_time() {
    if (Date.now() >= time_next_align) {
        if (!time_align_pending) {
            time_align_pending = align_steam_time()
                .then(() => {
                    time_next_align = Date.now() + TIME_REALIGN_MS;
                })
                .catch(() => {
                    time_next_align = Date.now() + TIME_RETRY_MS;
                })
                .finally(() => {
                    time_align_pending = null;
                });
        }
        await time_align_pending;
    }
    return Math.floor(Date.now() / 1000) + time_offset_seconds;
}

module.exports = {
    CODE_PERIOD_SECONDS,
    decode_secret,
    generate_auth_code,
    mobile_confirmation_signature,
    align_steam_time,
    steam_time
};
