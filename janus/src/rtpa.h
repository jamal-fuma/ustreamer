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
#include <inttypes.h>

#include <sys/types.h>

#include "tools.h"
#include "threading.h"
#include "audio.h"
#include "rtp.h"


typedef struct {
	rtp_s			*rtp;
	unsigned		bitrate;
	bool			stereo;
} rtpa_s;


rtpa_s *rtpa_init(unsigned bitrate, bool stereo);
void rtpa_destroy(rtpa_s *rtpa);

char *rtpa_make_sdp(rtpa_s *rtpa);
void rtpa_wrap(rtpa_s *rtpa, uint64_t now, const uint8_t *data, size_t size, rtp_callback_f callback);
