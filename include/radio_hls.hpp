// ProsperoRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>
#include <stdint.h>

#define RADIO_HLS_URL_BYTES 2048U
#define RADIO_HLS_MAX_VARIANTS 12U
#define RADIO_HLS_MAX_SEGMENTS 64U

enum radio_hls_result_t {
    RADIO_HLS_OK = 0,
    RADIO_HLS_INVALID = -1,
    RADIO_HLS_LIMIT = -2,
    RADIO_HLS_MALFORMED = -3,
    RADIO_HLS_UNSUPPORTED = -4,
    RADIO_HLS_NO_VARIANT = -5
};

enum radio_hls_kind_t {
    RADIO_HLS_NONE = 0,
    RADIO_HLS_MASTER,
    RADIO_HLS_MEDIA
};

struct radio_hls_variant_t {
    char url[RADIO_HLS_URL_BYTES];
    uint64_t bandwidth;
    uint32_t source_channels;
};

struct radio_hls_segment_t {
    char url[RADIO_HLS_URL_BYTES];
    uint64_t sequence;
    uint32_t discontinuity;
};

struct radio_hls_playlist_t {
    radio_hls_kind_t kind;
    uint32_t is_live;
    uint32_t target_duration_ms;
    uint64_t media_sequence;
    uint64_t discontinuity_sequence;
    uint32_t variant_count;
    uint32_t segment_count;
    radio_hls_variant_t variants[RADIO_HLS_MAX_VARIANTS];
    radio_hls_segment_t segments[RADIO_HLS_MAX_SEGMENTS];
};

radio_hls_result_t radio_hls_parse(const char * data, size_t size,
                                   const char * playlist_url,
                                   radio_hls_playlist_t * playlist);
int radio_hls_select_variant(const radio_hls_playlist_t * playlist);
