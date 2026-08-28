// PS5 Radio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "icy_metadata.hpp"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static char ascii_lower(char value)
{
    return value >= 'A' && value <= 'Z' ? (char)(value + ('a' - 'A')) : value;
}

static bool equal_case(const char *value, size_t size, const char *text)
{
    const size_t length = strlen(text);
    if (size != length)
        return false;
    for (size_t i = 0U; i < size; ++i)
    {
        if (ascii_lower(value[i]) != ascii_lower(text[i]))
            return false;
    }
    return true;
}

size_t icy_metadata_interval_from_headers(const char *headers, size_t size)
{
    if (headers == nullptr)
        return 0U;
    size_t position = 0U;
    while (position < size)
    {
        const size_t start = position;
        while (position < size && headers[position] != '\r' && headers[position] != '\n')
            ++position;
        const size_t end = position;
        while (position < size && (headers[position] == '\r' || headers[position] == '\n'))
            ++position;

        size_t colon = start;
        while (colon < end && headers[colon] != ':')
            ++colon;
        if (colon == end || !equal_case(headers + start, colon - start, "icy-metaint"))
            continue;

        size_t at = colon + 1U;
        while (at < end && (headers[at] == ' ' || headers[at] == '\t'))
            ++at;
        size_t interval = 0U;
        const size_t digits = at;
        while (at < end && headers[at] >= '0' && headers[at] <= '9')
        {
            const size_t digit = (size_t)(headers[at] - '0');
            if (interval > (SIZE_MAX - digit) / 10U)
                return 0U;
            interval = interval * 10U + digit;
            ++at;
        }
        while (at < end && (headers[at] == ' ' || headers[at] == '\t'))
            ++at;
        return at == end && at != digits && interval != 0U ? interval : 0U;
    }
    return 0U;
}

void icy_metadata_reader_init(icy_metadata_reader_t *reader, icy_metadata_read_fn read,
                              void *context, size_t interval)
{
    if (reader == nullptr)
        return;
    memset(reader, 0, sizeof(*reader));
    reader->read = read;
    reader->context = context;
    reader->interval = interval;
    reader->audio_remaining = interval;
}

int icy_metadata_read(void *context, void *data, size_t size)
{
    auto *reader = static_cast<icy_metadata_reader_t *>(context);
    if (reader == nullptr || reader->read == nullptr || (data == nullptr && size != 0U))
        return -1;
    if (size == 0U)
        return 0;
    if (reader->interval == 0U)
        return reader->read(reader->context, data, size);

    uint8_t discard[256];
    for (;;)
    {
        if (reader->metadata_remaining != 0U)
        {
            const size_t wanted = reader->metadata_remaining < sizeof(discard)
                                      ? reader->metadata_remaining
                                      : sizeof(discard);
            const int received = reader->read(reader->context, discard, wanted);
            if (received <= 0)
                return received;
            if ((size_t)received > wanted)
                return -1;
            reader->metadata_remaining -= (size_t)received;
            continue;
        }
        if (reader->audio_remaining == 0U)
        {
            uint8_t length = 0U;
            const int received = reader->read(reader->context, &length, 1U);
            if (received <= 0)
                return received;
            if (received != 1)
                return -1;
            reader->metadata_remaining = (size_t)length * 16U;
            reader->audio_remaining = reader->interval;
            continue;
        }

        const size_t wanted = size < reader->audio_remaining ? size : reader->audio_remaining;
        const int received = reader->read(reader->context, data, wanted);
        if (received <= 0)
            return received;
        if ((size_t)received > wanted)
            return -1;
        reader->audio_remaining -= (size_t)received;
        return received;
    }
}
