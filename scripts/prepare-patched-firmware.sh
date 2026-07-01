#!/bin/zsh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FW_DIR="$ROOT/build/firmware"
OFFICIAL_DIR="$FW_DIR/official"
mkdir -p "$OFFICIAL_DIR"

OFFICIAL_FIRMWARE="${OFFICIAL_FIRMWARE:-}"

if [[ -z "$OFFICIAL_FIRMWARE" ]]; then
  echo "No OFFICIAL_FIRMWARE path supplied."
  echo "Trying to download 8BitDo Ultimate 2C Wireless controller firmware v1.09 with tools/firmware/8bitdo-firmware.py..."
  if ! python3 -c 'import requests' 2>/dev/null; then
    echo "Python package 'requests' is required for automatic download." >&2
    echo "Install it or rerun with OFFICIAL_FIRMWARE=/path/to/official-v1.09.dat." >&2
    exit 1
  fi
  (
    cd "$OFFICIAL_DIR"
    python3 "$ROOT/tools/firmware/8bitdo-firmware.py" -f 75 109
  )
  OFFICIAL_FIRMWARE="$(find "$OFFICIAL_DIR" -maxdepth 1 -type f -name '*.dat' | sort | head -n 1)"
fi

if [[ -z "$OFFICIAL_FIRMWARE" || ! -f "$OFFICIAL_FIRMWARE" ]]; then
  echo "Official firmware .dat not found." >&2
  echo "Either set OFFICIAL_FIRMWARE=/path/to/official-v1.09.dat or put the official .dat in:" >&2
  echo "  $OFFICIAL_DIR" >&2
  exit 1
fi

LEN4="$FW_DIR/ultimate2c-controller-v1.09-bt-output-len4.dat"
ID1="$FW_DIR/ultimate2c-controller-v1.09-bt-output-id1.dat"
FINAL="$FW_DIR/ultimate2c-controller-v1.09-bt-rumble-proportional-latch30.dat"

python3 "$ROOT/tools/firmware/patch_ultimate2c_bt_rumble_len.py" "$OFFICIAL_FIRMWARE" "$LEN4"
python3 "$ROOT/tools/firmware/patch_ultimate2c_bt_output_id1.py" "$LEN4" "$ID1"
python3 "$ROOT/tools/firmware/patch_ultimate2c_bt_rumble_proportional.py" "$ID1" "$FINAL"

echo
echo "Prepared patched firmware:"
echo "  $FINAL"
echo
echo "This script does NOT flash the controller."
echo "Read docs/Firmware.md before flashing anything."
