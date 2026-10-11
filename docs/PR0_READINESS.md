# PR0 — sm64-vr

**EXPERIMENTAL.** This personal fork is public as experimental source, with old OAuth logs removed from its history. See the [README](../README.md) for the port's features, controls, game-data requirements and build instructions.

| Record | Revision |
| --- | --- |
| History-cleaned source | `92a0b450b4b2a14cd0e15133f43f33bd712a1a78` |
| Code tree | `0f54bea629ef4c7e84e02fb03c9487f40718704a` |
| Build/test source | `92a0b450b4b2a14cd0e15133f43f33bd712a1a78` |

Target: Linux ARM64 / SteamLinuxRuntime_4-arm64 / SteamVR OpenXR.

The ROM-free host tests passed on the history-cleaned source: 15 host suites and the integrated cinema renderer check, zero failures. The renderer check reported 8 cases and 8,309 checks. No full game build was run.

The history-cleaned `main` removes `grind.log` and `log.txt` from reachable history. This repository is source-only: no commercial ROM or release package is included, and user-provided game data is required for a full game build. Original upstream and component notices remain in place; they do not grant rights to the commercial game.

[Exact metadata](PR0_METADATA.json) · [Changelog](../CHANGELOG.md)
