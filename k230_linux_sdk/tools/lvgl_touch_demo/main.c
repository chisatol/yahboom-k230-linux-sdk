#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>

#include "lvgl/lvgl.h"
#include "lvgl/drivers/display/lv_linux_drm.h"
#include "lvgl/drivers/indev/lv_evdev.h"

static lv_obj_t *dot = NULL;
static lv_obj_t *coord_label = NULL;
static lv_obj_t *hint_label = NULL;
static uint32_t last_touch_ms = 0;
static uint32_t tap_count = 0;
static bool running = true;

uint32_t get_tick_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

static void sighandler(int sig) {
    (void)sig;
    running = false;
}

static void touch_event_cb(lv_event_t *e) {
    lv_indev_t *indev = lv_event_get_indev(e);
    lv_indev_state_t state = lv_indev_get_state(indev);
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    if (state == LV_INDEV_STATE_PR) {
        uint32_t now = get_tick_ms();

        /* Create dot if not exists */
        if (dot == NULL) {
            dot = lv_obj_create(lv_screen_active());
            lv_obj_set_size(dot, 20, 20);
            lv_obj_set_style_bg_color(dot, lv_palette_main(LV_PALETTE_GREEN), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(dot, 0, 0);
        }

        lv_obj_set_pos(dot, pt.x - 10, pt.y - 10);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);

        /* Update coordinate label */
        if (coord_label) {
            lv_label_set_text_fmt(coord_label, "X:%d Y:%d", (int)pt.x, (int)pt.y);
        }

        /* Double-tap detection */
        if ((now - last_touch_ms) < 500) {
            tap_count++;
        } else {
            tap_count = 1;
        }
        last_touch_ms = now;

        if (tap_count >= 2) {
            /* Clear all children except coord_label and hint_label */
            lv_obj_clean(lv_screen_active());
            dot = NULL;
            coord_label = NULL;
            hint_label = NULL;

            /* Recreate UI */
            coord_label = lv_label_create(lv_screen_active());
            lv_obj_set_style_text_font(coord_label, &lv_font_montserrat_28, 0);
            lv_obj_set_style_text_color(coord_label, lv_color_white(), 0);
            lv_obj_align(coord_label, LV_ALIGN_TOP_LEFT, 10, 10);
            lv_label_set_text(coord_label, "Cleared!");

            hint_label = lv_label_create(lv_screen_active());
            lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_24, 0);
            lv_obj_set_style_text_color(hint_label, lv_palette_main(LV_PALETTE_LIGHT_BLUE), 0);
            lv_obj_align(hint_label, LV_ALIGN_CENTER, 0, 0);
            lv_label_set_text(hint_label, "Touch to show green dot\nDouble-tap to clear");

            tap_count = 0;
        }
    } else {
        if (dot) {
            lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

int main(void) {
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);

    lv_init();

    /* Create display */
    lv_display_t *disp = lv_linux_drm_create();
    if (disp == NULL) {
        fprintf(stderr, "Failed to create DRM display\n");
        return 1;
    }

    lv_linux_drm_set_file(disp, "/dev/dri/card0", -1);

    /* Get display size */
    lv_coord_t hor_res = lv_display_get_horizontal_resolution(disp);
    lv_coord_t ver_res = lv_display_get_vertical_resolution(disp);
    printf("Display: %dx%d\n", (int)hor_res, (int)ver_res);

    /* Set screen background to black */
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);

    /* Create coordinate label */
    coord_label = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(coord_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(coord_label, lv_color_white(), 0);
    lv_obj_align(coord_label, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_label_set_text(coord_label, "Touch Demo");

    /* Create hint label */
    hint_label = lv_label_create(lv_screen_active());
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(hint_label, lv_palette_main(LV_PALETTE_LIGHT_BLUE), 0);
    lv_obj_align(hint_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(hint_label, "Touch to show green dot\nDouble-tap to clear");

    /* Create input device */
    lv_indev_t *indev = lv_evdev_create(LV_INDEV_TYPE_POINTER, "/dev/input/event0");
    if (indev == NULL) {
        fprintf(stderr, "Failed to create evdev input\n");
        return 1;
    }

    /* Set touch calibration for 480x640 display */
    lv_evdev_set_swap_axes(indev, false);
    lv_evdev_set_calibration(indev, 0, 0, 480, 640);

    /* Add event callback to screen */
    lv_obj_add_event_cb(lv_screen_active(), touch_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(lv_screen_active(), touch_event_cb, LV_EVENT_RELEASED, NULL);

    printf("LVGL touch demo running...\n");

    while (running) {
        lv_timer_handler();
        usleep(5000);
    }

    return 0;
}
