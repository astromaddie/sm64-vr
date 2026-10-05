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

- **Refresh rate.** `refresh_rate` defaults to 120. At the READY state the port calls
  `xrEnumerateDisplayRefreshRatesFB` and requests the offered rate nearest the setting with
  `xrRequestDisplayRefreshRateFB`. It logs what was offered and requested, and the runtime's change
  event (`refresh rate: the display changed from A to B Hz`) when SteamVR confirms.
- **Automatic fallback.** `vr_rate.c` counts shown frames in 10 s windows while the headset is worn
  (session FOCUSED and `shouldRender`). Two windows in a row below 90% of the display's rate request the
  next lower offered rate (120, then 90, then 72, never below 72), and log it
  (`refresh rate: falling back from 120 to 90 Hz`). The choice lasts for the run; `vr.toml` is not
  rewritten. Standby, taking the headset off and a rate change start the count over.
  `diag_rate_unworn = true` counts windows without a wearer, for unattended runs.
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
the refresh-rate fallback runs it logs `refresh rate: N fps over 10 s at R Hz (frame period ...)` each window.

## 7. Measurements

Unattended, headset on the desk (`force_render`), launched through Steam as the devkit title,
2026-10-05, Castle Grounds (start of the first level, Mario idle), 2160 x 2160 per eye, no MSAA:

| Item | Result |
| --- | --- |
| Offered by `xrEnumerateDisplayRefreshRatesFB` | 90 Hz only (SteamVR offers an app its per-app rate; this title's `preferredRefreshRate` in `steamvr.vrsettings` is 90) |
| Granted | 90 Hz: frame period 11.11 ms. The request for 120 can only be honoured once the per-app rate is 120 |
| Eye swapchains | 2160 x 2160 each (recommended 1728 x 1728 x 1.25, maximum 8192) |
| Frame rate | 90.0 fps, `missed=0`, in every window of two runs (about 20 windows of 10 s) |
| CPU (`[vr-perf]`) | 2.6 to 2.8 ms: tick 0.12, replay 2.1 (eyes 0.8 and 1.25), hud 0.5 |
| GPU (`[vr-perf]`) | 3.3 ms for both eyes and the HUD at 90 Hz |

3.3 ms GPU and 2.7 ms CPU against an 8.33 ms period at 120 Hz leave room, but 120 Hz itself has not been
measured: no run could get the display above 90 Hz, because the per-app setting is 90. To get 120, set the
title's per-app refresh rate to 120 in SteamVR's own settings (as for the Half-Life title); the file is
read at SteamVR start, so editing it while SteamVR runs does nothing. The port's default request stays
120. The live fallback step (120 to 90 to 72) has not been seen on the device, since only one rate was
offered; its window logic and the rate stepping are unit-tested (`tests/vr/test_vr_rate.c`,
`test_vr_xr.c`). Heavier scenes (Bob-omb Battlefield) were not run: the unattended run has no input.

## 8. Status table

| Feature | State |
| --- | --- |
| 120 Hz request, enumerated rates, change event logged | headset-unverified |
| Automatic fallback 120 / 90 / 72 (window logic unit-tested; the window counting runs on the device at 90 Hz, the step down not seen) | headset-unverified |
| `[vr-perf]` GPU time from timestamp queries | headset-unverified (read unattended) |
| 2160 x 2160 eye swapchains, rendered directly | headset-unverified |
| `refresh_rate` / `resolution_scale` settings and v1 to v2 migration (unit-tested) | headset-unverified |
| Stereo gameplay, display-time scheduler, level pitch | headset-unverified |
| Star select in stereo, warp transitions with a fade quad | headset-unverified |
| fov_mode 3 (the Frame's measured full fov) | headset-unverified |
| Frame controller input, snap or smooth turning, haptics | headset-unverified |
| Menus and HUD on the flat panel and HUD quad | headset-unverified |
