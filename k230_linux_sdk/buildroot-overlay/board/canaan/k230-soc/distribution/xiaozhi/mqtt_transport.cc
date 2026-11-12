#include "mqtt_transport.h"
#include "json.hpp"
#include <MQTTAsync.h>
#include <openssl/evp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

using json = nlohmann::json;
extern int g_websocket_start;
static json config;
static MQTTAsync client;
static mqtt_data_callback audio_cb, text_cb;
static std::atomic<bool> enabled{false}, connected{false};
static std::mutex audio_mutex;
static int audio_fd = -1;
static unsigned char audio_key[16], audio_nonce[16];
static uint32_t send_sequence, receive_sequence;
static unsigned int audio_generation;
static int mqtt_connect();

static bool unhex(const std::string &s, unsigned char *out) {
    if (s.size() != 32) return false;
    for (size_t i = 0; i < 16; ++i) {
        auto digit = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int a = digit(s[i * 2]), b = digit(s[i * 2 + 1]);
        if (a < 0 || b < 0) return false;
        out[i] = (a << 4) | b;
    }
    return true;
}

static bool crypt(const unsigned char *key, const unsigned char *iv,
                  const unsigned char *in, int len, unsigned char *out) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;
    int n = 0, tail = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, key, iv) == 1 &&
              EVP_EncryptUpdate(ctx, out, &n, in, len) == 1 &&
              EVP_EncryptFinal_ex(ctx, out + n, &tail) == 1 && n + tail == len;
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

static void close_audio() {
    std::lock_guard<std::mutex> lock(audio_mutex);
    ++audio_generation;
    if (audio_fd >= 0) close(audio_fd);
    audio_fd = -1;
    g_websocket_start = 0;
}

void mqtt_close_audio() { close_audio(); }

static void receive_audio(int fd, unsigned int generation) {
    unsigned char packet[65536];
    for (;;) {
        pollfd pfd{fd, POLLIN, 0};
        int result = poll(&pfd, 1, 1000);
        {
            std::lock_guard<std::mutex> lock(audio_mutex);
            if (generation != audio_generation) return;
        }
        if (result <= 0 || !(pfd.revents & POLLIN)) continue;
        ssize_t n = recv(fd, packet, sizeof(packet), 0);
        if (n < 16 || packet[0] != 1) continue;
        uint16_t len; uint32_t sequence;
        memcpy(&len, packet + 2, 2); len = ntohs(len);
        memcpy(&sequence, packet + 12, 4); sequence = ntohl(sequence);
        if (n != 16 + len) continue;
        std::vector<unsigned char> out(len);
        {
            std::lock_guard<std::mutex> lock(audio_mutex);
            if (generation != audio_generation) return;
            if (sequence <= receive_sequence || !crypt(audio_key, packet, packet + 16, len, out.data())) continue;
            receive_sequence = sequence;
            if (sequence == 1) std::cout << "MQTT UDP audio received" << std::endl;
        }
        if (audio_cb) audio_cb(reinterpret_cast<const char *>(out.data()), out.size());
    }
}

static bool setup_audio(const json &message) {
    const auto &udp = message.at("udp");
    unsigned char key[16], nonce[16];
    if (!unhex(udp.at("key").get<std::string>(), key) ||
        !unhex(udp.at("nonce").get<std::string>(), nonce)) return false;
    std::string host = udp.at("server").get<std::string>();
    int port = udp.at("port").get<int>();
    if (port <= 0 || port > 65535) return false;
    addrinfo hints{}, *addresses = nullptr;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &addresses)) return false;
    int fd = -1;
    for (auto *a = addresses; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd >= 0 && connect(fd, a->ai_addr, a->ai_addrlen) == 0) break;
        if (fd >= 0) close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses);
    if (fd < 0) return false;
    unsigned int generation;
    {
        std::lock_guard<std::mutex> lock(audio_mutex);
        if (audio_fd >= 0) close(audio_fd);
        audio_fd = fd; generation = ++audio_generation;
        memcpy(audio_key, key, 16); memcpy(audio_nonce, nonce, 16);
        send_sequence = receive_sequence = 0;
        g_websocket_start = 1;
    }
    std::thread(receive_audio, fd, generation).detach();
    std::cout << "MQTT UDP audio channel ready" << std::endl;
    return true;
}

static int on_message(void *, char *topic, int, MQTTAsync_message *msg) {
    std::string payload(static_cast<char *>(msg->payload), msg->payloadlen);
    MQTTAsync_freeMessage(&msg);
    MQTTAsync_free(topic);
    try {
        auto message = json::parse(payload);
        auto type = message.value("type", "");
        if (type == "hello") {
            if (message.value("transport", "") != "udp" || !setup_audio(message)) {
                std::cerr << "Invalid MQTT UDP hello" << std::endl;
                return 1;
            }
        } else if (type == "goodbye") {
            close_audio();
        }
        if (text_cb) text_cb(payload.data(), payload.size());
    } catch (const std::exception &e) {
        std::cerr << "MQTT message error: " << e.what() << std::endl;
    }
    return 1;
}

static void on_connected(void *, char *) {
    if (connected.exchange(true)) return;
    std::string topic = config.value("subscribe_topic", "");
    if (!topic.empty()) MQTTAsync_subscribe(client, topic.c_str(), 0, nullptr);
    std::cout << "MQTT control connected" << std::endl;
}
static void on_connect_success(void *, MQTTAsync_successData *) { on_connected(nullptr, nullptr); }
static void on_connect_failure(void *, MQTTAsync_failureData *r) {
    std::cerr << "MQTT connection failed: " << (r ? r->code : -1) << std::endl;
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (!connected) mqtt_connect();
    }).detach();
}
static void on_lost(void *, char *) {
    connected = false;
    close_audio();
    std::cerr << "MQTT control disconnected; reconnecting" << std::endl;
}

void mqtt_configure(const std::string &s, mqtt_data_callback audio, mqtt_data_callback text) {
    config = json::parse(s);
    audio_cb = audio; text_cb = text; enabled = true;
}
bool mqtt_enabled() { return enabled; }

static int mqtt_connect() {
    MQTTAsync_connectOptions options = MQTTAsync_connectOptions_initializer;
    MQTTAsync_SSLOptions ssl = MQTTAsync_SSLOptions_initializer;
    ssl.trustStore = "/etc/ssl/certs/ca-certificates.crt";
    ssl.enableServerCertAuth = 1;
    ssl.verify = 1;
    std::string username = config.value("username", ""), password = config.value("password", "");
    options.username = username.c_str(); options.password = password.c_str();
    options.ssl = &ssl; options.keepAliveInterval = config.value("keepalive", 240);
    options.cleansession = 1; options.automaticReconnect = 1;
    options.minRetryInterval = 2; options.maxRetryInterval = 60;
    options.onSuccess = on_connect_success; options.onFailure = on_connect_failure;
    int rc = MQTTAsync_connect(client, &options);
    if (rc != MQTTASYNC_SUCCESS) std::cerr << "MQTT start error " << rc << std::endl;
    return rc;
}

int mqtt_start() {
    std::string endpoint = config.at("endpoint");
    if (endpoint.find(':') == std::string::npos) endpoint += ":8883";
    endpoint = "ssl://" + endpoint;
    std::string id = config.at("client_id");
    if (MQTTAsync_create(&client, endpoint.c_str(), id.c_str(), MQTTCLIENT_PERSISTENCE_NONE, nullptr) != MQTTASYNC_SUCCESS) return -1;
    MQTTAsync_setCallbacks(client, nullptr, on_lost, on_message, nullptr);
    MQTTAsync_setConnected(client, nullptr, on_connected);
    return mqtt_connect();
}

int mqtt_send_text(const char *data, int size) {
    if (!connected || !data || size < 0) return -1;
    std::string topic = config.at("publish_topic");
    MQTTAsync_message message = MQTTAsync_message_initializer;
    message.payload = const_cast<char *>(data); message.payloadlen = size; message.qos = 0;
    return MQTTAsync_sendMessage(client, topic.c_str(), &message, nullptr);
}
int mqtt_open_audio() {
    if (!connected) { std::cerr << "MQTT not connected" << std::endl; return -1; }
    json hello = {{"type", "hello"}, {"version", 3}, {"transport", "udp"},
                  {"audio_params", {{"format", "opus"}, {"sample_rate", 16000},
                                    {"channels", 1}, {"frame_duration", 60}}}};
    auto s = hello.dump();
    return mqtt_send_text(s.data(), s.size());
}
int mqtt_send_audio(const char *data, int size) {
    if (!data || size < 0 || size > 65535) return -1;
    std::lock_guard<std::mutex> lock(audio_mutex);
    if (audio_fd < 0) return -1;
    std::vector<unsigned char> packet(16 + size);
    memcpy(packet.data(), audio_nonce, 16);
    uint16_t len = htons(size); uint32_t timestamp = 0, sequence = htonl(++send_sequence);
    memcpy(packet.data() + 2, &len, 2);
    memcpy(packet.data() + 8, &timestamp, 4);
    memcpy(packet.data() + 12, &sequence, 4);
    if (!crypt(audio_key, packet.data(), reinterpret_cast<const unsigned char *>(data), size, packet.data() + 16)) return -1;
    if (send(audio_fd, packet.data(), packet.size(), 0) != static_cast<ssize_t>(packet.size())) return -1;
    if (send_sequence == 1) std::cout << "MQTT UDP audio sent" << std::endl;
    return 0;
}
