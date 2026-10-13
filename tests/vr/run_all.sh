#!/usr/bin/env bash
# Builds and runs every host test in tests/vr inside the arm64 Docker image (or any Linux with the
# OpenXR headers in /opt/openxr), then the cinema renderer test (run_cinema.sh, Mesa GL).
#   docker run --rm -v "$PWD":/src -w /src sm64-vr-linux bash tests/vr/run_all.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
bin="$(mktemp /tmp/sm64vr-test.XXXXXX)"
trap 'rm -f "$bin"' EXIT
flags=(-std=c11 -D_GNU_SOURCE -Wall -Wextra -DSM64_VR -ffunction-sections -fdata-sections
	-Isrc/pc/vr -Ithird_party/sfvr/include -Ithird_party/sfvr/tests -I/opt/openxr/include)
pure=(src/pc/vr/vr_cam.c src/pc/vr/vr_cinema.c src/pc/vr/vr_clock.c src/pc/vr/vr_compose.c
	src/pc/vr/vr_log.c src/pc/vr/vr_math.c src/pc/vr/vr_menu.c src/pc/vr/vr_paths.c src/pc/vr/vr_phosphor.c src/pc/vr/vr_preview.c
	src/pc/vr/vr_rate.c src/pc/vr/vr_sched.c src/pc/vr/vr_settings.c src/pc/vr/vr_stage.c
	src/pc/vr/vr_turn.c src/pc/vr/vr_xr.c src/pc/vr/vr_perf.c third_party/sfvr/src/*.c)
failed=0
for t in tests/vr/test_vr_*.c; do
	name="$(basename "$t" .c)"
	extra=()
	[ "$name" = test_vr_input ] && extra=(src/pc/vr/vr_input.c)
	if ! cc "${flags[@]}" "$t" "${pure[@]}" "${extra[@]}" -Wl,--gc-sections -o "$bin" -lm; then
		echo "BUILD FAILED: $name"; failed=1; continue
	fi
	"$bin" | tail -1 || { echo "FAILED: $name"; failed=1; }
done
bash tests/vr/run_cinema.sh | grep -E "cases|FAIL" | tail -1 || failed=1
exit $failed
