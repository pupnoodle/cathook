#!/usr/bin/env bash
set -euo pipefail

force=0

usage() {
  printf 'usage: %s [--force]\n' "$0"
  printf 'default mode is dry-run. pass --force to delete Steam state.\n'
  printf 'installed games under steamapps/common are preserved.\n'
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --force)
      force=1
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
xdg_data_home="${XDG_DATA_HOME:-$home_dir/.local/share}"
xdg_config_home="${XDG_CONFIG_HOME:-$home_dir/.config}"
xdg_cache_home="${XDG_CACHE_HOME:-$home_dir/.cache}"

run_rm() {
  path="$1"
  if [ ! -e "$path" ] && [ ! -L "$path" ]; then
    return 0
  fi
  if [ "$force" -eq 1 ]; then
    printf 'remove %s\n' "$path"
    rm -rf -- "$path"
  else
    printf 'would remove %s\n' "$path"
  fi
}

clean_steam_root() {
  steam_root="$1"
  if [ ! -d "$steam_root" ]; then
    return 0
  fi

  printf '\nsteam root: %s\n' "$steam_root"

  steamapps_paths="$(collect_steamapps_paths "$steam_root")"

  run_rm "$steam_root/userdata"
  run_rm "$steam_root/config"
  run_rm "$steam_root/appcache"
  run_rm "$steam_root/logs"
  run_rm "$steam_root/depotcache"
  run_rm "$steam_root/steam/cached"
  run_rm "$steam_root/.crash"
  run_rm "$steam_root/local.vdf"
  run_rm "$steam_root/update_hosts_cached.vdf"

  for path in "$steam_root"/ssfn*; do
    [ -e "$path" ] || [ -L "$path" ] || continue
    run_rm "$path"
  done

  # Steam can leave these state directories below several different client
  # and runtime directories. Match by basename so custom layouts are covered.
  find "$steam_root" -xdev -type d \( \
    -name htmlcache -o -name avatarcache -o -name httpcache -o \
    -name librarycache -o -name stats -o -name dumps -o \
    -name shadercache -o -name compatdata -o -name workshop -o \
    -name downloading -o -name incomplete -o -name temp \
  \) -print 2>/dev/null | while IFS= read -r path; do
    run_rm "$path"
  done

  find "$steam_root" -xdev -type f \( \
    -name loginusers.vdf -o -name registry.vdf -o -name DialogConfig.vdf -o \
    -name remoteclients.vdf -o -name sharedconfig.vdf -o -name localconfig.vdf -o \
    -name steam.pid -o -name steam.token -o -name '*.crash' \
  \) -print 2>/dev/null | while IFS= read -r path; do
    run_rm "$path"
  done

  printf '%s\n' "$steamapps_paths" | awk '!seen[$0]++' | while IFS= read -r steamapps; do
    clean_steamapps_state "$steamapps"
  done
}

collect_steamapps_paths() {
  steam_root="$1"
  printf '%s\n' "$steam_root/steamapps"

  for library_file in \
    "$steam_root/steamapps/libraryfolders.vdf" \
    "$steam_root/config/libraryfolders.vdf"; do
    [ -f "$library_file" ] || continue
    sed -nE 's/^[[:space:]]*"path"[[:space:]]*"([^"]+)".*/\1/p' "$library_file" |
      while IFS= read -r library_root; do
        [ -n "$library_root" ] || continue
        printf '%s/steamapps\n' "$library_root"
      done
  done
}

clean_steamapps_state() {
  steamapps="$1"
  if [ ! -d "$steamapps" ]; then
    return 0
  fi

  printf '\nsteamapps state: %s\n' "$steamapps"

  # These are downloaded, generated, or user-created files. Installed game
  # files and app manifests remain in place.
  run_rm "$steamapps/downloading"
  run_rm "$steamapps/temp"
  run_rm "$steamapps/shadercache"
  run_rm "$steamapps/workshop"
  run_rm "$steamapps/compatdata"
  run_rm "$steamapps/sourcemods"
  run_rm "$steamapps/music"
  run_rm "$steamapps/incomplete"
}

clean_user_paths() {
  run_rm "$home_dir/.steampid"
  run_rm "$home_dir/.steampath"
  run_rm "$home_dir/.steam/registry.vdf"
  run_rm "$home_dir/.steam/exportedsettings.json"
  run_rm "$home_dir/.steam/steam.pid"
  run_rm "$home_dir/.steam/steam.token"
  run_rm "$home_dir/.steam/steam.pipe"

  # Native Steam, Flatpak Steam, Snap Steam, and custom XDG profiles keep
  # client state outside the Steam installation directory. Do not remove
  # the parent profile directories: they may contain data for other apps.
  for profile_root in \
    "$home_dir" \
    "$home_dir/.var/app/com.valvesoftware.Steam" \
    "$home_dir/snap/steam/common"; do
    run_rm "$profile_root/.cache/Steam"
    run_rm "$profile_root/.cache/steam"
    run_rm "$profile_root/.cache/Valve Corporation/Steam"
    run_rm "$profile_root/.config/Steam"
    run_rm "$profile_root/.config/steam"
    run_rm "$profile_root/.config/Valve Corporation/Steam"
  done

  for xdg_root in "$xdg_config_home" "$xdg_cache_home"; do
    run_rm "$xdg_root/Steam"
    run_rm "$xdg_root/steam"
    run_rm "$xdg_root/Valve Corporation/Steam"
  done
}

stop_steam() {
  if command -v pgrep >/dev/null 2>&1; then
    steam_processes=""
    for process_name in steam steamwebhelper steamservice; do
      if pgrep -u "$(id -u)" -x "$process_name" >/dev/null 2>&1; then
        steam_processes="${steam_processes:+$steam_processes }$process_name"
      fi
    done
    [ -n "$steam_processes" ] || return 0

    if [ "$force" -eq 1 ]; then
      printf 'stopping steam processes: %s\n' "$steam_processes"
      for process_name in $steam_processes; do
        pkill -u "$(id -u)" -x "$process_name" || true
      done
      sleep 2
    else
      printf 'would stop steam processes: %s\n' "$steam_processes"
    fi
  fi
}

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

is_steam_root() {
  root="$1"
  [ -d "$root" ] || return 1
  [ -f "$root/steam.sh" ] || return 1
  [ "$root" != "/" ] && [ "$root" != "$home_dir" ]
}

add_root() {
  root="$1"
  [ -n "$root" ] || return 0
  [ -d "$root" ] || return 0
  is_steam_root "$root" || return 0
  resolved_root="$(real_path "$root")"
  is_steam_root "$resolved_root" || return 0
  [ "$resolved_root" != "/" ] && [ "$resolved_root" != "$home_dir" ] || return 0
  printf '%s\n' "$resolved_root"
}

add_root_from_path() {
  path="$1"
  while [ -n "$path" ] && [ "$path" != "/" ]; do
    if is_steam_root "$path"; then
      add_root "$path"
      return 0
    fi
    parent="${path%/*}"
    [ "$parent" != "$path" ] || break
    path="${parent:-/}"
  done
}

collect_roots() {
  {
    # Standard native, Flatpak, Snap, and XDG locations.
    add_root "$home_dir/.steam/debian-installation"
    add_root "$home_dir/.steam/steam"
    add_root "$home_dir/.steam/root"
    add_root "$xdg_data_home/Steam"
    add_root "$xdg_data_home/steam"
    add_root "$home_dir/.var/app/com.valvesoftware.Steam/.local/share/Steam"
    add_root "$home_dir/snap/steam/common/.local/share/Steam"

    # Allow callers and sibling scripts to identify installations outside
    # the standard locations without making the cleaner scan the filesystem.
    add_root "${STEAM_ROOT:-}"
    add_root "${STEAM_PATH:-}"
    add_root "${CAT_STEAM_ROOT:-}"
    add_root "${CAT_SHARED_STEAM_ROOT:-}"

    # A Steam install in a custom directory normally has steam.sh. Searching
    # for that marker finds all such installs under this user's home while
    # avoiding broad deletion based on a directory name alone.
    if [ -d "$home_dir" ]; then
      find "$home_dir" -type f -name steam.sh -print 2>/dev/null |
        while IFS= read -r launcher; do
          add_root "${launcher%/*}"
        done
    fi

    # Inspect common system and mounted data locations as well. -xdev keeps
    # each search from unexpectedly walking an unrelated mounted filesystem.
    for search_root in /opt /mnt /media /run/media /srv /var/lib/flatpak /var/snap; do
      [ -d "$search_root" ] || continue
      find "$search_root" -xdev -type f -name steam.sh -print 2>/dev/null |
        while IFS= read -r launcher; do
          add_root "${launcher%/*}"
        done
    done

    # If an install lives outside HOME, discover it from this user's running
    # Steam processes before they are stopped below. Filter with pgrep so a
    # readable process from another user cannot become a deletion target.
    if command -v pgrep >/dev/null 2>&1; then
      for process_name in steam steamwebhelper steamservice; do
        pgrep -u "$(id -u)" -x "$process_name" 2>/dev/null || true
      done | while IFS= read -r process_id; do
        process_exe="/proc/$process_id/exe"
        [ -L "$process_exe" ] || continue
        executable="$(readlink -f "$process_exe" 2>/dev/null || true)"
        add_root_from_path "${executable%/*}"
      done
    fi
  } | awk '!seen[$0]++'
}

if [ "$force" -eq 0 ]; then
  printf 'dry-run mode. re-run with --force to delete.\n'
fi

roots="$(collect_roots)"

stop_steam
clean_user_paths

if [ -z "$roots" ]; then
  printf 'no steam roots found\n'
  exit 0
fi

printf '%s\n' "$roots" | while IFS= read -r steam_root; do
  clean_steam_root "$steam_root"
done

printf '\ndone\n'
