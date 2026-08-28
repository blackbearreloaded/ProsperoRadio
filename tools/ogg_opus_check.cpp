// PSRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ogg_opus.hpp"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned packets;
    uint32_t serial[4];
    uint32_t sequence[4];
    uint64_t granule[4];
    uint8_t channels[4];
    uint16_t pre_skip[4];
    int16_t gain_q8[4];
    uint8_t end_of_stream[4];
    uint8_t end_of_page[4];
    size_t sizes[4];
    uint8_t first[4];
    uint8_t last[4];
} capture_t;

typedef struct {
    unsigned pages;
    size_t size;
    size_t body_size;
    uint64_t granule;
    uint32_t serial;
    uint32_t sequence;
} page_capture_t;

typedef struct {
    unsigned packets;
    size_t last_size;
    uint8_t first;
} packet_size_capture_t;

static void write_u32le(uint8_t * p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void write_u64le(uint8_t * p, uint64_t value)
{
    write_u32le(p, (uint32_t)value);
    write_u32le(p + 4, (uint32_t)(value >> 32));
}

static uint32_t page_crc(const uint8_t * data, size_t size)
{
    uint32_t crc = 0;
    for(size_t i = 0; i < size; ++i) {
        const uint8_t byte = i >= 22U && i < 26U ? 0U : data[i];
        crc ^= (uint32_t)byte << 24;
        for(unsigned bit = 0; bit < 8U; ++bit) {
            crc = (crc << 1) ^ ((crc & UINT32_C(0x80000000)) != 0U
                ? UINT32_C(0x04c11db7) : 0U);
        }
    }
    return crc;
}

static size_t make_page_at(uint8_t * out, uint8_t flags, uint32_t serial,
                           uint32_t sequence, uint64_t granule,
                           const uint8_t * laces, size_t lace_count,
                           const uint8_t * body, size_t body_size)
{
    size_t i;
    size_t total = 0;
    assert(lace_count <= 255U);
    for(i = 0; i < lace_count; ++i) total += laces[i];
    assert(total == body_size);
    memset(out, 0, 27U);
    memcpy(out, "OggS", 4U);
    out[5] = flags;
    write_u64le(out + 6, granule);
    write_u32le(out + 14, serial);
    write_u32le(out + 18, sequence);
    out[26] = (uint8_t)lace_count;
    memcpy(out + 27U, laces, lace_count);
    memcpy(out + 27U + lace_count, body, body_size);
    const size_t size = 27U + lace_count + body_size;
    write_u32le(out + 22, page_crc(out, size));
    return size;
}

static size_t make_page(uint8_t * out, uint8_t flags, uint32_t serial,
                        uint32_t sequence, const uint8_t * laces,
                        size_t lace_count, const uint8_t * body,
                        size_t body_size)
{
    return make_page_at(out, flags, serial, sequence, 0U, laces, lace_count,
                        body, body_size);
}

static size_t make_headers_gain(uint8_t * out, uint8_t channels,
                                uint16_t pre_skip, int16_t gain_q8)
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
    out[16] = (uint8_t)gain_q8;
    out[17] = (uint8_t)((uint16_t)gain_q8 >> 8);
    out[18] = 0U;
    memcpy(out + 19U, tags, sizeof(tags));
    return 19U + sizeof(tags);
}

static size_t make_headers(uint8_t * out, uint8_t channels, uint16_t pre_skip)
{
    return make_headers_gain(out, channels, pre_skip, 0);
}

static int capture_page(const ogg_page_t * page, void * user_data)
{
    auto *capture = static_cast<page_capture_t *>(user_data);
    ++capture->pages;
    capture->size = page->size;
    capture->body_size = page->body_size;
    capture->granule = page->granule_position;
    capture->serial = page->stream_serial;
    capture->sequence = page->sequence;
    return 0;
}

static int capture_packet(const ogg_opus_packet_t * packet, void * user_data)
{
    auto *capture = static_cast<capture_t *>(user_data);
    const unsigned n = capture->packets++;
    assert(n < 4U);
    capture->serial[n] = packet->stream_serial;
    capture->sequence[n] = packet->page_sequence;
    capture->granule[n] = packet->granule_position;
    capture->channels[n] = packet->channels;
    capture->pre_skip[n] = packet->pre_skip;
    capture->gain_q8[n] = packet->output_gain_q8;
    capture->end_of_stream[n] = packet->end_of_stream;
    capture->end_of_page[n] = packet->end_of_page;
    capture->sizes[n] = packet->size;
    capture->first[n] = packet->data[0];
    capture->last[n] = packet->data[packet->size - 1U];
    return 0;
}

static int capture_packet_size(const ogg_opus_packet_t * packet,
                               void * user_data)
{
    auto *capture = static_cast<packet_size_capture_t *>(user_data);
    ++capture->packets;
    capture->last_size = packet->size;
    capture->first = packet->data[0];
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

static void feed_header_pages(ogg_opus_parser_t * parser, uint8_t * page,
                              uint8_t * body, uint32_t serial)
{
    const size_t headers = make_headers(body, 2U, 312U);
    uint8_t lace = 19U;
    size_t size = make_page_at(page, 0x02U, serial, 0U, 0U,
                               &lace, 1U, body, 19U);
    feed_split(parser, page, size);
    lace = (uint8_t)(headers - 19U);
    size = make_page_at(page, 0U, serial, 1U, 0U,
                        &lace, 1U, body + 19U, headers - 19U);
    feed_split(parser, page, size);
}

static size_t packet_laces(uint8_t * laces, size_t packet_size)
{
    size_t count = 0U;
    while(packet_size >= 255U) {
        laces[count++] = 255U;
        packet_size -= 255U;
    }
    laces[count++] = (uint8_t)packet_size;
    return count;
}

static void check_maximum_page(void)
{
    static uint8_t page[OGG_PAGE_MAX_SIZE];
    static uint8_t body[OGG_PAGE_MAX_BODY_SIZE];
    uint8_t laces[OGG_PAGE_MAX_SEGMENTS];
    ogg_page_parser_t parser;
    page_capture_t capture;

    memset(body, 0xa5, sizeof(body));
    memset(laces, 255, sizeof(laces));
    const size_t size = make_page_at(
        page, 0x06U, 0x78563412U, 0U, UINT64_C(0x0102030405060708),
        laces, sizeof(laces), body, sizeof(body));
    assert(size == OGG_PAGE_MAX_SIZE);

    memset(&capture, 0, sizeof(capture));
    ogg_page_init(&parser, capture_page, &capture);
    assert(ogg_page_feed(&parser, page, size - 1U) == OGG_PAGE_OK);
    assert(capture.pages == 0U);
    assert(ogg_page_feed(&parser, page + size - 1U, 1U) == OGG_PAGE_OK);
    assert(capture.pages == 1U);
    assert(capture.size == OGG_PAGE_MAX_SIZE &&
           capture.body_size == OGG_PAGE_MAX_BODY_SIZE);
    assert(capture.granule == UINT64_C(0x0102030405060708));
    assert(capture.serial == 0x78563412U && capture.sequence == 0U);
    assert(ogg_page_finish(&parser) == OGG_PAGE_OK);
}

static void check_packet_size_limit(void)
{
    static uint8_t page[OGG_PAGE_MAX_SIZE];
    static uint8_t body[OGG_OPUS_MAX_AUDIO_PACKET_SIZE + 1U];
    uint8_t header_body[64U];
    uint8_t laces[OGG_PAGE_MAX_SEGMENTS];
    ogg_opus_parser_t parser;
    packet_size_capture_t capture;

    memset(body, 0x5a, sizeof(body));
    memset(&capture, 0, sizeof(capture));
    ogg_opus_init(&parser, capture_packet_size, &capture);
    feed_header_pages(&parser, page, header_body, 0x11111111U);
    size_t lace_count = packet_laces(laces, OGG_OPUS_MAX_AUDIO_PACKET_SIZE);
    size_t size = make_page_at(
        page, 0x04U, 0x11111111U, 2U, 5760U, laces, lace_count,
        body, OGG_OPUS_MAX_AUDIO_PACKET_SIZE);
    feed_split(&parser, page, size);
    assert(capture.packets == 1U &&
           capture.last_size == OGG_OPUS_MAX_AUDIO_PACKET_SIZE &&
           capture.first == 0x5aU);

    memset(&capture, 0, sizeof(capture));
    ogg_opus_init(&parser, capture_packet_size, &capture);
    feed_header_pages(&parser, page, header_body, 0x22222222U);
    lace_count = packet_laces(laces, OGG_OPUS_MAX_AUDIO_PACKET_SIZE + 1U);
    size = make_page_at(
        page, 0x04U, 0x22222222U, 2U, 5760U, laces, lace_count,
        body, OGG_OPUS_MAX_AUDIO_PACKET_SIZE + 1U);
    assert(ogg_opus_feed(&parser, page, size) ==
           OGG_OPUS_ERR_PACKET_TOO_LARGE);
    assert(capture.packets == 0U);
}

static void check_live_join_continuation(void)
{
    ogg_opus_parser_t parser;
    packet_size_capture_t capture;
    uint8_t page[256U];
    uint8_t header_body[64U];
    uint8_t body[] = {0xaaU, 0xbbU, 0xf8U, 0x11U, 0x22U};
    const uint8_t laces[] = {2U, 3U};

    memset(&capture, 0, sizeof(capture));
    ogg_opus_init(&parser, capture_packet_size, &capture);
    feed_header_pages(&parser, page, header_body, 0x33333333U);
    const size_t size = make_page_at(
        page, 0x01U, 0x33333333U, 99U, 960U,
        laces, sizeof(laces), body, sizeof(body));
    feed_split(&parser, page, size);
    assert(capture.packets == 1U && capture.last_size == 3U &&
           capture.first == 0xf8U);
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

    headers = make_headers_gain(body, 2U, 312U, -512);
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
    page_size = make_page_at(page, 0x05U, 0x11223344U, 1U, 1920U,
                             laces, 2U, body, 48U);
    feed_split(&parser, page, page_size);
    assert(capture.packets == 2U);
    assert(capture.serial[0] == 0x11223344U);
    assert(capture.sequence[0] == 1U && capture.granule[0] == 1920U);
    assert(capture.channels[0] == 2U && capture.pre_skip[0] == 312U);
    assert(capture.gain_q8[0] == -512 && capture.end_of_stream[0] == 1U);
    assert(capture.end_of_page[0] == 0U);
    assert(capture.sizes[0] == 300U && capture.first[0] == 0U &&
           capture.last[0] == 43U);
    assert(capture.sizes[1] == 3U && capture.first[1] == 0xf8U &&
           capture.last[1] == 0x55U);
    assert(capture.sequence[1] == 1U && capture.granule[1] == 1920U &&
           capture.gain_q8[1] == -512 && capture.end_of_stream[1] == 1U);
    assert(capture.end_of_page[1] == 1U);

    headers = make_headers_gain(body, 1U, 120U, 384);
    laces[0] = 19U;
    laces[1] = (uint8_t)(headers - 19U);
    page_size = make_page(page, 0x02U, 0xaabbccddU, 0U, laces, 2U,
                          body, headers);
    feed_split(&parser, page, page_size);
    body[0] = 0x08U;
    body[1] = 0x99U;
    laces[0] = 2U;
    page_size = make_page_at(page, 0x04U, 0xaabbccddU, 99U, 480U,
                             laces, 1U, body, 2U);
    feed_split(&parser, page, page_size);
    assert(capture.packets == 3U);
    assert(capture.serial[2] == 0xaabbccddU);
    assert(capture.sequence[2] == 99U && capture.granule[2] == 480U);
    assert(capture.channels[2] == 1U && capture.pre_skip[2] == 120U);
    assert(capture.gain_q8[2] == 384 && capture.end_of_stream[2] == 1U);
    assert(capture.end_of_page[2] == 1U);
    assert(capture.sizes[2] == 2U && capture.last[2] == 0x99U);
    assert(ogg_opus_finish(&parser) == OGG_OPUS_OK);
}

static void check_rejections(void)
{
    ogg_opus_parser_t parser;
    capture_t capture;
    uint8_t page[2048];
    uint8_t body[1400];
    uint8_t laces[8];
    size_t headers;
    size_t size;

    memset(page, 0, 27U);
    ogg_opus_init(&parser, nullptr, nullptr);
    assert(ogg_opus_feed(&parser, page, 27U) == OGG_OPUS_ERR_CAPTURE);

    memcpy(page, "OggS", 4U);
    page[4] = 1U;
    ogg_opus_reset(&parser);
    assert(ogg_opus_feed(&parser, page, 27U) == OGG_OPUS_ERR_VERSION);

    memset(page, 0, 27U);
    memcpy(page, "OggS", 4U);
    page[5] = 0x08U;
    ogg_opus_reset(&parser);
    assert(ogg_opus_feed(&parser, page, 27U) == OGG_OPUS_ERR_PAGE);

    memset(&capture, 0, sizeof(capture));
    headers = make_headers_gain(body, 2U, 312U, -1);
    body[headers] = 0xf8U;
    body[headers + 1U] = 0xaaU;
    body[headers + 2U] = 0x55U;
    laces[0] = 19U;
    laces[1] = (uint8_t)(headers - 19U);
    laces[2] = 3U;
    size = make_page_at(page, 0x06U, 9U, 0U, 960U, laces, 3U,
                        body, headers + 3U);
    page[size - 1U] ^= 0x80U;
    ogg_opus_init(&parser, capture_packet, &capture);
    assert(ogg_opus_feed(&parser, page, size - 1U) == OGG_OPUS_OK);
    assert(capture.packets == 0U);
    assert(ogg_opus_feed(&parser, page + size - 1U, 1U) ==
           OGG_OPUS_ERR_CHECKSUM);
    assert(capture.packets == 0U);

    ogg_opus_init(&parser, nullptr, nullptr);
    headers = make_headers(body, 2U, 312U);
    laces[0] = 19U;
    laces[1] = (uint8_t)(headers - 19U);
    size = make_page(page, 0x02U, 7U, 0U, laces, 2U, body, headers);
    ogg_opus_reset(&parser);
    assert(ogg_opus_feed(&parser, page, size) == OGG_OPUS_OK);
    body[0] = 0xf8U;
    body[1] = 0xaaU;
    body[2] = 0x55U;
    laces[0] = 3U;
    size = make_page(page, 0U, 7U, 18088U, laces, 1U, body, 3U);
    assert(ogg_opus_feed(&parser, page, size) == OGG_OPUS_OK);
    size = make_page(page, 0U, 7U, 18090U, laces, 1U, body, 3U);
    assert(ogg_opus_feed(&parser, page, size) == OGG_OPUS_ERR_SEQUENCE);

    headers = make_headers(body, 2U, 312U);
    laces[0] = 19U;
    laces[1] = (uint8_t)(headers - 19U);
    size = make_page(page, 0x02U, 7U, 0U, laces, 2U, body, headers);
    ogg_opus_reset(&parser);
    assert(ogg_opus_feed(&parser, page, size) == OGG_OPUS_OK);
    size = make_page(page, 0U, 7U, 100U, laces, 0U, body, 0U);
    assert(ogg_opus_feed(&parser, page, size) == OGG_OPUS_OK);
    size = make_page(page, 0U, 7U, 200U, laces, 0U, body, 0U);
    assert(ogg_opus_feed(&parser, page, size) == OGG_OPUS_ERR_SEQUENCE);

    headers = make_headers(body, 2U, 312U);
    laces[0] = 19U;
    laces[1] = (uint8_t)(headers - 19U);
    size = make_page(page, 0x02U, 7U, 0U, laces, 2U, body, headers);
    ogg_opus_reset(&parser);
    assert(ogg_opus_feed(&parser, page, size) == OGG_OPUS_OK);
    body[0] = 0xf8U;
    laces[0] = 1U;
    size = make_page(page, 0x01U, 7U, 18088U, laces, 1U, body, 1U);
    assert(ogg_opus_feed(&parser, page, size) == OGG_OPUS_OK);

    headers = make_headers(body, 2U, 312U);
    body[18] = 1U;
    laces[0] = (uint8_t)headers;
    size = make_page(page, 0x06U, 7U, 0U, laces, 1U, body, headers);
    ogg_opus_reset(&parser);
    assert(ogg_opus_feed(&parser, page, size) == OGG_OPUS_ERR_HEAD);

    ogg_opus_reset(&parser);
    assert(ogg_opus_feed(&parser, "Ogg", 3U) == OGG_OPUS_OK);
    assert(ogg_opus_finish(&parser) == OGG_OPUS_ERR_TRUNCATED);
}

static int check_file(const char * path)
{
    ogg_opus_parser_t parser;
    uint8_t data[251];
    unsigned chunks = 0;
    FILE * file = fopen(path, "rb");
    if(file == nullptr) return 2;
    ogg_opus_init(&parser, nullptr, nullptr);
    for(;;) {
        const size_t size = fread(data, 1U, sizeof(data), file);
        if(size != 0U) {
            const ogg_opus_result_t result = ogg_opus_feed(&parser, data, size);
            if(result != OGG_OPUS_OK) {
                fprintf(stderr, "%s: %s (%d)\n", path,
                        ogg_opus_result_string(result), result);
                fclose(file);
                return 1;
            }
            ++chunks;
        }
        if(size != sizeof(data)) break;
    }
    fclose(file);
    printf("%s: ok (%u chunks)\n", path, chunks);
    return 0;
}

int main(int argc, char ** argv)
{
    if(argc == 2) return check_file(argv[1]);
    check_maximum_page();
    check_packet_size_limit();
    check_live_join_continuation();
    check_split_continuation_and_chain();
    check_rejections();
    puts("ogg_opus_check: ok");
    return 0;
}
