# Waveshare 5" DSI Display Driver for Orange Pi CM4

Get the [Waveshare 5-DSI-TOUCH-A](https://www.waveshare.com/wiki/5-DSI-TOUCH-A) touchscreen working on the **Orange Pi CM4** (Rockchip RK3566).

Waveshare only provides drivers for Raspberry Pi and ESP32. This repo contains a custom Linux kernel module, device tree overlay, and all the config files you need to use this display on the Orange Pi CM4 running Debian.

### Demo

[![Demo video](https://img.youtube.com/vi/CE7AW1fFeYs/0.jpg)](https://youtu.be/CE7AW1fFeYs)

## What's in the box

| Folder | What it does |
|--------|-------------|
| `driver/` | Kernel module source code (`ws_dsi_panel.c`) + Makefile |
| `overlay/` | Device tree overlay that wires up DSI, I2C, and the display |
| `xorg/` | Xorg config for the modesetting driver |
| `scripts/` | Install script, overlay merge helper, backlight control tool |

## About the display

The [Waveshare 5-DSI-TOUCH-A](https://www.waveshare.com/wiki/5-DSI-TOUCH-A) is a 5-inch 720x1280 IPS touchscreen that connects over a single 22-pin FPC cable carrying MIPI DSI (video), I2C (control + touch), and power.

- **Panel IC**: HX8394 (driven over 2-lane MIPI DSI)
- **Touch IC**: GT911 / Goodix (handled by the standard Linux Goodix driver)
- **Onboard MCU**: Sits at I2C address `0x45`, controls backlight and panel power
- **Native orientation**: Portrait (720x1280) -- use `xrandr` to rotate to landscape

## What was tested

- **Board**: Orange Pi CM4 + baseboard
- **OS**: Orange Pi Debian 11 (Bullseye)
- **Kernel**: 5.10.160-rockchip-rk356x
- **GPU**: Mali-G52

## How to set it up

### What you'll need

- An Orange Pi CM4 running Debian 11 (or compatible Armbian)
- The kernel source that **exactly matches** the kernel on your device (same version string)
- A cross-compiler if you're building from a PC: `aarch64-linux-gnu-gcc`
- A few standard tools: `dtc`, `fdtoverlay`, `i2c-tools`

Install the tools:

```bash
# On the Orange Pi itself:
sudo apt install device-tree-compiler i2c-tools

# On your build PC (Ubuntu / WSL):
sudo apt install gcc-aarch64-linux-gnu device-tree-compiler
```

### Step 1 -- Get the kernel source

The kernel module has to be compiled against the **exact same kernel** your Orange Pi is running, down to the version suffix. If there's a mismatch, the module won't load.

```bash
git clone https://github.com/orangepi-xunlong/linux-orangepi.git -b orange-pi-5.10-rk356x
cd linux-orangepi

# Grab the config from your running device
scp orangepi@<YOUR_DEVICE_IP>:/proc/config.gz .
zcat config.gz > .config

# Prepare the tree for out-of-tree module builds
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules_prepare
```

### Step 2 -- Build the kernel module

```bash
cd driver/
make KDIR=/path/to/linux-orangepi
```

This gives you `ws_dsi_panel.ko`.

### Step 3 -- Compile the device tree overlay

```bash
cd overlay/
dtc -@ -I dts -O dtb -o ws-5inch-dsi-lcd.dtbo ws-5inch-dsi-lcd.dts
```

### Step 4 -- Install everything to the SD card

Plug the SD card into your PC and mount both partitions:

```bash
sudo mount /dev/sdX1 /mnt/sdboot    # boot partition
sudo mount /dev/sdX2 /mnt/rootfs    # root filesystem
```

**4a) Copy the kernel module:**

```bash
KVER="5.10.160-rockchip-rk356x"   # must match your kernel
sudo cp driver/ws_dsi_panel.ko /mnt/rootfs/lib/modules/${KVER}/extra/
sudo chroot /mnt/rootfs depmod -a ${KVER}

# Make it load automatically on boot
echo "ws_dsi_panel" | sudo tee /mnt/rootfs/etc/modules-load.d/ws-display.conf
```

**4b) Merge the device tree overlay:**

The Orange Pi CM4's U-Boot doesn't reliably apply overlays at boot, so we bake it directly into the DTB files:

```bash
for dtb in /mnt/sdboot/dtb/rockchip/rk3566-orangepi-cm4*.dtb; do
    sudo cp "$dtb" "${dtb}.bak"
    sudo fdtoverlay -i "${dtb}.bak" -o "$dtb" overlay/ws-5inch-dsi-lcd.dtbo
done
```

Or just run the helper:

```bash
sudo ./scripts/merge-overlay.sh /mnt/sdboot/dtb/rockchip
```

**4c) Set boot parameters:**

Edit `/mnt/sdboot/orangepiEnv.txt` and make sure it has:

```
extraargs=cma=128M fbcon=rotate:1
```

- `cma=128M` -- gives the GPU enough memory for the framebuffer
- `fbcon=rotate:1` -- rotates the text console to landscape

**4d) Install the Xorg config:**

```bash
sudo mkdir -p /mnt/rootfs/etc/X11/xorg.conf.d/
sudo cp xorg/20-modesetting.conf /mnt/rootfs/etc/X11/xorg.conf.d/
```

**4e) (Optional) Install the backlight tool:**

```bash
sudo cp scripts/ws-backlight.sh /mnt/rootfs/usr/local/bin/
sudo chmod +x /mnt/rootfs/usr/local/bin/ws-backlight.sh
```

### Step 5 -- Boot it up

```bash
sudo umount /mnt/sdboot /mnt/rootfs
```

Pop the SD card into the Orange Pi, connect the display with the 22-pin FPC cable, and power on. You should see the Linux console on screen.

### Step 6 -- Rotate to landscape

The display is natively portrait. To flip it to landscape in your desktop environment:

```bash
xrandr --output DSI-1 --rotate right
```

To make it stick across reboots, add this to your session startup. For example in a systemd service:

```ini
ExecStartPre=/usr/bin/xrandr --output DSI-1 --rotate right
```

## Hardware connection

Just connect the display's 22-pin FPC cable to the **MIPI LCD** connector on the Orange Pi CM4 baseboard. That single cable carries everything:

- 2-lane MIPI DSI (video)
- I2C (MCU control + touch)
- Power (3.3V + GND)

No extra wires, adapters, or jumpers needed.

## Troubleshooting

### Screen stays black

1. Check if the module loaded:
   ```bash
   lsmod | grep ws_dsi
   dmesg | grep ws-5inch
   ```

2. Check if the display is detected:
   ```bash
   cat /sys/class/drm/card*-DSI-1/status
   # Should say "connected"
   ```

3. Force the backlight on:
   ```bash
   ws-backlight.sh 255
   ```

4. Look at DSI/DRM messages:
   ```bash
   dmesg | grep -iE 'dsi|mipi|panel|hx8394|vop|drm'
   ```

### Module refuses to load

You'll get a `vermagic` error if the module was built against a different kernel version. Double-check:

```bash
modinfo ws_dsi_panel.ko | grep vermagic
uname -r
```

These two strings must be identical.

### Overlay merge fails

Make sure `fdtoverlay` is installed (`sudo apt install device-tree-compiler`).

If you get symbol errors, your DTB might not have the expected nodes. This overlay was built for the stock Orange Pi CM4 DTB which has `raspits_panel`, `dsi1`, `video_phy1`, etc.

### Touch doesn't work

Touch is handled by the standard Goodix kernel driver (not this module). Check:

```bash
dmesg | grep -i goodix
ls /dev/input/event*
```

The GT911 can show up at I2C address `0x14` or `0x5D` depending on the INT pin state at reset.

### Desktop is black but console works fine

- Make sure your Xorg config does **not** have `Option "FlipFB" "always"` -- it causes flip timeouts on DSI
- Don't use `Option "Rotate"` in Xorg config -- the Rockchip modesetting driver ignores it. Use `xrandr` instead
- Check the Xorg log: `cat /var/log/Xorg.0.log | grep -iE 'error|fail'`

## How the driver works

The display has an onboard MCU (I2C address `0x45`) that sits between the host and the HX8394 panel IC. The driver talks to it like this:

1. **Unlocks the MCU** -- writes `0x11` then `0x17` to register `0x95`
2. **Sends HX8394 init commands** -- a long sequence of DSI commands that configures the panel
3. **Turns on the backlight** -- writes brightness (0-255) to register `0x96`

The init sequence was reverse-engineered from [Waveshare's official ESP32-P4 component](https://components.espressif.com/components/waveshare/esp_lcd_hx8394), since no Linux driver exists for this display.

## Lessons learned (the hard way)

If you're trying to get a similar display working on a Rockchip board, these might save you some time:

1. **This display uses 2 DSI lanes, not 4.** The HX8394 config byte at `0xBA` must be `0x61` (2-lane). Using `0x63` (4-lane) gives you a black screen with no errors.

2. **The MCU needs an unlock sequence.** Writing to the backlight register (`0x96`) does nothing until you send `0x11` then `0x17` to register `0x95`.

3. **Never send DSI commands in the panel's `enable()` callback.** The Synopsys DW MIPI DSI controller clears the high-speed clock bit whenever it processes a low-power message. If you do a DSI read/write after the link is active, the display dies silently.

4. **Pre-merge your device tree overlay.** The Orange Pi CM4's U-Boot overlay mechanism is unreliable. Use `fdtoverlay` to bake it into the base DTB.

5. **Don't disable HDMI.** Even if you're only using DSI, removing HDMI from the device tree breaks the Rockchip DRM component framework -- it waits forever for HDMI to bind.

6. **Set the D-PHY to 2 lanes explicitly.** Add `inno,lanes = <2>` to the `video_phy1` node, or the INNO D-PHY defaults to 4 and the link won't train properly.

7. **Avoid `FlipFB` in Xorg.** `Option "FlipFB" "always"` causes CRTC flip timeouts on DSI displays. Just don't use it.

8. **Xorg's `Rotate` option is ignored.** The Rockchip modesetting driver doesn't support it. Use `xrandr` for rotation.

## Technical reference

### MCU registers (I2C 0x45)

| Register | What it does | Values |
|----------|-------------|--------|
| `0x95` | Unlock / init | Write `0x11`, then `0x17` |
| `0x96` | Backlight brightness | `0x00` (off) to `0xFF` (max) |

### DSI link settings

| Setting | Value |
|---------|-------|
| Lanes | 2 |
| Color format | RGB888 |
| Video mode | Burst |
| Lane rate | 800 Mbps |
| Pixel clock | 63.264 MHz |

### Display timing (720x1280)

| | Active | Front porch | Sync | Back porch | Total |
|---|--------|-------------|------|------------|-------|
| **Horizontal** | 720 | 40 | 20 | 20 | 800 |
| **Vertical** | 1280 | 24 | 4 | 10 | 1318 |

## Credits

- HX8394 init sequence from [Waveshare's ESP32-P4 component](https://components.espressif.com/components/waveshare/esp_lcd_hx8394)
- MCU protocol from [Waveshare's ESP32 components](https://github.com/waveshareteam/Waveshare-ESP32-components)
- Display info from the [Waveshare 5-DSI-TOUCH-A wiki](https://www.waveshare.com/wiki/5-DSI-TOUCH-A)

## License

GPL-2.0+
