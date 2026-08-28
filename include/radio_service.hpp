// PSRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RADIO_RANK_NONE UINT16_MAX
#define RADIO_MAX_FACETS 512

enum radio_facet_kind_t {
    RADIO_FACET_COUNTRY,
    RADIO_FACET_GENRE,
    RADIO_FACET_LANGUAGE
};

struct radio_facet_t {
    char value[64];
    char label[80];
    uint32_t station_count;
};

struct radio_catalog_query_t {
    char name[157];
    char country_code[4];
    char tag[64];
    char language[64];
    uint32_t bitrate_min;
};

enum radio_catalog_order_t {
    RADIO_CATALOG_ORDER_POPULAR,
    RADIO_CATALOG_ORDER_TRENDING,
    RADIO_CATALOG_ORDER_VOTED,
    RADIO_CATALOG_ORDER_NAME
};

struct radio_station_t {
    char uuid[40];
    char name[112];
    char url[512];
    char country[56];
    char country_code[4];
    char state[64];
    char language[80];
    char tags[112];
    char codec[12];
    uint32_t bitrate;
    uint32_t votes;
    uint32_t click_count;
    int32_t click_trend;
    uint32_t hls;
    uint16_t popular_rank;
    uint16_t trending_rank;
    uint16_t voted_rank;
};

enum radio_catalog_state_t {
    RADIO_CATALOG_LOADING,
    RADIO_CATALOG_CACHED,
    RADIO_CATALOG_READY,
    RADIO_CATALOG_ERROR
};

enum radio_playback_state_t {
    RADIO_PLAYBACK_STOPPED,
    RADIO_PLAYBACK_CONNECTING,
    RADIO_PLAYBACK_BUFFERING,
    RADIO_PLAYBACK_PLAYING,
    RADIO_PLAYBACK_STOPPING,
    RADIO_PLAYBACK_ERROR
};

struct radio_service_status_t {
    radio_catalog_state_t catalog_state;
    radio_playback_state_t playback_state;
    unsigned catalog_generation;
    unsigned catalog_size;
    unsigned station_count;
    unsigned playing_index;
    unsigned sample_rate;
    unsigned channels;
    unsigned sync_station_count;
    int error_code;
    bool refreshing;
    bool searching;
};

bool radio_service_init(void);
void radio_service_shutdown(void);
void radio_service_get_status(radio_service_status_t * out_status);
bool radio_service_get_station(unsigned index, radio_station_t * out_station);
bool radio_service_query_page(const radio_catalog_query_t * query,
                              radio_catalog_order_t order,
                              bool favorites_only, unsigned offset,
                              unsigned limit, unsigned * out_total);
bool radio_service_station_is_playing(unsigned index);
bool radio_service_get_playing_station(radio_station_t * out_station);
unsigned radio_service_get_facet_count(radio_facet_kind_t kind);
bool radio_service_get_facet(radio_facet_kind_t kind, unsigned index,
                             radio_facet_t * out_facet);
bool radio_service_is_favorite(const char * uuid);
bool radio_service_toggle_favorite(unsigned station_index);
bool radio_service_refresh(void);
bool radio_service_search(const radio_catalog_query_t * query);
void radio_service_play(unsigned station_index);
void radio_service_stop(void);
