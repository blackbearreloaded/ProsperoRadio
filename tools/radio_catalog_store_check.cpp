// ProsperoRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "radio_catalog_store.hpp"

#include <assert.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static radio_station_t station(const char * uuid, const char * name,
                               unsigned clicks)
{
    radio_station_t value;
    memset(&value, 0, sizeof(value));
    strncpy(value.uuid, uuid, sizeof(value.uuid) - 1U);
    strncpy(value.name, name, sizeof(value.name) - 1U);
    strncpy(value.url, "https://example.invalid/live", sizeof(value.url) - 1U);
    strncpy(value.country_code, "US", sizeof(value.country_code) - 1U);
    strncpy(value.codec, "AAC", sizeof(value.codec) - 1U);
    value.click_count = clicks;
    value.bitrate = 128U;
    return value;
}

int main(void)
{
    const size_t page_cache_bytes = 8U * 1024U * 1024U;
    void * page_cache = malloc(page_cache_bytes);
    assert(page_cache != nullptr);
    assert(radio_catalog_store_global_init(page_cache, page_cache_bytes));
    char path[96];
    snprintf(path, sizeof(path), "/tmp/prospero-radio-catalog-%ld.sqlite3",
             (long)getpid());
    unlink(path);
    radio_catalog_store_t store;
    assert(radio_catalog_store_open(&store, path));
    sqlite3_stmt * journal = nullptr;
    assert(sqlite3_prepare_v2((sqlite3 *)store.database,
        "PRAGMA journal_mode", -1, &journal, nullptr) == SQLITE_OK);
    assert(sqlite3_step(journal) == SQLITE_ROW);
    assert(strcmp((const char *)sqlite3_column_text(journal, 0), "memory") == 0);
    sqlite3_finalize(journal);
    sqlite3_stmt * synchronous = nullptr;
    assert(sqlite3_prepare_v2((sqlite3 *)store.database,
        "PRAGMA synchronous", -1, &synchronous, nullptr) == SQLITE_OK);
    assert(sqlite3_step(synchronous) == SQLITE_ROW);
    assert(sqlite3_column_int(synchronous, 0) == 0);
    sqlite3_finalize(synchronous);
    sqlite3_stmt * cache_size = nullptr;
    assert(sqlite3_prepare_v2((sqlite3 *)store.database,
        "PRAGMA cache_size", -1, &cache_size, nullptr) == SQLITE_OK);
    assert(sqlite3_step(cache_size) == SQLITE_ROW);
    assert(sqlite3_column_int(cache_size, 0) == -32768);
    sqlite3_finalize(cache_size);
    radio_station_t first = station("first", "First", 10U);
    radio_station_t second = station("second", "Second", 20U);
    assert(radio_catalog_store_begin(&store));
    assert(radio_catalog_store_upsert_station(&store, &first, 1U));
    assert(radio_catalog_store_upsert_station(&store, &second, 1U));
    assert(radio_catalog_store_commit(&store));
    assert(radio_catalog_store_station_count(&store) == 2U);

    radio_station_t loaded[2];
    assert(radio_catalog_store_load_stations(&store, loaded, 2U) == 2U);
    assert(strcmp(loaded[0].uuid, "second") == 0);
    assert(strcmp(loaded[1].uuid, "first") == 0);

    strcpy(first.name, "Updated");
    assert(radio_catalog_store_upsert_station(&store, &first, 2U));
    size_t removed = 0U;
    assert(radio_catalog_store_prune_stations(&store, 2U, 128U, &removed));
    assert(removed == 1U);
    assert(radio_catalog_store_station_count(&store) == 1U);
    assert(radio_catalog_store_load_stations(&store, loaded, 2U) == 1U);
    assert(strcmp(loaded[0].name, "Updated") == 0);

    radio_station_t third = station("third", "Jazz Berlin", 30U);
    strcpy(third.country, "Germany");
    strcpy(third.country_code, "DE");
    strcpy(third.tags, "jazz,electronic");
    strcpy(third.language, "german");
    third.votes = 50U;
    third.click_trend = 7;
    assert(radio_catalog_store_upsert_station(&store, &third, 2U));
    radio_catalog_query_t query = {};
    strcpy(query.country_code, "DE");
    strcpy(query.tag, "JAZZ");
    strcpy(query.language, "GERMAN");
    strcpy(query.name, "BERLIN");
    query.bitrate_min = 64U;
    assert(radio_catalog_store_query_count(&store, &query, false) == 1U);
    assert(radio_catalog_store_query_stations(
        &store, &query, RADIO_CATALOG_ORDER_TRENDING, false, 0U,
        loaded, 2U) == 1U);
    assert(strcmp(loaded[0].uuid, "third") == 0);
    assert(radio_catalog_store_query_stations(
        &store, nullptr, RADIO_CATALOG_ORDER_POPULAR, false, 1U,
        loaded, 1U) == 1U);
    assert(strcmp(loaded[0].uuid, "first") == 0);

    assert(radio_catalog_store_set_favorite(&store, "first", true));
    assert(radio_catalog_store_favorite_count(&store) == 1U);
    char favorites[2][40] = {{0}};
    assert(radio_catalog_store_load_favorites(&store, favorites, 2U) == 1U);
    assert(strcmp(favorites[0], "first") == 0);
    assert(radio_catalog_store_query_count(&store, nullptr, true) == 1U);
    assert(radio_catalog_store_query_stations(
        &store, nullptr, RADIO_CATALOG_ORDER_POPULAR, true, 0U,
        loaded, 2U) == 1U);
    assert(strcmp(loaded[0].uuid, "first") == 0);
    assert(radio_catalog_store_set_favorite(&store, "first", false));
    assert(radio_catalog_store_favorite_count(&store) == 0U);
    assert(radio_catalog_store_load_favorites(&store, favorites, 2U) == 0U);

    const radio_facet_t facets[] = {
        {.value = "US", .label = "United States", .station_count = 100U},
        {.value = "DE", .label = "Germany", .station_count = 80U},
    };
    assert(radio_catalog_store_replace_facets(
        &store, RADIO_FACET_COUNTRY, facets, 2U));
    radio_facet_t loaded_facets[2];
    assert(radio_catalog_store_load_facets(
        &store, RADIO_FACET_COUNTRY, loaded_facets, 2U) == 2U);
    assert(strcmp(loaded_facets[1].value, "DE") == 0);

    assert(radio_catalog_store_set_meta(&store, "last_sync", 42));
    int64_t value = 0;
    assert(radio_catalog_store_get_meta(&store, "last_sync", &value));
    assert(value == 42);
    assert(radio_catalog_store_integrity_check(&store));

    char backup_path[104];
    snprintf(backup_path, sizeof(backup_path), "%s.backup", path);
    unlink(backup_path);
    radio_catalog_store_t backup;
    assert(radio_catalog_store_open(&backup, backup_path));
    assert(radio_catalog_store_backup(&backup, &store));
    assert(radio_catalog_store_integrity_check(&backup));
    assert(radio_catalog_store_station_count(&backup) == 2U);
    radio_catalog_store_close(&backup);
    assert(unlink(backup_path) == 0);
    radio_catalog_store_close(&store);

    assert(radio_catalog_store_open(&store, path));
    assert(radio_catalog_store_station_count(&store) == 2U);
    assert(radio_catalog_store_get_meta(&store, "last_sync", &value));
    assert(value == 42);
    radio_catalog_store_close(&store);
    assert(unlink(path) == 0);
    radio_catalog_store_global_shutdown();
    free(page_cache);
    return 0;
}
