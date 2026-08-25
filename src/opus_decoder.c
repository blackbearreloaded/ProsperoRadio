#include "opus_decoder.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define OPUS_DECODER_MODULE UINT32_C(0x80000069)
#define OPUS_STATE_ALIGNMENT 64U

extern int sceSysmoduleLoadModuleInternal(uint32_t module_id);
extern int sceSysmoduleUnloadModuleInternal(uint32_t module_id);
extern int sceOpusDecInitialize(uint32_t * context);
extern int sceOpusDecTerminate(uint32_t * context);
extern int sceOpusDecGetSize(int channels);
extern int sceOpusDecCreateEx(uint32_t * context, void * state,
                              int sample_rate, int channels);
extern int sceOpusDecDecode(void * state, const uint8_t * packet,
                            int packet_bytes, int16_t * pcm,
                            int pcm_capacity_bytes);
extern int sceOpusDecDestroy(void * state);

int opus_decoder_open(opus_decoder_t * decoder, unsigned channels)
{
    if(decoder == NULL || (channels != 1U && channels != 2U)) return -1;
    memset(decoder, 0, sizeof(*decoder));
    decoder->channels = channels;

    int result = sceSysmoduleLoadModuleInternal(OPUS_DECODER_MODULE);
    if(result < 0) return result;
    decoder->module_loaded = true;

    const int state_size = sceOpusDecGetSize((int)channels);
    if(state_size <= 0 || state_size > INT_MAX - (int)OPUS_STATE_ALIGNMENT) {
        opus_decoder_close(decoder);
        return state_size < 0 ? state_size : -1;
    }
    decoder->allocation = malloc((size_t)state_size + OPUS_STATE_ALIGNMENT - 1U);
    if(decoder->allocation == NULL) {
        opus_decoder_close(decoder);
        return -1;
    }
    const uintptr_t address = (uintptr_t)decoder->allocation;
    decoder->state = (void *)((address + OPUS_STATE_ALIGNMENT - 1U) &
                              ~(uintptr_t)(OPUS_STATE_ALIGNMENT - 1U));

    result = sceOpusDecInitialize(&decoder->context);
    if(result < 0) {
        opus_decoder_close(decoder);
        return result;
    }
    decoder->initialized = true;

    result = sceOpusDecCreateEx(&decoder->context, decoder->state, 48000, (int)channels);
    if(result < 0) {
        opus_decoder_close(decoder);
        return result;
    }
    decoder->created = true;
    return 0;
}

int opus_decoder_decode(opus_decoder_t * decoder,
                        const uint8_t * packet, size_t packet_size,
                        int16_t * pcm, size_t pcm_capacity_bytes,
                        size_t * produced_bytes)
{
    if(decoder == NULL || !decoder->created || packet == NULL || pcm == NULL ||
       produced_bytes == NULL || packet_size == 0 || packet_size > INT_MAX ||
       pcm_capacity_bytes == 0 || pcm_capacity_bytes > INT_MAX) return -1;

    const int result = sceOpusDecDecode(decoder->state, packet, (int)packet_size,
                                        pcm, (int)pcm_capacity_bytes);
    if(result < 0) return result;
    if((size_t)result > pcm_capacity_bytes || (result & 1) != 0) return -1;
    *produced_bytes = (size_t)result;
    return 0;
}

void opus_decoder_close(opus_decoder_t * decoder)
{
    if(decoder == NULL) return;
    if(decoder->created) sceOpusDecDestroy(decoder->state);
    if(decoder->initialized) sceOpusDecTerminate(&decoder->context);
    free(decoder->allocation);
    if(decoder->module_loaded) sceSysmoduleUnloadModuleInternal(OPUS_DECODER_MODULE);
    memset(decoder, 0, sizeof(*decoder));
}
