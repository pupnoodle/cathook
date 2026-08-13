# Steam text-mode preload

`libcatsteamtxtmode.so` is a paired 32/64-bit `LD_PRELOAD` library for headless
Steam instances. The 32-bit image handles Steam's pinned `steamui.so`; the
64-bit image handles the `steamwebhelper` process. It follows the same lifecycle model as TF2 nographics: it observes
the target UI modules at load time and applies version-gated PLT/GOT hooks to
the pinned `steamui.so` build's public rendering interfaces.

- uses the `steamui.so` X11/SDL3 window interfaces to make its window hidden,
  off-screen, and 1x1;
- patches only the exact host build's GOT slots for those imported SteamUI
  interfaces; the module's executable bytes are never changed;
- skips UI presentation through the Steam build's GLX/EGL/SDL interfaces and
  drops its direct GL draw calls;
- limits Mesa's software renderer to one worker before its UI context starts;
- leaves the root `steamwebhelper` command line unchanged by default;
  optional UI-only flags can be enabled after a host-specific validation.
- does not hook Steam IPC, network, authentication, auth tickets, account
  session state, or matchmaking.

The launcher enables it only for headless text-mode bots. It leaves the
official `steam`, `steamui`, `steamclient`, and Steam networking process
unchanged.

The bundled botpanel defaults to the validated headless profile:
`CAT_STEAM_TXTMODE=1`, hidden 1×1 windows, dropped UI draw/present work, a
100,000 µs present interval, `LP_NUM_THREADS=1`, `GALLIUM_NUM_THREADS=1`, and
`vblank_mode=0`. Set the corresponding `CAT_*` variables to opt out of any
individual behavior.

The pinned `steam.sh` launch boundary uses `steam.sh.patch`: `LD_PRELOAD` is
empty during the shell/runtime bootstrap and `CAT_STEAM_TXTMODE_PRELOAD` is
applied only to the final 32-bit Steam executable. This avoids ELF-class
warnings in 64-bit helper tools without imposing heap or process limits.

## Build

```sh
make -C botpanel/cat-steamtxtmode verify
make -C botpanel/cat-steamtxtmode ARCH=64 verify
```

The 32-bit build needs C++ multilib support (`g++ -m32`).

The standalone supervisor is built alongside the library:

```sh
make -C botpanel/cat-steamtxtmode ARCH=64 standalone
bin/libx64/catsteamtxtmode --config catsteamtxtmode.cfg
```

The repository includes `catsteamtxtmode.cfg`; copy or edit it (or start with
`catsteamtxtmode.cfg.example`) and change its `key=value` options. The
supervisor watches Steam and TF2 process names, logs their arrival and exit,
and can launch configured `steam_command`/`tf2_command` commands with the
matching preload and text-mode environment when `launch_missing=1`.
Commands are executed directly, not through a shell. If no preload path is
specified, it uses the library beside the supervisor executable.

An already-running process cannot be retroactively given `LD_PRELOAD` hooks by
this supervisor. For those processes, start Steam/TF2 through the supervisor
or the normal botpanel launcher; the watcher can still observe them.

## Runtime controls

- `CAT_STEAM_TXTMODE=1` enables interception.
- `CAT_STEAM_TXTMODE_HIDE_WINDOWS=0` preserves normal Steam windows.
- `CAT_STEAM_TXTMODE_DROP_DRAWS=0` retains draw calls but still paces present.
- `CAT_STEAM_TXTMODE_TRIM_WEBHELPER=1` opts into extra root-webhelper UI flags
  after validation. It is disabled by default.
- `CAT_STEAM_TXTMODE_FRAME_INTERVAL_US=100000` sets the per-thread present
  interval; `0` disables pacing.
- `CAT_STEAM_TXTMODE_SYNTHETIC_HARDWARE=1` enables deterministic, per-session
  synthetic Linux machine, adapter, disk, CPU-info, udev, OpenGL, Vulkan, and
  NetworkManager identity values. The catalog contains the original profiles
  plus 7,934 additional distinct records built from 201 named Intel/AMD CPU
  variants and 79 NVIDIA/AMD/Intel adapter variants. It includes a broad
  workstation, server, desktop, laptop, and embedded board catalog, 35 SSD/HDD
  identities, and a deterministic pool of locally administered NIC prefixes.
  The selected profile, GUIDs, disk serials, and per-interface MACs remain
  stable within a session. It is disabled by default and does not rewrite
  Steam auth messages or persisted account state.
- This is an OS/API identity shim. It cannot replace inline `CPUID` instructions
  executed directly by TF2 or Steam; those require VM/container CPU masking.
- This target is the Linux `.so` build. Windows `MachineGuid`, Windows registry
  reads, and Windows kernel/device queries are not intercepted by it; that
  requires a separate Windows DLL implementation.
- `CAT_STEAM_TXTMODE_HARDWARE_SEED=value` overrides the default `CAT_BOT_ID`
  seed. Keep the seed stable for a bot if its synthetic identity must persist.
- `CAT_STEAM_TXTMODE_HARDWARE_RANDOMIZE=1` creates a fresh per-process seed
  when no explicit seed is supplied. The botpanel launcher instead generates
  one session seed and passes it to both Steam and TF2, so they see the same
  synthetic machine rather than contradictory values. Change the seed between
  sessions to select another CPU/GPU/MAC/GUID profile.

Use `CAT_STEAM_TXTMODE=0` to return to the untouched Steam rendering path.
