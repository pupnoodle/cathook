#!/bin/bash

set -u

panel_dir="${PUP_PANEL_DIR:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
puphook_root="${PUPHOOK_ROOT:-/opt/puphook}"
server_path="${PUP_IPC_SERVER_PATH:-$puphook_root/ipc/bin/server}"
state_path="${PUP_IPC_STATE_PATH:-$puphook_root/ipc/puphook_followbot_server}"
server_log="${PUP_IPC_SERVER_LOG:-$panel_dir/ipc-server.log}"

if [ ! -x "$server_path" ]; then
	echo "[ipc] server binary not found: $server_path"
	exit 0
fi

server_pids=()
for proc_dir in /proc/[0-9]*; do
	pid="${proc_dir##*/}"
	[ "$pid" = "$$" ] && continue
	[ -r "$proc_dir/cmdline" ] || continue
	cmdline="$(tr '\0' ' ' <"$proc_dir/cmdline" 2>/dev/null || true)"
	case "$cmdline" in
		"$server_path"|"$server_path "*)
			server_pids+=("$pid")
			;;
	esac
done

server_is_blocked() {
	local pid="$1"
	local wchan=""
	if [ -r "/proc/$pid/wchan" ]; then
		wchan="$(cat "/proc/$pid/wchan" 2>/dev/null || true)"
	fi
	case "$wchan" in
		*futex*|*mutex*) return 0 ;;
		*) return 1 ;;
	esac
}

healthy_pid=""
for pid in "${server_pids[@]}"; do
	if [ -e "/proc/$pid" ] && [ -e "$state_path" ] && ! server_is_blocked "$pid"; then
		healthy_pid="$pid"
		break
	fi
done

if [ -n "$healthy_pid" ]; then
	for pid in "${server_pids[@]}"; do
		if [ "$pid" != "$healthy_pid" ] && server_is_blocked "$pid"; then
			kill -TERM "$pid" 2>/dev/null || true
		fi
	done
	echo "[ipc] server healthy pid=$healthy_pid"
	exit 0
fi

for pid in "${server_pids[@]}"; do
	kill -TERM "$pid" 2>/dev/null || true
done
sleep 1
for pid in "${server_pids[@]}"; do
	if [ -e "/proc/$pid" ]; then
		kill -KILL "$pid" 2>/dev/null || true
	fi
done

mkdir -p "$(dirname -- "$server_log")"
nohup "$server_path" --reset -s >>"$server_log" 2>&1 </dev/null &
new_pid="$!"
echo "[ipc] started fresh server pid=$new_pid"
