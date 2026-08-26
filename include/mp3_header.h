#ifndef MP3_HEADER_H
#define MP3_HEADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t frame_bytes;
    uint32_t samples_per_channel;
} mp3_header_t;

static inline bool mp3_header_parse(const uint8_t * data, size_t size,
                                    mp3_header_t * header)
{
    static const uint16_t mpeg1_bitrates[] = {
        0U, 32U, 40U, 48U, 56U, 64U, 80U, 96U,
        112U, 128U, 160U, 192U, 224U, 256U, 320U, 0U
    };
    static const uint16_t mpeg2_bitrates[] = {
        0U, 8U, 16U, 24U, 32U, 40U, 48U, 56U,
        64U, 80U, 96U, 112U, 128U, 144U, 160U, 0U
    };
    static const uint32_t base_rates[] = {44100U, 48000U, 32000U};

    if(data == NULL || header == NULL || size < 4U || data[0] != 0xffU ||
       (data[1] & 0xe0U) != 0xe0U) return false;

    const uint32_t version = (data[1] >> 3U) & 0x03U;
    const uint32_t layer = (data[1] >> 1U) & 0x03U;
    const uint32_t bitrate_index = data[2] >> 4U;
    const uint32_t rate_index = (data[2] >> 2U) & 0x03U;
    if(version == 1U || layer != 1U || bitrate_index == 0U ||
       bitrate_index == 15U || rate_index == 3U) return false;

    uint32_t rate = base_rates[rate_index];
    if(version == 2U) rate /= 2U;
    else if(version == 0U) rate /= 4U;
    const bool mpeg1 = version == 3U;
    const uint32_t bitrate_kbps = mpeg1
        ? mpeg1_bitrates[bitrate_index] : mpeg2_bitrates[bitrate_index];
    const uint32_t coefficient = mpeg1 ? 144U : 72U;
    const uint32_t frame_bytes = coefficient * bitrate_kbps * 1000U / rate +
                                 ((data[2] >> 1U) & 0x01U);
    if(frame_bytes < 4U || frame_bytes > 1441U) return false;

    header->sample_rate = rate;
    header->channels = (data[3] >> 6U) == 3U ? 1U : 2U;
    header->frame_bytes = frame_bytes;
    header->samples_per_channel = mpeg1 ? 1152U : 576U;
    return true;
}

static inline size_t mp3_header_find(const uint8_t * data, size_t size,
                                     mp3_header_t * header)
{
    if(data == NULL || header == NULL) return size;
    for(size_t offset = 0; offset + 4U <= size; ++offset) {
        if(mp3_header_parse(data + offset, size - offset, header)) return offset;
    }
    return size;
}

#endif
