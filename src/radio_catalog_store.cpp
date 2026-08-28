// PS5 Radio - Disk-backed Radio Browser catalogue.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "radio_catalog_store.hpp"

#include <sqlite3.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define STORE_SCHEMA_VERSION 1
#define STORE_PAGE_SIZE 4096U
#define STORE_CACHE_KIB 32768

static bool g_external_page_cache;

bool radio_catalog_store_global_init(void *page_cache, size_t bytes)
{
    if (page_cache == nullptr || bytes < STORE_PAGE_SIZE)
        return false;
    if (sqlite3_shutdown() != SQLITE_OK)
        return false;
    int header_size = 0;
    int result = sqlite3_config(SQLITE_CONFIG_PCACHE_HDRSZ, &header_size);
    size_t slot_size = STORE_PAGE_SIZE;
    if (result == SQLITE_OK && header_size >= 0 && (size_t)header_size <= SIZE_MAX - slot_size)
    {
        slot_size += (size_t)header_size;
        slot_size = (slot_size + 7U) & ~(size_t)7U;
        const size_t available = bytes / slot_size;
        const int slots = available > INT_MAX ? INT_MAX : (int)available;
        if (slots == 0 || slot_size > INT_MAX)
            result = SQLITE_NOMEM;
        else
            result = sqlite3_config(SQLITE_CONFIG_PAGECACHE, page_cache, (int)slot_size, slots);
    }
    if (result == SQLITE_OK)
        result = sqlite3_initialize();
    g_external_page_cache = result == SQLITE_OK;
    if (!g_external_page_cache)
    {
        sqlite3_shutdown();
        sqlite3_config(SQLITE_CONFIG_PAGECACHE, nullptr, 0, 0);
        sqlite3_initialize();
    }
    return g_external_page_cache;
}

void radio_catalog_store_global_shutdown(void)
{
    sqlite3_shutdown();
    sqlite3_config(SQLITE_CONFIG_PAGECACHE, nullptr, 0, 0);
    g_external_page_cache = false;
}

static sqlite3 *database(radio_catalog_store_t *store)
{
    return (sqlite3 *)store->database;
}

static sqlite3_stmt *upsert_statement(radio_catalog_store_t *store)
{
    return (sqlite3_stmt *)store->upsert_station;
}

static bool set_error(radio_catalog_store_t *store, int result)
{
    store->error = result;
    return result == SQLITE_OK || result == SQLITE_DONE || result == SQLITE_ROW;
}

static bool execute(radio_catalog_store_t *store, const char *sql)
{
    char *message = nullptr;
    const int result = sqlite3_exec(database(store), sql, nullptr, nullptr, &message);
    sqlite3_free(message);
    return set_error(store, result);
}

static bool prepare(radio_catalog_store_t *store, const char *sql, sqlite3_stmt **statement)
{
    return set_error(store, sqlite3_prepare_v2(database(store), sql, -1, statement, nullptr));
}

static bool bind_text(sqlite3_stmt *statement, int index, const char *value)
{
    return sqlite3_bind_text(statement, index, value, -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

static void contains_nocase(sqlite3_context *context, int count, sqlite3_value **values)
{
    if (count != 2)
    {
        sqlite3_result_int(context, 0);
        return;
    }
    const char *text = (const char *)sqlite3_value_text(values[0]);
    const char *needle = (const char *)sqlite3_value_text(values[1]);
    if (text == nullptr || needle == nullptr)
    {
        sqlite3_result_int(context, 0);
        return;
    }
    const int needle_length = sqlite3_value_bytes(values[1]);
    if (needle_length == 0)
    {
        sqlite3_result_int(context, 1);
        return;
    }
    const int text_length = sqlite3_value_bytes(values[0]);
    for (int i = 0; i <= text_length - needle_length; ++i)
    {
        if (sqlite3_strnicmp(text + i, needle, needle_length) == 0)
        {
            sqlite3_result_int(context, 1);
            return;
        }
    }
    sqlite3_result_int(context, 0);
}

static void copy_text(char *destination, size_t capacity, const unsigned char *source)
{
    if (capacity == 0U)
        return;
    const char *text = source == nullptr ? "" : (const char *)source;
    size_t length = strlen(text);
    if (length >= capacity)
        length = capacity - 1U;
    memcpy(destination, text, length);
    destination[length] = '\0';
}

static void read_station(sqlite3_stmt *statement, radio_station_t *station)
{
    memset(station, 0, sizeof(*station));
    copy_text(station->uuid, sizeof(station->uuid), sqlite3_column_text(statement, 0));
    copy_text(station->name, sizeof(station->name), sqlite3_column_text(statement, 1));
    copy_text(station->url, sizeof(station->url), sqlite3_column_text(statement, 2));
    copy_text(station->country, sizeof(station->country), sqlite3_column_text(statement, 3));
    copy_text(station->country_code, sizeof(station->country_code),
              sqlite3_column_text(statement, 4));
    copy_text(station->state, sizeof(station->state), sqlite3_column_text(statement, 5));
    copy_text(station->language, sizeof(station->language), sqlite3_column_text(statement, 6));
    copy_text(station->tags, sizeof(station->tags), sqlite3_column_text(statement, 7));
    copy_text(station->codec, sizeof(station->codec), sqlite3_column_text(statement, 8));
    station->bitrate = (uint32_t)sqlite3_column_int64(statement, 9);
    station->votes = (uint32_t)sqlite3_column_int64(statement, 10);
    station->click_count = (uint32_t)sqlite3_column_int64(statement, 11);
    station->click_trend = (int32_t)sqlite3_column_int64(statement, 12);
    station->hls = (uint32_t)sqlite3_column_int64(statement, 13);
    station->popular_rank = station->trending_rank = station->voted_rank = 0U;
}

static bool bind_query(radio_catalog_store_t *store, sqlite3_stmt *statement,
                       const radio_catalog_query_t *query, bool favorites_only)
{
    static const radio_catalog_query_t empty_query = {};
    if (query == nullptr)
        query = &empty_query;
    const int results[] = {
        sqlite3_bind_int(statement, 1, favorites_only ? 1 : 0),
        sqlite3_bind_text(statement, 2, query->country_code, -1, SQLITE_TRANSIENT),
        sqlite3_bind_text(statement, 3, query->tag, -1, SQLITE_TRANSIENT),
        sqlite3_bind_text(statement, 4, query->language, -1, SQLITE_TRANSIENT),
        sqlite3_bind_int64(statement, 5, query->bitrate_min),
        sqlite3_bind_text(statement, 6, query->name, -1, SQLITE_TRANSIENT),
    };
    for (size_t i = 0U; i < sizeof(results) / sizeof(results[0]); ++i)
    {
        if (results[i] == SQLITE_OK)
            continue;
        store->error = results[i];
        return false;
    }
    return true;
}

#define STATION_COLUMNS                                                                            \
    "s.uuid,s.name,s.url,s.country,s.country_code,s.state,s.language,"                             \
    "s.tags,s.codec,s.bitrate,s.votes,s.click_count,s.click_trend,s.hls"
#define STATION_FILTER                                                                             \
    " FROM stations s WHERE "                                                                      \
    "(?1=0 OR EXISTS(SELECT 1 FROM favorites f WHERE f.uuid=s.uuid)) AND "                         \
    "(?2='' OR s.country_code=?2 COLLATE NOCASE) AND "                                             \
    "(?3='' OR contains_nocase(s.tags,?3)) AND "                                                   \
    "(?4='' OR contains_nocase(s.language,?4)) AND "                                               \
    "(?5=0 OR s.bitrate>=?5) AND "                                                                 \
    "(?6='' OR contains_nocase(s.name,?6) OR "                                                     \
    "contains_nocase(s.tags,?6) OR contains_nocase(s.country,?6) OR "                              \
    "contains_nocase(s.state,?6) OR contains_nocase(s.language,?6))"

bool radio_catalog_store_open(radio_catalog_store_t *store, const char *path)
{
    if (store == nullptr || path == nullptr)
        return false;
    memset(store, 0, sizeof(*store));
    sqlite3 *db = nullptr;
    int result = sqlite3_open_v2(
        path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    store->database = db;
    store->error = result;
    if (result != SQLITE_OK)
    {
        radio_catalog_store_close(store);
        return false;
    }
    result =
        sqlite3_create_function_v2(db, "contains_nocase", 2, SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                   nullptr, contains_nocase, nullptr, nullptr, nullptr);
    if (result != SQLITE_OK)
    {
        store->error = result;
        radio_catalog_store_close(store);
        return false;
    }
    sqlite3_busy_timeout(db, 2000);
    if (!execute(store,
                 g_external_page_cache ? "PRAGMA cache_size=-32768" : "PRAGMA cache_size=-256"))
    {
        radio_catalog_store_close(store);
        return false;
    }
    if (!execute(store,
                 /* The PacBrew PS5 VFS can open the primary database, but its secondary
                  * rollback-journal xOpen and fdatasync paths are not safe through the
                  * current static SharpProspero link. The catalog is disposable server
                  * data, so keep rollback pages in RAM and avoid sync while committed
                  * database pages remain on disk. */
                 "PRAGMA journal_mode=MEMORY;"
                 "PRAGMA synchronous=OFF;"
                 "PRAGMA temp_store=MEMORY;"
                 "CREATE TABLE IF NOT EXISTS stations("
                 "uuid TEXT PRIMARY KEY NOT NULL,name TEXT NOT NULL,url TEXT NOT NULL,"
                 "country TEXT NOT NULL,country_code TEXT NOT NULL,state TEXT NOT NULL,"
                 "language TEXT NOT NULL,tags TEXT NOT NULL,codec TEXT NOT NULL,"
                 "bitrate INTEGER NOT NULL,votes INTEGER NOT NULL,"
                 "click_count INTEGER NOT NULL,click_trend INTEGER NOT NULL,"
                 "hls INTEGER NOT NULL,sync_id INTEGER NOT NULL) WITHOUT ROWID;"
                 "CREATE INDEX IF NOT EXISTS stations_clicks ON stations(click_count DESC);"
                 "CREATE INDEX IF NOT EXISTS stations_trend ON stations(click_trend DESC);"
                 "CREATE INDEX IF NOT EXISTS stations_votes ON stations(votes DESC);"
                 "CREATE INDEX IF NOT EXISTS stations_country ON stations(country_code);"
                 "CREATE TABLE IF NOT EXISTS favorites("
                 "uuid TEXT PRIMARY KEY NOT NULL) WITHOUT ROWID;"
                 "CREATE TABLE IF NOT EXISTS facets("
                 "kind INTEGER NOT NULL,value TEXT NOT NULL,label TEXT NOT NULL,"
                 "station_count INTEGER NOT NULL,position INTEGER NOT NULL,"
                 "PRIMARY KEY(kind,value)) WITHOUT ROWID;"
                 "CREATE INDEX IF NOT EXISTS facets_order ON facets(kind,position);"
                 "CREATE TABLE IF NOT EXISTS metadata("
                 "key TEXT PRIMARY KEY NOT NULL,value INTEGER NOT NULL) WITHOUT ROWID;"
                 "PRAGMA user_version=1;"))
    {
        radio_catalog_store_close(store);
        return false;
    }
    sqlite3_stmt *statement = nullptr;
    if (!prepare(store,
                 "INSERT INTO stations(uuid,name,url,country,country_code,state,language,"
                 "tags,codec,bitrate,votes,click_count,click_trend,hls,sync_id)"
                 "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(uuid) DO UPDATE SET "
                 "name=excluded.name,url=excluded.url,country=excluded.country,"
                 "country_code=excluded.country_code,state=excluded.state,"
                 "language=excluded.language,tags=excluded.tags,codec=excluded.codec,"
                 "bitrate=excluded.bitrate,votes=excluded.votes,"
                 "click_count=excluded.click_count,click_trend=excluded.click_trend,"
                 "hls=excluded.hls,sync_id=excluded.sync_id",
                 &statement))
    {
        radio_catalog_store_close(store);
        return false;
    }
    store->upsert_station = statement;
    return true;
}

void radio_catalog_store_close(radio_catalog_store_t *store)
{
    if (store == nullptr)
        return;
    if (store->upsert_station != nullptr)
        sqlite3_finalize(upsert_statement(store));
    if (store->database != nullptr)
        sqlite3_close_v2(database(store));
    store->database = nullptr;
    store->upsert_station = nullptr;
}

int radio_catalog_store_error(const radio_catalog_store_t *store)
{
    return store == nullptr ? SQLITE_MISUSE : store->error;
}

bool radio_catalog_store_integrity_check(radio_catalog_store_t *store)
{
    if (store == nullptr || store->database == nullptr)
        return false;
    sqlite3_stmt *statement = nullptr;
    if (!prepare(store, "PRAGMA quick_check(1)", &statement))
        return false;
    const int result = sqlite3_step(statement);
    const unsigned char *text = result == SQLITE_ROW ? sqlite3_column_text(statement, 0) : nullptr;
    const bool ok = text != nullptr && strcmp((const char *)text, "ok") == 0;
    store->error = ok ? SQLITE_OK : (result == SQLITE_ROW ? SQLITE_CORRUPT : result);
    sqlite3_finalize(statement);
    return ok;
}

bool radio_catalog_store_backup(radio_catalog_store_t *destination, radio_catalog_store_t *source)
{
    if (destination == nullptr || source == nullptr || destination->database == nullptr ||
        source->database == nullptr)
        return false;
    sqlite3_backup *backup =
        sqlite3_backup_init(database(destination), "main", database(source), "main");
    if (backup == nullptr)
    {
        destination->error = sqlite3_errcode(database(destination));
        return false;
    }
    int result = sqlite3_backup_step(backup, -1);
    const int finish = sqlite3_backup_finish(backup);
    if (result == SQLITE_DONE)
        result = finish;
    return set_error(destination, result);
}

bool radio_catalog_store_begin(radio_catalog_store_t *store)
{
    return store != nullptr && execute(store, "BEGIN IMMEDIATE");
}

bool radio_catalog_store_commit(radio_catalog_store_t *store)
{
    return store != nullptr && execute(store, "COMMIT");
}

void radio_catalog_store_rollback(radio_catalog_store_t *store)
{
    if (store != nullptr && store->database != nullptr)
        execute(store, "ROLLBACK");
}

bool radio_catalog_store_upsert_station(radio_catalog_store_t *store,
                                        const radio_station_t *station, uint64_t sync_id)
{
    if (store == nullptr || station == nullptr || store->upsert_station == nullptr)
        return false;
    sqlite3_stmt *statement = upsert_statement(store);
    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);
    bool bound =
        bind_text(statement, 1, station->uuid) && bind_text(statement, 2, station->name) &&
        bind_text(statement, 3, station->url) && bind_text(statement, 4, station->country) &&
        bind_text(statement, 5, station->country_code) && bind_text(statement, 6, station->state) &&
        bind_text(statement, 7, station->language) && bind_text(statement, 8, station->tags) &&
        bind_text(statement, 9, station->codec) &&
        sqlite3_bind_int64(statement, 10, station->bitrate) == SQLITE_OK &&
        sqlite3_bind_int64(statement, 11, station->votes) == SQLITE_OK &&
        sqlite3_bind_int64(statement, 12, station->click_count) == SQLITE_OK &&
        sqlite3_bind_int64(statement, 13, station->click_trend) == SQLITE_OK &&
        sqlite3_bind_int64(statement, 14, station->hls) == SQLITE_OK &&
        sqlite3_bind_int64(statement, 15, (sqlite3_int64)sync_id) == SQLITE_OK;
    if (!bound)
        return set_error(store, sqlite3_errcode(database(store)));
    return set_error(store, sqlite3_step(statement));
}

bool radio_catalog_store_prune_stations(radio_catalog_store_t *store, uint64_t sync_id,
                                        size_t limit, size_t *removed)
{
    if (store == nullptr || limit == 0U || limit > INT_MAX)
        return false;
    sqlite3_stmt *statement = nullptr;
    if (!prepare(store,
                 "DELETE FROM stations WHERE uuid IN (SELECT uuid FROM stations "
                 "WHERE sync_id<>? LIMIT ?)",
                 &statement))
        return false;
    sqlite3_bind_int64(statement, 1, (sqlite3_int64)sync_id);
    sqlite3_bind_int(statement, 2, (int)limit);
    const bool result = set_error(store, sqlite3_step(statement));
    if (result && removed != nullptr)
        *removed = (size_t)sqlite3_changes(database(store));
    sqlite3_finalize(statement);
    return result;
}

size_t radio_catalog_store_station_count(radio_catalog_store_t *store)
{
    if (store == nullptr)
        return 0U;
    sqlite3_stmt *statement = nullptr;
    if (!prepare(store, "SELECT count(*) FROM stations", &statement))
        return 0U;
    size_t count = 0U;
    if (sqlite3_step(statement) == SQLITE_ROW)
    {
        const sqlite3_int64 value = sqlite3_column_int64(statement, 0);
        if (value > 0 && (uint64_t)value <= SIZE_MAX)
            count = (size_t)value;
        store->error = SQLITE_OK;
    }
    else
        store->error = sqlite3_errcode(database(store));
    sqlite3_finalize(statement);
    return count;
}

size_t radio_catalog_store_load_stations(radio_catalog_store_t *store, radio_station_t *stations,
                                         size_t capacity)
{
    if (store == nullptr || stations == nullptr || capacity == 0U)
        return 0U;
    sqlite3_stmt *statement = nullptr;
    if (!prepare(store,
                 "SELECT uuid,name,url,country,country_code,state,language,tags,codec,"
                 "bitrate,votes,click_count,click_trend,hls FROM stations "
                 "ORDER BY click_count DESC,uuid",
                 &statement))
        return 0U;
    size_t count = 0U;
    int result = SQLITE_ROW;
    while (count < capacity && (result = sqlite3_step(statement)) == SQLITE_ROW)
    {
        read_station(statement, &stations[count++]);
    }
    store->error = result == SQLITE_DONE || count == capacity ? SQLITE_OK : result;
    sqlite3_finalize(statement);
    return count;
}

size_t radio_catalog_store_query_count(radio_catalog_store_t *store,
                                       const radio_catalog_query_t *query, bool favorites_only)
{
    if (store == nullptr)
        return 0U;
    sqlite3_stmt *statement = nullptr;
    if (!prepare(store, "SELECT count(*)" STATION_FILTER, &statement))
        return 0U;
    if (!bind_query(store, statement, query, favorites_only))
    {
        sqlite3_finalize(statement);
        return 0U;
    }
    size_t count = 0U;
    const int result = sqlite3_step(statement);
    if (result == SQLITE_ROW)
    {
        const sqlite3_int64 value = sqlite3_column_int64(statement, 0);
        if (value > 0 && (uint64_t)value <= SIZE_MAX)
            count = (size_t)value;
        store->error = SQLITE_OK;
    }
    else
        store->error = result;
    sqlite3_finalize(statement);
    return count;
}

size_t radio_catalog_store_query_stations(radio_catalog_store_t *store,
                                          const radio_catalog_query_t *query,
                                          radio_catalog_order_t order, bool favorites_only,
                                          size_t offset, radio_station_t *stations, size_t capacity)
{
    if (store == nullptr || stations == nullptr || capacity == 0U || capacity > INT_MAX ||
        offset > INT64_MAX)
        return 0U;
    const char *ordering = "s.click_count DESC,s.uuid";
    if (order == RADIO_CATALOG_ORDER_TRENDING)
        ordering = "s.click_trend DESC,s.click_count DESC,s.uuid";
    else if (order == RADIO_CATALOG_ORDER_VOTED)
        ordering = "s.votes DESC,s.click_count DESC,s.uuid";
    else if (order == RADIO_CATALOG_ORDER_NAME)
        ordering = "s.name COLLATE NOCASE,s.uuid";
    static const char prefix[] = "SELECT " STATION_COLUMNS STATION_FILTER " ORDER BY ";
    static const char suffix[] = " LIMIT ?7 OFFSET ?8";
    char sql[1400];
    const int sql_length = snprintf(sql, sizeof(sql), "%s%s%s", prefix, ordering, suffix);
    if (sql_length < 0 || (size_t)sql_length >= sizeof(sql))
    {
        store->error = SQLITE_TOOBIG;
        return 0U;
    }
    sqlite3_stmt *statement = nullptr;
    if (!prepare(store, sql, &statement) || !bind_query(store, statement, query, favorites_only) ||
        sqlite3_bind_int(statement, 7, (int)capacity) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 8, (sqlite3_int64)offset) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        return 0U;
    }
    size_t count = 0U;
    int result = SQLITE_ROW;
    while (count < capacity && (result = sqlite3_step(statement)) == SQLITE_ROW)
        read_station(statement, &stations[count++]);
    store->error = result == SQLITE_DONE || count == capacity ? SQLITE_OK : result;
    sqlite3_finalize(statement);
    return count;
}

bool radio_catalog_store_set_favorite(radio_catalog_store_t *store, const char *uuid, bool favorite)
{
    if (store == nullptr || uuid == nullptr)
        return false;
    sqlite3_stmt *statement = nullptr;
    const char *sql = favorite ? "INSERT OR IGNORE INTO favorites(uuid) VALUES(?)"
                               : "DELETE FROM favorites WHERE uuid=?";
    if (!prepare(store, sql, &statement))
        return false;
    bind_text(statement, 1, uuid);
    const bool result = set_error(store, sqlite3_step(statement));
    sqlite3_finalize(statement);
    return result;
}

size_t radio_catalog_store_favorite_count(radio_catalog_store_t *store)
{
    if (store == nullptr)
        return 0U;
    sqlite3_stmt *statement = nullptr;
    if (!prepare(store, "SELECT count(*) FROM favorites", &statement))
        return 0U;
    size_t count = 0U;
    if (sqlite3_step(statement) == SQLITE_ROW)
    {
        const sqlite3_int64 value = sqlite3_column_int64(statement, 0);
        if (value > 0 && (uint64_t)value <= SIZE_MAX)
            count = (size_t)value;
        store->error = SQLITE_OK;
    }
    else
        store->error = sqlite3_errcode(database(store));
    sqlite3_finalize(statement);
    return count;
}

size_t radio_catalog_store_load_favorites(radio_catalog_store_t *store, char (*uuids)[40],
                                          size_t capacity)
{
    if (store == nullptr || uuids == nullptr || capacity == 0U)
        return 0U;
    sqlite3_stmt *statement = nullptr;
    if (!prepare(store, "SELECT uuid FROM favorites ORDER BY uuid", &statement))
        return 0U;
    size_t count = 0U;
    int result = SQLITE_ROW;
    while (count < capacity && (result = sqlite3_step(statement)) == SQLITE_ROW)
        copy_text(uuids[count++], 40U, sqlite3_column_text(statement, 0));
    store->error = result == SQLITE_DONE || count == capacity ? SQLITE_OK : result;
    sqlite3_finalize(statement);
    return count;
}

bool radio_catalog_store_replace_facets(radio_catalog_store_t *store, radio_facet_kind_t kind,
                                        const radio_facet_t *facets, size_t count)
{
    if (store == nullptr || (facets == nullptr && count != 0U))
        return false;
    if (!radio_catalog_store_begin(store))
        return false;
    sqlite3_stmt *remove = nullptr;
    sqlite3_stmt *insert = nullptr;
    bool ok = prepare(store, "DELETE FROM facets WHERE kind=?", &remove) &&
              sqlite3_bind_int(remove, 1, (int)kind) == SQLITE_OK &&
              set_error(store, sqlite3_step(remove));
    sqlite3_finalize(remove);
    if (ok)
        ok = prepare(
            store, "INSERT INTO facets(kind,value,label,station_count,position) VALUES(?,?,?,?,?)",
            &insert);
    for (size_t i = 0U; ok && i < count; ++i)
    {
        sqlite3_reset(insert);
        sqlite3_clear_bindings(insert);
        ok = sqlite3_bind_int(insert, 1, (int)kind) == SQLITE_OK &&
             bind_text(insert, 2, facets[i].value) && bind_text(insert, 3, facets[i].label) &&
             sqlite3_bind_int64(insert, 4, facets[i].station_count) == SQLITE_OK &&
             sqlite3_bind_int64(insert, 5, (sqlite3_int64)i) == SQLITE_OK &&
             set_error(store, sqlite3_step(insert));
    }
    sqlite3_finalize(insert);
    if (ok)
        ok = radio_catalog_store_commit(store);
    if (!ok)
        radio_catalog_store_rollback(store);
    return ok;
}

size_t radio_catalog_store_load_facets(radio_catalog_store_t *store, radio_facet_kind_t kind,
                                       radio_facet_t *facets, size_t capacity)
{
    if (store == nullptr || facets == nullptr || capacity == 0U)
        return 0U;
    sqlite3_stmt *statement = nullptr;
    if (!prepare(store,
                 "SELECT value,label,station_count FROM facets WHERE kind=? ORDER BY position",
                 &statement))
        return 0U;
    sqlite3_bind_int(statement, 1, (int)kind);
    size_t count = 0U;
    int result = SQLITE_ROW;
    while (count < capacity && (result = sqlite3_step(statement)) == SQLITE_ROW)
    {
        copy_text(facets[count].value, sizeof(facets[count].value),
                  sqlite3_column_text(statement, 0));
        copy_text(facets[count].label, sizeof(facets[count].label),
                  sqlite3_column_text(statement, 1));
        facets[count].station_count = (uint32_t)sqlite3_column_int64(statement, 2);
        ++count;
    }
    store->error = result == SQLITE_DONE || count == capacity ? SQLITE_OK : result;
    sqlite3_finalize(statement);
    return count;
}

bool radio_catalog_store_set_meta(radio_catalog_store_t *store, const char *key, int64_t value)
{
    if (store == nullptr || key == nullptr)
        return false;
    sqlite3_stmt *statement = nullptr;
    if (!prepare(store,
                 "INSERT INTO metadata(key,value) VALUES(?,?) ON CONFLICT(key) "
                 "DO UPDATE SET value=excluded.value",
                 &statement))
        return false;
    bind_text(statement, 1, key);
    sqlite3_bind_int64(statement, 2, (sqlite3_int64)value);
    const bool result = set_error(store, sqlite3_step(statement));
    sqlite3_finalize(statement);
    return result;
}

bool radio_catalog_store_get_meta(radio_catalog_store_t *store, const char *key, int64_t *value)
{
    if (store == nullptr || key == nullptr || value == nullptr)
        return false;
    sqlite3_stmt *statement = nullptr;
    if (!prepare(store, "SELECT value FROM metadata WHERE key=?", &statement))
        return false;
    bind_text(statement, 1, key);
    const int result = sqlite3_step(statement);
    const bool found = result == SQLITE_ROW;
    if (found)
        *value = (int64_t)sqlite3_column_int64(statement, 0);
    store->error = found ? SQLITE_OK : result;
    sqlite3_finalize(statement);
    return found;
}
