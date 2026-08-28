// PSRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vorbis_decoder.hpp"
#include "ogg_stream.hpp"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vorbis_fixture.inc"

static void check(int condition, const char * message);

typedef struct {
    const uint8_t * data;
    size_t size;
    size_t at;
} memory_source_t;

static int memory_read(void * context, void * output, size_t size)
{
    auto *source = static_cast<memory_source_t *>(context);
    if(size > 7U) size = 7U;
    if(size > source->size - source->at) size = source->size - source->at;
    memcpy(output, source->data + source->at, size);
    source->at += size;
    return (int)size;
}

static void write_u32le(uint8_t * output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static uint32_t ogg_crc(const uint8_t * data, size_t size)
{
    uint32_t crc = 0U;
    for(size_t i = 0U; i < size; ++i) {
        const uint8_t byte = i >= 22U && i < 26U ? 0U : data[i];
        crc ^= (uint32_t)byte << 24;
        for(unsigned bit = 0U; bit < 8U; ++bit)
            crc = (crc << 1) ^ ((crc & UINT32_C(0x80000000)) != 0U
                ? UINT32_C(0x04c11db7) : 0U);
    }
    return crc;
}

static void rewrite_ogg_serial(uint8_t * data, size_t size, uint32_t serial)
{
    size_t at = 0U;
    while(at < size) {
        check(size - at >= 27U && memcmp(data + at, "OggS", 4U) == 0,
              "fixture page header");
        const size_t laces = data[at + 26U];
        check(size - at >= 27U + laces, "fixture lacing table");
        size_t body = 0U;
        for(size_t i = 0U; i < laces; ++i) body += data[at + 27U + i];
        const size_t page_size = 27U + laces + body;
        check(page_size <= size - at, "fixture page body");
        write_u32le(data + at + 14U, serial);
        write_u32le(data + at + 22U, 0U);
        write_u32le(data + at + 22U, ogg_crc(data + at, page_size));
        at += page_size;
    }
}

static void check_chained_streams(void)
{
    static ogg_stream_t stream;
    auto *input = static_cast<uint8_t *>(malloc(VORBIS_FIXTURE_SIZE * 2U));
    auto *output = static_cast<uint8_t *>(malloc(VORBIS_FIXTURE_SIZE));
    check(input != nullptr && output != nullptr, "chain fixture allocation");
    memcpy(input, VORBIS_FIXTURE, VORBIS_FIXTURE_SIZE);
    memcpy(input + VORBIS_FIXTURE_SIZE, VORBIS_FIXTURE,
           VORBIS_FIXTURE_SIZE);
    rewrite_ogg_serial(input + VORBIS_FIXTURE_SIZE, VORBIS_FIXTURE_SIZE,
                       UINT32_C(0x66554433));

    memory_source_t source = {input, VORBIS_FIXTURE_SIZE * 2U, 0U};
    ogg_stream_init(&stream, memory_read, &source);
    for(unsigned chain = 0U; chain < 2U; ++chain) {
        size_t size = 0U;
        for(;;) {
            const int read = ogg_stream_read(
                &stream, output + size, VORBIS_FIXTURE_SIZE - size);
            check(read >= 0, "validated chain read");
            if(read == 0) break;
            size += (size_t)read;
        }
        check(size == VORBIS_FIXTURE_SIZE, "one complete logical stream");
        check(ogg_stream_status(&stream) == OGG_STREAM_CHAIN_END,
              "logical stream EOS boundary");

        vorbis_decoder_t decoder;
        memset(&decoder, 0, sizeof(decoder));
        size_t consumed = 0U;
        check(vorbis_decoder_open(&decoder, output, size, &consumed) ==
              VORBIS_DECODER_OK, "chained stream reopens decoder");
        int16_t pcm[VORBIS_DECODER_MAX_FRAME_FRAMES * 2U];
        size_t at = consumed;
        size_t total_samples = 0U;
        bool nonzero = false;
        while(at < size) {
            size_t used = 0U;
            size_t samples = 0U;
            const int result = vorbis_decoder_decode(
                &decoder, output + at, size - at, &used,
                pcm, sizeof(pcm) / sizeof(pcm[0]), &samples);
            check(result == VORBIS_DECODER_OK ||
                  result == VORBIS_DECODER_NEED_MORE,
                  "chained stream frame decode");
            for(size_t i = 0U; i < samples; ++i)
                nonzero = nonzero || pcm[i] != 0;
            total_samples += samples;
            at += used;
            if(used == 0U) break;
        }
        check(total_samples >= 4800U,
              "chained stream decoded expected PCM duration");
        check(nonzero, "chained stream decoded nonzero PCM");
        vorbis_decoder_close(&decoder);

        check(ogg_stream_next_chain(&stream) == OGG_STREAM_OK,
              "advance to next logical stream");
    }
    free(output);
    free(input);
}

static void check(int condition, const char * message)
{
    if(condition) return;
    fprintf(stderr, "vorbis_decoder_check: FAIL: %s\n", message);
    exit(1);
}

int main(void)
{
    check_chained_streams();
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
    check(decoder.max_frame_frames == VORBIS_DECODER_MAX_FRAME_FRAMES,
          "decoder uses allocated frame capacity");
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
    check(decoder.handle == nullptr, "decoder closes cleanly");

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
