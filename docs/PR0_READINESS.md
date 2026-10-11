# PR0 readiness — SM64 VR

Visibility remains private. This is an evidence record for review, not a release authorization.

The immutable PR0 test commit, supported targets and final host-check results are in [PR0_METADATA.json](PR0_METADATA.json). Full application builds and focused wearer acceptance remain pending.

## Source and test identity

- Repository source revision: a1546d1f3a090da4c83505ffde6d27466abdd787.
- PR0 production-source tree for the changed ignore rule and Discord translation unit: 2bff884b9f9e5cde6e8bc3c0e098107a1726ac8e. This tree object was computed from that revision plus those exact working-tree changes.
- Host test source: the same production-source tree above; tests themselves are unchanged from the source revision.
- Initial root checkout was clean at the listed revision. The root is private, so no anonymous fresh root checkout is claimed. Public dependencies used by the Docker build are resolved through the checked-in build recipe.
- The Frame captures summarized in VR_ARCHITECTURE.md identify installed source 7e4f10d8db8a8b3457bfa36b7153633c3ca019d6. They are historical unattended evidence and do not identify a PR0-built package.

## Supported target and dependencies

Target: native Linux AArch64 Steam Frame, launched as a Steam title under SteamLinuxRuntime_4-arm64; SteamVR OpenXR with desktop GL through GLX/Zink. The current documented user-data location is ~/.local/share/SM64VR/.

Copyable build/test commands are in the root README. The Docker image recipe is tools/docker/linux.Dockerfile. PR0 did not run the full VR build, package, deploy, or device launch. No commercial ROM or extracted game data is included.

## Verification by level

| Level | Evidence |
|---|---|
| Host | tools/docker/build-linux.sh tests passed all listed suites at the PR0 source tree. The changed Discord object compiled with VR=1 in the sm64-vr-linux image. This is not a full executable build. |
| Unattended Frame | Earlier source 7e4f10d8db8a8b3457bfa36b7153633c3ca019d6 was packaged and launched from Steam. Synthetic-head settings/cinema captures had zero tracked stereo frames. After capture windows, rate windows returned to 120 fps with zero missed frames; this is not worn performance. |
| Worn | No wearer acceptance is recorded for the integrated settings/cinema preview. There is no complete-level or complete-gameplay result. |

Known failures and omissions include a failed OpenXR startup when another Mario process was already active; a clean-idle retry passed without a source change. Physical grip input, tracked recentering, panel readability, comfort, pause/resume transitions, and the real King Whomp introduction remain unaccepted. The detailed camera/menu status table in VR_ARCHITECTURE.md is authoritative.

## Fresh-checkout and path evidence

The starting root was clean. A full anonymous root checkout is intentionally unavailable because this project is private. The Docker recipe and current host tests ran without copying credentials into the repository. A PR0 full build from a new isolated root checkout was not performed; it remains part of reproducibility review.

## Rights and publication blockers

- The root has no license statement authorizing distribution of commercial game data.
- Coordinator history audit identified OAuth token logging in historical grind.log at cc0ed0e8b74afd50d707862729dff6dc2dfdfcd2 and log.txt at 6c687a5a7335ff59f9f0addfaf2d300e2bf08afb. Historical media provenance also remains open for sound/precomp/samples.zip and mods/sex-in-minecraft/sound/funnyjoel.mp3. Do not publish this history until credential review/history sanitization and media licensing are resolved. Token values are deliberately not repeated here.
- The current Discord callback now logs only successful authorization, without the access token. The historical Git history was not rewritten.
- No public release or visibility change is included in PR0.

## Pending wearer acceptance

After a candidate is built from an exact source revision and the Frame is available for this title, record each separately:

- Install/update the candidate and launch it from Steam.
- Exercise actual Frame controller input, including the physical grip/modifier behavior.
- Verify tracked stereo, head pose, recentering, and both-eye alignment.
- Navigate and read the menus/settings while worn; check comfort and seated behavior.
- Play representative gameplay, pause, resume, save, reload the save, and exit normally.
- Measure sustained frame pacing in a defined real gameplay scene with the headset worn; existing synthetic-head timings do not count.
