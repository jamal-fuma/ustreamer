/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    This source file is partially based on this code:                       #
#      - https://github.com/catid/kvm/blob/master/kvm_pipeline/src           #
#                                                                            #
#    Copyright (C) 2018-2022  Maxim Devaev <mdevaev@gmail.com>               #
#                                                                            #
#    This program is free software: you can redistribute it and/or modify    #
#    it under the terms of the GNU General Public License as published by    #
#    the Free Software Foundation, either version 3 of the License, or       #
#    (at your option) any later version.                                     #
#                                                                            #
#    This program is distributed in the hope that it will be useful,         #
#    but WITHOUT ANY WARRANTY; without even the implied warranty of          #
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           #
#    GNU General Public License for more details.                            #
#                                                                            #
#    You should have received a copy of the GNU General Public License       #
#    along with this program.  If not, see <https://www.gnu.org/licenses/>.  #
#                                                                            #
*****************************************************************************/


#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <assert.h>

#include <sys/types.h>

#include <pthread.h>
#include <alsa/asoundlib.h>
#include <opus/opus.h>

#include "tools.h"
#include "jlogging.h"
#include "threading.h"
#include "queue.h"


typedef struct {
	snd_pcm_t			*pcm;
	snd_pcm_hw_params_t	*pcm_params;
	OpusEncoder			*enc;

	queue_s				*pcm_queue;
	queue_s				*enc_queue;

	pthread_t			pcm_tid;
	pthread_t			enc_tid;
	atomic_bool			run;
} audio_s;


audio_s *audio_init(const char *name, unsigned bitrate, bool stereo);
void audio_destroy(audio_s *audio);

int audio_copy_encoded(audio_s *audio, uint8_t *data, size_t *size, uint64_t *pts);
