const $ = require('jquery');
const format = require('format-duration');
const request = require('browser-request');
const steam_id = require('./steam_id');

const jquery_text = $.fn.text;
$.fn.text = function(value) {
	if (arguments.length === 0 || typeof value === 'function')
		return jquery_text.apply(this, arguments);

	const next = value == null ? '' : String(value);
	return this.each(function() {
		if (this.textContent !== next)
			this.textContent = next;
	});
};

const STATE = [
	'INITIALIZING',
	'INITIALIZED',
	'PREPARING',
	'STARTING',
	'WAITING',
	'RUNNING',
	'RESTARTING',
	'STOPPING',
	'NO ACCOUNT',
	'INVALID PASSWORD E5',
	'ACCOUNT DISABLED E43',
	'PENDING'
];
const MAX_BOT_QUOTA = 254;

const classes = [
	"Unknown", "Scout",
	"Sniper", "Soldier",
	"Demoman", "Medic",
	"Heavy", "Pyro",
	"Spy", "Engineer"
];

const teams = [
    "UNK", "SPEC", "RED", "BLU"
]

const status = {
    info: function(text) {
        console.log('[INFO]', text);
        $('#status-text').attr('class', '').text(text);
    },
    warning: function(text) {
        console.log('[WARNING]', text);
        $('#status-text').attr('class', 'warning').text(text);
    },
    error: function(text) {
        console.log('[ERROR]', text);
        $('#status-text').attr('class', 'error').text(text);
    }
}

var last_count = 0;
var refresh_in_progress = false;
var state_request_in_progress = false;
var poll_interval_ms = 1000;
var poll_timer = null;
var bot_rows = {};

function poll_interval_for_count(count) {
	if (count > 60)
		return 8000;
	if (count > 30)
		return 5000;
	return 1000;
}

function schedule_poll(delay) {
	if (poll_timer)
		clearTimeout(poll_timer);

	poll_timer = setTimeout(function() {
		poll_timer = null;
		updateData();
	}, delay);
}

function clear_bot_rows() {
	bot_rows = {};
}

function updateData() {
	if (state_request_in_progress) {
		schedule_poll(250);
		return;
	}

	state_request_in_progress = true;
	request('api/state', function(error, r, b) {
		state_request_in_progress = false;
		try {
			if (request_failed(error, r)) {
				if (r && r.statusCode === 403)
					status.error('Not authorized; log in with the panel password');
				else
					status.error('Error loading bot state from server!');
				return;
			}
			var data = parse_json_body(b);
			if (!data || !data.bots) {
				status.error('Error parsing bot state from server!');
				return;
			}
			if (last_count != Object.keys(data.bots).length) {
				refreshComplete();
			}
			last_count = Object.keys(data.bots).length;
			poll_interval_ms = poll_interval_for_count(last_count);
			for (var i in data.bots) {
				try {
					updateUserData(i, data.bots[i]);
				} catch (error) {
					console.log('Failed to update bot row', i, error, data.bots[i]);
					status.error('Error updating bot ' + i);
				}
			}
		} finally {
			schedule_poll(poll_interval_ms);
		}
	});
}

function commandButtonCallback() {
    var cmdz = prompt('Enter a command');
    if (cmdz) {
		const row = $(this).closest('tr');
		const target = Number.parseInt(String(row.attr('data-ipc-id') || row.find('.client-id').text()), 10);
		if (!Number.isFinite(target)) {
			status.error('Bot IPC id is not available yet');
			return;
		}
		cmd('exec', {
			target: target,
			cmd: cmdz
		}, function(e) {
			if (!e)
				status.info('Command sent');
		})
    }
}

function restartButtonCallback() {
	console.log('restarting',$(this).parent().parent().attr('data-id'));
	request.post({
		url: `api/bot/${$(this).parent().parent().attr('data-id')}/restart`,
		form: { confirm: 'restart-bot' }
	}, function(e, r, b) {
		if (request_failed(e, r)) {
			console.log(e,b);
			status.error('Error restarting bot');
		} else {
			status.info('Bot restarted');
		}
	});
}

function restartAllButtonCallback() {
	console.log('restarting all bots');
	request.post({
		url: 'api/bot/all/restart',
		form: { confirm: 'restart-all' }
	}, function(e, r, b) {
		if (request_failed(e, r)) {
			console.log(e,b);
			status.error('Error restarting bots');
		} else {
			status.info('Bots restarted');
		}
	});
}

function set_config(option, value, callback) {
	request.post({
		url: `api/config/${option}/${value ? 'true' : 'false'}`
	}, function(e, r, b) {
		if (e) {
			console.log(e, b);
			status.error('Error applying config');
			if (callback)
				callback(e);
			return;
		}
		if (callback)
			callback(null, b);
	});
}

function load_config_checkbox(option, selector) {
	request.get(`api/config/${option}`, function(e, r, b) {
		if (!e)
			$(selector).prop('checked', String(b).trim() === 'true');
	});
}

function load_config_number(option, selector) {
	request.get(`api/config/${option}`, function(e, r, b) {
		if (!e)
			$(selector).val(String(b).trim());
	});
}

function apply_config_number(option, selector, label) {
	const raw_value = String($(selector).val()).trim();
	if (!/^[0-9]+$/.test(raw_value)) {
		status.error(`${label} must be 0 or higher`);
		load_config_number(option, selector);
		return;
	}

	const value = Number.parseInt(raw_value, 10);
	if (!Number.isSafeInteger(value) || value < 0) {
		status.error(`${label} must be 0 or higher`);
		load_config_number(option, selector);
		return;
	}

	request.post({
		url: `api/config/${option}`,
		form: { value: value }
	}, function(e, r, b) {
		if (request_failed(e, r)) {
			console.log(e, b);
			status.error(`Error applying ${label}`);
			load_config_number(option, selector);
			return;
		}

		$(selector).val(String(b).trim());
		status.info(`Applied ${label}`);
	});
}

function request_failed(e, r) {
	return e || !r || r.statusCode < 200 || r.statusCode >= 300;
}

function parse_json_body(body) {
	try {
		return JSON.parse(body);
	} catch (error) {
		console.log(error);
		return null;
	}
}

function load_max_concurrent_bots() {
	request.get('api/concurrent', function(e, r, b) {
		if (request_failed(e, r)) {
			console.log(e, b);
			status.error('Error loading max concurrent');
			return;
		}

		const data = parse_json_body(b);
		if (data && data.value)
			$('#bot-concurrent').val(data.value);
	});
}

function apply_max_concurrent_bots() {
	const value = Number.parseInt($('#bot-concurrent').val(), 10);
	if (!Number.isFinite(value) || value < 1) {
		status.error('Max concurrent must be at least 1');
		load_max_concurrent_bots();
		return;
	}

	request.post({
		url: 'api/concurrent',
		form: { value: value }
	}, function(e, r, b) {
		if (request_failed(e, r)) {
			console.log(e, b);
			status.error('Error applying max concurrent!');
			load_max_concurrent_bots();
			return;
		}

		const data = parse_json_body(b);
		$('#bot-concurrent').val(data && data.value ? data.value : value);
		status.info('Applied max concurrent successfully');
	});
}

function load_bot_quota() {
	request.get('api/list', function(e, r, b) {
		if (request_failed(e, r))
			return;

		const data = parse_json_body(b);
		if (data && Number.isFinite(data.quota))
			$('#bot-quota').val(data.quota);
	});
}

function apply_bot_quota() {
	const value_text = String($('#bot-quota').val()).trim();
	if (!/^[0-9]+$/.test(value_text) || Number(value_text) > MAX_BOT_QUOTA) {
		status.error('Bot quota must be between 0 and ' + MAX_BOT_QUOTA);
		load_bot_quota();
		return;
	}

	request.post({
		url: 'api/quota',
		form: { value: value_text }
	}, function(e, r, b) {
		if (request_failed(e, r)) {
			console.log(e, b);
			status.error('Error applying bot quota!');
			load_bot_quota();
			return;
		}

		const data = parse_json_body(b);
		if (data && Number.isFinite(data.quota))
			$('#bot-quota').val(data.quota);
		status.info('Applied bot quota successfully');
	});
}

function terminateButtonCallback() {
	console.log('terminating',$(this).parent().parent().attr('data-id'));
	request.post({
		url: `api/bot/${$(this).parent().parent().attr('data-id')}/terminate`,
		form: { confirm: 'terminate-bot' }
	}, function(e, r, b) {
		if (request_failed(e, r)) {
			console.log(e,b);
			status.error('Error terminating bot');
		} else {
			status.info('Bot terminated');
		}
	});
}

function terminateAllButtonCallback() {
	console.log('terminating all bots');
	request.post({
		url: 'api/bot/all/terminate',
		form: { confirm: 'terminate-all' }
	}, function(e, r, b) {
		if (request_failed(e, r)) {
			console.log(e,b);
			status.error('Error terminating bots');
		} else {
			status.info('Bots terminated');
		}
	});
}

function cmd(command, data, callback) {
	request.post({
		url: 'api/direct/' + command,
		body: JSON.stringify(data),
		headers: {
			"Content-Type": "application/json"
		}
	}, function(e, r, b) {
		if (request_failed(e, r)) {
			console.log(e, b);
			if (r && r.statusCode === 403)
				status.error('Not authorized; log in with the panel password');
			else
				status.error('Command request failed');
			if (callback)
				callback(e || new Error(b || 'request failed'));
			return;
		}
		try {
			if (callback)
				callback(null, JSON.parse(b));
			else
				status.info('Command sent');
		} catch (e) {
			console.log(e);
			status.error('Error parsing data from server!');
			if (callback)
				callback(e);
		}
	});
}

var autorestart = {};

function update_ban_tracker_data(row, data) {
	if (!data) {
		row.find('.client-ban-tracker').text('N/A');
		return;
	}

	var text = data.status || 'unchecked';
	if (data.reason)
		text += ` (${data.reason})`;

	row.find('.client-ban-tracker')
		.removeClass('error warning')
		.toggleClass('warning', data.status === 'suspicious')
		.toggleClass('error', data.status === 'confirmed' || data.status === 'error')
		.text(text);
}

function clearIPCId(row) {
	row.removeAttr('data-ipc-id').removeAttr('data-pid');
	row.find('.client-pid, .client-id, .client-name, .client-status').text('N/A');
}

function updateIPCData(row, id, data, state, ipc_observed_at) {
	if (!data) {
		clearIPCId(row);
		return;
	}
	var accumulated = data.accumulated || {};
	var ingame = data.ingame || {};
	var heartbeat = Number(data.heartbeat);
	var ts_injected = Number(data.ts_injected);
	var observed_at = Number(ipc_observed_at);
	var observed_age = observed_at ? Math.floor((Date.now() - observed_at) / 1000) : 0;
	var time = Number.isFinite(heartbeat) ? Math.floor(Date.now() / 1000 - heartbeat) : 0;
	row.toggleClass('stale', observed_age > 30);
	if (observed_age > 30) {
		row.find('.client-status').removeClass('error').addClass('warning').text('Query stale ' + observed_age + 's (last known)');
		return;
	} else if (!data.heartbeat || time < 4) {
		row.find('.client-status').removeClass('error warning').text('OK ' + time);
	} else if (time < 45) {
		row.find('.client-status').removeClass('error').addClass('warning').text('Warning ' + time);
	} else {
		row.find('.client-status').removeClass('warning').addClass('error').text('Dead ' + time);
		if (state === 5 && $('#autorestart-bots').prop('checked')) {
			if ((Date.now() - ts_injected * 1000 > 20) && data.heartbeat && (!autorestart[row.attr('data-id')] || (Date.now() - autorestart[row.attr('data-id')]) > 1000 * 5)) {
				autorestart[row.attr('data-id')] = Date.now();
				console.log('auto-restarting', row.attr('data-id'));
			    request(`api/bot/${row.attr('data-id')}/autorestart`, function(e, r, b) {
					if (request_failed(e, r)) {
						if (r && r.statusCode === 409)
							return;
						console.log(e, b);
						status.error('Error restarting bot ' + JSON.stringify(data));
					} else {
						status.info('Unresponsive bot restarted');
					}
				});
			}
		}
	}
	row.find('.client-pid').text(data.pid);
	row.find('.client-id').text(id);
	row.attr('data-ipc-id', id);
	row.find('.client-name').text(data.name);
	row.find('.client-total').text(accumulated.score || 0);
	var hitrate = Math.floor((accumulated.shots ? accumulated.hits / accumulated.shots : 0) * 100);
	var hsrate = Math.floor((accumulated.hits ? accumulated.headshots / accumulated.hits : 0) * 100);
	row.find('.client-shots').text(accumulated.shots || 0);
	row.find('.client-hitrate').text(hitrate + '%');
	row.find('.client-hsrate').text(hsrate + '%');
	row.find('.client-uptime-total').text(Number.isFinite(ts_injected) ? format(Date.now() - ts_injected * 1000) : 'N/A');
	if (data.ts_queue_started) {
		row.find('.client-uptime-queue').text(format(Date.now() - data.ts_queue_started * 1000));
	} else if (data.connected && data.ts_disconnected && data.ts_connected > data.ts_disconnected) {
		row.find('.client-uptime-queue').text(format(1000 * (data.ts_connected - data.ts_disconnected)));
	} else if (!data.connected) {
		if (data.ts_disconnected) {
			row.find('.client-uptime-queue').text(format(Date.now() - data.ts_disconnected * 1000));
		} else {
			row.find('.client-uptime-queue').text(Number.isFinite(ts_injected) ? format(Date.now() - ts_injected * 1000) : 'N/A');
		}
	} else {
		row.find('.client-uptime-queue').text('N/A');
	}
	if (data.connected) {
		row.toggleClass('disconnected', false);
		row.toggleClass('stale', observed_age > 30);
		row.find('.client-uptime-server').text(format(Date.now() - data.ts_connected * 1000));
		row.find('.client-ip').text(ingame.server || 'N/A');
		row.find('.client-alive').text(ingame.life_state ? 'Dead' : 'Alive');
		row.find('.client-team').text(teams[ingame.team] || 'N/A');
		row.find('.client-class').text(classes[ingame.role] || 'N/A');
		row.find('.client-score').text(ingame.score || 0);
		row.find('.client-health').text((ingame.health || 0) + '/' + (ingame.health_max || 0));
        row.find('.client-map').text(ingame.mapname || 'N/A');
        row.find('.client-players').text(ingame.player_count || 0);
        row.find('.client-bots').text(ingame.bot_count || 0);
	} else {
		row.toggleClass('disconnected', true);
		row.find('.connected').text('N/A');
	}
}

function update_steam_guard_data(row, guard) {
	const cell = row.find('.client-steam-guard');
	if (!guard) {
		cell.text('N/A').attr('title', 'No maFile for this account');
		return;
	}
	if (guard.last_error) {
		cell.text('Error').attr('title', guard.last_error);
		return;
	}
	const confirmed = guard.last_approval ? ', last confirmed ' + new Date(guard.last_approval).toLocaleTimeString() : '';
	cell.text(guard.approvals ? 'Confirmed ' + guard.approvals : 'maFile').attr('title', guard.mafile + confirmed);
}

function updateUserData(bot, data) {
	var row = bot_rows[bot];
	if (!row || !row.length) {
		row = $(`tr[data-id="${bot}"]`);
		if (!row.length)
			return;
		bot_rows[bot] = row;
	}
	row.toggleClass('stopped', data.state != 5);
	row.find('.client-state').text(STATE[data.state]);
	row.find('.client-restarts').text(data.restarts);
	const ipc_id = data.ipcID;
	const has_ipc = data.state === 5 && data.ipc && Number.isInteger(ipc_id) && ipc_id >= 0;
	if (has_ipc) {
		row.attr('data-ipc-id', ipc_id);
		row.attr('data-pid', data.ipc.pid);
		row.find('.client-pid').text(data.ipc.pid);
		const profile_url = data.profile_url || steam_id.profile_url_from_account_id32(data.ipc.friendid);
		const steam_cell = row.find('.client-steam')[0];
		if (steam_cell && steam_cell.getAttribute('data-profile-url') !== (profile_url || '')) {
			steam_cell.setAttribute('data-profile-url', profile_url || '');
			$(steam_cell).empty();
			if (profile_url) {
				$(steam_cell).append($('<a></a>').text('Profile').attr('href', profile_url).attr('target', '_blank'));
			} else {
				$(steam_cell).text('N/A');
			}
		}
	}
	if (!has_ipc) {
		clearIPCId(row);
		row.find('.active').text('N/A');
	}
	update_steam_guard_data(row, data.steam_guard);
	update_ban_tracker_data(row, data.ban_tracker);
	updateIPCData(row, has_ipc ? ipc_id : -1, has_ipc ? data.ipc : null, data.state, data.ipc_observed_at);
}

function addClientRow(botid, data) {
	data = data || {};
	$("#clients tr").filter(function() {
		return $(this).attr('data-id') === String(botid);
	}).remove();

    var row = $('<tr></tr>').attr('data-id', botid).addClass('disconnected stopped');
    var actions = $('<td></td>').attr('class', 'client-actions');
    actions.append($('<input>').attr('type', 'button').attr('value', 'Command').on('click', commandButtonCallback));
    actions.append($('<input>').attr('type', 'button').attr('value', 'Restart').on('click', restartButtonCallback));
    actions.append($('<input>').attr('type', 'button').attr('value', 'Terminate').on('click', terminateButtonCallback));
	row.append(actions);
	row.append($('<td></td>').attr('class', 'client-restarts').text('N/A'));
	row.append($('<td></td>').attr('class', 'client-bot-name').text(botid));
	row.append($('<td></td>').attr('class', 'client-state').text(STATE[data.state] || 'PENDING'));
	row.append($('<td></td>').attr('class', 'client-steam').text('N/A'));
	row.append($('<td></td>').attr('class', 'client-steam-guard').text('N/A'));
	row.append($('<td></td>').attr('class', 'client-ban-tracker active').text('N/A'));
    row.append($('<td></td>').attr('class', 'client-uptime-total active').text('N/A'));
    row.append($('<td></td>').attr('class', 'client-pid active').text('N/A'));
	row.append($('<td></td>').attr('class', 'client-id active').text('N/A'));
    row.append($('<td></td>').attr('class', 'client-status active').text('N/A'));
    row.append($('<td></td>').attr('class', 'client-name active').text('N/A'));
    row.append($('<td></td>').attr('class', 'client-uptime-queue active').text('N/A'));
    row.append($('<td></td>').attr('class', 'client-total active').text('N/A'));
    row.append($('<td></td>').attr('class', 'client-score connected active').text('N/A'));
    row.append($('<td></td>').attr('class', 'client-shots active').text('N/A'));
    row.append($('<td></td>').attr('class', 'client-hitrate active').text('N/A'));
    row.append($('<td></td>').attr('class', 'client-hsrate active').text('N/A'));
    row.append($('<td></td>').attr('class', 'client-uptime-server connected active').text('N/A'));
    row.append($('<td></td>').attr('class', 'client-alive connected active').text('N/A'));
    row.append($('<td></td>').attr('class', 'client-team connected active').text('N/A'));
    row.append($('<td></td>').attr('class', 'client-class connected active').text('N/A'));
    row.append($('<td></td>').attr('class', 'client-health connected active').text('N/A'));
    row.append($('<td></td>').attr('class', 'client-ip connected active').text('N/A'));
    row.append($('<td></td>').attr('class', 'client-map connected active').text('NYI'));
    row.append($('<td></td>').attr('class', 'client-players connected active').text('NYI'));
    row.append($('<td></td>').attr('class', 'client-bots connected active').text('NYI'));
    $('#clients').append(row);
    bot_rows[botid] = row;
    return row;
}

function runCommand() {
	const command_text = $('#console').val();
	if (!command_text)
		return;

	cmd('exec_all', { cmd: command_text }, function(e) {
		if (!e)
			$('#console').val('');
	});
}

function refreshComplete() {
	if (refresh_in_progress)
		return;

	refresh_in_progress = true;
	request.get({
		url: 'api/list'
	}, function(e, r, b) {
		refresh_in_progress = false;
		if (request_failed(e, r)) {
			console.log(e, b);
			if (r && r.statusCode === 403)
				status.error('Not authorized; log in with the panel password');
			else
				status.error('Error refreshing the list!');
			return;
		}

		var count = 0;
		var data = parse_json_body(b);
		if (!data || !data.bots) {
			status.error('Error parsing bot list!');
			return;
		}

		console.log(data.bots ? Object.keys(data.bots).length + ' bots' : data);
		clear_bot_rows();
		$("#clients tr").slice(1).remove();
		var bot_names = Object.keys(data.bots).sort(function(left, right) {
			var left_id = Number(String(left).replace(/^b/, ''));
			var right_id = Number(String(right).replace(/^b/, ''));
			return right_id - left_id;
		});
		for (var index = 0; index < bot_names.length; index++) {
			var i = bot_names[index];
			count++;
			addClientRow(i, data.bots[i])
		}
		last_count = count;
	})
}

$(function() {
	updateData();
    status.info('init done');
	$('#console').on('keypress', function(e) {
		if (e.keyCode === '13') {
			runCommand();
			e.preventDefault();
		}
	});
	$('#bot-quota-apply').on('click', apply_bot_quota);
	$('#bot-quota').on('keypress', function(e) {
		if (e.keyCode === 13) {
			apply_bot_quota();
			e.preventDefault();
		}
	});
	$('#bot-concurrent-apply').on('click', apply_max_concurrent_bots);
	$('#bot-concurrent').on('keypress', function(e) {
		if (e.keyCode === 13) {
			apply_max_concurrent_bots();
			e.preventDefault();
		}
	}).on('change', function() {
		apply_max_concurrent_bots();
	});
	load_bot_quota();
	load_max_concurrent_bots();
    $('#api-login-button').on('click', () => {
        const password = String($('#api-password').val() || '').trim();
        request.post({
            uri: "/api/auth",
            form: {
                password: password
            }
        }, (e, r, b) => {
            if (e || !r || r.statusCode !== 200) {
                if (r && r.statusCode === 403)
                    status.error('Login failed: invalid password');
                else
                    status.error('Login failed');
                return;
            }

            status.info('Logged in');
            updateData();
            refreshComplete();
        });
    });
	$('#bot-refresh').on('click', refreshComplete);
	$('#console-send').on('click', runCommand);
	$("#bot-restartall").on('click', restartAllButtonCallback);
	$("#bot-terminateall").on('click', terminateAllButtonCallback);
	$('#ban-tracker-enabled').on('change', function() {
		set_config('ban_tracker_enabled', $(this).prop('checked'), function(e) {
			if (!e)
				status.info('Account ban tracker ' + ($('#ban-tracker-enabled').prop('checked') ? 'enabled' : 'disabled'));
		});
	});
	load_config_checkbox('ban_tracker_enabled', '#ban-tracker-enabled');
	$('#steam-login-timeout-apply').on('click', function() {
		apply_config_number('auto_restart_steam_if_not_logged_within', '#steam-login-timeout', 'Steam login timeout');
	});
	$('#steam-login-timeout').on('keypress', function(e) {
		if (e.keyCode === 13) {
			apply_config_number('auto_restart_steam_if_not_logged_within', '#steam-login-timeout', 'Steam login timeout');
			e.preventDefault();
		}
	}).on('change', function() {
		apply_config_number('auto_restart_steam_if_not_logged_within', '#steam-login-timeout', 'Steam login timeout');
	});
	load_config_number('auto_restart_steam_if_not_logged_within', '#steam-login-timeout');
});
