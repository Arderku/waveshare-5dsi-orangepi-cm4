# Waveshare 5" DSI Display Driver for Orange Pi CM4

Get the [Waveshare 5-DSI-TOUCH-A](https://www.waveshare.com/wiki/5-DSI-TOUCH-A) touchscreen working on the **Orange Pi CM4** (Rockchip RK3566).

Waveshare only provides drivers for Raspberry Pi and ESP32. This repo contains a custom Linux kernel module, device tree overlay, userspace touch polling library, and all the config files you need to use this display (including touch) on the Orange Pi CM4 running Debian.

### Demo

[![Demo video](https://img.youtube.com/vi/CE7AW1fFeYs/0.jpg)](https://youtu.be/CE7AW1fFeYs)

## What's in the box

| Folder | What it does |
|--------|-------------|
| `driver/` | Kernel module source code (`ws_dsi_panel.c`) + Makefile |
| `overlay/` | Device tree overlay that wires up DSI, I2C, and the display |
| `xorg/` | Xorg config for the modesetting driver |
| `scripts/` | Install script, overlay merge helper, backlight control tool |
| `touch/` | Userspace I2C touch polling library for the GT911 |

## About the display

The [Waveshare 5-DSI-TOUCH-A](https://www.waveshare.com/wiki/5-DSI-TOUCH-A) is a 5-inch 720x1280 IPS touchscreen that connects over a single 22-pin FPC cable carrying MIPI DSI (video), I2C (control + touch), and power.

- **Panel IC**: HX8394 (driven over 2-lane MIPI DSI)
- **Touch IC**: GT911 / Goodix (requires userspace I2C polling on Orange Pi CM4 — see `touch/`)
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

**4f) Enable touch (userspace I2C polling):**

Disable the kernel Goodix driver so your application can poll the GT911 directly (see the "Touch input" section below for full details):

```bash
# Decompile the DTB you merged the overlay into
dtc -I dtb -O dts -o /tmp/cm4.dts /mnt/sdboot/dtb/rockchip/rk3566-orangepi-cm4.dtb

# Edit /tmp/cm4.dts: find gt911@5d and set status = "disabled"
# Then recompile:
dtc -I dts -O dtb -o /mnt/sdboot/dtb/rockchip/rk3566-orangepi-cm4.dtb /tmp/cm4.dts
```

Set up I2C permissions for non-root users:

```bash
echo 'KERNEL=="i2c-[0-9]*", GROUP="i2c", MODE="0660"' | \
    sudo tee /mnt/rootfs/etc/udev/rules.d/99-i2c.rules

# Add your user to the i2c group (replace 'youruser')
sudo chroot /mnt/rootfs usermod -aG i2c youruser
```

Copy the touch library into your project and integrate `gt911_init()` / `gt911_poll_sdl()` into your main loop. See `touch/` for the source.

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

The GT911 touch IC is at I2C address `0x5D` (or `0x14` depending on INT pin state at reset). The standard Linux Goodix kernel driver will probe the chip successfully, but **on the Orange Pi CM4 the interrupt line (GPIO0_A5) is not physically connected to the GT911's INT pin**. This means the driver loads and registers an input device, but no touch events are ever delivered because the IRQ never fires.

**Solution: Userspace I2C polling.** Instead of relying on the broken kernel IRQ path, poll the GT911's touch registers directly over I2C. See `touch/` for the implementation.

Verify the GT911 is detected:

```bash
dmesg | grep -i goodix
# Should show: Goodix-TS 1-005d: ID 911, version: 1060
```

If you need to test raw I2C communication:

```bash
# Read product ID (should return "911")
i2cget -f -y 1 0x5d 0x81 0x40 i 4
```

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

9. **The GT911 touch IRQ is not wired on Orange Pi CM4.** The 22-pin FPC cable carries I2C for both the MCU and the GT911, but the interrupt line expected by the Goodix kernel driver is not connected. The driver loads, probes the chip, reports its ID — but never delivers events. The fix is to disable the kernel driver and poll the GT911's status register (`0x814E`) from userspace at ~60 Hz.

10. **GT911 coordinate byte order can vary.** The Goodix datasheet says little-endian, but on this hardware the raw bytes at `0x8150` are big-endian. If touch works but reports the same coordinate everywhere, try swapping the byte order.

11. **GT911 requires I2C repeated-start.** Simple separate write-then-read calls over I2C don't work for register access. You must use `ioctl(fd, I2C_RDWR, ...)` with a two-message transaction (write register address + read data in one operation).

## Touch input (GT911 via userspace I2C polling)

The GT911 touch controller shares the same I2C bus (I2C1) as the display MCU (`0x45`). On the Raspberry Pi, the Goodix kernel driver handles touch via an interrupt-driven flow. On the Orange Pi CM4, the interrupt GPIO is not wired to the GT911's INT pin through the 22-pin FPC cable, so the kernel driver loads but never delivers any touch events.

The solution is to **disable the kernel driver** and **poll the GT911 directly from userspace** over `/dev/i2c-1`.

### How it works

1. Open `/dev/i2c-1`
2. Read the status register (`0x814E`) at ~60 Hz
3. When bit 7 is set and touch count > 0, read the first touch point from `0x8150`
4. Convert raw coordinates to screen coordinates (accounting for display rotation)
5. Push the result as an `SDL_MOUSEBUTTONDOWN`/`UP` event (or inject via `uinput`)
6. Clear the status register by writing `0x00` to `0x814E`

### GT911 register map (touch-relevant)

| Register | Length | Description |
|----------|--------|-------------|
| `0x8140` | 4 bytes | Product ID (ASCII `"911\0"`) |
| `0x8146` | 4 bytes | X resolution (2 bytes LE) + Y resolution (2 bytes LE) |
| `0x814E` | 1 byte | Status: bit 7 = data ready, bits 3:0 = touch count |
| `0x8150` | 8 bytes | Touch point 1: track ID, X (2 bytes), Y (2 bytes), size (2 bytes), reserved |

### Coordinate byte order

**On this specific hardware, the GT911 sends coordinates in big-endian format**, even though the Goodix datasheet describes little-endian. The raw point data at `0x8150` is:

```
pt[0] = track ID
pt[1] = X high byte    ← (pt[1] << 8) | pt[2] = X
pt[2] = X low byte
pt[3] = Y high byte    ← (pt[3] << 8) | pt[4] = Y
pt[4] = Y low byte
pt[5..7] = size + reserved
```

If your coordinates are stuck at `(1279, 0)` or `(719, 0)` regardless of where you tap, you likely have the byte order wrong.

### I2C access method

The GT911 requires **I2C repeated-start** (combined write-then-read) transactions for register access. A simple `write()` followed by `read()` does not work — the GT911 ignores the second transaction. Use `ioctl(fd, I2C_RDWR, ...)` with a two-message `i2c_rdwr_ioctl_data`:

```c
static int gt911_read_reg(int fd, uint16_t reg, uint8_t *data, int len)
{
    uint8_t addr[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    struct i2c_msg msgs[2] = {
        { .addr = 0x5D, .flags = 0,        .len = 2,   .buf = addr },
        { .addr = 0x5D, .flags = I2C_M_RD, .len = len, .buf = data },
    };
    struct i2c_rdwr_ioctl_data rdwr = { .msgs = msgs, .nmsgs = 2 };
    return ioctl(fd, I2C_RDWR, &rdwr) < 0 ? -1 : 0;
}
```

### Disabling the kernel Goodix driver

If the kernel Goodix driver is bound to the GT911, userspace I2C access will fail with `EBUSY`. You must disable the kernel driver by setting `status = "disabled"` on the `gt911@5d` node in the device tree.

Decompile the DTB, patch, and recompile:

```bash
dtc -I dtb -O dts -o cm4.dts rk3566-orangepi-cm4.dtb
```

Find the `gt911@5d` node and change:

```dts
gt911@5d {
    status = "disabled";   /* was "okay" */
    /* ... rest of the node stays the same ... */
};
```

Recompile:

```bash
dtc -I dts -O dtb -o rk3566-orangepi-cm4.dtb cm4.dts
```

### I2C permissions for non-root users

If your application runs as a non-root user, you need a udev rule:

```bash
# /etc/udev/rules.d/99-i2c.rules
KERNEL=="i2c-[0-9]*", GROUP="i2c", MODE="0660"
```

And add your user to the `i2c` group:

```bash
sudo usermod -aG i2c youruser
```

### Display rotation and coordinate mapping

The display is natively portrait (720x1280) but typically rotated to landscape (1280x720) using `xrandr --rotate right`. This means the raw GT911 coordinates need to be transformed:

```
screen_x = raw_y
screen_y = (native_width - 1) - raw_x
```

Where `native_width` is 720 (the GT911's X resolution as reported in its config at `0x8146`).

### Reference implementation

See `touch/gt911_i2c.c` for a complete working implementation that:

- Initializes the GT911 over `/dev/i2c-1`
- Reads and logs the product ID and resolution at startup
- Polls touch data at ~16ms intervals
- Transforms coordinates for landscape rotation
- Pushes `SDL_MOUSEBUTTONDOWN`, `SDL_MOUSEMOTION`, and `SDL_MOUSEBUTTONUP` events

## Backlight / brightness control

The kernel module (`ws_dsi_panel.ko`) registers a standard Linux backlight device, so brightness is available through `/sys/class/backlight`:

```bash
# Read current brightness (0–255)
cat /sys/class/backlight/*/brightness

# Set brightness
echo 128 | sudo tee /sys/class/backlight/*/brightness

# Read max brightness
cat /sys/class/backlight/*/max_brightness
```

This is the recommended way to control brightness from application code — just read/write the sysfs files. No I2C access or unlock sequence needed, the kernel driver handles that.

For quick manual testing or if the kernel driver isn't loaded, the `ws-backlight.sh` script talks to the MCU directly over I2C:

```bash
ws-backlight.sh 255   # full brightness
ws-backlight.sh 128   # 50%
ws-backlight.sh 0     # off
```

### Backlight permissions for non-root users

To let a non-root user write to the sysfs brightness file, add a udev rule:

```bash
# /etc/udev/rules.d/99-backlight.rules
SUBSYSTEM=="backlight", ACTION=="add", RUN+="/bin/chmod 0666 /sys/class/backlight/%k/brightness"
```

Or grant write access to a specific group and add your user to it.

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
