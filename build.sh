#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
asset_source_dir="$project_root/assets"
install_root="${CATHOOK_ROOT:-/opt/cathook}"
build_mode="${CAT_BUILD_MODE:-}"

usage() {
    cat <<'EOF'
Usage: sudo ./build.sh [default|textmode|both|--default|--textmode|--both|--no-install]

Without a mode argument, builds the default NORMAL/NON-TEXTMODE
libcathook.so with SDL hooking enabled.

Install mode writes to /opt/cathook by default and MUST RUN AS SUDO.
Use --no-install for a local user build without sudo.

Environment:
  CAT_BUILD_MODE=default|textmode|both
  CATHOOK_ROOT=/opt/cathook
EOF
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

fix_install_permissions() {
    if [ ! -d "$install_root" ]; then
        return
    fi

    run_as_root chmod -R u=rwX,go=rX "$install_root"
    if [ -d "$install_root/bin" ]; then
        run_as_root find "$install_root/bin" -type f -exec chmod 0755 {} +
    fi
    if [ -d "$install_root/ipc/bin" ]; then
        run_as_root find "$install_root/ipc/bin" -type f -exec chmod 0755 {} +
    fi
}

restore_workspace_permissions() {
    if [ "$(id -u)" -ne 0 ] || [ -z "${SUDO_UID:-}" ] || [ -z "${SUDO_GID:-}" ]; then
        return
    fi

    for path in "$project_root/bin" "$project_root/obj" "$project_root/libs/funchook" "$project_root/botpanel/catbot-ipc-server-main/bin"; do
        if [ -e "$path" ]; then
            chown -R "$SUDO_UID:$SUDO_GID" "$path" 2>/dev/null || true
        fi
    done
}

trap restore_workspace_permissions EXIT

run_git() {
    if [ "$(id -u)" -eq 0 ] && [ -n "${SUDO_UID:-}" ] && command -v sudo >/dev/null 2>&1; then
        sudo -u "#$SUDO_UID" git -C "$project_root" "$@"
    else
        git -C "$project_root" "$@"
    fi
}

update_project_if_needed() {
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
        echo "Local branch is not a fast-forward of $upstream_branch; update manually before building." >&2
        exit 1
    fi

    if [ -n "$(run_git status --porcelain --untracked-files=no)" ]; then
        echo "Working tree has local changes; commit or stash them before updating." >&2
        exit 1
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

normalize_mode() {
    case "$1" in
        default | non-textmode | non_textmode | normal | gui | 1)
            printf '%s\n' "default"
            ;;
        textmode | text | 2)
            printf '%s\n' "textmode"
            ;;
        both | all | 3)
            printf '%s\n' "both"
            ;;
        *)
            return 1
            ;;
    esac
}

choose_build_mode() {
    if [ -n "$build_mode" ]; then
        normalize_mode "$build_mode"
        return
    fi

    printf '%s\n' "default"
}

ensure_funchook() {
    mkdir -p "$project_root/libs"

    if [ -d "$project_root/libs/funchook" ] &&
       [ ! -f "$project_root/libs/funchook/libfunchook.a" ] &&
       [ ! -f "$project_root/libs/funchook/libdistorm.a" ]; then
        rm -rf "$project_root/libs/funchook"
    fi

    if [ ! -d "$project_root/libs/funchook" ]; then
        git clone https://github.com/Doctor-Coomer/funchook.git "$project_root/libs/funchook"
    fi

    if [ ! -f "$project_root/libs/funchook/libfunchook.a" ] ||
       [ ! -f "$project_root/libs/funchook/libdistorm.a" ]; then
        cmake -S "$project_root/libs/funchook" -B "$project_root/libs/funchook/build" -DCMAKE_BUILD_TYPE=Release
        cmake --build "$project_root/libs/funchook/build" --parallel "$(nproc 2>/dev/null || echo 1)"
    fi
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
            make -C "$project_root"
            ;;
        textmode)
            make -C "$project_root" TEXTMODE=1
            ;;
        both)
            make -C "$project_root" both
            ;;
    esac

    make -C "$project_root" catbot_ipc
}

install_outputs() {
    local mode="$1"
    local install_bin_dir="$install_root/bin"
    local install_assets_dir="$install_root/assets"
    local install_ipc_dir="$install_root/ipc"

    run_as_root install -d -m 0755 "$install_root" "$install_bin_dir" "$install_ipc_dir/bin"

    if [ "$mode" = "default" ] || [ "$mode" = "both" ]; then
        run_as_root install -m 0755 "$project_root/bin/libcathook.so" "$install_bin_dir/libcathook.so"
    fi

    if [ "$mode" = "textmode" ] || [ "$mode" = "both" ]; then
        run_as_root install -m 0755 "$project_root/bin/libcathooktextmode.so" "$install_bin_dir/libcathooktextmode.so"
        run_as_root install -m 0755 "$project_root/bin/libcathooktextmode.so" "$install_bin_dir/libcathook-textmode.so"
    fi

    run_as_root make -C "$project_root/botpanel/catbot-ipc-server-main" REPO_ROOT="$project_root" INSTALL_DIR="$install_ipc_dir" install
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
ensure_funchook
build_cat "$selected_mode"

clear_execstack_if_needed "$project_root/bin/libcathook.so"
clear_execstack_if_needed "$project_root/bin/libcathooktextmode.so"

if [ "$install_enabled" = "1" ]; then
    install_outputs "$selected_mode"
fi
