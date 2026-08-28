// PSRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>

enum radio_playlist_kind_t {
    RADIO_PLAYLIST_NONE = 0,
    RADIO_PLAYLIST_M3U,
    RADIO_PLAYLIST_PLS,
    RADIO_PLAYLIST_HLS
};

enum radio_playlist_result_t {
    RADIO_PLAYLIST_OK = 0,
    RADIO_PLAYLIST_INVALID = -1,
    RADIO_PLAYLIST_LIMIT = -2,
    RADIO_PLAYLIST_NO_ENTRY = -3,
    RADIO_PLAYLIST_IS_HLS = -4
};

radio_playlist_kind_t radio_playlist_kind_from_url(const char * url);
radio_playlist_kind_t radio_playlist_kind_from_headers(const char * headers,
                                                        size_t size);
radio_playlist_kind_t radio_playlist_kind_from_body(const char * data,
                                                     size_t size);
radio_playlist_result_t radio_playlist_resolve_url(
    const char * base_url, const char * reference,
    char * output, size_t output_size);
radio_playlist_result_t radio_playlist_first_url(
    radio_playlist_kind_t kind, const char * data, size_t size,
    const char * playlist_url, char * output, size_t output_size);
