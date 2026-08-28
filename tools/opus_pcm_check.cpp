// PSRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "opus_pcm.hpp"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

static void check_gain(void)
{
    assert(fabs(opus_pcm_gain_factor(0) - 1.0) < 0.000001);
    assert(fabs(opus_pcm_gain_factor(1541) - 2.0) < 0.001);
    assert(fabs(opus_pcm_gain_factor(-1541) - 0.5) < 0.001);

    int16_t unchanged[] = {1234, -1234};
    assert(opus_pcm_apply_gain_s16(unchanged, 1U, 2U, 0) == 0);
    assert(unchanged[0] == 1234 && unchanged[1] == -1234);

    int16_t louder[] = {1000, -1000, 20000, -20000};
    assert(opus_pcm_apply_gain_s16(louder, 2U, 2U, 1541) == 0);
    assert(louder[0] == 2000 && louder[1] == -2000);
    assert(louder[2] == INT16_MAX && louder[3] == INT16_MIN);

    int16_t quieter[] = {2000, -2000};
    assert(opus_pcm_apply_gain_s16(quieter, 1U, 2U, -1541) == 0);
    assert(quieter[0] == 1000 && quieter[1] == -1000);
    assert(opus_pcm_apply_gain_s16(nullptr, 1U, 2U, 0) == -1);
    assert(opus_pcm_apply_gain_s16(nullptr, 0U, 2U, 0) == 0);
}

static void check_end_trim(void)
{
    size_t trim = 99U;
    assert(opus_pcm_end_trim(2880U, 2880U, 960U, 0, 1, &trim) ==
           OPUS_PCM_TRIM_NONE);
    assert(trim == 0U);

    trim = 99U;
    assert(opus_pcm_end_trim(2880U, 2880U, 960U, 1, 0, &trim) ==
           OPUS_PCM_TRIM_NONE);
    assert(trim == 0U);

    trim = 99U;
    assert(opus_pcm_end_trim(2880U, 2880U, 960U, 1, 1, &trim) ==
           OPUS_PCM_TRIM_VALID);
    assert(trim == 0U);

    assert(opus_pcm_end_trim(5760U, 5520U, 960U, 1, 1, &trim) ==
           OPUS_PCM_TRIM_VALID);
    assert(trim == 240U);

    trim = 99U;
    assert(opus_pcm_end_trim(5760U, UINT64_MAX, 960U, 1, 1, &trim) ==
           OPUS_PCM_TRIM_NO_GRANULE);
    assert(trim == 0U);

    assert(opus_pcm_end_trim(5760U, 4000U, 960U, 1, 1, &trim) ==
           OPUS_PCM_TRIM_EXCEEDS_PACKET);
    assert(trim == 0U);
    assert(opus_pcm_end_trim(5760U, 6000U, 960U, 1, 1, &trim) ==
           OPUS_PCM_TRIM_INVALID);
    assert(opus_pcm_end_trim(0U, 0U, 0U, 1, 1, nullptr) ==
           OPUS_PCM_TRIM_INVALID);
}

int main(void)
{
    check_gain();
    check_end_trim();
    puts("opus_pcm_check: ok");
    return 0;
}
