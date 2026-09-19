<p align="center">
  <img src="assets/puphook.png" alt="Puphook">
</p>

<p align="center">
  Puphook is a Team Fortress 2 Linux internal DLC.
</p>

# BETA WARNING

This project is currently in beta. Some features may be broken, unfinished, unstable, or may not work at all. Expect bugs, crashes, missing behavior, and issues.
You need to downgrade steam before trying to host bots. ( /scripts )

# SUPPORT DISCORD SERVER

Discord: [https://discord.gg/VP8JRguD83](https://discord.gg/VP8JRguD83).

##

## Risk of Manual Ban

Doesnt matter what cheat you use, get ready for a gameban.

## Overview

Puphook builds into a shared object that is injected into the TF2 process. It reads game state, calls Source Engine interfaces, hooks selected client/runtime functions, and renders or automates features from inside the game.

Useful project details:

* The build uses a unity-style entry point in `src/puphook.cpp`.
* Runtime files live under `/opt/puphook` by default.
* Built libraries are installed to `/opt/puphook/bin`.
* Runtime assets from `assets/` are installed to `/opt/puphook/assets`.
* Runtime configs are stored as `.pup` files under `/opt/puphook/configs` by default, including `default.pup`. Legacy `.cat` files still load.
* Logs are written under `/opt/puphook/logs`.

For Source Engine interface and structure references, see Valve's [Source SDK 2013 Multiplayer](https://github.com/ValveSoftware/source-sdk-2013).

## Installing, Building, Attaching

Officially supported Linux distros are Debian, Ubuntu, Linux Mint, Debian-close distros, and Manjaro.
Other distros may work if the dependency installer has a matching package script, but they are not officially supported.

Install build dependencies for your distro:

```sh
./install-deps
```

Install dependencies, build Pup, install runtime files, and prepare the bundled botpanel:

```sh
./setup.sh
```

Build Pup and choose the mode from the first-run terminal menu:

```sh
sudo ./build.sh
```

The saved mode is reused by `build.sh`, `attach.sh`, `preload`, `debug.sh`, and the Docker build wrappers. Remove `~/.config/puphook/mode` or set `PUPHOOK_MODE_FILE` to choose a different preference file.

Build an explicit mode:

```sh
sudo ./build.sh --default
sudo ./build.sh --textmode
sudo ./build.sh --both
```

For a local build without installing to `/opt/puphook`, use `./build.sh --no-install`.

For local development, use dev mode to skip repository update checks and avoid reset paths:

```sh
sudo ./build.sh --dev
sudo ./attach.sh --dev
./setup.sh --dev
```

You can also set `PUPHOOK_DEV_MODE=1` for `build.sh`, `attach.sh`, `setup.sh`, or `botpanel/update`.
`--no-update` is accepted as an alias for `--dev`.

Attach to a running TF2 process:

```sh
sudo ./attach.sh
```

Use the textmode binary when attaching, overriding the saved mode:

```sh
sudo PUPHOOK_MODE=textmode ./attach.sh
sudo TEXTMODE=1 ./attach.sh
```

`attach.sh` refreshes `/opt/puphook/assets` and keeps puphook exceptions in the single `/opt/puphook/logs/exception.log` file. Ctrl+C performs a synchronous cleanup, waits for all puphook callbacks to leave, and only then calls `dlclose`.
It stages the temporary `dlopen` copy through `/proc/<pid>/root`, checks that the target process can read it, and retries alternate runtime directories when Ubuntu/Steam Runtime path isolation hides a staged file.
It also copies any bundled `/opt/puphook/bin/libGLEW.so.*` next to the temporary library.

## Container Build

To build in the repo's Linux container:

```sh
./docker_build.sh
```

On PowerShell hosts:

```powershell
./docker_build.ps1
```

## Contributing

Do you want to submit code to Puphook? Open a pull request and keep changes focused.

Currently, pupnoodle is the only maintainer, so reviews and issue responses may take time.

Keep code simple, direct, and consistent with the existing style:

* Use `snake_case`.
* Prefer small focused functions and POD-style data where practical.
* Keep platform-specific behavior isolated.

## Credits

This project is based on [TeamFortress2-Linux-Internal](https://github.com/Doctor-Coomer/TeamFortress2-Linux-Internal). <br>
nullworks - original puphook creators<br>
DrCoomer - cheat i based the "puphook" on<br>
GatoPotato658 - Unibox. without him there would be nothing<br>
Salmon - Salmonpaste.
