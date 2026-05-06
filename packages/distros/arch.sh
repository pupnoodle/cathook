#!/usr/bin/env bash
set -euo pipefail

run_as_root() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    else
        echo "This installer needs root privileges. Install sudo or run as root." >&2
        exit 1
    fi
}

has_shared_library() {
    local library_name="$1"

    if command -v ldconfig >/dev/null 2>&1 && ldconfig -p 2>/dev/null | grep -Fq "/$library_name"; then
        return 0
    fi

    [ -e "/usr/lib/$library_name" ] || \
        [ -e "/usr/lib64/$library_name" ] || \
        [ -e "/run/host/usr/lib/$library_name" ] || \
        [ -e "/run/host/usr/lib64/$library_name" ]
}

check_bundled_glew_soname() {
    local script_dir
    local repo_root
    local binary_path
    local required_library

    if ! command -v readelf >/dev/null 2>&1; then
        return
    fi

    script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
    repo_root="$(cd -- "$script_dir/../.." && pwd)"

    for binary_path in "$repo_root/bin/libcathook.so" "$repo_root/bin/libcathooktextmode.so"; do
        if [ ! -f "$binary_path" ]; then
            continue
        fi

        required_library="$(readelf -d "$binary_path" 2>/dev/null | awk -F'[][]' '/NEEDED/ && $2 ~ /^libGLEW\.so\./ { print $2; exit }')"
        if [ -z "$required_library" ] || has_shared_library "$required_library"; then
            continue
        fi

        echo "Warning: $binary_path needs $required_library, but pacman did not install that SONAME." >&2
        echo "On Manjaro this usually means the checked-in/prebuilt binary was built against a newer Arch GLEW." >&2
        echo "Run sudo ./build.sh after ./install-deps, or update Manjaro with sudo pacman -Syu until glew provides $required_library." >&2
    done
}

run_as_root pacman -Syu --needed --noconfirm \
    base-devel \
    ca-certificates \
    cmake \
    firejail \
    gdb \
    git \
    glew \
    glu \
    iproute2 \
    libglvnd \
    make \
    mesa \
    net-tools \
    nodejs \
    npm \
    pkgconf \
    rsync \
    sdl2 \
    vulkan-headers \
    vulkan-icd-loader \
    wget \
    xorg-server-xvfb \
    xorg-xauth \
    xpra

if pacman -Si execstack >/dev/null 2>&1; then
    run_as_root pacman -S --needed --noconfirm execstack
else
    echo "execstack is not in the configured pacman repositories; build.sh will skip it."
fi

check_bundled_glew_soname
