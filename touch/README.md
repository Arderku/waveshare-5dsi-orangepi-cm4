# GT911 Userspace I2C Touch Polling

Polls the GT911 touch controller directly over `/dev/i2c-1`, bypassing the kernel Goodix driver which doesn't work on the Orange Pi CM4 because the IRQ GPIO is not wired through the 22-pin FPC cable.

## Files

| File | Description |
|------|-------------|
| `gt911_i2c.h` | Header with init/poll/shutdown API |
| `gt911_i2c.c` | Implementation (I2C read/write, coordinate transform, optional SDL events) |

## Quick start

### With SDL2

```c
#define GT911_USE_SDL
#include "gt911_i2c.h"

// at init
gt911_init(1280, 720);

// in your main loop (~60fps)
gt911_poll_sdl(window);

// at shutdown
gt911_shutdown();
```

Compile:

```bash
aarch64-linux-gnu-gcc -DGT911_USE_SDL $(sdl2-config --cflags) \
    -c gt911_i2c.c -o gt911_i2c.o
```

### Without SDL2

```c
#include "gt911_i2c.h"

gt911_init(1280, 720);

// in your loop
int x, y, touching;
if (gt911_poll_raw(&x, &y, &touching) == 0) {
    if (touching)
        printf("finger at (%d, %d)\n", x, y);
}

gt911_shutdown();
```

## Prerequisites

1. **Disable the kernel Goodix driver** — set `status = "disabled"` on the `gt911@5d` node in the device tree (see main README)
2. **I2C permissions** — either run as root or set up a udev rule:
   ```
   # /etc/udev/rules.d/99-i2c.rules
   KERNEL=="i2c-[0-9]*", GROUP="i2c", MODE="0660"
   ```
   Then add your user to the `i2c` group: `sudo usermod -aG i2c youruser`

## Key details

- **I2C address**: `0x5D`
- **I2C bus**: `/dev/i2c-1`
- **Byte order**: Big-endian (despite Goodix datasheet saying little-endian)
- **I2C method**: `ioctl(I2C_RDWR)` with repeated-start (simple read/write calls don't work)
- **Coordinate transform**: Accounts for `xrandr --rotate right` (portrait 720x1280 → landscape 1280x720)
