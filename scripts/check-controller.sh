#!/bin/zsh
set -euo pipefail

system_profiler SPBluetoothDataType 2>/dev/null | \
  rg -i -C 4 '8BitDo|Ultimate|2C|Connected|Not Connected' || true

