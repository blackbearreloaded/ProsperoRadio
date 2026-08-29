// ProsperoRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PLAYBACK_RETRY_LIMIT 3U
#define PLAYBACK_RETRY_BASE_MS 250U
#define PLAYBACK_RETRY_RESET_MS 30000U

inline bool playback_retry_is_stable(uint64_t output_frames,
                                      uint32_t output_rate)
{
    if(output_rate == 0U) return false;
    return output_frames >=
        (static_cast<uint64_t>(output_rate) * PLAYBACK_RETRY_RESET_MS) / 1000U;
}

inline unsigned playback_retry_next_failures(
    unsigned failures, bool stable_playback)
{
    if(stable_playback) return 0U;
    return failures < PLAYBACK_RETRY_LIMIT ? failures + 1U : failures;
}

inline bool playback_retry_allowed(unsigned failures)
{
    return failures < PLAYBACK_RETRY_LIMIT;
}

inline unsigned playback_retry_delay_ms(unsigned failures)
{
    const unsigned exponent = failures > 0U ? failures - 1U : 0U;
    return PLAYBACK_RETRY_BASE_MS << exponent;
}
