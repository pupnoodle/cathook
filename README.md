<p align="center">
  <img src="assets/cathook.png" alt="Cathook">
</p>

<p align="center">
  Cathook is a Team Fortress 2 Linux internal DLC.
</p>

# BETA WARNING

This project is currently in beta. Some features may be broken, unfinished, unstable, or may not work at all. Expect bugs, crashes, missing behavior, and issues.

##

## Risk of Manual Ban

Doesnt matter what cheat you use, get ready for a gameban.

## Overview

Cathook builds into a shared object that is injected into the TF2 process. It reads game state, calls Source Engine interfaces, hooks selected client/runtime functions, and renders or automates features from inside the game.

Useful project details:

* The build uses a unity-style entry point in `src/cathook.cpp`.
* Runtime files live under `/opt/cathook` by default.
* Built libraries are installed to `/opt/cathook/bin`.
* Runtime assets from `assets/` are installed to `/opt/cathook/assets`.
* Runtime configs are stored as `.cat` files under `/opt/cathook/configs` by default, including `default.cat`.
* Logs are written under `/opt/cathook/logs`.

For Source Engine interface and structure references, see Valve's [Source SDK 2013 Multiplayer](https://github.com/ValveSoftware/source-sdk-2013).

## Installing, Building, Attaching

Officially supported Linux distros are Debian, Ubuntu, Linux Mint, Debian-close distros, and Manjaro.
Other distros may work if the dependency installer has a matching package script, but they are not officially supported.

Install build dependencies for your distro:

```sh
./install-deps
```

Install dependencies, build Cat, install runtime files, and prepare the bundled botpanel:

```sh
./setup.sh
```

Build Cat and choose the mode from the first-run terminal menu:

```sh
sudo ./build.sh
```

The saved mode is reused by `build.sh`, `attach.sh`, `preload`, `debug.sh`, and the Docker build wrappers. Remove `~/.config/cathook/mode` or set `CATHOOK_MODE_FILE` to choose a different preference file.

Build an explicit mode:

```sh
sudo ./build.sh --default
sudo ./build.sh --textmode
sudo ./build.sh --both
```

For a local build without installing to `/opt/cathook`, use `./build.sh --no-install`.

For local development, use dev mode to skip repository update checks and avoid reset paths:

```sh
sudo ./build.sh --dev
sudo ./attach.sh --dev
./setup.sh --dev
```

You can also set `CATHOOK_DEV_MODE=1` for `build.sh`, `attach.sh`, `setup.sh`, or `botpanel/update`.
`--no-update` is accepted as an alias for `--dev`.

Attach to a running TF2 process:

```sh
sudo ./attach.sh
```

Use the textmode binary when attaching, overriding the saved mode:

```sh
sudo CATHOOK_MODE=textmode ./attach.sh
sudo TEXTMODE=1 ./attach.sh
```

`attach.sh` refreshes `/opt/cathook/assets` and keeps cathook exceptions in the single `/opt/cathook/logs/exception.log` file. Ctrl+C performs a synchronous cleanup, waits for all cathook callbacks to leave, and only then calls `dlclose`.
It stages the temporary `dlopen` copy through `/proc/<pid>/root`, checks that the target process can read it, and retries alternate runtime directories when Ubuntu/Steam Runtime path isolation hides a staged file.
It also copies any bundled `/opt/cathook/bin/libGLEW.so.*` next to the temporary library.

## Container Build

To build in the repo's Linux container:

```sh
./docker_build.sh
```

On PowerShell hosts:

```powershell
./docker_build.ps1
```

## Preload

Use `./preload` before launching TF2 when the library needs to be loaded before game modules are ready. It uses the saved mode unless `CATHOOK_MODE`, `TEXTMODE`, `CATHOOK_TEXTMODE`, or `CATHOOK_BINARY` overrides it.

## Repository Layout

* `src/cathook.cpp` - unity build entry point
* `src/core/` - shared runtime systems, hooks, logging, config, diagnostics
* `src/features/` - gameplay, automation, visual, movement, combat, and menu features
* `src/games/tf2/sdk/` - TF2-facing entities, interfaces, materials, and SDK types
* `src/external/` - bundled third-party code such as ImGui, MD5, libsigscan, and funchook headers
* `assets/` - runtime fonts, sounds, and textures copied from upstream cathook data
* `botpanel/` - bundled catbot IPC server and web panel
* `packages/` - distro dependency installers used by `install-deps`

## Contributing

Do you want to submit code to Cathook? Open a pull request and keep changes focused.

Currently, pupnoodle is the only maintainer, so reviews and issue responses may take time.

Keep code simple, direct, and consistent with the existing style:

* Use `snake_case`.
* Prefer small focused functions and POD-style data where practical.
* Keep platform-specific behavior isolated.

## Community

Discord: [https://discord.gg/VP8JRguD83](https://discord.gg/VP8JRguD83)

## Credits

This project is based on [TeamFortress2-Linux-Internal](https://github.com/Doctor-Coomer/TeamFortress2-Linux-Internal). <br>
nullworks - original cathook creators<br>
DrCoomer - cheat i based the "cathook" on<br>
GatoPotato658 - Unibox. without him there would be nothing<br>
Salmon - Salmonpaste.
