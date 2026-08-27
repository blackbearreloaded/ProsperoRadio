#include "vorbis_decoder.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vorbis_fixture.inc"

static void check(int condition, const char * message)
{
    if(condition) return;
    fprintf(stderr, "vorbis_decoder_check: FAIL: %s\n", message);
    exit(1);
}

int main(void)
{
    vorbis_decoder_t decoder;
    memset(&decoder, 0, sizeof(decoder));
    size_t consumed = 0U;
    check(vorbis_decoder_open(&decoder, VORBIS_FIXTURE, 8U, &consumed) ==
          VORBIS_DECODER_NEED_MORE, "truncated headers request more data");

    size_t available = 8U;
    int opened = VORBIS_DECODER_NEED_MORE;
    while(opened == VORBIS_DECODER_NEED_MORE && available < VORBIS_FIXTURE_SIZE) {
        available += 37U;
        if(available > VORBIS_FIXTURE_SIZE) available = VORBIS_FIXTURE_SIZE;
        opened = vorbis_decoder_open(&decoder, VORBIS_FIXTURE,
                                     available, &consumed);
    }
    check(opened == VORBIS_DECODER_OK, "split headers open");
    check(decoder.sample_rate == 48000U, "48 kHz source rate");
    check(decoder.channels == 2U, "stereo source");
    check(decoder.max_frame_frames <= VORBIS_DECODER_MAX_FRAME_FRAMES,
          "bounded maximum frame");
    check(consumed > 0U && consumed < VORBIS_FIXTURE_SIZE,
          "headers consume a bounded prefix");

    int16_t pcm[VORBIS_DECODER_MAX_FRAME_FRAMES * 2U];
    size_t at = consumed;
    size_t total_samples = 0U;
    bool nonzero = false;
    while(at < VORBIS_FIXTURE_SIZE) {
        size_t used = 0U;
        size_t samples = 0U;
        const int result = vorbis_decoder_decode(
            &decoder, VORBIS_FIXTURE + at, VORBIS_FIXTURE_SIZE - at,
            &used, pcm, sizeof(pcm) / sizeof(pcm[0]), &samples);
        check(result == VORBIS_DECODER_OK ||
              result == VORBIS_DECODER_NEED_MORE, "frame decode result");
        check(used <= VORBIS_FIXTURE_SIZE - at, "bounded input consumption");
        for(size_t i = 0U; i < samples; ++i) {
            if(pcm[i] != 0) nonzero = true;
        }
        total_samples += samples;
        at += used;
        if(used == 0U) break;
    }
    check(total_samples >= 4800U, "decoded expected PCM duration");
    check(nonzero, "decoded PCM is nonzero");
    vorbis_decoder_close(&decoder);
    check(decoder.handle == NULL, "decoder closes cleanly");

    memset(&decoder, 0, sizeof(decoder));
    available = 0U;
    consumed = 0U;
    opened = VORBIS_DECODER_NEED_MORE;
    while(opened == VORBIS_DECODER_NEED_MORE && available < VORBIS_FIXTURE_SIZE) {
        const size_t left = VORBIS_FIXTURE_SIZE - available;
        available += left < 257U ? left : 257U;
        opened = vorbis_decoder_open(&decoder, VORBIS_FIXTURE,
                                     available, &consumed);
    }
    check(opened == VORBIS_DECODER_OK, "chunked stream headers open");

    size_t buffered_at = consumed;
    size_t supplied = available;
    total_samples = 0U;
    while(buffered_at < supplied || supplied < VORBIS_FIXTURE_SIZE) {
        size_t used = 0U;
        size_t samples = 0U;
        const int result = vorbis_decoder_decode(
            &decoder, VORBIS_FIXTURE + buffered_at, supplied - buffered_at,
            &used, pcm, sizeof(pcm) / sizeof(pcm[0]), &samples);
        check(result == VORBIS_DECODER_OK ||
              result == VORBIS_DECODER_NEED_MORE,
              "chunked frame decode result");
        buffered_at += used;
        total_samples += samples;
        if(result == VORBIS_DECODER_NEED_MORE) {
            check(supplied < VORBIS_FIXTURE_SIZE,
                  "chunked decoder only requests available data");
            const size_t left = VORBIS_FIXTURE_SIZE - supplied;
            supplied += left < 257U ? left : 257U;
        }
    }
    check(total_samples >= 4800U, "chunked stream decoded expected PCM");
    vorbis_decoder_close(&decoder);
    puts("vorbis_decoder_check: PASS");
    return 0;
}
