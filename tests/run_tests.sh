#!/usr/bin/env bash
# Host-side tests for FlukeDrums. Builds the real plugin sources natively and
# runs the workflow test twice: under ASan+UBSan (memory/leaks) and TSan (races).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${TMPDIR:-/tmp}/flukedrums-tests"
mkdir -p "$OUT"
cd "$REPO_ROOT"

# LeakSanitizer only exists on Linux; on macOS asking for it aborts ASan.
if [ "$(uname)" = "Linux" ]; then LEAKS=1; else LEAKS=0; fi
CXX="${CXX:-c++}"

SRCS="tests/reroll_workflow_test.cpp src/dsp/mrdrums_plugin.cpp src/dsp/mrdrums_engine.cpp src/dsp/mrdrums_params.cpp src/dsp/reroll.cpp"

build() {  # name, extra flags
    local name="$1"; shift
    local root="$OUT/fixture-$name"
    "$CXX" -std=c++14 -g -O1 -pthread -Isrc/dsp \
        -DTEST_ROOT="\"$root\"" \
        -DREROLL_COMMIT_ROOT="\"$root/pool/Schwung/FlukeDrums\"" \
        -DREROLL_ROOT_MY="\"$root/pool\"" -DREROLL_ROOT_CORE="\"$root/longpool\"" \
        "$@" $SRCS -o "$OUT/reroll_test_$name" -lm
}

echo "== engine only (reroll.cpp, fake loader)"
for SAN in "address,undefined" "thread"; do
    "$CXX" -std=c++14 -g -O1 -pthread -Isrc/dsp -fsanitize=$SAN -fno-omit-frame-pointer \
        -DTEST_ROOT="\"$OUT/engine-${SAN%%,*}\"" \
        tests/reroll_engine_test.cpp src/dsp/reroll.cpp -o "$OUT/engine_${SAN%%,*}"
    ASAN_OPTIONS=detect_leaks=$LEAKS:halt_on_error=1 TSAN_OPTIONS=halt_on_error=1 "$OUT/engine_${SAN%%,*}"
done

echo "== build: asan+ubsan"
build asan -fsanitize=address,undefined -fno-omit-frame-pointer
echo "== build: tsan"
build tsan -fsanitize=thread

echo "== run: asan+ubsan"
ASAN_OPTIONS=detect_leaks=$LEAKS:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    "$OUT/reroll_test_asan"
echo "== run: tsan"
TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1 "$OUT/reroll_test_tsan"

echo "== all FlukeDrums tests passed"
