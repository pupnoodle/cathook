const crypto = require('crypto');
const fs = require('fs');
const path = require('path');
const steam_guard = require('./steam_guard');

const PBKDF2_ITERATIONS = 50000;
const KEY_SIZE_BYTES = 32;
const RESCAN_INTERVAL_MS = 5000;

const cache = {
    checked_at: 0,
    signature: null,
    loaded: null,
    reported_errors: ''
};

function mafiles_dir() {
    return process.env.CAT_MAFILES_DIR || path.join(__dirname, '..', '..', 'maFiles');
}

function read_passkey() {
    if (process.env.CAT_MAFILE_PASSKEY)
        return process.env.CAT_MAFILE_PASSKEY;
    if (!process.env.CAT_MAFILE_PASSKEY_FILE)
        return null;
    return fs.readFileSync(process.env.CAT_MAFILE_PASSKEY_FILE, 'utf8').replace(/\r?\n$/, '');
}

function decrypt_sda_mafile(ciphertext, passkey, salt, iv) {
    const key = crypto.pbkdf2Sync(passkey, Buffer.from(salt, 'base64'), PBKDF2_ITERATIONS, KEY_SIZE_BYTES, 'sha1');
    const decipher = crypto.createDecipheriv('aes-256-cbc', key, Buffer.from(iv, 'base64'));
    return Buffer.concat([
        decipher.update(Buffer.from(ciphertext.trim(), 'base64')),
        decipher.final()
    ]).toString('utf8');
}

// V8's JSON.parse errors quote the input, which here holds secrets.
function parse_json(text, what) {
    try {
        return JSON.parse(text);
    } catch (error) {
        throw new Error(`${what} is not valid JSON`);
    }
}

// Session.SteamID is a 64-bit integer that JSON.parse would round, so read it from the text.
function steamid_from_text(text) {
    const match = /"SteamID"\s*:\s*"?(\d{5,20})"?/.exec(text);
    return match ? match[1] : null;
}

function parse_mafile(text, file_name) {
    const data = parse_json(text, 'maFile');
    if (!data || typeof data !== 'object')
        throw new Error('maFile is not a JSON object');

    const account_name = typeof data.account_name === 'string' ? data.account_name.trim() : '';
    if (!account_name)
        throw new Error('missing account_name');
    if (typeof data.shared_secret !== 'string' || steam_guard.decode_secret(data.shared_secret).length !== 20)
        throw new Error('missing or malformed shared_secret');

    const session = data.Session && typeof data.Session === 'object' ? data.Session : {};
    const token = (value) => (typeof value === 'string' && value ? value : null);
    return {
        file: file_name,
        account_name,
        shared_secret: data.shared_secret,
        steamid: steamid_from_text(text),
        access_token: token(session.AccessToken),
        refresh_token: token(session.RefreshToken)
    };
}

function load_mafiles(dir, passkey) {
    const result = { dir, accounts: new Map(), errors: [] };
    let names;
    try {
        names = fs.readdirSync(dir).filter((name) => name.toLowerCase().endsWith('.mafile')).sort();
    } catch (error) {
        if (error.code !== 'ENOENT')
            result.errors.push(`${dir}: ${error.message}`);
        return result;
    }

    let manifest_entries = [];
    const manifest_path = path.join(dir, 'manifest.json');
    if (fs.existsSync(manifest_path)) {
        try {
            const manifest = parse_json(fs.readFileSync(manifest_path, 'utf8').replace(/^﻿/, ''), 'manifest.json');
            if (manifest && Array.isArray(manifest.entries))
                manifest_entries = manifest.entries;
        } catch (error) {
            result.errors.push(`manifest.json: ${error.message}`);
        }
    }

    for (const name of names) {
        try {
            const text = fs.readFileSync(path.join(dir, name), 'utf8').replace(/^﻿/, '');
            let account;
            if (text.trimStart().startsWith('{')) {
                account = parse_mafile(text, name);
            } else {
                const entry = manifest_entries.find((candidate) => candidate && candidate.filename === name);
                if (!entry || !entry.encryption_salt || !entry.encryption_iv)
                    throw new Error('looks encrypted, but manifest.json has no encryption_salt/encryption_iv for it');
                if (!passkey)
                    throw new Error('is encrypted; set CAT_MAFILE_PASSKEY or CAT_MAFILE_PASSKEY_FILE to the SDA passkey');
                try {
                    account = parse_mafile(decrypt_sda_mafile(text, passkey, entry.encryption_salt, entry.encryption_iv), name);
                } catch (error) {
                    throw new Error(`could not be decrypted with the configured passkey (${error.message})`);
                }
            }

            const key = account.account_name.toLowerCase();
            if (result.accounts.has(key))
                throw new Error(`duplicates account_name ${account.account_name} from ${result.accounts.get(key).file}`);
            result.accounts.set(key, account);
        } catch (error) {
            result.errors.push(`${name}: ${error.message}`);
        }
    }
    return result;
}

function directory_signature(dir) {
    try {
        return fs.readdirSync(dir)
            .filter((name) => name.toLowerCase().endsWith('.mafile') || name === 'manifest.json')
            .sort()
            .map((name) => {
                const stat = fs.statSync(path.join(dir, name));
                return `${name}:${stat.size}:${stat.mtimeMs}`;
            })
            .join('|');
    } catch (error) {
        return `error:${error.code || error.message}`;
    }
}

// Rescans at most every few seconds, so maFiles can be added while the panel runs.
function current(log) {
    const now = Date.now();
    if (cache.loaded && now - cache.checked_at < RESCAN_INTERVAL_MS)
        return cache.loaded;
    cache.checked_at = now;

    const dir = mafiles_dir();
    const signature = `${dir}#${directory_signature(dir)}`;
    if (cache.loaded && signature === cache.signature)
        return cache.loaded;

    let passkey = null;
    let passkey_error = null;
    try {
        passkey = read_passkey();
    } catch (error) {
        passkey_error = `CAT_MAFILE_PASSKEY_FILE: ${error.message}`;
    }
    cache.signature = signature;
    cache.loaded = load_mafiles(dir, passkey);
    if (passkey_error)
        cache.loaded.errors.unshift(passkey_error);

    const report = cache.loaded.errors.join('\n');
    if (report && report !== cache.reported_errors && log) {
        for (const error of cache.loaded.errors)
            log(`maFile ${error}`);
    }
    cache.reported_errors = report;
    return cache.loaded;
}

function find(login, log) {
    if (!login)
        return null;
    return current(log).accounts.get(String(login).toLowerCase()) || null;
}

module.exports = {
    mafiles_dir,
    decrypt_sda_mafile,
    parse_mafile,
    load_mafiles,
    current,
    find
};
