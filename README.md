# SM64 VR for Steam Frame

This is my Steam Frame fork of [SM64CoopDX](https://github.com/coop-deluxe/sm64coopdx). It runs natively on the headset through SteamLinuxRuntime_4-arm64, SteamVR OpenXR and GLX/Zink; no gaming PC or streaming is needed.

**EXPERIMENTAL.** I made this for personal use on the Valve Steam Frame, and it is still a work in progress. You need your own legally obtained US Super Mario 64 game image; no ROM is included. The repository also contains the source and build tools.

**Want to play on Steam Frame?** Download the prebuilt standalone package from the [v0.1.0 prerelease](https://github.com/astromaddie/sm64-vr/releases/tag/v0.1.0); no compiler is needed. You still need your own legally obtained US Super Mario 64 game image.

## What this fork adds

- Stereoscopic VR with head tracking, a room-scale camera offset and adjustable world size, height and camera position.
- A VR settings page with live camera adjustments and options for turning, seated use, head movement and comfort.
- A stereo cinema presentation for selected scripted cameras and cutscenes, while ordinary gameplay remains in the game's camera view.
- A pause/settings panel and a HUD that stays level as you move your head.
- Steam Frame controller bindings, plus Index and Touch fallback bindings.

The [architecture notes](docs/VR_ARCHITECTURE.md) describe the camera, settings and rendering implementation. The original upstream README is preserved in [README_UPSTREAM.md](README_UPSTREAM.md).

## Build and install

The [Steam Frame build and install guide](docs/STEAM-FRAME-BUILD-INSTALL.md) covers ARM64 Docker builds from Linux, macOS, or Windows through WSL2, the supported game image, package output, first installation and Steam launch. It distinguishes the standalone Linux ARM64 Frame package from Windows or desktop builds.

## Credits and licenses

This port is based on [SM64CoopDX](https://github.com/coop-deluxe/sm64coopdx). Shared Steam Frame VR helpers come from [sfvr](https://github.com/astromaddie/sfvr). The original upstream README remains in the repository. This fork has no root license; check each upstream component's notices before reusing its code. The game image and other commercial game data must come from your own copy.
