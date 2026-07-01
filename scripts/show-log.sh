#!/bin/zsh
set -euo pipefail

tail -n "${1:-240}" /tmp/8bitdo-sdlshim.log

