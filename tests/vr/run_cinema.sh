#!/usr/bin/env bash
# Run inside sm64-vr-linux, from the repository root. No OpenXR runtime needed.
set -euo pipefail
test_binary="$(mktemp /tmp/sm64vr-cinema-test.XXXXXX)"
trap 'rm -f "$test_binary"' EXIT
test_flags=(-std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -DSM64_VR
    -ffunction-sections -fdata-sections -Isrc/pc/vr
    -Ithird_party/sfvr/include -Ithird_party/sfvr/tests -I/opt/openxr/include)
test_sources=(src/pc/vr/vr_cinema.c src/pc/vr/vr_cam.c src/pc/vr/vr_math.c
    src/pc/vr/vr_stage.c src/pc/vr/vr_compose.c src/pc/vr/vr_xr.c
    src/pc/vr/vr_settings.c src/pc/vr/vr_log.c src/pc/vr/vr_paths.c
    third_party/sfvr/src/*.c)
for name in cinema cam math compose settings; do
    cc "${test_flags[@]}" "tests/vr/test_vr_${name}.c" "${test_sources[@]}" \
        -Wl,--gc-sections -o "$test_binary" -lm
    "$test_binary"
done
cc "${test_flags[@]}" -Wno-missing-field-initializers -DVR_CINEMA_RENDER_TEST \
    -DVERSION_US=1 -DF3DEX_GBI_2E=1 -DF3DEX_GBI_SHARED=1 -D_LANGUAGE_C \
    -Iinclude -Isrc -I. tests/vr/test_vr_cinema.c \
    src/pc/vr/vr_render.c src/pc/vr/vr_game.c "${test_sources[@]}" \
    -Wl,--gc-sections -o "$test_binary" -lm -lGL -lSDL2
SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 "$test_binary"
