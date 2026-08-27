#ifndef VORBIS_DECODER_H
#define VORBIS_DECODER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VORBIS_DECODER_MAX_FRAME_FRAMES 8192U
#define VORBIS_DECODER_OK 0
#define VORBIS_DECODER_NEED_MORE 1
#define VORBIS_DECODER_ERROR (-3001)
#define VORBIS_DECODER_UNSUPPORTED (-3002)
#define VORBIS_DECODER_OUTPUT_TOO_SMALL (-3003)
#define VORBIS_DECODER_LIBRARY_ERROR_BASE (-3100)

typedef struct {
    void * handle;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t max_frame_frames;
    int last_error;
} vorbis_decoder_t;

int vorbis_decoder_open(vorbis_decoder_t * decoder,
                        const uint8_t * data, size_t size,
                        size_t * consumed);
int vorbis_decoder_decode(vorbis_decoder_t * decoder,
                          const uint8_t * data, size_t size,
                          size_t * consumed,
                          int16_t * pcm, size_t pcm_capacity_samples,
                          size_t * pcm_samples);
void vorbis_decoder_close(vorbis_decoder_t * decoder);

#ifdef __cplusplus
}
#endif

#endif
