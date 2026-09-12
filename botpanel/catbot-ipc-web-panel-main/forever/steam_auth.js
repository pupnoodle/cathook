const crypto = require('crypto');
const protobuf = require('./steam_protobuf');
const steam_guard = require('./steam_guard');

const WEBAPI_BASE = 'https://api.steampowered.com';
const REQUEST_TIMEOUT_MS = 15000;
const HANDLED_SESSION_TTL_MS = 10 * 60 * 1000;
const TOKEN_MIN_REMAINING_SECONDS = 120;
const MOBILE_APP_HEADERS = {
    'user-agent': 'okhttp/4.9.2',
    cookie: 'mobileClient=android; mobileClientVersion=777777 3.10.3'
};
const MOBILE_DEVICE_NAME = 'Galaxy S25';
const MOBILE_OS_TYPE = -500; // EOSType AndroidUnknown
const MOBILE_GAMING_DEVICE_TYPE = 528;

const PLATFORM_TYPE = { STEAM_CLIENT: 1, WEB_BROWSER: 2, MOBILE_APP: 3 };
const GUARD_TYPE = { NONE: 1, EMAIL_CODE: 2, DEVICE_CODE: 3, DEVICE_CONFIRMATION: 4, EMAIL_CONFIRMATION: 5, MACHINE_TOKEN: 6 };
const SESSION_PERSISTENT = 1;
const TOKEN_RENEWAL_NONE = 0;
const ERESULT_RATE_LIMIT_EXCEEDED = 84;

const PLATFORM_NAMES = { 0: 'Unknown', 1: 'SteamClient', 2: 'WebBrowser', 3: 'MobileApp' };
const GUARD_NAMES = { 0: 'Unknown', 1: 'None', 2: 'EmailCode', 3: 'DeviceCode', 4: 'DeviceConfirmation', 5: 'EmailConfirmation', 6: 'MachineToken', 7: 'LegacyMachineAuth' };
const ERESULT_NAMES = {
    2: 'Fail', 3: 'NoConnection', 5: 'InvalidPassword', 8: 'InvalidParam', 9: 'FileNotFound', 10: 'Busy',
    11: 'InvalidState', 15: 'AccessDenied', 16: 'Timeout', 17: 'Banned', 18: 'AccountNotFound',
    20: 'ServiceUnavailable', 21: 'NotLoggedOn', 25: 'LimitExceeded', 26: 'Revoked', 27: 'Expired',
    29: 'DuplicateRequest', 63: 'AccountLogonDenied', 65: 'InvalidLoginAuthCode', 84: 'RateLimitExceeded',
    85: 'AccountLoginDeniedNeedTwoFactor', 87: 'AccountLoginDeniedThrottle', 88: 'TwoFactorCodeMismatch'
};

class SteamAuthError extends Error {
    constructor(method, eresult, detail) {
        const reason = eresult ? (ERESULT_NAMES[eresult] || `EResult ${eresult}`) : 'request failed';
        super(`${method}: ${reason}${detail ? ` (${detail})` : ''}`);
        this.name = 'SteamAuthError';
        this.method = method;
        this.eresult = eresult;
    }
}

const requests = {
    get_password_rsa_public_key(account_name) {
        return new protobuf.Writer().string(1, account_name);
    },

    // Same fields steam-session sends for a MobileApp login.
    begin_auth_session_via_credentials({ account_name, encrypted_password, encryption_timestamp }) {
        const device_details = new protobuf.Writer()
            .string(1, MOBILE_DEVICE_NAME)
            .varint(2, PLATFORM_TYPE.MOBILE_APP)
            .varint(3, MOBILE_OS_TYPE)
            .varint(4, MOBILE_GAMING_DEVICE_TYPE);
        return new protobuf.Writer()
            .string(2, account_name)
            .string(3, encrypted_password)
            .varint(4, encryption_timestamp)
            .bool(5, true)
            .varint(7, SESSION_PERSISTENT)
            .string(8, 'Mobile')
            .message(9, device_details);
    },

    update_auth_session_with_steam_guard_code({ client_id, steamid, code }) {
        return new protobuf.Writer()
            .varint(1, client_id)
            .fixed64(2, steamid)
            .string(3, code)
            .varint(4, GUARD_TYPE.DEVICE_CODE);
    },

    poll_auth_session_status({ client_id, request_id }) {
        return new protobuf.Writer().varint(1, client_id).bytes(2, request_id);
    },

    generate_access_token_for_app({ refresh_token, steamid }) {
        return new protobuf.Writer().string(1, refresh_token).fixed64(2, steamid).varint(3, TOKEN_RENEWAL_NONE);
    },

    get_auth_sessions_for_account() {
        return new protobuf.Writer();
    },

    get_auth_session_info({ client_id }) {
        return new protobuf.Writer().varint(1, client_id);
    },

    update_auth_session_with_mobile_confirmation({ version, client_id, steamid, signature }) {
        return new protobuf.Writer()
            .varint(1, version)
            .varint(2, client_id)
            .fixed64(3, steamid)
            .bytes(4, signature)
            .bool(5, true)
            .varint(6, SESSION_PERSISTENT);
    }
};

async function call_authentication_service(fetch_impl, method, request, options = {}) {
    const url = new URL(`${WEBAPI_BASE}/IAuthenticationService/${method}/v1/`);
    const encoded = request.finish();
    const init = {
        method: options.get ? 'GET' : 'POST',
        headers: { ...MOBILE_APP_HEADERS },
        signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS)
    };
    if (options.access_token)
        url.searchParams.set('access_token', options.access_token);
    if (options.get)
        url.searchParams.set('origin', 'SteamMobile');
    if (encoded.length) {
        if (options.get)
            url.searchParams.set('input_protobuf_encoded', encoded.toString('base64'));
        else
            init.body = new URLSearchParams({ input_protobuf_encoded: encoded.toString('base64') });
    }

    const response = await fetch_impl(url, init);
    if (!response.ok)
        throw new SteamAuthError(method, 0, `HTTP ${response.status}`);
    const eresult = Number.parseInt(response.headers.get('x-eresult') || '0', 10);
    if (eresult !== 1)
        throw new SteamAuthError(method, eresult, response.headers.get('x-error_message'));
    return protobuf.decode(Buffer.from(await response.arrayBuffer()));
}

function encrypt_password(password, modulus_hex, exponent_hex) {
    const key = crypto.createPublicKey({
        key: {
            kty: 'RSA',
            n: Buffer.from(modulus_hex, 'hex').toString('base64url'),
            e: Buffer.from(exponent_hex, 'hex').toString('base64url')
        },
        format: 'jwk'
    });
    return crypto.publicEncrypt({ key, padding: crypto.constants.RSA_PKCS1_PADDING }, Buffer.from(password, 'utf8')).toString('base64');
}

function decode_jwt(token) {
    const parts = typeof token === 'string' ? token.split('.') : [];
    if (parts.length !== 3)
        return null;
    try {
        return JSON.parse(Buffer.from(parts[1], 'base64url').toString('utf8'));
    } catch (error) {
        return null;
    }
}

function jwt_usable(token, audience, now_seconds) {
    const payload = decode_jwt(token);
    return Boolean(payload && Array.isArray(payload.aud) && payload.aud.includes(audience)
        && Number(payload.exp) - now_seconds > TOKEN_MIN_REMAINING_SECONDS);
}

// Only a MobileApp access token (not a refresh token) may confirm sign-ins.
function mobile_access_token_usable(token, now_seconds) {
    return jwt_usable(token, 'mobile', now_seconds) && !decode_jwt(token).aud.includes('derive');
}

class SteamGuardSession {
    constructor(account, options = {}) {
        this.fetch = options.fetch || fetch;
        this.now_seconds = options.now_seconds || steam_guard.steam_time;
        this.sleep = options.sleep || ((ms) => new Promise((resolve) => setTimeout(resolve, ms)));
        this.allow_location_mismatch = options.allow_location_mismatch !== undefined
            ? options.allow_location_mismatch
            : process.env.CAT_STEAM_GUARD_ALLOW_LOCATION_MISMATCH === '1';
        this.access_token = null;
        this.refresh_token = null;
        this.token_request = null;
        this.handled_client_ids = new Map();
        this.steamid = null;
        this.update_account(account);
    }

    update_account(account) {
        this.login = account.login;
        this.password = account.password;
        this.mafile = account.mafile;
        this.steamid = account.mafile.steamid || this.steamid;
    }

    call(method, request, options) {
        return call_authentication_service(this.fetch, method, request, options);
    }

    adopt_access_token(access_token) {
        this.access_token = access_token;
        const payload = decode_jwt(access_token);
        if (!this.steamid && payload && payload.sub)
            this.steamid = String(payload.sub);
    }

    async get_access_token() {
        const now = await this.now_seconds();
        if (mobile_access_token_usable(this.access_token, now))
            return this.access_token;
        if (!this.token_request) {
            this.token_request = this.acquire_access_token(now).finally(() => {
                this.token_request = null;
            });
        }
        return this.token_request;
    }

    async acquire_access_token(now) {
        if (mobile_access_token_usable(this.mafile.access_token, now)) {
            this.adopt_access_token(this.mafile.access_token);
            return this.access_token;
        }

        for (const refresh_token of new Set([this.refresh_token, this.mafile.refresh_token])) {
            if (!jwt_usable(refresh_token, 'derive', now))
                continue;
            try {
                const response = await this.call('GenerateAccessTokenForApp', requests.generate_access_token_for_app({
                    refresh_token,
                    steamid: decode_jwt(refresh_token).sub
                }));
                const access_token = response.string(1);
                if (mobile_access_token_usable(access_token, now)) {
                    this.refresh_token = response.string(2) || refresh_token;
                    this.adopt_access_token(access_token);
                    return this.access_token;
                }
            } catch (error) {
                // A rejected token falls back to a password sign-in; network trouble
                // and rate limiting must not spend one.
                if (!(error instanceof SteamAuthError) || !error.eresult || error.eresult === ERESULT_RATE_LIMIT_EXCEEDED)
                    throw error;
            }
        }

        await this.sign_in_as_mobile_app();
        return this.access_token;
    }

    async sign_in_as_mobile_app() {
        if (!this.password)
            throw new Error(`no password for ${this.login}, and ${this.mafile.file} has no usable token`);

        const rsa = await this.call('GetPasswordRSAPublicKey', requests.get_password_rsa_public_key(this.login), { get: true });
        const modulus = rsa.string(1);
        const exponent = rsa.string(2);
        const encryption_timestamp = rsa.uint64(3);
        if (!modulus || !exponent || !encryption_timestamp)
            throw new Error('GetPasswordRSAPublicKey returned no key');

        const begin = await this.call('BeginAuthSessionViaCredentials', requests.begin_auth_session_via_credentials({
            account_name: this.login,
            encrypted_password: encrypt_password(this.password, modulus, exponent),
            encryption_timestamp
        }));
        const client_id = begin.uint64(1);
        const request_id = begin.bytes(2);
        const steamid = begin.uint64(5);
        const poll_interval_ms = Math.max(1, begin.float(3) || 5) * 1000;
        const guards = begin.messages(4).map((confirmation) => confirmation.int32(1));
        if (!client_id || !request_id || !steamid)
            throw new Error('BeginAuthSessionViaCredentials returned no session');
        if (this.mafile.steamid && this.mafile.steamid !== steamid)
            throw new Error(`${this.mafile.file} is for SteamID ${this.mafile.steamid}, but ${this.login} is ${steamid}`);
        this.steamid = steamid;

        if (guards.includes(GUARD_TYPE.DEVICE_CODE)) {
            const code = steam_guard.generate_auth_code(this.mafile.shared_secret, await this.now_seconds());
            await this.call('UpdateAuthSessionWithSteamGuardCode', requests.update_auth_session_with_steam_guard_code({ client_id, steamid, code }));
        } else if (!guards.includes(GUARD_TYPE.NONE)) {
            const names = guards.map((guard) => GUARD_NAMES[guard] || guard).join('/');
            throw new Error(`Steam wants ${names || 'an unknown confirmation'} for ${this.login}, not a mobile authenticator code`);
        }

        for (let attempt = 0; attempt < 12; attempt++) {
            const status = await this.call('PollAuthSessionStatus', requests.poll_auth_session_status({ client_id, request_id }));
            const refresh_token = status.string(3);
            const access_token = status.string(4);
            if (refresh_token && access_token) {
                this.refresh_token = refresh_token;
                this.adopt_access_token(access_token);
                return;
            }
            await this.sleep(poll_interval_ms);
        }
        throw new Error(`Steam accepted the authenticator code for ${this.login} but issued no tokens`);
    }

    // Confirms every pending Steam client sign-in for the account. Callers only
    // poll this while one of their bots is signing in.
    async approve_pending_client_logins() {
        const access_token = await this.get_access_token();
        if (!this.steamid)
            throw new Error(`SteamID for ${this.login} is unknown`);

        const now = Date.now();
        for (const [client_id, handled_at] of this.handled_client_ids) {
            if (now - handled_at > HANDLED_SESSION_TTL_MS)
                this.handled_client_ids.delete(client_id);
        }

        const result = { approved: [], refused: [] };
        const sessions = await this.call('GetAuthSessionsForAccount', requests.get_auth_sessions_for_account(), { access_token });
        for (const client_id of sessions.repeated_uint64(1)) {
            if (this.handled_client_ids.has(client_id))
                continue;

            const info = await this.call('GetAuthSessionInfo', requests.get_auth_session_info({ client_id }), { access_token });
            const platform = info.int32(6);
            const session = {
                client_id,
                ip: info.string(1) || '?',
                location: [info.string(3), info.string(4), info.string(5)].filter(Boolean).join(', ') || '?',
                device: info.string(7) || '?',
                platform: PLATFORM_NAMES[platform] || String(platform),
                version: info.int32(8) ?? 1
            };

            if (platform !== PLATFORM_TYPE.STEAM_CLIENT) {
                session.reason = `${session.platform} sign-in, not a Steam client`;
                result.refused.push(session);
            } else if (info.bool(10) && !this.allow_location_mismatch) {
                session.reason = 'Steam flags a location mismatch with this panel host (set CAT_STEAM_GUARD_ALLOW_LOCATION_MISMATCH=1 if bots use another egress IP)';
                result.refused.push(session);
            } else {
                session.method = await this.confirm_client_login(access_token, session);
                result.approved.push(session);
            }
            this.handled_client_ids.set(client_id, now);
        }
        return result;
    }

    async confirm_client_login(access_token, session) {
        try {
            await this.call('UpdateAuthSessionWithMobileConfirmation', requests.update_auth_session_with_mobile_confirmation({
                version: session.version,
                client_id: session.client_id,
                steamid: this.steamid,
                signature: steam_guard.mobile_confirmation_signature(this.mafile.shared_secret, session.version, session.client_id, this.steamid)
            }), { access_token });
            return 'mobile confirmation';
        } catch (error) {
            if (!(error instanceof SteamAuthError) || !error.eresult)
                throw error;
            const code = steam_guard.generate_auth_code(this.mafile.shared_secret, await this.now_seconds());
            await this.call('UpdateAuthSessionWithSteamGuardCode', requests.update_auth_session_with_steam_guard_code({
                client_id: session.client_id,
                steamid: this.steamid,
                code
            }));
            return `authenticator code, after mobile confirmation failed with ${error.message}`;
        }
    }
}

const sessions = new Map();

function session_for(account, options) {
    const key = String(account.login).toLowerCase();
    const existing = sessions.get(key);
    if (existing) {
        existing.update_account(account);
        return existing;
    }
    const session = new SteamGuardSession(account, options);
    sessions.set(key, session);
    return session;
}

module.exports = {
    PLATFORM_TYPE,
    GUARD_TYPE,
    SteamAuthError,
    SteamGuardSession,
    decode_jwt,
    encrypt_password,
    requests,
    session_for
};
