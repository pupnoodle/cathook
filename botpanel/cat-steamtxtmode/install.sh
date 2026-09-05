#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
steam_root="${1:-/opt/catbot-shared-steam}"
install_root="${2:-/opt/cathook/botpanel/cat-steamtxtmode}"
python3 - "$steam_root" <<'PY'
from pathlib import Path
import re
import sys
root = Path(sys.argv[1])
manifest = (root / 'package/steam_client_ubuntu12.manifest').read_text()
if not re.search(r'"version"\s+"1689034492"', manifest):
    raise SystemExit('Steam must be downgraded to build 1689034492 before installation')
launcher = root / 'steam.sh'
text = launcher.read_text()
if 'CAT_STEAM_TXTMODE_PRELOAD' not in text:
    original = 'elif [ -z "$STEAM_DEBUGGER" ]; then\n\t"$STEAMROOT/$STEAMEXEPATH" "$@"'
    replacement = 'elif [ -z "$STEAM_DEBUGGER" ]; then\n\tif [ -n "${CAT_STEAM_TXTMODE_PRELOAD-}" ]; then\n\t\tLD_PRELOAD="$CAT_STEAM_TXTMODE_PRELOAD" "$STEAMROOT/$STEAMEXEPATH" "$@"\n\telse\n\t\t"$STEAMROOT/$STEAMEXEPATH" "$@"\n\tfi'
    if text.count(original) != 1:
        raise SystemExit('Steam launch boundary was not found uniquely')
    temporary = launcher.with_suffix('.sh.tmp')
    temporary.write_text(text.replace(original, replacement))
    temporary.chmod(launcher.stat().st_mode)
    temporary.replace(launcher)
PY
for arch in 32 64; do
    make -C "$script_dir" ARCH="$arch" all verify test
    if [ "$arch" = 32 ]; then directory=lib64; else directory=libx64; fi
    mkdir -p "$install_root/bin/$directory"
    for binary in libcatsteamtxtmode.so catsteamtxtmode; do
        install -m 755 "$script_dir/bin/$directory/$binary" "$install_root/bin/$directory/$binary.tmp"
        mv -f "$install_root/bin/$directory/$binary.tmp" "$install_root/bin/$directory/$binary"
    done
done
