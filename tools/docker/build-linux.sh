#!/usr/bin/env bash
# Builds SM64 VR for Linux inside the pinned environment from linux.Dockerfile. The target
# architecture is the Docker host's: arm64 on Apple Silicon, which is the Steam Frame's.
#
#   docker build -t sm64-vr-linux -f tools/docker/linux.Dockerfile tools/docker
#   tools/docker/build-linux.sh [step]
#
# Steps (default: all):
#   on       VR build (make VR=1) into build/us_pc_vr/
#   tests    the host unit tests (tests/vr/: scheduler, rig maths, settings, input, sfvr)
#   package  stage the VR build as build/package/SM64VR: binary, resources, libraries, launcher
#   abi      the ABI check on the staged package (fails above glibc 2.35)
#   verify   compare flat builds before/after the preview (SM64_VR_FLAT_BASELINE overrides)
#   all      on, tests, package, abi, verify
set -euo pipefail

repo="$(cd "$(dirname "$0")/../.." && pwd)"
image="${SM64_VR_IMAGE:-sm64-vr-linux}"
step="${1:-all}"
jobs="${JOBS:-8}"

in_container() {
	docker run --rm -v "$repo":/src -w /src "$image" bash -c "set -euo pipefail; $1"
}

source_revision() {
	local revision
	revision="$(git -C "$repo" rev-parse --verify HEAD^{commit})" || {
		echo "cannot resolve source revision on host" >&2
		return 1
	}
	[[ "$revision" =~ ^[0-9a-fA-F]{40,64}$ ]] || {
		echo "invalid source revision: $revision" >&2
		return 1
	}
	printf '%s\n' "$revision"
}

source_tree_state() {
	local status
	status="$(git -C "$repo" status --porcelain --untracked-files=all)" || {
		echo "cannot resolve source tree state on host" >&2
		return 1
	}
	if [[ -n "$status" ]]; then
		printf 'dirty\n'
	else
		printf 'clean\n'
	fi
}

step_on() {
	in_container "make VR=1 -j$jobs 2>&1 | grep -v 'overriding recipe\|ignoring old recipe' | tail -5; test -x build/us_pc_vr/sm64coopdx"
}

step_tests() {
	in_container "bash tests/vr/run_all.sh"
}

step_package() {
	local revision tree
	revision="$(source_revision)"
	tree="$(source_tree_state)"
	docker run --rm -v "$repo":/src -w /src \
		-e "SOURCE_REVISION=$revision" -e "SOURCE_TREE=$tree" \
		"$image" bash -c 'set -euo pipefail; tools/package_linux.sh build/us_pc_vr build/package/SM64VR > /dev/null && echo staged build/package/SM64VR'
}

step_abi() {
	in_container "python3 tools/check_linux_abi.py --lib-dir build/package/SM64VR/lib build/package/SM64VR/sm64coopdx"
}

step_verify() {
	SM64_VR_IMAGE="$image" JOBS="$jobs" "$repo/tools/verify_flat_build_unchanged.sh"
}

case "$step" in
	on) step_on ;;
	tests) step_tests ;;
	package) step_package ;;
	abi) step_abi ;;
	verify) step_verify ;;
	all) step_on; step_tests; step_package; step_abi; step_verify ;;
	*) echo "unknown step $step" >&2; exit 2 ;;
esac
