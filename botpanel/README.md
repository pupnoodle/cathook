# Cat botpanel setup

This botpanel is bundled inside the main Cat repository. Do not clone separate
`catbot-ipc-server`, `catbot-ipc-web-panel`, or `cathook` repositories.

```sh
git clone https://github.com/pupnoodle/cathook
cd cathook
./setup.sh
```

Next, edit `botpanel/accounts.txt` and add bot accounts in this format:

USERNAME:PASSWORD
USERNAME:PASSWORD
USERNAME:PASSWORD

# Cat botpanel launcher

`./botpanel/start` asks which headless bot display backend to use each time
when both xpra and Xvfb are installed. The choice is not saved between runs.
Choosing xpra stops the matching Xvfb display first; choosing Xvfb stops the
matching xpra display first.

- Default display: `:699`
- Override display: `CAT_DISPLAY=:700 ./botpanel/start`
- Xpra-specific override: `CAT_XPRA_DISPLAY=:700 ./botpanel/start`
- Legacy Xvfb override still accepted: `CAT_XVFB_DISPLAY=:700 ./botpanel/start`
- Headless display size: `CAT_X_SCREEN=1x1x24 ./botpanel/start` (default `1x1x24`; use `CAT_XPRA_SCREEN`, `CAT_XVFB_SCREEN`, or `CAT_PER_BOT_X_SCREEN` for backend-specific overrides)
- Xvfb client limit: `CAT_XVFB_MAX_CLIENTS=512 ./botpanel/start` (default `512`; accepted values are `64`, `128`, `256`, `512`)
- Headless bots use chunked Xvfb displays by default: 25 bots per display, base display `:699`, max 12 displays.
- Headless text-mode defaults are enabled: Steam text-mode preload, hidden 1×1
  UI windows, dropped UI draw/present work, 100 ms present pacing, one Mesa
  llvmpipe worker, `vblank_mode=0`, and the 64-bit WebHelper preload wrapper.
- Text-mode TF2 defaults remain `-noshaderapi -nomouse -nosound`, one game
  thread, `+fps_max 30`, and the non-cap allocator tuning used on the SSH
  host. No heap-size or renderer-process hard limits are added.
- Steam WebHelper cleanup is disabled by default so Steam's normal service/UI
  process remains available; set `CAT_STEAMWEBHELPER_CLEANUP=1` explicitly if
  you want the old cleanup behavior.
- Override chunking: `CAT_CHUNKED_X_DISPLAY_BOTS_PER_DISPLAY=25 CAT_CHUNKED_X_DISPLAY_MAX_DISPLAYS=12 ./botpanel/start`
- Disable chunking: `CAT_CHUNKED_X_DISPLAY=0 ./botpanel/start`
- Override hidden TF2 window flags: `CAT_GAME_WINDOW_OPTIONS="-gl -silent -sw -w 800 -h 600" ./botpanel/start`
- Synthetic hardware anti-fingerprinting is enabled by default for headless text-mode bots; disable it with `CAT_STEAM_TXTMODE_SYNTHETIC_HARDWARE=0 ./botpanel/start`.
- Use an existing desktop display instead: `CAT_VISIBLE_WINDOWS=1 ./botpanel/start`
- After game IPC has stayed connected for 10 seconds, the panel freezes the main `steamwebhelper` in that bot's Steam process tree and kills its child helper processes.
- Disable helper cleanup: `CAT_STEAMWEBHELPER_CLEANUP=0 ./botpanel/start`
- Override helper cleanup delay: `CAT_STEAMWEBHELPER_CLEANUP_SECONDS=15 ./botpanel/start`
- Optional ban tracker API key: `CAT_STEAM_WEB_API_KEY=... ./botpanel/start`; without it, the panel falls back to Steam Community profile HTML checks.
- Host Steam content is shared at `/opt/steamapps` as a symlink to the detected host Steam `steamapps` directory, then bot instances symlink their own `steamapps` directory to it. `/opt/catbot-shared-steam` contains copied Steam client/runtime files but its `steamapps` entry points to the existing host library, so TF2 is never copied into the shared client tree. Debian/Ubuntu and Arch Steam layouts are checked.
- The host Steam path is detected automatically. If detection fails, the launcher prints and writes `/tmp/cat-steamapps-detect.log` with every checked path.

`./botpanel/stop` stops the matching headless display unless `CAT_VISIBLE_WINDOWS=1` is set.

`./botpanel/fix_permissions` is disabled and exits without changing ownership or
permissions.

`./botpanel/fix-all` audits the Steam VGUI pin, required system/runtime tools,
Node dependencies, TF2 files, Cat runtime, and paired Steam text-mode
libraries. If it finds problems it offers two choices: repair everything that
can be repaired automatically, or leave it for manual repair. DNS/HTTPS
connectivity is checked and reported but is never treated as script-fixable.
Use `./botpanel/fix-all --check-only` for diagnostics without prompts.

`./botpanel/fix-oldshi` repairs old botpanel path layouts that created partial
Steam directories or recursive `/opt/steamapps` symlinks. Stop the panel first;
the script refuses to touch live bot paths unless `CAT_FIX_OLD_PANEL_FORCE=1` is
set.

`./botpanel/update` updates this single repository, installs dependencies,
builds Cat default/textmode libraries, builds the bundled IPC server, installs
web panel npm dependencies, and refreshes navmeshes in `/opt/cathook/navmeshes`.
When TF2 is installed it also refreshes `/opt/steamapps`; set
`CAT_UPDATE_HOST_NAVMESHES=1` to copy navmeshes directly through the host Steam
path instead.
