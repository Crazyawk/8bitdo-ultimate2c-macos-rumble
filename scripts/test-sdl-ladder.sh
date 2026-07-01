#!/bin/zsh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

"$ROOT/scripts/build.sh"
: > /tmp/8bitdo-sdlshim.log
arch -x86_64 "$ROOT/build/sdlshim_curve_ladder"
echo
tail -120 /tmp/8bitdo-sdlshim.log

