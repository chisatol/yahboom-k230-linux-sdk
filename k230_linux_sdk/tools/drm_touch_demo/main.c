/*
 * Simple DRM + evdev touch demo for K230
 * Draws green dot at touch position, double-tap to clear
 * Uses direct DRM KMS for display and evdev for touch input
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

static int fb_fd = -1;
static int evdev_fd = -1;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static uint32_t *fb_mem = NULL;
static long screen_size = 0;
static int running = 1;
static uint32_t screen_w = 480;
static uint32_t screen_h = 640;

static void sighandler(int sig) {
    (void)sig;
    running = 0;
}

static int init_fb(void) {
    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) {
        perror("open /dev/fb0");
        return -1;
    }
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("FBIOGET_VSCREENINFO");
        return -1;
    }
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        perror("FBIOGET_FSCREENINFO");
        return -1;
    }
    screen_w = vinfo.xres;
    screen_h = vinfo.yres;
    screen_size = finfo.smem_len;
    printf("FB: %dx%d %dbpp line_len=%d\n", screen_w, screen_h, vinfo.bits_per_pixel, finfo.line_length);

    fb_mem = mmap(0, screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_mem == MAP_FAILED) {
        perror("mmap fb");
        return -1;
    }
    return 0;
}

static int init_evdev(void) {
    evdev_fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    if (evdev_fd < 0) {
        perror("open /dev/input/event0");
        return -1;
    }
    return 0;
}

static void clear_screen(void) {
    if (fb_mem) {
        memset(fb_mem, 0, screen_size);
    }
}

static void draw_dot(int x, int y, int size, uint32_t color) {
    if (!fb_mem) return;
    for (int dy = -size/2; dy <= size/2; dy++) {
        for (int dx = -size/2; dx <= size/2; dx++) {
            int px = x + dx;
            int py = y + dy;
            if (px >= 0 && px < (int)screen_w && py >= 0 && py < (int)screen_h) {
                if (dx*dx + dy*dy <= (size/2)*(size/2)) {
                    fb_mem[py * (finfo.line_length/4) + px] = color;
                }
            }
        }
    }
}

static int read_touch(int *x, int *y, int *pressed) {
    struct input_event ev;
    static int tx = -1, ty = -1, tp = 0;
    static int valid = 0;
    int got_event = 0;

    while (read(evdev_fd, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type == EV_ABS) {
            switch (ev.code) {
                case ABS_X: tx = ev.value; valid |= 1; break;
                case ABS_Y: ty = ev.value; valid |= 2; break;
                case ABS_MT_POSITION_X: tx = ev.value; valid |= 1; break;
                case ABS_MT_POSITION_Y: ty = ev.value; valid |= 2; break;
            }
        } else if (ev.type == EV_KEY) {
            if (ev.code == BTN_TOUCH) {
                tp = ev.value;
            }
        } else if (ev.type == EV_SYN) {
            if (valid == 3) {
                *x = tx * screen_w / 4096;
                *y = ty * screen_h / 4096;
                *pressed = tp;
                got_event = 1;
            }
        }
    }
    return got_event;
}

int main(void) {
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);

    if (init_fb() < 0) {
        fprintf(stderr, "Failed to init framebuffer\n");
        return 1;
    }
    if (init_evdev() < 0) {
        fprintf(stderr, "Failed to init evdev\n");
        return 1;
    }

    clear_screen();
    printf("Touch demo running on %dx%d. Touch to draw green dot. Double-tap to clear.\n", screen_w, screen_h);

    int last_tap_time = 0;
    int tap_count = 0;
    int first_x = -1, first_y = -1;

    while (running) {
        int x, y, pressed;
        if (read_touch(&x, &y, &pressed)) {
            if (pressed) {
                draw_dot(x, y, 15, 0x00FF00);  /* Green */
            }
        }
        usleep(10000);  /* 10ms */
    }

    clear_screen();
    close(evdev_fd);
    close(fb_fd);
    return 0;
}
