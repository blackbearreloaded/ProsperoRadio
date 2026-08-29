// ProsperoRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>

using icy_metadata_read_fn = int (*)(void * context, void * data, size_t size);

struct icy_metadata_reader_t {
    icy_metadata_read_fn read;
    void * context;
    size_t interval;
    size_t audio_remaining;
    size_t metadata_remaining;
};

size_t icy_metadata_interval_from_headers(const char * headers, size_t size);
void icy_metadata_reader_init(icy_metadata_reader_t * reader,
                              icy_metadata_read_fn read, void * context,
                              size_t interval);
int icy_metadata_read(void * context, void * data, size_t size);
