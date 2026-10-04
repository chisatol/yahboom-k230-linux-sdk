#include "lvgl/lvgl.h"
#include "lv_drivers/display/drm.h"
#include "lv_drivers/indev/evdev.h"
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>

lv_coord_t lcd_w;
lv_coord_t lcd_h;
uint32_t lcd_dpi;
bool flag_running = true;

static lv_obj_t *dot = NULL;
static lv_obj_t *coord_label = NULL;
static uint32_t last_touch_time = 0;
static uint32_t touch_count = 0;
static lv_point_t last_point = {0, 0};
static bool screen_cleared = false;

static void sighandler(int sig) {
    flag_running = false;
}

static void touch_event_cb(lv_event_t *e) {
    lv_indev_data_t *data = lv_event_get_param(e);
    lv_point_t point = data->point;
    uint32_t now = lv_tick_get();

    if (data->state == LV_INDEV_STATE_PR) {
        screen_cleared = false;

        if (dot == NULL) {
            dot = lv_obj_create(lv_scr_act());
            lv_obj_set_size(dot, 20, 20);
            lv_obj_set_style_bg_color(dot, lv_palette_main(LV_PALETTE_GREEN), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(dot, 0, 0);
        }

        lv_obj_set_pos(dot, point.x - 10, point.y - 10);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);

        if (coord_label == NULL) {
            coord_label = lv_label_create(lv_scr_act());
            lv_obj_set_style_text_font(coord_label, &lv_font_montserrat_28, 0);
            lv_obj_set_style_text_color(coord_label, lv_color_white(), 0);
            lv_obj_align(coord_label, LV_ALIGN_TOP_LEFT, 10, 10);
        }

        lv_label_set_text_fmt(coord_label, "X:%d Y:%d", (int)point.x, (int)point.y);

        if (touch_count > 0 && (now - last_touch_time) < 300 &&
            abs(point.x - last_point.x) < 30 && abs(point.y - last_point.y) < 30) {
            lv_obj_clean(lv_scr_act());
            dot = NULL;
            coord_label = NULL;
            screen_cleared = true;
            touch_count = 0;
        } else {
            touch_count = 1;
            last_point = point;
            last_touch_time = now;
        }
    } else {
        if (dot && !screen_cleared) {
            lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

int main(void)
{
    lv_init();

    drm_init();
    drm_get_sizes(&lcd_w, &lcd_h, &lcd_dpi);
    printf("lcd w,h,dpi:%d,%d,%d \n", lcd_w, lcd_h, lcd_dpi);

    uint32_t draw_buf_size = lcd_w * lcd_h * sizeof(lv_color_t);
    static lv_disp_draw_buf_t disp_buf;
#if DRM_DIRECT_BUFFER
    lv_color_t *buf_2_1 = drm_get_map(0);
    lv_color_t *buf_2_2 = drm_get_map(1);
#else
    lv_color_t *buf_2_1 = malloc(draw_buf_size);
    lv_color_t *buf_2_2 = malloc(draw_buf_size);
#endif
    lv_disp_draw_buf_init(&disp_buf, buf_2_1, buf_2_2, draw_buf_size);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf   = &disp_buf;
    disp_drv.flush_cb   = drm_flush;
    disp_drv.hor_res    = lcd_w;
    disp_drv.ver_res    = lcd_h;
    disp_drv.screen_transp = 1;
    disp_drv.full_refresh = 1;
#if DRM_DIRECT_BUFFER
    disp_drv.direct_mode = 1;
#endif
    lv_disp_drv_register(&disp_drv);

#if USE_EVDEV
    evdev_init();
    static lv_indev_drv_t indev_drv_1;
    lv_indev_drv_init(&indev_drv_1);
    indev_drv_1.type = LV_INDEV_TYPE_POINTER;
    indev_drv_1.read_cb = evdev_read;
    lv_indev_t *touch_indev = lv_indev_drv_register(&indev_drv_1);

    lv_obj_t *touch_obj = lv_obj_create(lv_scr_act());
    lv_obj_set_size(touch_obj, lcd_w, lcd_h);
    lv_obj_set_style_bg_opa(touch_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(touch_obj, 0, 0);
    lv_obj_add_event_cb(touch_obj, touch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_indev_set_cursor(touch_indev, touch_obj);

    lv_obj_t *hint = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(hint, lv_palette_main(LV_PALETTE_LIGHT_BLUE), 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(hint, "Touch to show green dot\nDouble-tap to clear");
#endif

    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);

    while(flag_running) {
        lv_timer_handler();
        usleep(5000);
    }

    return 0;
}

uint32_t custom_tick_get(void)
{
    static uint64_t start_ms = 0;
    if(start_ms == 0) {
        struct timeval tv_start;
        gettimeofday(&tv_start, NULL);
        start_ms = (tv_start.tv_sec * 1000000 + tv_start.tv_usec) / 1000;
    }

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    uint64_t now_ms;
    now_ms = (tv_now.tv_sec * 1000000 + tv_now.tv_usec) / 1000;

    uint32_t time_ms = now_ms - start_ms;
    return time_ms;
}
