// ProsperoRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "radio_ts_aac.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int condition, const char * message)
{
    if(condition) return;
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

static uint32_t crc32(const uint8_t * data, size_t size)
{
    uint32_t crc = UINT32_C(0xffffffff);
    for(size_t i = 0; i < size; ++i) {
        crc ^= (uint32_t)data[i] << 24U;
        for(unsigned bit = 0; bit < 8U; ++bit) {
            crc = (crc & UINT32_C(0x80000000)) != 0U
                ? (crc << 1U) ^ UINT32_C(0x04c11db7) : crc << 1U;
        }
    }
    return crc;
}

static void finish_crc(uint8_t * section, size_t without_crc)
{
    const uint32_t crc = crc32(section, without_crc);
    section[without_crc] = (uint8_t)(crc >> 24U);
    section[without_crc + 1U] = (uint8_t)(crc >> 16U);
    section[without_crc + 2U] = (uint8_t)(crc >> 8U);
    section[without_crc + 3U] = (uint8_t)crc;
}

static void packet(uint8_t * output, uint16_t pid, int start,
                   const uint8_t * payload, size_t size, uint8_t continuity)
{
    check(size <= 184U, "test payload fits one TS packet");
    memset(output, 0xff, RADIO_TS_PACKET_BYTES);
    output[0] = 0x47U;
    output[1] = (uint8_t)((start ? 0x40U : 0U) | (pid >> 8U));
    output[2] = (uint8_t)pid;
    output[3] = (uint8_t)(0x10U | (continuity & 0x0fU));
    memcpy(output + 4U, payload, size);
}

typedef struct {
    uint8_t data[64];
    size_t size;
    size_t total;
    FILE * capture;
} output_t;

static int output_ready(const uint8_t * data, size_t size, void * user_data)
{
    auto *output = static_cast<output_t *>(user_data);
    const size_t available = sizeof(output->data) - output->size;
    const size_t copy = size < available ? size : available;
    memcpy(output->data + output->size, data, copy);
    output->size += copy;
    output->total += size;
    if(output->capture != nullptr)
        check(fwrite(data, 1U, size, output->capture) == size,
              "probe AAC capture writes");
    return 0;
}

static size_t make_pat(uint8_t * output)
{
    static const uint8_t body[] = {
        0x00U, 0xb0U, 0x0dU, 0x00U, 0x01U, 0xc1U, 0x00U, 0x00U,
        0x00U, 0x01U, 0xe1U, 0x00U
    };
    memcpy(output, body, sizeof(body));
    finish_crc(output, sizeof(body));
    return sizeof(body) + 4U;
}

static size_t make_pmt(uint8_t * output, uint8_t stream_type)
{
    const uint8_t body[] = {
        0x02U, 0xb0U, 0x12U, 0x00U, 0x01U, 0xc1U, 0x00U, 0x00U,
        0xe1U, 0x01U, 0xf0U, 0x00U,
        stream_type, 0xe1U, 0x01U, 0xf0U, 0x00U
    };
    memcpy(output, body, sizeof(body));
    finish_crc(output, sizeof(body));
    return sizeof(body) + 4U;
}

static int probe_file(const char * path, const char * capture_path)
{
    FILE * file = fopen(path, "rb");
    check(file != nullptr, "probe TS file opens");
    output_t output = {{0}, 0U, 0U, nullptr};
    if(capture_path != nullptr) {
        output.capture = fopen(capture_path, "wb");
        check(output.capture != nullptr, "probe AAC capture opens");
    }
    radio_ts_aac_parser_t parser;
    radio_ts_aac_init(&parser, output_ready, &output);
    uint8_t data[4096];
    while(!feof(file)) {
        const size_t size = fread(data, 1U, sizeof(data), file);
        check(radio_ts_aac_feed(&parser, data, size) == RADIO_TS_AAC_OK,
              "probe TS file parses");
        if(ferror(file)) check(0, "probe TS file reads");
    }
    fclose(file);
    if(output.capture != nullptr) check(fclose(output.capture) == 0,
                                     "probe AAC capture closes");
    check(output.total > 1024U, "probe emitted an AAC elementary stream");
    printf("radio_ts_aac_check: PROBE PASS (%zu AAC bytes)\n", output.total);
    return 0;
}

int main(int argc, char ** argv)
{
    if(argc == 2 || argc == 3)
        return probe_file(argv[1], argc == 3 ? argv[2] : nullptr);
    check(argc == 1,
          "usage: radio_ts_aac_check [segment.ts [output.aac]]");
    uint8_t bytes[RADIO_TS_PACKET_BYTES * 3U];
    uint8_t payload[184];
    payload[0] = 0U;
    size_t section = make_pat(payload + 1U);
    packet(bytes, 0U, 1, payload, section + 1U, 0U);

    payload[0] = 0U;
    section = make_pmt(payload + 1U, 0x0fU);
    packet(bytes + RADIO_TS_PACKET_BYTES, 0x100U, 1,
           payload, section + 1U, 0U);

    static const uint8_t frame[] = {
        0xffU, 0xf1U, 0x50U, 0x80U, 0x01U, 0x7fU, 0xfcU,
        0x11U, 0x22U, 0x33U, 0x44U
    };
    const uint8_t pes_header[] = {
        0x00U, 0x00U, 0x01U, 0xc0U, 0x00U, 0x0eU,
        0x80U, 0x00U, 0x00U
    };
    memcpy(payload, pes_header, sizeof(pes_header));
    memcpy(payload + sizeof(pes_header), frame, sizeof(frame));
    packet(bytes + RADIO_TS_PACKET_BYTES * 2U, 0x101U, 1, payload,
           sizeof(pes_header) + sizeof(frame), 0U);

    output_t output = {{0}, 0U, 0U, nullptr};
    radio_ts_aac_parser_t parser;
    radio_ts_aac_init(&parser, output_ready, &output);
    const size_t chunks[] = {1U, 13U, 211U, sizeof(bytes) - 225U};
    size_t at = 0U;
    for(size_t i = 0; i < sizeof(chunks) / sizeof(chunks[0]); ++i) {
        check(radio_ts_aac_feed(&parser, bytes + at, chunks[i]) ==
              RADIO_TS_AAC_OK, "split TS input parses");
        at += chunks[i];
    }
    check(output.size >= sizeof(frame) &&
          memcmp(output.data, frame, sizeof(frame)) == 0,
          "AAC ADTS elementary stream emitted");

    uint8_t continuity_bytes[RADIO_TS_PACKET_BYTES * 3U];
    uint8_t long_frame[200];
    memset(long_frame, 0x5a, sizeof(long_frame));
    long_frame[0] = 0xffU;
    long_frame[1] = 0xf1U;
    long_frame[2] = 0x50U;
    long_frame[3] = 0x80U;
    long_frame[4] = 0x19U;
    long_frame[5] = 0x1fU;
    long_frame[6] = 0xfcU;
    static const uint8_t long_pes_header[] = {
        0x00U, 0x00U, 0x01U, 0xc0U, 0x00U, 0xcbU,
        0x80U, 0x00U, 0x00U
    };
    memcpy(payload, long_pes_header, sizeof(long_pes_header));
    memcpy(payload + sizeof(long_pes_header), long_frame, 175U);
    packet(continuity_bytes, 0x101U, 1, payload, sizeof(payload), 1U);
    payload[0] = 0U;
    section = make_pmt(payload + 1U, 0x0fU);
    packet(continuity_bytes + RADIO_TS_PACKET_BYTES, 0x100U, 1,
           payload, section + 1U, 1U);
    packet(continuity_bytes + RADIO_TS_PACKET_BYTES * 2U, 0x101U, 0,
           long_frame + 175U, sizeof(long_frame) - 175U, 2U);
    output.size = 0U;
    output.total = 0U;
    check(radio_ts_aac_feed(&parser, continuity_bytes,
                            sizeof(continuity_bytes)) == RADIO_TS_AAC_OK,
          "repeated PMT inside AAC PES parses");
    check(output.total == sizeof(long_frame) &&
          memcmp(output.data, long_frame, sizeof(output.data)) == 0,
          "repeated PMT preserves the in-progress AAC PES");

    radio_ts_aac_reset(&parser);
    output.size = 0U;
    payload[0] = 0U;
    section = make_pat(payload + 1U);
    packet(bytes, 0U, 1, payload, section + 1U, 0U);
    payload[0] = 0U;
    section = make_pmt(payload + 1U, 0x11U);
    packet(bytes + RADIO_TS_PACKET_BYTES, 0x100U, 1,
           payload, section + 1U, 0U);
    check(radio_ts_aac_feed(&parser, bytes, RADIO_TS_PACKET_BYTES * 2U) ==
          RADIO_TS_AAC_UNSUPPORTED, "non-ADTS AAC transport rejected");

    puts("radio_ts_aac_check: PASS");
    return 0;
}
