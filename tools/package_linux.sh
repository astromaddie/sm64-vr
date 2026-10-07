#!/usr/bin/env bash
# Stages the Linux VR build for the Steam Frame, run inside the build container:
#
#   tools/package_linux.sh <build-dir> <out-dir>
#
# The package holds the sm64coopdx binary (RPATH $ORIGIN/lib), the resources it loads from
# beside itself (lang/, mods/, palettes/, dynos/), the shared libraries it needs that the
# Frame's Steam Linux Runtime does not guarantee (see check_linux_abi.py), the launcher, a
# BUILD-METADATA.json, and nothing else. It never contains the ROM, extracted assets or saves:
# those live in $XDG_DATA_HOME/SM64VR/.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:?build dir}"
out="${2:?output dir}"

rm -rf "$out"
mkdir -p "$out/lib"
cp "$build/sm64coopdx" "$out/sm64coopdx"
for dir in lang mods palettes dynos; do
	[ -d "$build/$dir" ] && cp -R "$build/$dir" "$out/$dir"
done
cp "$root/tools/steam_frame/LAUNCH-STEAM-FRAME.sh" "$out/LAUNCH-STEAM-FRAME.sh"
chmod +x "$out/sm64coopdx" "$out/LAUNCH-STEAM-FRAME.sh"

# The libraries the binary needs that are not system libraries
for lib in $(python3 "$root/tools/check_linux_abi.py" --list-unbundled "$out/sm64coopdx"); do
	path="$(ldd "$out/sm64coopdx" | awk -v lib="$lib" '$1 == lib { print $3 }')"
	[ -n "$path" ] || { echo "cannot find $lib" >&2; exit 1; }
	cp -L "$path" "$out/lib/$lib"
done

# Package allowlist: no ROMs, game data or saves
if find "$out" \( -iname '*.z64' -o -iname '*.n64' -o -iname '*.v64' -o -iname '*.sav' -o -iname 'sm64config*.txt' \) | grep -q .; then
	echo "package contains a ROM, save or config" >&2
	exit 1
fi

# build-linux.sh resolves this on the host because a managed checkout's gitdir may be
# outside the Docker bind mount. Direct invocations still resolve the mounted/local git tree.
if [[ -n "${SOURCE_REVISION:-}" || -n "${SOURCE_TREE:-}" ]]; then
	rev="${SOURCE_REVISION:-}"
	dirty="${SOURCE_TREE:-}"
else
	rev="$(git -C "$root" rev-parse --verify HEAD^{commit})" || {
		echo "cannot resolve source revision" >&2
		exit 1
	}
	status="$(git -C "$root" status --porcelain --untracked-files=all)" || {
		echo "cannot resolve source tree state" >&2
		exit 1
	}
	if [[ -n "$status" ]]; then dirty=dirty; else dirty=clean; fi
fi
[[ "$rev" =~ ^[0-9a-fA-F]{40,64}$ ]] || { echo "invalid or missing source revision" >&2; exit 1; }
[[ "$dirty" == clean || "$dirty" == dirty ]] || { echo "invalid or missing source tree state" >&2; exit 1; }
{
	echo "{"
	echo "  \"title\": \"SM64VR\","
	echo "  \"source_revision\": \"$rev\","
	echo "  \"tree\": \"$dirty\","
	echo "  \"compiler\": \"$(${CC:-gcc} --version | head -1)\","
	echo "  \"openxr_loader\": \"$(cat /opt/openxr/PINNED 2>/dev/null || echo unknown)\","
	echo "  \"files\": {"
	(cd "$out" && find . -type f ! -name BUILD-METADATA.json | sort | while read -r f; do
		printf '    "%s": "%s",\n' "${f#./}" "$(sha256sum "$f" | cut -d' ' -f1)"
	done) | sed '$ s/,$//'
	echo "  }"
	echo "}"
} > "$out/BUILD-METADATA.json"
ls -l "$out" "$out/lib"
