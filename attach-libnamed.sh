#!/usr/bin/env bash
# gdb inject libcathook.so into live linux64 tf_linux64.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LIB="${LIB:-$ROOT/bin/libcathook.so}"
GAME_BIN="${GAME_BIN:-tf_linux64}"

if [ ! -f "$LIB" ]; then
    echo "Missing $LIB" >&2
    exit 1
fi

# Same-user gdb attach works; do not require root.

PROCID="${1:-}"
if [ -z "$PROCID" ]; then
    mapfile -t pids < <(pgrep -x "$GAME_BIN" || true)
    if [ "${#pids[@]}" -eq 0 ]; then
        echo "TF2 ($GAME_BIN) isn't running" >&2
        exit 1
    fi
    PROCID="${pids[0]}"
fi

if [ ! -r "/proc/$PROCID/maps" ]; then
    echo "Cannot read /proc/$PROCID/maps" >&2
    exit 1
fi

STAGE_DIR="$(mktemp -d /tmp/cathook-runtime-XXXXXX)"
STAGE_LIB="$STAGE_DIR/libcathook.so"
install -m 0755 "$LIB" "$STAGE_LIB"
chmod 0755 "$STAGE_DIR"

gdb -n -q --batch \
    -ex "set pagination off" \
    -ex "set confirm off" \
    -ex "attach $PROCID" \
    -ex "call ((void *(*)(const char *, int)) dlopen)(\"$STAGE_LIB\", 1)" \
    -ex "call ((char *(*)(void)) dlerror)()" \
    -ex "detach" \
    -ex "quit"
