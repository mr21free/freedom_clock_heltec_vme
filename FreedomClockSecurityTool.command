#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
python3 "$SCRIPT_DIR/tools/freedom_clock_security_tool.py" "$@"

if [[ $# -eq 0 ]]; then
  echo
  echo "Press Enter to close..."
  read
fi
