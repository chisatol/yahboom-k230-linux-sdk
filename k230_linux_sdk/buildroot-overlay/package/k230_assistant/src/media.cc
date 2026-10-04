#include "media.h"
#include "atomic"
#include <pthread.h>
#include "alsa/asoundlib.h"
#include "opus/opus.h"
#include "webrtc_audio_improvement.h"

opus_int16 *output_buffer = NULL;
OpusDecoder *opus_decoder = NULL;

OpusEncoder *opus_encoder = NULL;
char *encoder_input_buffer = NULL;
uint8_t *encoder_output_buffer = NULL;
std::atomic<bool> audio_stop(false);


void init_filter(){
    webrtc_audio_quality_init(SAMPLE_RATE,GET_CHANNELS_NUM);
}

void init_audio_capture(void){
    initPcm(SAMPLE_RATE,GET_CHANNELS_NUM,BUFFER_SAMPLES,BITS_PER_SAMPLE);
    printf("%s\n","init_audio_capture");
}

void init_audio_play(void){
    initPlayer(SAMPLE_RATE,PUT_CHANNELS_NUM,BUFFER_SAMPLES/2,BITS_PER_SAMPLE);
    printf("%s\n","init_audio_capture");
}

void init_audio_decoder(void){
    int decoder_error = 0;
    opus_decoder = opus_decoder_create(SAMPLE_RATE, 1, &decoder_error);
    if (decoder_error != OPUS_OK) {
      printf("Failed to create OPUS decoder");
      return;
    }
    output_buffer = (opus_int16 *)malloc(BUFFER_SAMPLES/2 * sizeof(opus_int16));
    printf("%s\n","init_audio_decoder");
}

void init_audio_encoder(){
    int encoder_error;
    opus_encoder = opus_encoder_create(SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP,
                                      &encoder_error);
    if (encoder_error != OPUS_OK) {
      printf("Failed to create OPUS encoder");
      return;
    }

    if (opus_encoder_init(opus_encoder, SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP) !=OPUS_OK) {
      printf("Failed to initialize OPUS encoder");
      return;
    }
    opus_encoder_ctl(opus_encoder, OPUS_SET_BITRATE(OPUS_ENCODER_BITRATE));
    opus_encoder_ctl(opus_encoder, OPUS_SET_COMPLEXITY(OPUS_ENCODER_COMPLEXITY));
    opus_encoder_ctl(opus_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    encoder_input_buffer = (char *)malloc(BUFFER_SAMPLES*GET_CHANNELS_NUM*BITS_PER_SAMPLE/8);
    encoder_output_buffer = (uint8_t *)malloc(OPUS_OUT_BUFFER_SIZE);
    printf("%s\n","init_audio_encoder");
}

void send_audio(PeerConnection *peer_connection){
  short out_buf[BUFFER_SAMPLES];
  while (!audio_stop) {
    //Send data once every 15ms,Send the collected data from k230 using the 'peer_connection_send_audio' interface.encoder output->pc.audio_rb
    getPcm(encoder_input_buffer);
    if ( webrtc_enhance_audio_quality(encoder_input_buffer,out_buf) < 0)
    {
        continue;
    }
    auto encoded_size =opus_encode(opus_encoder, (opus_int16 *)out_buf, BUFFER_SAMPLES,encoder_output_buffer, OPUS_OUT_BUFFER_SIZE);
    int res=peer_connection_send_audio(peer_connection, encoder_output_buffer,encoded_size);
    vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL));
  }
}

void play_audio(uint8_t *data, size_t size){
    int decoded_size =opus_decode(opus_decoder, data, size, output_buffer, BUFFER_SAMPLES, 0);
    webrtc_process_reference_audio((char*)output_buffer, decoded_size*2);
    if (decoded_size > 0) {//decoded_size 160
      playPcm((char*)output_buffer);
    }
}

void deinit_audio(){
  audio_stop=true;
  usleep(100000);
  deinitPcm();
  deinitPlayer();
  free(opus_encoder);
  free(opus_decoder);
  free(output_buffer);
  free(encoder_input_buffer);
  free(encoder_output_buffer);
}