/*
 * gt911_i2c.c — Userspace I2C touch polling for the GT911 (Goodix)
 *
 * Designed for the Waveshare 5-DSI-TOUCH-A on Orange Pi CM4 (RK3566),
 * where the kernel Goodix driver probes successfully but never fires
 * touch events because the IRQ GPIO is not physically connected.
 *
 * Prerequisites:
 *   - GT911 kernel driver disabled in DTB (status = "disabled" on gt911@5d)
 *   - /dev/i2c-1 accessible (udev rule + user in i2c group, or run as root)
 *
 * Compile:
 *   gcc -c gt911_i2c.c -o gt911_i2c.o
 *   gcc -c gt911_i2c.c -o gt911_i2c.o -DGT911_USE_SDL $(sdl2-config --cflags)
 */

#ifdef __linux__

#include "gt911_i2c.h"
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#define GT911_ADDR       0x5D
#define GT911_STATUS_REG 0x814E
#define GT911_POINT1_REG 0x8150
#define GT911_MAX_POINTS 5
#define GT911_POINT_SIZE 8

static int g_fd        = -1;
static int g_screen_w, g_screen_h;
static int g_abs_x_max = 720;
static int g_abs_y_max = 1280;
static int g_touching;
static int g_last_sx, g_last_sy;

/* ── I2C helpers ─────────────────────────────────────────────── */

static int gt911_write_reg(int fd, uint16_t reg,
                           const uint8_t *data, int len)
{
    uint8_t buf[34];
    if (len > 32) return -1;
    buf[0] = (uint8_t)(reg >> 8);
    buf[1] = (uint8_t)(reg & 0xFF);
    memcpy(buf + 2, data, len);

    struct i2c_msg msg = {
        .addr = GT911_ADDR, .flags = 0,
        .len = (uint16_t)(len + 2), .buf = buf
    };
    struct i2c_rdwr_ioctl_data rdwr = { .msgs = &msg, .nmsgs = 1 };
    return ioctl(fd, I2C_RDWR, &rdwr) < 0 ? -1 : 0;
}

static int gt911_read_reg(int fd, uint16_t reg,
                          uint8_t *data, int len)
{
    uint8_t addr[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    struct i2c_msg msgs[2] = {
        { .addr = GT911_ADDR, .flags = 0,        .len = 2,   .buf = addr },
        { .addr = GT911_ADDR, .flags = I2C_M_RD, .len = (uint16_t)len, .buf = data },
    };
    struct i2c_rdwr_ioctl_data rdwr = { .msgs = msgs, .nmsgs = 2 };
    return ioctl(fd, I2C_RDWR, &rdwr) < 0 ? -1 : 0;
}

/* ── Coordinate transform ────────────────────────────────────── */

/*
 * The DSI panel is natively portrait (720x1280).
 * With `xrandr --rotate right` the screen is landscape (1280x720).
 *
 *   screen_x = raw_y  * screen_w / abs_y_max
 *   screen_y = (abs_x_max - raw_x) * screen_h / abs_x_max
 */
static void raw_to_screen(int raw_x, int raw_y, int *sx, int *sy)
{
    float nx = (float)raw_x / (float)g_abs_x_max;
    float ny = (float)raw_y / (float)g_abs_y_max;

    *sx = (int)(ny * g_screen_w);
    *sy = (int)((1.0f - nx) * g_screen_h);

    if (*sx < 0) *sx = 0;
    if (*sx >= g_screen_w) *sx = g_screen_w - 1;
    if (*sy < 0) *sy = 0;
    if (*sy >= g_screen_h) *sy = g_screen_h - 1;
}

/* ── Public API ──────────────────────────────────────────────── */

int gt911_init(int screen_w, int screen_h)
{
    g_screen_w = screen_w;
    g_screen_h = screen_h;

    g_fd = open("/dev/i2c-1", O_RDWR);
    if (g_fd < 0) {
        fprintf(stderr, "[gt911] open /dev/i2c-1: %s\n", strerror(errno));
        return -1;
    }

    uint8_t id_buf[4] = {0};
    if (gt911_read_reg(g_fd, 0x8140, id_buf, 4) == 0) {
        fprintf(stderr, "[gt911] Product ID: %c%c%c%c\n",
                id_buf[0], id_buf[1], id_buf[2], id_buf[3]);
    } else {
        fprintf(stderr, "[gt911] Cannot read product ID: %s\n",
                strerror(errno));
        close(g_fd);
        g_fd = -1;
        return -1;
    }

    uint8_t res_buf[4] = {0};
    if (gt911_read_reg(g_fd, 0x8146, res_buf, 4) == 0) {
        g_abs_x_max = res_buf[0] | (res_buf[1] << 8);
        g_abs_y_max = res_buf[2] | (res_buf[3] << 8);
        if (g_abs_x_max == 0) g_abs_x_max = 720;
        if (g_abs_y_max == 0) g_abs_y_max = 1280;
    }

    fprintf(stderr, "[gt911] Ready: X_max=%d Y_max=%d screen=%dx%d\n",
            g_abs_x_max, g_abs_y_max, g_screen_w, g_screen_h);
    return 0;
}

int gt911_poll_raw(int *screen_x, int *screen_y, int *touching)
{
    if (g_fd < 0) return -1;

    uint8_t status;
    if (gt911_read_reg(g_fd, GT911_STATUS_REG, &status, 1) != 0)
        return -1;

    int buf_ready   = (status >> 7) & 1;
    int touch_count = status & 0x0F;

    if (!buf_ready) return -1;

    if (touch_count > 0 && touch_count <= GT911_MAX_POINTS) {
        uint8_t pt[GT911_POINT_SIZE];
        if (gt911_read_reg(g_fd, GT911_POINT1_REG, pt, GT911_POINT_SIZE) == 0) {
            /* Big-endian coordinates on this hardware */
            int raw_x = (pt[1] << 8) | pt[2];
            int raw_y = (pt[3] << 8) | pt[4];

            raw_to_screen(raw_x, raw_y, &g_last_sx, &g_last_sy);
            g_touching = 1;
        }
    } else if (touch_count == 0 && g_touching) {
        g_touching = 0;
    }

    /* Clear status */
    uint8_t zero = 0;
    gt911_write_reg(g_fd, GT911_STATUS_REG, &zero, 1);

    if (screen_x) *screen_x = g_last_sx;
    if (screen_y) *screen_y = g_last_sy;
    if (touching) *touching = g_touching;
    return 0;
}

#ifdef GT911_USE_SDL

static void push_mouse_event(Uint32 type, int x, int y, int pressed)
{
    SDL_Event ev;
    SDL_memset(&ev, 0, sizeof(ev));

    if (type == SDL_MOUSEMOTION) {
        ev.type = SDL_MOUSEMOTION;
        ev.motion.x = x;
        ev.motion.y = y;
        ev.motion.state = pressed ? SDL_BUTTON_LMASK : 0;
    } else {
        ev.type = type;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.x = x;
        ev.button.y = y;
        ev.button.clicks = 1;
        ev.button.state = (type == SDL_MOUSEBUTTONDOWN)
                            ? SDL_PRESSED : SDL_RELEASED;
    }
    SDL_PushEvent(&ev);
}

void gt911_poll_sdl(SDL_Window *win)
{
    (void)win;
    int was_touching = g_touching;
    int sx, sy, touching;

    if (gt911_poll_raw(&sx, &sy, &touching) != 0)
        return;

    if (touching && !was_touching)
        push_mouse_event(SDL_MOUSEBUTTONDOWN, sx, sy, 1);
    else if (touching && was_touching)
        push_mouse_event(SDL_MOUSEMOTION, sx, sy, 1);
    else if (!touching && was_touching)
        push_mouse_event(SDL_MOUSEBUTTONUP, sx, sy, 0);
}

#endif /* GT911_USE_SDL */

void gt911_shutdown(void)
{
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
}

#endif /* __linux__ */
