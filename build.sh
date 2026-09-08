#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
asset_source_dir="$project_root/assets"
install_root="${CATHOOK_ROOT:-/opt/cathook}"
build_mode=""

source "$project_root/cathook_mode.sh"

usage() {
    cat <<'EOF'
Usage: sudo ./build.sh [default|textmode|both|--default|--textmode|--both|--no-install]

Without a mode argument or saved preference, asks which mode to build.

Install mode writes to /opt/cathook by default and MUST RUN AS SUDO.
Use --no-install for a local user build without sudo.
Use --dev or --no-update to skip repository update checks and never reset local changes.

Environment:
  CATHOOK_MODE=default|textmode|both
  CAT_BUILD_MODE=default|textmode|both
  CATHOOK_TEXTMODE=1
  TEXTMODE=1
  CATHOOK_MODE_FILE=~/.config/cathook/mode
  CATHOOK_ROOT=/opt/cathook
  CATHOOK_DEV_MODE=1
EOF
}

is_enabled() {
    case "${1:-}" in
        "" | 0 | false | FALSE | no | NO | off | OFF)
            return 1
            ;;
    esac

    return 0
}

is_dev_mode() {
    is_enabled "${CATHOOK_DEV_MODE:-${CAT_DEV_MODE:-0}}"
}

require_root_for_install() {
    if [ "$install_enabled" = "0" ]; then
        return
    fi

    if [ "$(id -u)" -ne 0 ]; then
        echo "MUST RUN AS SUDO: sudo ./build.sh ${selected_mode:-}" >&2
        echo "Use ./build.sh --no-install for a local build that does not write to $install_root." >&2
        exit 1
    fi
}

run_as_root() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    else
        echo "sudo is required to install to $install_root." >&2
        return 1
    fi
}

make_shell_path() {
    if [ -x /bin/sh ]; then
        printf '%s\n' /bin/sh
        return
    fi

    if command -v bash >/dev/null 2>&1; then
        command -v bash
        return
    fi

    echo "No usable shell found for make. Install dash or bash, then run ./build.sh again." >&2
    exit 1
}

run_make() {
    make SHELL="$(make_shell_path)" "$@"
}

fix_install_permissions() {
    if [ ! -d "$install_root" ]; then
        return
    fi

    run_as_root chmod -R u=rwX,go=rX "$install_root"
    if [ -n "${SUDO_UID:-}" ] && [ -n "${SUDO_GID:-}" ]; then
        for path in "$install_root/configs" "$install_root/logs" "$install_root/ipc"; do
            if [ -e "$path" ]; then
                run_as_root chown -R "$SUDO_UID:$SUDO_GID" "$path" 2>/dev/null || true
            fi
        done
    fi
    if [ -d "$install_root/bin" ]; then
        run_as_root find "$install_root/bin" -type f -exec chmod 0755 {} +
    fi
    if [ -d "$install_root/ipc/bin" ]; then
        run_as_root find "$install_root/ipc/bin" -type f -exec chmod 0755 {} +
    fi
}

fix_runtime_permissions_once() {
    local permissions_script="$project_root/botpanel/fix_permissions"

    if [ "$(uname -s)" != "Linux" ]; then
        return
    fi

    if [ -x "$permissions_script" ]; then
        "$permissions_script" --once
    fi
}

restore_workspace_permissions() {
    if [ "$(id -u)" -ne 0 ] || [ -z "${SUDO_UID:-}" ] || [ -z "${SUDO_GID:-}" ]; then
        return
    fi

    for path in "$project_root/bin" "$project_root/obj" "$project_root/libs/funchook" "$project_root/botpanel/catbot-ipc-server-main/bin" "$project_root/botpanel/cat-steamtxtmode/bin"; do
        if [ -e "$path" ]; then
            chown -R "$SUDO_UID:$SUDO_GID" "$path" 2>/dev/null || true
        fi
    done
}

restore_git_permissions() {
    if [ "$(id -u)" -ne 0 ] || [ -z "${SUDO_UID:-}" ] || [ -z "${SUDO_GID:-}" ]; then
        return
    fi

    if [ ! -d "$project_root/.git" ]; then
        return
    fi

    if find "$project_root/.git" \( ! -user "$SUDO_UID" -o ! -group "$SUDO_GID" \) -print -quit 2>/dev/null | grep -q .; then
        echo "Fixing .git ownership for sudo user before update check..."
        chown -R "$SUDO_UID:$SUDO_GID" "$project_root/.git"
    fi
}

workspace_permissions_need_fix() {
    local path

    for path in \
        "$project_root/obj" \
        "$project_root/bin" \
        "$project_root/libs/funchook" \
        "$project_root/botpanel/catbot-ipc-server-main/bin" \
        "$project_root/botpanel/cat-steamtxtmode/bin"; do
        if [ -e "$path" ] && [ ! -w "$path" ]; then
            return 0
        fi
    done

    return 1
}

fix_workspace_permissions() {
    local target_uid
    local target_gid

    target_uid="${SUDO_UID:-$(id -u)}"
    target_gid="${SUDO_GID:-$(id -g)}"

    run_as_root chown -R "$target_uid:$target_gid" \
        "$project_root/obj" \
        "$project_root/bin" \
        "$project_root/libs/funchook" \
        "$project_root/botpanel/catbot-ipc-server-main/bin" \
        "$project_root/botpanel/cat-steamtxtmode/bin" 2>/dev/null || true
}

run_git() {
    if [ "$(id -u)" -eq 0 ] && [ -n "${SUDO_UID:-}" ] && command -v sudo >/dev/null 2>&1; then
        sudo -H -u "#$SUDO_UID" git -C "$project_root" "$@"
    else
        git -C "$project_root" "$@"
    fi
}

update_project_if_needed() {
    if is_dev_mode; then
        echo "Dev mode enabled; skipping repository update check."
        return
    fi

    if [ ! -d "$project_root/.git" ]; then
        echo "Skipping update check: $project_root is not a git repository."
        return
    fi

    local upstream_branch
    upstream_branch="$(run_git rev-parse --abbrev-ref --symbolic-full-name '@{u}' 2>/dev/null || true)"
    if [ -z "$upstream_branch" ]; then
        echo "Skipping update check: current branch has no upstream."
        return
    fi

    echo "Checking for updates from $upstream_branch..."
    run_git fetch --quiet

    local local_commit
    local remote_commit
    local merge_base
    local_commit="$(run_git rev-parse '@')"
    remote_commit="$(run_git rev-parse '@{u}')"

    if [ "$local_commit" = "$remote_commit" ]; then
        echo "Already up to date."
        return
    fi

    merge_base="$(run_git merge-base '@' '@{u}')"
    if [ "$merge_base" = "$remote_commit" ]; then
        echo "Local branch is ahead of $upstream_branch; no update needed."
        return
    fi

    if [ "$merge_base" != "$local_commit" ]; then
        echo "Local branch has diverged from $upstream_branch; skipping update."
        return
    fi

    if [ -n "$(run_git status --porcelain)" ]; then
        echo "Local changes found; skipping update."
        return
    fi
    echo "Updating from $upstream_branch..."
    run_git pull --ff-only
}

copy_assets() {
    local install_assets_dir="$1"

    if [ ! -d "$asset_source_dir" ]; then
        echo "No assets directory found at $asset_source_dir"
        return
    fi

    run_as_root install -d -m 0755 "$install_assets_dir"
    run_as_root cp -a "$asset_source_dir"/. "$install_assets_dir"/
    echo "Installed assets to $install_assets_dir"
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

install_glew_dependency_for_binary() {
    local binary_path="$1"
    local install_bin_dir="$2"
    local required_library=""
    local source_path=""

    if [ ! -f "$binary_path" ]; then
        return
    fi

    if ! command -v readelf >/dev/null 2>&1; then
        echo "Warning: readelf is missing; cannot bundle libGLEW fallback for $binary_path." >&2
        return
    fi

    required_library="$(readelf -d "$binary_path" 2>/dev/null | awk -F'[][]' '/NEEDED/ && $2 ~ /^libGLEW\.so\./ { print $2; exit }')"
    if [ -z "$required_library" ]; then
        return
    fi

    if [ -f "$install_bin_dir/$required_library" ]; then
        return
    fi

    if ! source_path="$(find_shared_library "$required_library")"; then
        echo "Warning: $binary_path needs $required_library, but it was not found on this system." >&2
        echo "Install glew and rebuild on this machine, or provide $required_library in $install_bin_dir." >&2
        return
    fi

    run_as_root install -m 0755 "$source_path" "$install_bin_dir/$required_library"
    echo "Installed bundled fallback $required_library to $install_bin_dir"
}

install_runtime_dependencies() {
    local mode="$1"
    local install_bin_dir="$2"

    if [ "$mode" = "default" ] || [ "$mode" = "both" ]; then
        install_glew_dependency_for_binary "$project_root/bin/libcathook.so" "$install_bin_dir"
    fi

    if [ "$mode" = "textmode" ] || [ "$mode" = "both" ]; then
        install_glew_dependency_for_binary "$project_root/bin/libcathooktextmode.so" "$install_bin_dir"
    fi
}

normalize_mode() {
    cathook_normalize_mode "$1" 1
}

choose_build_mode() {
    if [ -n "$build_mode" ]; then
        normalize_mode "$build_mode"
        return
    fi

    cathook_select_mode 1
}

clear_execstack_if_needed() {
    local output_binary="$1"

    if ! command -v execstack >/dev/null 2>&1 || [ ! -f "$output_binary" ]; then
        return
    fi

    if [ "$(execstack -q "$output_binary")" = "X $output_binary" ]; then
        execstack -c "$output_binary"
    fi
}

build_cat() {
    local mode="$1"

    case "$mode" in
        default)
            run_make -C "$project_root" CATHOOK_DEBUG_SYMBOLS=1
            ;;
        textmode)
            run_make -C "$project_root" TEXTMODE=1
            ;;
        both)
            run_make -C "$project_root" CATHOOK_DEBUG_SYMBOLS=1
            run_make -C "$project_root" TEXTMODE=1
            ;;
    esac

    run_make -C "$project_root" catbot_ipc
}

install_outputs() {
    local mode="$1"
    local install_bin_dir="$install_root/bin"
    local install_assets_dir="$install_root/assets"
    local install_ipc_dir="$install_root/ipc"
    local install_config_dir="$install_root/configs"
    local install_log_dir="$install_root/logs"
    local source_config_dir="$project_root/opt/cathook/configs"
    local file
    local target_file

    run_as_root install -d -m 0755 "$install_root" "$install_bin_dir" "$install_ipc_dir/bin" "$install_config_dir" "$install_log_dir"

    if [ "$mode" = "default" ] || [ "$mode" = "both" ]; then
        run_as_root install -m 0755 "$project_root/bin/libcathook.so" "$install_bin_dir/libcathook.so"
    fi

    if [ "$mode" = "textmode" ] || [ "$mode" = "both" ]; then
        run_as_root install -m 0755 "$project_root/bin/libcathooktextmode.so" "$install_bin_dir/libcathooktextmode.so"
        run_as_root install -m 0755 "$project_root/bin/libcathooktextmode.so" "$install_bin_dir/libcathook-textmode.so"
    fi

    install_runtime_dependencies "$mode" "$install_bin_dir"
    if [ -d "$source_config_dir" ]; then
        while IFS= read -r -d '' file; do
            target_file="$install_config_dir/$(basename -- "$file")"
            if [ ! -e "$target_file" ]; then
                run_as_root install -m 0644 "$file" "$target_file"
            fi
        done < <(find "$source_config_dir" -maxdepth 1 -type f -name '*.cat' -print0)
    fi
    run_as_root make SHELL="$(make_shell_path)" -C "$project_root/botpanel/catbot-ipc-server-main" REPO_ROOT="$project_root" INSTALL_DIR="$install_ipc_dir" install
    if [ -x "$project_root/botpanel/cat-steamtxtmode/install.sh" ]; then
        run_as_root env CATHOOK_ROOT="$install_root" bash "$project_root/botpanel/cat-steamtxtmode/install.sh" || \
            echo "cat-steamtxtmode build/install failed; bots will run without the Steam shim." >&2
    fi
    copy_assets "$install_assets_dir"
    fix_install_permissions
    echo "Installed Cat runtime to $install_root"
}

install_enabled=1

while [ "$#" -gt 0 ]; do
    case "$1" in
        --default)
            build_mode="default"
            ;;
        --textmode)
            build_mode="textmode"
            ;;
        --both)
            build_mode="both"
            ;;
        --no-install)
            install_enabled=0
            ;;
        --dev | --no-update)
            export CATHOOK_DEV_MODE=1
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        default | non-textmode | non_textmode | normal | gui | textmode | text | both | all)
            build_mode="$(normalize_mode "$1")"
            ;;
        *)
            echo "Unknown build option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
    shift
done

selected_mode="$(choose_build_mode)" || {
    echo "Invalid build mode: ${build_mode:-}" >&2
    exit 1
}

update_project_if_needed
require_root_for_install

if workspace_permissions_need_fix; then
    fix_workspace_permissions
fi
build_cat "$selected_mode"

clear_execstack_if_needed "$project_root/bin/libcathook.so"
clear_execstack_if_needed "$project_root/bin/libcathooktextmode.so"

if [ "$install_enabled" = "1" ]; then
    install_outputs "$selected_mode"
fi
