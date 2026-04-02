/*
 * gt911_i2c.h — Userspace I2C polling for GT911 touch controller
 *
 * For the Waveshare 5-DSI-TOUCH-A on Orange Pi CM4, where the kernel
 * Goodix driver can't deliver events because the IRQ GPIO is not wired.
 *
 * Usage with SDL2:
 *   gt911_init(screen_w, screen_h);
 *   // in your main loop:
 *   gt911_poll_sdl(window);
 *   // on shutdown:
 *   gt911_shutdown();
 *
 * Usage without SDL2 (raw coordinates):
 *   gt911_init(screen_w, screen_h);
 *   int x, y, touching;
 *   gt911_poll_raw(&x, &y, &touching);
 *   gt911_shutdown();
 */

#ifndef GT911_I2C_H
#define GT911_I2C_H

#ifdef __linux__

/*
 * Initialize the GT911 over /dev/i2c-1.
 * screen_w / screen_h are the logical display dimensions after rotation.
 * Returns 0 on success, -1 on failure.
 */
int gt911_init(int screen_w, int screen_h);

/*
 * Poll touch data and push SDL mouse events.
 * Call this once per frame (~16ms).
 * Requires SDL2.
 */
#ifdef GT911_USE_SDL
#include <SDL2/SDL.h>
void gt911_poll_sdl(SDL_Window *win);
#endif

/*
 * Poll touch data without SDL dependency.
 * Sets *screen_x, *screen_y to the mapped screen coordinates.
 * Sets *touching to 1 if a finger is down, 0 if released.
 * Returns 0 if data was read, -1 if no new data.
 */
int gt911_poll_raw(int *screen_x, int *screen_y, int *touching);

/* Close the I2C file descriptor. */
void gt911_shutdown(void);

#else /* not __linux__ */

static inline int  gt911_init(int w, int h) { (void)w; (void)h; return 0; }
static inline int  gt911_poll_raw(int *x, int *y, int *t) { (void)x; (void)y; (void)t; return -1; }
static inline void gt911_shutdown(void) {}

#endif
#endif /* GT911_I2C_H */
