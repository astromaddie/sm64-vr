# PR0 — sm64-vr

**EXPERIMENTAL.** Kept private while the items below are resolved. See the [README](../README.md) for the port's features, controls, game-data requirements and build instructions.

| Record | Revision |
| --- | --- |
| Hygiene audit | `47df66e35a3ac97d68069e10007025d2119ba6a0` |
| Checkout when this record was updated | `47df66e35a3ac97d68069e10007025d2119ba6a0` |
| Build/test source | `21dae7702c486aff4002ec92eb1f4568483e2539` |

Target: Linux ARM64 / SteamLinuxRuntime_4-arm64 / SteamVR OpenXR.

Host tests passed: 15 suites, including 8,309 cinema checks, zero failures. Discord translation unit compiled in the pinned linux/arm64 Docker image. Full game build was not run. No new full application build is claimed for this audit.

The publication blockers are:

- reachable history contains OAuth token logging in grind.log and log.txt; active status is unverified. sound/precomp/samples.zip and third-party mod audio also need provenance resolution. These items require a rights-cleared history route and credential-owner review.

No commercial ROM is included; user-provided game data is required for a full game build. Keep the original upstream and component notices; they do not grant rights to the commercial game.

[Exact metadata](PR0_METADATA.json) · [Changelog](../CHANGELOG.md)
