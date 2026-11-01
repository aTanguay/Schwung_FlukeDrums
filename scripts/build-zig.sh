#!/usr/bin/env bash
# Recommended build. Cross-compiles dsp.so for the Move (aarch64 Linux) with
# Zig, pinned to glibc 2.28 so it can't depend on anything newer than the
# device has. Works on macOS (Intel or Apple Silicon) and Linux. No Docker.
#
#   pip install ziglang        # once (or: brew install zig, then ZIG=zig)
#   ./scripts/build-zig.sh     # -> dist/flukedrums-module.tar.gz
#
# This is the same build CI uses for releases.
set -euo pipefail
cd "$(dirname "$0")/.."
ZIG="${ZIG:-python3 -m ziglang}"
mkdir -p build && rm -rf dist/flukedrums && mkdir -p dist/flukedrums
$ZIG c++ -target aarch64-linux-gnu.2.28 -O3 -shared -fPIC -std=c++14 -pthread \
    src/dsp/mrdrums_plugin.cpp src/dsp/mrdrums_engine.cpp src/dsp/mrdrums_params.cpp src/dsp/reroll.cpp \
    -Isrc/dsp -o build/dsp.so -lm -Wl,--version-script=scripts/exports.map -s
cp src/module.json src/help.json README.md dist/flukedrums/
cp build/dsp.so dist/flukedrums/dsp.so
(cd dist && COPYFILE_DISABLE=1 tar -czf flukedrums-module.tar.gz flukedrums)
echo "Built dist/flukedrums-module.tar.gz"
echo "Install: upload it at http://move.local:7700 -> Install Custom Module, or run ./scripts/install.sh"
