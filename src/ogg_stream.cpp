// PS5 Radio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ogg_stream.hpp"

#include <limits.h>
#include <string.h>

static ogg_stream_result_t page_error(ogg_page_result_t result)
{
    switch (result)
    {
    case OGG_PAGE_OK:
        return OGG_STREAM_OK;
    case OGG_PAGE_ERR_ARGUMENT:
        return OGG_STREAM_ERR_ARGUMENT;
    case OGG_PAGE_ERR_CAPTURE:
        return OGG_STREAM_ERR_CAPTURE;
    case OGG_PAGE_ERR_VERSION:
        return OGG_STREAM_ERR_VERSION;
    case OGG_PAGE_ERR_CHECKSUM:
        return OGG_STREAM_ERR_CHECKSUM;
    case OGG_PAGE_ERR_TRUNCATED:
        return OGG_STREAM_ERR_TRUNCATED;
    default:
        return OGG_STREAM_ERR_PAGE;
    }
}

static int page_ready(const ogg_page_t *page, void *user_data)
{
    ogg_stream_t *stream = (ogg_stream_t *)user_data;

    if (!stream->have_stream && (((page->flags & 0x02U) == 0U) || page->sequence != 0U))
    {
        stream->status = OGG_STREAM_ERR_STREAM;
        return -1;
    }
    if (stream->have_stream &&
        (page->stream_serial != stream->stream_serial || (page->flags & 0x02U) != 0U))
    {
        stream->status = OGG_STREAM_ERR_STREAM;
        return -1;
    }
    if (!stream->have_stream)
    {
        stream->stream_serial = page->stream_serial;
        stream->have_stream = 1U;
    }

    memcpy(stream->page, page->data, page->size);
    stream->page_size = page->size;
    stream->page_offset = 0U;
    stream->page_eos = (uint8_t)((page->flags & 0x04U) != 0U);
    return 0;
}

static int pull_page(ogg_stream_t *stream)
{
    uint8_t input[OGG_PAGE_FIXED_HEADER_SIZE];

    while (stream->page_size == 0U)
    {
        size_t needed = stream->parser.target - stream->parser.used;
        if (needed > sizeof(input))
            needed = sizeof(input);
        const int count = stream->source_read(stream->source_context, input, needed);
        if (count < 0 || (size_t)count > needed)
        {
            stream->status = OGG_STREAM_ERR_READ;
            return -1;
        }
        if (count == 0)
        {
            const ogg_page_result_t result = ogg_page_finish(&stream->parser);
            stream->status = result == OGG_PAGE_OK ? OGG_STREAM_EOF : page_error(result);
            return stream->status == OGG_STREAM_EOF ? 0 : -1;
        }

        const ogg_page_result_t result = ogg_page_feed(&stream->parser, input, (size_t)count);
        if (result != OGG_PAGE_OK)
        {
            if (result != OGG_PAGE_ERR_CALLBACK || stream->status == OGG_STREAM_OK)
                stream->status = page_error(result);
            return -1;
        }
    }
    return 1;
}

void ogg_stream_init(ogg_stream_t *stream, ogg_stream_source_read_fn source_read,
                     void *source_context)
{
    if (stream == nullptr)
        return;
    memset(stream, 0, sizeof(*stream));
    stream->source_read = source_read;
    stream->source_context = source_context;
    stream->status = source_read != nullptr ? OGG_STREAM_OK : OGG_STREAM_ERR_ARGUMENT;
    ogg_page_init(&stream->parser, page_ready, stream);
}

int ogg_stream_read(void *context, void *output, size_t size)
{
    ogg_stream_t *stream = (ogg_stream_t *)context;
    uint8_t *data = (uint8_t *)output;
    size_t copied = 0U;

    if (stream == nullptr)
        return -1;
    if (data == nullptr && size != 0U)
    {
        stream->status = OGG_STREAM_ERR_ARGUMENT;
        return -1;
    }
    if (stream->status < OGG_STREAM_OK)
        return -1;
    if (stream->status != OGG_STREAM_OK || size == 0U)
        return 0;
    if (size > (size_t)INT_MAX)
        size = (size_t)INT_MAX;

    while (copied < size)
    {
        if (stream->page_offset == stream->page_size)
        {
            stream->page_size = 0U;
            stream->page_offset = 0U;
            if (pull_page(stream) <= 0)
                break;
        }

        size_t take = stream->page_size - stream->page_offset;
        if (take > size - copied)
            take = size - copied;
        memcpy(data + copied, stream->page + stream->page_offset, take);
        stream->page_offset += take;
        copied += take;

        if (stream->page_offset == stream->page_size && stream->page_eos)
        {
            stream->status = OGG_STREAM_CHAIN_END;
            break;
        }
    }

    if (copied != 0U)
        return (int)copied;
    return stream->status < OGG_STREAM_OK ? -1 : 0;
}

ogg_stream_result_t ogg_stream_status(const ogg_stream_t *stream)
{
    return stream != nullptr ? stream->status : OGG_STREAM_ERR_ARGUMENT;
}

ogg_stream_result_t ogg_stream_next_chain(ogg_stream_t *stream)
{
    if (stream == nullptr)
        return OGG_STREAM_ERR_ARGUMENT;
    if (stream->status != OGG_STREAM_CHAIN_END)
        return OGG_STREAM_ERR_STATE;

    stream->page_size = 0U;
    stream->page_offset = 0U;
    stream->page_eos = 0U;
    stream->have_stream = 0U;
    stream->stream_serial = 0U;
    stream->status = OGG_STREAM_OK;
    ogg_page_reset(&stream->parser);
    return OGG_STREAM_OK;
}
