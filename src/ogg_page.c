#include "ogg_page.h"

#include <string.h>

#define OGG_CRC_POLYNOMIAL UINT32_C(0x04c11db7)

enum {
    OGG_PAGE_STAGE_HEADER,
    OGG_PAGE_STAGE_LACES,
    OGG_PAGE_STAGE_BODY
};

static uint32_t read_u32le(const uint8_t * p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_u64le(const uint8_t * p)
{
    return (uint64_t)read_u32le(p) | ((uint64_t)read_u32le(p + 4) << 32);
}

static ogg_page_result_t fail(ogg_page_parser_t * parser,
                              ogg_page_result_t error)
{
    parser->error = error;
    return error;
}

static uint32_t page_crc(const uint8_t * data, size_t size)
{
    uint32_t crc = 0;
    for(size_t i = 0; i < size; ++i) {
        const uint8_t byte = i >= 22U && i < 26U ? 0U : data[i];
        crc ^= (uint32_t)byte << 24;
        for(unsigned bit = 0; bit < 8U; ++bit) {
            crc = (crc << 1) ^
                ((crc & UINT32_C(0x80000000)) != 0U ? OGG_CRC_POLYNOMIAL : 0U);
        }
    }
    return crc;
}

static ogg_page_result_t advance(ogg_page_parser_t * parser)
{
    if(parser->stage == OGG_PAGE_STAGE_HEADER) {
        if(memcmp(parser->data, "OggS", 4U) != 0)
            return fail(parser, OGG_PAGE_ERR_CAPTURE);
        if(parser->data[4] != 0U)
            return fail(parser, OGG_PAGE_ERR_VERSION);
        if((parser->data[5] & ~0x07U) != 0U)
            return fail(parser, OGG_PAGE_ERR_FLAGS);
        parser->stage = OGG_PAGE_STAGE_LACES;
        parser->target = OGG_PAGE_FIXED_HEADER_SIZE + parser->data[26];
        return OGG_PAGE_OK;
    }

    if(parser->stage == OGG_PAGE_STAGE_LACES) {
        size_t body_size = 0;
        const size_t lace_count = parser->data[26];
        for(size_t i = 0; i < lace_count; ++i)
            body_size += parser->data[OGG_PAGE_FIXED_HEADER_SIZE + i];
        parser->stage = OGG_PAGE_STAGE_BODY;
        parser->target = OGG_PAGE_FIXED_HEADER_SIZE + lace_count + body_size;
        return OGG_PAGE_OK;
    }

    if(page_crc(parser->data, parser->target) !=
       read_u32le(parser->data + 22))
        return fail(parser, OGG_PAGE_ERR_CHECKSUM);

    const size_t lace_count = parser->data[26];
    ogg_page_t page;
    page.data = parser->data;
    page.size = parser->target;
    page.laces = parser->data + OGG_PAGE_FIXED_HEADER_SIZE;
    page.lace_count = lace_count;
    page.body = page.laces + lace_count;
    page.body_size = parser->target - OGG_PAGE_FIXED_HEADER_SIZE - lace_count;
    page.granule_position = read_u64le(parser->data + 6);
    page.stream_serial = read_u32le(parser->data + 14);
    page.sequence = read_u32le(parser->data + 18);
    page.flags = parser->data[5];
    if(parser->on_page != NULL &&
       parser->on_page(&page, parser->user_data) != 0)
        return fail(parser, OGG_PAGE_ERR_CALLBACK);

    parser->used = 0;
    parser->target = OGG_PAGE_FIXED_HEADER_SIZE;
    parser->stage = OGG_PAGE_STAGE_HEADER;
    return OGG_PAGE_OK;
}

void ogg_page_init(ogg_page_parser_t * parser,
                   ogg_page_fn on_page, void * user_data)
{
    if(parser == NULL) return;
    memset(parser, 0, sizeof(*parser));
    parser->target = OGG_PAGE_FIXED_HEADER_SIZE;
    parser->on_page = on_page;
    parser->user_data = user_data;
}

void ogg_page_reset(ogg_page_parser_t * parser)
{
    ogg_page_fn on_page;
    void * user_data;
    if(parser == NULL) return;
    on_page = parser->on_page;
    user_data = parser->user_data;
    ogg_page_init(parser, on_page, user_data);
}

ogg_page_result_t ogg_page_feed(ogg_page_parser_t * parser,
                                const void * input, size_t size)
{
    const uint8_t * data = input;
    size_t offset = 0;

    if(parser == NULL || (data == NULL && size != 0U))
        return OGG_PAGE_ERR_ARGUMENT;
    if(parser->error != OGG_PAGE_OK) return parser->error;

    while(offset < size) {
        size_t take = parser->target - parser->used;
        if(take > size - offset) take = size - offset;
        memcpy(parser->data + parser->used, data + offset, take);
        parser->used += take;
        offset += take;
        while(parser->used == parser->target) {
            const ogg_page_result_t result = advance(parser);
            if(result != OGG_PAGE_OK) return result;
        }
    }
    return OGG_PAGE_OK;
}

ogg_page_result_t ogg_page_finish(ogg_page_parser_t * parser)
{
    if(parser == NULL) return OGG_PAGE_ERR_ARGUMENT;
    if(parser->error != OGG_PAGE_OK) return parser->error;
    if(parser->used != 0U)
        return fail(parser, OGG_PAGE_ERR_TRUNCATED);
    return OGG_PAGE_OK;
}
