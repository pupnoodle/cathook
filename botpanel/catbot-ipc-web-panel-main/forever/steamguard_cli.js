// Diagnostics for .maFile support, run as ./botpanel/steamguard.

const fs = require('fs');
const accounts = require('./acc.js');
const mafile = require('./mafile.js');
const steam_auth = require('./steam_auth.js');
const steam_guard = require('./steam_guard.js');

const APPROVE_WATCH_SECONDS = 90;
const APPROVE_POLL_MS = 5000;

function usage() {
    console.log(`usage: ./botpanel/steamguard <command>

  list                      show which accounts have a maFile, and their token state
  code <login>              print the current Steam Guard code for an account
  approve <login> [seconds] confirm pending Steam client sign-ins (default ${APPROVE_WATCH_SECONDS}s)

Set CAT_MAFILE_PASSKEY when the maFiles are encrypted by SDA.`);
}

function load_accounts() {
    const found = [];
    for (let generation = 0; generation < 100; generation++) {
        const file = accounts.account_file_for_generation(generation);
        if (!fs.existsSync(file)) {
            if (generation === 0)
                continue;
            break;
        }
        accounts.parse_accounts(fs.readFileSync(file, 'utf8')).forEach((account, index) => {
            found.push({ ...account, file, index });
        });
    }
    return found;
}

function token_state(account) {
    const describe = (token, kind) => {
        const payload = steam_auth.decode_jwt(token);
        if (!payload)
            return null;
        const expires = Number(payload.exp) * 1000;
        const state = expires > Date.now() ? 'valid until' : 'expired';
        return `${kind} ${state} ${new Date(expires).toISOString().slice(0, 10)}`;
    };
    const parts = [describe(account.refresh_token, 'refresh token'), describe(account.access_token, 'access token')]
        .filter(Boolean);
    return parts.length ? parts.join(', ') : 'no stored token, will sign in with the password';
}

function command_list() {
    const loaded = mafile.current();
    console.log(`maFiles: ${loaded.dir} (${loaded.accounts.size} loaded)`);
    for (const error of loaded.errors)
        console.log(`  problem: ${error}`);

    const matched = new Set();
    const all_accounts = load_accounts();
    if (!all_accounts.length)
        console.log('\nNo accounts found. Add USERNAME:PASSWORD lines to botpanel/accounts.txt.');
    else
        console.log('');

    for (const account of all_accounts) {
        const found = loaded.accounts.get(account.login.toLowerCase());
        if (found)
            matched.add(found.file);
        const label = `bot b${account.index} ${account.login}`;
        console.log(found
            ? `  ${label} -> ${found.file} (steamid ${found.steamid || 'unknown'}, ${token_state(found)})`
            : `  ${label} -> no maFile (fine unless the account uses a mobile authenticator)`);
    }

    const unused = [...loaded.accounts.values()].filter((account) => !matched.has(account.file));
    if (unused.length) {
        console.log('\nmaFiles with no matching accounts.txt line:');
        for (const account of unused)
            console.log(`  ${account.file} (account_name ${account.account_name})`);
    }
}

function find_account(login) {
    const found = mafile.find(login);
    if (!found)
        throw new Error(`no maFile for ${login} in ${mafile.mafiles_dir()}`);
    const credentials = load_accounts().find((account) => account.login.toLowerCase() === login.toLowerCase());
    return { login: credentials ? credentials.login : found.account_name, password: credentials ? credentials.password : null, mafile: found };
}

async function command_code(login) {
    const account = find_account(login);
    const now = await steam_guard.steam_time();
    const code = steam_guard.generate_auth_code(account.mafile.shared_secret, now);
    const remaining = steam_guard.CODE_PERIOD_SECONDS - (now % steam_guard.CODE_PERIOD_SECONDS);
    console.log(`${account.login}: ${code} (valid for ${remaining}s)`);
}

async function command_approve(login, seconds) {
    const account = find_account(login);
    const session = steam_auth.session_for(account);
    const deadline = Date.now() + (Number(seconds) || APPROVE_WATCH_SECONDS) * 1000;
    console.log(`Watching for Steam client sign-ins for ${account.login} using ${account.mafile.file}...`);

    let confirmed = 0;
    for (;;) {
        try {
            const result = await session.approve_pending_client_logins();
            for (const pending of result.approved) {
                confirmed++;
                console.log(`  confirmed by ${pending.method}: client_id=${pending.client_id} ip=${pending.ip} location=${pending.location} device=${pending.device}`);
            }
            for (const pending of result.refused)
                console.log(`  left alone: ${pending.reason} (client_id=${pending.client_id} ip=${pending.ip} location=${pending.location})`);
        } catch (error) {
            console.log(`  ${error.message}`);
        }
        if (Date.now() >= deadline)
            break;
        await new Promise((resolve) => setTimeout(resolve, APPROVE_POLL_MS));
    }
    console.log(`Done, ${confirmed} sign-in(s) confirmed.`);
}

async function main() {
    const [command, ...args] = process.argv.slice(2);
    switch (command) {
        case 'list':
        case undefined:
            command_list();
            break;
        case 'code':
            if (!args[0])
                throw new Error('code needs a login');
            await command_code(args[0]);
            break;
        case 'approve':
            if (!args[0])
                throw new Error('approve needs a login');
            await command_approve(args[0], args[1]);
            break;
        default:
            usage();
            process.exitCode = 2;
    }
}

main().catch((error) => {
    console.error(error.message);
    process.exitCode = 1;
});
