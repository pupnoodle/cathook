#!/bin/bash

# This script is intended to attach GDB and inject, and not much else.
# That means no automatic log display. Open a seperate shell and `tail -f /tmp/tf2.log` for active logs.

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck source=/dev/null
source "$SCRIPT_DIR/cathook_mode.sh"

LIB_NAME="${CATHOOK_BINARY:-}"
if selected_mode="$(cathook_mode_from_env 0)"; then
    LIB_NAME="$(cathook_binary_for_mode "$selected_mode")"
elif [ -z "$LIB_NAME" ]; then
    selected_mode="$(cathook_select_mode 0)"
    LIB_NAME="$(cathook_binary_for_mode "$selected_mode")"
fi

LIB_PATH="$SCRIPT_DIR/bin/$LIB_NAME"
PROCID=$(pgrep tf_linux64 | head -n 1)

if [[ "$(execstack -q "$LIB_PATH")" = "X $LIB_PATH" ]]; then
    execstack -c "$LIB_PATH"
fi

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root"
    exit 1
fi

if [ -z "$PROCID" ]; then
    echo "Please open game"
    exit 1
fi

if [ "${CATHOOK_USE_GDB:-1}" != "1" ]; then
    echo "debug.sh uses a live gdb attach and was disabled by CATHOOK_USE_GDB=0."
    exit 1
fi

sudo gdb -n -q -ex "attach $PROCID" \
     -ex "call ((void * (*) (const char*, int)) dlopen)(\"$LIB_PATH\", 1)" \
     -ex "alias un = call call (int (*)(void*))dlclose($1)" \
     -ex "continue"
