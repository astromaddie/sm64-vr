# SM64 VR (Steam Frame) architecture

Current state of the VR layer in this fork of sm64coopdx. The code is in `src/pc/vr/`, all of it behind
the `VR=1` build flag (`SM64_VR`); with the flag off the flat build is unchanged. The OpenXR host is
`vr_xr.c`, the frame loop and setup are `vr.c`, the per-eye replay of the game's display list is
`vr_render.c`. The settings registry is the vendored `third_party/sfvr` plus the port keys in
`vr_settings.c`.

The Frame build includes live camera controls, pause/settings presentation and a stereo cinema path
for selected scripted cameras. This document describes the implementation and retains the dated
device notes below for detailed history.

## 1. Target

Native Linux ARM64 title for the Steam Frame (SteamVR OpenXR, desktop GL through GLX/Zink), launched
from Steam as the devkit title `SM64VR`. The user supplies the US ROM in `~/.local/share/SM64VR/`.

## 2. Build and deploy

```
docker build -t sm64-vr-linux -f tools/docker/linux.Dockerfile tools/docker   # once
tools/docker/build-linux.sh on          # make VR=1 into build/us_pc_vr
tools/docker/build-linux.sh package     # build/package/SM64VR
tools/docker/build-linux.sh abi
tools/docker/build-linux.sh verify      # deterministic VR=0 comparison with the pre-preview main
FRAME_HOST=steamos@<frame> tools/steam_frame/deploy.sh [--config KEY=VALUE ...]
```

Book the Frame (`frame-lock.sh`) before deploying or launching. Launch through Steam, never over SSH:
a binary started from SSH sees no controllers, and the per-app refresh rate only applies to Steam
launches.

## 3. Display (standard.md section 9)

- **The app can't change the rate on the Frame.** SteamVR offers an app one rate: the title's per-app
  preferred rate (`steam.app.<id>` in `steamvr.vrsettings`), applied to Steam-title launches only. The user
  sets it once, in SteamVR's per-app settings in the headset. **This title: 120 Hz, resolution 2160**, set
  on 2026-10-06 (`steam.app.3166136263`: `preferredRefreshRate` 120, `resolutionOverride` 2160, with the
  playbook's `steamvr-per-app.sh`). It holds 120 Hz at 2160 per eye (section 7).
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
- **Resolution.** Each eye has its own swapchain, created at 1728 px (the Frame's default
  recommendation, `VR_EYE_BASE_PX`, keeping the recommended aspect) times `resolution_scale`, and
  rendered into directly: the replay binds the swapchain image as its framebuffer. There is no larger
  intermediate target and no blit. The default `resolution_scale = 1.25` gives 2160 x 2160, the panel's
  own resolution. The base is fixed rather than the runtime's recommendation because SteamVR's per-app
  resolution changes the recommendation: with the per-app resolution at 2160 it recommends 2160, and
  1.25 times that would be 2700 px (DK64 ran at exactly 60 Hz that way; playbook lesson 97). So the
  per-app resolution doesn't size this game's eyes; `resolution_scale` does. The log has one `eye N
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
size. The range is 20 (Mario 8 m) to 416 (Mario 0.39 m, a diorama); the Room row shows apparent size from
25% to 520% relative to the 104 units/metre baseline (the native range maps to 25% to 520%). The star select screen has its own scale
(`star_select_scale`, 300). `camera_closer` (-1 to 0.8) moves the head origin along or beyond the line
between the game camera and its look-at point; `camera_lower` (-1 to 1) then changes the remaining height
toward or away from the look-at point (for camera E and look-at A, P.xz = E.xz + C (A.xz - E.xz),
P.y = A.y + (1 - C)(1 - L)(E.y - A.y)). Negative closer extends farther, and negative lower extends higher.
Lowering turns the inherited pitch toward A; heading stays. They apply in gameplay only, not on star select.
The renderer uses the engine's ray collision test to stop the shown origin five game units before an
obstruction; settings remain at the requested values, so they take effect again when the path clears.
`height_offset` raises the eyes by that many metres
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
by the ordinary recentre. Switching `seated` on takes the height at once (the height only: the yaw, the
horizontal origin and a held menu stay), so the settings page shows what it does. Menu is the game's Start; a short View press is the gamepad Back (the player
list in online play; SM64 itself has no Select).

**Manual camera (`camera_auto_rotate` off; settings page, Comfort: "Auto camera turning").** Lakitu
turns on his own in four ways: the default camera trails Mario on a leash (its heading is re-read
from where the camera is each update, `update_default_camera`), it swings behind an idle Mario and
away when he comes within 250 units, `pan_ahead_of_player` looks toward where he faces, and
Lakitu's shown position trails more slowly than his focus (`update_lakitu`, 0.3 against 0.8). With
the setting off (`gVrCamTurn.manual`, set each frame in `comfort_refresh`), `camera.c` keeps last
update's heading and puts the camera back on it before the wall checks, zeroes the swing, skips
the pan, and puts Lakitu's shown position on the held heading at his own distance and height. The
radial and outward radial cameras turn about the area's centre as Mario moves, which cannot be
held still, so they run as the Lakitu camera (`mode_lakitu_camera`) and take turns without limits.
What still turns the camera: comfort turns, C-left/C-right, the wall avoidance
(`rotate_camera_around_walls`, obstructed view only) and the door exit. `collide_with_walls` and
the floor and ceiling checks are untouched. The 8-direction camera already had no drift. Cameras
that follow Mario's facing by design (swimming, flying, slides, the cannon), boss, fixed and
scripted cameras are unchanged.

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

**Live adjustment shortcut (Steam Frame).** Hold left grip to adjust the view with the right stick:
left/right shrinks/grows the world; up/down raises and distances or lowers and closes the camera.
The grip is the profile's separate boolean `/user/hand/left/input/squeeze/click`, following
[Valve's controller input profile](https://partner.steamgames.com/doc/steamhardware/steamframe/input).
The bumper and trigger retain their existing actions. Index and Touch keep their squeeze-to-bumper
mapping and have no grip shortcut.

Scale uses `world_scale` (units per metre, inverse apparent size) and changes proportionally at
1.5x per second at full deflection. Vertical adjustment changes `camera_closer` and `camera_lower`
together at 0.25 per second. A 20% deadzone and predicted display time make partial deflection slower
and the rate independent of refresh. Values stop at their existing limits; up returns to the original
higher/farther game camera at zero, and cannot extend beyond it. Changes apply immediately and save
after the usual quiet interval. The open settings page refreshes its rows when values change.

The shortcut works in gameplay and the settings page's gameplay preview. The right stick is consumed
while grip is held, preventing comfort turns and C-button actions; after release it stays muted until
centred. The left stick continues to move Mario. Title/menu, cinema and ordinary pause states keep
their existing input. Menu and View remain available, and either prevents simultaneous adjustment.
Focus loss releases input and clears the transient modifier state. Physical control and comfort
acceptance remain headset-unverified.

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
into the HUD quad); menus use a world-locked flat panel over black eye images. Scripted cutscenes,
including the new-game Peach/Lakitu intro, use a stereo cinema projection screen (section 11). A fade
quad covers mode changes and warp transitions. Standby (`shouldRender` false) freezes the simulation
and keeps polling.

## 6. Settings and diagnostics

`vr.toml` in `~/.local/share/SM64VR/` (keys from the sfvr registry plus `world_scale`, `level_pitch`,
`star_select_scale`, `camera_closer`, `camera_lower`, `third_person_move`, `force_mono`, `fov_mode`,
`defaults_profile`, `panel_distance`, `panel_width`, `cutscenes`, `cinema_distance`, `cinema_width` and the diagnostics).

Comfort keys (section 4; all live):

| Key | Default | Range |
| --- | --- | --- |
| `turn_mode` | `snap` | `snap`, `smooth`, `off` |
| `snap_angle` | 30 | 15, 30, 45, 60, 90 |
| `smooth_turn_speed` | 120 deg/s | 30 to 360 |
| `vignette` / `vignette_strength` | off / 0.6 | 0 to 1 |
| `move_relative` | `head` | `head`, `left`, `right` |
| `third_person_move` | `face_move` | `face_move` only |
| `world_scale` | 104 units/m | 20 to 416 |
| `camera_closer` | 0 | -1 to 0.8 |
| `camera_lower` | 0 | -1 to 1 |
| `defaults_profile` | `standard` | `standard`, `frame_personal` |
| `height_offset` | 0 m | -1 to 1 (the sfvr registry's range) |
| `seated` | off | |
| `head_translation` | 1.0 | 0 to 2 |
| `camera_shake` | off | |
| `camera_auto_rotate` | on | Off: Lakitu turns only when the wearer turns him (section 4, "Manual camera") |
| `level_pitch` | on | |
| `diag_turn_stick` | 0 (off) | -1 to 1, config and env only |
| `settings_page_side` | `right` | `right`, `left`, `centre` (section 10) |
| `diag_settings_preview` | 0 (off) | 0 to 600 s, config and env only (section 10) |

Environment variables `SM64_VR_<KEY>` override a key for one run. Diagnostics: `force_render`
(draw and run in standby with a synthetic head), `diag_yaw`, `dump_frame`, `dump_worn_frame`,
`probe_seconds`, `diag_menu_stereo`, `diag_rate_unworn`, `diag_cinema_seconds`,
`diag_dialog_cutscene_seconds` (section 11),
`diag_turn_stick` (section 4), `diag_settings_preview` (section 10), and `timing` for the `[vr-perf]` line every 10 s
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

**120 Hz, 2026-10-06.** With the per-app entry at 120 Hz / 2160, same method (Steam launch, `force_render`,
`diag_menu_stereo`: the main menu's Castle Grounds background in stereo), three runs of about 60 s:

| Item | Result |
| --- | --- |
| Offered / granted | 120 Hz / 120 Hz (`Preferred refresh rate for 'steam.app.3166136263' is 120.00Hz`), frame period 8.33 ms |
| Eye swapchains | 2160 x 2160 (base 1728 x 1.25; the runtime now recommends 2160, which is no longer multiplied) |
| Frame rate | 120.0 fps, `missed=0` in every `[vr-perf]` window of all three runs |
| CPU / GPU | CPU 3.0 to 3.1 ms (replay 2.2 to 2.3, eyes 0.9 and 1.3, hud 0.7); GPU 3.6 ms, of an 8.33 ms period |

Before that, the display stayed at 90 Hz because the per-app setting was 90 and the app can't change it. The
live "not held" report has not been seen on the device, since the rate was held; the window logic is
unit-tested (`tests/vr/test_vr_rate.c`, `test_vr_xr.c`). Heavier scenes (Bob-omb Battlefield) were not run: the unattended run has no input.

## 8. Wear checklist (at most 15 minutes)

0. The per-app rate is already 120 Hz / 2160 (set over SSH 2026-10-06). Launch from Steam.
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
   - Hold left grip and move the right stick left/right to shrink/grow the world, then up/down to raise/distance or lower/close the camera. Check changes stop at their limits, the right stick cannot turn or press C-up/down while held, Mario still moves from the left stick, and a held right stick does not turn on release until centred.
   - `camera_closer = 0.4`, `camera_lower = 0.5`, `world_scale` 60 and 200: any of them uncomfortable or
     clipping into walls?
   - Haptics on the game's rumble.
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
| Title runs at 120 Hz with the per-app rate set to 120 | verified unattended (3 runs, 120.0 fps, 0 missed, GPU 3.6 ms at 2160) |
| `[vr-perf]` GPU time from timestamp queries | headset-unverified (read unattended) |
| 2160 x 2160 eye swapchains, rendered directly | headset-unverified |
| `refresh_rate` / `resolution_scale` settings and v1 to v2 migration (unit-tested) | headset-unverified |
| Stereo gameplay, display-time scheduler, level pitch | headset-unverified |
| Star select in stereo, warp transitions with a fade quad | headset-unverified |
| fov_mode 3 (the Frame's measured full fov) | headset-unverified |
| Frame controller input, haptics | headset-unverified |
| Left grip + right stick live scale/camera shortcut and open settings-row sync | host input and renderer tests pass (126 input checks, 72/90/120 Hz parity); clean `4eefd2271` installed and Frame binding accepted, preview captured unattended 2026-10-07 (section 13); physical input and comfort remain headset-unverified |
| Snap and smooth turning handed to the game camera in one step (protocol simulated with the camera's interpolation; snap instant once, smooth rate exact), radial bound, idle hold | verified unattended 2026-10-06 (Castle Grounds, `diag_turn_stick = 1`: a 30 degree snap stepped the shown heading 90 to 120 in one frame and the game camera arrived the next tick, no swing; at 120 Hz, 0 missed). Afterwards Lakitu drifted back about 8 degrees over a few seconds with Mario idle, and the view followed (vanilla). Not worn |
| Stick relative to the view and the head or a hand (`move_relative`); C-left/C-right on the D-pad | pure test (direction maths); input mapping unrun |
| `world_scale` 20 to 416, signed `camera_closer` / `camera_lower`, height and IPD | pure tests; LOS constraint exercised through the production camera function |
| Recentre keeps the standing height; View held 3 s, focus and `seated` take it | pure test |
| Manual camera (`camera_auto_rotate` off) | pure tests (key registered, page row); ARM64 VR build and package; not run on the device, headset-unverified |
| Vignette on artificial rotation (ramp), `camera_shake` off | pure test (ramp); drawing and the shake hook unrun |
| `[vr-cam]` logs and `diag_turn_stick` | verified unattended 2026-10-06 |
| Menus and HUD on the flat panel and HUD quad | headset-unverified |
| In-headset VR settings page: tab table, apparent-size percentage mapping, registered-key filter | pure test (`tests/vr/test_vr_menu.c`) |
| Runtime settings API: signed camera clamping, persistent defaults profile, profile-aware reset, save/reload, env overrides | pure test (`tests/vr/test_vr_settings.c`) |
| Live apply of a changed setting to the renderer (`vr_render_apply_settings`) | verified unattended 2026-10-07 for world scale and camera closer through the actual Room rows; height/lower/seated/head movement have host coverage and need a wear check |
| The DJUI panels (pause menu "VR settings", tabs, Defaults), pad navigation | actual pause/Room panels rendered unattended 2026-10-07; manual pad navigation, other tabs and Defaults remain wearer-unverified |
| The pause menu held in the world where it opened; HUD quad kept while a menu is up even with `hud_visible` off | verified unattended 2026-10-07 for HUD-on/off placement and page visibility; tracked head movement and manual recentre remain wearer-unverified |
| Settings page over the gameplay view: pause overview held off while the page is open, overview cut faded both ways, menu turned aside (`settings_page_side`) and eased home, game HUD left out for `hud_visible` off, `seated` on takes the height | pure/host render tests pass; production camera/DJUI sequence verified unattended 2026-10-07 with both-eye/HUD captures, right yaw -20.31 degrees, exact value restoration, and overview fades logged; seated height and tracked recentre remain wearer-unverified |
| Cinema screen corners, positive parallax, display-time latch, stage fades, cinema projection-layer structs and settings | pure test; headset-unverified |
| Cinema eye replay, opaque text band below the picture, no-roll entry/recentre anchor | verified unattended 2026-10-07 in the real dialog/camera pipeline: both eyes and opaque text band inspected, classification 162/11, height-preserving recentre on exit logged; tracked/worn acceptance pending |
| Cinema aspect and interpolated projection load | verified unattended 2026-10-07 (`diag_dialog_cutscene_seconds = 30`: cinema 162 in camera mode 11, `world_proj_loads=2` in both eyes, `picture_aspect=1.777778`; the 16:9 picture shows Mario in normal proportions, positive parallax, and gameplay stereo returns after). Root cause: VR code's `atan2f` linked to the game's own (SM64 convention) on the device only. The real King Whomp scene is not worn |
| One-shot boss-mode dialog diagnostic | verified unattended 2026-10-07 (recipe in section 11) |
| Mono panels and closed warps always have black projection beneath quads | pure test + host render with synthetic retained list; device/worn unverified |
| Integrated Linux ARM64 VR build, staged package and glibc ABI floor | built, packaged and deployed 2026-10-07 from clean `7e4f10d8d`: ELF AArch64, glibc 2.34 below the 2.35 floor, all 416 file hashes verified, installed executable hash matches; not worn |
| VR-disabled flat build and source identity of modified shared translation units | verified 2026-10-07: independent deterministic pre-preview main and integrated builds have identical executable bytes (section 12); flat game not played |

## 10. VR settings page

The pause menu has a "VR settings" button while a VR session is running (the flat build has no such
button). It opens a list of tabs, each a DJUI panel built like coopdx's own option panels (checkboxes,
selection boxes, sliders, buttons), in the standard's order: Comfort, Controls, HUD & menus, Display &
performance, Accessibility & audio (no entries yet, so it is not listed), Room. Every tab ends with
"Defaults" (resets that tab's keys) and "Back". Diagnostics never appear.

- **Content.** `src/pc/vr/vr_menu.c` is a table of entries: key, label, tab, widget (toggle, choice,
  slider, button), choice labels and values, slider range and step, display unit. An entry whose key is
  not in the registry is left out, and so is a tab left empty, so keys added by other features show up
  with no change here. The cinema and comfort keys are in the table already (`cutscenes`,
  `cinema_distance`, `cinema_width`, `camera_closer`, `camera_lower`). `src/pc/djui/djui_panel_vr.c`
  builds the panels from it; `djui_slider.c` has a small `SM64_VR` hook to show a unit instead of the
  step number. World scale is shown as Mario's height in metres (161 / `world_scale`, 0.45 to 3.0 m; `world_scale` 20 to 416 allows 0.40 to 8.05 m).
  Resolution is read when the swapchains are made, so its label says "(restart)"; the refresh rate
  label says "SteamVR wins" (section 3). Sliders use whole steps (at most about 60 stops), because
  DJUI's pad control moves a slider one step per tick.
- **Applying and saving.** `vr_setting_set()` (`vr.h`) clamps with the registry's rules, rebuilds the
  renderer's settings and hands them to `vr_render_apply_settings()`, so the next XR frame uses them.
  Input reads `haptics` every frame already; other code that caches a key can poll
  `vr_settings_revision()`. The file is written once the page has been quiet for 0.5 s, when a panel
  closes and at shutdown, atomically (temp file, fsync, rename). A key set from the page leaves the
  environment override behind; an override nobody changed is not written back.
- **Where it is drawn.** In gameplay the pause menu is part of the 2D segment, so it is on the HUD quad:
  1280 x 720 px over 40 degrees at `hud_scale` 100, 32 px a degree, so DJUI's 32 px text is 1 degree
  tall (cap height about 0.7 degree, over the 0.6 degree the standard asks for; at `hud_scale` below
  about 85 it drops under). The panel is 640 px, about 20 degrees wide. On the mono panel (2.2 m wide at
  2 m, about 22 px a degree) the text is 1.4 degrees. The HUD quad used to follow the head; while a menu is
  up it is now held where the head looked when the menu opened (until a recentre), as the standard asks
  for menus. The quad also stays on while a menu is up with `hud_visible` off, or the page that turns it
  on could not be seen.
- **Pad navigation.** The same path as a gamepad in flat coopdx: D-pad or stick moves between entries, A
  changes a toggle or choice or picks up a slider (then stick left and right), B goes back. Buttons are 2
  degrees tall (the standard's 2.5 to 3 degree target is for the laser).
- **Preview on the gameplay view.** SM64's course pause zooms Lakitu out: two frames after Start, in
  the outdoor areas of `sZoomOutAreaMasks`, `zoom_out_if_paused_and_outside` puts the camera node 6000
  units out over the area's centre. The page used to be seen over that overview, so a change of scale,
  height, closer or lower showed on a toybox of the whole map, not on the view it changes. Now:
  - While the page is open (any of its panels: `gVrSettingsPage.open`, set and cleared by
    `djui_panel_vr.c`'s first panel) `camera.c` holds the overview off, as flat coopdx does for its
    player menu. The camera node then takes Lakitu's pose, and a paused game does not update Lakitu, so
    that pose is the one from the last tick before the pause, with Mario in view. Nothing is captured or
    restored: Lakitu's state is never touched by the overview. The vanilla overview stays on the plain
    pause screen and comes back when the page closes. In online play the game is not paused and there
    is no overview, so the page is over live gameplay.
  - Every key that changes the view reads live (section 4): `world_scale`, `height_offset`,
    `camera_closer`, `camera_lower`, `head_translation`, `level_pitch` (no page entry), `hud_distance`
    and `hud_scale` (the held menu is placed again from the head it opened at, each frame, with the
    current distance), `seated` (switching it on takes the height now). `hud_visible` off leaves the
    game's HUD out behind the open page (`gVrSettingsPage.hide_hud`, area.c). `star_select_scale` only
    shows on the act select, where there is no pause. The comfort gate (`vrr.play`) is open on the
    paused gameplay view, so closer and lower apply; it is closed on the overview, which is a map, not
    the play camera. The pause shade (`shade_screen`) is left out while the page is open.
  - **The menu turns aside.** While the page is open the held menu (the HUD quad) turns about the head
    it was placed from, to `settings_page_side` (`right` by default, `left`, or `centre` for where it
    opened), far enough that the page's inner edge is 10 degrees from the forward: 20 degrees at
    `hud_scale` 100, 30 at 200. It eases there at 160 degrees per second (it is a panel, not the
    world) and back when the page closes; with the menu gone the gameplay HUD is unturned at once. The
    page is read with a head turn and the scene ahead stays clear. The gameplay HUD is on the same quad,
    so it turns with it. Right is the default so the dominant hand's laser (follow-up) points to it
    without crossing the body; it is a setting (lesson 13).
  - **No snap.** The overview's cut, both ways (Start, the page opening and closing, resume), starts the
    stage's black fade (0.25 s hold, 0.35 s out) in stereo, from `gVrTick.pause_overview` published with
    the tick that draws it, so the frame that jumps is already black. The page opening and closing
    alone is not a camera jump and does not fade. Before this the plain pause cut to the overview with
    no fade.
  - **Device recipe (unattended).** `diag_settings_preview = N`: after N seconds of gameplay it
    presses Start (the overview), opens the pause menu at 1.5 s, dumps at 3 s (`paused-`), opens the page's
    Room tab at 4 s, dumps at 6 s (`page-`: the gameplay view behind the turned page), steps
    `world_scale` to Mario 0.80 m and `camera_closer` to 0.4 through the tab's rows at 7 s, dumps at
    8 s (`step-`), puts both back exactly at 9 s, closes the menus at 10 s, presses Start at 10.5 s and
    dumps at 12 s (`resumed-`). Each dump is `<tag>vr-eye0.png`, `vr-eye1.png`, `vr-hud.png` and
    `vr-state.txt` (its `preview` line: page, overview, menu yaw, side, `hide_hud`), and each step logs a
    `settings page: diag:` line with the play mode, the overview and the two values. Run with
    a booked Frame with backed-up config/save files: set `force_render = true`,
    `diag_settings_preview = 15`, and temporary title arguments `--server 7777 --skip-intro`
    through `deploy.sh`. Launch the registered Steam title via
    `steam://rungameid/13598451704298209280` in `~/.steam/steam.pipe` (Castle Grounds is an overview area),
    then allow at least 45 s after process startup. Expect `paused-vr-eye0.png` to
    be the overview, `page-` and `step-` the view Mario was in before the pause with the page turned
    right, `step-` visibly closer and larger, `resumed-` the gameplay view, `preview: ... faded` log
    lines at each overview cut, and `world_scale` / `camera_closer` back at their values at the end.
    The page writes `vr.toml` 0.5 s after a change, so the stepped values are in it from 7.5 s to 9.5 s:
    let the run last past N + 10 s. Stop the owned process, restore the original config/save bytes,
    restore normal empty title arguments, verify the hashes and disabled diagnostics, remove task dumps,
    and release the booking, on success or failure.
- **Paused.** The pause menu does not stop the game loop in SM64: each XR frame still replays the newest
  tick, and the scheduler keeps ticking, so the page redraws and reads input while the game is paused.
  Nothing branches on pause in the VR loop (the DK64 lesson 99 case does not apply).
- **Follow-up, not built.** DJUI's cursor takes its position from `mouse_window_x` / `mouse_window_y`
  (`djui_cursor.c`) and the left button from the mouse buttons, so the laser pointer can drive it: intersect
  the dominant hand's aim ray with the HUD quad (pose in `vr_render.c`), write the hit's pixel into those
  and set the button on a trigger click.
- **Wear checklist for the page.** (1) Pause: "VR settings" is in the pause menu, and the menu stays where it
  opened when the head turns. (2) Read the smallest text of every tab (the value labels on the sliders); if
  any is hard to read, say at which `hud_scale`. (3) Pad: D-pad and stick move, A toggles and picks up a slider,
  B backs out, Start closes. (4) Haptics slider: the buzz of the next event changes. (5) World scale and
  height offset: Mario and the view change as the slider moves, with the game still paused. (6) Turn the
  HUD off from the page: the page stays; close the menu and the HUD is gone; turn it back on. (7) "Recentre
  now" puts the menu and the view in front of the head. (8) Defaults on a tab restores that tab only.
  (9) Quit and relaunch: the changed values are in `vr.toml` and in effect.
  (10) The preview, in Bob-omb Battlefield: Start (the view fades to the overview), R, VR settings: a
  fade back to the view you paused in, Mario in front, the page turned to the right. Room tab: move
  World scale, Height offset, Camera closer and lower, switch Seated on while sitting; each shows on
  Mario at once. Is the page's place comfortable to read with a head turn, and does it ever hide what
  you were changing? Try `left` and `centre` under HUD & menus. Back: a fade to the overview; resume:
  a fade to play. Any fade that felt like a turn or a snap?


## 11. Scripted cameras and the stereo cinema

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
Horizontal projection scale is adapted to 16:9 from the **retained matrix's** vertical scale, preserving
its interpolated FOV, asymmetric lens centre and near/far mapping. Tick FOV is only the initial sky
fallback. Cinema world/sky viewports fill the picture even when the game overrides its viewport;
authored scissors remain clipping operations, and HUD/panel/gameplay viewport behavior is unchanged.
The camera-space eye shift is applied analytically, avoiding cancellation roundoff from large world
coordinates.

The 2026-10-07 host reproduction found a production-linkage defect: SM64 exports `atan2f` with its
game-axis convention. Cinema's screen-corner calls resolved to it in the game but to libc in the old
host fixture, submitting a reciprocal 9:16 frustum for a 16:9 picture. Linking the actual
`src/engine/math_util.c` made the original screen-corner suite fail 64 checks. Cinema now explicitly
uses libc's double `atan2`. The same investigation reproduced `patch_mtx_interpolated` retaining the
**GraphNodePerspective address** instead of the generated matrix, bypassing perspective replacement.
The VR-only path now retains the matrix address. Source-aspect leakage was also reproduced with
4:3/ultrawide projections; cinema now normalizes it rather than stretching it across the screen.
These are host reproductions of the defective paths; the specific worn King Whomp scene still needs
an eye dump and wear check to confirm the full reported symptom is resolved.

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
| `diag_dialog_cutscene_seconds` | 0 | 0–120 s; one real `CUTSCENE_DIALOG` in `CAMERA_MODE_BOSS_FIGHT`, after 30 settled live gameplay ticks; uses Mario as speaking-object focus and King Whomp's authored text; restores the previous mode/focus at expiry |

For unattended integration checks use `force_render = true`, `diag_cinema_seconds = 10`,
`dump_frame = 120`, and `timing = true`. The forced route still respects `force_mono`, never changes
simulation or loads a level, and leaves act select alone. The ordinary New Game intro exercises real
classification. `vr-state.txt` includes `mode cinema`, latch/request details, cutscene/mode/shot/timer,
screen pose/size and both submitted eye poses/FOVs, alongside the virtual game eyes. Cinema reports
the actual matrix's source aspect/FOV, tick FOV, normalized picture aspect and `world_proj_loads` for
each eye (must be nonzero), virtual eye spacing in game units and runtime IPD in metres; it never converts that spacing through
gameplay `world_scale`. `[vr-perf]` names
cinema separately from mono and stereo. Eye dumps are the square storage images; resize them to 16:9
to inspect the picture as seen on the screen.

For the coordinator's unattended **real dialog** check, install the integrated package and claim the
Frame according to the port playbook. Preserve config/save hashes, then temporarily configure
`force_render = true`, `force_mono = false`, `cutscenes = 0`, `diag_cinema_seconds = 0`,
`diag_settings_preview = 0`, `diag_dialog_cutscene_seconds = 30`, `dump_frame = 1200` and `timing = true`.
Register temporary `--server 7777 --skip-intro` title arguments and launch through Steam
(`steam://rungameid/13598451704298209280` into `~/.steam/steam.pipe`), allowing at least 45 s after
process startup. Confirm the diagnostic armed log and `vr-state.txt`:
`mode cinema`, `cutscene=162`, `camera_mode=11`, nonzero world projection loads in **both** eyes,
`picture_aspect=1.777778`, and screen height = width × 9/16. Compare both PNGs resized to 16:9,
including the sky, Mario's proportions and the opaque dialogue band; expiry should restore gameplay.
After stopping the owned process, restore config/save bytes and empty title arguments, verify hashes
and disabled diagnostics, remove task dumps and release the booking even on failure. This exercises
the real camera/dialog pipeline in the
current scene, not King Whomp's actual encounter or controller input. Wear check: talk to King Whomp
at the summit, then a sign/NPC, Bob-omb King and Bowser; confirm stable proportions, readable text,
stereo depth, and correct entry/exit/recentre.


### Integrated device verification, 2026-10-07

The installed source is clean `7e4f10d8db8a8b3457bfa36b7153633c3ca019d6`; package and installed
executable SHA256 are `92230b67ceec6e6471473013fa79ae60c1b16105319ab61d5436f85728e2623e`.
All 416 package payload hashes and executable/bundled-library ABI/dependency checks pass.

Three runs used the existing Steam title, 2160 x 2160 eyes at a runtime period of 8.33 ms, with the
headset on the desk and `force_render` synthetic views (tracked stereo frames: 0):

| Capture | Observed production behavior |
| --- | --- |
| Preview, HUD on | `paused-`: overview=1, page=0, comfort play=0. `page-`: overview=0, page=1, play=1, right menu yaw=-20.31 degrees. `step-`: world_scale=201.25, closer=0.40. Exact restore to 104/0; `resumed-`: page/menu/overview=0. Page and resumed game-camera position/back are identical |
| Preview, HUD off | All four both-eye/HUD/state captures repeat the sequence; open page remains visible with hide_hud=1 and gameplay counters absent, resumed HUD hidden |
| Boss-dialog cinema | Cutscene 162, camera mode 11, screen 3.6 x 2.025 m, picture_aspect=1.777778, world_proj_loads=2 in each eye. Both 16:9 picture reconstructions and the complete text band inspected. Diagnostic expires, camera mode 16 and stereo return, height-preserving recentre logged |

Preview overview entry/exit/page cuts log the stage fade; still captures are after its black interval.
The panel composites use the submitted poses/FOVs and HUD straight alpha; they do not simulate lens
distortion or establish depth/comfort acceptance in the headset. The diagnostic uses King Whomp's
text with Mario as the speaking-object focus, not the actual Whomp encounter.

Capture windows include frame-rate dips (about 92-106 fps in the preview run). Initial/post-capture
refresh windows recover to 120.1 fps; the cinema run has two post-return windows at 120 fps with zero
missed frames (GPU 3.25-3.36 ms). These are unattended measurements, not worn performance acceptance.

Original `vr.toml`, `sm64config.txt`, save, ROM and pre-existing run-log hashes were verified after
restoration. Normal title arguments are `LAUNCH-STEAM-FRAME.sh` only, diagnostic keys disabled, no
SM64 process/task dumps remain on the device, and the Frame booking was released.

Wear acceptance remains pending: live height/lower/head movement/seated changes, tracked recentre,
pad navigation and panel readability/comfort, pause/resume transitions, and the real King Whomp
introduction. The short checklist in section 10 covers the settings portion.

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
An additional Mesa 4.5 offscreen host check links the actual game `math_util.c`, perspective builder,
`patch_mtx_interpolated`, and Fast3D matrix/viewport consumers, calls the actual `vr_render_frame`
with a synthetic retained-list producer, then reads its GL eye/HUD targets outside the frame.
It checks rendered-square proportions across 252 stereo frames (7 cutscene/mode pairs × 4 source
aspects × 3 interpolated FOVs × 3 viewports), the retained matrix address, nonzero projection
replacement counts and exact screen corners in the presence of the game's colliding `atan2f`.
The integrated host cinema executable has 8 cases and 8,304 checks. The old `vr_cinema.c` linked into
this same executable fails 1,112 checks. Further checks cover distinct stereo eye images, opaque black text-band margins,
matching interpolated sky/world FOV, cinema holds/gaps, black mono/warp eye pixels, act-select routing
(including a conflicting gameplay flag and stale cutscene id),
the immersive setting, diagnostic activation/expiry and acquisition failure. It is a **host renderer
check, not a real SM64 scene, device run or worn acceptance**. Strict host compilation exposed a
pre-existing dump-path truncation warning; the dump slot now has room for the directory and filename.
`tools/docker/build-linux.sh tests` runs all host suites, including this bounded renderer check.
`tools/docker/build-linux.sh verify` now runs `tools/verify_flat_build_unchanged.sh` on the host,
so managed worktrees whose Git directory is outside the Docker mount work too. It builds isolated
snapshots of pre-preview main (`fcb49579d`) and the current tree, including staged/unstaged source,
with `VR=0 DEVELOPMENT=0 DEBUG=0 DEBUG_INFO_LEVEL=0`, and requires identical executable SHA256
values. `DEBUG=0` alone retains debug line tables, which change when VR-only source lines are added;
`DEBUG_INFO_LEVEL=0` explicitly disables that metadata without changing assertions or optimization. The Linux ARM
flat target is `sm64coopdx.arm`; desktop Linux uses `sm64coopdx`. Scratch trees are removed on exit.
`tools/test_verify_flat_build_unchanged.sh` checks snapshot handling, both executable names and the
mismatch gate with a mock container command.

The 2026-10-07 integrated comparison passed against `fcb49579d`: both full AArch64 flat executables
have SHA256 `f139a1287a55561ac2bd153ebf5e37e55b0516018ccb5ccc63bff5267be8f04d`.
This is deterministic binary parity, not a flat gameplay test. The earlier comparison with debug
line tables retained included changed source-line metadata and did not match.

Package provenance is resolved on the host before entering Docker, with a validated source SHA and
`clean`/`dirty` state including ordinary untracked files, then passed to the package script. Direct
package-script invocations still resolve local Git. Missing or partial provenance fails packaging
instead of emitting `unknown` or claiming a clean tree when Git is inaccessible.

The 2026-10-07 aspect lane also compared the changed
camera, graph-renderer and Fast3D translation units' VR-off preprocessor output against the branch
base; only assertion source-line metadata needed normalization. This is source parity evidence,
not a new flat runtime check.

The direct flat build uses the same image and default build options:

```sh
docker run --rm -v "$PWD":/src -w /src sm64-vr-linux \
  bash -c 'make VR=0 -j4 && test -x build/us_pc/sm64coopdx.arm'
```

This ARM image's regular flat target has the `.arm` suffix; the VR target is unsuffixed.

The 2026-10-07 aspect lane passed `tools/docker/build-linux.sh on`, `tests`, `package` and `abi`.
After the cold native build, an incremental build recompiled the final camera hook and linked
successfully. The executable is ELF AArch64; its maximum requirements are glibc 2.34 and
GLIBCXX 3.4.29, below the glibc 2.35 release floor. All 416 staged file hashes were checked,
and the staged executable matched the native build. Final package metadata uses a read-only
mount of the common Git directory to record the worktree's clean committed revision.
The native cinema object has four `atan2` relocations, and the executable imports libc's
double-precision `atan2` while also exporting the game's conflicting `atan2f`.
This proves the native link uses the corrected corner-angle function; it is not device or
worn acceptance. Evidence logs are in `build/cinema-check/` in the aspect worktree.

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

## 13. Live adjustment shortcut delivery (2026-10-07)

The left-grip/right-stick shortcut is built and installed from clean source
`4eefd2271958510f075c514e1222a8c85c9f3bbe`. All host suites pass, including 126 input, 738 preview
and 8,309 cinema-renderer checks. The input harness exercises production action creation, binding,
sampling and routing, left-stick forwarding, right-stick suppression, neutral rearming, focus loss
and 72/90/120 Hz parity. The final DJUI revision-refresh hook compiled in the full ARM64 build;
the pure host harness does not compile DJUI.

`tools/docker/build-linux.sh on`, `package` and `abi` passed: AArch64, glibc 2.34 below the 2.35 floor,
GLIBCXX 3.4.29, valid dependency closure and 416 matching payload hashes. Package metadata records
that clean source revision, GCC 12.3.0 and pinned OpenXR-SDK 1.1.63. The installed executable matches
SHA256 `a0493c98a21e1dbbdc816d85e5fd93d27f5fe03486bf3fc9d76b843cdf2721ae`.
`SM64_VR_FLAT_BASELINE=fd125285f tools/docker/build-linux.sh verify` also passed: before/after
VR-disabled executables both have SHA256
`f139a1287a55561ac2bd153ebf5e37e55b0516018ccb5ccc63bff5267be8f04d`.

After booking the Frame as `sm64vr-shortcut`, a Steam launch accepted all three profiles and attached
23 actions. Both-eye/HUD/state captures cover ordinary pause overview, settings gameplay preview,
changed scale/camera, restored values and resumed gameplay. The existing row diagnostic changed
world scale 104 → 201.25 → 104 and camera closer 0 → 0.40 → 0; it did not emulate physical grip
presses. Open Room labels match the stepped values. Capture overhead dropped one window to 78.2 fps
with seven misses; subsequent windows recovered to 120.0 fps with zero misses and GPU 3.35–3.36 ms.

This was a synthetic-head unattended run with zero tracked stereo frames. Actual grip operation,
tracked recentring, worn panel readability, comfort and the real King Whomp introduction remain
headset-unverified. The prior milestone's boss-dialog device evidence remains valid; this increment
reran its renderer regressions.

The task process stopped, device diagnostics were removed, normal Steam launch settings restored
and the booking released. Active settings, save and ROM hashes match the pre-run values; original
title logs were restored. The game's `sm64config-backup.txt` sidecar was not independently backed up
and now matches the preserved active configuration; its original identity cannot be verified.
Future runs must include every config sidecar in the pre-run snapshot. Raw current-run evidence is
archived in the task output `sm64-live-shortcut-evidence.zip` with a SHA256 ledger. The evidence-only
follow-up commit does not change the installed package's source identity.

## Camera standard integration verification (2026-10-07)

The earlier live-settings preview and grip shortcut were already delivered.
This increment extends Mario's camera offsets in both directions with native
surface-ray collision and aligns the existing adapter with canonical sfvr
adjustment, range and defaults-profile behaviour.

Source `4f88cc3d6574adf16c13f296929c922a7990f286` passed the existing host
regressions, ARM64 build, ABI/dependency and 416-file package checks. The final
independent VR-disabled baseline (`fcb49579d`) and candidate builds match:
`f139a1287a55561ac2bd153ebf5e37e55b0516018ccb5ccc63bff5267be8f04d`.

The installed `SM64VR` Steam-title executable matches
`f0faad825816ce245ed4099dfc994ea69a6cc28d848defccb8ac833b54d59768`.
After booking the Frame, clean-idle Steam launches captured both eyes/HUD/state
for pause overview, the live Room page, changed scale/camera and resumed play.
Native units/metre changed 85.088 -> 208.000 -> 85.088, closer 0.30 -> 0.44 -> 0.30;
runtime IPD remained 0.064 m. Submitted Room panel yaw was -20.31 degrees at 2 m,
leaving the page's inner edge about 10 degrees right of the forward view.
The reconstructed layer montage uses untouched RGBA dumps and logged poses;
it is an inspection aid, not a SteamVR compositor screenshot.

The separate dialog diagnostic reached cutscene 162 / camera mode 11, a
3.6 x 2.025 m cinema screen and 1.777778 picture aspect in both eyes, then
restored camera mode 16 and stereo gameplay. The real King Whomp introduction
remains a wear check. These synthetic-head runs had zero tracked stereo frames;
physical grip input, tracked recentring, readability and comfort are unworn.

The first attempt launched over an existing Mario process and failed in the
unchanged OpenXR startup (invalid handle), then the flat fallback. The failed
log/coredump is retained; the clean-idle retry passed without a source change.
Future launch gates must stop on an active scene process. The helper restored
the original launch arguments and user data, including configuration sidecars;
active config and save hashes match their pre-run values.

| Current increment | Evidence level |
| --- | --- |
| Package, integrated preview and synthetic dialog/cinema transitions | verified unattended |
| Physical modifier, tracked recentring, panel readability, comfort, real boss introduction | not worn |

Evidence: task output `device-camera-migration/SM64VR/`,
`SM64VR-settings-composed.png`, and the cross-port delivery report. Evidence-only
documentation commits do not change the installed package's source identity.
