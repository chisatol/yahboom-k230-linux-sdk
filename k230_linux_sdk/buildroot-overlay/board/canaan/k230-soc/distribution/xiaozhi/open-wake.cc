#include "okws_bridge.h"
#include "json.hpp"
#include <arpa/inet.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

using json = nlohmann::json;

int main(int argc, char **argv) {
    if (argc != 4 && argc != 7) return 2;
    int lock = open("/run/xiaozhi-ai.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock < 0) return 1;
    flock(lock, LOCK_EX);
    const char *error = nullptr;
    auto *engine = okws_engine_create(argv[1], argv[2], &error);
    flock(lock, LOCK_UN);
    if (!engine) { std::fprintf(stderr, "Open KWS: %s\n", error); return 1; }
    if (argc == 7) {
        if (okws_engine_enroll_begin(engine, 1)) return 1;
        for (int i = 4; i < 7; ++i) {
            std::ifstream in(argv[i], std::ios::binary);
            std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
            if (bytes.size() % 2 || bytes.empty()) return 1;
            std::vector<int16_t> pcm(bytes.size() / 2);
            for (size_t n = 0; n < pcm.size(); ++n)
                pcm[n] = static_cast<int16_t>(static_cast<unsigned char>(bytes[n*2]) | (static_cast<unsigned char>(bytes[n*2+1]) << 8));
            size_t begin = 0, end = 0;
            if (okws_trim(pcm.data(), pcm.size(), &begin, &end) != 0 || end <= begin) {
                std::fprintf(stderr, "No speech in recording %d\n", i - 3); return 1;
            }
            okws_audio_metrics_t metrics{};
            flock(lock, LOCK_EX);
            int code = okws_engine_enroll_add(engine, pcm.data() + begin, end - begin, &metrics, -1);
            flock(lock, LOCK_UN);
            if (code) { std::fprintf(stderr, "Recording %d: %s\n", i-3, okws_audio_reason_str(code)); return 1; }
        }
        int done = 0, progress = 0, total = 0;
        while (!done) {
            flock(lock, LOCK_EX);
            int code = okws_engine_enroll_step(engine, &done, &progress, &total);
            flock(lock, LOCK_UN);
            if (code) { std::fprintf(stderr, "Enrollment: %s\n", okws_enroll_error_str(code)); return 1; }
        }
        okws_enroll_result_t result{};
        if (okws_engine_enroll_result(engine, &result) || !result.n_hyps) return 1;
        json output = {{"threshold", result.threshold}, {"verify_threshold", result.verify_threshold}};
        output["units"] = json::array(); output["weights"] = json::array();
        for (int i = 0; i < result.n_hyps; ++i) {
            output["units"].push_back(std::vector<int>(result.units[i], result.units[i] + result.lens[i]));
            output["weights"].push_back(result.weights[i]);
        }
        std::ofstream out(argv[3]); out << output.dump(2) << '\n';
        return out ? 0 : 1;
    }
    std::ifstream input(argv[3]);
    json config;
    try { input >> config; } catch (...) { std::fprintf(stderr, "Missing wake enrollment\n"); return 1; }
    std::vector<int> units, lengths; std::vector<double> weights;
    for (const auto &h : config.at("units")) {
        auto u = h.get<std::vector<int>>(); lengths.push_back(u.size()); units.insert(units.end(), u.begin(), u.end());
    }
    weights = config.at("weights").get<std::vector<double>>();
    if (lengths.empty() || lengths.size() != weights.size()) return 1;
    okws_engine_set_verify(engine, 8, 2.0, 1.25);
    if (okws_engine_add_keyword(engine, units.data(), lengths.data(), weights.data(), lengths.size(),
            config.at("threshold"), 1500, 2, config.at("verify_threshold")) < 0) return 1;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); addr.sin_port = htons(5682);
    if (fd < 0 || bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr))) return 1;
    sockaddr_in target = addr; target.sin_port = htons(5678);
    auto last = std::chrono::steady_clock::now() - std::chrono::seconds(5);
    auto last_diag = std::chrono::steady_clock::now();
    const bool diagnostic = std::getenv("XIAOZHI_WAKE_DIAG") != nullptr;
    std::vector<int16_t> pending;
    std::puts("Wake word detector ready: 凑企鹅凑企鹅"); std::fflush(stdout);
    for (;;) {
        int16_t pcm[2048]; ssize_t n = recv(fd, pcm, sizeof(pcm), 0);
        if (n <= 0) continue;
        if (n == 4 && (!std::memcmp(pcm, "PLAY", 4) || !std::memcmp(pcm, "IDLE", 4))) {
            const bool playing = !std::memcmp(pcm, "PLAY", 4);
            okws_engine_set_bias(engine, playing ? -0.15 : 0.0);
            okws_engine_set_verify(engine, 8, playing ? 0.0 : 2.0, playing ? 0.0 : 1.25);
            std::printf("Wake sensitivity: %s\n", playing ? "playback" : "normal");
            std::fflush(stdout);
            continue;
        }
        pending.insert(pending.end(), pcm, pcm + n / 2);
        if (pending.size() < 2560) continue;
        okws_event_t events[2];
        flock(lock, LOCK_EX);
        int count = okws_engine_push(engine, pending.data(), 2560, events, 2);
        flock(lock, LOCK_UN);
        pending.erase(pending.begin(), pending.begin() + 2560);
        if (count < 0) { std::fprintf(stderr, "Open KWS inference failed\n"); return 1; }
        auto now = std::chrono::steady_clock::now();
        if (diagnostic && now - last_diag >= std::chrono::seconds(3)) {
            okws_kw_stats_t stats{};
            if (!okws_engine_keyword_stats(engine, 0, &stats))
                std::printf("Wake diagnostic: score=%.3f threshold=%.3f rejected=%d\n", stats.best_score, stats.threshold, stats.n_rejected);
            std::fflush(stdout); last_diag = now;
        }
        if (count && now - last > std::chrono::seconds(2)) {
            const char msg[] = "{\"type\":\"wake\"}";
            sendto(fd, msg, sizeof(msg)-1, 0, reinterpret_cast<sockaddr *>(&target), sizeof(target));
            last = now; std::puts("Wake word detected: 凑企鹅凑企鹅"); std::fflush(stdout);
        }
    }
}
