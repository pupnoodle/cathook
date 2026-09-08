#!/usr/bin/env bash

PROCID=""
GAME_BINARY_PATH=""
CRASH_WATCHER_PID=""
TAIL_PID=""
TMP_RUNTIME_DIR=""
TMP_RUNTIME_HOST_DIR=""
TMP_PROC_SELF_LIB=""
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

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
CATHOOK_USE_GDB=${CATHOOK_USE_GDB:-1}
CATHOOK_GDB_CRASH_REPORTS=${CATHOOK_GDB_CRASH_REPORTS:-0}
CATHOOK_GDB_KEEP_CORE=${CATHOOK_GDB_KEEP_CORE:-0}
CATHOOK_TARGET_PID=${CATHOOK_TARGET_PID:-}
CATHOOK_INCLUDE_BOTS=${CATHOOK_INCLUDE_BOTS:-0}
CATHOOK_DETACH_TIMEOUT_SECONDS=${CATHOOK_DETACH_TIMEOUT_SECONDS:-15}

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
            echo "Usage: sudo ./attach.sh [PID] [--gdb|--no-gdb|--gdb-crash-reports|--no-gdb-crash-reports|--dev|--no-update]"
            echo "Mode: CATHOOK_MODE=default|textmode, CATHOOK_TEXTMODE=1, TEXTMODE=1, or saved first-run choice."
            echo "GDB injection is enabled by default. Use ./preload for a no-gdb launch or --no-gdb to block attach injection."
            echo "By default, bot TF2 processes with CAT_BOT_ID or CAT_BOT_NAME are skipped. Set CATHOOK_INCLUDE_BOTS=1 to include them."
            exit 0
            ;;
        ''|*[!0-9]*)
            echo "Unknown attach option: $1" >&2
            echo "Usage: sudo ./attach.sh [PID] [--gdb|--no-gdb|--gdb-crash-reports|--no-gdb-crash-reports|--dev|--no-update]" >&2
            exit 1
            ;;
        *)
            if [ -n "$CATHOOK_TARGET_PID" ]; then
                echo "Only one target PID can be specified." >&2
                exit 1
            fi
            CATHOOK_TARGET_PID="$1"
            ;;
    esac
    shift
done

if [[ ! "$CATHOOK_ATTACH_DELAY_SECONDS" =~ ^[0-9]+$ ]]; then
    CATHOOK_ATTACH_DELAY_SECONDS=0
fi

if [[ ! "$CATHOOK_DETACH_TIMEOUT_SECONDS" =~ ^[0-9]+$ ]] || [ "$CATHOOK_DETACH_TIMEOUT_SECONDS" -lt 1 ]; then
    CATHOOK_DETACH_TIMEOUT_SECONDS=15
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

run_gdb_batch() {
    local timeout_seconds="$1"
    shift

    if command -v timeout >/dev/null 2>&1; then
        timeout --kill-after=2s "${timeout_seconds}s" sudo gdb -n --batch "$@"
        return $?
    fi

    sudo gdb -n --batch "$@"
}

is_dev_mode() {
    is_enabled "${CATHOOK_DEV_MODE:-${CAT_DEV_MODE:-0}}"
}

proc_environ_has_key() {
    local pid="$1"
    local key="$2"

    [ -r "/proc/$pid/environ" ] || return 1
    tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null | grep -q "^$key="
}

is_bot_game_process() {
    local pid="$1"

    proc_environ_has_key "$pid" "CAT_BOT_ID" || proc_environ_has_key "$pid" "CAT_BOT_NAME"
}

is_tf2_process() {
    local pid="$1"
    local comm=""
    local exe=""
    local cmdline=""
    local first_arg=""

    [ -d "/proc/$pid" ] || return 1

    comm="$(cat "/proc/$pid/comm" 2>/dev/null || true)"
    if [ "$comm" = "tf_linux64" ]; then
        return 0
    fi

    exe="$(readlink -f "/proc/$pid/exe" 2>/dev/null || true)"
    exe="${exe##*/}"
    if [ "$exe" = "tf_linux64" ]; then
        return 0
    fi

    cmdline="$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null || true)"
    first_arg="${cmdline%% *}"
    first_arg="${first_arg##*/}"
    [ "$first_arg" = "tf_linux64" ]
}

require_gdb_injection_enabled() {
    if is_enabled "$CATHOOK_USE_GDB"; then
        return
    fi

    echo "gdb injection is disabled by CATHOOK_USE_GDB=0 or --no-gdb."
    echo "Use ./preload to launch TF2 without gdb, or run sudo ./attach.sh to attach with gdb."
    exit 1
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

restore_repo_git_permissions() {
    if [ "$(id -u)" -ne 0 ] || [ -z "${SUDO_UID:-}" ] || [ -z "${SUDO_GID:-}" ]; then
        return
    fi

    if [ ! -d "$SCRIPT_DIR/.git" ]; then
        return
    fi

    if find "$SCRIPT_DIR/.git" \( ! -user "$SUDO_UID" -o ! -group "$SUDO_GID" \) -print -quit 2>/dev/null | grep -q .; then
        echo "Fixing .git ownership for sudo user before auto update..."
        chown -R "$SUDO_UID:$SUDO_GID" "$SCRIPT_DIR/.git"
    fi
}

run_repo_git() {
    if [ "$(id -u)" -eq 0 ] && [ -n "${SUDO_UID:-}" ] && command -v sudo >/dev/null 2>&1; then
        sudo -H -u "#$SUDO_UID" git -C "$SCRIPT_DIR" -c "safe.directory=$SCRIPT_DIR" "$@"
    else
        git -C "$SCRIPT_DIR" -c "safe.directory=$SCRIPT_DIR" "$@"
    fi
}

allow_discard_enabled() {
    case "${CATHOOK_ALLOW_DISCARD:-${CAT_ALLOW_DISCARD:-0}}" in
        1|true|TRUE|yes|YES|on|ON|force|FORCE)
            return 0
            ;;
    esac

    return 1
}

discard_local_tracked_changes() {
    if [ -z "$(run_repo_git status --porcelain --untracked-files=no)" ]; then
        return 0
    fi

    if allow_discard_enabled; then
        echo "Discarding local tracked changes before updating (CATHOOK_ALLOW_DISCARD is set)..."
        run_repo_git reset --hard
        return
    fi

    echo "Local tracked changes found; refusing to discard them. Skipping auto update." >&2
    echo "Commit, stash, or set CATHOOK_ALLOW_DISCARD=1 to allow discarding." >&2
    return 1
}

rebuild_after_update() {
    local build_arg="--default"
    local build_script="$SCRIPT_DIR/build.sh"

    if [ "$CATHOOK_BINARY" = "libcathooktextmode.so" ]; then
        build_arg="--textmode"
    fi

    echo "Rebuilding Cat with ./build.sh $build_arg..."
    CATHOOK_ROOT="$CATHOOK_ROOT" bash "$build_script" "$build_arg"
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
        if ! discard_local_tracked_changes; then
            echo "Auto update skipped to preserve local changes."
            return 0
        fi
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

    echo "Local checkout has diverged from $upstream; refusing to reset."
    echo "Rebase or merge manually, or set CATHOOK_ALLOW_DISCARD=1 to reset to upstream."
    if ! allow_discard_enabled; then
        echo "Auto update skipped to preserve local commits."
        return 0
    fi
    echo "Resetting to $upstream (CATHOOK_ALLOW_DISCARD is set)."
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

    mkdir -p "$CATHOOK_LOG_DIR" "$CATHOOK_ASSET_DIR"

    rm -f \
        "$CATHOOK_LOG_DIR/cathook.log" \
        "$CATHOOK_LOG_DIR"/crash*.log \
        "$CATHOOK_LOG_DIR"/gdb-crash*.log \
        "$CATHOOK_ROOT"/cathook.log \
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

    if [ -n "$CATHOOK_TARGET_PID" ]; then
        if ! is_tf2_process "$CATHOOK_TARGET_PID"; then
            echo "PID $CATHOOK_TARGET_PID is not a running tf_linux64 process." >&2
            exit 1
        fi

        if ! is_enabled "$CATHOOK_INCLUDE_BOTS" && is_bot_game_process "$CATHOOK_TARGET_PID"; then
            echo "PID $CATHOOK_TARGET_PID is a bot tf_linux64 process; refusing to attach by default." >&2
            echo "Set CATHOOK_INCLUDE_BOTS=1 if you really want to inject into bot processes." >&2
            exit 1
        fi

        PROCID="$CATHOOK_TARGET_PID"
        echo "Using requested tf_linux64 PID $PROCID"
        GAME_BINARY_PATH=$(readlink -f "/proc/$PROCID/exe" 2>/dev/null || true)
        return
    fi

    local pids=()
    local pid=""
    local skipped_bot_pids=()

    while [ -z "$PROCID" ]; do
        mapfile -t pids < <(pgrep -x tf_linux64 2>/dev/null || true)

        skipped_bot_pids=()
        for pid in "${pids[@]}"; do
            if ! is_tf2_process "$pid"; then
                continue
            fi

            if ! is_enabled "$CATHOOK_INCLUDE_BOTS" && is_bot_game_process "$pid"; then
                skipped_bot_pids+=("$pid")
                continue
            fi

            PROCID="$pid"
            break
        done

        if [ -n "$PROCID" ]; then
            break
        fi

        sleep 1
    done

    if [ "${#skipped_bot_pids[@]}" -gt 0 ]; then
        echo "Ignored bot tf_linux64 PIDs: ${skipped_bot_pids[*]}"
    fi
    echo "Found tf_linux64 at PID $PROCID"
    GAME_BINARY_PATH=$(readlink -f "/proc/$PROCID/exe" 2>/dev/null || true)
}

target_has_cathook_mapping() {
    [ -r "/proc/$PROCID/maps" ] || return 1
    grep -Eq '/cathook-runtime-[^/]+/libcathook[^/]*\.so|/libcathook(textmode)?\.so' "/proc/$PROCID/maps"
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
    if [ -n "$TMP_RUNTIME_HOST_DIR" ] && [ -d "$TMP_RUNTIME_HOST_DIR" ]; then
        rm -rf "$TMP_RUNTIME_HOST_DIR"
    elif [ -n "$TMP_RUNTIME_DIR" ] && [ -d "$TMP_RUNTIME_DIR" ]; then
        rm -rf "$TMP_RUNTIME_DIR"
    fi
    TMP_RUNTIME_DIR=""
    TMP_RUNTIME_HOST_DIR=""
}

get_process_uid() {
    awk '/^Uid:/ { print $2; exit }' "/proc/$PROCID/status" 2>/dev/null || true
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

report_missing_shared_dependencies() {
    local binary_path="$1"
    local label="$2"
    local binary_dir=""
    local ldd_output=""
    local missing_libraries=""

    if [ ! -f "$binary_path" ]; then
        return 0
    fi

    if ! command -v ldd >/dev/null 2>&1; then
        echo "Warning: ldd is missing; cannot check runtime dependencies for $label." >&2
        return 0
    fi

    binary_dir="$(dirname -- "$binary_path")"
    ldd_output="$(LD_LIBRARY_PATH="$binary_dir:$CATHOOK_BIN_DIR:${LD_LIBRARY_PATH:-}" ldd "$binary_path" 2>&1 || true)"
    missing_libraries="$(printf "%s\n" "$ldd_output" | awk '/=>[[:space:]]+not found/ { print $1 }' | sort -u)"

    if [ -z "$missing_libraries" ]; then
        return 0
    fi

    echo "Missing runtime dependencies for $label ($binary_path):" >&2
    printf "%s\n" "$missing_libraries" | sed 's/^/  - /' >&2
    echo "Install the missing libraries with ./install-deps, then run sudo ./build.sh and sudo ./attach.sh again." >&2
    echo "Full ldd output:" >&2
    printf "%s\n" "$ldd_output" >&2
    return 1
}

report_dlopen_failure_context() {
    local dlerror_line="$1"
    local missing_library=""

    report_missing_shared_dependencies "$LIB_PATH" "$CATHOOK_BINARY" || true

    if [ -n "$TMP_RUNTIME_HOST_DIR" ]; then
        report_missing_shared_dependencies "$TMP_RUNTIME_HOST_DIR/$CATHOOK_BINARY" "staged $CATHOOK_BINARY" || true
    fi

    missing_library="$(printf "%s\n" "$dlerror_line" | sed -n 's/.*"\([^"]*\): cannot open shared object file: No such file or directory".*/\1/p' | tail -n 1)"
    if [ -n "$missing_library" ]; then
        case "$missing_library" in
            /*)
                if [ "$missing_library" = "$TMP_LIB" ] || [ "$missing_library" = "$TMP_PROC_SELF_LIB" ]; then
                    echo "The target process could not see the staged cathook library path." >&2
                    echo "This usually means the game is in a different mount namespace or runtime sandbox; the injector already tried alternate target-visible directories." >&2
                else
                    echo "dlopen could not open an absolute dependency path: $missing_library" >&2
                fi
                ;;
            *)
                echo "dlopen is missing dependency: $missing_library" >&2
                echo "Install the package that provides $missing_library, or rerun ./install-deps and sudo ./build.sh." >&2
                ;;
        esac
    fi
}

target_can_read_path() {
    local target_path="$1"
    local access_output=""

    access_output=$(sudo gdb -n --batch -ex "attach $PROCID" \
        -ex "call ((int (*) (const char *, int)) access)(\"$target_path\", 4)" \
        -ex "detach" 2>&1)

    if printf "%s\n" "$access_output" | grep -Eq '\$[0-9]+ = 0'; then
        return 0
    fi

    if ! printf "%s\n" "$access_output" | grep -Eq '\$[0-9]+ = -1'; then
        echo "Could not verify target read access for $target_path; trying dlopen anyway:" >&2
        printf "%s\n" "$access_output" >&2
        return 0
    fi

    echo "Target process cannot read $target_path:" >&2
    printf "%s\n" "$access_output" >&2
    return 1
}

stage_temp_runtime_at() {
    local target_parent_dir="$1"
    local host_parent_dir="/proc/$PROCID/root$target_parent_dir"
    local tmp_base=""

    if [ ! -d "$host_parent_dir" ]; then
        if [ "$target_parent_dir" = "$CATHOOK_ROOT/run" ] && [ -d "/proc/$PROCID/root$CATHOOK_ROOT" ]; then
            mkdir -p "$host_parent_dir" || return 1
            chmod 0755 "$host_parent_dir" || return 1
        fi
    fi

    if [ ! -d "$host_parent_dir" ]; then
        return 1
    fi

    TMP_RUNTIME_HOST_DIR=$(mktemp -d "$host_parent_dir/cathook-runtime-XXXXXX") || return 1
    tmp_base="$(basename -- "$TMP_RUNTIME_HOST_DIR")"
    TMP_RUNTIME_DIR="$target_parent_dir/$tmp_base"
    TMP_LIB="$TMP_RUNTIME_DIR/$CATHOOK_BINARY"

    if ! chmod 0755 "$TMP_RUNTIME_HOST_DIR"; then
        cleanup_temp_runtime
        return 1
    fi

    if ! install -m 0755 "$LIB_PATH" "$TMP_RUNTIME_HOST_DIR/$CATHOOK_BINARY"; then
        cleanup_temp_runtime
        return 1
    fi

    copy_bundled_runtime_dependencies "$(dirname -- "$LIB_PATH")" "$TMP_RUNTIME_HOST_DIR"
    if [ "$(dirname -- "$LIB_PATH")" != "$CATHOOK_BIN_DIR" ]; then
        copy_bundled_runtime_dependencies "$CATHOOK_BIN_DIR" "$TMP_RUNTIME_HOST_DIR"
    fi

    if [ ! -r "$TMP_RUNTIME_HOST_DIR/$CATHOOK_BINARY" ]; then
        echo "Failed to stage $CATHOOK_BINARY in target-visible runtime dir $TMP_RUNTIME_HOST_DIR." >&2
        cleanup_temp_runtime
        return 1
    fi

    if ! target_can_read_path "$TMP_LIB"; then
        cleanup_temp_runtime
        return 1
    fi

    echo "TMP_RUNTIME_DIR=$TMP_RUNTIME_DIR"
    echo "TMP_LIB=$TMP_LIB"
    return 0
}

parse_lib_handle() {
    printf "%s\n" "$1" | grep -oP '\$[0-9]+ = \(void \*\) \K0x[0-9a-f]+'
}

dlopen_missing_current_runtime() {
    local output="$1"

    printf "%s\n" "$output" | grep -Fq "$TMP_LIB: cannot open shared object file: No such file or directory" && return 0

    if [ -n "$TMP_PROC_SELF_LIB" ]; then
        printf "%s\n" "$output" | grep -Fq "$TMP_PROC_SELF_LIB: cannot open shared object file: No such file or directory" && return 0
    fi

    return 1
}

try_load_staged_runtime() {
    LOAD_OUTPUT=$(gdb_dlopen_path "$TMP_LIB")
    LIB_HANDLE=$(parse_lib_handle "$LOAD_OUTPUT")

    if [[ "$LIB_HANDLE" = "0x0" ]] && printf "%s\n" "$LOAD_OUTPUT" | grep -q "No such file or directory"; then
        TMP_PROC_SELF_LIB="/proc/self/root$TMP_LIB"
        echo "dlopen could not see $TMP_LIB; retrying as $TMP_PROC_SELF_LIB"
        LOAD_OUTPUT=$(gdb_dlopen_path "$TMP_PROC_SELF_LIB")
        LIB_HANDLE=$(parse_lib_handle "$LOAD_OUTPUT")
    fi

    [[ -n "$LIB_HANDLE" && "$LIB_HANDLE" != "0x0" ]]
}

stage_and_load_runtime() {
    local target_uid=""
    local target_dir=""
    local target_dirs=()

    target_uid="$(get_process_uid)"
    target_dirs=("$CATHOOK_ROOT/run" "/tmp" "/var/tmp")
    if [ -n "$target_uid" ]; then
        target_dirs+=("/run/user/$target_uid")
    fi
    target_dirs+=("/dev/shm")

    for target_dir in "${target_dirs[@]}"; do
        TMP_PROC_SELF_LIB=""
        echo "Trying target runtime dir $target_dir"
        if ! stage_temp_runtime_at "$target_dir"; then
            continue
        fi

        if try_load_staged_runtime; then
            return 0
        fi

        if dlopen_missing_current_runtime "$LOAD_OUTPUT"; then
            echo "dlopen could not see staged library from $target_dir; trying another runtime dir." >&2
            cleanup_temp_runtime
            continue
        fi

        return 1
    done

    echo "Failed to stage and load $CATHOOK_BINARY from a target-visible runtime directory." >&2
    echo "Tried: ${target_dirs[*]}" >&2
    return 1
}

gdb_dlopen_path() {
    local library_path="$1"

    sudo gdb -n --batch -ex "attach $PROCID" \
        -ex "call ((int (*) (const char *, const char *, int)) setenv)(\"CATHOOK_ATTACH_DELAY_SECONDS\", \"$CATHOOK_ATTACH_DELAY_SECONDS\", 1)" \
        -ex "call ((int (*) (const char *, const char *, int)) setenv)(\"CATHOOK_DISABLE_SDL_HOOKS\", \"${CATHOOK_DISABLE_SDL_HOOKS:-}\", 1)" \
        -ex "call ((void * (*) (const char*, int)) dlopen)(\"$library_path\", 1)" \
        -ex "call ((char * (*) (void)) dlerror)()" \
        -ex "detach" 2>&1
}

unload() {
    echo -e "\nUnloading library with handle $LIB_HANDLE"
    stop_gdb_crash_watcher
    stop_log_tail
    local unload_output=""
    local close_output=""
    local detach_result=""
    local detached_result=""
    local close_result=""

    # cathook_detach performs cleanup synchronously in this debugger call. It
    # restores hooks, drains guarded callbacks, and returns only when the
    # module is safe for the following dlclose. Do not issue dlclose unless
    # both native lifecycle checks succeeded; a failed cleanup must leave the
    # mapping in place so it can be diagnosed or retried safely.
    unload_output=$(run_gdb_batch "$CATHOOK_DETACH_TIMEOUT_SECONDS" \
        -ex "attach $PROCID" \
        -ex "call ((int (*)()) dlsym((void *) $LIB_HANDLE, \"cathook_detach\"))()" \
        -ex "call ((int (*)()) dlsym((void *) $LIB_HANDLE, \"cathook_is_detached\"))()" \
        -ex "detach" 2>&1)
    echo "$unload_output"

    detach_result=$(printf '%s\n' "$unload_output" | grep -oP '\$[0-9]+ = (true|false|0|1)' | head -n 1 | awk '{print $3}')
    detached_result=$(printf '%s\n' "$unload_output" | grep -oP '\$[0-9]+ = (true|false|0|1)' | head -n 2 | tail -n 1 | awk '{print $3}')
    if [[ "$detach_result" != "true" && "$detach_result" != "1" ]] ||
       [[ "$detached_result" != "true" && "$detached_result" != "1" ]]; then
        echo "Failed to detach safely; leaving library loaded" >&2
        cleanup_temp_runtime
        exit 1
    fi

    close_output=$(run_gdb_batch 5 -ex "attach $PROCID" \
        -ex "call ((int (*) (void *)) dlclose)((void *) $LIB_HANDLE)" \
        -ex "call ((char * (*) (void)) dlerror)()" \
        -ex "detach" 2>&1)
    echo "$close_output"
    close_result=$(printf '%s\n' "$close_output" | grep -oP '\$[0-9]+ = -?[0-9]+' | head -n 1 | awk '{print $3}')

    if [[ "$close_result" == "0" ]]; then
        echo "Library unloaded successfully"
        cleanup_temp_runtime
        exit 0
    else
        echo "Failed to detach and unload library safely" >&2
        cleanup_temp_runtime
        exit 1
    fi
}

trap unload SIGINT
trap cleanup_temp_runtime EXIT

if ! maybe_auto_update; then
    exit 1
fi

require_gdb_injection_enabled
wait_for_game_process

if [ ! -r "/proc/$PROCID/maps" ]; then
    echo "Refusing to inject: cannot inspect memory mappings for TF2 PID $PROCID." >&2
    exit 1
fi

if target_has_cathook_mapping; then
    echo "Refusing to inject: TF2 PID $PROCID already has a cathook library mapped." >&2
    echo "Detach and unload the existing cathook instance, or restart TF2 before injecting again." >&2
    exit 1
fi

setup_cathook_root

if [ ! -f "$LIB_PATH" ]; then
    echo "Missing $CATHOOK_BINARY. Run ./build.sh first."
    exit 1
fi

install_glew_fallback_for_binary "$LIB_PATH"
report_missing_shared_dependencies "$LIB_PATH" "$CATHOOK_BINARY" || exit 1

echo "Using $LIB_PATH"
if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$LIB_PATH"
fi

stage_and_load_runtime

if [ -z "$LIB_HANDLE" ]; then
    echo "Failed to load library"
    echo "$LOAD_OUTPUT"
    report_dlopen_failure_context "$LOAD_OUTPUT"
    cleanup_temp_runtime
    exit 1
fi

if [[ "$LIB_HANDLE" = "0x0" ]]; then
    echo "Failed to load library at $LIB_HANDLE"
    ERR=$(printf "%s\n" "$LOAD_OUTPUT" | grep -E '\$[0-9]+ = 0x[0-9a-f]+ .+' | tail -n 1)
    echo "Result from dlerror: $ERR"
    report_dlopen_failure_context "$ERR"
    cleanup_temp_runtime
    exit 1
fi

cleanup_temp_runtime

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
echo "Cathook log: $CATHOOK_LOG_DIR/cathook.log"
echo "Exception log: $CATHOOK_LOG_DIR/exception.log"
tail -f "$CATHOOK_LOG_DIR/cathook.log" &
TAIL_PID=$!
start_gdb_crash_watcher

wait "$TAIL_PID" >/dev/null 2>&1 || true
