#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"

if [ "$(id -u)" -ne 0 ]; then
    echo "MUST RUN AS SUDO: sudo ./install.sh" >&2
    exit 1
fi

make -C "$script_dir" REPO_ROOT="$repo_root" clean
make -C "$script_dir" REPO_ROOT="$repo_root" -j"$(nproc --all)"
make -C "$script_dir" REPO_ROOT="$repo_root" install
chmod -R u=rwX,go=rX /opt/puphook/ipc
