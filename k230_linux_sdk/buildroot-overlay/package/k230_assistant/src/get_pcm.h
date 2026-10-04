#ifndef GET_PCM_H
#define GET_PCM_H

#include<stdio.h>
#include<string.h>
#include <vector>
#include "opus/opus.h"

using namespace std;

int initPcm(int sample_rate, int num_channels,int frame_samples,int bits_per_sample);

void getPcm(char* encoder_input_buffer);

int deinitPcm();

#endif // GET_PCM_H