// ProsperoRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ogg_opus.hpp"

#include <string.h>

static uint16_t read_u16le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int16_t read_i16le(const uint8_t *p)
{
    const uint16_t value = read_u16le(p);
    return value < UINT16_C(0x8000) ? (int16_t)value : (int16_t)((int32_t)value - INT32_C(0x10000));
}

static uint32_t read_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static ogg_opus_result_t fail(ogg_opus_parser_t *parser, ogg_opus_result_t error)
{
    parser->error = error;
    return error;
}

static void reset_logical_stream(ogg_opus_parser_t *parser)
{
    parser->packet_size = 0;
    parser->head_seen = 0;
    parser->tags_seen = 0;
    parser->audio_seen = 0;
    parser->sequence_jump_seen = 0;
    parser->discard_continued = 0;
    parser->channels = 0;
    parser->pre_skip = 0;
    parser->output_gain_q8 = 0;
    parser->stream_ended = 0;
}

static int valid_opus_head(ogg_opus_parser_t *parser)
{
    const uint8_t *p = parser->packet;
    const size_t size = parser->packet_size;
    if (size < 19U || memcmp(p, "OpusHead", 8) != 0 || p[8] == 0U || p[8] > 15U || p[9] == 0U)
        return 0;

    if (size != 19U || p[9] > 2U || p[18] != 0U)
        return 0;

    parser->channels = p[9];
    parser->pre_skip = read_u16le(p + 10);
    parser->output_gain_q8 = read_i16le(p + 16);
    return 1;
}

static int valid_opus_tags(const uint8_t *p, size_t size)
{
    size_t offset = 8U;
    uint32_t count;
    uint32_t length;
    uint32_t i;

    if (size < 16U || memcmp(p, "OpusTags", 8) != 0)
        return 0;
    length = read_u32le(p + offset);
    offset += 4U;
    if (length > size - offset)
        return 0;
    offset += length;
    if (size - offset < 4U)
        return 0;
    count = read_u32le(p + offset);
    offset += 4U;
    for (i = 0; i < count; ++i)
    {
        if (size - offset < 4U)
            return 0;
        length = read_u32le(p + offset);
        offset += 4U;
        if (length > size - offset)
            return 0;
        offset += length;
    }
    return 1;
}

static ogg_opus_result_t complete_packet(ogg_opus_parser_t *parser, const ogg_page_t *page,
                                         int end_of_page)
{
    ogg_opus_packet_t packet;

    if (!parser->head_seen)
    {
        if (!valid_opus_head(parser))
            return fail(parser, OGG_OPUS_ERR_HEAD);
        parser->head_seen = 1;
    }
    else if (!parser->tags_seen)
    {
        if (!valid_opus_tags(parser->packet, parser->packet_size))
            return fail(parser, OGG_OPUS_ERR_TAGS);
        parser->tags_seen = 1;
    }
    else
    {
        if (parser->packet_size == 0U || parser->packet_size > OGG_OPUS_MAX_AUDIO_PACKET_SIZE)
            return fail(parser, OGG_OPUS_ERR_PACKET_TOO_LARGE);
        packet.data = parser->packet;
        packet.size = parser->packet_size;
        packet.stream_serial = parser->stream_serial;
        packet.page_sequence = page->sequence;
        packet.granule_position = page->granule_position;
        packet.channels = parser->channels;
        packet.pre_skip = parser->pre_skip;
        packet.output_gain_q8 = parser->output_gain_q8;
        packet.end_of_stream = (uint8_t)((page->flags & 0x04U) != 0U);
        packet.end_of_page = (uint8_t)(end_of_page != 0);
        parser->audio_seen = 1;
        if (parser->on_packet != nullptr && parser->on_packet(&packet, parser->user_data) != 0)
            return fail(parser, OGG_OPUS_ERR_CALLBACK);
    }
    parser->packet_size = 0;
    return OGG_OPUS_OK;
}

static ogg_opus_result_t begin_page(ogg_opus_parser_t *parser, const ogg_page_t *page)
{
    const int continued = (page->flags & 0x01U) != 0U;

    if (!parser->have_stream || page->stream_serial != parser->stream_serial)
    {
        if (parser->have_stream && (!parser->stream_ended || parser->packet_size != 0U))
            return fail(parser, OGG_OPUS_ERR_STREAM);
        if ((page->flags & 0x02U) == 0U || page->sequence != 0U)
            return fail(parser, OGG_OPUS_ERR_STREAM);
        reset_logical_stream(parser);
        parser->have_stream = 1;
        parser->stream_serial = page->stream_serial;
    }
    else
    {
        if (parser->stream_ended || (page->flags & 0x02U) != 0U)
            return fail(parser, OGG_OPUS_ERR_STREAM);
        /* Icecast can prepend fresh headers before joining the live page
           sequence. Let the first complete audio page establish that value. */
        if (page->sequence != parser->next_sequence)
        {
            if (!parser->tags_seen || parser->audio_seen || parser->sequence_jump_seen)
                return fail(parser, OGG_OPUS_ERR_SEQUENCE);
            parser->sequence_jump_seen = 1;
            parser->discard_continued = (uint8_t)continued;
        }
    }

    if (!parser->discard_continued && (parser->packet_size != 0U) != continued)
        return fail(parser, OGG_OPUS_ERR_PAGE);
    parser->next_sequence = page->sequence + 1U;
    return OGG_OPUS_OK;
}

static int page_ready(const ogg_page_t *page, void *user_data)
{
    auto *parser = static_cast<ogg_opus_parser_t *>(user_data);
    if (begin_page(parser, page) != OGG_OPUS_OK)
        return -1;

    size_t body_at = 0;
    for (size_t i = 0; i < page->lace_count; ++i)
    {
        const size_t lace = page->laces[i];
        if (parser->discard_continued)
        {
            body_at += lace;
            if (lace < 255U)
                parser->discard_continued = 0U;
            continue;
        }
        if (lace > OGG_OPUS_MAX_PACKET_SIZE - parser->packet_size)
        {
            fail(parser, OGG_OPUS_ERR_PACKET_TOO_LARGE);
            return -1;
        }
        memcpy(parser->packet + parser->packet_size, page->body + body_at, lace);
        parser->packet_size += lace;
        body_at += lace;
        if (lace < 255U && complete_packet(parser, page, i + 1U == page->lace_count) != OGG_OPUS_OK)
            return -1;
    }

    if ((page->flags & 0x04U) != 0U)
    {
        if (parser->packet_size != 0U)
        {
            fail(parser, OGG_OPUS_ERR_PAGE);
            return -1;
        }
        parser->stream_ended = 1;
    }
    return 0;
}

static ogg_opus_result_t page_error(ogg_opus_parser_t *parser, ogg_page_result_t result)
{
    switch (result)
    {
    case OGG_PAGE_OK:
        return OGG_OPUS_OK;
    case OGG_PAGE_ERR_ARGUMENT:
        return OGG_OPUS_ERR_ARGUMENT;
    case OGG_PAGE_ERR_CAPTURE:
        return OGG_OPUS_ERR_CAPTURE;
    case OGG_PAGE_ERR_VERSION:
        return OGG_OPUS_ERR_VERSION;
    case OGG_PAGE_ERR_FLAGS:
        return OGG_OPUS_ERR_PAGE;
    case OGG_PAGE_ERR_CHECKSUM:
        return OGG_OPUS_ERR_CHECKSUM;
    case OGG_PAGE_ERR_CALLBACK:
        return parser->error != OGG_OPUS_OK ? parser->error : OGG_OPUS_ERR_CALLBACK;
    case OGG_PAGE_ERR_TRUNCATED:
        return OGG_OPUS_ERR_TRUNCATED;
    default:
        return OGG_OPUS_ERR_PAGE;
    }
}

void ogg_opus_init(ogg_opus_parser_t *parser, ogg_opus_packet_fn on_packet, void *user_data)
{
    if (parser == nullptr)
        return;
    memset(parser, 0, sizeof(*parser));
    parser->on_packet = on_packet;
    parser->user_data = user_data;
    ogg_page_init(&parser->pages, page_ready, parser);
}

void ogg_opus_reset(ogg_opus_parser_t *parser)
{
    ogg_opus_packet_fn on_packet;
    void *user_data;
    if (parser == nullptr)
        return;
    on_packet = parser->on_packet;
    user_data = parser->user_data;
    ogg_opus_init(parser, on_packet, user_data);
}

ogg_opus_result_t ogg_opus_feed(ogg_opus_parser_t *parser, const void *data, size_t size)
{
    if (parser == nullptr || (data == nullptr && size != 0U))
        return OGG_OPUS_ERR_ARGUMENT;
    if (parser->error != OGG_OPUS_OK)
        return parser->error;

    const ogg_page_result_t result = ogg_page_feed(&parser->pages, data, size);
    if (result != OGG_PAGE_OK)
        return fail(parser, page_error(parser, result));
    return OGG_OPUS_OK;
}

ogg_opus_result_t ogg_opus_finish(ogg_opus_parser_t *parser)
{
    if (parser == nullptr)
        return OGG_OPUS_ERR_ARGUMENT;
    if (parser->error != OGG_OPUS_OK)
        return parser->error;
    const ogg_page_result_t result = ogg_page_finish(&parser->pages);
    if (result != OGG_PAGE_OK)
        return fail(parser, page_error(parser, result));
    if (parser->packet_size != 0U)
        return fail(parser, OGG_OPUS_ERR_TRUNCATED);
    return OGG_OPUS_OK;
}

const char *ogg_opus_result_string(ogg_opus_result_t result)
{
    switch (result)
    {
    case OGG_OPUS_OK:
        return "ok";
    case OGG_OPUS_ERR_ARGUMENT:
        return "invalid argument";
    case OGG_OPUS_ERR_CAPTURE:
        return "invalid Ogg capture pattern";
    case OGG_OPUS_ERR_VERSION:
        return "unsupported Ogg version";
    case OGG_OPUS_ERR_PAGE:
        return "malformed Ogg page";
    case OGG_OPUS_ERR_SEQUENCE:
        return "Ogg page sequence mismatch";
    case OGG_OPUS_ERR_STREAM:
        return "unsupported Ogg stream transition";
    case OGG_OPUS_ERR_PACKET_TOO_LARGE:
        return "packet too large";
    case OGG_OPUS_ERR_HEAD:
        return "invalid OpusHead";
    case OGG_OPUS_ERR_TAGS:
        return "invalid OpusTags";
    case OGG_OPUS_ERR_CALLBACK:
        return "packet callback failed";
    case OGG_OPUS_ERR_TRUNCATED:
        return "truncated Ogg stream";
    case OGG_OPUS_ERR_CHECKSUM:
        return "Ogg page checksum mismatch";
    default:
        return "unknown Ogg Opus error";
    }
}
