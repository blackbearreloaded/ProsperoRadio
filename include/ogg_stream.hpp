// PSRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ogg_page.hpp"

using ogg_stream_source_read_fn = int (*)(void * context, void * data,
                                          size_t size);

enum ogg_stream_result_t {
    OGG_STREAM_OK = 0,
    OGG_STREAM_CHAIN_END = 1,
    OGG_STREAM_EOF = 2,
    OGG_STREAM_ERR_ARGUMENT = -1,
    OGG_STREAM_ERR_READ = -2,
    OGG_STREAM_ERR_CAPTURE = -3,
    OGG_STREAM_ERR_VERSION = -4,
    OGG_STREAM_ERR_PAGE = -5,
    OGG_STREAM_ERR_CHECKSUM = -6,
    OGG_STREAM_ERR_TRUNCATED = -7,
    OGG_STREAM_ERR_STREAM = -8,
    OGG_STREAM_ERR_STATE = -9
};

/* Caller-owned validated pull-reader. Allocate large instances off-stack. */
struct ogg_stream_t {
    ogg_page_parser_t parser;
    uint8_t page[OGG_PAGE_MAX_SIZE];
    size_t page_size;
    size_t page_offset;
    uint32_t stream_serial;
    uint8_t have_stream;
    uint8_t page_eos;
    ogg_stream_result_t status;
    ogg_stream_source_read_fn source_read;
    void * source_context;
};

void ogg_stream_init(ogg_stream_t * stream,
                     ogg_stream_source_read_fn source_read,
                     void * source_context);

/* Decoder-compatible read callback: positive bytes, zero at EOF or chain end,
   and -1 on error. Check ogg_stream_status() to distinguish those cases. */
int ogg_stream_read(void * context, void * data, size_t size);
ogg_stream_result_t ogg_stream_status(const ogg_stream_t * stream);

/* Call only after OGG_STREAM_CHAIN_END to expose the next logical stream. */
ogg_stream_result_t ogg_stream_next_chain(ogg_stream_t * stream);
