#!/bin/sh
#
# ws-backlight.sh — Force-enable the Waveshare 5-DSI-TOUCH-A backlight
#
# The display MCU at I2C 0x45 needs an unlock sequence before
# the backlight brightness register (0x96) becomes writable.
#
# Usage:
#   ws-backlight.sh          # turn backlight on (full brightness)
#   ws-backlight.sh 128      # set brightness to 128/255
#   ws-backlight.sh 0        # turn backlight off
#
BRIGHTNESS="${1:-255}"
BRIGHTNESS_HEX=$(printf "0x%02x" "$BRIGHTNESS")

# MCU unlock sequence
i2cset -f -y 1 0x45 0x95 0x11
i2cset -f -y 1 0x45 0x95 0x17

# Set backlight
i2cset -f -y 1 0x45 0x96 "$BRIGHTNESS_HEX"

echo "Backlight set to $BRIGHTNESS/255"
