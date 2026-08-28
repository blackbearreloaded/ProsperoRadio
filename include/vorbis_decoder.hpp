// PSRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>
#include <stdint.h>

#define VORBIS_DECODER_MAX_FRAME_FRAMES 8192U
#define VORBIS_DECODER_OK 0
#define VORBIS_DECODER_NEED_MORE 1
#define VORBIS_DECODER_ERROR (-3001)
#define VORBIS_DECODER_UNSUPPORTED (-3002)
#define VORBIS_DECODER_OUTPUT_TOO_SMALL (-3003)
#define VORBIS_DECODER_INPUT_BOUNDS (-3004)
#define VORBIS_DECODER_CHANNEL_MISMATCH (-3005)
#define VORBIS_DECODER_BUFFER_FULL (-3006)
#define VORBIS_DECODER_NULL_OUTPUT (-3007)
#define VORBIS_DECODER_FRAME_BOUNDS (-3008)
#define VORBIS_DECODER_LIBRARY_ERROR_BASE (-3100)

struct vorbis_decoder_t {
    void * handle;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t max_frame_frames;
    int last_error;
};

int vorbis_decoder_open(vorbis_decoder_t * decoder,
                        const uint8_t * data, size_t size,
                        size_t * consumed);
int vorbis_decoder_decode(vorbis_decoder_t * decoder,
                          const uint8_t * data, size_t size,
                          size_t * consumed,
                          int16_t * pcm, size_t pcm_capacity_samples,
                          size_t * pcm_samples);
void vorbis_decoder_close(vorbis_decoder_t * decoder);
