#include "ogg_stream.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const uint8_t * data;
    size_t size;
    size_t offset;
    size_t max_chunk;
    int fail;
} memory_source_t;

static void write_u32le(uint8_t * p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint32_t page_crc(const uint8_t * data, size_t size)
{
    uint32_t crc = 0U;
    for(size_t i = 0; i < size; ++i) {
        const uint8_t byte = i >= 22U && i < 26U ? 0U : data[i];
        crc ^= (uint32_t)byte << 24;
        for(unsigned bit = 0; bit < 8U; ++bit)
            crc = (crc << 1) ^ ((crc & UINT32_C(0x80000000)) != 0U
                ? UINT32_C(0x04c11db7) : 0U);
    }
    return crc;
}

static size_t make_page(uint8_t * output, uint8_t flags, uint32_t serial,
                        uint32_t sequence, const uint8_t * body,
                        size_t body_size)
{
    assert(body_size <= 255U);
    memset(output, 0, OGG_PAGE_FIXED_HEADER_SIZE);
    memcpy(output, "OggS", 4U);
    output[5] = flags;
    write_u32le(output + 14U, serial);
    write_u32le(output + 18U, sequence);
    output[26] = 1U;
    output[27] = (uint8_t)body_size;
    memcpy(output + 28U, body, body_size);
    const size_t size = 28U + body_size;
    write_u32le(output + 22U, page_crc(output, size));
    return size;
}

static int memory_read(void * context, void * output, size_t size)
{
    memory_source_t * source = (memory_source_t *)context;
    if(source->fail) return -1;
    if(size > source->max_chunk) size = source->max_chunk;
    if(size > source->size - source->offset)
        size = source->size - source->offset;
    memcpy(output, source->data + source->offset, size);
    source->offset += size;
    return (int)size;
}

static void check_split_reads_and_chains(void)
{
    uint8_t input[256];
    uint8_t output[256];
    static ogg_stream_t stream;
    size_t first_size = make_page(input, 0x02U, 11U, 0U,
                                  (const uint8_t *)"one", 3U);
    first_size += make_page(input + first_size, 0x04U, 11U, 1U,
                            (const uint8_t *)"two", 3U);
    const size_t second_size = make_page(input + first_size, 0x06U, 22U, 0U,
                                         (const uint8_t *)"three", 5U);
    memory_source_t source = {
        input, first_size + second_size, 0U, 2U, 0
    };

    ogg_stream_init(&stream, memory_read, &source);
    assert(ogg_stream_read(&stream, output, sizeof(output)) ==
           (int)first_size);
    assert(memcmp(output, input, first_size) == 0);
    assert(ogg_stream_status(&stream) == OGG_STREAM_CHAIN_END);
    assert(source.offset == first_size);
    assert(ogg_stream_read(&stream, output, sizeof(output)) == 0);
    assert(ogg_stream_next_chain(&stream) == OGG_STREAM_OK);

    assert(ogg_stream_read(&stream, output, sizeof(output)) ==
           (int)second_size);
    assert(memcmp(output, input + first_size, second_size) == 0);
    assert(ogg_stream_status(&stream) == OGG_STREAM_CHAIN_END);
    assert(source.offset == first_size + second_size);
    assert(stream.stream_serial == 22U);
    assert(ogg_stream_next_chain(&stream) == OGG_STREAM_OK);
    assert(ogg_stream_read(&stream, output, sizeof(output)) == 0);
    assert(ogg_stream_status(&stream) == OGG_STREAM_EOF);
}

static void check_rejections(void)
{
    uint8_t input[128];
    uint8_t output[128];
    static ogg_stream_t stream;
    size_t size = make_page(input, 0x06U, 7U, 0U,
                            (const uint8_t *)"bad", 3U);
    input[size - 1U] ^= 1U;
    memory_source_t source = {input, size, 0U, 3U, 0};

    ogg_stream_init(&stream, memory_read, &source);
    assert(ogg_stream_read(&stream, output, sizeof(output)) == -1);
    assert(ogg_stream_status(&stream) == OGG_STREAM_ERR_CHECKSUM);
    assert(stream.page_size == 0U);

    size = make_page(input, 0x06U, 7U, 0U,
                     (const uint8_t *)"short", 5U);
    source = (memory_source_t){input, size - 1U, 0U, 4U, 0};
    ogg_stream_init(&stream, memory_read, &source);
    assert(ogg_stream_read(&stream, output, sizeof(output)) == -1);
    assert(ogg_stream_status(&stream) == OGG_STREAM_ERR_TRUNCATED);

    size = make_page(input, 0x02U, 7U, 0U,
                     (const uint8_t *)"first", 5U);
    size += make_page(input + size, 0x04U, 8U, 0U,
                      (const uint8_t *)"wrong", 5U);
    source = (memory_source_t){input, size, 0U, 5U, 0};
    ogg_stream_init(&stream, memory_read, &source);
    assert(ogg_stream_read(&stream, output, sizeof(output)) > 0);
    assert(ogg_stream_status(&stream) == OGG_STREAM_ERR_STREAM);
    assert(ogg_stream_read(&stream, output, sizeof(output)) == -1);

    source = (memory_source_t){input, size, 0U, 5U, 1};
    ogg_stream_init(&stream, memory_read, &source);
    assert(ogg_stream_read(&stream, output, sizeof(output)) == -1);
    assert(ogg_stream_status(&stream) == OGG_STREAM_ERR_READ);
    assert(ogg_stream_next_chain(&stream) == OGG_STREAM_ERR_STATE);

    size = make_page(input, 0x04U, 7U, 0U,
                     (const uint8_t *)"no-bos", 6U);
    source = (memory_source_t){input, size, 0U, 5U, 0};
    ogg_stream_init(&stream, memory_read, &source);
    assert(ogg_stream_read(&stream, output, sizeof(output)) == -1);
    assert(ogg_stream_status(&stream) == OGG_STREAM_ERR_STREAM);
}

int main(void)
{
    check_split_reads_and_chains();
    check_rejections();
    puts("ogg_stream_check: ok");
    return 0;
}
