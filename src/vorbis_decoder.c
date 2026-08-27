#include "vorbis_decoder.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#if defined(__clang__)
#define PSRADIO_VORBIS_NO_OPT __attribute__((noinline, optnone))
#else
#define PSRADIO_VORBIS_NO_OPT
#endif

static PSRADIO_VORBIS_NO_OPT double vorbis_sin(double value)
{
    return sin(value);
}

static PSRADIO_VORBIS_NO_OPT double vorbis_cos(double value)
{
    return cos(value);
}

static PSRADIO_VORBIS_NO_OPT double vorbis_ldexp(double value, int exponent)
{
    return scalbn(value, exponent);
}

#define STB_VORBIS_MAX_CHANNELS 2
#define STB_VORBIS_NO_INTEGER_CONVERSION
#define STB_VORBIS_NO_PULLDATA_API
#define STB_VORBIS_NO_STDIO
#define sin vorbis_sin
#define cos vorbis_cos
#define ldexp vorbis_ldexp
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "../vendor/stb/stb_vorbis.c"
#pragma clang diagnostic pop
#undef ldexp
#undef cos
#undef sin
#undef PSRADIO_VORBIS_NO_OPT

static int16_t float_to_s16(float sample)
{
    if(sample <= -1.0f) return INT16_MIN;
    if(sample >= 1.0f) return INT16_MAX;
    return (int16_t)(sample * 32768.0f);
}

int vorbis_decoder_open(vorbis_decoder_t * decoder,
                        const uint8_t * data, size_t size,
                        size_t * consumed)
{
    if(decoder == NULL || data == NULL || consumed == NULL ||
       decoder->handle != NULL || size > INT_MAX) return VORBIS_DECODER_ERROR;
    *consumed = 0U;
    int used = 0;
    int error = 0;
    stb_vorbis * handle = stb_vorbis_open_pushdata(
        data, (int)size, &used, &error, NULL);
    decoder->last_error = error;
    if(handle == NULL) {
        return error == VORBIS_need_more_data
            ? VORBIS_DECODER_NEED_MORE
            : VORBIS_DECODER_LIBRARY_ERROR_BASE - error;
    }

    const stb_vorbis_info info = stb_vorbis_get_info(handle);
    if(used < 0 || (size_t)used > size ||
       info.sample_rate < 8000U || info.sample_rate > 192000U ||
       info.channels < 1 || info.channels > 2 || info.max_frame_size < 1 ||
       info.max_frame_size > (int)VORBIS_DECODER_MAX_FRAME_FRAMES) {
        stb_vorbis_close(handle);
        return VORBIS_DECODER_UNSUPPORTED;
    }

    decoder->handle = handle;
    decoder->sample_rate = info.sample_rate;
    decoder->channels = (uint32_t)info.channels;
    decoder->max_frame_frames = (uint32_t)info.max_frame_size;
    decoder->last_error = 0;
    *consumed = (size_t)used;
    return VORBIS_DECODER_OK;
}

int vorbis_decoder_decode(vorbis_decoder_t * decoder,
                          const uint8_t * data, size_t size,
                          size_t * consumed,
                          int16_t * pcm, size_t pcm_capacity_samples,
                          size_t * pcm_samples)
{
    if(decoder == NULL || decoder->handle == NULL || data == NULL ||
       consumed == NULL || pcm == NULL || pcm_samples == NULL ||
       size > INT_MAX) return VORBIS_DECODER_ERROR;
    *consumed = 0U;
    *pcm_samples = 0U;

    int channels = 0;
    int frames = 0;
    float ** output = NULL;
    const int used = stb_vorbis_decode_frame_pushdata(
        decoder->handle, data, (int)size, &channels, &output, &frames);
    const int error = stb_vorbis_get_error(decoder->handle);
    decoder->last_error = error;
    if(error != VORBIS__no_error && error != VORBIS_need_more_data)
        return VORBIS_DECODER_LIBRARY_ERROR_BASE - error;
    if(used < 0 || (size_t)used > size || frames < 0)
        return VORBIS_DECODER_INPUT_BOUNDS;
    *consumed = (size_t)used;
    if(frames == 0)
        return used == 0 ? VORBIS_DECODER_NEED_MORE : VORBIS_DECODER_OK;
    if(channels != (int)decoder->channels || output == NULL ||
       (uint32_t)frames > decoder->max_frame_frames)
        return VORBIS_DECODER_OUTPUT_SHAPE;

    const size_t samples = (size_t)frames * decoder->channels;
    if(samples > pcm_capacity_samples) return VORBIS_DECODER_OUTPUT_TOO_SMALL;
    for(int frame = 0; frame < frames; ++frame) {
        for(int channel = 0; channel < channels; ++channel) {
            pcm[(size_t)frame * decoder->channels + (size_t)channel] =
                float_to_s16(output[channel][frame]);
        }
    }
    *pcm_samples = samples;
    return VORBIS_DECODER_OK;
}

void vorbis_decoder_close(vorbis_decoder_t * decoder)
{
    if(decoder == NULL) return;
    if(decoder->handle != NULL) stb_vorbis_close(decoder->handle);
    memset(decoder, 0, sizeof(*decoder));
}
