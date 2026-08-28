// PS5 Radio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "icy_metadata.hpp"

#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    const uint8_t * data;
    size_t size;
    size_t at;
    size_t chunk;
    size_t error_at;
} test_reader_t;

static int test_read(void * context, void * data, size_t size)
{
    auto *reader = static_cast<test_reader_t *>(context);
    if(reader->at == reader->error_at) return -77;
    if(reader->at == reader->size) return 0;
    size_t copy = size < reader->chunk ? size : reader->chunk;
    if(copy > reader->size - reader->at) copy = reader->size - reader->at;
    if(reader->error_at > reader->at && copy > reader->error_at - reader->at)
        copy = reader->error_at - reader->at;
    memcpy(data, reader->data + reader->at, copy);
    reader->at += copy;
    return (int)copy;
}

static size_t read_all(icy_metadata_reader_t * reader,
                       uint8_t * output, size_t capacity)
{
    size_t size = 0U;
    while(size < capacity) {
        const int received = icy_metadata_read(
            reader, output + size, capacity - size < 4U ? capacity - size : 4U);
        if(received == 0) break;
        assert(received > 0);
        size += (size_t)received;
    }
    return size;
}

int main(void)
{
    static const char headers[] =
        "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nIcY-MeTaInT:\t5 \r\n";
    assert(icy_metadata_interval_from_headers(headers, sizeof(headers) - 1U) == 5U);
    static const char absent[] = "X-Icy-Metaint: 5\r\n";
    static const char malformed[] = "icy-metaint: 5x\r\n";
    static const char overflow[] = "icy-metaint: 999999999999999999999999999999\r\n";
    assert(icy_metadata_interval_from_headers(absent, sizeof(absent) - 1U) == 0U);
    assert(icy_metadata_interval_from_headers(malformed, sizeof(malformed) - 1U) == 0U);
    assert(icy_metadata_interval_from_headers(overflow, sizeof(overflow) - 1U) == 0U);

    uint8_t stream[32U];
    memcpy(stream, "abcde", 5U);
    stream[5] = 1U;
    memset(stream + 6U, 'M', 16U);
    stream[9] = 0xffU;
    stream[10] = 0xfbU;
    memcpy(stream + 22U, "FGHIJ", 5U);
    stream[27] = 0U;
    memcpy(stream + 28U, "klmn", 4U);

    test_reader_t source = {stream, sizeof(stream), 0U, 3U, SIZE_MAX};
    icy_metadata_reader_t reader;
    icy_metadata_reader_init(&reader, test_read, &source, 5U);
    uint8_t output[14U];
    assert(read_all(&reader, output, sizeof(output)) == sizeof(output));
    assert(memcmp(output, "abcdeFGHIJklmn", sizeof(output)) == 0);

    source = (test_reader_t){stream, 8U, 0U, 8U, SIZE_MAX};
    icy_metadata_reader_init(&reader, test_read, &source, 5U);
    assert(icy_metadata_read(&reader, output, sizeof(output)) == 5);
    assert(icy_metadata_read(&reader, output, sizeof(output)) == 0);

    source = (test_reader_t){stream, sizeof(stream), 0U, 32U, 8U};
    icy_metadata_reader_init(&reader, test_read, &source, 5U);
    assert(icy_metadata_read(&reader, output, sizeof(output)) == 5);
    assert(icy_metadata_read(&reader, output, sizeof(output)) == -77);

    source = (test_reader_t){stream, sizeof(stream), 0U, 32U, SIZE_MAX};
    icy_metadata_reader_init(&reader, test_read, &source, 0U);
    assert(icy_metadata_read(&reader, output, 4U) == 4);
    assert(memcmp(output, stream, 4U) == 0);
    return 0;
}
