// PS5 Radio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "aac_timing.hpp"

#include <assert.h>
#include <stdint.h>

int main(void)
{
    const uint8_t lc_48k[7] = {0xff, 0xf1, 0x4c, 0, 0, 0, 0};
    const uint8_t he_24k[7] = {0xff, 0xf1, 0x58, 0x40, 0, 0, 0};
    const uint8_t he_24k_stereo[7] = {0xff, 0xf1, 0x58, 0x80, 0, 0, 0};
    assert(aac_adts_core_rate(he_24k, sizeof(he_24k)) == 24000U);
    assert(aac_adts_channels(he_24k, sizeof(he_24k)) == 1U);
    assert(aac_should_disable_he(he_24k, sizeof(he_24k), 2, 48000, 1, 1));
    assert(!aac_should_disable_he(he_24k, sizeof(he_24k), 1, 48000, 1, 1));
    assert(!aac_should_disable_he(he_24k_stereo, sizeof(he_24k_stereo), 2, 48000, 2, 1));
    assert(aac_pcm_rate(lc_48k, sizeof(lc_48k), 2, 4096, 1) == 48000U);
    assert(aac_pcm_rate(he_24k, sizeof(he_24k), 2, 8192, 1) == 48000U);
    assert(aac_pcm_rate(he_24k, sizeof(he_24k), 1, 2048, 48000) == 24000U);
    assert(aac_pcm_rate(lc_48k, sizeof(lc_48k), 2, 8192, 1) == 96000U);
    assert(aac_pcm_rate(nullptr, 0, 0, 0, 44100) == 44100U);
    return 0;
}
