# SM64 VR for Steam Frame — experimental technical preview

This private fork adapts SM64CoopDX for native Steam Frame VR. It is an experimental implementation preview, not a complete-gameplay or release claim. The Frame target is native Linux ARM64 through SteamLinuxRuntime_4-arm64, SteamVR OpenXR, and desktop OpenGL through GLX/Zink; it does not stream from a PC.

The upstream project is [SM64CoopDX](https://github.com/coop-deluxe/sm64coopdx). General engine fixes belong upstream. Frame-specific work in this private fork needs owner access for review. The previous upstream README is preserved in [README_UPSTREAM.md](README_UPSTREAM.md). The current private root is not an anonymous checkout or a release channel.

## Capability evidence

| Capability | Host | Frame on desk | Worn |
|---|---|---|---|
| VR loop and source | All listed host suites passed; the changed Discord translation unit compiled. Full current-source executable was not built. | Earlier installed source 7e4f10d captured both eyes and HUD with synthetic head views; tracked stereo frames: 0. | Current settings/cinema integration has no wearer acceptance. |
| Pause and VR settings | Camera, renderer, settings and menu tests passed. | Pause overview, Room page, live scale/camera adjustment, and resume were captured; panel yaw was -20.31 degrees. | Pad navigation, tracked recenter, readability and comfort remain unverified. |
| Cutscene cinema | Projection/aspect and transition tests passed. | A diagnostic reached cutscene 162 / camera mode 11, checked both-eye 16:9 output and returned to stereo. | The real King Whomp introduction is untested. |
| Frame pacing | Rate and timing logic has host tests. | After-capture windows returned to 120.0 fps with zero missed frames; capture itself dipped to about 92–106 fps. | Real worn performance is unmeasured. |
| Gameplay | No complete level or campaign test is claimed. | Heavier scenes and the real boss encounter were not tested. | Physical grip input and full gameplay acceptance remain open. |

Evidence and dates are separated in [docs/VR_ARCHITECTURE.md](docs/VR_ARCHITECTURE.md) and [docs/PR0_READINESS.md](docs/PR0_READINESS.md). Those device captures came from an earlier installed source, not a PR0 package built from the exact source state below.

## Build and test

Requires Docker and the repository's Linux build image. The full Frame build/package path is shown below; PR0 ran the host tests and compiled the changed Discord object, but did not build or package the full game.

~~~sh
docker build -t sm64-vr-linux -f tools/docker/linux.Dockerfile tools/docker
tools/docker/build-linux.sh tests
tools/docker/build-linux.sh on
tools/docker/build-linux.sh package
~~~

Supply a legally obtained US Super Mario 64 game image with `tools/steam_frame/deploy.sh --rom /path/to/your-us-image`. The script validates it against this checkout's pinned US image, installs it as `~/.local/share/SM64VR/baserom.us.z64`, and registers the Steam title by default. No commercial game data is included.

## Limits and rights

The game and its user data remain external to this repository. This fork has no release package and remains private. The source tree has no root license notice establishing rights to commercial game data. Publication is blocked pending review of historical credential-bearing log blobs and the provenance/licensing of historical audio archives; current source no longer logs the OAuth access token, and historical Git objects have not been rewritten.

## PR0 changelog

- Replaced the upstream-facing root README with this private Frame technical preview and preserved the previous README separately.
- Removed OAuth access-token contents from the Discord success log and ignored the two local runtime log names.
- Added exact evidence and acceptance boundaries in docs/PR0_READINESS.md.
