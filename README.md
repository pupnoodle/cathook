<p align="center">
  <img src="assets/cathook.png" alt="Cathook">
</p>

<p align="center">
  Cathook is a Team Fortress 2 Linux internal DLC.
</p>

## Risk of Manual Ban

Doesnt matter what cheat you use, get ready for a gameban.

## Overview

Cathook builds into a shared object that is injected into the TF2 process. It reads game state, calls Source Engine interfaces, hooks selected client/runtime functions, and renders or automates features from inside the game.

Useful project details:

* The build uses a unity-style entry point in `src/cathook.cpp`.
* Runtime files live under `/opt/cathook` by default.
* Built libraries are installed to `/opt/cathook/bin`.
* Runtime assets from `assets/` are installed to `/opt/cathook/assets`.
* Logs are written under `/opt/cathook/logs`.

For Source Engine interface and structure references, see Valve's [Source SDK 2013 Multiplayer](https://github.com/ValveSoftware/source-sdk-2013).

## Installing, Building, Attaching

Install build dependencies for your distro:

```sh
./install-deps
```

Install dependencies, build Cat, install runtime files, and prepare the bundled botpanel:

```sh
./setup.sh
```

Build the normal NON-TEXTMODE library with SDL hooking:

```sh
sudo ./build.sh
```

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
sudo ./inject.sh --dev
./setup.sh --dev
```

You can also set `CATHOOK_DEV_MODE=1` for `build.sh`, `inject.sh`, `setup.sh`, or `botpanel/update`.
`--no-update` is accepted as an alias for `--dev`.

Attach to a running TF2 process:

```sh
sudo ./inject.sh
```

Use the textmode binary when attaching:

```sh
sudo TEXTMODE=1 ./inject.sh
```

`inject.sh` refreshes `/opt/cathook/assets` and clears old cathook/exception logs before loading the library.

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

Use `./preload` before launching TF2 when the library needs to be loaded before game modules are ready. Set `TEXTMODE=1` or `CATHOOK_TEXTMODE=1` to use `libcathooktextmode.so`.

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

Discord: https://discord.gg/pQmvJPUwdJ

## Credits

This project is based on [TeamFortress2-Linux-Internal](https://github.com/Doctor-Coomer/TeamFortress2-Linux-Internal). <br>
AI - that's how i learned how to code<br>
RH - motivation and some methods<br>
nullworks - original cathook creators<br>
DrCoomer - cheat i based the "cathook" on<br>
