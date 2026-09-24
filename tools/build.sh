#!/usr/bin/env bash
# Builds the simulation core, its tests and tools, the GDExtension, and exports the release binary.
# Needs: cmake, a C++20 compiler, and Godot 4.7.2-stable with its export templates (GODOT=/path/to/godot).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GODOT="${GODOT:-godot}"
JOBS="${JOBS:-$(nproc)}"
cd "$ROOT"
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DWORMFARM_BUILD_EXTENSION=ON
cmake --build build -j"$JOBS" --target wormfarm_core wormfarm_tests wormfarm_sim wormfarm_gdext
"$GODOT" --headless --path game --import >/dev/null 2>&1 || true
mkdir -p dist/shadow-worm-farm
"$GODOT" --headless --path game --export-release Linux "$ROOT/dist/shadow-worm-farm/shadow-worm-farm.x86_64"
cp packaging/shadow-worm-farm.svg dist/shadow-worm-farm/
cp LICENSE README.md dist/shadow-worm-farm/ 2>/dev/null || true
echo "Exported to dist/shadow-worm-farm/"
ls -la dist/shadow-worm-farm/
