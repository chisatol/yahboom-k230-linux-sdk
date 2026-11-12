#define main xiaozhi_ui_main
#include "../xiaozhi-ui.c"
#undef main
#include <stdlib.h>

int main(void)
{
    char dir[] = "/tmp/xiaonan-test-XXXXXX";
    if (!mkdtemp(dir)) return 1;
    face_dir = dir;
    for (int id = 1; id <= 3; id += 2) {
        char path[128];
        face_path(path, sizeof(path), id, "db");
        FILE *file = fopen(path, "wb");
        if (!file || fputs("test", file) < 0 || fclose(file)) return 2;
        face_path(path, sizeof(path), id, "name");
        file = fopen(path, "wb");
        if (!file || fputs("old", file) < 0 || fclose(file)) return 3;
    }
    lv_init();
    lv_display_t *display = lv_display_create(640, 480);
    if (!display) return 4;
    lv_obj_t *record = lv_label_create(lv_screen_active());
    refresh_faces(record);
    if (face_count != 2 || face_ids[0] != 1 || face_ids[1] != 3) return 5;
    if (save_face_name(1, "new-name")) return 6;
    refresh_faces(record);
    if (!strstr(lv_label_get_text(record), "new-name")) return 7;
    if (delete_face(1)) return 8;
    refresh_faces(record);
    if (face_count != 1 || face_ids[0] != 3) return 9;
    if (delete_face(3)) return 10;
    refresh_faces(record);
    if (face_count || rmdir(dir)) return 11;
    puts("face data management test passed");
    return 0;
}
