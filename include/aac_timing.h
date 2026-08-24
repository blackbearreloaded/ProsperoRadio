#ifndef AAC_TIMING_H
#define AAC_TIMING_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

static inline uint32_t aac_adts_core_rate(const uint8_t * adts, size_t adts_size)
{
    static const uint32_t rates[] = {
        96000U, 88200U, 64000U, 48000U, 44100U, 32000U,
        24000U, 22050U, 16000U, 12000U, 11025U, 8000U
    };
    if(adts == NULL || adts_size < 7U) return 0;
    const uint32_t index = (adts[2] >> 2) & 0x0fU;
    return index < sizeof(rates) / sizeof(rates[0]) ? rates[index] : 0;
}

static inline uint32_t aac_adts_channels(const uint8_t * adts, size_t adts_size)
{
    if(adts == NULL || adts_size < 7U) return 0;
    return ((uint32_t)(adts[2] & 0x01U) << 2) | ((adts[3] >> 6) & 0x03U);
}

static inline bool aac_should_disable_he(const uint8_t * adts, size_t adts_size,
                                         uint32_t source_channels,
                                         uint32_t decoded_rate,
                                         uint32_t decoded_channels,
                                         uint32_t decoded_he)
{
    const uint32_t core_rate = aac_adts_core_rate(adts, adts_size);
    return decoded_he != 0U && source_channels == 2U &&
           aac_adts_channels(adts, adts_size) == 1U &&
           decoded_channels == 1U && core_rate != 0U &&
           decoded_rate == core_rate * 2U;
}

static inline uint32_t aac_pcm_rate(const uint8_t * adts, size_t adts_size,
                                    uint32_t channels, uint32_t pcm_bytes,
                                    uint32_t fallback_rate)
{
    if(adts == NULL || adts_size < 7U || channels == 0U) return fallback_rate;

    const uint32_t core_rate = aac_adts_core_rate(adts, adts_size);
    if(core_rate == 0U) return fallback_rate;

    const uint32_t blocks = (adts[6] & 0x03U) + 1U;
    const uint32_t frames = pcm_bytes / (sizeof(int16_t) * channels);
    const uint32_t coded_frames = 1024U * blocks;
    const uint64_t decoded_rate =
        ((uint64_t)core_rate * frames + coded_frames / 2U) / coded_frames;
    return decoded_rate >= 8000U && decoded_rate <= 192000U
        ? (uint32_t)decoded_rate : fallback_rate;
}

#endif


