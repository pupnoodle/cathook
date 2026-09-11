const BotManager = require('./botmanager');
const config = require('./config');
const Bot = require('./bot');

var manager = null;

function parse_config_value(value) {
	if (value === 'true')
		return true;
	if (value === 'false')
		return false;
	if (value === '1')
		return true;
	if (value === '0')
		return false;
	return value;
}

function parse_positive_integer(value) {
	const text = String(value).trim();
	if (!/^[0-9]+$/.test(text))
		return null;

	const number = Number.parseInt(text, 10);
	if (!Number.isSafeInteger(number) || number < 1)
		return null;

	return number;
}

function parse_nonnegative_integer(value) {
	const text = String(value).trim();
	if (!/^[0-9]+$/.test(text))
		return null;

	const number = Number.parseInt(text, 10);
	if (!Number.isSafeInteger(number))
		return null;

	return number;
}

function has_config_option(option) {
	return Object.prototype.propertyIsEnumerable.call(config, option);
}

function set_config_value(option, raw_value) {
	if (option === 'max_concurrent_bots' || option === 'steam_boot_concurrency') {
		const value = parse_positive_integer(raw_value);
		if (value === null)
			return null;
		return value;
	}

	if (option === 'chunked_x_display_base' ||
		option === 'chunked_x_display_bots_per_display' ||
		option === 'chunked_x_display_max_displays' ||
		option === 'chunked_x_display_max_clients') {
		const value = parse_positive_integer(raw_value);
		if (value === null)
			return null;
		return value;
	}

	if (option === 'auto_restart_steam_if_not_logged_within') {
		const value = parse_nonnegative_integer(raw_value);
		if (value === null)
			return null;
		return value;
	}

	return parse_config_value(raw_value);
}

function save_config(res) {
	try {
		config.save_settings();
		return true;
	} catch (error) {
		console.log(`[ERROR] Failed to save ${config.settings_path}: ${error.message}`);
		res.status(500).send({ error: 'Failed to save config' });
		return false;
	}
}

function apply_config_value(option, raw_value, res) {
	if (!has_config_option(option)) {
		res.status(404).end();
		return;
	}

	const value = set_config_value(option, raw_value);
	if (value === null) {
		res.status(400).send({ error: 'Invalid value' });
		return;
	}

	const previous_value = config[option];
	config[option] = value;
	if (!save_config(res)) {
		config[option] = previous_value;
		return;
	}

	console.log(`Switching ${option} to ${value}`);
	res.status(200).end('' + config[option]);
}

class app {
	constructor(app, cc) {
		if (process.getuid() != 0) {
			console.log('[FATAL] Bot manager needs superuser privileges, please restart as root');
			process.exit(1);
		}
		if (manager) {
			console.log('[FATAL] Initialized function for bot manager called twice');
			process.exit(1);
		} else {
			manager = new BotManager(cc);
		}

		this.manager = manager;

		app.post('/api/config/:option/:value', (req, res) => {
			apply_config_value(req.params.option, req.params.value, res);
		});
		app.post('/api/config/:option', (req, res) => {
			apply_config_value(req.params.option, req.body.value, res);
		});
		app.get('/api/config/:option', (req, res) => {
			if (!has_config_option(req.params.option))
				res.status(404).end();
			else
				res.status(200).end('' + config[req.params.option]);
		});

		app.get('/api/list', function (req, res) {
			res.set('Cache-Control', 'no-store').type('application/json').send(manager.get_list_json());
		});

		app.get('/api/state', function (req, res) {
			res.set('Cache-Control', 'no-store').type('application/json').send(manager.get_state_json());
		});

		app.post('/api/bot/:bot/restart', function (req, res) {
			if (req.params.bot === "all") {
				if (req.body.confirm !== 'restart-all') {
					res.status(409).send({ error: 'restart all requires confirm=restart-all' });
					return;
				}
				const processes = Bot.read_process_table(true);
				const children_by_parent = Bot.build_process_children_by_parent(processes);
				for (var bot of manager.bots) {
					bot.kill_existing_runtime_processes(processes, children_by_parent);
					bot.restart();
				}
				manager.request_update(0);
				res.status(200).end();
				return;
			}
			if (req.body.confirm !== 'restart-bot') {
				res.status(409).send({ error: 'bot restart requires confirm=restart-bot' });
				return;
			}
			var bot = manager.bot(req.params.bot);
			if (bot) {
				const processes = Bot.read_process_table(true);
				const children_by_parent = Bot.build_process_children_by_parent(processes);
				bot.kill_existing_runtime_processes(processes, children_by_parent);
				bot.restart();
				manager.request_update(0);
				res.status(200).end();
			} else {
				res.status(400).send({
					'error': 'Bot does not exist'
				})
			}
		});

		app.get('/api/bot/:bot/autorestart', function (req, res) {
			var bot = manager.bot(req.params.bot);
			if (bot) {
				if (bot.auto_restart('panel nonresponsive autorestart')) {
					res.status(200).end();
				} else {
					res.status(409).send({
						'error': 'Bot is not eligible for automatic restart'
					});
				}
			} else {
				res.status(400).send({
					'error': 'Bot does not exist'
				})
			}
		});

		app.post('/api/bot/:bot/terminate', function (req, res) {
			if (req.params.bot === "all") {
				if (req.body.confirm !== 'terminate-all') {
					res.status(409).send({ error: 'terminate all requires confirm=terminate-all' });
					return;
				}
				const processes = Bot.read_process_table(true);
				const children_by_parent = Bot.build_process_children_by_parent(processes);
				for (var bot of manager.bots)
					bot.terminate(processes, children_by_parent);
				manager.request_update(0);
				res.status(200).end();
				return;
			}
			if (req.body.confirm !== 'terminate-bot') {
				res.status(409).send({ error: 'bot termination requires confirm=terminate-bot' });
				return;
			}
			var bot = manager.bot(req.params.bot);
			if (bot) {
				const processes = Bot.read_process_table(true);
				const children_by_parent = Bot.build_process_children_by_parent(processes);
				bot.terminate(processes, children_by_parent);
				manager.request_update(0);
				res.status(200).end();
			} else {
				res.status(400).send({
					'error': 'Bot does not exist'
				})
			}
		});

		app.get('/api/quota/:quota', function (req, res) {
			if (!manager.setQuota(req.params.quota)) {
				res.status(400).send({ error: 'Invalid quota' });
				return;
			}
			res.send({
				quota: manager.quota
			});
		});

		app.post('/api/quota', function (req, res) {
			if (!manager.setQuota(req.body.value)) {
				res.status(400).send({ error: 'Invalid quota' });
				return;
			}
			res.send({
				quota: manager.quota
			});
		});

		const set_concurrent_limit = function (raw_value, res) {
			const value = parse_positive_integer(raw_value);
			if (value === null) {
				res.status(400).send({ error: 'Invalid value' });
				return;
			}

			const previous_value = Bot.MAX_CONCURRENT_BOTS;
			Bot.MAX_CONCURRENT_BOTS = value;
			if (!save_config(res)) {
				Bot.MAX_CONCURRENT_BOTS = previous_value;
				return;
			}

			res.status(200).send({ value: Bot.MAX_CONCURRENT_BOTS });
		};

		app.post('/api/concurrent', function (req, res) {
			set_concurrent_limit(req.body.value, res);
		});

		app.get('/api/concurrent/:value', function (req, res) {
			set_concurrent_limit(req.params.value, res);
		});

		app.get('/api/concurrent', function (req, res) {
			res.status(200).send({ value: Bot.MAX_CONCURRENT_BOTS });
		});
	}

	stop()
	{
		return this.manager.stop();
	}
}

exports.Forever = app;
