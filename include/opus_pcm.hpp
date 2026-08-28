// PS5 Radio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>
#include <stdint.h>

enum opus_pcm_trim_result_t {
    OPUS_PCM_TRIM_INVALID = -1,
    OPUS_PCM_TRIM_NO_GRANULE = -2,
    OPUS_PCM_TRIM_EXCEEDS_PACKET = -3,
    OPUS_PCM_TRIM_NONE = 0,
    OPUS_PCM_TRIM_VALID = 1
};

double opus_pcm_gain_factor(int16_t output_gain_q8);

int opus_pcm_apply_gain_s16(int16_t * pcm, size_t frames, size_t channels,
                            int16_t output_gain_q8);

/* decoded_end_frame is the cumulative 48 kHz position after this packet. */
opus_pcm_trim_result_t opus_pcm_end_trim(
    uint64_t decoded_end_frame, uint64_t page_granule_position,
    size_t final_packet_frames, int end_of_stream,
    int last_completed_packet, size_t * trim_frames);
