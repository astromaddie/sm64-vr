# SM64 VR for Steam Frame

This is my Steam Frame fork of [SM64CoopDX](https://github.com/coop-deluxe/sm64coopdx). It runs natively on the headset through SteamLinuxRuntime_4-arm64, SteamVR OpenXR and GLX/Zink; no gaming PC or streaming is needed.

**EXPERIMENTAL.** This is a personal-use port preview and remains a work in progress. It targets the Valve Steam Frame. You need your own legally obtained US Super Mario 64 game image; no game data is included. This repository contains the source and build tools, not a release package.

## What this fork adds

- Stereoscopic VR with head tracking, a room-scale camera offset and adjustable world size, height and camera position.
- A VR settings page with live camera adjustments and options for turning, seated use, head movement and comfort.
- A stereo cinema presentation for selected scripted cameras and cutscenes, while ordinary gameplay remains in the game's camera view.
- A pause/settings panel and a HUD that stays level as you move your head.
- Steam Frame controller bindings, plus Index and Touch fallback bindings.

The [architecture notes](docs/VR_ARCHITECTURE.md) describe the camera, settings and rendering implementation. The original upstream README is preserved in [README_UPSTREAM.md](README_UPSTREAM.md).

## Build and install

Build with Docker using the repository's Linux build image:

```sh
git clone --recurse-submodules --branch main https://github.com/astromaddie/sm64-vr.git
cd sm64-vr
docker build -t sm64-vr-linux -f tools/docker/linux.Dockerfile tools/docker
tools/docker/build-linux.sh on
tools/docker/build-linux.sh package
```

Install the package on your Frame with your own game image:

```sh
tools/steam_frame/deploy.sh --rom /path/to/your-us-image
```

The deploy script checks the image against the pinned US game image and stores it under `~/.local/share/SM64VR/`. Launch the registered title from Steam so SteamVR supplies the controller bindings.

## Credits and licenses

This port is based on [SM64CoopDX](https://github.com/coop-deluxe/sm64coopdx). Shared Steam Frame VR helpers come from [sfvr](https://github.com/astromaddie/sfvr). The original upstream README remains in the repository. This fork has no root license; check each upstream component's notices before reusing its code. The game image and other commercial game data must come from your own copy.
