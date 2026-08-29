// ProsperoRadio - Disk-backed Radio Browser catalogue interface.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "radio_service.hpp"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct radio_catalog_store_t {
    void * database;
    void * upsert_station;
    int error;
};

bool radio_catalog_store_global_init(void * page_cache, size_t bytes);
void radio_catalog_store_global_shutdown(void);
bool radio_catalog_store_open(radio_catalog_store_t * store, const char * path);
void radio_catalog_store_close(radio_catalog_store_t * store);
int radio_catalog_store_error(const radio_catalog_store_t * store);
bool radio_catalog_store_integrity_check(radio_catalog_store_t * store);
bool radio_catalog_store_backup(radio_catalog_store_t * destination,
                                radio_catalog_store_t * source);

bool radio_catalog_store_begin(radio_catalog_store_t * store);
bool radio_catalog_store_commit(radio_catalog_store_t * store);
void radio_catalog_store_rollback(radio_catalog_store_t * store);
bool radio_catalog_store_upsert_station(radio_catalog_store_t * store,
                                        const radio_station_t * station,
                                        uint64_t sync_id);
bool radio_catalog_store_prune_stations(radio_catalog_store_t * store,
                                        uint64_t sync_id, size_t limit,
                                        size_t * removed);
size_t radio_catalog_store_station_count(radio_catalog_store_t * store);
size_t radio_catalog_store_load_stations(radio_catalog_store_t * store,
                                         radio_station_t * stations,
                                         size_t capacity);
size_t radio_catalog_store_query_count(radio_catalog_store_t * store,
                                       const radio_catalog_query_t * query,
                                       bool favorites_only);
size_t radio_catalog_store_query_stations(
    radio_catalog_store_t * store, const radio_catalog_query_t * query,
    radio_catalog_order_t order, bool favorites_only, size_t offset,
    radio_station_t * stations, size_t capacity);

bool radio_catalog_store_set_favorite(radio_catalog_store_t * store,
                                      const char * uuid, bool favorite);
size_t radio_catalog_store_favorite_count(radio_catalog_store_t * store);
size_t radio_catalog_store_load_favorites(radio_catalog_store_t * store,
                                          char (*uuids)[40], size_t capacity);

bool radio_catalog_store_replace_facets(radio_catalog_store_t * store,
                                        radio_facet_kind_t kind,
                                        const radio_facet_t * facets,
                                        size_t count);
size_t radio_catalog_store_load_facets(radio_catalog_store_t * store,
                                       radio_facet_kind_t kind,
                                       radio_facet_t * facets,
                                       size_t capacity);

bool radio_catalog_store_set_meta(radio_catalog_store_t * store,
                                  const char * key, int64_t value);
bool radio_catalog_store_get_meta(radio_catalog_store_t * store,
                                  const char * key, int64_t * value);
