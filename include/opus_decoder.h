#ifndef OPUS_DECODER_H
#define OPUS_DECODER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t context;
    void * allocation;
    void * state;
    unsigned channels;
    bool celt_only;
    bool module_loaded;
    bool initialized;
    bool created;
} opus_decoder_t;

int opus_decoder_open(opus_decoder_t * decoder, unsigned channels,
                      bool celt_only);
int opus_decoder_decode(opus_decoder_t * decoder,
                        const uint8_t * packet, size_t packet_size,
                        int16_t * pcm, size_t pcm_capacity_bytes,
                        size_t * produced_bytes);
void opus_decoder_close(opus_decoder_t * decoder);

#endif
