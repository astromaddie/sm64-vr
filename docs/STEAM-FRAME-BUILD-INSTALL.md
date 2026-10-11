# Steam Frame build and install

This guide builds the standalone Steam Frame target: a Linux ARM64 package. The build host can be Linux, macOS, or Windows running a Linux shell under WSL2; it does not need to be ARM64. A Windows or desktop VR executable is a different target and is not the Frame package.

## Build host

The build host needs Bash, Git, and Docker with Buildx. The Docker image supplies the compiler and Python used by its build and test steps. On Windows, use Docker Desktop with WSL2 integration, clone and build inside the Linux distribution's filesystem, and run these commands from Bash. See Microsoft's [install WSL](https://learn.microsoft.com/windows/wsl/install) instructions and Docker's [WSL2 integration](https://docs.docker.com/desktop/features/wsl/use-wsl/) and [multi-platform builds with emulation](https://docs.docker.com/build/building/multi-platform/) guides. `docker buildx inspect --bootstrap` should list `linux/arm64` under supported platforms. ARM64 emulation can make this compiler and game-code build slow.

On an ARM64 Linux Docker host, the default Docker platform is already the target. On x86-64 Linux, macOS, or WSL2, set `DOCKER_PLATFORM=linux/arm64`; the wrapper builds and loads the target image with Buildx, then runs the build inside that ARM64 image.

## Build the Frame package

```sh
git clone --recurse-submodules --branch main https://github.com/astromaddie/sm64-vr.git
cd sm64-vr
export DOCKER_PLATFORM=linux/arm64
tools/docker/build-linux.sh image
JOBS=4 tools/docker/build-linux.sh on
tools/docker/build-linux.sh tests
tools/docker/build-linux.sh package
tools/docker/build-linux.sh abi
```

The source checkout already contains the extracted build assets, so this build does not need a ROM. If Docker and an ARM64-capable Buildx builder are already available in Frame Desktop, run the same steps natively from a writable checkout with `DOCKER_PLATFORM` unset:

```sh
tools/docker/build-linux.sh image
JOBS=4 tools/docker/build-linux.sh on
tools/docker/build-linux.sh tests
tools/docker/build-linux.sh package
tools/docker/build-linux.sh abi
```

This repository does not set up Docker or add packages to SteamOS. Otherwise, build on an external Linux, macOS, or WSL2 host.

On an ARM64 Linux Docker host, omit the `DOCKER_PLATFORM` export. The package is written to `build/package/SM64VR/`; it contains the ARM64 executable, resources, libraries and `LAUNCH-STEAM-FRAME.sh`, but not the ROM. The ABI step checks the staged package.

## First install and game data

For a first install from another computer, follow Valve's [Steam Frame development setup](https://partner.steamgames.com/doc/steamhardware/steamframe/setup) to enable Developer Mode and pair the host. In SteamOS Devkit Client, connect to the Frame by IP, then upload the package directory above. Set **Name** to `SM64VR`, **Local Folder** to the package directory, **Start Command** to `LAUNCH-STEAM-FRAME.sh`, and **Runtime** to **Steam Linux Runtime 3.0 ARM64 (Sniper)**. The [Valve upload guide](https://partner.steamgames.com/doc/steamhardware/steamframe/loadgames) describes those fields. Uploading creates the Steam library entry; launch it there so SteamVR supplies the headset and controller bindings.

Before the first launch, transfer your own supported US `.z64` image to the Frame. The pinned image SHA-1 is `9bef1128717f958171a4afac3ed78ee2bb4e86ce` (from `sm64.us.sha1`). In a Frame Desktop terminal, after placing the file in `~/game-data/`, verify and copy it to the filename used by the deploy helper:

```sh
sha1sum "$HOME/game-data/owned-us-image.z64"
mkdir -p "$HOME/.local/share/SM64VR"
cp "$HOME/game-data/owned-us-image.z64" "$HOME/.local/share/SM64VR/baserom.us.z64"
```

The launcher reads `.z64` files from this writable app-data directory; saves, configuration and `vr.toml` are kept there too.

If the package is already on the Frame, keep the whole package in a writable folder under your home directory, add `LAUNCH-STEAM-FRAME.sh` through Steam's **Add a Non-Steam Game** flow, and start it from the Steam library. Copy the ROM into the app-data path first. Start it from Steam so SteamVR supplies OpenXR and controller bindings.
