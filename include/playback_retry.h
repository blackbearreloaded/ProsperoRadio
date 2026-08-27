#ifndef PLAYBACK_RETRY_H
#define PLAYBACK_RETRY_H

#include <stdbool.h>

#define PLAYBACK_RETRY_LIMIT 3U
#define PLAYBACK_RETRY_BASE_MS 250U
#define PLAYBACK_RETRY_RESET_MS 30000U

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
