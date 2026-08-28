// PS5 Radio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ogg_page.hpp"

#ifndef OGG_OPUS_MAX_PACKET_SIZE
#define OGG_OPUS_MAX_PACKET_SIZE 65536U
#endif

#define OGG_OPUS_MAX_AUDIO_PACKET_SIZE 61440U

enum ogg_opus_result_t {
    OGG_OPUS_OK = 0,
    OGG_OPUS_ERR_ARGUMENT = -1,
    OGG_OPUS_ERR_CAPTURE = -2,
    OGG_OPUS_ERR_VERSION = -3,
    OGG_OPUS_ERR_PAGE = -4,
    OGG_OPUS_ERR_SEQUENCE = -5,
    OGG_OPUS_ERR_STREAM = -6,
    OGG_OPUS_ERR_PACKET_TOO_LARGE = -7,
    OGG_OPUS_ERR_HEAD = -8,
    OGG_OPUS_ERR_TAGS = -9,
    OGG_OPUS_ERR_CALLBACK = -10,
    OGG_OPUS_ERR_TRUNCATED = -11,
    OGG_OPUS_ERR_CHECKSUM = -12
};

struct ogg_opus_packet_t {
    const uint8_t * data;
    size_t size;
    uint32_t stream_serial;
    uint32_t page_sequence;
    uint64_t granule_position;
    uint8_t channels;
    uint16_t pre_skip;
    int16_t output_gain_q8;
    uint8_t end_of_stream;
    uint8_t end_of_page;
};

/* Packet data remains valid only for the duration of the callback. */
using ogg_opus_packet_fn = int (*)(const ogg_opus_packet_t * packet,
                                   void * user_data);

/* Caller-owned state; no allocation or external dependency is required. */
struct ogg_opus_parser_t {
    ogg_page_parser_t pages;
    uint8_t packet[OGG_OPUS_MAX_PACKET_SIZE];
    size_t packet_size;
    uint32_t stream_serial;
    uint32_t next_sequence;
    uint16_t pre_skip;
    int16_t output_gain_q8;
    uint8_t have_stream;
    uint8_t stream_ended;
    uint8_t head_seen;
    uint8_t tags_seen;
    uint8_t audio_seen;
    uint8_t sequence_jump_seen;
    uint8_t discard_continued;
    uint8_t channels;
    ogg_opus_result_t error;
    ogg_opus_packet_fn on_packet;
    void * user_data;
};

void ogg_opus_init(ogg_opus_parser_t * parser,
                   ogg_opus_packet_fn on_packet, void * user_data);
void ogg_opus_reset(ogg_opus_parser_t * parser);
ogg_opus_result_t ogg_opus_feed(ogg_opus_parser_t * parser,
                                const void * data, size_t size);
ogg_opus_result_t ogg_opus_finish(ogg_opus_parser_t * parser);
const char * ogg_opus_result_string(ogg_opus_result_t result);
