#include <iostream>
#include "webrtc.h"
using std::cerr;
using std::cout;
using std::endl;

int main(int argc, char *argv[])
{
    init_filter();
    peer_init();
    init_audio_play();
    init_audio_capture();
    init_audio_encoder();
    init_audio_decoder();
    start_webrtc();
    peer_deinit();
    return 0;
}