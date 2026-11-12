// SPDX-License-Identifier: GPL-3.0-only
#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <tuple>
#include <unistd.h>
#include <vector>
#include "vi_vo.h"
#include "sensor_buf_manager.h"
#include "face_detection.h"
#include "face_recognition.h"

static void message(const std::string &text)
{
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(5679);
    std::string payload = "{\"face_text\":\"" + text + "\"}";
    sendto(fd, payload.data(), payload.size(), 0, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    close(fd);
}

int main(int argc, char **argv)
{
    if (argc != 2 || (strcmp(argv[1], "register") && strcmp(argv[1], "match"))) return 2;
    const bool enrolling = !strcmp(argv[1], "register");
    char db[] = "/var/lib/xiaozhi/faces";
    umask(0077);
    mkdir("/var/lib/xiaozhi", 0700);
    if (mkdir(db, 0700) && errno != EEXIST) { message("无法打开人脸数据目录"); return 1; }
    int lock = open("/run/xiaozhi-ai.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock < 0) { message("无法初始化人脸识别"); return 1; }
    FILE *sensor_control = fopen("/proc/vsi/isp_subdev0", "w");
    if (sensor_control) { fputs("0 mode=1\n", sensor_control); fclose(sensor_control); }
    constexpr unsigned width = 1280, height = 720, channels = 3;
    v4l2_drm_context context;
    v4l2_drm_default_context(&context);
    context.device = 2;
    context.display = false;
    context.width = width;
    context.height = height;
    context.video_format = v4l2_fourcc('B', 'G', '3', 'P');
    context.buffer_num = 3;
    if (v4l2_drm_setup(&context, 1, nullptr) || v4l2_drm_start(&context)) {
        message("摄像头启动失败"); return 1;
    }
    flock(lock, LOCK_EX);
    FaceDetection detect("/opt/xiaozhi/face_detection_320.kmodel", 0.6, 0.2, {channels, height, width}, 0);
    FaceRecognition recognize("/opt/xiaozhi/face_recognition.kmodel", 75, {channels, height, width}, 0);
    int highest = 0;
    for (int id = 1; id <= 999; ++id) {
        std::string base = std::string(db) + "/" + std::to_string(id);
        struct stat feature_stat{}, name_stat{};
        bool has_feature = stat((base + ".db").c_str(), &feature_stat) == 0;
        bool has_name = stat((base + ".name").c_str(), &name_stat) == 0;
        if (has_feature || has_name) highest = id;
        if (!has_feature || !has_name) continue;
        if (feature_stat.st_size != 512 * sizeof(float) || name_stat.st_size <= 0 || name_stat.st_size > 80) {
            message("人脸数据异常，请检查记录");
            flock(lock, LOCK_UN); v4l2_drm_stop(&context); return 1;
        }
        std::ifstream features(base + ".db", std::ios::binary), name(base + ".name", std::ios::binary);
        std::vector<float> values(512);
        std::string label(static_cast<size_t>(name_stat.st_size), '\0');
        if (!features.read(reinterpret_cast<char *>(values.data()), feature_stat.st_size) || !name.read(&label[0], name_stat.st_size)) {
            message("人脸数据读取失败");
            flock(lock, LOCK_UN); v4l2_drm_stop(&context); return 1;
        }
        recognize.features_.push_back(std::move(values));
        recognize.names_.push_back(std::move(label));
        ++recognize.valid_register_face_;
    }
    flock(lock, LOCK_UN);
    if (!enrolling && recognize.database_count(db) == 0) {
        message("尚无人脸，请先注册");
        v4l2_drm_stop(&context);
        return 0;
    }
    message(enrolling ? "请正对摄像头，正在注册" : "请正对摄像头，正在比对");
    std::vector<std::tuple<int, void *>> buffers;
    for (unsigned i = 0; i < context.buffer_num; ++i)
        buffers.emplace_back(context.buffers[i].fd, context.buffers[i].mmap);
    SensorBufManager sensor({channels, height, width}, buffers);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    bool finished = false;
    while (std::chrono::steady_clock::now() < deadline) {
        if (v4l2_drm_dump(&context, 1000)) continue;
        auto &tensor = sensor.get_buf_for_index(context.vbuffer.index);
        flock(lock, LOCK_EX);
        detect.pre_process(tensor);
        detect.inference();
        std::vector<FaceDetectionInfo> faces;
        detect.post_process({width, height}, faces);
        if (faces.size() == 1) {
            recognize.pre_process(tensor, faces[0].sparse_kps.points);
            recognize.inference();
            if (enrolling) {
                if (highest >= 999) {
                    message("人脸记录已满");
                    finished = true;
                    flock(lock, LOCK_UN);
                    v4l2_drm_dump_release(&context);
                    break;
                }
                std::string name = "用户" + std::to_string(highest + 1);
                recognize.valid_register_face_ = highest;
                recognize.database_add(name, db);
                message(recognize.valid_register_face_ > highest ? "注册成功：" + name : "注册失败，请重试");
            } else {
                FaceRecognitionInfo result;
                recognize.database_search(result);
                message(result.id < 0 ? "未匹配到已注册人脸" : "比对成功：" + result.name);
            }
            finished = true;
        }
        flock(lock, LOCK_UN);
        v4l2_drm_dump_release(&context);
        if (finished) break;
    }
    if (!finished) message("未检测到单张人脸，请重试");
    v4l2_drm_stop(&context);
    close(lock);
    return 0;
}
