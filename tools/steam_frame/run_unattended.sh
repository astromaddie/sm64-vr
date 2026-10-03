#!/usr/bin/env bash
# Runs the installed SM64VR title on the Frame without a wearer, for diagnostics: started over
# SSH under the user's systemd manager (logind kills SSH-scope processes on disconnect), with
# force_render (draw and run the sim in standby, synthetic head at 1.6 m), for N seconds, then
# stopped. Prints the log and pulls vr-dumps/ (eye, HUD and panel PNGs plus vr-state.txt).
# An SSH launch gets no controllers; this is never an input test.
#
#   tools/steam_frame/run_unattended.sh [--seconds N] [--out DIR] [--env KEY=VALUE ...] [-- game args]
#
#   --seconds N   how long to let it run (default 40)
#   --out DIR     where to pull vr-dumps/ (default build/device-dumps/<timestamp>)
#   --env K=V     extra environment, e.g. --env SM64_VR_DIAG_YAW=90 --env SM64_VR_DUMP_FRAME=900
#   game args     passed to sm64coopdx, e.g. -- --server 7777 --skip-intro
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
frame_host="${FRAME_HOST:-steamos@frame.local}"
remote_dir="devkit-game/SM64VR"
seconds=40
out=""
envs=(SM64_VR_FORCE_RENDER=1)
args=()

while (($#)); do
	case "$1" in
		--seconds) seconds="$2"; shift 2 ;;
		--out) out="$2"; shift 2 ;;
		--env) envs+=("$2"); shift 2 ;;
		--) shift; args=("$@"); break ;;
		*) echo "unknown option $1" >&2; exit 2 ;;
	esac
done
out="${out:-$root/build/device-dumps/$(date +%Y%m%d-%H%M%S)}"
ssh_opts=(-o ConnectTimeout=5 -o BatchMode=yes)
unit="sm64vr-diag"

env_flags=""
for e in "${envs[@]}"; do
	env_flags+=" -E $(printf '%q' "$e")"
done
arg_str=""
for a in ${args[@]+"${args[@]}"}; do
	arg_str+=" $(printf '%q' "$a")"
done

ssh "${ssh_opts[@]}" "$frame_host" "systemctl --user stop $unit 2>/dev/null; systemctl --user reset-failed $unit 2>/dev/null; rm -rf ~/.local/share/SM64VR/vr-dumps; cd ~/$remote_dir && systemd-run --user --unit=$unit --collect -p WorkingDirectory=\$HOME/$remote_dir -E DISPLAY=:0 -E SM64_VR_LOG=run-diag.txt$env_flags ./LAUNCH-STEAM-FRAME.sh$arg_str"
echo "running for $seconds s ..."
sleep "$seconds"
ssh "${ssh_opts[@]}" "$frame_host" "systemctl --user stop $unit 2>/dev/null; sleep 2; cat ~/$remote_dir/run-diag.txt" > /tmp/sm64vr-run-diag.txt || true
mkdir -p "$out"
cp /tmp/sm64vr-run-diag.txt "$out/run-diag.txt"
rsync -a "$frame_host:.local/share/SM64VR/vr-dumps/" "$out/" 2>/dev/null || echo "(no vr-dumps)"
grep -E '^\[vr|^\[launch|vr-perf|error|Error|fatal' "$out/run-diag.txt" | head -80 || true
echo "log and dumps in $out"
