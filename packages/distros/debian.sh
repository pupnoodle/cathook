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

export DEBIAN_FRONTEND="${DEBIAN_FRONTEND:-noninteractive}"

package_available() {
    apt-cache policy "$1" 2>/dev/null | awk '
        $1 == "Candidate:" {
            found = 1
            exit ($2 == "(none)" ? 1 : 0)
        }
        END {
            if (!found) {
                exit 1
            }
        }'
}

source_os_release() {
    if [ -r /etc/os-release ]; then
        # shellcheck disable=SC1091
        . /etc/os-release
    fi
}

xpra_repo_file() {
    case "${CAT_XPRA_REPO_CHANNEL:-stable}" in
        stable)
            printf '%s\n' "xpra.sources"
            ;;
        lts)
            printf '%s\n' "xpra-lts.sources"
            ;;
        beta)
            printf '%s\n' "xpra-beta.sources"
            ;;
        *)
            echo "Unsupported CAT_XPRA_REPO_CHANNEL=${CAT_XPRA_REPO_CHANNEL}. Use stable, lts, or beta." >&2
            return 1
            ;;
    esac
}

configure_xpra_repo() {
    local architecture=""
    local codename="${VERSION_CODENAME:-${UBUNTU_CODENAME:-}}"
    local repo_file=""
    local repo_url=""

    if [ "${CAT_INSTALL_XPRA_REPO:-1}" = "0" ]; then
        return 1
    fi

    if command -v dpkg >/dev/null 2>&1; then
        architecture="$(dpkg --print-architecture)"
    fi

    case "$architecture" in
        amd64 | arm64)
            ;;
        *)
            echo "Skipping Xpra upstream apt repo for unsupported architecture: ${architecture:-unknown}." >&2
            return 1
            ;;
    esac

    if [ -z "$codename" ]; then
        echo "Skipping Xpra upstream apt repo because the distro codename is unknown." >&2
        return 1
    fi

    if ! repo_file="$(xpra_repo_file)"; then
        return 1
    fi

    repo_url="https://raw.githubusercontent.com/Xpra-org/xpra/master/packaging/repos/$codename/$repo_file"

    echo "xpra package is not available in this apt repository; adding Xpra upstream apt repo for $codename."
    run_as_root apt-get install -y --no-install-recommends ca-certificates wget
    run_as_root install -d /usr/share/keyrings /etc/apt/sources.list.d

    if ! run_as_root wget -O /usr/share/keyrings/xpra.asc https://xpra.org/xpra.asc; then
        echo "Failed to download Xpra apt signing key." >&2
        return 1
    fi

    if ! run_as_root wget -O "/etc/apt/sources.list.d/$repo_file" "$repo_url"; then
        echo "Failed to download Xpra apt source file: $repo_url" >&2
        run_as_root rm -f "/etc/apt/sources.list.d/$repo_file"
        return 1
    fi

    run_as_root apt-get update
    package_available xpra
}

source_os_release
run_as_root apt-get update
packages=(
    build-essential \
    ca-certificates \
    cmake \
    firejail \
    gdb \
    git \
    iproute2 \
    libglew-dev \
    libgl-dev \
    libsdl2-dev \
    libvulkan-dev \
    make \
    net-tools \
    nodejs \
    npm \
    pkg-config \
    rsync \
    wget \
    xauth \
    xserver-xorg-core \
    xserver-xorg-video-dummy \
    xvfb
)

if package_available xpra || configure_xpra_repo; then
    packages+=(xpra)
    if package_available xpra-x11; then
        packages+=(xpra-x11)
    fi
else
    echo "xpra package is not available; botpanel/start will use Xvfb instead."
fi

run_as_root apt-get install -y --no-install-recommends "${packages[@]}"

if package_available execstack; then
    run_as_root apt-get install -y --no-install-recommends execstack
else
    echo "execstack package is not available on this apt repository; build.sh will skip it."
fi
