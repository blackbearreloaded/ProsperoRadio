// PSRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FLAC_DECODER_MAX_BLOCK_FRAMES 8192U
#define FLAC_DECODER_READ_FRAMES 4096U
#define FLAC_DECODER_ALLOCATION_LIMIT (1024U * 1024U)
#define FLAC_DECODER_OPEN_READ_LIMIT (1024U * 1024U)

#define FLAC_DECODER_OK 0
#define FLAC_DECODER_EOF 1
#define FLAC_DECODER_ERROR (-4001)
#define FLAC_DECODER_OPEN_FAILED (-4002)
#define FLAC_DECODER_UNSUPPORTED (-4003)
#define FLAC_DECODER_ALLOCATION_LIMIT_EXCEEDED (-4004)
#define FLAC_DECODER_OPEN_READ_LIMIT_EXCEEDED (-4005)
#define FLAC_DECODER_OUTPUT_TOO_SMALL (-4006)

using flac_decoder_read_fn = int (*)(void * context, void * data, size_t size);

struct flac_decoder_t {
    void * handle;
    flac_decoder_read_fn read;
    void * read_context;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bits_per_sample;
    uint32_t max_block_frames;
    uint64_t stream_position;
    size_t allocation_bytes;
    int read_error;
    bool opening;
};

int flac_decoder_open(flac_decoder_t * decoder,
                      flac_decoder_read_fn read, void * read_context);
int flac_decoder_read_pcm(flac_decoder_t * decoder,
                          int16_t * pcm, size_t pcm_capacity_samples,
                          size_t * pcm_samples);
void flac_decoder_close(flac_decoder_t * decoder);
