const randomstring = require('randomstring');
const fs = require('fs');
const crypto = require('crypto');

class SimpleAuth
{
    constructor(app)
    {
        this.password = String(process.env.PUP_WEB_PASSWORD || randomstring.generate(12));
        this.apikey = randomstring.generate(64);
        this.failed_logins = new Map();

        app.post('/api/auth', require('body-parser').json(), this.handleLogin.bind(this));
        app.use(this.middleware.bind(this));
    }
    middleware(req, res, next)
    {
        if (!req.path.startsWith('/api/') || req.session.auth)
        {
            next();
            return;
        }

        if (req.query.key)
        {
            const presented_key = String(req.get('x-api-key') || req.query.key || '');
            if (this.key_matches(presented_key))
            {
                req.session.auth = 1;
            }
            else
            {
                res.status(403).end('Not authorized');
                return;
            }
        }

        if (!req.session.auth)
        {
            res.status(403).end('Not authorized');
            return;
        }
        next();
    }
    key_matches(presented_key)
    {
        if (typeof presented_key !== 'string' || presented_key.length !== this.apikey.length)
        {
            return false;
        }
        return crypto.timingSafeEqual(Buffer.from(presented_key), Buffer.from(this.apikey));
    }
    handleLogin(req, res)
    {
        const now = Date.now();
        const attempts = this.failed_logins.get(req.ip) || { count: 0, reset: now + 60000 };
        if (attempts.reset <= now)
        {
            attempts.count = 0;
            attempts.reset = now + 60000;
        }
        if (attempts.count >= 10)
        {
            res.status(429).json({ ok: false, error: 'too many attempts' });
            return;
        }

        const submitted_password = String(req.body.password || '');
        const submitted = Buffer.from(submitted_password);
        const expected = Buffer.from(this.password);
        const valid = submitted.length === expected.length && crypto.timingSafeEqual(submitted, expected);
        if (valid)
        {
            this.failed_logins.delete(req.ip);
            req.session.auth = 1;
            res.status(200).json({ ok: true });
        }
        else
        {
            attempts.count += 1;
            this.failed_logins.set(req.ip, attempts);
            res.status(403).json({ ok: false, error: 'invalid password' });
        }
    }
    storeAPIKey(path)
    {
        fs.mkdirSync(require('path').dirname(path), { recursive: true, mode: 0o700 });
        fs.writeFileSync(path, this.apikey, { mode: 0o600 });
        fs.chmodSync(path, 0o600);
    }
}

module.exports = SimpleAuth;
