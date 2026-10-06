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

## 4. Camera, scale and comfort

The eyes ride on the game camera (Lakitu): its interpolated view is the anchor, with its heading, its
pitch unless `level_pitch` (default on) drops it, and never its roll; the head pose goes on top. All of
this section's code is `src/pc/vr/vr_cam.c` (rig maths), `vr_turn.c` (turning, the stick's frame, the
vignette ramp; both pure and host-tested), the comfort part of `vr_render.c`, and the hooks in
`src/game/camera.c` and `mario.c`.

**Scale and height.** Mario is 161 units tall; `world_scale = 104` units per metre makes him 1.55 m, life
size. The range is 20 (Mario 8 m) to 400 (Mario 0.4 m, a diorama). The star select screen has its own
scale (`star_select_scale`, 300). `camera_closer` (0 to 0.8) moves the head origin along the line from the
game camera to its look-at point and `camera_lower` (0 to 1) then moves the height that is left toward the
look-at point's (after DK64: for camera E and look-at A, P.xz = E.xz + C (A.xz - E.xz),
P.y = A.y + (1 - C)(1 - L)(E.y - A.y)); lowering turns the inherited pitch toward A, the heading stays.
They apply in gameplay only, not on the star select. There is no collision test for the moved origin, so
large values can put the eye inside walls or Mario. `height_offset` raises the eyes by that many metres
(times `world_scale`). Eye offsets (the IPD) come only from the runtime's eye poses and are scaled by
`world_scale` alone; `head_translation` scales only the head's displacement from the recentre point;
orbit distances and pivots stay in game units. Every key here is read every frame, so a change applies
at once.

**Recentre and height (standard.md section 1).** View held 1 s recentres: the head's yaw becomes the view's
forward and its horizontal position the origin, with a 0.6/80 ms buzz on both hands. The standing height is
kept, so ducking lowers the view (by `head_translation`). View held 3 s recentres again and takes the
standing height from the head, with a second buzz. The height is also taken at the first stereo frame,
when the headset is put on again (focus gained), on the runtime's own recentre, and when tracked views take
over from `force_render`'s synthetic ones. A switch between the panel and stereo recentres the yaw and
keeps the height. In this third-person view the calibrated head height sits at the game camera, so a
seated wearer already sees the standing view; `seated` makes every recentre (the 1 s hold and each panel
switch too) take the height again, so a seated wearer who slumps or shifts is put back at the camera
by the ordinary recentre. Menu is the game's Start; a short View press is the gamepad Back (the player
list in online play; SM64 itself has no Select).

**Turning (`turn_mode`, `snap_angle`, `smooth_turn_speed`).** Right stick X, through the sfvr turn helper:
a 30 degree snap at 70% deflection that re-arms below 30% (15 to 90 degrees), or smooth turning at up to
120 degrees per second past a 20% deadzone. SM64's camera eases to a new yaw (Lakitu flies a chord to his
goal), so a written yaw alone would read as a slow swing (lesson 94). The turn is handed to the game
camera instead, in one step:

1. The renderer adds the stick's turn to `gVrCamTurn.queued` and shows it the same frame: the anchor is the
   game camera orbited about Mario (the interpolated `gVrInterpPivot`) by what is queued.
2. The next camera update (`vr_camera_take_turn` in `update_camera`) takes the queue and turns the whole
   camera rig about Mario by it: Lakitu's current and goal position and focus, the camera's yaws, the
   mode's own yaw (`sModeOffsetYaw` for the radial cameras, `s8DirModeYawOffset` for the 8-direction
   one) and the camera node's previous position (`update_graph_node_camera`), so the frames between the
   ticks interpolate the turned camera. Nothing eases, and the sound and culling follow the turned camera.
3. The queue is empty again in the frame that shows the turned camera, so the orbit and the game's own turn
   never add up: the shown heading moves once.

The game camera's own turns (C buttons, walls, cuts, the radial camera following Mario) are not
compensated; they show as the game makes them, as before. Which camera takes a turn
(`vr_turn_kind`): the Lakitu and Mario cameras that trail Mario (free roam, close, Mario cam), the
radial cameras within the game's own 120 degree bound on `sModeOffsetYaw` (60 degrees for the outward
radial cameras of Tick Tock Clock, Dire Dire Docks and the volcano, which pull anything further back at about
84 degrees a second; a snap is cut at the bound, or dropped when less than 5 degrees is left, and logged),
and the 8-direction camera. When Mario runs, the radial camera's own rule eases an offset past 60 degrees
back to 60, as it does after the C buttons. Authored cameras
(boss, fixed, path, slide, stairs), the ones that follow Mario's facing (swimming, flying, the cannon),
C-up, the free and rom-hack cameras, cutscenes, pause, mode transitions and a radial C-button swing in
progress take none: the stick does nothing then. The Lakitu camera normally swings back behind an idle
Mario at about 32 degrees per second; after a comfort turn it holds the chosen heading until Mario moves
or the player turns the camera. With turning on, right stick X never reaches the game; the game's C-left
and C-right (its own camera swing) are on the D-pad's left and right (the Frame only: Index and Touch have
no D-pad, so there they need `turn_mode = off`, which gives the stick back to them). C-up and C-down stay
on right stick Y.

**Movement (`move_relative`, `third_person_move`).** In stereo play Mario's stick is relative to what the
wearer sees: `mario.c` adds to the camera's yaw the turn still queued and the yaw of the head (`head`, the
default) or of the left or right hand's aim pose (`left`, `right`; the head when the hand is not tracked),
each measured from the recentre's forward. SM64 already turns Mario to walk where the stick points
(`third_person_move = face_move`; there is no aiming, so `face_camera` does not apply). The game's own
rule that pulling back does not turn the camera's movement yaw is kept.

**The rest of the comfort set.** `vignette` (off) / `vignette_strength` (0.6) darken the edge of each eye's
image, centred on that eye's own axis, during artificial rotation: the shown heading's rate without the
head (smooth turning, the game camera turning by itself), from nothing below 5 degrees per second to the
full strength at 30. It closes in 0.2 s and opens in 0.5 s; a snap or a camera cut is a jump, not motion,
and shows none. It is a blended full-screen triangle drawn after each eye's world pass (the HUD quad stays
clear). `camera_shake` (off) leaves out Lakitu's shakes (`update_lakitu`: pitch, yaw, roll and handheld
shake). `head_translation` (1.0) and `level_pitch` as above. `horizon_lock`, `follow_vehicle_rotation`,
`roomscale`, `lean_limit` and `duck_crouch` do not apply (no vehicles, no first-person body).

**Diagnostics.** `[vr-cam]` lines: every snap (shown and game camera heading, and whether the camera's limit
cut it), a turn the camera could not take or that was ignored (at most one line in 2 s), and every 10 s the shown and game camera headings, the turn taken in all, the gate
and the comfort values. `diag_turn_stick = D` holds right stick X at D for 4 s and releases it for 4 s in
stereo play (a snap a hold, or the smooth turn), logs the headings and their rates every 0.25 s, and traces
the 12 frames after each snap: the shown heading should step once by the snap angle and the game camera's
heading should reach it within a tick, never swinging after. The `dump_frame` state file has a `comfort` line.

## 5. Frame protocol

`xrWaitFrame` paces the loop. The game ticks at its fixed 30 Hz; `vr_sched.c` places each XR frame on that
timeline by `predictedDisplayTime` and the render interpolates between ticks, so the simulation rate is
the same at any refresh rate. Gameplay renders in stereo (one replay of the display list per eye, 2D
into the HUD quad); menus, the intro and transitions that need it use a world-locked flat panel, with
a fade quad over warp transitions. Standby (`shouldRender` false) freezes the simulation and keeps polling.

## 6. Settings and diagnostics

`vr.toml` in `~/.local/share/SM64VR/` (keys from the sfvr registry plus `world_scale`, `level_pitch`,
`star_select_scale`, `camera_closer`, `camera_lower`, `third_person_move`, `force_mono`, `fov_mode`,
`panel_distance`, `panel_width` and the diagnostics).

Comfort keys (section 4; all live):

| Key | Default | Range |
| --- | --- | --- |
| `turn_mode` | `snap` | `snap`, `smooth`, `off` |
| `snap_angle` | 30 | 15, 30, 45, 60, 90 |
| `smooth_turn_speed` | 120 deg/s | 30 to 360 |
| `vignette` / `vignette_strength` | off / 0.6 | 0 to 1 |
| `move_relative` | `head` | `head`, `left`, `right` |
| `third_person_move` | `face_move` | `face_move` only |
| `world_scale` | 104 units/m | 20 to 400 |
| `camera_closer` | 0 | 0 to 0.8 |
| `camera_lower` | 0 | 0 to 1 |
| `height_offset` | 0 m | -1 to 1 (the sfvr registry's range) |
| `seated` | off | |
| `head_translation` | 1.0 | 0 to 2 |
| `camera_shake` | off | |
| `level_pitch` | on | |
| `diag_turn_stick` | 0 (off) | -1 to 1, config and env only |
Environment variables `SM64_VR_<KEY>` override a key for one run. Diagnostics: `force_render`
(draw and run in standby with a synthetic head), `diag_yaw`, `dump_frame`, `dump_worn_frame`,
`probe_seconds`, `diag_menu_stereo`, `diag_rate_unworn`, `diag_turn_stick` (section 4), and `timing` for the `[vr-perf]` line every 10 s
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
4. Turning and camera (section 4), in Castle Grounds (free roam) and Bob-omb Battlefield (radial):
   - Snap right and left with Mario idle: one clean 30 degree step, no swing after, and the view stays
     there (no slow swing back behind Mario). Push forward: Mario walks where you look.
   - In Bob-omb Battlefield keep snapping one way: the turns stop at the radial camera's bound; note how
     that feels. D-pad left/right still swing the game's camera (C-left/C-right). In the castle lobby
     (a fixed camera) the stick does not turn at all: is head turning enough there?
   - `turn_mode = smooth`: a steady turn at the stick's rate that stops on release; then with
     `vignette = true`: the edges darken while turning (and while the game camera turns by itself).
   - Turn the head 90 degrees and push forward: Mario walks where the head points.
   - Recentre (hold L View 1 s, buzz) facing sideways; duck: the view lowers; hold 3 s (second buzz): the
     duck becomes the standing height. `seated = true` sitting down: the view stays at the camera.
   - `camera_closer = 0.4`, `camera_lower = 0.5`, `world_scale` 60 and 200: any of them uncomfortable or
     clipping into walls?
   - Haptics on the game's rumble.
5. Say whether 120 felt held; if not, which scene dropped.

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
| Frame controller input, haptics | headset-unverified |
| Snap and smooth turning handed to the game camera in one step (protocol simulated with the camera's interpolation; snap instant once, smooth rate exact), radial bound, idle hold | pure test |
| Stick relative to the view and the head or a hand (`move_relative`); C-left/C-right on the D-pad | pure test (direction maths); input mapping unrun |
| `world_scale` 20 to 400, `camera_closer`, `camera_lower`, `height_offset` (rig maths, contract, IPD, default bit parity) | pure test |
| Recentre keeps the standing height; View held 3 s, focus and `seated` take it | pure test |
| Vignette on artificial rotation (ramp), `camera_shake` off | pure test (ramp); drawing and the shake hook unrun |
| `[vr-cam]` logs and `diag_turn_stick` | built, unrun |
| Menus and HUD on the flat panel and HUD quad | headset-unverified |
