#!/bin/bash
#
# merge-overlay.sh — Compile and merge the device tree overlay into the base DTBs
#
# Usage:
#   sudo ./merge-overlay.sh                          # default DTB path
#   sudo ./merge-overlay.sh /path/to/dtb/directory   # custom DTB path
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DTS="$SCRIPT_DIR/overlay/ws-5inch-dsi-lcd.dts"
DTBO="/tmp/ws-5inch-dsi-lcd.dtbo"
DTB_DIR="${1:-/boot/dtb/rockchip}"

echo "=== Waveshare 5-DSI overlay merge ==="
echo ""

# Compile DTS -> DTBO
echo "Compiling overlay..."
dtc -@ -I dts -O dtb -o "$DTBO" "$DTS"
echo "  -> $DTBO"

# Merge into each Orange Pi CM4 DTB
echo ""
echo "Merging into DTBs in $DTB_DIR ..."
for dtb in "$DTB_DIR"/rk3566-orangepi-cm4*.dtb; do
    [ -f "$dtb" ] || continue
    echo "  Processing $(basename "$dtb")"
    cp "$dtb" "${dtb}.bak"
    fdtoverlay -i "${dtb}.bak" -o "$dtb" "$DTBO"
    echo "    OK (backup at ${dtb}.bak)"
done

echo ""
echo "Done! Reboot to apply."
