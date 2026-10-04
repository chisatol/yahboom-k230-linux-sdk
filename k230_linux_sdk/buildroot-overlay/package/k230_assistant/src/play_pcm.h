#ifndef __ALSA_PLAY_PCM_H__
#define __ALSA_PLAY_PCM_H__

#include <stdio.h>
#include <string.h>
#include "opus/opus.h"

int initPlayer(int sample_rate,int channels,int frame_samples,int bits_per_sample);

int playPcm(char *decoder_output_buffer);

int deinitPlayer();

#endif