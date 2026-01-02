#!/bin/bash
#
# nyanBOX by Nyan Devices
# https://github.com/jbohack/nyanBOX
# Copyright (c) 2025 jbohack
#
# Licensed under the MIT License
# https://opensource.org/licenses/MIT
#
# SPDX-License-Identifier: MIT
#

set -e

PLATFORMIO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$PLATFORMIO_DIR/.." && pwd)"
OUT_DIR="$REPO_ROOT/firmware-files"
ABOUT_FILE="$PLATFORMIO_DIR/include/about.h"
BUILD_DIR="$PLATFORMIO_DIR/.pio/build/nyanbox-main"

echo "Building nyanBOX firmware..."

if [ ! -f "$ABOUT_FILE" ]; then
    echo "about.h not found."
    exit 1
fi
VERSION=$(grep -E '#define\s+NYANBOX_VERSION' "$ABOUT_FILE" | grep -oE 'v?[0-9]+\.[0-9]+\.[0-9]+')
[ -z "$VERSION" ] && VERSION="latest"

if command -v pio >/dev/null 2>&1; then
    PIO="pio"
elif [ -x "$HOME/.platformio/penv/bin/pio" ]; then
    PIO="$HOME/.platformio/penv/bin/pio"
else
    echo "PlatformIO not found. Please install it or add it to PATH."
    exit 1
fi

$PIO run -e nyanbox-main

mkdir -p "$OUT_DIR"
cp -f "$BUILD_DIR/bootloader.bin" "$OUT_DIR/" 2>/dev/null || true
cp -f "$BUILD_DIR/partitions.bin" "$OUT_DIR/" 2>/dev/null || true
cp -f "$BUILD_DIR/firmware.bin" "$OUT_DIR/" 2>/dev/null || true

cat > "$OUT_DIR/manifest.json" <<EOF
{
  "name": "nyanBOX",
  "version": "$VERSION",
  "home_assistant_domain": "esphome",
  "funding_url": "https://shop.nyandevices.com",
  "new_install_prompt_erase": true,
  "builds": [
    {
      "chipFamily": "ESP32",
      "parts": [
        { "path": "https://raw.githubusercontent.com/jbohack/nyanBOX/main/firmware-files/bootloader.bin", "offset": 4096 },
        { "path": "https://raw.githubusercontent.com/jbohack/nyanBOX/main/firmware-files/partitions.bin", "offset": 32768 },
        { "path": "https://raw.githubusercontent.com/jbohack/nyanBOX/main/firmware-files/firmware.bin", "offset": 65536 }
      ]
    }
  ]
}
EOF

echo "Build complete. Version: $VERSION"
echo "Files copied to: $OUT_DIR"