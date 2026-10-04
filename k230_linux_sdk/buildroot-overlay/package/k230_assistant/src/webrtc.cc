#include "webrtc.h"
#include "peer_connection.h"
#include "atomic"
#include <iostream>
#include <thread>

using namespace std;

PeerConnection *peer_connection = NULL;
std::atomic<bool> loop_stop(false);
std::thread audio_send_thread;

void check_state(PeerConnection *peer_connection_tmp){
    while(getchar() != 'q'){
      usleep(100000);
    }
    loop_stop=true;
    peer_connection_close(peer_connection_tmp);
    deinit_audio();
    return;
}

static void onconnectionstatechange_task(PeerConnectionState state,
                                             void *user_data) {
  if (state == PEER_CONNECTION_DISCONNECTED ||state == PEER_CONNECTION_CLOSED) {
      printf("peer connection current state is: %s\n",peer_connection_state_to_string(state));
      peer_connection_destroy(peer_connection);
  } else if (state == PEER_CONNECTION_COMPLETED) {
      printf("peer connection current state is: %s\n",peer_connection_state_to_string(state));
      audio_send_thread=std::thread(send_audio,peer_connection);
  }
  else{
      printf("peer connection current state is: %s\n",peer_connection_state_to_string(state));
  }
}

static void on_icecandidate_task(char *description, void *user_data) {
  printf("peer connection local SDP:\n%s\n",description);
  char local_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};
  http_request(description, local_buffer);
  peer_connection_set_remote_description(peer_connection, local_buffer);
}

void start_webrtc() {
  PeerConfiguration peer_connection_config = {
      .ice_servers = {},
      .audio_codec = CODEC_OPUS,
      .video_codec = CODEC_NONE,
      .datachannel = DATA_CHANNEL_NONE,
      .onaudiotrack = [](uint8_t *data, size_t size, void *userdata) -> void {
          play_audio(data, size);
      },
      .onvideotrack = NULL,
      .on_request_keyframe = NULL,
      .user_data = NULL,
  };

  peer_connection = peer_connection_create(&peer_connection_config);
  printf("peer_connection: %p\n",peer_connection);
  if (peer_connection == NULL) {
    printf("%s\n","peer connect failed!");
  }

  std::thread thread_check_state(check_state,peer_connection);

  peer_connection_oniceconnectionstatechange(peer_connection, onconnectionstatechange_task);
  printf("%s\n","set ice connection state change callback function");
  peer_connection_onicecandidate(peer_connection, on_icecandidate_task);
  printf("%s\n","set ice candidate callback function");
  peer_connection_create_offer(peer_connection);
  printf("%s\n","create offer,set peer connection PEER_CONNECTION_NEW");

  while (!loop_stop) {
    peer_connection_loop(peer_connection);
    vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL));
  }

  // 等待外部q打断检查线程完成
  if (thread_check_state.joinable()) {
      thread_check_state.join();
  }
  // 等待音频发送线程完成
  if (audio_send_thread.joinable()) {
      audio_send_thread.join();
  }

  printf("WebRTC terminated.\n");
}
