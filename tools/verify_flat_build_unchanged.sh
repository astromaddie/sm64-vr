#!/usr/bin/env bash
# Build the pre-preview baseline and current source independently in the pinned Linux image,
# then require the flat executable bytes to match. Run from the host so Git works even when
# the checkout's gitdir is outside Docker's source mount.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
image="${SM64_VR_IMAGE:-sm64-vr-linux}"
docker_bin="${DOCKER_BIN:-docker}"
jobs="${JOBS:-8}"
default_baseline="fcb49579d0ed74b5d3860c2039b488a22c76a765"
baseline_ref="${1:-${SM64_VR_FLAT_BASELINE:-$default_baseline}}"

command -v git >/dev/null || { echo "git is required on the host" >&2; exit 1; }
command -v "$docker_bin" >/dev/null || { echo "docker is required: $docker_bin" >&2; exit 1; }
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo "JOBS must be a positive integer" >&2; exit 2; }

baseline="$(git -C "$root" rev-parse --verify "${baseline_ref}^{commit}")" || {
	echo "cannot resolve flat-build baseline: $baseline_ref" >&2
	exit 1
}
git -C "$root" rev-parse --verify HEAD^{commit} >/dev/null || {
	echo "cannot resolve candidate HEAD" >&2
	exit 1
}

had_build_dir=0
[[ -d "$root/build" ]] && had_build_dir=1
mkdir -p "$root/build"
scratch="$(mktemp -d "$root/build/.flat-verify.XXXXXX")"
cleanup() {
	rm -rf "$scratch"
	[[ "$had_build_dir" == 1 ]] || rmdir "$root/build" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM
baseline_tree="$scratch/baseline"
candidate_tree="$scratch/candidate"
mkdir -p "$baseline_tree" "$candidate_tree"

git -C "$root" archive "$baseline" | tar -xf - -C "$baseline_tree"
git -C "$root" archive HEAD | tar -xf - -C "$candidate_tree"
# Make the archive a repository so Git applies diff paths relative to this isolated root,
# even though scratch lives below the source checkout's ignored build directory.
git -C "$candidate_tree" init --quiet
# Include staged and unstaged tracked changes so the candidate reflects the working tree.
patch_file="$scratch/working-tree.patch"
git -C "$root" diff --binary HEAD > "$patch_file"
if [[ -s "$patch_file" ]]; then
	git -C "$candidate_tree" apply --binary "$patch_file"
fi
# Include ordinary untracked source files too. Ignored game data, ROMs and build caches are
# excluded by Git's standard ignore rules and never enter either scratch tree.
while IFS= read -r -d '' rel; do
	case "$rel" in
		*.z64|*.n64|*.v64|*.sav|sm64config*.txt) continue ;;
	esac
	dest="$candidate_tree/$rel"
	mkdir -p "$(dirname "$dest")"
	if [[ -L "$root/$rel" ]]; then
		ln -s "$(readlink "$root/$rel")" "$dest"
	else
		cp -p "$root/$rel" "$dest"
	fi
done < <(git -C "$root" ls-files --others --exclude-standard -z)

build_flat() {
	local tree="$1"
	local executable="$tree/build/us_pc/sm64coopdx"
	# DEBUG=0 still includes line-table debug info by default. Disable it explicitly:
	# VR-only additions change source line numbers even when the flat code is identical.
	"$docker_bin" run --rm -v "$tree":/src -w /src "$image" \
		make -j"$jobs" VR=0 DEVELOPMENT=0 DEBUG=0 DEBUG_INFO_LEVEL=0 >&2
	# The flat Linux ARM build uses the Raspberry Pi target's .arm filename.
	[[ -x "$executable" ]] || executable="$tree/build/us_pc/sm64coopdx.arm"
	[[ -x "$executable" ]] || {
		echo "flat build did not produce build/us_pc/sm64coopdx[.arm] ($tree)" >&2
		return 1
	}
	shasum -a 256 "$executable" | awk '{print $1}'
}

printf 'flat baseline: %s (%s)\n' "$baseline_ref" "$baseline"
baseline_hash="$(build_flat "$baseline_tree")"
printf 'flat candidate: %s\n' "$(git -C "$root" rev-parse --short HEAD) plus working tree"
candidate_hash="$(build_flat "$candidate_tree")"
printf 'baseline sha256:  %s\ncandidate sha256: %s\n' "$baseline_hash" "$candidate_hash"
[[ "$baseline_hash" == "$candidate_hash" ]] || {
	echo "flat build changed from baseline $baseline" >&2
	exit 1
}
echo "flat build matches baseline"
