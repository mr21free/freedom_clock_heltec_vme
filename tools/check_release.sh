#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARDUINO_CLI="${ARDUINO_CLI:-/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli}"
BUILD_ROOT="${BUILD_ROOT:-/private/tmp/freedom-clock-release-check}"

cd "$ROOT_DIR"

echo "Checking whitespace with git diff --check..."
git diff --check

echo "Compiling E213 firmware..."
"$ARDUINO_CLI" compile \
  --build-path "$BUILD_ROOT/e213" \
  --fqbn Heltec-esp32:esp32:heltec_vision_master_e_213 \
  .

echo "Compiling E290 firmware..."
"$ARDUINO_CLI" compile \
  --build-path "$BUILD_ROOT/e290" \
  --fqbn Heltec-esp32:esp32:heltec_vision_master_e290 \
  .

echo "Release checks passed."
