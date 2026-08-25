#include "ogg_opus.h"

#include <string.h>

enum {
    OGG_OPUS_STAGE_HEADER,
    OGG_OPUS_STAGE_LACES,
    OGG_OPUS_STAGE_BODY
};

static uint16_t read_u16le(const uint8_t * p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32le(const uint8_t * p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static ogg_opus_result_t fail(ogg_opus_parser_t * parser,
                              ogg_opus_result_t error)
{
    parser->error = error;
    return error;
}

static void reset_logical_stream(ogg_opus_parser_t * parser)
{
    parser->packet_size = 0;
    parser->head_seen = 0;
    parser->tags_seen = 0;
    parser->channels = 0;
    parser->pre_skip = 0;
    parser->stream_ended = 0;
}

static int valid_opus_head(ogg_opus_parser_t * parser)
{
    const uint8_t * p = parser->packet;
    const size_t size = parser->packet_size;
    if(size < 19U || memcmp(p, "OpusHead", 8) != 0 ||
       p[8] == 0U || p[8] > 15U || p[9] == 0U)
        return 0;

    if(size != 19U || p[9] > 2U || p[18] != 0U) return 0;

    parser->channels = p[9];
    parser->pre_skip = read_u16le(p + 10);
    return 1;
}

static int valid_opus_tags(const uint8_t * p, size_t size)
{
    size_t offset = 8U;
    uint32_t count;
    uint32_t length;
    uint32_t i;

    if(size < 16U || memcmp(p, "OpusTags", 8) != 0) return 0;
    length = read_u32le(p + offset);
    offset += 4U;
    if(length > size - offset) return 0;
    offset += length;
    if(size - offset < 4U) return 0;
    count = read_u32le(p + offset);
    offset += 4U;
    for(i = 0; i < count; ++i) {
        if(size - offset < 4U) return 0;
        length = read_u32le(p + offset);
        offset += 4U;
        if(length > size - offset) return 0;
        offset += length;
    }
    return 1;
}

static ogg_opus_result_t complete_packet(ogg_opus_parser_t * parser)
{
    ogg_opus_packet_t packet;

    if(!parser->head_seen) {
        if(!valid_opus_head(parser)) return fail(parser, OGG_OPUS_ERR_HEAD);
        parser->head_seen = 1;
    } else if(!parser->tags_seen) {
        if(!valid_opus_tags(parser->packet, parser->packet_size))
            return fail(parser, OGG_OPUS_ERR_TAGS);
        parser->tags_seen = 1;
    } else {
        if(parser->packet_size == 0U ||
           parser->packet_size > OGG_OPUS_MAX_AUDIO_PACKET_SIZE)
            return fail(parser, OGG_OPUS_ERR_PACKET_TOO_LARGE);
        packet.data = parser->packet;
        packet.size = parser->packet_size;
        packet.stream_serial = parser->stream_serial;
        packet.channels = parser->channels;
        packet.pre_skip = parser->pre_skip;
        if(parser->on_packet != NULL &&
           parser->on_packet(&packet, parser->user_data) != 0)
            return fail(parser, OGG_OPUS_ERR_CALLBACK);
    }
    parser->packet_size = 0;
    return OGG_OPUS_OK;
}

static ogg_opus_result_t finish_page(ogg_opus_parser_t * parser)
{
    if((parser->page_flags & 0x04U) != 0U) {
        if(parser->packet_size != 0U)
            return fail(parser, OGG_OPUS_ERR_PAGE);
        parser->stream_ended = 1;
    }
    parser->header_used = 0;
    parser->laces_used = 0;
    parser->segment_used = 0;
    parser->page_segments = 0;
    parser->page_lace = 0;
    parser->stage = OGG_OPUS_STAGE_HEADER;
    return OGG_OPUS_OK;
}

static ogg_opus_result_t begin_page(ogg_opus_parser_t * parser)
{
    const uint32_t serial = read_u32le(parser->header + 14);
    const uint32_t sequence = read_u32le(parser->header + 18);
    const int continued = (parser->page_flags & 0x01U) != 0U;

    if(!parser->have_stream || serial != parser->stream_serial) {
        if(parser->have_stream && (!parser->stream_ended ||
                                   parser->packet_size != 0U))
            return fail(parser, OGG_OPUS_ERR_STREAM);
        if((parser->page_flags & 0x02U) == 0U || sequence != 0U)
            return fail(parser, OGG_OPUS_ERR_STREAM);
        reset_logical_stream(parser);
        parser->have_stream = 1;
        parser->stream_serial = serial;
    } else {
        if(parser->stream_ended || (parser->page_flags & 0x02U) != 0U)
            return fail(parser, OGG_OPUS_ERR_STREAM);
        if(sequence != parser->next_sequence)
            return fail(parser, OGG_OPUS_ERR_SEQUENCE);
    }

    if((parser->packet_size != 0U) != continued)
        return fail(parser, OGG_OPUS_ERR_PAGE);

    parser->next_sequence = sequence + 1U;
    parser->page_lace = 0;
    parser->segment_used = 0;
    parser->stage = OGG_OPUS_STAGE_BODY;
    if(parser->page_segments == 0U) return finish_page(parser);
    return OGG_OPUS_OK;
}

static ogg_opus_result_t consume_empty_segments(ogg_opus_parser_t * parser)
{
    ogg_opus_result_t result;
    while(parser->stage == OGG_OPUS_STAGE_BODY &&
          parser->page_lace < parser->page_segments &&
          parser->laces[parser->page_lace] == 0U) {
        result = complete_packet(parser);
        if(result != OGG_OPUS_OK) return result;
        ++parser->page_lace;
    }
    if(parser->stage == OGG_OPUS_STAGE_BODY &&
       parser->page_lace == parser->page_segments)
        return finish_page(parser);
    return OGG_OPUS_OK;
}

void ogg_opus_init(ogg_opus_parser_t * parser,
                   ogg_opus_packet_fn on_packet, void * user_data)
{
    if(parser == NULL) return;
    memset(parser, 0, sizeof(*parser));
    parser->on_packet = on_packet;
    parser->user_data = user_data;
}

void ogg_opus_reset(ogg_opus_parser_t * parser)
{
    ogg_opus_packet_fn on_packet;
    void * user_data;
    if(parser == NULL) return;
    on_packet = parser->on_packet;
    user_data = parser->user_data;
    ogg_opus_init(parser, on_packet, user_data);
}

ogg_opus_result_t ogg_opus_feed(ogg_opus_parser_t * parser,
                                const void * input, size_t size)
{
    const uint8_t * data = (const uint8_t *)input;
    size_t offset = 0;
    size_t take;
    size_t remaining;
    ogg_opus_result_t result;

    if(parser == NULL || (data == NULL && size != 0U))
        return OGG_OPUS_ERR_ARGUMENT;
    if(parser->error != OGG_OPUS_OK) return parser->error;

    while(offset < size) {
        if(parser->stage == OGG_OPUS_STAGE_HEADER) {
            take = sizeof(parser->header) - parser->header_used;
            if(take > size - offset) take = size - offset;
            memcpy(parser->header + parser->header_used, data + offset, take);
            parser->header_used += take;
            offset += take;
            if(parser->header_used != sizeof(parser->header)) continue;
            if(memcmp(parser->header, "OggS", 4) != 0)
                return fail(parser, OGG_OPUS_ERR_CAPTURE);
            if(parser->header[4] != 0U)
                return fail(parser, OGG_OPUS_ERR_VERSION);
            if((parser->header[5] & ~0x07U) != 0U)
                return fail(parser, OGG_OPUS_ERR_PAGE);
            parser->page_flags = parser->header[5];
            parser->page_segments = parser->header[26];
            parser->laces_used = 0;
            parser->stage = OGG_OPUS_STAGE_LACES;
            if(parser->page_segments == 0U) {
                result = begin_page(parser);
                if(result != OGG_OPUS_OK) return result;
            }
        } else if(parser->stage == OGG_OPUS_STAGE_LACES) {
            take = parser->page_segments - parser->laces_used;
            if(take > size - offset) take = size - offset;
            memcpy(parser->laces + parser->laces_used, data + offset, take);
            parser->laces_used += take;
            offset += take;
            if(parser->laces_used != parser->page_segments) continue;
            result = begin_page(parser);
            if(result != OGG_OPUS_OK) return result;
            result = consume_empty_segments(parser);
            if(result != OGG_OPUS_OK) return result;
        } else {
            result = consume_empty_segments(parser);
            if(result != OGG_OPUS_OK) return result;
            if(parser->stage != OGG_OPUS_STAGE_BODY) continue;

            remaining = parser->laces[parser->page_lace] - parser->segment_used;
            take = remaining;
            if(take > size - offset) take = size - offset;
            if(take > OGG_OPUS_MAX_PACKET_SIZE - parser->packet_size)
                return fail(parser, OGG_OPUS_ERR_PACKET_TOO_LARGE);
            memcpy(parser->packet + parser->packet_size, data + offset, take);
            parser->packet_size += take;
            parser->segment_used += take;
            offset += take;
            if(parser->segment_used != parser->laces[parser->page_lace])
                continue;

            if(parser->laces[parser->page_lace] < 255U) {
                result = complete_packet(parser);
                if(result != OGG_OPUS_OK) return result;
            }
            ++parser->page_lace;
            parser->segment_used = 0;
            if(parser->page_lace == parser->page_segments) {
                result = finish_page(parser);
                if(result != OGG_OPUS_OK) return result;
            }
        }
    }
    return OGG_OPUS_OK;
}

ogg_opus_result_t ogg_opus_finish(ogg_opus_parser_t * parser)
{
    if(parser == NULL) return OGG_OPUS_ERR_ARGUMENT;
    if(parser->error != OGG_OPUS_OK) return parser->error;
    if(parser->stage != OGG_OPUS_STAGE_HEADER || parser->header_used != 0U ||
       parser->packet_size != 0U)
        return fail(parser, OGG_OPUS_ERR_TRUNCATED);
    return OGG_OPUS_OK;
}

const char * ogg_opus_result_string(ogg_opus_result_t result)
{
    switch(result) {
        case OGG_OPUS_OK: return "ok";
        case OGG_OPUS_ERR_ARGUMENT: return "invalid argument";
        case OGG_OPUS_ERR_CAPTURE: return "invalid Ogg capture pattern";
        case OGG_OPUS_ERR_VERSION: return "unsupported Ogg version";
        case OGG_OPUS_ERR_PAGE: return "malformed Ogg page";
        case OGG_OPUS_ERR_SEQUENCE: return "Ogg page sequence mismatch";
        case OGG_OPUS_ERR_STREAM: return "unsupported Ogg stream transition";
        case OGG_OPUS_ERR_PACKET_TOO_LARGE: return "packet too large";
        case OGG_OPUS_ERR_HEAD: return "invalid OpusHead";
        case OGG_OPUS_ERR_TAGS: return "invalid OpusTags";
        case OGG_OPUS_ERR_CALLBACK: return "packet callback failed";
        case OGG_OPUS_ERR_TRUNCATED: return "truncated Ogg stream";
        default: return "unknown Ogg Opus error";
    }
}
