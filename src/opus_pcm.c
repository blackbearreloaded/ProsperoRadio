#include "opus_pcm.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>

double opus_pcm_gain_factor(int16_t output_gain_q8)
{
    return pow(10.0, (double)output_gain_q8 / 5120.0);
}

int opus_pcm_apply_gain_s16(int16_t * pcm, size_t frames, size_t channels,
                            int16_t output_gain_q8)
{
    if(channels == 0U || frames > SIZE_MAX / channels) return -1;
    const size_t samples = frames * channels;
    if(samples != 0U && pcm == NULL) return -1;
    if(output_gain_q8 == 0) return 0;

    const double gain = opus_pcm_gain_factor(output_gain_q8);
    for(size_t i = 0; i < samples; ++i) {
        const double scaled = (double)pcm[i] * gain;
        if(scaled >= INT16_MAX) pcm[i] = INT16_MAX;
        else if(scaled <= INT16_MIN) pcm[i] = INT16_MIN;
        else pcm[i] = (int16_t)lround(scaled);
    }
    return 0;
}

opus_pcm_trim_result_t opus_pcm_end_trim(
    uint64_t decoded_end_frame, uint64_t page_granule_position,
    size_t final_packet_frames, int end_of_stream,
    int last_completed_packet, size_t * trim_frames)
{
    if(trim_frames == NULL) return OPUS_PCM_TRIM_INVALID;
    *trim_frames = 0U;
    if(!end_of_stream || !last_completed_packet) return OPUS_PCM_TRIM_NONE;
    if(page_granule_position == UINT64_MAX) return OPUS_PCM_TRIM_NO_GRANULE;
    if(page_granule_position > decoded_end_frame)
        return OPUS_PCM_TRIM_INVALID;

    const uint64_t trim = decoded_end_frame - page_granule_position;
    if(trim > final_packet_frames)
        return OPUS_PCM_TRIM_EXCEEDS_PACKET;
    *trim_frames = (size_t)trim;
    return OPUS_PCM_TRIM_VALID;
}
