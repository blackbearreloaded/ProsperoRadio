#ifndef PLAYBACK_RETRY_H
#define PLAYBACK_RETRY_H

#include <stdbool.h>
#include <stdint.h>

#define PLAYBACK_RETRY_LIMIT 3U
#define PLAYBACK_RETRY_BASE_MS 250U
#define PLAYBACK_RETRY_RESET_MS 30000U

static inline bool playback_retry_is_stable(uint64_t output_frames,
                                             uint32_t output_rate)
{
    if(output_rate == 0U) return false;
    return output_frames >=
        ((uint64_t)output_rate * PLAYBACK_RETRY_RESET_MS) / 1000U;
}

static inline unsigned playback_retry_next_failures(
    unsigned failures, bool stable_playback)
{
    if(stable_playback) return 0U;
    return failures < PLAYBACK_RETRY_LIMIT ? failures + 1U : failures;
}

static inline bool playback_retry_allowed(unsigned failures)
{
    return failures < PLAYBACK_RETRY_LIMIT;
}

static inline unsigned playback_retry_delay_ms(unsigned failures)
{
    const unsigned exponent = failures > 0U ? failures - 1U : 0U;
    return PLAYBACK_RETRY_BASE_MS << exponent;
}

#endif
