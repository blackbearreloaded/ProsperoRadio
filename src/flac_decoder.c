#include "flac_decoder.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DRFLAC_ASSERT(expression) ((void)0)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "../vendor/dr_flac/dr_flac.h"
#pragma clang diagnostic pop

#define FLAC_DECODER_IO_CHUNK 4096U

typedef union {
    max_align_t alignment;
    struct {
        size_t size;
    } metadata;
} flac_allocation_header_t;

static void * flac_malloc(size_t size, void * user_data)
{
    flac_decoder_t * decoder = user_data;
    if(size == 0U || size > FLAC_DECODER_ALLOCATION_LIMIT ||
       decoder->allocation_bytes > FLAC_DECODER_ALLOCATION_LIMIT - size ||
       size > SIZE_MAX - sizeof(flac_allocation_header_t)) {
        decoder->read_error = FLAC_DECODER_ALLOCATION_LIMIT_EXCEEDED;
        return NULL;
    }
    flac_allocation_header_t * header =
        malloc(sizeof(*header) + size);
    if(header == NULL) return NULL;
    header->metadata.size = size;
    decoder->allocation_bytes += size;
    return header + 1;
}

static void flac_free(void * memory, void * user_data)
{
    if(memory == NULL) return;
    flac_decoder_t * decoder = user_data;
    flac_allocation_header_t * header =
        (flac_allocation_header_t *)memory - 1;
    if(header->metadata.size <= decoder->allocation_bytes)
        decoder->allocation_bytes -= header->metadata.size;
    free(header);
}

static void * flac_realloc(void * memory, size_t new_size, void * user_data)
{
    if(memory == NULL) return flac_malloc(new_size, user_data);
    if(new_size == 0U) {
        flac_free(memory, user_data);
        return NULL;
    }

    flac_decoder_t * decoder = user_data;
    flac_allocation_header_t * header =
        (flac_allocation_header_t *)memory - 1;
    const size_t old_size = header->metadata.size;
    const size_t without_old = decoder->allocation_bytes >= old_size
        ? decoder->allocation_bytes - old_size : 0U;
    if(new_size > FLAC_DECODER_ALLOCATION_LIMIT ||
       without_old > FLAC_DECODER_ALLOCATION_LIMIT - new_size ||
       new_size > SIZE_MAX - sizeof(*header)) {
        decoder->read_error = FLAC_DECODER_ALLOCATION_LIMIT_EXCEEDED;
        return NULL;
    }

    header = realloc(header, sizeof(*header) + new_size);
    if(header == NULL) return NULL;
    header->metadata.size = new_size;
    decoder->allocation_bytes = without_old + new_size;
    return header + 1;
}

static size_t flac_read_callback(void * user_data, void * output,
                                 size_t bytes_to_read)
{
    flac_decoder_t * decoder = user_data;
    uint8_t * bytes = output;
    size_t total = 0U;
    while(total < bytes_to_read) {
        if(decoder->opening &&
           decoder->stream_position >= FLAC_DECODER_OPEN_READ_LIMIT) {
            decoder->read_error = FLAC_DECODER_OPEN_READ_LIMIT_EXCEEDED;
            break;
        }
        size_t request = bytes_to_read - total;
        if(request > FLAC_DECODER_IO_CHUNK) request = FLAC_DECODER_IO_CHUNK;
        if(decoder->opening) {
            const size_t opening_remaining = FLAC_DECODER_OPEN_READ_LIMIT -
                (size_t)decoder->stream_position;
            if(request > opening_remaining) request = opening_remaining;
        }
        const int received = decoder->read(
            decoder->read_context, bytes + total, request);
        if(received < 0) {
            decoder->read_error = received;
            break;
        }
        if(received == 0) break;
        if((size_t)received > request) {
            decoder->read_error = FLAC_DECODER_ERROR;
            break;
        }
        total += (size_t)received;
        decoder->stream_position += (uint64_t)received;
    }
    return total;
}

static drflac_bool32 flac_seek_callback(void * user_data, int offset,
                                        drflac_seek_origin origin)
{
    flac_decoder_t * decoder = user_data;
    uint64_t target = decoder->stream_position;
    if(origin == DRFLAC_SEEK_SET) {
        if(offset < 0) return DRFLAC_FALSE;
        target = (uint64_t)(unsigned)offset;
    }
    else if(origin == DRFLAC_SEEK_CUR) {
        if(offset < 0) return DRFLAC_FALSE;
        if(UINT64_MAX - target < (uint64_t)(unsigned)offset)
            return DRFLAC_FALSE;
        target += (uint64_t)(unsigned)offset;
    }
    else return DRFLAC_FALSE;

    if(target < decoder->stream_position) return DRFLAC_FALSE;
    uint8_t discard[FLAC_DECODER_IO_CHUNK];
    while(decoder->stream_position < target) {
        const uint64_t remaining = target - decoder->stream_position;
        const size_t request = remaining < sizeof(discard)
            ? (size_t)remaining : sizeof(discard);
        if(flac_read_callback(decoder, discard, request) != request)
            return DRFLAC_FALSE;
    }
    return DRFLAC_TRUE;
}

int flac_decoder_open(flac_decoder_t * decoder,
                      flac_decoder_read_fn read, void * read_context)
{
    if(decoder == NULL || read == NULL || decoder->handle != NULL)
        return FLAC_DECODER_ERROR;
    memset(decoder, 0, sizeof(*decoder));
    decoder->read = read;
    decoder->read_context = read_context;
    decoder->opening = true;
    const drflac_allocation_callbacks allocation = {
        decoder, flac_malloc, flac_realloc, flac_free
    };
    drflac * handle = drflac_open(
        flac_read_callback, flac_seek_callback, NULL, decoder, &allocation);
    decoder->opening = false;
    if(handle != NULL && decoder->read_error < 0) {
        const int result = decoder->read_error;
        drflac_close(handle);
        memset(decoder, 0, sizeof(*decoder));
        return result;
    }
    if(handle == NULL) {
        const int result = decoder->read_error != 0
            ? decoder->read_error : FLAC_DECODER_OPEN_FAILED;
        memset(decoder, 0, sizeof(*decoder));
        return result;
    }

    if(handle->sampleRate < 8000U || handle->sampleRate > 192000U ||
       handle->channels < 1U || handle->channels > 2U ||
       handle->bitsPerSample < 4U || handle->bitsPerSample > 32U ||
       handle->maxBlockSizeInPCMFrames < 1U ||
       handle->maxBlockSizeInPCMFrames > FLAC_DECODER_MAX_BLOCK_FRAMES) {
        drflac_close(handle);
        memset(decoder, 0, sizeof(*decoder));
        return FLAC_DECODER_UNSUPPORTED;
    }

    decoder->handle = handle;
    decoder->sample_rate = handle->sampleRate;
    decoder->channels = handle->channels;
    decoder->bits_per_sample = handle->bitsPerSample;
    decoder->max_block_frames = handle->maxBlockSizeInPCMFrames;
    decoder->read_error = 0;
    return FLAC_DECODER_OK;
}

int flac_decoder_read_pcm(flac_decoder_t * decoder,
                          int16_t * pcm, size_t pcm_capacity_samples,
                          size_t * pcm_samples)
{
    if(decoder == NULL || decoder->handle == NULL || pcm == NULL ||
       pcm_samples == NULL || decoder->channels == 0U)
        return FLAC_DECODER_ERROR;
    *pcm_samples = 0U;
    size_t frames = pcm_capacity_samples / decoder->channels;
    if(frames == 0U) return FLAC_DECODER_OUTPUT_TOO_SMALL;
    if(frames > FLAC_DECODER_READ_FRAMES) frames = FLAC_DECODER_READ_FRAMES;
    const drflac_uint64 produced = drflac_read_pcm_frames_s16(
        decoder->handle, (drflac_uint64)frames, pcm);
    if(produced == 0U) {
        if(decoder->read_error < 0) return decoder->read_error;
        return FLAC_DECODER_EOF;
    }
    if(produced > frames) return FLAC_DECODER_ERROR;
    *pcm_samples = (size_t)produced * decoder->channels;
    return FLAC_DECODER_OK;
}

void flac_decoder_close(flac_decoder_t * decoder)
{
    if(decoder == NULL) return;
    if(decoder->handle != NULL) drflac_close(decoder->handle);
    memset(decoder, 0, sizeof(*decoder));
}
