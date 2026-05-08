const randomstring = require('randomstring');
const fs = require('fs');

class SimpleAuth
{
    constructor(app)
    {
        this.password = process.env.CAT_WEB_PASSWORD || randomstring.generate(12);
        this.apikey = randomstring.generate(64);

        app.post('/api/auth', this.handleLogin.bind(this));
        app.use(this.middleware.bind(this));
    }
    middleware(req, res, next)
    {
        if (!req.session.auth && (req.ip == '::1' || req.ip == '127.0.0.1' || req.ip == '::ffff:127.0.0.1'))
        {
            console.log(`Auth: ${req.ip} by LOCAL`);
            req.session.auth = 1;
        }

        if (req.url.indexOf('api') < 0 || req.session.auth)
        {
            next();
            return;
        }

        if (req.query.key)
        {
            if (req.query.key === this.apikey)
            {
                console.log(`Auth: ${req.ip} by apikey`);
                req.session.auth = 1;
            }
            else
            {
                console.log(`Auth fail: ${req.ip} by invalid apikey ${req.query.key}`)
            }
        }

        if (!req.session.auth)
        {
            res.status(403).end('Not authorized');
            return;
        }
        next();
    }
    handleLogin(req, res)
    {
        const submitted_password = String(req.body.password || '').trim();
        if (submitted_password === this.password)
        {
            console.log(`Auth: ${req.ip} by password ${submitted_password}`);
            req.session.auth = 1;
            res.status(200).json({ ok: true });
        }
        else
        {
            console.log(`Auth fail: ${req.ip} by password ${submitted_password}`);
            res.status(403).json({ ok: false, error: 'invalid password' });
        }
    }
    storeAPIKey(path)
    {
        fs.writeFileSync(path, this.apikey);
    }
}

module.exports = SimpleAuth;
