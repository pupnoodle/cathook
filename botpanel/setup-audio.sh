#!/usr/bin/env bash
#
# setup-audio.sh - prepare the audio the bots need, on PulseAudio or PipeWire.
#
# Two things have to exist before micspam works:
#
#   1. a capture device. burgerhook's micspam feeds voice_input.wav through the
#      engine, not a real microphone, but TF2 will not start its voice subsystem
#      at all unless *some* source is present - so a headless box needs a
#      virtual one.
#   2. a pulse socket at /tmp/pulse.sock. forever/bot.js launches every instance
#      with PULSE_SERVER="unix:/tmp/pulse.sock", and firejail --private gives
#      each jail an empty home, so the socket also has to accept anonymous
#      clients: the jail cannot read ~/.config/pulse/cookie.
#
# Idempotent - safe to re-run, and safe to call from start.
#
# Usage: ./setup-audio.sh [--socket PATH] [--check] [--verbose]
# Made by HappyKuro

set -euo pipefail

SOCKET="/tmp/pulse.sock"
CHECK_ONLY=0
VERBOSE=0

SINK_NAME="virtmic_sink"
SOURCE_NAME="virtmic"

die() { printf 'error: %s\n' "$1" >&2; exit 1; }
info() { printf '%s\n' "$1"; }
debug() { [[ "$VERBOSE" -eq 1 ]] && printf '  %s\n' "$1" || true; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --socket) SOCKET="${2:-}"; [[ -n "$SOCKET" ]] || die "--socket needs a path"; shift 2 ;;
        --check)   CHECK_ONLY=1; shift ;;
        --verbose) VERBOSE=1; shift ;;
        -h|--help) sed -n '3,18p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown option: $1" ;;
    esac
done

# Not with sudo, and this has to be caught before anything else is attempted.
#
# PipeWire and PulseAudio are per-user session services. Under sudo the effective user is
# root, whose XDG_RUNTIME_DIR is not the one holding your session's socket - so pactl finds
# no server and the check below reports "no audio server reachable", sending you off to
# investigate a daemon that is running perfectly well.
#
# Nothing here wants root anyway: the config drop-in goes in your ~/.config and the restart
# is systemctl --user, which under sudo would address root's service manager instead of the
# one actually running your audio.
if [[ "${EUID}" -eq 0 ]]; then
    if [[ -n "${SUDO_USER:-}" ]]; then
        die "run this as ${SUDO_USER}, not with sudo - PipeWire/PulseAudio is a per-user service and root cannot see your session (try: ./$(basename "${BASH_SOURCE[0]}"))"
    fi
    die "run this as your normal user, not as root - PipeWire/PulseAudio is a per-user service"
fi

command -v pactl >/dev/null 2>&1 || die "pactl not found - install pulseaudio-utils"

# pactl talks to whichever server is running; pipewire-pulse implements the same
# interface, so the client tools are identical and only the socket setup differs.
if ! server_name="$(pactl info 2>/dev/null | sed -n 's/^Server Name: //p')"; then
    die "no audio server reachable - is pipewire-pulse or pulseaudio running? (if you used sudo, do not)"
fi

if printf '%s' "$server_name" | grep -qi pipewire; then
    BACKEND="pipewire"
else
    BACKEND="pulseaudio"
fi

info "audio server : $server_name"
info "backend      : $BACKEND"
info "socket       : $SOCKET"

have_source() { pactl list sources short 2>/dev/null | awk '{print $2}' | grep -qx "$1"; }
have_sink()   { pactl list sinks   short 2>/dev/null | awk '{print $2}' | grep -qx "$1"; }

socket_live() {
    [[ -S "$SOCKET" ]] && PULSE_SERVER="unix:$SOCKET" pactl info >/dev/null 2>&1
}

if [[ "$CHECK_ONLY" -eq 1 ]]; then
    status=0
    have_source "$SOURCE_NAME" && info "ok   source $SOURCE_NAME" || { info "MISSING source $SOURCE_NAME"; status=1; }
    socket_live && info "ok   socket $SOCKET" || { info "MISSING or dead socket $SOCKET"; status=1; }

    if socket_live && ! PULSE_SERVER="unix:$SOCKET" pactl list sources short 2>/dev/null \
        | awk '{print $2}' | grep -qx "$SOURCE_NAME"; then
        # The distinction that actually bites: modules loaded into the desktop
        # session are invisible to the bots, and everything looks configured.
        info "MISSING $SOURCE_NAME is not visible on $SOCKET (wrong daemon)"
        status=1
    fi
    exit "$status"
fi

# ---------------------------------------------------------------------------
# 1. socket (PipeWire first - restarting it discards runtime modules)
# ---------------------------------------------------------------------------
# Ordering matters and cost one debugging round: on PipeWire the socket needs a
# config drop-in plus a service restart, and that restart throws away every
# module loaded with pactl. Creating the virtual mic first meant the restart
# silently deleted it, leaving a live socket with no source on it.
#
# So on PipeWire everything - socket AND modules - goes in the drop-in and one
# restart establishes the lot. It also means the setup survives later restarts
# rather than evaporating the next time the service bounces.
if [[ "$BACKEND" = "pipewire" ]] && ! socket_live; then
    conf_dir="$HOME/.config/pipewire/pipewire-pulse.conf.d"
    conf_file="$conf_dir/20-botpanel-socket.conf"

    mkdir -p "$conf_dir"
    cat > "$conf_file" <<EOF
# Written by botpanel/setup-audio.sh
#
# Extra listen address for the bot jails. They get an empty home from
# firejail --private and cannot read the pulse cookie, hence unrestricted.
pulse.properties = {
    server.address = [
        "unix:native"
        { address = "unix:$SOCKET" client.access = "unrestricted" }
    ]
}

# The virtual mic lives here rather than being loaded with pactl, so a service
# restart does not delete it.
pulse.cmd = [
    { cmd = "load-module" args = "module-null-sink sink_name=$SINK_NAME sink_properties=device.description=VirtualMicSink" }
    { cmd = "load-module" args = "module-remap-source master=$SINK_NAME.monitor source_name=$SOURCE_NAME source_properties=device.description=VirtualMic" }
]
EOF
    info "wrote $conf_file"

    command -v systemctl >/dev/null 2>&1 || die "no systemctl - restart pipewire-pulse by hand, then re-run"
    info "restarting pipewire-pulse"
    systemctl --user restart pipewire-pulse || die "restart failed - systemctl --user status pipewire-pulse"

    for _ in $(seq 1 20); do
        socket_live && break
        sleep 0.25
    done
fi

# ---------------------------------------------------------------------------
# 2. virtual microphone
# ---------------------------------------------------------------------------
# The sink and source are named differently on purpose. Giving both "virtmic"
# makes the server disambiguate the second one to "virtmic.2", and that suffix
# is not stable across reloads - so set-default-source virtmic then fails.
if have_sink "$SINK_NAME"; then
    debug "sink $SINK_NAME already present"
else
    info "creating sink $SINK_NAME"
    pactl load-module module-null-sink \
        sink_name="$SINK_NAME" \
        sink_properties=device.description=VirtualMicSink >/dev/null
fi

if have_source "$SOURCE_NAME"; then
    debug "source $SOURCE_NAME already present"
else
    info "creating source $SOURCE_NAME"
    pactl load-module module-remap-source \
        master="$SINK_NAME.monitor" \
        source_name="$SOURCE_NAME" \
        source_properties=device.description=VirtualMic >/dev/null
fi

pactl set-default-source "$SOURCE_NAME"

# ---------------------------------------------------------------------------
# 3. socket, PulseAudio path
# ---------------------------------------------------------------------------
if socket_live; then
    info "socket already serving"
elif [[ "$BACKEND" = "pulseaudio" ]]; then
    info "publishing socket via module-native-protocol-unix"
    pactl load-module module-native-protocol-unix \
        socket="$SOCKET" auth-anonymous=1 >/dev/null
else
    # PipeWire is handled entirely in section 1, config and modules together.
    # Reaching here means that restart did not bring the socket up. Do NOT
    # rewrite the drop-in from this branch - an earlier version did, and it
    # replaced the good config (socket + modules) with a socket-only one,
    # deleting the virtual mic on the next restart.
    die "socket $SOCKET did not come up after restarting pipewire-pulse - check: systemctl --user status pipewire-pulse"
fi

# ---------------------------------------------------------------------------
# 4. verify against the socket the bots actually use
# ---------------------------------------------------------------------------
# Checking the local session proves nothing: the failure this catches is modules
# loaded into the desktop daemon while the bots connect somewhere else.
if ! socket_live; then
    die "socket $SOCKET is still not answering"
fi

if PULSE_SERVER="unix:$SOCKET" pactl list sources short 2>/dev/null \
    | awk '{print $2}' | grep -qx "$SOURCE_NAME"; then
    info "verified: $SOURCE_NAME is visible on $SOCKET"
else
    die "$SOURCE_NAME exists locally but not on $SOCKET - the modules went to a different daemon"
fi

info "audio ready"
