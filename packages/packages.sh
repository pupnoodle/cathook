#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/.." && pwd)"

source_os_release() {
    if [ -r /etc/os-release ]; then
        # shellcheck disable=SC1091
        . /etc/os-release
    else
        ID=""
        ID_LIKE=""
        NAME="unknown Linux"
    fi
}

run_distro_script() {
    local distro_name="$1"
    local script_name="$2"
    local support_status="${3:-official}"

    echo "Detected $distro_name"
    if [ "$support_status" != "official" ]; then
        echo "This distro is not officially supported."
        print_supported_distros
    fi

    exec bash "$repo_root/packages/distros/$script_name"
}

source_os_release

print_supported_distros() {
    echo "Officially supported Linux distros: Debian, Ubuntu, Linux Mint, Debian-close distros, and Manjaro."
}

case "${ID:-}" in
    debian | ubuntu | linuxmint | pop | elementary | zorin)
        run_distro_script "Debian/Ubuntu family" "debian.sh"
        ;;
    manjaro)
        run_distro_script "Manjaro" "arch.sh"
        ;;
    arch | endeavouros | garuda)
        run_distro_script "Arch Linux family" "arch.sh" "unofficial"
        ;;
    fedora | rhel | centos | rocky | almalinux | nobara)
        run_distro_script "Fedora/RHEL family" "fedora.sh" "unofficial"
        ;;
    gentoo)
        run_distro_script "Gentoo" "gentoo.sh" "unofficial"
        ;;
    void)
        run_distro_script "Void Linux" "void.sh" "unofficial"
        ;;
    alpine)
        run_distro_script "Alpine Linux" "alpine.sh" "unofficial"
        ;;
    opensuse* | sles)
        run_distro_script "openSUSE/SLES family" "opensuse.sh" "unofficial"
        ;;
esac

case " ${ID_LIKE:-} " in
    *" debian "*)
        run_distro_script "Debian-like distribution" "debian.sh"
        ;;
    *" arch "*)
        run_distro_script "Arch-like distribution" "arch.sh" "unofficial"
        ;;
    *" fedora "* | *" rhel "*)
        run_distro_script "Fedora/RHEL-like distribution" "fedora.sh" "unofficial"
        ;;
    *" gentoo "*)
        run_distro_script "Gentoo-like distribution" "gentoo.sh" "unofficial"
        ;;
    *" suse "*)
        run_distro_script "SUSE-like distribution" "opensuse.sh" "unofficial"
        ;;
esac

if command -v apt-get >/dev/null 2>&1; then
    run_distro_script "apt-based distribution" "debian.sh"
elif command -v pacman >/dev/null 2>&1; then
    run_distro_script "pacman-based distribution" "arch.sh" "unofficial"
elif command -v dnf >/dev/null 2>&1 || command -v yum >/dev/null 2>&1; then
    run_distro_script "dnf/yum-based distribution" "fedora.sh" "unofficial"
elif command -v emerge >/dev/null 2>&1; then
    run_distro_script "emerge-based distribution" "gentoo.sh" "unofficial"
elif command -v xbps-install >/dev/null 2>&1; then
    run_distro_script "xbps-based distribution" "void.sh" "unofficial"
elif command -v apk >/dev/null 2>&1; then
    run_distro_script "apk-based distribution" "alpine.sh" "unofficial"
elif command -v zypper >/dev/null 2>&1; then
    run_distro_script "zypper-based distribution" "opensuse.sh" "unofficial"
fi

echo "Unsupported Linux distribution: ${NAME:-unknown}"
print_supported_distros
exit 1
