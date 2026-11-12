#pragma once
#include <string>
#include <cstddef>

using mqtt_data_callback = void (*)(const char *, size_t);
void mqtt_configure(const std::string &, mqtt_data_callback, mqtt_data_callback);
bool mqtt_enabled();
int mqtt_start();
int mqtt_open_audio();
void mqtt_close_audio();
int mqtt_send_text(const char *, int);
int mqtt_send_audio(const char *, int);
