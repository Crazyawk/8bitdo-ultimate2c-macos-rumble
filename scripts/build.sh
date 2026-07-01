#!/bin/zsh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

make all
codesign --force --sign - "$ROOT/build/libSDL2-2.0.0.dylib"

echo "Built:"
echo "  $ROOT/build/libSDL2-2.0.0.dylib"
echo "  $ROOT/build/sdlshim_curve_ladder"
echo "  $ROOT/build/sdlshim_gta_cancel_test"
echo "  $ROOT/build/bt_raw_all4_ladder"

