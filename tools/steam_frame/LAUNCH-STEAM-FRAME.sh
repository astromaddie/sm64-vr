#!/usr/bin/env bash
# The devkit title SM64VR runs this (deploy.sh registers it). Steam starts it, which is also
# what hands the game the headset: SteamVR owns the controllers, so a binary started from SSH
# gets no input. Logs go to run-stdout.txt beside this script.
#
# The ROM is not part of the package. Put the user's own US ROM (any file name, .z64) in
# $XDG_DATA_HOME/SM64VR/ (default ~/.local/share/SM64VR/); saves, the game's config and
# vr.toml live there too. SM64_VR_<KEY> environment variables override a vr.toml setting,
# e.g. SM64_VR_PROBE_SECONDS=10.
set -euo pipefail

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$here"

# The Frame runs gamescope with Xwayland on :0; the XR OpenGL binding needs SDL's x11 driver.
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}"
export DISPLAY="${DISPLAY:-:0}"

data="${XDG_DATA_HOME:-$HOME/.local/share}/SM64VR"
mkdir -p "$data"
log="${SM64_VR_LOG:-run-stdout.txt}"
{
	echo "[launch] $(date -Is) $here/sm64coopdx $*"
	echo "[launch] data dir $data"
	ls "$data"/*.z64 > /dev/null 2>&1 || echo "[launch] no .z64 ROM in $data: the game will stop at its ROM screen"
} > "$log"

exec ./sm64coopdx "$@" >> "$log" 2>&1
