#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <lvgl/lvgl.h>
#include <lvgl/drivers/display/lv_linux_drm.h>
#include "cJSON.h"

static volatile sig_atomic_t running = 1;
static int touch_x = -1, touch_y = -1, touch_down;
static const char *face_dir = "/var/lib/xiaozhi/faces";
static int face_ids[999], face_count, face_selected;
static char edit_name[21];
static void stop(int sig) { (void)sig; running = 0; }
static uint32_t ticks(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}
static lv_obj_t *label(lv_obj_t *parent, const char *value, int x, int y, unsigned color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, value);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    return obj;
}
static lv_obj_t *button(lv_obj_t *parent, const char *value, int x, int y, int w, int h, unsigned bg)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 5, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_t *text = lv_label_create(obj);
    lv_label_set_text(text, value);
    lv_obj_center(text);
    return obj;
}
static void finish_worker(pid_t *worker)
{
    if (*worker > 0) {
        kill(*worker, SIGTERM);
        waitpid(*worker, NULL, 0);
        *worker = 0;
    }
}
static void face_path(char *path, size_t size, int id, const char *suffix)
{
    snprintf(path, size, "%s/%d.%s", face_dir, id, suffix);
}
static void refresh_faces(lv_obj_t *display)
{
    face_count = 0;
    for (int id = 1; id <= 999; ++id) {
        char data[128], name[128];
        face_path(data, sizeof(data), id, "db"); face_path(name, sizeof(name), id, "name");
        if (!access(data, F_OK) && !access(name, F_OK)) face_ids[face_count++] = id;
    }
    if (face_selected >= face_count) face_selected = face_count ? face_count - 1 : 0;
    if (!face_count) { lv_label_set_text(display, "暂无注册人脸"); return; }
    char path[128], name[81] = {0}, text[160];
    face_path(path, sizeof(path), face_ids[face_selected], "name");
    FILE *file = fopen(path, "rb");
    if (file) { fread(name, 1, sizeof(name) - 1, file); fclose(file); }
    snprintf(text, sizeof(text), "%d / %d     %s", face_selected + 1, face_count, name[0] ? name : "未命名");
    lv_label_set_text(display, text);
}
static int save_face_name(int id, const char *name)
{
    char path[128], temporary[160];
    face_path(path, sizeof(path), id, "name");
    snprintf(temporary, sizeof(temporary), "%s/.%d.%ld.tmp", face_dir, id, (long)getpid());
    int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    size_t len = strlen(name);
    int ok = write(fd, name, len) == (ssize_t)len && fsync(fd) == 0;
    if (close(fd)) ok = 0;
    if (ok && rename(temporary, path) == 0) return 0;
    unlink(temporary);
    return -1;
}
static int delete_face(int id)
{
    char feature[128], name[128];
    face_path(feature, sizeof(feature), id, "db");
    face_path(name, sizeof(name), id, "name");
    if (unlink(feature)) return -1;
    return unlink(name);
}
static void show_page(lv_obj_t **pages, lv_obj_t *back, int page)
{
    for (int i = 0; i < 6; ++i) {
        if (i == page) lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (page) lv_obj_remove_flag(back, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(back, LV_OBJ_FLAG_HIDDEN);
}

int main(void)
{
    static const char *states[] = {"等待启动", "正在启动", "等待网络", "待命", "正在连接",
        "正在聆听", "正在回复", "更新中", "等待设备激活", "发生错误"};
    struct sockaddr_in address = { .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK), .sin_port = htons(5679) };
    int sock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sock < 0 || bind(sock, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("UI socket"); return 1;
    }
    signal(SIGTERM, stop); signal(SIGINT, stop);
    lv_init(); lv_tick_set_cb(ticks);
    lv_display_t *display = lv_linux_drm_create();
    if (!display || lv_linux_drm_set_file(display, "/dev/dri/card0", -1) != LV_RESULT_OK) return 1;
    lv_sysmon_hide_performance(display);
    int touch = open("/dev/input/by-path/platform-91408000.i2c-event", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (touch < 0) perror("touchscreen");
    lv_font_t *font = lv_freetype_font_create("/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 22, LV_FREETYPE_FONT_STYLE_NORMAL);
    lv_font_t *large = lv_freetype_font_create("/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        LV_FREETYPE_FONT_RENDER_MODE_BITMAP, 34, LV_FREETYPE_FONT_STYLE_NORMAL);
    if (!font || !large) return 1;
    font->fallback = &lv_font_montserrat_22;
    lv_obj_t *native = lv_screen_active();
    lv_obj_set_style_bg_color(native, lv_color_hex(0x101313), 0);
    lv_obj_clear_flag(native, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *screen = lv_obj_create(native);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, lv_display_get_vertical_resolution(display), lv_display_get_horizontal_resolution(display));
    lv_obj_set_pos(screen, 0, lv_display_get_vertical_resolution(display));
    lv_obj_set_style_transform_pivot_x(screen, 0, 0);
    lv_obj_set_style_transform_pivot_y(screen, 0, 0);
    lv_obj_set_style_transform_rotation(screen, 2700, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(screen, font, 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xf1f4ef), 0);
    label(screen, "凑企鹅 AI", 26, 23, 0xf1f4ef);
    lv_obj_t *back = label(screen, LV_SYMBOL_LEFT "  返回", 510, 23, 0xa6b0ab);
    lv_obj_add_flag(back, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *rule = lv_obj_create(screen);
    lv_obj_remove_style_all(rule); lv_obj_set_pos(rule, 26, 65); lv_obj_set_size(rule, 588, 1);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0); lv_obj_set_style_bg_color(rule, lv_color_hex(0x35403d), 0);

    lv_obj_t *chat = lv_obj_create(screen);
    lv_obj_remove_style_all(chat); lv_obj_set_size(chat, 640, 405); lv_obj_set_pos(chat, 0, 75);
    lv_obj_t *status = label(chat, "正在启动", 28, 22, 0x7ed6b5);
    lv_obj_t *headline = label(chat, "你好，我是凑企鹅", 28, 75, 0xf5f6f2);
    lv_obj_set_style_text_font(headline, large, 0);
    label(chat, "智能语音助手", 28, 132, 0x9ba8a4);
    lv_obj_t *text = label(chat, "正在连接服务...", 28, 205, 0xd5ddd8);
    lv_obj_set_size(text, 340, 108); lv_label_set_long_mode(text, LV_LABEL_LONG_CLIP);
    lv_obj_t *face_image = lv_image_create(chat);
    lv_image_set_src(face_image, "A:/opt/xiaozhi/assets/img_naughty.png");
    lv_image_set_scale(face_image, 640);
    lv_obj_set_pos(face_image, 447, 95);
    button(chat, "人脸识别", 28, 330, 245, 54, 0x287965);
    button(chat, "手动唤醒", 289, 330, 245, 54, 0x3c4844);

    lv_obj_t *face_page = lv_obj_create(screen);
    lv_obj_remove_style_all(face_page); lv_obj_set_size(face_page, 640, 405); lv_obj_set_pos(face_page, 0, 75);
    lv_obj_add_flag(face_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *face_title = label(face_page, "人脸识别", 28, 22, 0xf5f6f2);
    lv_obj_set_style_text_font(face_title, large, 0);
    label(face_page, "本机人脸注册与身份比对", 28, 78, 0xa6b0ab);
    lv_obj_t *face_status = label(face_page, "选择操作以启用摄像头", 28, 154, 0x7ed6b5);
    lv_obj_set_width(face_status, 575); lv_label_set_long_mode(face_status, LV_LABEL_LONG_WRAP);
    button(face_page, "管理人脸", 28, 238, 245, 50, 0x303b37);
    button(face_page, "注册人脸", 28, 330, 245, 54, 0x287965);
    button(face_page, "身份比对", 289, 330, 245, 54, 0x3c4844);

    lv_obj_t *manage = lv_obj_create(screen);
    lv_obj_remove_style_all(manage); lv_obj_set_size(manage, 640, 405); lv_obj_set_pos(manage, 0, 75);
    lv_obj_t *manage_title = label(manage, "人脸管理", 28, 22, 0xf5f6f2);
    lv_obj_set_style_text_font(manage_title, large, 0);
    lv_obj_t *record = label(manage, "暂无注册人脸", 28, 105, 0xf1f4ef);
    lv_obj_set_width(record, 570); lv_label_set_long_mode(record, LV_LABEL_LONG_DOT);
    button(manage, LV_SYMBOL_LEFT, 28, 185, 100, 52, 0x303b37);
    button(manage, LV_SYMBOL_RIGHT, 144, 185, 100, 52, 0x303b37);
    lv_obj_t *manage_status = label(manage, "", 28, 260, 0x7ed6b5);
    button(manage, "修改名称", 28, 330, 245, 54, 0x287965);
    button(manage, "删除人脸", 289, 330, 245, 54, 0x874c43);

    lv_obj_t *editor = lv_obj_create(screen);
    lv_obj_remove_style_all(editor); lv_obj_set_size(editor, 640, 405); lv_obj_set_pos(editor, 0, 75);
    label(editor, "修改名称", 28, 8, 0xf5f6f2);
    lv_obj_t *edit_display = label(editor, "", 28, 45, 0x7ed6b5);
    label(editor, "英文、数字或 - _", 310, 45, 0x9ba8a4);
    const char *keys[] = {"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm_<<"};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 10; ++col) {
            char key[2] = {keys[row][col], 0};
            button(editor, key, 28 + col * 58, 82 + row * 50, 52, 44, 0x303b37);
        }
    }
    button(editor, "保存", 28, 330, 245, 54, 0x287965);
    button(editor, "取消", 289, 330, 245, 54, 0x3c4844);

    lv_obj_t *confirm = lv_obj_create(screen);
    lv_obj_remove_style_all(confirm); lv_obj_set_size(confirm, 640, 405); lv_obj_set_pos(confirm, 0, 75);
    lv_obj_t *confirm_title = label(confirm, "确认删除该人脸？", 28, 42, 0xf5f6f2);
    lv_obj_set_style_text_font(confirm_title, large, 0);
    lv_obj_t *confirm_record = label(confirm, "", 28, 110, 0xd8aaa3);
    label(confirm, "删除后不能恢复", 28, 155, 0xa6b0ab);
    button(confirm, "确认删除", 28, 330, 245, 54, 0x874c43);
    button(confirm, "取消", 289, 330, 245, 54, 0x3c4844);

    lv_obj_t *enroll = lv_obj_create(screen);
    lv_obj_remove_style_all(enroll); lv_obj_set_size(enroll, 640, 405); lv_obj_set_pos(enroll, 0, 75);
    lv_obj_t *enroll_title = label(enroll, "录入唤醒词", 28, 22, 0xf5f6f2);
    lv_obj_set_style_text_font(enroll_title, large, 0);
    label(enroll, "凑企鹅凑企鹅", 28, 90, 0x7ed6b5);
    lv_obj_t *enroll_status = label(enroll, "点击开始，录入第 1 遍", 28, 160, 0xf1f4ef);
    lv_obj_set_width(enroll_status, 575); lv_label_set_long_mode(enroll_status, LV_LABEL_LONG_WRAP);
    button(enroll, "开始录音", 28, 330, 245, 54, 0x287965);
    button(enroll, "关闭", 289, 330, 245, 54, 0x3c4844);

    lv_obj_t *pages[] = {chat, face_page, manage, editor, confirm, enroll};
    int page = access("/run/xiaozhi-enroll-mode", F_OK) == 0 ? 5 : 0;
    show_page(pages, back, page);
    pid_t worker = 0;
    int enroll_step = 0, enroll_job = 0;
    while (running) {
        if (worker > 0) {
            int code;
            if (waitpid(worker, &code, WNOHANG) == worker) {
                worker = 0;
                if (enroll_job) {
                    if (!WIFEXITED(code) || WEXITSTATUS(code)) {
                        enroll_step = 0;
                        lv_label_set_text(enroll_status, "录音或注册失败，请重新录入三遍");
                    } else if (enroll_job == 1) {
                        ++enroll_step;
                        if (enroll_step == 3) {
                            lv_label_set_text(enroll_status, "正在生成唤醒词，请稍候...");
                            worker = fork();
                            if (worker == 0) { execl("/usr/bin/python3", "python3", "/opt/xiaozhi/enroll-wake.py", "finish", NULL); _exit(127); }
                            enroll_job = 2;
                            if (worker < 0) { worker = 0; enroll_job = 0; enroll_step = 0; lv_label_set_text(enroll_status, "注册启动失败，请重试"); }
                            continue;
                        }
                        char msg[96]; snprintf(msg, sizeof(msg), "点击开始，录入第 %d 遍", enroll_step + 1);
                        lv_label_set_text(enroll_status, msg);
                    } else {
                        lv_label_set_text(enroll_status, "注册成功，正在返回...");
                        if (fork() == 0) { execl("/usr/bin/systemctl", "systemctl", "restart", "yahboom-xiaozhi", NULL); _exit(127); }
                    }
                    enroll_job = 0;
                } else if (!WIFEXITED(code) || WEXITSTATUS(code)) lv_label_set_text(face_status, "人脸服务异常，请重试");
            }
        }
        struct input_event event;
        while (touch >= 0 && read(touch, &event, sizeof(event)) == sizeof(event)) {
            if (event.type == EV_ABS && (event.code == ABS_X || event.code == ABS_MT_POSITION_X)) touch_x = event.value;
            if (event.type == EV_ABS && (event.code == ABS_Y || event.code == ABS_MT_POSITION_Y)) touch_y = event.value;
            if (event.type != EV_KEY || event.code != BTN_TOUCH) continue;
            if (event.value) { touch_down = 1; continue; }
            if (!touch_down || touch_x < 0 || touch_y < 0) continue;
            touch_down = 0;
            int x = lv_display_get_vertical_resolution(display) - 1 - touch_y;
            int y = lv_display_get_horizontal_resolution(display) - 1 - touch_x;
            fprintf(stderr, "touch raw=%d,%d mapped=%d,%d\n", touch_x, touch_y, x, y);
            if (page == 5 && !worker && ((y < 65 && x >= 490) || (y >= 396 && y < 475 && x >= 289 && x < 545))) {
                unlink("/run/xiaozhi-enroll-mode");
                if (fork() == 0) { execl("/usr/bin/systemctl", "systemctl", "restart", "yahboom-xiaozhi", NULL); _exit(127); }
            } else if (page == 5 && !worker && y >= 396 && y < 475 && x >= 20 && x < 280) {
                char number[2] = {(char)('1' + enroll_step), 0};
                lv_label_set_text(enroll_status, "正在录音，请说唤醒词...");
                worker = fork();
                if (worker == 0) { execl("/usr/bin/python3", "python3", "/opt/xiaozhi/enroll-wake.py", "record", number, NULL); _exit(127); }
                if (worker < 0) { worker = 0; lv_label_set_text(enroll_status, "录音启动失败，请重试"); }
                else enroll_job = 1;
            } else if (page != 5 && page && y < 65 && x >= 490) {
                finish_worker(&worker);
                page = page == 1 ? 0 : page == 2 ? 1 : 2;
                show_page(pages, back, page);
            } else if (!page && y >= 396 && y < 475 && x >= 20 && x < 280) {
                page = 1;
                show_page(pages, back, page);
            } else if (y >= 396 && y < 475 && x >= 289 && x < 545 && !page) {
                static const char command[] = "{\"type\":\"wake\"}";
                struct sockaddr_in client = { .sin_family = AF_INET,
                    .sin_addr.s_addr = htonl(INADDR_LOOPBACK), .sin_port = htons(5678) };
                sendto(sock, command, sizeof(command) - 1, 0, (struct sockaddr *)&client, sizeof(client));
                lv_label_set_text(status, "正在连接");
            } else if (page == 1 && y >= 305 && y < 365 && x >= 20 && x < 280 && !worker) {
                page = 2;
                refresh_faces(record);
                lv_label_set_text(manage_status, "");
                show_page(pages, back, page);
            } else if (page == 1 && y >= 396 && y < 475 && x >= 20 && x < 545 && !worker) {
                const char *mode = x < 280 ? "register" : "match";
                lv_label_set_text(face_status, "正在启动摄像头...");
                worker = fork();
                if (worker == 0) { execl("/opt/xiaozhi/xiaozhi-face", "xiaozhi-face", mode, NULL); _exit(127); }
                if (worker < 0) { worker = 0; lv_label_set_text(face_status, "人脸服务启动失败"); }
            } else if (page == 2 && y >= 255 && y < 315 && x >= 20 && x < 250 && face_count) {
                face_selected = (face_selected + (x < 136 ? face_count - 1 : 1)) % face_count;
                refresh_faces(record);
                lv_label_set_text(manage_status, "");
            } else if (page == 2 && y >= 396 && y < 475 && x >= 20 && x < 545 && face_count) {
                if (x < 280) {
                    edit_name[0] = '\0';
                    lv_label_set_text(edit_display, "请输入名称");
                    page = 3;
                } else {
                    lv_label_set_text(confirm_record, lv_label_get_text(record));
                    page = 4;
                }
                show_page(pages, back, page);
            } else if (page == 3 && y >= 157 && y < 351 && x >= 28 && x < 608) {
                int row = (y - 157) / 50, col = (x - 28) / 58;
                if ((y - 157) % 50 < 44 && (x - 28) % 58 < 52 && row < 4 && col < 10) {
                    char key = keys[row][col];
                    size_t len = strlen(edit_name);
                    if (key == '<') { if (len) edit_name[len - 1] = '\0'; }
                    else if (len < sizeof(edit_name) - 1) { edit_name[len] = key; edit_name[len + 1] = '\0'; }
                    lv_label_set_text(edit_display, edit_name[0] ? edit_name : "请输入名称");
                }
            } else if (page == 3 && y >= 396 && y < 475 && x >= 20 && x < 545) {
                if (x < 280 && !edit_name[0]) lv_label_set_text(edit_display, "名称不能为空");
                else {
                    if (x < 280 && save_face_name(face_ids[face_selected], edit_name))
                        lv_label_set_text(manage_status, "保存失败");
                    else lv_label_set_text(manage_status, x < 280 ? "名称已更新" : "");
                    refresh_faces(record); page = 2; show_page(pages, back, page);
                }
            } else if (page == 4 && y >= 396 && y < 475 && x >= 20 && x < 545) {
                if (x < 280) {
                    if (delete_face(face_ids[face_selected])) lv_label_set_text(manage_status, "删除失败，请检查存储");
                    else lv_label_set_text(manage_status, "人脸已删除");
                    refresh_faces(record);
                }
                page = 2; show_page(pages, back, page);
            }
        }
        char buffer[16385]; ssize_t size;
        while ((size = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
            buffer[size] = '\0'; cJSON *json = cJSON_Parse(buffer);
            if (!json) continue;
            cJSON *state = cJSON_GetObjectItemCaseSensitive(json, "state");
            if (cJSON_IsNumber(state) && state->valueint >= 0 && state->valueint < 10) {
                lv_label_set_text(status, states[state->valueint]);
                lv_image_set_src(face_image, state->valueint == 6 ? "A:/opt/xiaozhi/assets/img_joke.png" :
                    state->valueint == 4 ? "A:/opt/xiaozhi/assets/img_think.png" : "A:/opt/xiaozhi/assets/img_naughty.png");
            }
            cJSON *message = cJSON_GetObjectItemCaseSensitive(json, "text");
            if (cJSON_IsString(message) && message->valuestring) lv_label_set_text(text, message->valuestring);
            cJSON *face_message = cJSON_GetObjectItemCaseSensitive(json, "face_text");
            if (cJSON_IsString(face_message) && face_message->valuestring) lv_label_set_text(face_status, face_message->valuestring);
            cJSON_Delete(json);
        }
        lv_timer_handler(); usleep(10000);
    }
    finish_worker(&worker);
    close(sock); if (touch >= 0) close(touch);
    return 0;
}
