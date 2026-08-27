#include "playback_retry.h"

#include <stdio.h>
#include <stdlib.h>

static void check(int condition, const char * message)
{
    if(condition) return;
    fprintf(stderr, "playback_retry_check: FAIL: %s\n", message);
    exit(1);
}

int main(void)
{
    check(!playback_retry_is_stable(1439999U, 48000U),
          "29.999 seconds is not stable playback");
    check(playback_retry_is_stable(1440000U, 48000U),
          "30 seconds of submitted PCM is stable playback");
    check(!playback_retry_is_stable(UINT64_MAX, 0U),
          "zero output rate is never stable");

    unsigned failures = 0U;
    failures = playback_retry_next_failures(failures, false);
    check(failures == 1U && playback_retry_allowed(failures),
          "first short failure retries");
    check(playback_retry_delay_ms(failures) == 250U,
          "first retry waits 250 ms");
    failures = playback_retry_next_failures(failures, false);
    check(failures == 2U && playback_retry_allowed(failures),
          "second short failure retries");
    check(playback_retry_delay_ms(failures) == 500U,
          "second retry waits 500 ms");
    failures = playback_retry_next_failures(failures, false);
    check(failures == PLAYBACK_RETRY_LIMIT &&
          !playback_retry_allowed(failures),
          "third consecutive short failure stops");

    failures = playback_retry_next_failures(failures, true);
    check(failures == 0U && playback_retry_allowed(failures),
          "stable playback resets the reconnect budget");
    check(playback_retry_delay_ms(failures) == 250U,
          "stable reconnect uses base delay");
    puts("playback_retry_check: PASS");
    return 0;
}
