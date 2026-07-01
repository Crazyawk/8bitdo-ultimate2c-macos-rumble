#!/bin/zsh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FIRMWARE="${1:-$ROOT/build/firmware/ultimate2c-controller-v1.09-bt-rumble-proportional-latch30.dat}"

if [[ "${I_UNDERSTAND_BRICK_RISK:-}" != "1" ]]; then
  echo "Refusing to flash by default."
  echo
  echo "Firmware flashing can brick the controller."
  echo "Read docs/Firmware.md first, put the controller in BOOT mode, then run:"
  echo
  echo "  I_UNDERSTAND_BRICK_RISK=1 $0 \"$FIRMWARE\""
  echo
  exit 2
fi

if [[ ! -f "$FIRMWARE" ]]; then
  echo "Patched firmware not found: $FIRMWARE" >&2
  echo "Run ./scripts/prepare-patched-firmware.sh first." >&2
  exit 1
fi

"$ROOT/scripts/build-firmware-tools.sh"
"$ROOT/build/ultimate2c_boot_flash" "$FIRMWARE"

