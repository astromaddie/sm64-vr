#!/usr/bin/env bash
# Copies the VR build to a Steam Frame and registers it as the devkit title SM64VR, which then
# shows in the Frame's Steam library.
#
#   tools/steam_frame/deploy.sh [--package DIR] [--rom FILE] [--config KEY=VALUE ...]
#                               [--no-register] [--logs] [--dry-run]
#
#   --package DIR    the staged package (default build/package/SM64VR, made by
#                    `tools/docker/build-linux.sh package`)
#   --rom FILE       also copy the user's own US ROM (sha1 in sm64.us.sha1, checked first) to
#                    ~/.local/share/SM64VR/ on the Frame. Never put it in git.
#   --config K=V     set a key in the Frame's vr.toml [vr] table, e.g. --config probe_seconds=10
#   --launch-args A  extra arguments for the registered title, e.g. --launch-args "--server 7777
#                    --skip-intro" (host a single-player game straight away, skipping the menu)
#   --no-register    only copy files
#   --logs           print the last run's log (run-stdout.txt) and exit
#   --dry-run        print what would run, change nothing
#
# FRAME_HOST (default steamos@frame.local; the USB link is steamos@10.86.200.233). The
# title is a native Linux ARM64 title: compat tool SteamLinuxRuntime_4-arm64, steam_play=0.
# Start it from the Steam library; a binary started over SSH sees no controllers.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
frame_host="${FRAME_HOST:-steamos@frame.local}"
title="SM64VR"
remote_dir="devkit-game/$title"
package="$root/build/package/$title"
rom=""
register=1
logs=0
dry=0
settings=()
launch_args=""

while (($#)); do
	case "$1" in
		--package) package="$2"; shift 2 ;;
		--rom) rom="$2"; shift 2 ;;
		--config) settings+=("$2"); shift 2 ;;
		--launch-args) launch_args="$2"; shift 2 ;;
		--no-register) register=0; shift ;;
		--logs) logs=1; shift ;;
		--dry-run) dry=1; shift ;;
		*) echo "unknown option $1" >&2; exit 2 ;;
	esac
done

run() {
	if ((dry)); then
		printf '+'; printf ' %q' "$@"; printf '\n'
	else
		"$@"
	fi
}

ssh_opts=(-o ConnectTimeout=5 -o BatchMode=yes)

if ((logs)); then
	exec ssh "${ssh_opts[@]}" "$frame_host" "cat ~/$remote_dir/run-stdout.txt"
fi

[[ -x "$package/sm64coopdx" && -x "$package/LAUNCH-STEAM-FRAME.sh" ]] || {
	echo "$package is not a package (run tools/docker/build-linux.sh package)" >&2
	exit 1
}
if [[ -n "$rom" ]]; then
	[[ -f "$rom" ]] || { echo "$rom is not a file" >&2; exit 1; }
	want="$(cut -d' ' -f1 "$root/sm64.us.sha1")"
	have="$(shasum -a 1 "$rom" | cut -d' ' -f1)"
	[[ "$want" == "$have" ]] || { echo "$rom is not the US ROM (sha1 $have, want $want)" >&2; exit 1; }
fi

if ! ((dry)); then
	ssh "${ssh_opts[@]}" "$frame_host" true 2>/dev/null || {
		echo "The Frame is not reachable at $frame_host (USB cable, charge, ssh key)." >&2
		exit 1
	}
fi

# macOS rsync is 2.6: no --info. -a keeps the executable bits.
run ssh "${ssh_opts[@]}" "$frame_host" "mkdir -p ~/$remote_dir"
run rsync -a --delete --partial --exclude 'run-*.txt' "$package/" "$frame_host:$remote_dir/"

if [[ -n "$rom" ]]; then
	run ssh "${ssh_opts[@]}" "$frame_host" 'mkdir -p ~/.local/share/SM64VR'
	run rsync -a --partial "$rom" "$frame_host:.local/share/SM64VR/baserom.us.z64"
fi

# vr.toml is written by the game on its first start; a key edited here is replaced in place or
# appended to [vr]. The same file layout as src/pc/vr/vr_settings.c writes.
for setting in ${settings[@]+"${settings[@]}"}; do
	key="${setting%%=*}"
	value="${setting#*=}"
	[[ "$key" =~ ^[a-z0-9_]+$ ]] || { echo "bad key $key" >&2; exit 2; }
	quoted_value="$(printf '%q' "$value")"
	run ssh "${ssh_opts[@]}" "$frame_host" python3 - "\$HOME/.local/share/SM64VR/vr.toml" "$key" "$quoted_value" <<'PY'
import os, re, sys
path, key, value = sys.argv[1:]
text = open(path).read() if os.path.exists(path) else ""
line = "%s = %s" % (key, value)
pattern = re.compile(r"(^\[vr\]\n(?:(?!\[).*\n)*?)^%s[ \t]*=.*$" % re.escape(key), re.M)
if pattern.search(text):
    text = pattern.sub(lambda m: m.group(1) + line, text, count=1)
elif re.search(r"^\[vr\]$", text, re.M):
    text = re.sub(r"^\[vr\]$", "[vr]\n" + line, text, count=1, flags=re.M)
else:
    text += "\n[vr]\n" + line + "\n"
os.makedirs(os.path.dirname(path), exist_ok=True)
open(path, "w").write(text)
print("vr.toml: " + line)
PY
done

if ((register)); then
	# The devkit service's own shortcut tool, as the other native Frame ports use it. The title name
	# must be alphanumeric: "SM64-VR" would be rejected, "SM64VR" is accepted.
	argv='"LAUNCH-STEAM-FRAME.sh"'
	for a in $launch_args; do
		[[ "$a" =~ ^[A-Za-z0-9._-]+$ ]] || { echo "bad launch argument $a" >&2; exit 2; }
		argv+=', "'$a'"'
	done
	parms='{"gameid": "'$title'", "directory": "/home/steamos/'$remote_dir'", "force_appid": "", "argv": ['$argv'], "env": {}, "settings": {"steam_play": "0", "compat_tool": "SteamLinuxRuntime_4-arm64"}, "lepton_args": ""}'
	run ssh "${ssh_opts[@]}" "$frame_host" "cd ~/devkit-utils && python3 steam-client-create-shortcut --parms '$parms'"
	echo "Registered $title. Start it from the Frame's Steam library; read the log with: $0 --logs"
fi
