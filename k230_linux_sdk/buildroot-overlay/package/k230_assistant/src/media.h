
#include <cstddef>
#include <cstdio>
#include <sys/time.h>
#include <unistd.h>
#include "peer.h"
#include "get_pcm.h"
#include "play_pcm.h"
#define TICK_INTERVAL 15
#define pdMS_TO_TICKS(a) a
#define vTaskDelay(m)               usleep((m)*0)

#define OPUS_OUT_BUFFER_SIZE 1276  // 1276 bytes is recommended by opus_encode
#define SAMPLE_RATE 8000
#define BUFFER_SAMPLES 320
#define BITS_PER_SAMPLE 16
#define GET_CHANNELS_NUM 1
#define PUT_CHANNELS_NUM 1

#define OPUS_ENCODER_BITRATE 30000
#define OPUS_ENCODER_COMPLEXITY 0

void init_filter();
void init_audio_capture(void);
void init_audio_play(void);
void init_audio_decoder(void);
void init_audio_encoder();
void send_audio(PeerConnection *peer_connection);
void play_audio(uint8_t *data, size_t size);
void deinit_audio();
