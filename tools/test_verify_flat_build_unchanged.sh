#!/usr/bin/env bash
# Lightweight smoke test for the flat-build verifier's Git snapshots and hash gate.
# The fake Docker command validates the invocation and writes a tiny stand-in executable;
# no container, compiler or real game build is run.
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/sm64-vr-flat-smoke.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
fixture="$tmp/repo"
mkdir -p "$fixture/tools" "$fixture/src/pc/vr"
cp "$repo/tools/verify_flat_build_unchanged.sh" "$fixture/tools/verify_flat_build_unchanged.sh"
chmod +x "$fixture/tools/verify_flat_build_unchanged.sh"
cat > "$fixture/.gitignore" <<'IGNORE'
build/
IGNORE
printf 'base source\n' > "$fixture/src/pc/vr/old.c"
git -C "$fixture" init --quiet
git -C "$fixture" config user.email smoke@example.invalid
git -C "$fixture" config user.name 'Verifier Smoke Test'
git -C "$fixture" add .
git -C "$fixture" commit -m baseline --quiet
baseline="$(git -C "$fixture" rev-parse HEAD)"

cat > "$tmp/docker-mock" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
[[ "${1:-}" == run ]] || { echo "expected docker run" >&2; exit 21; }
shift
[[ "${1:-}" == --rm ]] || { echo "expected --rm" >&2; exit 22; }
shift
[[ "${1:-}" == -v ]] || { echo "expected bind mount" >&2; exit 23; }
mount="$2"
shift 2
[[ "${1:-}" == -w && "${2:-}" == /src ]] || { echo "expected /src workdir" >&2; exit 24; }
shift 2
[[ "${1:-}" == "${MOCK_IMAGE:-smoke-image}" ]] || { echo "wrong image" >&2; exit 25; }
shift
[[ "${1:-}" == make && "${2:-}" == -j1 && "${3:-}" == VR=0 && "${4:-}" == DEVELOPMENT=0 && "${5:-}" == DEBUG=0 && "${6:-}" == DEBUG_INFO_LEVEL=0 && $# == 6 ]] || {
  echo "wrong flat build command: $*" >&2
  exit 26
}
tree="${mount%:/src}"
printf 'mock build log\n'
if [[ -f "$tree/src/pc/vr/vr_preview.c" ]]; then
  printf 'candidate\n' >> "$MOCK_LOG"
  value=baseline-bytes
  [[ "${MOCK_MISMATCH:-0}" != 1 ]] || value=candidate-bytes
else
  printf 'baseline\n' >> "$MOCK_LOG"
  value=baseline-bytes
fi
mkdir -p "$tree/build/us_pc"
executable="$tree/build/us_pc/sm64coopdx${MOCK_SUFFIX:-}"
printf '%s\n' "$value" > "$executable"
chmod +x "$executable"
MOCK
chmod +x "$tmp/docker-mock"

run_verifier() {
  DOCKER_BIN="$tmp/docker-mock" SM64_VR_IMAGE=smoke-image JOBS=1 MOCK_LOG="$tmp/docker.log" \
    "$fixture/tools/verify_flat_build_unchanged.sh" "$baseline"
}

# A clean candidate has an empty patch; both snapshots still build and compare.
: > "$tmp/docker.log"
run_verifier > "$tmp/clean.out" 2> "$tmp/clean.err"
rg -q 'flat build matches baseline' "$tmp/clean.out"
! rg -q 'mock build log' "$tmp/clean.out"
rg -q 'mock build log' "$tmp/clean.err"
[[ "$(cat "$tmp/docker.log")" == $'baseline\nbaseline' ]]

# Mix staged and unstaged changes; the newly staged preview source must reach candidate scratch.
printf 'staged change\n' > "$fixture/src/pc/vr/old.c"
printf 'preview source\n' > "$fixture/src/pc/vr/vr_preview.c"
git -C "$fixture" add src/pc/vr/old.c src/pc/vr/vr_preview.c
printf 'unstaged change\n' >> "$fixture/src/pc/vr/old.c"
: > "$tmp/docker.log"
run_verifier > "$tmp/dirty.out" 2> "$tmp/dirty.err"
rg -q 'flat build matches baseline' "$tmp/dirty.out"
[[ "$(cat "$tmp/docker.log")" == $'baseline\ncandidate' ]]

# The Linux ARM flat target uses the .arm executable name.
MOCK_SUFFIX=.arm run_verifier > "$tmp/arm.out" 2> "$tmp/arm.err"
rg -q 'flat build matches baseline' "$tmp/arm.out"

# Candidate byte changes must fail the parity gate.
: > "$tmp/docker.log"
if DOCKER_BIN="$tmp/docker-mock" SM64_VR_IMAGE=smoke-image JOBS=1 MOCK_LOG="$tmp/docker.log" MOCK_MISMATCH=1 \
  "$fixture/tools/verify_flat_build_unchanged.sh" "$baseline" > "$tmp/mismatch.out" 2> "$tmp/mismatch.err"; then
  echo 'mismatching candidate unexpectedly passed' >&2
  exit 1
fi
rg -q 'flat build changed from baseline' "$tmp/mismatch.err"
[[ "$(cat "$tmp/docker.log")" == $'baseline\ncandidate' ]]
if [[ -d "$fixture/build" ]] && find "$fixture/build" -maxdepth 1 -name '.flat-verify.*' -print -quit | rg -q .; then
  echo 'verifier left scratch directories behind' >&2
  exit 1
fi
printf 'flat verifier smoke: clean snapshot, staged/unstaged candidate snapshot, command flags, ARM filename, mismatch gate and cleanup passed\n'
