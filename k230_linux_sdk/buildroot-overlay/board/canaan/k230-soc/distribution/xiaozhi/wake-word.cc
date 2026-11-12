// SPDX-License-Identifier: GPL-3.0-only
#include <arpa/inet.h>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include "kws.h"

class WakeKWS : public KWS {
public:
    using KWS::KWS;
    float keyword_score() const {
        float score = 0;
        for (int i = 0; i < 30; ++i) score = std::max(score, p_outputs_[0][i * 2 + 1]);
        return score;
    }
};

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(5682);
    if (fd < 0 || bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr))) {
        perror("wake word socket");
        return 1;
    }
    int lock = open("/run/xiaozhi-ai.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock < 0) { perror("AI lock"); return 1; }
    flock(lock, LOCK_EX);
    WakeKWS kws(argv[1], "xiaonan", 2, 0.3, 0);
    flock(lock, LOCK_UN);
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    target.sin_port = htons(5678);
    std::vector<float> samples;
    samples.reserve(6000);
    bool first = true;
    bool logged_audio = false;
    auto last_wake = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    auto last_diagnostic = std::chrono::steady_clock::now();
    const bool diagnostic = std::getenv("XIAOZHI_WAKE_DIAG") != nullptr;
    float max_score = 0;
    double max_rms = 0;
    int max_clipped = 0;
    std::puts("Wake word detector ready: 小楠小楠");
    std::fflush(stdout);
    for (;;) {
        int16_t pcm[2048];
        ssize_t n = recv(fd, pcm, sizeof(pcm), 0);
        if (n <= 0) continue;
        if (!logged_audio) {
            std::puts("Wake word audio stream received");
            std::fflush(stdout);
            logged_audio = true;
        }
        for (ssize_t i = 0; i < n / 2; ++i) samples.push_back(pcm[i]);
        size_t need = first ? 5120 : 4800;
        if (samples.size() < need) continue;
        std::vector<float> chunk(samples.begin(), samples.begin() + need);
        samples.erase(samples.begin(), samples.begin() + need);
        first = false;
        if (!kws.pre_process(chunk)) continue;
        flock(lock, LOCK_EX);
        kws.inference();
        auto result = kws.post_process();
        float score = diagnostic ? kws.keyword_score() : 0;
        flock(lock, LOCK_UN);
        auto now = std::chrono::steady_clock::now();
        if (diagnostic) {
            double power = 0;
            int clipped = 0;
            for (float sample : chunk) {
                power += sample * sample;
                if (std::abs(sample) >= 32000) ++clipped;
            }
            max_score = std::max(max_score, score);
            max_rms = std::max(max_rms, std::sqrt(power / chunk.size()));
            max_clipped = std::max(max_clipped, clipped);
            if (now - last_diagnostic >= std::chrono::seconds(2)) {
                std::printf("Wake diagnostic: rms=%.0f clipped=%d/%zu score=%.4f\n",
                            max_rms, max_clipped, chunk.size(), max_score);
                std::fflush(stdout);
                max_score = 0; max_rms = 0; max_clipped = 0;
                last_diagnostic = now;
            }
        }
        if (result != "Deactivated!" && now - last_wake > std::chrono::seconds(5)) {
            const char message[] = "{\"type\":\"wake\"}";
            sendto(fd, message, sizeof(message) - 1, 0,
                   reinterpret_cast<sockaddr *>(&target), sizeof(target));
            last_wake = now;
            std::puts("Wake word detected: 小楠小楠");
            std::fflush(stdout);
        }
    }
}
