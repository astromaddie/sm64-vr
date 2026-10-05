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
into the HUD quad); menus, the intro and transitions that need it use a world-locked flat panel, with
a fade quad over warp transitions. Standby (`shouldRender` false) freezes the simulation and keeps polling.

## 6. Settings and diagnostics

`vr.toml` in `~/.local/share/SM64VR/` (keys from the sfvr registry plus `world_scale`, `level_pitch`,
`star_select_scale`, `force_mono`, `fov_mode`, `panel_distance`, `panel_width` and the diagnostics).
Environment variables `SM64_VR_<KEY>` override a key for one run. Diagnostics: `force_render`
(draw and run in standby with a synthetic head), `diag_yaw`, `dump_frame`, `dump_worn_frame`,
`probe_seconds`, `diag_menu_stereo`, `diag_rate_unworn`, and `timing` for the `[vr-perf]` line every 10 s
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
