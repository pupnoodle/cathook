#!/usr/bin/env bash

PROCID=""
GAME_BINARY_PATH=""
CRASH_WATCHER_PID=""
TAIL_PID=""
TMP_RUNTIME_DIR=""
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck source=/dev/null
source "$SCRIPT_DIR/cathook_mode.sh"

CATHOOK_ROOT=${CATHOOK_ROOT:-/opt/cathook}
CATHOOK_BIN_DIR=$CATHOOK_ROOT/bin
CATHOOK_LOG_DIR=$CATHOOK_ROOT/logs
CATHOOK_ASSET_DIR=$CATHOOK_ROOT/assets
CATHOOK_SOURCE_ASSET_DIR="$SCRIPT_DIR/assets"
CATHOOK_BINARY=${CATHOOK_BINARY:-}
CATHOOK_CONFIG_DIR=${CATHOOK_CONFIG_DIR:-$CATHOOK_ROOT/config}
CATHOOK_AUTO_UPDATE_FILE=${CATHOOK_AUTO_UPDATE_FILE:-$CATHOOK_CONFIG_DIR/auto_update}
CATHOOK_ATTACH_DELAY_SECONDS=${CATHOOK_ATTACH_DELAY_SECONDS:-0}
CATHOOK_USE_GDB=${CATHOOK_USE_GDB:-0}
CATHOOK_GDB_CRASH_REPORTS=${CATHOOK_GDB_CRASH_REPORTS:-0}
CATHOOK_GDB_KEEP_CORE=${CATHOOK_GDB_KEEP_CORE:-0}
CATHOOK_DISCORD_REPORTS=${CATHOOK_DISCORD_REPORTS:-1}
# pls dont spam it, i need it to fix ze bugs und crashes! :(
CATHOOK_DISCORD_WEBHOOK_URL=${CATHOOK_DISCORD_WEBHOOK_URL:-"https://discord.com/api/webhooks/1501401839831093420/2CNm0glVBv3rRw8-nMGS6uZG8vY3wy1O2a_KLhcJVQvA5P1vRg7GFfIbh8J7OZudj5P7"}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --gdb)
            CATHOOK_USE_GDB=1
            ;;
        --no-gdb)
            CATHOOK_USE_GDB=0
            ;;
        --gdb-crash-reports)
            CATHOOK_GDB_CRASH_REPORTS=1
            ;;
        --no-gdb-crash-reports)
            CATHOOK_GDB_CRASH_REPORTS=0
            ;;
        --dev | --no-update)
            export CATHOOK_DEV_MODE=1
            ;;
        -h | --help)
            echo "Usage: sudo ./inject.sh [--gdb|--no-gdb|--gdb-crash-reports|--no-gdb-crash-reports|--dev|--no-update]"
            echo "Mode: CATHOOK_MODE=default|textmode, CATHOOK_TEXTMODE=1, TEXTMODE=1, or saved first-run choice."
            echo "GDB injection is disabled by default. Use ./preload for a no-gdb launch or --gdb to force attach injection."
            exit 0
            ;;
        *)
            echo "Unknown inject option: $1" >&2
            echo "Usage: sudo ./inject.sh [--gdb|--no-gdb|--gdb-crash-reports|--no-gdb-crash-reports|--dev|--no-update]" >&2
            exit 1
            ;;
    esac
    shift
done

if [[ ! "$CATHOOK_ATTACH_DELAY_SECONDS" =~ ^[0-9]+$ ]]; then
    CATHOOK_ATTACH_DELAY_SECONDS=0
fi

if selected_mode="$(cathook_mode_from_env 0)"; then
    CATHOOK_BINARY="$(cathook_binary_for_mode "$selected_mode")"
elif [ -z "$CATHOOK_BINARY" ]; then
    selected_mode="$(cathook_select_mode 0)"
    CATHOOK_BINARY="$(cathook_binary_for_mode "$selected_mode")"
fi

LIB_PATH="$CATHOOK_BIN_DIR/$CATHOOK_BINARY"
if [ ! -f "$LIB_PATH" ] && [ -f "$(pwd)/bin/$CATHOOK_BINARY" ]; then
    LIB_PATH="$(pwd)/bin/$CATHOOK_BINARY"
fi

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root"
    exit 1
fi

is_enabled() {
    case "${1:-}" in
        ""|0|false|FALSE|no|NO|off|OFF)
            return 1
            ;;
    esac

    return 0
}

is_dev_mode() {
    is_enabled "${CATHOOK_DEV_MODE:-${CAT_DEV_MODE:-0}}"
}

require_gdb_injection_enabled() {
    if is_enabled "$CATHOOK_USE_GDB"; then
        return
    fi

    echo "gdb injection is disabled for now."
    echo "Use ./preload to launch TF2 without gdb, or run CATHOOK_USE_GDB=1 sudo ./inject.sh if you explicitly need attach injection."
    exit 1
}

send_discord_report() {
    local file_path="$1"
    local report_name="$2"
    local host_name=""

    if ! is_enabled "$CATHOOK_DISCORD_REPORTS"; then
        return
    fi

    if [ -z "$CATHOOK_DISCORD_WEBHOOK_URL" ]; then
        return
    fi

    if [ ! -s "$file_path" ]; then
        return
    fi

    if ! command -v curl >/dev/null 2>&1; then
        echo "discord report skipped: curl is missing"
        return
    fi

    host_name=$(hostname 2>/dev/null || printf '%s' "unknown-host")

    if curl --fail --silent --show-error --max-time 60 \
        -F "content=$report_name from $host_name at $(date --iso-8601=seconds)" \
        -F "files[0]=@$file_path;filename=$(basename -- "$file_path")" \
        "$CATHOOK_DISCORD_WEBHOOK_URL" >/dev/null; then
        echo "Sent $report_name to Discord: $file_path"
    else
        echo "Failed to send $report_name to Discord: $file_path"
    fi
}

send_exception_reports() {
    local report_path=""

    for report_path in "$CATHOOK_LOG_DIR"/exception*.log "$CATHOOK_ROOT"/exception*.log; do
        if [ -f "$report_path" ]; then
            send_discord_report "$report_path" "cathook exception report"
        fi
    done
}

normalize_yes_no() {
    case "${1:-}" in
        y|Y|yes|YES|Yes|1|true|TRUE|True|on|ON|On)
            printf '%s\n' "yes"
            ;;
        n|N|no|NO|No|0|false|FALSE|False|off|OFF|Off)
            printf '%s\n' "no"
            ;;
        *)
            return 1
            ;;
    esac
}

save_auto_update_preference() {
    local preference="$1"
    local owner_user="${SUDO_USER:-}"
    local owner_group=""
    local preference_dir=""

    preference_dir="$(dirname -- "$CATHOOK_AUTO_UPDATE_FILE")"
    mkdir -p "$preference_dir"
    printf '%s\n' "$preference" > "$CATHOOK_AUTO_UPDATE_FILE"

    if [ -n "$owner_user" ]; then
        owner_group=$(id -gn "$owner_user")
        chown "$owner_user:$owner_group" "$preference_dir" "$CATHOOK_AUTO_UPDATE_FILE"
    fi
}

load_auto_update_preference() {
    local preference=""

    if [ -n "${CATHOOK_AUTO_UPDATE:-}" ]; then
        normalize_yes_no "$CATHOOK_AUTO_UPDATE"
        return
    fi

    if [ -f "$CATHOOK_AUTO_UPDATE_FILE" ]; then
        IFS= read -r preference < "$CATHOOK_AUTO_UPDATE_FILE" || true
        normalize_yes_no "$preference"
        return
    fi

    return 1
}

choose_auto_update_preference() {
    local answer=""
    local preference=""

    if preference=$(load_auto_update_preference); then
        [ "$preference" = "yes" ]
        return
    fi

    if [ ! -t 0 ]; then
        echo "Auto update preference is not set and stdin is not interactive; skipping update check."
        return 1
    fi

    while true; do
        read -r -p "Enable auto update before injecting? [y/n] " answer
        if preference=$(normalize_yes_no "$answer"); then
            save_auto_update_preference "$preference"
            break
        fi

        echo "Please answer yes or no."
    done

    if [ "$preference" = "yes" ]; then
        echo "Auto update enabled."
        return 0
    fi

    echo "Auto update disabled. Remove $CATHOOK_AUTO_UPDATE_FILE to choose again."
    return 1
}

run_repo_git() {
    git -C "$SCRIPT_DIR" -c "safe.directory=$SCRIPT_DIR" "$@"
}

discard_local_tracked_changes() {
    if [ -z "$(run_repo_git status --porcelain --untracked-files=no)" ]; then
        return
    fi

    echo "Discarding local tracked changes before updating..."
    run_repo_git reset --hard
}

rebuild_after_update() {
    local build_arg="--default"

    if [ "$CATHOOK_BINARY" = "libcathooktextmode.so" ]; then
        build_arg="--textmode"
    fi

    echo "Rebuilding Cat with ./build.sh $build_arg..."
    CATHOOK_ROOT="$CATHOOK_ROOT" "$SCRIPT_DIR/build.sh" "$build_arg"
}

check_for_updates() {
    local upstream=""
    local remote=""
    local local_rev=""
    local upstream_rev=""

    if ! command -v git >/dev/null 2>&1; then
        echo "Auto update skipped: git is missing."
        return 0
    fi

    if ! run_repo_git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        echo "Auto update skipped: $SCRIPT_DIR is not a git checkout."
        return 0
    fi

    upstream=$(run_repo_git rev-parse --abbrev-ref --symbolic-full-name '@{u}' 2>/dev/null || true)
    if [ -z "$upstream" ]; then
        echo "Auto update skipped: no upstream branch is configured."
        return 0
    fi

    remote="${upstream%%/*}"
    echo "Checking for updates from $upstream..."
    if ! run_repo_git fetch --quiet "$remote"; then
        echo "Auto update check failed; continuing with the current build."
        return 0
    fi

    local_rev=$(run_repo_git rev-parse HEAD)
    upstream_rev=$(run_repo_git rev-parse "$upstream")

    if [ "$local_rev" = "$upstream_rev" ]; then
        echo "Cat is already up to date."
        return 0
    fi

    if run_repo_git merge-base --is-ancestor "$local_rev" "$upstream_rev"; then
        echo "Update found. Downloading latest changes..."
        discard_local_tracked_changes
        if ! run_repo_git pull --ff-only --quiet; then
            echo "Auto update failed: could not fast-forward from $upstream."
            return 1
        fi

        if ! rebuild_after_update; then
            echo "Auto update failed: rebuild failed."
            return 1
        fi

        echo "Auto update finished."
        return 0
    fi

    if run_repo_git merge-base --is-ancestor "$upstream_rev" "$local_rev"; then
        echo "Local checkout is ahead of $upstream; no update needed."
        return 0
    fi

    echo "Local checkout has diverged from $upstream; resetting to upstream."
    if ! run_repo_git reset --hard "$upstream"; then
        echo "Auto update failed: could not reset to $upstream."
        return 1
    fi

    if ! rebuild_after_update; then
        echo "Auto update failed: rebuild failed."
        return 1
    fi

    echo "Auto update finished."
    return 0
}

maybe_auto_update() {
    if is_dev_mode; then
        echo "Dev mode enabled; skipping auto update check."
        return 0
    fi

    if ! choose_auto_update_preference; then
        return 0
    fi

    check_for_updates
}

setup_cathook_root() {
    local owner_user="${SUDO_USER:-}"
    local owner_group=""
    local log_user="${SUDO_USER:-$USER}"

    mkdir -p "$CATHOOK_BIN_DIR" "$CATHOOK_LOG_DIR" "$CATHOOK_ASSET_DIR"

    rm -f \
        "$CATHOOK_LOG_DIR/cathook.log" \
        "$CATHOOK_LOG_DIR"/exception*.log \
        "$CATHOOK_LOG_DIR"/crash*.log \
        "$CATHOOK_LOG_DIR"/gdb-crash*.log \
        "$CATHOOK_ROOT"/cathook.log \
        "$CATHOOK_ROOT"/exception*.log \
        "$CATHOOK_ROOT"/crash*.log \
        /tmp/cathook-"$log_user"-*-segfault.log

    if [ -d "$CATHOOK_SOURCE_ASSET_DIR" ]; then
        cp -a "$CATHOOK_SOURCE_ASSET_DIR"/. "$CATHOOK_ASSET_DIR"/
    else
        echo "Warning: missing local assets directory at $CATHOOK_SOURCE_ASSET_DIR"
    fi

    if [ -n "$owner_user" ]; then
        owner_group=$(id -gn "$owner_user")
        chown -R "$owner_user:$owner_group" "$CATHOOK_ROOT"
    fi

    chmod 0755 "$CATHOOK_ROOT"
    chmod 0775 "$CATHOOK_LOG_DIR"
    touch "$CATHOOK_LOG_DIR/cathook.log"

    if [ -n "$owner_user" ]; then
        chown "$owner_user:$owner_group" "$CATHOOK_LOG_DIR/cathook.log"
    fi

    chmod 0664 "$CATHOOK_LOG_DIR/cathook.log"
}

wait_for_game_process() {
    echo "Waiting for tf_linux64..."

    while [ -z "$PROCID" ]; do
        PROCID=$(pgrep tf_linux64 | head -n 1 || true)
        if [ -n "$PROCID" ]; then
            break
        fi

        sleep 1
    done

    echo "Found tf_linux64 at PID $PROCID"
    GAME_BINARY_PATH=$(readlink -f "/proc/$PROCID/exe" 2>/dev/null || true)
}

run_gdb_crash_report() {
    local pid="$1"
    local binary_path="$2"
    local timestamp=""
    local log_path=""
    local core_path=""
    local has_core=0

    timestamp=$(date +%Y%m%d-%H%M%S)
    log_path="$CATHOOK_LOG_DIR/gdb-crash-$pid-$timestamp.log"
    core_path="/tmp/cathook-$pid-$timestamp.core"

    {
        echo "========== $(date --iso-8601=seconds) crash pid=$pid =========="
        echo "binary=$binary_path"
        echo
        echo "[coredumpctl info]"
        coredumpctl info "$pid" 2>&1 || true
        echo
        echo "[coredumpctl dump]"

        if coredumpctl dump "$pid" --output="$core_path" >/dev/null 2>&1 && [ -s "$core_path" ]; then
            has_core=1
            echo "core=$core_path"
            echo
            echo "[gdb]"
            if [ -n "$binary_path" ] && [ -e "$binary_path" ]; then
                gdb -n -q --batch "$binary_path" "$core_path" \
                    -ex "set pagination off" \
                    -ex "info threads" \
                    -ex "info sharedlibrary" \
                    -ex "thread apply all bt full" 2>&1 || true
            else
                gdb -n -q --batch -c "$core_path" \
                    -ex "set pagination off" \
                    -ex "info threads" \
                    -ex "info sharedlibrary" \
                    -ex "thread apply all bt full" 2>&1 || true
            fi

            if is_enabled "$CATHOOK_GDB_KEEP_CORE"; then
                echo
                echo "kept core=$core_path"
            else
                rm -f "$core_path"
            fi
        else
            echo "no core dump available for pid $pid"
            rm -f "$core_path"
        fi
    } >>"$log_path" 2>&1

    echo "Wrote gdb crash report to $log_path"
    if [ "$has_core" = "1" ]; then
        send_discord_report "$log_path" "cathook gdb crash report"
    else
        echo "Discord gdb crash report skipped: no core dump was available"
    fi
}

start_gdb_crash_watcher() {
    if ! is_enabled "$CATHOOK_GDB_CRASH_REPORTS"; then
        return
    fi

    if ! command -v coredumpctl >/dev/null 2>&1; then
        echo "gdb crash reports disabled: coredumpctl is missing"
        return
    fi

    if ! command -v gdb >/dev/null 2>&1; then
        echo "gdb crash reports disabled: gdb is missing"
        return
    fi

    local pid="$PROCID"
    local binary_path="$GAME_BINARY_PATH"
    local tail_pid="$TAIL_PID"

    (
        while [ -d "/proc/$pid" ]; do
            sleep 1
        done

        sleep 1
        run_gdb_crash_report "$pid" "$binary_path"
        send_exception_reports
        if [ -n "$tail_pid" ]; then
            kill "$tail_pid" >/dev/null 2>&1 || true
        fi
    ) &

    CRASH_WATCHER_PID=$!
    echo "gdb crash watcher started for PID $pid"
}

stop_gdb_crash_watcher() {
    if [ -n "$CRASH_WATCHER_PID" ]; then
        kill "$CRASH_WATCHER_PID" >/dev/null 2>&1 || true
        wait "$CRASH_WATCHER_PID" >/dev/null 2>&1 || true
        CRASH_WATCHER_PID=""
    fi
}

stop_log_tail() {
    if [ -n "$TAIL_PID" ]; then
        kill "$TAIL_PID" >/dev/null 2>&1 || true
        wait "$TAIL_PID" >/dev/null 2>&1 || true
        TAIL_PID=""
    fi
}

cleanup_temp_runtime() {
    if [ -n "$TMP_RUNTIME_DIR" ] && [ -d "$TMP_RUNTIME_DIR" ]; then
        rm -rf "$TMP_RUNTIME_DIR"
    fi
    TMP_RUNTIME_DIR=""
}

copy_bundled_runtime_dependencies() {
    local source_dir="$1"
    local target_dir="$2"
    local dependency_path=""
    local target_path=""

    if [ ! -d "$source_dir" ]; then
        return
    fi

    for dependency_path in "$source_dir"/libGLEW.so.*; do
        [ -f "$dependency_path" ] || continue
        target_path="$target_dir/$(basename -- "$dependency_path")"
        if [ "$dependency_path" = "$target_path" ]; then
            continue
        fi

        cp "$dependency_path" "$target_path"
        chmod 0755 "$target_path"
    done
}

find_shared_library() {
    local library_name="$1"
    local candidate=""

    if command -v ldconfig >/dev/null 2>&1; then
        candidate="$(ldconfig -p 2>/dev/null | awk -v library_name="$library_name" '$1 == library_name { print $NF; exit }')"
        if [ -n "$candidate" ] && [ -f "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    fi

    for candidate in \
        "/usr/lib/$library_name" \
        "/usr/lib64/$library_name" \
        "/usr/lib/x86_64-linux-gnu/$library_name" \
        "/usr/local/lib/$library_name" \
        "/run/host/usr/lib/$library_name" \
        "/run/host/usr/lib64/$library_name"; do
        if [ -f "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    return 1
}

install_glew_fallback_for_binary() {
    local binary_path="$1"
    local required_library=""
    local source_path=""

    if [ ! -f "$binary_path" ]; then
        return
    fi

    if ! command -v readelf >/dev/null 2>&1; then
        echo "Warning: readelf is missing; cannot prepare libGLEW fallback for $binary_path." >&2
        return
    fi

    required_library="$(readelf -d "$binary_path" 2>/dev/null | awk -F'[][]' '/NEEDED/ && $2 ~ /^libGLEW\.so\./ { print $2; exit }')"
    if [ -z "$required_library" ] || [ -f "$CATHOOK_BIN_DIR/$required_library" ]; then
        return
    fi

    if ! source_path="$(find_shared_library "$required_library")"; then
        echo "Warning: $binary_path needs $required_library, but it was not found on this system." >&2
        echo "Run ./install-deps and sudo ./build.sh, or put $required_library in $CATHOOK_BIN_DIR." >&2
        return
    fi

    install -d -m 0755 "$CATHOOK_BIN_DIR"
    install -m 0755 "$source_path" "$CATHOOK_BIN_DIR/$required_library"
    echo "Installed missing fallback $required_library to $CATHOOK_BIN_DIR"
}

unload() {
    echo -e "\nUnloading library with handle $LIB_HANDLE"
    stop_gdb_crash_watcher
    stop_log_tail
    cleanup_temp_runtime

    sudo gdb -n --batch -ex "attach $PROCID" \
         -ex "call cathook_detach()" \
         -ex "detach" >/dev/null 2>&1 || true

    local detached=0
    for _ in $(seq 1 40); do
        if sudo gdb -n --batch -ex "attach $PROCID" \
             -ex "call cathook_is_detached()" \
             -ex "detach" 2>/dev/null | grep -Eq '\$1 = (true|1)'; then
            detached=1
            break
        fi

        sleep 0.25
    done

    if [[ "$detached" != "1" ]]; then
        echo "Timed out waiting for cathook to detach cleanly"
        exit 1
    fi

    sleep 0.25

    RC=$(sudo gdb -n --batch -ex "attach $PROCID" \
             -ex "call ((int (*) (void *)) dlclose)((void *) $LIB_HANDLE)" \
             -ex "call ((char * (*) (void)) dlerror)()" \
             -ex "detach" 2> /dev/null | grep -oP '\$2 = 0x\K[0-9a-f]+')

    if [[ "$RC" == "0" ]]; then
        echo "Library unloaded successfully"
    else
        echo "Failed to unload library"
    fi

    exit 0
}

trap unload SIGINT
trap cleanup_temp_runtime EXIT

if ! maybe_auto_update; then
    exit 1
fi

require_gdb_injection_enabled
setup_cathook_root
wait_for_game_process

if [ ! -f "$LIB_PATH" ]; then
    echo "Missing $CATHOOK_BINARY. Run ./build.sh first."
    exit 1
fi

install_glew_fallback_for_binary "$LIB_PATH"

echo "Using $LIB_PATH"
if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$LIB_PATH"
fi

TMP_RUNTIME_DIR=$(mktemp -d /tmp/.glXXXXXX)
TMP_LIB="$TMP_RUNTIME_DIR/$CATHOOK_BINARY"
chmod 0755 "$TMP_RUNTIME_DIR"
cp "$LIB_PATH" "$TMP_LIB"
chmod 0755 "$TMP_LIB"
copy_bundled_runtime_dependencies "$(dirname -- "$LIB_PATH")" "$TMP_RUNTIME_DIR"
if [ "$(dirname -- "$LIB_PATH")" != "$CATHOOK_BIN_DIR" ]; then
    copy_bundled_runtime_dependencies "$CATHOOK_BIN_DIR" "$TMP_RUNTIME_DIR"
fi

LOAD_OUTPUT=$(sudo gdb -n --batch -ex "attach $PROCID" \
                   -ex "call ((int (*) (const char *, const char *, int)) setenv)(\"CATHOOK_ATTACH_DELAY_SECONDS\", \"$CATHOOK_ATTACH_DELAY_SECONDS\", 1)" \
                   -ex "call ((int (*) (const char *, const char *, int)) setenv)(\"CATHOOK_DISABLE_SDL_HOOKS\", \"${CATHOOK_DISABLE_SDL_HOOKS:-}\", 1)" \
                   -ex "call ((void * (*) (const char*, int)) dlopen)(\"$TMP_LIB\", 1)" \
                   -ex "detach" 2>&1)
LIB_HANDLE=$(printf "%s\n" "$LOAD_OUTPUT" | grep -oP '\$[0-9]+ = \(void \*\) \K0x[0-9a-f]+')

cleanup_temp_runtime

if [ -z "$LIB_HANDLE" ]; then
    echo "Failed to load library"
    echo "$LOAD_OUTPUT"
    exit 1
fi

if [[ "$LIB_HANDLE" = "0x0" ]]; then
    echo "Failed to load library at $LIB_HANDLE"
    ERR=$(sudo gdb -n --batch -ex "attach $PROCID" \
              -ex "call ((char * (*) (void)) dlerror)()" \
              -ex "detach" 2>&1 | grep '\$1')
    echo "Result from dlerror: $ERR"
    exit 1
fi

ATTACH_OUTPUT=$(sudo gdb -n --batch -ex "attach $PROCID" \
                     -ex "call ((int (*)()) dlsym((void *) $LIB_HANDLE, \"cathook_attach\"))()" \
                     -ex "detach" 2>&1)
ATTACH_RESULT=$(printf "%s\n" "$ATTACH_OUTPUT" | grep -oP '\$[0-9]+ = (true|false|0|1)' | tail -n 1 | awk '{print $3}')

if [[ "$ATTACH_RESULT" != "true" && "$ATTACH_RESULT" != "1" ]]; then
    echo "Attach export reported failure"
    echo "$ATTACH_OUTPUT"
    exit 1
fi

echo "$CATHOOK_BINARY loaded successfully at $LIB_HANDLE. Use Ctrl+C to unload."
tail -f "$CATHOOK_LOG_DIR/cathook.log" &
TAIL_PID=$!
start_gdb_crash_watcher

wait "$TAIL_PID" >/dev/null 2>&1 || true
