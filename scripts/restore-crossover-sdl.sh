#!/bin/zsh
set -euo pipefail

CROSSOVER_APP="${CROSSOVER_APP:-/Applications/CrossOver.app}"
SDL_DIR="$CROSSOVER_APP/Contents/SharedSupport/CrossOver/lib64"
SDL="$SDL_DIR/libSDL2-2.0.0.dylib"
REAL="$SDL_DIR/libSDL2-2.0.0.8bitdo-real.dylib"

if [[ ! -f "$REAL" ]]; then
  echo "No saved real SDL copy found at:"
  echo "  $REAL"
  echo "Nothing restored."
  exit 1
fi

cp "$REAL" "$SDL"
rm -f "$REAL"

codesign --force --deep --sign - "$CROSSOVER_APP"
codesign --verify --deep --strict "$CROSSOVER_APP"

echo "Restored CrossOver's SDL and removed the shim copy."

