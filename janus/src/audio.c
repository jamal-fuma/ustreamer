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


#include "audio.h"


#define JLOG_INFO_ALSA(_msg, ...)			JLOG_INFO("ALSA: " _msg, ##__VA_ARGS__)
#define JLOG_PERROR_ALSA(_err, _msg, ...)	JLOG_ERROR("ALSA: " _msg ": %s", ##__VA_ARGS__, snd_strerror(_err))

#define JLOG_INFO_OPUS(_msg, ...)			JLOG_INFO("OPUS: " _msg, ##__VA_ARGS__)
#define JLOG_PERROR_OPUS(_err, _msg, ...)	JLOG_ERROR("OPUS: " _msg ": %s", ##__VA_ARGS__, opus_strerror(_err))

#define FRAMES (960 * 6)
#define PCM_DATA_SIZE (FRAMES * 2)
#define RAW_DATA_SIZE (PCM_DATA_SIZE * sizeof(opus_int16))


typedef struct {
	opus_int16	data[PCM_DATA_SIZE];
	uint64_t	pts;
} _pcm_buf_s;

typedef struct {
	uint8_t		data[RAW_DATA_SIZE]; // Worst
	size_t		used;
	uint64_t	pts;
} _enc_buf_s;


static void *_pcm_thread(void *v_audio);
static void *_enc_thread(void *v_audio);


audio_s *audio_init(const char *name, unsigned bitrate, bool stereo) {
	const unsigned channels = stereo + 1;
	unsigned pcm_bitrate = 48000;
	int err;

	audio_s *audio;
	A_CALLOC(audio, 1);
	audio->pcm_queue = queue_init(8);
	audio->enc_queue = queue_init(8);
	atomic_init(&audio->run, false);

	{
		JLOG_INFO_ALSA("Opening capture device=%s ...", name);
		if ((err = snd_pcm_open(&audio->pcm, name, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
			audio->pcm = NULL;
			JLOG_PERROR_ALSA(err, "Can't open capture");
			goto error;
		}

		assert(!snd_pcm_hw_params_malloc(&audio->pcm_params));
#		define SET_PARAM(_msg, _func, ...) { \
				if ((err = _func(audio->pcm, audio->pcm_params, ##__VA_ARGS__)) < 0) { \
					JLOG_PERROR_ALSA(err, _msg); \
					goto error; \
				} \
			}
		SET_PARAM("Can't initialize HW params",	snd_pcm_hw_params_any);
		SET_PARAM("Can't set access type",		snd_pcm_hw_params_set_access, SND_PCM_ACCESS_RW_INTERLEAVED);
		SET_PARAM("Can't set channels number",	snd_pcm_hw_params_set_channels, channels);
		SET_PARAM("Can't set sampling format",	snd_pcm_hw_params_set_format, SND_PCM_FORMAT_S16_LE);
		SET_PARAM("Can't set sampling rate",	snd_pcm_hw_params_set_rate_near, &pcm_bitrate, 0);
		SET_PARAM("Can't apply HW params",		snd_pcm_hw_params);
#		undef SET_PARAM
	}

	{
		// OPUS_APPLICATION_VOIP
		audio->enc = opus_encoder_create(pcm_bitrate, channels, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err);
		if (err < 0) {
			audio->enc = NULL;
			JLOG_PERROR_OPUS(err, "Can't create encoder");
			goto error;
		}

#		define SET_PARAM(_msg, _ctl) { \
				if ((err = opus_encoder_ctl(audio->enc, _ctl)) < 0) { \
					JLOG_PERROR_OPUS(err, _msg); \
					goto error; \
				} \
			}
		SET_PARAM("Can't set bitrate", OPUS_SET_BITRATE(bitrate));
		SET_PARAM("Can't set max bandwidth", OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND));
		SET_PARAM("Can't set FEC", OPUS_SET_INBAND_FEC(1));
		SET_PARAM("Can't set exploss", OPUS_SET_PACKET_LOSS_PERC(10));
#		undef SET_PARAM
	}

	atomic_store(&audio->run, true);
	A_THREAD_CREATE(&audio->enc_tid, _enc_thread, audio);
	A_THREAD_CREATE(&audio->pcm_tid, _pcm_thread, audio);

	return audio;

	error:
		audio_destroy(audio);
		return NULL;
}

void audio_destroy(audio_s *audio) {
	if (atomic_load(&audio->run)) {
		atomic_store(&audio->run, false);
		A_THREAD_JOIN(audio->pcm_tid);
		A_THREAD_JOIN(audio->enc_tid);
	}
	if (audio->enc) {
		opus_encoder_destroy(audio->enc);
	}
	if (audio->pcm) {
		snd_pcm_close(audio->pcm);
	}
	if (audio->pcm_params) {
		snd_pcm_hw_params_free(audio->pcm_params);
	}
#	define FREE_QUEUE(_suffix) { \
			while (queue_get_free(audio->_suffix##_queue)) { \
				_##_suffix##_buf_s *ptr; \
				assert(!queue_get(audio->_suffix##_queue, (void **)&ptr, 1)); \
				free(ptr); \
			} \
			queue_destroy(audio->_suffix##_queue); \
		}
	FREE_QUEUE(enc);
	FREE_QUEUE(pcm);
#	undef FREE_QUEUE
	free(audio);
}

int audio_copy_encoded(audio_s *audio, uint8_t *data, size_t *size, uint64_t *pts) {
	_enc_buf_s *in;
	if (!queue_get(audio->enc_queue, (void **)&in, 1)) {
		if (*size < in->used) {
			free(in);
			return -1;
		}
		memcpy(data, in->data, in->used);
		*size = in->used;
		*pts = in->pts;
		free(in);
		return 0;
	}
	return -1;
}

static void *_pcm_thread(void *v_audio) {
	audio_s *audio = (audio_s *)v_audio;

	while (atomic_load(&audio->run)) {
		uint8_t in[RAW_DATA_SIZE];
		int frames = snd_pcm_readi(audio->pcm, in, FRAMES);
		if (frames < 0) {
			JLOG_PERROR_ALSA(frames, "Can't read PCM frame");
			break;
		} else if (frames < FRAMES) {
			JLOG_ERROR("Too few PCM frames");
			break;
		}
		uint64_t pts = get_now_monotonic_u64();

		if (queue_get_free(audio->pcm_queue)) {
			_pcm_buf_s *out;
			A_CALLOC(out, 1);
			/*for (unsigned index = 0; index < PCM_DATA_SIZE; ++index) {
				out->data[index] = (opus_int16)in[index * 2 + 1] << 8 | in[index * 2];
			}*/
			memcpy(out->data, in, PCM_DATA_SIZE);
			out->pts = pts;
			assert(!queue_put(audio->pcm_queue, out, 1));
		} else {
			JLOG_INFO_ALSA("PCM queue is full");
		}
	}
	return NULL;
}

#include <stdio.h>

static void *_enc_thread(void *v_audio) {
	audio_s *audio = (audio_s *)v_audio;

	//FILE *fp = fopen("/tmp/rec", "wb");

	while (atomic_load(&audio->run)) {
		_pcm_buf_s *in;
		if (!queue_get(audio->pcm_queue, (void **)&in, 1)) {
			_enc_buf_s *out;
			A_CALLOC(out, 1);
			out->pts = in->pts;
	//		fwrite(in->data, 2, FRAMES, fp);
	//		fflush(fp);
			int size = opus_encode(audio->enc, in->data, FRAMES, out->data, RAW_DATA_SIZE);
			free(in);
			if (size < 0) {
				JLOG_PERROR_OPUS(size, "Can't encode PCM frame");
				free(out);
				break;
			}
			out->used = size;
			if (queue_get_free(audio->enc_queue)) {
				assert(!queue_put(audio->enc_queue, out, 1));
			} else {
				JLOG_INFO_OPUS("Encode queue is full");
				free(out);
			}
		}
	}
	return NULL;
}
