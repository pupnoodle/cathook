#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

if [ "$(uname -s)" != "Linux" ]; then
    echo "./setup.sh must run on Linux." >&2
    exit 1
fi

if [ "$(id -u)" -eq 0 ]; then
    echo "Run setup as your normal Linux user, not root." >&2
    exit 1
fi

echo "Installing dependencies, building Cat, and preparing the bundled botpanel..."
bash "$script_dir/botpanel/update" "$@"

echo
echo "Setup complete."
echo "Start the botpanel with ./botpanel/start"
