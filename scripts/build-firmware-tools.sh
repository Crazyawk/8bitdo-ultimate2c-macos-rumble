#!/bin/zsh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

make firmware-tools

echo "Built firmware tools:"
echo "  $ROOT/build/ultimate2c_boot_backup"
echo "  $ROOT/build/ultimate2c_boot_flash"

