#!/bin/zsh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CROSSOVER_APP="${CROSSOVER_APP:-/Applications/CrossOver.app}"
SDL_DIR="$CROSSOVER_APP/Contents/SharedSupport/CrossOver/lib64"
SDL="$SDL_DIR/libSDL2-2.0.0.dylib"
REAL="$SDL_DIR/libSDL2-2.0.0.8bitdo-real.dylib"

if [[ ! -d "$CROSSOVER_APP" ]]; then
  echo "CrossOver app not found at: $CROSSOVER_APP" >&2
  echo "Set CROSSOVER_APP=/path/to/CrossOver.app and run again." >&2
  exit 1
fi

if [[ ! -f "$SDL" ]]; then
  echo "CrossOver SDL dylib not found at: $SDL" >&2
  exit 1
fi

"$ROOT/scripts/build.sh"

if [[ ! -f "$REAL" ]]; then
  echo "Saving current CrossOver SDL as:"
  echo "  $REAL"
  cp "$SDL" "$REAL"
else
  echo "Existing real SDL copy found:"
  echo "  $REAL"
fi

echo "Installing shim over CrossOver SDL:"
echo "  $SDL"
cp "$ROOT/build/libSDL2-2.0.0.dylib" "$SDL"

echo "Signing shim, real SDL copy, and CrossOver bundle..."
codesign --force --sign - "$SDL"
codesign --force --sign - "$REAL"
codesign --force --deep --sign - "$CROSSOVER_APP"
codesign --verify --deep --strict "$CROSSOVER_APP"

echo "Installed. Fully quit and reopen CrossOver/GTA before testing."
echo "Rumble log: /tmp/8bitdo-sdlshim.log"

