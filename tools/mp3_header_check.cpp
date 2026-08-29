// ProsperoRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mp3_header.hpp"

#include <assert.h>
#include <stdint.h>

int main(void)
{
    mp3_header_t header;
    const uint8_t mpeg1_44k_stereo[] = {0xff, 0xfb, 0x90, 0x64};
    assert(mp3_header_parse(mpeg1_44k_stereo, sizeof(mpeg1_44k_stereo), &header));
    assert(header.sample_rate == 44100U);
    assert(header.channels == 2U);
    assert(header.frame_bytes == 417U);
    assert(header.samples_per_channel == 1152U);

    const uint8_t mpeg2_24k_mono[] = {0xff, 0xf3, 0x84, 0xc0};
    assert(mp3_header_parse(mpeg2_24k_mono, sizeof(mpeg2_24k_mono), &header));
    assert(header.sample_rate == 24000U);
    assert(header.channels == 1U);
    assert(header.frame_bytes == 192U);
    assert(header.samples_per_channel == 576U);

    const uint8_t mpeg25_11k_stereo[] = {0xff, 0xe3, 0x40, 0x00};
    assert(mp3_header_parse(mpeg25_11k_stereo, sizeof(mpeg25_11k_stereo), &header));
    assert(header.sample_rate == 11025U);
    assert(header.channels == 2U);
    assert(header.frame_bytes == 208U);
    assert(header.samples_per_channel == 576U);

    const uint8_t prefixed[] = {0x49, 0x44, 0x33, 0xff, 0xfb, 0x92, 0x64};
    assert(mp3_header_find(prefixed, sizeof(prefixed), &header) == 3U);
    assert(header.frame_bytes == 418U);

    const uint8_t reserved_version[] = {0xff, 0xeb, 0x90, 0x64};
    const uint8_t wrong_layer[] = {0xff, 0xfd, 0x90, 0x64};
    assert(!mp3_header_parse(reserved_version, sizeof(reserved_version), &header));
    assert(!mp3_header_parse(wrong_layer, sizeof(wrong_layer), &header));
    assert(!mp3_header_parse(nullptr, 0, &header));
    return 0;
}
