#include "flac_decoder.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flac_fixture.inc"

typedef struct {
    const uint8_t * data;
    size_t size;
    size_t at;
    size_t max_chunk;
} memory_reader_t;

static void check(bool condition, const char * message)
{
    if(condition) return;
    fprintf(stderr, "flac_decoder_check: FAIL: %s\n", message);
    exit(1);
}

static int memory_read(void * context, void * output, size_t size)
{
    memory_reader_t * reader = context;
    size_t available = reader->size - reader->at;
    if(size > available) size = available;
    if(reader->max_chunk != 0U && size > reader->max_chunk)
        size = reader->max_chunk;
    memcpy(output, reader->data + reader->at, size);
    reader->at += size;
    return (int)size;
}

static size_t decode_fixture(size_t input_size, size_t max_chunk,
                             bool expect_complete)
{
    memory_reader_t reader = {
        FLAC_FIXTURE, input_size, 0U, max_chunk
    };
    flac_decoder_t decoder;
    memset(&decoder, 0, sizeof(decoder));
    check(flac_decoder_open(&decoder, memory_read, &reader) == FLAC_DECODER_OK,
          "fixture opens");
    check(decoder.sample_rate == 44100U, "sample rate is 44.1 kHz");
    check(decoder.channels == 2U, "fixture is stereo");
    check(decoder.bits_per_sample == 16U, "fixture is signed 16-bit source");
    check(decoder.max_block_frames <= FLAC_DECODER_MAX_BLOCK_FRAMES,
          "block frame bound");
    check(decoder.allocation_bytes <= FLAC_DECODER_ALLOCATION_LIMIT,
          "allocation bound");

    int16_t pcm[FLAC_DECODER_READ_FRAMES * 2U];
    size_t total_samples = 0U;
    bool nonzero = false;
    for(;;) {
        size_t samples = 0U;
        const int result = flac_decoder_read_pcm(
            &decoder, pcm, sizeof(pcm) / sizeof(pcm[0]), &samples);
        if(result == FLAC_DECODER_EOF) break;
        check(result == FLAC_DECODER_OK, "fixture decode succeeds");
        check(samples <= sizeof(pcm) / sizeof(pcm[0]), "PCM output is bounded");
        total_samples += samples;
        for(size_t i = 0U; i < samples; ++i) {
            if(pcm[i] != 0) nonzero = true;
        }
    }
    check(nonzero, "decoded PCM is nonzero");
    if(expect_complete)
        check(total_samples == 11025U * 2U, "decoded exact fixture duration");
    flac_decoder_close(&decoder);
    check(decoder.allocation_bytes == 0U, "decoder frees all tracked memory");
    return total_samples;
}

typedef struct {
    uint8_t prefix[46];
    size_t at;
    size_t size;
} oversized_metadata_reader_t;

static int oversized_metadata_read(void * context, void * output, size_t size)
{
    oversized_metadata_reader_t * reader = context;
    const size_t available = reader->size - reader->at;
    if(size > available) size = available;
    uint8_t * bytes = output;
    size_t copied = 0U;
    if(reader->at < sizeof(reader->prefix)) {
        copied = sizeof(reader->prefix) - reader->at;
        if(copied > size) copied = size;
        memcpy(bytes, reader->prefix + reader->at, copied);
    }
    if(copied < size) memset(bytes + copied, 0, size - copied);
    reader->at += size;
    return (int)size;
}

int main(void)
{
    check(FLAC_FIXTURE_len == 12206U, "fixture byte count");
    decode_fixture(FLAC_FIXTURE_len, 0U, true);
    decode_fixture(FLAC_FIXTURE_len, 257U, true);
    check(decode_fixture(FLAC_FIXTURE_len - 1000U, 113U, false) < 11025U * 2U,
          "truncated stream stops without over-read");

    static const uint8_t invalid[] = {0x00U, 0x01U, 0x02U, 0x03U};
    memory_reader_t invalid_reader = {
        invalid, sizeof(invalid), 0U, 0U
    };
    flac_decoder_t decoder;
    memset(&decoder, 0, sizeof(decoder));
    check(flac_decoder_open(&decoder, memory_read, &invalid_reader) ==
              FLAC_DECODER_OPEN_FAILED,
          "invalid signature is rejected");

    uint8_t * oversized_block = malloc(FLAC_FIXTURE_len);
    check(oversized_block != NULL, "oversized-block fixture allocation");
    memcpy(oversized_block, FLAC_FIXTURE, FLAC_FIXTURE_len);
    oversized_block[10] = 0xffU;
    oversized_block[11] = 0xffU;
    memory_reader_t oversized_block_reader = {
        oversized_block, FLAC_FIXTURE_len, 0U, 0U
    };
    memset(&decoder, 0, sizeof(decoder));
    check(flac_decoder_open(&decoder, memory_read, &oversized_block_reader) ==
              FLAC_DECODER_UNSUPPORTED,
          "oversized decoded block is rejected");
    free(oversized_block);

    oversized_metadata_reader_t metadata;
    memset(&metadata, 0, sizeof(metadata));
    memcpy(metadata.prefix, FLAC_FIXTURE, 42U);
    metadata.prefix[42] = 0x81U;
    metadata.prefix[43] = 0xffU;
    metadata.prefix[44] = 0xffU;
    metadata.prefix[45] = 0xffU;
    metadata.size = FLAC_DECODER_OPEN_READ_LIMIT + 4096U;
    memset(&decoder, 0, sizeof(decoder));
    const int metadata_result = flac_decoder_open(
        &decoder, oversized_metadata_read, &metadata);
    if(metadata_result != FLAC_DECODER_OPEN_READ_LIMIT_EXCEEDED)
        fprintf(stderr, "flac_decoder_check: metadata result=%d\n",
                metadata_result);
    check(metadata_result == FLAC_DECODER_OPEN_READ_LIMIT_EXCEEDED,
          "opening scan is bounded");

    puts("flac_decoder_check: PASS");
    return 0;
}
