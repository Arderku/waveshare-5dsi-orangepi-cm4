#!/bin/bash
#
# install.sh — Install the Waveshare 5-DSI-TOUCH-A driver on Orange Pi CM4
#
# Run this ON THE ORANGE PI (or with the SD card mounted at /mnt/sdboot
# and /mnt/rootfs for the boot and root partitions respectively).
#
# Usage:
#   sudo ./install.sh              # install on running system
#   sudo ./install.sh --sd /mnt    # install to mounted SD card at /mnt
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SD_ROOT=""

if [ "$1" = "--sd" ] && [ -n "$2" ]; then
    SD_ROOT="$2"
    echo "Installing to SD card mounted at $SD_ROOT"
    BOOT_DIR="$SD_ROOT/sdboot"
    ROOT_DIR="$SD_ROOT/rootfs"
else
    echo "Installing on running system"
    BOOT_DIR="/boot"
    ROOT_DIR=""
fi

# ---- 1. Install kernel module ----
echo ""
echo "=== Step 1: Installing kernel module ==="
KVER=$(uname -r 2>/dev/null || echo "5.10.160-rockchip-rk356x")
MOD_DIR="${ROOT_DIR}/lib/modules/${KVER}/extra"
mkdir -p "$MOD_DIR"

if [ -f "$SCRIPT_DIR/driver/ws_dsi_panel.ko" ]; then
    cp "$SCRIPT_DIR/driver/ws_dsi_panel.ko" "$MOD_DIR/"
    echo "  Installed ws_dsi_panel.ko to $MOD_DIR/"
    if [ -z "$SD_ROOT" ]; then
        depmod -a
        echo "  depmod done"
    fi
else
    echo "  WARNING: ws_dsi_panel.ko not found — you need to build it first!"
    echo "  See README.md for build instructions."
fi

# ---- 2. Apply device tree overlay ----
echo ""
echo "=== Step 2: Applying device tree overlay ==="
DTS="$SCRIPT_DIR/overlay/ws-5inch-dsi-lcd.dts"
DTBO="/tmp/ws-5inch-dsi-lcd.dtbo"

if command -v dtc &> /dev/null; then
    dtc -@ -I dts -O dtb -o "$DTBO" "$DTS"
    echo "  Compiled overlay to $DTBO"
else
    echo "  WARNING: dtc not found. Install device-tree-compiler:"
    echo "    sudo apt install device-tree-compiler"
    exit 1
fi

DTB_DIR="${BOOT_DIR}/dtb/rockchip"
if [ -d "$DTB_DIR" ]; then
    for dtb in "$DTB_DIR"/rk3566-orangepi-cm4*.dtb; do
        [ -f "$dtb" ] || continue
        cp "$dtb" "${dtb}.bak"
        fdtoverlay -i "${dtb}.bak" -o "$dtb" "$DTBO"
        echo "  Merged overlay into $(basename "$dtb")"
    done
else
    echo "  WARNING: DTB directory not found at $DTB_DIR"
    echo "  You may need to merge the overlay manually."
fi

# ---- 3. Configure boot environment ----
echo ""
echo "=== Step 3: Configuring boot parameters ==="
ENV_FILE="${BOOT_DIR}/orangepiEnv.txt"
if [ -f "$ENV_FILE" ]; then
    if ! grep -q "fbcon=rotate:1" "$ENV_FILE"; then
        sed -i 's/^extraargs=.*/& fbcon=rotate:1/' "$ENV_FILE"
        echo "  Added fbcon=rotate:1 to extraargs"
    fi
    if ! grep -q "cma=" "$ENV_FILE"; then
        sed -i 's/^extraargs=.*/& cma=128M/' "$ENV_FILE"
        echo "  Added cma=128M to extraargs"
    fi
    echo "  orangepiEnv.txt configured"
else
    echo "  WARNING: $ENV_FILE not found"
fi

# ---- 4. Install Xorg config ----
echo ""
echo "=== Step 4: Installing Xorg configuration ==="
XORG_DIR="${ROOT_DIR}/etc/X11/xorg.conf.d"
mkdir -p "$XORG_DIR"
cp "$SCRIPT_DIR/xorg/20-modesetting.conf" "$XORG_DIR/"
echo "  Installed 20-modesetting.conf"

# ---- 5. Module autoload ----
echo ""
echo "=== Step 5: Configuring module autoload ==="
MODULES_LOAD="${ROOT_DIR}/etc/modules-load.d"
mkdir -p "$MODULES_LOAD"
echo "ws_dsi_panel" > "$MODULES_LOAD/ws-display.conf"
echo "  Module will load automatically on boot"

# ---- 6. Install backlight helper ----
echo ""
echo "=== Step 6: Installing backlight helper script ==="
BIN_DIR="${ROOT_DIR}/usr/local/bin"
mkdir -p "$BIN_DIR"
cp "$SCRIPT_DIR/scripts/ws-backlight.sh" "$BIN_DIR/"
chmod +x "$BIN_DIR/ws-backlight.sh"
echo "  Installed ws-backlight.sh"

echo ""
echo "=== Installation complete ==="
echo ""
echo "Next steps:"
echo "  1. If you haven't built ws_dsi_panel.ko yet, see README.md"
echo "  2. Reboot to activate the display"
echo "  3. After boot, rotate display with: xrandr --output DSI-1 --rotate right"
echo ""
