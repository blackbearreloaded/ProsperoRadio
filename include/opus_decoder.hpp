// PSRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct opus_decoder_t {
    uint32_t context;
    void * allocation;
    void * state;
    unsigned channels;
    bool celt_only;
    bool module_loaded;
    bool initialized;
    bool created;
};

int opus_decoder_open(opus_decoder_t * decoder, unsigned channels,
                      bool celt_only);
int opus_decoder_decode(opus_decoder_t * decoder,
                        const uint8_t * packet, size_t packet_size,
                        int16_t * pcm, size_t pcm_capacity_bytes,
                        size_t * produced_bytes);
void opus_decoder_close(opus_decoder_t * decoder);
