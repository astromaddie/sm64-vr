# Changelog

## 2026-10-14 — Phosphor mask

- Added `phosphor` (0 to 1, on at 0.5 by default; 0 = off, and the frame is drawn as before) and `phosphor_size` (0.25 degrees; 0.15 to 1.0), shown on the VR settings page under Display as "CRT phosphors" and "Phosphor size". Red, green and blue stripes and scanlines are multiplied over each stereo world eye, read from the direction each pixel looks in, in the room, so both eyes agree and the mask stays put when the head turns. Not applied to the HUD, the menu panel or the cinema screen. Pure tests and ARM64 build; not run on the device, headset-unverified.

## 2026-10-13 — Manual camera setting

- Added `camera_auto_rotate` (VR settings, Comfort: "Auto camera turning"). Off stops Lakitu swinging behind Mario and following where he runs; the view turns only from the right stick or C buttons, and walls still turn and stop the camera. Not worn.

## 2026-10-11 — PR0 source publication preparation

- Prepared this experimental personal fork for public source publication after removing OAuth-token-containing `grind.log` and `log.txt` from reachable `main` history.
- Recorded the history-cleaned source revision and current ROM-free host test results.
- Kept the supplied ROM and local build outputs out of the repository and preserved upstream/component notices.
