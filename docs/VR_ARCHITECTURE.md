# SM64 VR (Steam Frame) architecture

Current state of the VR layer in this fork of sm64coopdx. The code is in `src/pc/vr/`, all of it behind
the `VR=1` build flag (`SM64_VR`); with the flag off the flat build is unchanged. The OpenXR host is
`vr_xr.c`, the frame loop and setup are `vr.c`, the per-eye replay of the game's display list is
`vr_render.c`. The settings registry is the vendored `third_party/sfvr` plus the port keys in
`vr_settings.c`.

Nothing here has been worn since the display changes of 2026-10-05. Every row of the status table is
headset-unverified.

## 1. Target

Native Linux ARM64 title for the Steam Frame (SteamVR OpenXR, desktop GL through GLX/Zink), launched
from Steam as the devkit title `SM64VR`. The user supplies the US ROM in `~/.local/share/SM64VR/`.

## 2. Build and deploy

```
docker build -t sm64-vr-linux -f tools/docker/linux.Dockerfile tools/docker   # once
tools/docker/build-linux.sh on          # make VR=1 into build/us_pc_vr
tools/docker/build-linux.sh package     # build/package/SM64VR
tools/docker/build-linux.sh abi
FRAME_HOST=steamos@<frame> tools/steam_frame/deploy.sh [--config KEY=VALUE ...]
```

Book the Frame (`frame-lock.sh`) before deploying or launching. Launch through Steam, never over SSH:
a binary started from SSH sees no controllers, and the per-app refresh rate only applies to Steam
launches.

## 3. Display (standard.md section 9)

- **The app can't change the rate on the Frame.** SteamVR offers an app one rate: the title's per-app
  preferred rate (`steam.app.<id>` in `steamvr.vrsettings`), applied to Steam-title launches only. The user
  sets it once, in SteamVR's per-app settings in the headset. **Recommended for this title: 120 Hz.** The
  unattended measurement below suggests it fits (GPU 3.3 ms, CPU 2.7 ms against 8.33 ms), but 120 has not
  been run, so this is a recommendation, not a measurement.
- **Request.** `refresh_rate` (default 120) is the requested rate where a runtime honours it. At the READY
  state the port enumerates the offered rates and, once, requests the offered rate nearest the setting with
  `xrRequestDisplayRefreshRateFB`, unless the display already runs at it (SteamVR answers that with an
  error). There is no retry and no fallback ladder, because the request can't take effect. The log has the
  offered rates, the rate the display runs at, and the runtime's change event if one comes.
- **Is the rate held?** `vr_rate.c` counts shown frames in 10 s windows while the headset is worn (session
  FOCUSED and `shouldRender`) against the display's rate, taken from the runtime's frame period (not from
  the extension's list). After two windows in a row below 90%, it logs once per run that the frame rate is
  not held and which per-app rate to set instead (the next one down, 90 or 72; at 72, lower
  `resolution_scale`). Standby, taking the headset off and a rate change start the count over.
  `diag_rate_unworn = true` counts windows without a wearer, for unattended runs. Each window otherwise
  logs `refresh rate: N fps over 10 s at R Hz`.
- **Resolution.** Each eye has its own swapchain, created at the runtime's recommended size times
  `resolution_scale` and rendered into directly: the replay binds the swapchain image as its framebuffer.
  There is no larger intermediate target and no blit. The Frame recommends 1728 x 1728, so the default
  `resolution_scale = 1.25` gives 2160 x 2160, the panel's own resolution. The log has one `eye N
  swapchain WxH` line per eye with the recommended and maximum sizes. No MSAA, no foveation.
- **Settings and migration.** Both keys are in `vr.toml` (`SM64_VR_REFRESH_RATE`,
  `SM64_VR_RESOLUTION_SCALE` override them for a run). Schema version 2 changed the defaults from 90 Hz
  and 1.0. A version-1 file that still says `refresh_rate = 90` or `resolution_scale = 1.0` (the old
  defaults, written into every file) moves to 120 and 1.25 when it is read; any other value, such as 72
  or 1.5, is kept. Version-2 files are never touched.
- **Measurement.** See section 7.

## 4. World scale

Mario is 161 units tall; `world_scale = 104` units per metre makes him 1.55 m, life size. The star
select screen has its own scale (`star_select_scale`, 300). `level_pitch` drops the game camera's pitch so
the head supplies it.

## 5. Frame protocol

`xrWaitFrame` paces the loop. The game ticks at its fixed 30 Hz; `vr_sched.c` places each XR frame on that
timeline by `predictedDisplayTime` and the render interpolates between ticks, so the simulation rate is
the same at any refresh rate. Gameplay renders in stereo (one replay of the display list per eye, 2D
into the HUD quad); menus use a world-locked flat panel over black eye images. Scripted cutscenes,
including the new-game Peach/Lakitu intro, use a stereo cinema projection screen (section 10). A fade
quad covers mode changes and warp transitions. Standby (`shouldRender` false) freezes the simulation
and keeps polling.

## 6. Settings and diagnostics

`vr.toml` in `~/.local/share/SM64VR/` (keys from the sfvr registry plus `world_scale`, `level_pitch`,
`star_select_scale`, `force_mono`, `fov_mode`, `panel_distance`, `panel_width`, `cutscenes`,
`cinema_distance`, `cinema_width` and the diagnostics).
Environment variables `SM64_VR_<KEY>` override a key for one run. Diagnostics: `force_render`
(draw and run in standby with a synthetic head), `diag_yaw`, `dump_frame`, `dump_worn_frame`,
`probe_seconds`, `diag_menu_stereo`, `diag_rate_unworn`, `diag_cinema_seconds`, and `timing` for the `[vr-perf]` line every 10 s
(`fps`, `missed`, `cpu`, `tick`, `replay`, `hud`, `eye`, `gpu`). The stages are CPU submission times;
`gpu` is the GPU time of the eye and HUD passes from timestamp queries read a few frames late. While
the rate check runs it logs `refresh rate: N fps over 10 s at R Hz (frame period ...)` each window.

## 7. Measurements

Unattended, headset on the desk (`force_render`), launched through Steam as the devkit title,
2026-10-05, Castle Grounds (start of the first level, Mario idle), 2160 x 2160 per eye, no MSAA:

| Item | Result |
| --- | --- |
| Offered by `xrEnumerateDisplayRefreshRatesFB` | 90 Hz only (SteamVR offers an app its per-app rate; this title's `preferredRefreshRate` in `steamvr.vrsettings` is 90) |
| Granted | 90 Hz: frame period 11.11 ms (the per-app rate; the request for 120 had nothing to act on) |
| Eye swapchains | 2160 x 2160 each (recommended 1728 x 1728 x 1.25, maximum 8192) |
| Frame rate | 90.0 fps, `missed=0`, in every window of two runs (about 20 windows of 10 s) |
| CPU (`[vr-perf]`) | 2.6 to 2.8 ms: tick 0.12, replay 2.1 (eyes 0.8 and 1.25), hud 0.5 |
| GPU (`[vr-perf]`) | 3.3 ms for both eyes and the HUD at 90 Hz |

3.3 ms GPU and 2.7 ms CPU against an 8.33 ms period at 120 Hz leave room, but 120 Hz itself has not been
measured: the display stayed at 90 Hz because the per-app setting is 90 and the app can't change it. The
live "not held" report has not been seen on the device, since the rate was held; the window logic is
unit-tested (`tests/vr/test_vr_rate.c`, `test_vr_xr.c`). Heavier scenes (Bob-omb Battlefield) were not run: the unattended run has no input.

## 8. Wear checklist (at most 15 minutes)

0. In SteamVR's per-app settings, **set the SM64 title to 120 Hz** (it is 90 now), then launch it from Steam.
   Check the log (`deploy.sh --logs`) for `frame period 8.33 ms` and `refresh rate: ... at 120 Hz`.
1. Menus on the flat panel: file select, pause, the course and act select (stereo).
2. Castle Grounds start to the first painting: sharpness of the world and the HUD at 2160 per eye.
3. Enter a course (Bob-omb Battlefield): the warp fade, then play for a few minutes, looking at frame
   smoothness. Note any `refresh rate: ... not held` line in the log.
4. Snap and smooth turning, recentre (hold L View), haptics.
5. Say whether 120 felt held; if not, which scene dropped.
6. New Game: read the Peach letter below the stereo picture, then watch Lakitu's tour. Turn/tilt the
   head and recentre: the screen stays world-locked and level; camera cuts cause no extra fades.
   Check an NPC/sign dialogue, Bowser intro/key reward, a star dance, cap switch and credits when reachable.
7. Check file select and a closed warp: black surroundings, no white runtime room. Confirm door/pipe
   warps stay in gameplay stereo and act select still uses its diorama scale.
8. Repeat a cutscene with `cutscenes = 1` to check the immersive option and transitions back to gameplay.

## 9. Status table

| Feature | State |
| --- | --- |
| Single refresh-rate request for an offered rate, offered rates and change event logged | headset-unverified |
| "Rate not held" report after two low windows (window logic unit-tested; the counting runs on the device at 90 Hz, the report not seen) | headset-unverified |
| Title runs at 120 Hz with the per-app rate set to 120 | not started (needs the per-app setting, then a run) |
| `[vr-perf]` GPU time from timestamp queries | headset-unverified (read unattended) |
| 2160 x 2160 eye swapchains, rendered directly | headset-unverified |
| `refresh_rate` / `resolution_scale` settings and v1 to v2 migration (unit-tested) | headset-unverified |
| Stereo gameplay, display-time scheduler, level pitch | headset-unverified |
| Star select in stereo, warp transitions with a fade quad | headset-unverified |
| fov_mode 3 (the Frame's measured full fov) | headset-unverified |
| Frame controller input, snap or smooth turning, haptics | headset-unverified |
| Menus and HUD on the flat panel and HUD quad | headset-unverified |
| Cinema screen corners, positive parallax, display-time latch, stage fades, cinema projection-layer structs and settings | pure test; headset-unverified |
| Cinema eye replay, opaque text band below the picture, no-roll entry/recentre anchor | host render with synthetic retained list; real game/device/worn unverified |
| Mono panels and closed warps always have black projection beneath quads | pure test + host render with synthetic retained list; device/worn unverified |
| Cinema lane Linux ARM64 VR build, staged package and glibc ABI floor | built + packaged; ABI checked; device/worn unverified |
| VR-disabled flat build and source identity of modified shared translation units | linked AArch64 executable; VR-off tokens identical to lane base; flat game not played |

## 10. Scripted cameras and the stereo cinema

`vr_cinema.c/.h` owns the pure classification, display-time latch and screen/camera maths.
`geo_camera_main` publishes read-only cutscene id, camera mode, shot, shot-local timer and FOV alongside
the tick's camera. Compile-time assertions match the policy's ids to `camera.h`; neither the timer nor
the shot number gates cinema, because each camera cut can reset them.

The latch qualifies entry for 50 ms (covered with compositor black) and requires 400 ms without a
cinema request before exit. A closed warp freezes it and preserves the presentation mode. Ordinary
camera cuts, shot changes and one-tick flag gaps keep the screen anchor and do not restart fades.
Actual flat/stereo/cinema changes use `vr_stage`'s 0.25 s black hold and 0.35 s fade; the stronger of
that fade and the game's warp fade wins. Real menus clear the latch; act select keeps its stereo diorama.

The 16:9 picture is placed on cinema entry from head position and **head yaw only**, 3 m away and
3.6 m wide by default. Recentre places it again. The virtual game eyes use the scripted game camera's
full view, independent of head heading/translation, `level_pitch`, `world_scale` and comfort turn orbit.
Their parallel baseline is derived from runtime IPD; their off-axis frusta converge at the game's near
clip plane. Visible content therefore has zero or positive parallax, with infinity disparity limited
to 60% of IPD on the screen. Near/far depth mapping is kept, including fog. A small optional live sky
projection pointer in `GfxVrPass` keeps sky and world FOVs matched to the interpolated game projection.
The camera-space eye shift
is applied analytically, avoiding cancellation roundoff from large world coordinates.

Both eye images go into **one OpenXR projection layer**, never per-eye quad layers. Each submitted
view has the screen's orientation, that runtime eye's position, and `atan2` FOVs through the screen
corners. The opaque environment outside it is black. Mono/menu and closed-warp paths acquire and clear
both eyes to black before submitting their panel/fade quads. Acquisition failure submits no layers,
rather than quads over SteamVR's default environment. No dim quad is used beneath panels.

The complete authored HUD/dialogue/letter/credits image is resolved over black into an opaque band
below the picture, 55% of its width, preserving its 16:9 aspect and all text without cropping. Its top
is 2 cm below the picture and it shares the same yaw-only orientation and depth. Cutscene text stays
visible when gameplay `hud_visible` is off. No new art or replacement text is added. This conservative
whole-HUD placement needs a wear check for seated legibility; it also carries counters/overlays the game
chooses to emit during a cutscene.

| Setting | Default | Range / meaning |
| --- | --- | --- |
| `cutscenes` | 0 | 0 = `cinema_3d`, 1 = immersive gameplay rig |
| `cinema_distance` | 3.0 m | 1.0–10.0 m |
| `cinema_width` | 3.6 m | 0.5–10.0 m; picture height = width × 9/16 |
| `diag_cinema_seconds` | 0 | 0–120 s; 0 disables; starts at the first usable gameplay camera, including the main-menu background |

For unattended integration checks use `force_render = true`, `diag_cinema_seconds = 10`,
`dump_frame = 120`, and `timing = true`. The forced route still respects `force_mono`, never changes
simulation or loads a level, and leaves act select alone. The ordinary New Game intro exercises real
classification. `vr-state.txt` includes `mode cinema`, latch/request details, cutscene/mode/shot/timer,
screen pose/size and both submitted eye poses/FOVs, alongside the virtual game eyes. Cinema reports
virtual eye spacing in game units and runtime IPD in metres; it never converts that spacing through
gameplay `world_scale`. `[vr-perf]` names
cinema separately from mono and stereo. Eye dumps are the square storage images; resize them to 16:9
to inspect the picture as seen on the screen.

### Cutscene policy

These are all ids defined in `camera.h` (including unused ids). Cinema rows use immersive stereo
instead when `cutscenes = 1`; stereo rows keep the existing gameplay rig in either setting.

| Id | `CUTSCENE_*` | Route | Why |
| --- | --- | --- | --- |
| 130 | `DOOR_PULL` | stereo | short local door action |
| 131 | `DOOR_PUSH` | stereo | short local door action |
| 133 | `ENTER_CANNON` | cinema | scripted swoop into the cannon |
| 134 | `ENTER_PAINTING` | stereo | short warp covered by the warp fade |
| 135 | `DEATH_EXIT` | stereo | short respawn covered by the warp fade |
| 139 | `DOOR_WARP` | stereo | short door warp covered by the warp fade |
| 140 | `DOOR_PULL_MODE` | stereo | short local door action |
| 141 | `DOOR_PUSH_MODE` | stereo | short local door action |
| 142 | `INTRO_PEACH` | cinema | Peach letter and Lakitu camera tour |
| 143 | `DANCE_ROTATE` | cinema | reward camera orbits Mario |
| 144 | `ENTER_BOWSER_ARENA` | cinema | boss introduction and dialogue |
| 145 | `0F_UNUSED` | cinema | unused scripted camera; safe cinema fallback |
| 147 | `UNUSED_EXIT` | cinema | unused scripted camera; safe cinema fallback |
| 149 | `SLIDING_DOORS_OPEN` | stereo | short local door action |
| 150 | `PREPARE_CANNON` | cinema | remote cannon reveal |
| 151 | `UNLOCK_KEY_DOOR` | stereo | short local key-door action |
| 152 | `STANDING_DEATH` | stereo | local death feedback |
| 153 | `DEATH_ON_STOMACH` | stereo | local death feedback |
| 154 | `DEATH_ON_BACK` | stereo | local death feedback |
| 155 | `QUICKSAND_DEATH` | stereo | local death feedback |
| 156 | `SUFFOCATION_DEATH` | stereo | local death feedback |
| 157 | `EXIT_BOWSER_SUCC` | stereo | short exit warp; key dance is separate |
| 158 | `EXIT_BOWSER_DEATH` | stereo | unused short exit warp |
| 159 | `WATER_DEATH` | stereo | local death; absent from play_cutscene switch |
| 160 | `EXIT_PAINTING_SUCC` | stereo | short exit warp; star dance is separate |
| 161 | `CAP_SWITCH_PRESS` | cinema | scripted switch reveal and dialogue |
| 162 | `DIALOG` | cinema | scripted focus on a speaking object |
| 163 | `RACE_DIALOG` | cinema | scripted race dialogue focus |
| 164 | `ENTER_PYRAMID_TOP` | cinema | scripted pyramid entrance camera |
| 165 | `DANCE_FLY_AWAY` | cinema | reward camera flies away |
| 166 | `DANCE_CLOSEUP` | cinema | reward camera close-up |
| 167 | `KEY_DANCE` | cinema | Bowser key-grab reward camera |
| 168 | `SSL_PYRAMID_EXPLODE` | cinema | unused remote pyramid reveal |
| 169 | `EXIT_SPECIAL_SUCC` | stereo | short exit warp |
| 170 | `NONPAINTING_DEATH` | stereo | short respawn warp |
| 171 | `READ_MESSAGE` | cinema | scripted sign or message focus |
| 172 | `ENDING` | cinema | scripted ending camera sequence |
| 173 | `STAR_SPAWN` | cinema | remote star reveal |
| 174 | `GRAND_STAR` | cinema | final Bowser defeat and flight |
| 175 | `DANCE_DEFAULT` | cinema | reward camera orbits Mario |
| 176 | `RED_COIN_STAR_SPAWN` | cinema | remote star reveal |
| 177 | `END_WAVING` | cinema | scripted ending tableau |
| 178 | `CREDITS` | cinema | scripted credits camera tours |
| 179 | `EXIT_WATERFALL` | stereo | short warp landing feedback |
| 180 | `EXIT_FALL_WMOTR` | stereo | short warp landing feedback |
| 181 | `ENTER_POOL` | stereo | short local pool warp |
| 182 | `PALETTE_EDITOR` | cinema | scripted character preview camera |

Id 0 means normal gameplay stereo; unknown nonzero ids default to cinema for mod-authored scripts.
There is no separate pipe camera cutscene id: pipe/painting/door warp actions remain stereo under the
existing warp fade/hold. File select is flat; the new-game level intro becomes id 142 and enters cinema.
Bowser speaking/defeat dialogue uses ids 144/162, his key reward 167, and final defeat/flight 174.

`set_camera_mode` alone does not imply a cutscene. All current modes stay stereo unless a cinema id
is active: NONE (0), RADIAL (1), OUTWARD_RADIAL (2), BEHIND_MARIO (3), CLOSE (4), C_UP (6),
WATER_SURFACE (8), SLIDE_HOOT (9), INSIDE_CANNON (10), BOSS_FIGHT (11), PARALLEL_TRACKING (12),
FIXED (13), 8_DIRECTIONS (14), FREE_ROAM (16), SPIRAL_STAIRS (17), NEWCAM (18), ROM_HACK (19).
These are player-controlled aiming/look modes or course-following camera rules, including fixed room
cameras; treating them as cutscenes would turn ordinary play into repeated cinema transitions. Cannon
entry id 133 is cinematic, then INSIDE_CANNON returns to stereo for interactive aiming.

### Verification

Run the bounded host suite inside the arm64 image:

```sh
docker run --rm -v "$PWD":/src -w /src sm64-vr-linux bash tests/vr/run_cinema.sh
```

The cinema test exercises the production helpers and real compositor/XR layer constructors: independent
screen-corner projection round trips at yaw 0/±90/180 with translated/tilted eyes; rejection behind the
screen; positive parallax from the near plane to the far plane with an asymmetric source frustum and
non-origin camera; refresh-independent 72/90/120 Hz hysteresis; warp holds; all mode fade timings;
anchor persistence/recentre; settings save/load/clamp; and an intro-classified frame whose projection
views match the screen and each runtime eye. Existing rig/maths/composition/settings checks run too.
The pure suite has 945 checks. An additional Mesa 4.5 offscreen host check calls the actual
`vr_render_frame` with a synthetic retained-list producer, then reads its GL eye/HUD targets outside
the frame: 110 additional checks cover distinct stereo eye images, opaque black text-band margins,
matching interpolated sky/world FOV, cinema holds/gaps, black mono/warp eye pixels, act-select routing
(including a conflicting gameplay flag and stale cutscene id),
the immersive setting, diagnostic activation/expiry and acquisition failure. It is a **host renderer
check, not a real SM64 scene, device run or worn acceptance**. Strict host compilation exposed a
pre-existing dump-path truncation warning; the dump slot now has room for the directory and filename.
The checkout lacks the
`tests/vr/Makefile` and `tools/verify_flat_build_unchanged.sh` referred to by the generic Docker wrapper;
use the bounded runner above and the recorded direct flat build check for this lane.

The direct flat build uses the same image and default build options:

```sh
docker run --rm -v "$PWD":/src -w /src sm64-vr-linux \
  bash -c 'make VR=0 -j4 && test -x build/us_pc/sm64coopdx.arm'
```

This ARM image's regular flat target has the `.arm` suffix; the VR target is unsuffixed.

The 2026-10-06 lane validation also passed `tools/docker/build-linux.sh on`, `package` and `abi`
inside `sm64-vr-linux`: the executable is ELF AArch64 and its maximum required glibc is 2.34,
below the 2.35 release floor (GLIBCXX 3.4.29). The staged package is
`build/package/SM64VR`; all 416 file hashes in `BUILD-METADATA.json` were checked against the files,
and its executable matched the built executable. The package records source revision
`44af5ee1c3fbe4e6f99a8c2a70c4524290e04912` (clean), with executable SHA256
`cc0f9f4bf3b3bba7ce55ae2ff038f6a79fd7691c6b2e823c489820293a2927c3`, by
mounting the repository's common Git directory read-only; this changes no repository or build scripts.

The build reused only three large unchanged data objects (`custom_font`, `custom_textures`,
`sound_data`) from the existing main-clone cache after hashing all of their listed dependencies:
76, 82 and 8 matching entries respectively. Their complete VR-on and VR-off preprocessed tokens
were then compared byte for byte before using them for the flat link. All other flat objects are
compiled afresh. Independently, both modified non-VR translation units (`camera.c` and `gfx_pc.c`)
have identical VR-off preprocessed tokens to the lane base `6e663516e`, with source filenames
normalised. These checks establish flag-off source identity; no flat game was played.
The full `make VR=0 -j4` link also completed successfully: `build/us_pc/sm64coopdx.arm` is ELF
AArch64, contains no `vr_` or `sfvr_` function symbols, and has SHA256
`10a0447ddc8f533a7d4ef49f32fe13cffb154741d3159771376fa01bd1f21828`.
