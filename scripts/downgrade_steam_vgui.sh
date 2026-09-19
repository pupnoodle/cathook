#!/usr/bin/env bash
set -euo pipefail

target_version="1689034492"
archive_url="${PUP_STEAM_ARCHIVE_URL:-http://web.archive.org/web/20230711164652if_/media.steampowered.com/client}"
force=0
display_value="${DISPLAY:-}"

usage() {
  printf 'usage: %s [--force] [--display DISPLAY]\n' "$0"
  printf 'example: %s --force --display :10\n' "$0"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --force)
      force=1
      ;;
    --display)
      shift
      if [ "$#" -eq 0 ]; then
        usage >&2
        exit 2
      fi
      display_value="$1"
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      usage >&2
      exit 2
      ;;
  esac
  shift
done

home_dir="${HOME:?}"

real_path() {
  path="$1"
  if command -v realpath >/dev/null 2>&1; then
    realpath "$path"
  elif command -v readlink >/dev/null 2>&1; then
    readlink -f "$path" 2>/dev/null || printf '%s\n' "$path"
  else
    printf '%s\n' "$path"
  fi
}

add_root() {
  root="$1"
  [ -n "$root" ] || return 0
  [ -d "$root" ] || return 0
  if [ -x "$root/steam.sh" ] || [ -x "$root/steam" ]; then
    real_path "$root"
  fi
}

collect_roots() {
  {
    add_root "${PUP_STEAM_ROOT:-}"
    add_root "${PUP_SHARED_STEAM_ROOT:-}"
    add_root "$home_dir/.steam/debian-installation"
    add_root "$home_dir/.steam/steam"
    add_root "$home_dir/.steam/root"
    add_root "$home_dir/.local/share/Steam"
    add_root "$home_dir/.var/app/com.valvesoftware.Steam/.local/share/Steam"
    add_root "$home_dir/snap/steam/common/.local/share/Steam"
  } | awk '!seen[$0]++'
}

detect_display() {
  if [ -n "$display_value" ]; then
    printf '%s\n' "$display_value"
    return 0
  fi

  if [ -d /tmp/.X11-unix ]; then
    find /tmp/.X11-unix -maxdepth 1 -type s -name 'X*' -printf '%f\n' 2>/dev/null | sed 's/^X/:/' | sort -V | tail -1
  fi
}

stop_steam() {
  pkill -u "$(id -u)" -x steam 2>/dev/null || true
  pkill -u "$(id -u)" -x steamwebhelper 2>/dev/null || true
  sleep 2
}

write_lock_files() {
  steam_root="$1"
  cat > "$steam_root/steam.cfg" <<EOF
BootStrapperInhibitAll=Enable
BootStrapperForceSelfUpdate=False
EOF
  rm -rf "$steam_root/package/tmp"
  mkdir -p "$steam_root/package"
  : > "$steam_root/package/tmp"
}

write_launchers() {
  steam_root="$1"
  mkdir -p "$home_dir/.local/bin" "$home_dir/bin" "$home_dir/Desktop"
  cat > "$home_dir/.local/bin/steam-vgui" <<EOF
#!/usr/bin/env bash
exec "$steam_root/steam.sh" -vgui "\$@"
EOF
  chmod +x "$home_dir/.local/bin/steam-vgui"
  ln -sf "$home_dir/.local/bin/steam-vgui" "$home_dir/bin/steam-vgui"
  cat > "$home_dir/Desktop/Steam VGUI.desktop" <<EOF
[Desktop Entry]
Name=Steam VGUI
Comment=Steam old VGUI client
Exec=$home_dir/.local/bin/steam-vgui
Icon=steam
Terminal=false
Type=Application
Categories=Network;FileTransfer;Game;
StartupNotify=false
EOF
  chmod +x "$home_dir/Desktop/Steam VGUI.desktop"
  for profile in "$home_dir/.bashrc" "$home_dir/.profile"; do
    touch "$profile"
    if ! grep -Fq '$HOME/bin:$HOME/.local/bin' "$profile"; then
      printf '\nexport PATH="$HOME/bin:$HOME/.local/bin:$PATH"\n' >> "$profile"
    fi
  done
}

installed_version() {
  steam_root="$1"
  grep -R '"version"' "$steam_root/package"/steam_client_ubuntu12* 2>/dev/null | head -1 | awk -F'"' '{print $4}'
}

run_downgrade() {
  steam_root="$1"
  display_name="$(detect_display)"
  if [ -z "$display_name" ]; then
    printf 'no display found. pass --display :0 or --display :10\n' >&2
    exit 1
  fi

  export DISPLAY="$display_name"
  if [ -f "$home_dir/.Xauthority" ]; then
    export XAUTHORITY="$home_dir/.Xauthority"
  fi

  stop_steam
  rm -f "$steam_root/steam.cfg"
  rm -rf "$steam_root/package"
  mkdir -p "$steam_root/package"

  printf 'downgrading %s to %s using display %s\n' "$steam_root" "$target_version" "$DISPLAY"
  "$steam_root/steam.sh" -forcesteamupdate -forcepackagedownload -overridepackageurl "$archive_url" -exitsteam -textmode
  stop_steam
}

roots="$(collect_roots)"

if [ -z "$roots" ]; then
  printf 'no steam install found\n' >&2
  exit 1
fi

steam_root="$(printf '%s\n' "$roots" | head -1)"
current_version="$(installed_version "$steam_root" || true)"

printf 'steam root: %s\n' "$steam_root"
printf 'current version: %s\n' "${current_version:-unknown}"
printf 'target version: %s\n' "$target_version"

if [ "$force" -ne 1 ]; then
  printf 'dry-run mode. re-run with --force to downgrade and write launchers.\n'
  exit 0
fi

run_downgrade "$steam_root"
write_lock_files "$steam_root"
write_launchers "$steam_root"
final_version="$(installed_version "$steam_root" || true)"

printf 'final version: %s\n' "${final_version:-unknown}"
printf 'launcher: %s\n' "$home_dir/.local/bin/steam-vgui"
printf 'desktop: %s\n' "$home_dir/Desktop/Steam VGUI.desktop"
printf 'done\n'
