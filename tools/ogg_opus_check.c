#include "ogg_opus.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned packets;
    uint32_t serial[4];
    uint8_t channels[4];
    uint16_t pre_skip[4];
    size_t sizes[4];
    uint8_t first[4];
    uint8_t last[4];
} capture_t;

static void write_u32le(uint8_t * p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static size_t make_page(uint8_t * out, uint8_t flags, uint32_t serial,
                        uint32_t sequence, const uint8_t * laces,
                        size_t lace_count, const uint8_t * body,
                        size_t body_size)
{
    size_t i;
    size_t total = 0;
    assert(lace_count <= 255U);
    for(i = 0; i < lace_count; ++i) total += laces[i];
    assert(total == body_size);
    memset(out, 0, 27U);
    memcpy(out, "OggS", 4U);
    out[5] = flags;
    write_u32le(out + 14, serial);
    write_u32le(out + 18, sequence);
    out[26] = (uint8_t)lace_count;
    memcpy(out + 27U, laces, lace_count);
    memcpy(out + 27U + lace_count, body, body_size);
    return 27U + lace_count + body_size;
}

static size_t make_headers(uint8_t * out, uint8_t channels, uint16_t pre_skip)
{
    static const uint8_t tags[] = {
        'O','p','u','s','T','a','g','s', 4,0,0,0,
        't','e','s','t', 0,0,0,0
    };
    memset(out, 0, 19U);
    memcpy(out, "OpusHead", 8U);
    out[8] = 1U;
    out[9] = channels;
    out[10] = (uint8_t)pre_skip;
    out[11] = (uint8_t)(pre_skip >> 8);
    out[12] = 0x80U;
    out[13] = 0xbbU;
    out[18] = 0U;
    memcpy(out + 19U, tags, sizeof(tags));
    return 19U + sizeof(tags);
}

static int capture_packet(const ogg_opus_packet_t * packet, void * user_data)
{
    capture_t * capture = (capture_t *)user_data;
    const unsigned n = capture->packets++;
    assert(n < 4U);
    capture->serial[n] = packet->stream_serial;
    capture->channels[n] = packet->channels;
    capture->pre_skip[n] = packet->pre_skip;
    capture->sizes[n] = packet->size;
    capture->first[n] = packet->data[0];
    capture->last[n] = packet->data[packet->size - 1U];
    return 0;
}

static void feed_split(ogg_opus_parser_t * parser, const uint8_t * data,
                       size_t size)
{
    static const size_t chunks[] = {1U, 2U, 7U, 3U, 29U, 5U, 251U};
    size_t offset = 0;
    unsigned n = 0;
    while(offset < size) {
        size_t take = chunks[n++ % (sizeof(chunks) / sizeof(chunks[0]))];
        if(take > size - offset) take = size - offset;
        assert(ogg_opus_feed(parser, data + offset, take) == OGG_OPUS_OK);
        offset += take;
    }
}

static void check_split_continuation_and_chain(void)
{
    ogg_opus_parser_t parser;
    capture_t capture;
    uint8_t page[2048];
    uint8_t body[1400];
    uint8_t laces[8];
    size_t headers;
    size_t page_size;
    size_t i;

    memset(&capture, 0, sizeof(capture));
    ogg_opus_init(&parser, capture_packet, &capture);

    headers = make_headers(body, 2U, 312U);
    for(i = 0; i < 300U; ++i) body[headers + i] = (uint8_t)i;
    laces[0] = 19U;
    laces[1] = (uint8_t)(headers - 19U);
    laces[2] = 255U;
    page_size = make_page(page, 0x02U, 0x11223344U, 0U, laces, 3U,
                          body, headers + 255U);
    feed_split(&parser, page, page_size);
    assert(capture.packets == 0U);

    memcpy(body, body + headers + 255U, 45U);
    body[45] = 0xf8U;
    body[46] = 0xaaU;
    body[47] = 0x55U;
    laces[0] = 45U;
    laces[1] = 3U;
    page_size = make_page(page, 0x05U, 0x11223344U, 1U, laces, 2U,
                          body, 48U);
    feed_split(&parser, page, page_size);
    assert(capture.packets == 2U);
    assert(capture.serial[0] == 0x11223344U);
    assert(capture.channels[0] == 2U && capture.pre_skip[0] == 312U);
    assert(capture.sizes[0] == 300U && capture.first[0] == 0U &&
           capture.last[0] == 43U);
    assert(capture.sizes[1] == 3U && capture.first[1] == 0xf8U &&
           capture.last[1] == 0x55U);

    headers = make_headers(body, 1U, 120U);
    body[headers] = 0x08U;
    body[headers + 1U] = 0x99U;
    laces[0] = 19U;
    laces[1] = (uint8_t)(headers - 19U);
    laces[2] = 2U;
    page_size = make_page(page, 0x06U, 0xaabbccddU, 0U, laces, 3U,
                          body, headers + 2U);
    feed_split(&parser, page, page_size);
    assert(capture.packets == 3U);
    assert(capture.serial[2] == 0xaabbccddU);
    assert(capture.channels[2] == 1U && capture.pre_skip[2] == 120U);
    assert(capture.sizes[2] == 2U && capture.last[2] == 0x99U);
    assert(ogg_opus_finish(&parser) == OGG_OPUS_OK);
}

static void check_rejections(void)
{
    ogg_opus_parser_t parser;
    uint8_t page[2048];
    uint8_t body[1400];
    uint8_t laces[8];
    size_t headers;
    size_t size;

    memset(page, 0, 27U);
    ogg_opus_init(&parser, NULL, NULL);
    assert(ogg_opus_feed(&parser, page, 27U) == OGG_OPUS_ERR_CAPTURE);

    memcpy(page, "OggS", 4U);
    page[4] = 1U;
    ogg_opus_reset(&parser);
    assert(ogg_opus_feed(&parser, page, 27U) == OGG_OPUS_ERR_VERSION);

    headers = make_headers(body, 2U, 312U);
    body[18] = 1U;
    laces[0] = (uint8_t)headers;
    size = make_page(page, 0x06U, 7U, 0U, laces, 1U, body, headers);
    ogg_opus_reset(&parser);
    assert(ogg_opus_feed(&parser, page, size) == OGG_OPUS_ERR_HEAD);

    headers = make_headers(body, 2U, 312U);
    memset(body + headers, 0x7f, 1276U);
    laces[0] = 19U;
    laces[1] = (uint8_t)(headers - 19U);
    laces[2] = 255U;
    laces[3] = 255U;
    laces[4] = 255U;
    laces[5] = 255U;
    laces[6] = 255U;
    laces[7] = 1U;
    size = make_page(page, 0x06U, 7U, 0U, laces, 8U, body,
                     headers + 1276U);
    ogg_opus_reset(&parser);
    assert(ogg_opus_feed(&parser, page, size) ==
           OGG_OPUS_ERR_PACKET_TOO_LARGE);

    ogg_opus_reset(&parser);
    assert(ogg_opus_feed(&parser, "Ogg", 3U) == OGG_OPUS_OK);
    assert(ogg_opus_finish(&parser) == OGG_OPUS_ERR_TRUNCATED);
}

int main(void)
{
    check_split_continuation_and_chain();
    check_rejections();
    puts("ogg_opus_check: ok");
    return 0;
}
