// PSRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "radio_service.hpp"

#include "aac_timing.hpp"
#include "flac_decoder.hpp"
#include "icy_metadata.hpp"
#include "mp3_header.hpp"
#include "ogg_opus.hpp"
#include "ogg_stream.hpp"
#include "opus_decoder.hpp"
#include "opus_pcm.hpp"
#include "pcm_queue.hpp"
#include "playback_retry.hpp"
#include "radio_hls.hpp"
#include "radio_catalog_store.hpp"
#include "radio_playlist.hpp"
#include "radio_ts_aac.hpp"
#include "vorbis_decoder.hpp"

#include "SDL.h"

#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define LEGACY_CACHE_MAGIC UINT32_C(0x52424331)
#define FAVORITES_MAGIC UINT32_C(0x52424631)
#define LEGACY_CATALOG_CACHE_VERSION 8U
#define FAVORITES_VERSION 3U
#define FAVORITES_FIXED_VERSION 2U
#define FAVORITES_LEGACY_VERSION 1U
#define LEGACY_CATALOG_CAPACITY 480U
#define FAVORITES_LEGACY_CAPACITY 100U
#define LEGACY_CACHE_PATH "/download0/radio-browser-cache.bin"
#define FAVORITES_PATH "/download0/radio-browser-favorites.bin"
#define FAVORITES_TEMP_PATH "/download0/radio-browser-favorites.tmp"
#define CATALOG_DATABASE_PATH "/download0/radio-browser.sqlite3"
#define CATALOG_STAGING_PATH "/download0/radio-browser-next.sqlite3"
#define CATALOG_PAGE_LIMIT 10000U
#define CATALOG_WRITE_BATCH 256U
#define CATALOG_VIEW_CAPACITY 16U
#define CATALOG_PRUNE_BATCH 128U
#define CATALOG_RESULT_LIMIT 200000U
#define CATALOG_MAX_MIRRORS 16U
#define CATALOG_MIRROR_LENGTH 96U
#define CATALOG_API_RETRY_ROUNDS 3U
#define CATALOG_API_RETRY_DELAY_MS 200U
#define CATALOG_SYNC_META "catalog_sync"
#define CATALOG_SYNC_TIME_META "catalog_sync_time"
#define CATALOG_LAST_ERROR_META "catalog_last_error"
#define FAVORITES_MIGRATED_META "favorites_migrated"
#define CATALOG_REFRESH_SECONDS (12 * 60 * 60)
#define CATALOG_THREAD_STACK_SIZE (4U * 1024U * 1024U)
#define CATALOG_PAGE_CACHE_SIZE (64U * 1024U * 1024U)
#define USER_AGENT "PSRadio/0.2.0 (+https://www.radio-browser.info/)"
#define JSON_CAPACITY (16U * 1024U * 1024U)
#define STREAM_BUFFER_SIZE (64U * 1024U)
#define PCM_BUFFER_SIZE (2048U * 2U * 2U)
#define OPUS_PCM_BUFFER_SIZE (5760U * 2U * sizeof(int16_t))
#define VORBIS_STREAM_BUFFER_SIZE (256U * 1024U)
#define VORBIS_NETWORK_CHUNK_SIZE (16U * 1024U)
#define VORBIS_PCM_BUFFER_SAMPLES (VORBIS_DECODER_MAX_FRAME_FRAMES * 2U)
#define VORBIS_CHAIN_END 2
#define OGG_STREAM_PLAYBACK_ERROR_BASE (-3400)
#define FLAC_PCM_BUFFER_SAMPLES (FLAC_DECODER_READ_FRAMES * 2U)
#define OPEN_READ_ONLY 0x0000
#define HTTP_VERSION_11 2
#define HTTP_METHOD_GET 0
#define HTTP_HEADER_OVERWRITE 0U
#define AUDIODEC_MP3 2U
#define AUDIODEC_AAC 3U
#define AUDIODEC_WORD_S16 1
#define AUDIO_OUT_GRAIN 256U
#define AUDIO_OUT_RATE 48000U
#define AUDIO_OUT_STEREO_S16 1U
#define AUDIO_OUT_VOLUME_0DB 0x8000
#define AUDIO_QUEUE_BLOCKS 1500U
#define AUDIO_START_BLOCKS 563U
#define AUDIO_RESTART_BLOCKS 375U
#define AUDIO_WAIT_MS 20U
#define PLAYLIST_BUFFER_SIZE (64U * 1024U)
#define PLAYLIST_REDIRECT_LIMIT 3U
#define STREAM_OPEN_DIRECT 0
#define STREAM_OPEN_HLS 1
#define HLS_PLAYLIST_BUFFER_SIZE (128U * 1024U)
#define HLS_NETWORK_BUFFER_SIZE (16U * 1024U)
#define HLS_OUTPUT_BUFFER_SIZE (64U * 1024U)
#define HLS_MASTER_LIMIT 2U
#define HLS_LIVE_EDGE_SEGMENTS 2U
#define HLS_ERROR_PLAYLIST (-2101)
#define HLS_ERROR_TRANSPORT (-2102)
#define STREAM_READ_DISCONTINUITY (-4095)
#define OPUS_RETRYABLE_ERROR (-502)
#define OGG_PROBE_BUFFER_SIZE 4096U

struct file_header_t
{
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t checksum;
};

struct cache_file_t
{
    file_header_t header;
    radio_station_t stations[LEGACY_CATALOG_CAPACITY];
};

struct favorites_file_t
{
    file_header_t header;
    char uuids[LEGACY_CATALOG_CAPACITY][40];
};

struct favorites_legacy_file_t
{
    file_header_t header;
    char uuids[FAVORITES_LEGACY_CAPACITY][40];
};

struct sce_audiodec_au_info_t
{
    uint32_t size;
    void *address;
    uint32_t length;
};

struct sce_audiodec_pcm_item_t
{
    uint32_t size;
    void *address;
    uint32_t length;
};

struct sce_audiodec_ctrl_t
{
    void *param;
    void *stream_info;
    sce_audiodec_au_info_t *au_info;
    sce_audiodec_pcm_item_t *pcm_item;
};

struct sce_audiodec_param_mp3_t
{
    uint32_t size;
    int32_t word_size;
};

struct sce_audiodec_mp3_info_t
{
    uint32_t size;
    uint32_t header;
    uint8_t crc;
    uint8_t mode;
    uint8_t mode_extension;
    uint8_t copyright;
    uint8_t original;
    uint8_t emphasis;
    uint8_t reserved[2];
    int32_t result;
};

struct sce_audiodec_param_aac_t
{
    uint32_t size;
    int32_t word_size;
    uint32_t config_number;
    uint32_t sampling_frequency_index;
    uint32_t max_channels;
    uint32_t enable_he_aac;
};

struct sce_audiodec_aac_info_t
{
    uint32_t size;
    uint32_t sampling_frequency;
    uint32_t channel_count;
    uint32_t he_aac;
    int32_t result;
};

static_assert(sizeof(sce_audiodec_au_info_t) == 24U, "SceAudiodecAuInfo ABI mismatch");
static_assert(sizeof(sce_audiodec_pcm_item_t) == 24U, "SceAudiodecPcmItem ABI mismatch");
static_assert(sizeof(sce_audiodec_ctrl_t) == 32U, "SceAudiodecCtrl ABI mismatch");
static_assert(sizeof(sce_audiodec_param_mp3_t) == 8U, "SceAudiodecParamMp3 ABI mismatch");
static_assert(sizeof(sce_audiodec_mp3_info_t) == 20U, "SceAudiodecMp3Info ABI mismatch");

struct audio_sink_t
{
    int handle;
    uint32_t input_rate;
    uint32_t channels;
    uint64_t input_index;
    uint64_t next_output_position;
    int16_t previous_left;
    int16_t previous_right;
    bool have_previous;
    unsigned pending;
    int16_t block[AUDIO_OUT_GRAIN * 2U];
    int16_t *queue_blocks;
    pcm_queue_state_t queue;
    SDL_mutex *mutex;
    SDL_cond *can_read;
    SDL_cond *can_write;
    SDL_Thread *thread;
    bool input_finished;
    bool cancel;
    uint64_t output_frames;
    int output_result;
};

struct catalog_task_t
{
    bool full_sync;
    radio_catalog_query_t query;
};

struct catalog_http_client_t
{
    int connection;
    unsigned mirror;
};

extern "C"
{
    extern int sceKernelOpen(const char *path, int flags, uint16_t mode);
    extern int sceKernelClose(int descriptor);
    extern int64_t sceKernelRead(int descriptor, void *buffer, size_t length);
    extern int scePthreadCreate(void **thread, const void *attributes, void *(*entry)(void *),
                                void *argument, const char *name);
    extern int scePthreadDetach(void *thread);

    extern int sceNetPoolCreate(const char *name, int size, int flags);
    extern int sceNetPoolDestroy(int mem_id);
    extern int sceSslInit(size_t pool_size);
    extern int sceSslTerm(int ssl_context_id);
    extern int sceHttpInit(int net_mem_id, int ssl_context_id, size_t pool_size);
    extern int sceHttpTerm(int http_context_id);
    extern int sceHttpCreateTemplate(int http_context_id, const char *user_agent, int version,
                                     int auto_proxy);
    extern int sceHttpDeleteTemplate(int template_id);
    extern int sceHttpCreateConnectionWithURL(int template_id, const char *url, int keep_alive);
    extern int sceHttpDeleteConnection(int connection_id);
    extern int sceHttpCreateRequestWithURL(int connection_id, int method, const char *url,
                                           uint64_t content_length);
    extern int sceHttpDeleteRequest(int request_id);
    extern int sceHttpAddRequestHeader(int request_id, const char *name, const char *value,
                                       uint32_t mode);
    extern int sceHttpSetAutoRedirect(int id, int enabled);
    extern int sceHttpSetConnectTimeOut(int id, uint32_t usec);
    extern int sceHttpSetRecvTimeOut(int id, uint32_t usec);
    extern int sceHttpSetSendTimeOut(int id, uint32_t usec);
    extern int sceHttpSetResolveTimeOut(int id, uint32_t usec);
    extern int sceHttpSendRequest(int request_id, const void *data, size_t size);
    extern int sceHttpGetStatusCode(int request_id, int *status_code);
    extern int sceHttpGetAllResponseHeaders(int request_id, char **headers, size_t *size);
    extern int sceHttpReadData(int request_id, void *data, size_t size);
    extern int sceHttpAbortRequest(int request_id);

    extern int sceSysmoduleLoadModule(uint16_t id);
    extern int sceSysmoduleUnloadModule(uint16_t id);
    extern int sceAudiodecInitLibrary(uint32_t codec_type);
    extern int sceAudiodecTermLibrary(uint32_t codec_type);
    extern int sceAudiodecCreateDecoder(sce_audiodec_ctrl_t *ctrl, uint32_t codec_type);
    extern int sceAudiodecDeleteDecoder(int handle);
    extern int sceAudiodecDecode(int handle, sce_audiodec_ctrl_t *ctrl);
    extern int sceAudioOutInit(void);
    extern int sceAudioOutOpen(int user_id, int type, int index, uint32_t length,
                               uint32_t frequency, uint32_t format);
    extern int sceAudioOutClose(int handle);
    extern int sceAudioOutOutput(int handle, const void *samples);
    extern int sceAudioOutSetVolume(int handle, int flags, const int *volumes);
}

static radio_station_t g_stations[CATALOG_VIEW_CAPACITY];
static radio_station_t g_playing_station;
static bool g_have_playing_station;
static void *g_catalog_page_cache;
static char (*g_favorites)[40];
static unsigned g_station_count;
static unsigned g_favorite_count;
static unsigned g_favorite_capacity;
static radio_facet_t g_facets[3][RADIO_MAX_FACETS];
static unsigned g_facet_count[3];
static char g_mirrors[CATALOG_MAX_MIRRORS][CATALOG_MIRROR_LENGTH];
static unsigned g_mirror_count;
static unsigned g_mirror_index;
static radio_catalog_store_t g_catalog_store;
static SDL_mutex *g_store_mutex;
static catalog_task_t g_pending_task;
static bool g_pending_task_ready;
static radio_service_status_t g_status;
static SDL_mutex *g_state_mutex;
static SDL_mutex *g_request_mutex;
static SDL_atomic_t g_refresh_running;
static SDL_atomic_t g_playback_running;
static SDL_atomic_t g_stop_playback;
static SDL_atomic_t g_shutting_down;
static int g_playback_request = -1;
static int g_net_pool = -1;
static int g_ssl_context = -1;
static int g_http_context = -1;
static int g_http_template = -1;

static void playback_request_set(int request);
static void playback_request_clear(int request);

static uint32_t checksum(const void *data, size_t size)
{
    const auto *bytes = static_cast<const uint8_t *>(data);
    uint32_t value = UINT32_C(2166136261);
    for (size_t i = 0; i < size; ++i)
    {
        value = (value ^ bytes[i]) * UINT32_C(16777619);
    }
    return value;
}

static bool read_exact(const char *path, void *data, size_t size)
{
    const int fd = sceKernelOpen(path, OPEN_READ_ONLY, 0);
    if (fd < 0)
        return false;

    size_t done = 0;
    while (done < size)
    {
        const int64_t count = sceKernelRead(fd, (uint8_t *)data + done, size - done);
        if (count <= 0)
            break;
        done += (size_t)count;
    }
    sceKernelClose(fd);
    return done == size;
}

static bool favorite_unlocked(const char *uuid)
{
    for (unsigned i = 0; i < g_favorite_count; ++i)
    {
        if (strcmp(g_favorites[i], uuid) == 0)
            return true;
    }
    return false;
}

static bool ensure_favorite_capacity(unsigned capacity)
{
    if (capacity <= g_favorite_capacity)
        return true;
    unsigned next = g_favorite_capacity == 0U ? 32U : g_favorite_capacity;
    while (next < capacity && next <= UINT_MAX / 2U)
        next *= 2U;
    if (next < capacity)
        next = capacity;
    auto *resized = static_cast<decltype(g_favorites)>(
        realloc(g_favorites, (size_t)next * sizeof(g_favorites[0])));
    if (resized == nullptr)
        return false;
    g_favorites = resized;
    g_favorite_capacity = next;
    return true;
}

static bool load_favorites_file(void)
{
    FILE *file = fopen(FAVORITES_PATH, "rb");
    if (file == nullptr)
        return false;
    file_header_t header = {};
    bool ok = fread(&header, 1U, sizeof(header), file) == sizeof(header) &&
              header.magic == FAVORITES_MAGIC && header.version == FAVORITES_VERSION &&
              header.count <= 100000U && ensure_favorite_capacity(header.count);
    const size_t bytes = (size_t)header.count * sizeof(g_favorites[0]);
    if (ok && bytes != 0U)
        ok = fread(g_favorites, 1U, bytes, file) == bytes;
    if (ok)
        ok = checksum(g_favorites, bytes) == header.checksum;
    fclose(file);
    g_favorite_count = ok ? header.count : 0U;
    return ok;
}

static bool save_favorites_file(void)
{
    SDL_LockMutex(g_state_mutex);
    const unsigned count = g_favorite_count;
    const size_t bytes = (size_t)count * sizeof(g_favorites[0]);
    auto *favorites = bytes == 0U ? nullptr : static_cast<decltype(g_favorites)>(malloc(bytes));
    if (favorites != nullptr)
        memcpy(favorites, g_favorites, bytes);
    SDL_UnlockMutex(g_state_mutex);
    if (bytes != 0U && favorites == nullptr)
        return false;

    file_header_t header = {FAVORITES_MAGIC, FAVORITES_VERSION, count, checksum(favorites, bytes)};
    FILE *file = fopen(FAVORITES_TEMP_PATH, "wb");
    bool ok = file != nullptr && fwrite(&header, 1U, sizeof(header), file) == sizeof(header) &&
              (bytes == 0U || fwrite(favorites, 1U, bytes, file) == bytes) && fflush(file) == 0;
    if (file != nullptr && fclose(file) != 0)
        ok = false;
    free(favorites);
    if (ok)
        ok = rename(FAVORITES_TEMP_PATH, FAVORITES_PATH) == 0;
    if (!ok)
        unlink(FAVORITES_TEMP_PATH);
    return ok;
}

static void migrate_legacy_favorites(void)
{
    int64_t migrated = 0;
    if (radio_catalog_store_get_meta(&g_catalog_store, FAVORITES_MIGRATED_META, &migrated) &&
        migrated != 0)
        return;

    favorites_file_t file;
    unsigned count = 0U;
    const char (*uuids)[40] = nullptr;
    if (read_exact(FAVORITES_PATH, &file, sizeof(file)) && file.header.magic == FAVORITES_MAGIC &&
        file.header.version == FAVORITES_FIXED_VERSION &&
        file.header.count <= LEGACY_CATALOG_CAPACITY &&
        file.header.checksum == checksum(file.uuids, sizeof(file.uuids)))
    {
        count = file.header.count;
        uuids = file.uuids;
    }
    else
    {
        favorites_legacy_file_t legacy;
        if (read_exact(FAVORITES_PATH, &legacy, sizeof(legacy)) &&
            legacy.header.magic == FAVORITES_MAGIC &&
            legacy.header.version == FAVORITES_LEGACY_VERSION &&
            legacy.header.count <= FAVORITES_LEGACY_CAPACITY &&
            legacy.header.checksum == checksum(legacy.uuids, sizeof(legacy.uuids)))
        {
            for (unsigned i = 0U; i < legacy.header.count; ++i)
                radio_catalog_store_set_favorite(&g_catalog_store, legacy.uuids[i], true);
        }
        radio_catalog_store_set_meta(&g_catalog_store, FAVORITES_MIGRATED_META, 1);
        return;
    }
    for (unsigned i = 0U; i < count; ++i)
        radio_catalog_store_set_favorite(&g_catalog_store, uuids[i], true);
    radio_catalog_store_set_meta(&g_catalog_store, FAVORITES_MIGRATED_META, 1);
}

static bool load_favorites(void)
{
    if (load_favorites_file())
    {
        for (unsigned i = 0U; i < g_favorite_count; ++i)
            radio_catalog_store_set_favorite(&g_catalog_store, g_favorites[i], true);
        return true;
    }
    migrate_legacy_favorites();
    const size_t count = radio_catalog_store_favorite_count(&g_catalog_store);
    if (count > UINT_MAX || !ensure_favorite_capacity((unsigned)count))
        return false;
    g_favorite_count = (unsigned)radio_catalog_store_load_favorites(&g_catalog_store, g_favorites,
                                                                    g_favorite_capacity);
    const bool loaded = g_favorite_count == count;
    if (loaded)
        save_favorites_file();
    return loaded;
}

static int network_init(void)
{
    g_net_pool = sceNetPoolCreate("ps5_radio_http", 0x4000, 0);
    if (g_net_pool < 0)
        return g_net_pool;
    g_ssl_context = sceSslInit(304U * 1024U);
    if (g_ssl_context < 0)
        return g_ssl_context;
    g_http_context = sceHttpInit(g_net_pool, g_ssl_context, 0x10000);
    if (g_http_context < 0)
        return g_http_context;
    g_http_template = sceHttpCreateTemplate(g_http_context, USER_AGENT, HTTP_VERSION_11, 1);
    if (g_http_template < 0)
        return g_http_template;
    sceHttpSetAutoRedirect(g_http_template, 1);
    sceHttpSetResolveTimeOut(g_http_template, 5000000U);
    sceHttpSetConnectTimeOut(g_http_template, 5000000U);
    sceHttpSetSendTimeOut(g_http_template, 5000000U);
    sceHttpSetRecvTimeOut(g_http_template, 5000000U);
    return 0;
}

static void network_shutdown(void)
{
    if (g_http_template >= 0)
        sceHttpDeleteTemplate(g_http_template);
    if (g_http_context >= 0)
        sceHttpTerm(g_http_context);
    if (g_ssl_context >= 0)
        sceSslTerm(g_ssl_context);
    if (g_net_pool >= 0)
        sceNetPoolDestroy(g_net_pool);
    g_http_template = -1;
    g_http_context = -1;
    g_ssl_context = -1;
    g_net_pool = -1;
}

static int http_send_on_connection(int connection, const char *url, bool streaming,
                                   const char *codec, int *request, unsigned *failure_stage)
{
    if (failure_stage != nullptr)
        *failure_stage = 2U;
    *request = sceHttpCreateRequestWithURL(connection, HTTP_METHOD_GET, url, 0);
    if (*request < 0)
    {
        return *request;
    }
    sceHttpSetAutoRedirect(*request, 1);
    sceHttpSetResolveTimeOut(*request, 5000000U);
    sceHttpSetConnectTimeOut(*request, 5000000U);
    sceHttpSetSendTimeOut(*request, 5000000U);
    sceHttpSetRecvTimeOut(*request, streaming ? 2000000U : 5000000U);
    const char *accept = "application/json";
    if (streaming)
    {
        if (codec != nullptr &&
            (strcasecmp(codec, "OPUS") == 0 || strcasecmp(codec, "VORBIS") == 0 ||
             strcasecmp(codec, "OGG") == 0 || strcasecmp(codec, "FLAC") == 0))
            accept = "audio/ogg, audio/opus, audio/flac, audio/x-flac, */*";
        else if (codec != nullptr && strcasecmp(codec, "MP3") == 0)
            accept = "audio/mpeg, audio/mp3, */*";
        else
            accept = "application/vnd.apple.mpegurl, application/x-mpegURL, "
                     "video/mp2t, audio/aac, audio/aacp, */*";
    }
    sceHttpAddRequestHeader(*request, "Accept", accept, HTTP_HEADER_OVERWRITE);
    if (streaming)
    {
        sceHttpAddRequestHeader(*request, "Icy-MetaData", "0", HTTP_HEADER_OVERWRITE);
        playback_request_set(*request);
    }
    if (failure_stage != nullptr)
        *failure_stage = 3U;
    int result = sceHttpSendRequest(*request, nullptr, 0);
    if (result >= 0)
    {
        int status = 0;
        if (failure_stage != nullptr)
            *failure_stage = 4U;
        result = sceHttpGetStatusCode(*request, &status);
        if (result >= 0 && (status < 200 || status >= 300))
            result = -status;
    }
    if (result < 0)
    {
        if (streaming)
            playback_request_clear(*request);
        sceHttpDeleteRequest(*request);
        *request = -1;
    }
    else if (failure_stage != nullptr)
        *failure_stage = 0U;
    return result;
}

static int http_open(const char *url, bool streaming, const char *codec, int *connection,
                     int *request, unsigned *failure_stage)
{
    if (failure_stage != nullptr)
        *failure_stage = 1U;
    *connection = sceHttpCreateConnectionWithURL(g_http_template, url, 0);
    if (*connection < 0)
        return *connection;
    const int result =
        http_send_on_connection(*connection, url, streaming, codec, request, failure_stage);
    if (result < 0)
    {
        sceHttpDeleteConnection(*connection);
        *connection = -1;
    }
    return result;
}

static void http_close(int connection, int request)
{
    if (request >= 0)
        sceHttpDeleteRequest(request);
    if (connection >= 0)
        sceHttpDeleteConnection(connection);
}

static unsigned http_audio_channels(int request)
{
    char *headers = nullptr;
    size_t size = 0;
    if (sceHttpGetAllResponseHeaders(request, &headers, &size) < 0 || headers == nullptr)
        return 0;
    static const char key[] = "channels=";
    for (size_t i = 0; i + sizeof(key) < size; ++i)
    {
        size_t match = 0;
        while (match + 1U < sizeof(key))
        {
            char value = headers[i + match];
            if (value >= 'A' && value <= 'Z')
                value = (char)(value + ('a' - 'A'));
            if (value != key[match])
                break;
            ++match;
        }
        if (match + 1U == sizeof(key))
        {
            const char value = headers[i + match];
            return value == '1' || value == '2' ? (unsigned)(value - '0') : 0U;
        }
    }
    return 0;
}

static size_t http_icy_metadata_interval(int request)
{
    char *headers = nullptr;
    size_t size = 0U;
    if (sceHttpGetAllResponseHeaders(request, &headers, &size) < 0 || headers == nullptr)
        return 0U;
    return icy_metadata_interval_from_headers(headers, size);
}

static radio_playlist_kind_t http_playlist_kind(int request, const char *url)
{
    const radio_playlist_kind_t url_kind = radio_playlist_kind_from_url(url);
    if (url_kind != RADIO_PLAYLIST_NONE)
        return url_kind;
    char *headers = nullptr;
    size_t size = 0;
    if (sceHttpGetAllResponseHeaders(request, &headers, &size) < 0 || headers == nullptr)
        return RADIO_PLAYLIST_NONE;
    return radio_playlist_kind_from_headers(headers, size);
}

static int read_playlist_document(int request, char *data, size_t capacity, size_t *size)
{
    *size = 0;
    while (*size < capacity && !SDL_AtomicGet(&g_stop_playback))
    {
        const int received = sceHttpReadData(request, data + *size, capacity - *size);
        if (received < 0)
            return received;
        if (received == 0)
            return *size == 0 ? -2 : 0;
        *size += (size_t)received;
    }
    return SDL_AtomicGet(&g_stop_playback) ? 0 : -2;
}

static int open_resolved_stream(const radio_station_t *station, int *connection, int *request,
                                char *resolved_url, size_t resolved_capacity)
{
    char current[sizeof(station->url)];
    SDL_strlcpy(current, station->url, sizeof(current));
    auto *document = static_cast<char *>(malloc(PLAYLIST_BUFFER_SIZE));
    if (document == nullptr)
        return -1;

    int result = -2;
    for (unsigned depth = 0; depth < PLAYLIST_REDIRECT_LIMIT; ++depth)
    {
        result = http_open(current, true, station->codec, connection, request, nullptr);
        if (result < 0)
            break;
        radio_playlist_kind_t kind = station->hls != 0U && depth == 0U
                                         ? RADIO_PLAYLIST_HLS
                                         : http_playlist_kind(*request, current);
        if (kind == RADIO_PLAYLIST_NONE)
        {
            SDL_strlcpy(resolved_url, current, resolved_capacity);
            free(document);
            return STREAM_OPEN_DIRECT;
        }
        if (kind == RADIO_PLAYLIST_HLS)
        {
            SDL_strlcpy(resolved_url, current, resolved_capacity);
            playback_request_clear(*request);
            http_close(*connection, *request);
            *connection = -1;
            *request = -1;
            free(document);
            return STREAM_OPEN_HLS;
        }

        size_t document_size = 0;
        result = read_playlist_document(*request, document, PLAYLIST_BUFFER_SIZE, &document_size);
        playback_request_clear(*request);
        http_close(*connection, *request);
        *connection = -1;
        *request = -1;
        if (result < 0)
            break;
        const radio_playlist_kind_t body_kind =
            radio_playlist_kind_from_body(document, document_size);
        if (body_kind == RADIO_PLAYLIST_HLS)
        {
            SDL_strlcpy(resolved_url, current, resolved_capacity);
            free(document);
            return STREAM_OPEN_HLS;
        }
        if (body_kind == RADIO_PLAYLIST_M3U || body_kind == RADIO_PLAYLIST_PLS)
            kind = body_kind;

        char resolved[sizeof(current)];
        result = (int)radio_playlist_first_url(kind, document, document_size, current, resolved,
                                               sizeof(resolved));
        if (result < 0)
            break;
        SDL_strlcpy(current, resolved, sizeof(current));
    }
    free(document);
    return result < 0 ? result : -2;
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

static void append_utf8(char *output, size_t capacity, size_t *written, unsigned codepoint)
{
    uint8_t bytes[3];
    unsigned count;
    if (codepoint < 0x80U)
    {
        bytes[0] = (uint8_t)codepoint;
        count = 1;
    }
    else if (codepoint < 0x800U)
    {
        bytes[0] = (uint8_t)(0xc0U | (codepoint >> 6));
        bytes[1] = (uint8_t)(0x80U | (codepoint & 0x3fU));
        count = 2;
    }
    else
    {
        bytes[0] = (uint8_t)(0xe0U | (codepoint >> 12));
        bytes[1] = (uint8_t)(0x80U | ((codepoint >> 6) & 0x3fU));
        bytes[2] = (uint8_t)(0x80U | (codepoint & 0x3fU));
        count = 3;
    }
    for (unsigned i = 0; i < count && *written + 1 < capacity; ++i)
    {
        output[(*written)++] = (char)bytes[i];
    }
}

static bool json_string_field(const char *begin, const char *end, const char *field, char *output,
                              size_t capacity)
{
    const size_t field_length = strlen(field);
    for (const char *at = begin; at + field_length + 2 < end; ++at)
    {
        if (*at != '"' || memcmp(at + 1, field, field_length) != 0 || at[1 + field_length] != '"')
            continue;
        const char *value = at + field_length + 2;
        while (value < end && (*value == ' ' || *value == '\t' || *value == ':'))
            ++value;
        if (value >= end || *value != '"')
            return false;
        ++value;
        size_t written = 0;
        while (value < end && *value != '"')
        {
            if (*value != '\\')
            {
                if (written + 1 < capacity)
                    output[written++] = *value;
                ++value;
                continue;
            }
            ++value;
            if (value >= end)
                break;
            unsigned codepoint;
            const char escape = *value++;
            if (escape == 'u' && value + 4 <= end)
            {
                codepoint = 0;
                bool valid = true;
                for (unsigned i = 0; i < 4; ++i)
                {
                    const int hex = hex_value(value[i]);
                    if (hex < 0)
                        valid = false;
                    codepoint = (codepoint << 4) | (unsigned)(hex < 0 ? 0 : hex);
                }
                value += 4;
                if (!valid || (codepoint >= 0xd800U && codepoint <= 0xdfffU))
                    codepoint = '?';
            }
            else if (escape == 'n' || escape == 'r' || escape == 't')
                codepoint = ' ';
            else if (escape == 'b' || escape == 'f')
                continue;
            else
                codepoint = (uint8_t)escape;
            append_utf8(output, capacity, &written, codepoint);
        }
        if (capacity != 0)
            output[written] = '\0';
        return true;
    }
    if (capacity != 0)
        output[0] = '\0';
    return false;
}

static int json_integer_field(const char *begin, const char *end, const char *field, int fallback)
{
    const size_t field_length = strlen(field);
    for (const char *at = begin; at + field_length + 2 < end; ++at)
    {
        if (*at != '"' || memcmp(at + 1, field, field_length) != 0 || at[1 + field_length] != '"')
            continue;
        const char *value = at + field_length + 2;
        while (value < end && (*value == ' ' || *value == '\t' || *value == ':'))
            ++value;
        const bool negative = value < end && *value == '-';
        if (negative)
            ++value;
        int result = 0;
        bool found = false;
        while (value < end && *value >= '0' && *value <= '9')
        {
            found = true;
            if (result <= (INT_MAX - 9) / 10)
                result = result * 10 + (*value - '0');
            ++value;
        }
        return found ? (negative ? -result : result) : fallback;
    }
    return fallback;
}

static char ascii_lower(char value)
{
    return value >= 'A' && value <= 'Z' ? (char)(value + ('a' - 'A')) : value;
}

static bool contains_ascii_case_insensitive(const char *text, const char *needle)
{
    if (text == nullptr || needle == nullptr || *needle == '\0')
        return false;
    for (; *text != '\0'; ++text)
    {
        const char *hay = text;
        const char *find = needle;
        while (*hay != '\0' && *find != '\0' && ascii_lower(*hay) == ascii_lower(*find))
        {
            ++hay;
            ++find;
        }
        if (*find == '\0')
            return true;
    }
    return false;
}

static bool normalize_supported_codec(radio_station_t *station)
{
    if (strcasecmp(station->codec, "AAC") == 0 || strcasecmp(station->codec, "AAC+") == 0 ||
        strcasecmp(station->codec, "MP3") == 0 || strcasecmp(station->codec, "FLAC") == 0 ||
        strcasecmp(station->codec, "OPUS") == 0 || strcasecmp(station->codec, "VORBIS") == 0)
        return true;
    if (strcasecmp(station->codec, "OGG") == 0)
    {
        const bool opus = contains_ascii_case_insensitive(station->url, "opus") ||
                          contains_ascii_case_insensitive(station->name, "opus");
        const bool flac = contains_ascii_case_insensitive(station->url, "flac") ||
                          contains_ascii_case_insensitive(station->name, "flac");
        if (!opus && !flac && contains_ascii_case_insensitive(station->url, ".mp3"))
            return false;
        if (opus)
            SDL_strlcpy(station->codec, "OPUS", sizeof(station->codec));
        else if (flac)
            SDL_strlcpy(station->codec, "FLAC", sizeof(station->codec));
        return true;
    }
    return false;
}

static unsigned parse_catalog(const char *json, size_t length, radio_station_t *stations,
                              unsigned capacity)
{
    const char *end = json + length;
    const char *at = json;
    unsigned count = 0;
    while (at < end && count < capacity)
    {
        while (at < end && *at != '{')
            ++at;
        if (at == end)
            break;
        const char *object = at++;
        bool in_string = false;
        bool escaped = false;
        unsigned depth = 1;
        while (at < end && depth != 0)
        {
            const char value = *at++;
            if (in_string)
            {
                if (escaped)
                    escaped = false;
                else if (value == '\\')
                    escaped = true;
                else if (value == '"')
                    in_string = false;
            }
            else if (value == '"')
                in_string = true;
            else if (value == '{')
                ++depth;
            else if (value == '}')
                --depth;
        }
        if (depth != 0)
            break;
        const char *object_end = at;
        radio_station_t station;
        memset(&station, 0, sizeof(station));
        station.popular_rank = RADIO_RANK_NONE;
        station.trending_rank = RADIO_RANK_NONE;
        station.voted_rank = RADIO_RANK_NONE;
        json_string_field(object, object_end, "stationuuid", station.uuid, sizeof(station.uuid));
        json_string_field(object, object_end, "name", station.name, sizeof(station.name));
        json_string_field(object, object_end, "url_resolved", station.url, sizeof(station.url));
        if (station.url[0] == '\0')
        {
            json_string_field(object, object_end, "url", station.url, sizeof(station.url));
        }
        json_string_field(object, object_end, "country", station.country, sizeof(station.country));
        json_string_field(object, object_end, "countrycode", station.country_code,
                          sizeof(station.country_code));
        json_string_field(object, object_end, "state", station.state, sizeof(station.state));
        json_string_field(object, object_end, "language", station.language,
                          sizeof(station.language));
        json_string_field(object, object_end, "tags", station.tags, sizeof(station.tags));
        json_string_field(object, object_end, "codec", station.codec, sizeof(station.codec));
        const int bitrate = json_integer_field(object, object_end, "bitrate", 0);
        const int votes = json_integer_field(object, object_end, "votes", 0);
        const int clicks = json_integer_field(object, object_end, "clickcount", 0);
        station.bitrate = bitrate > 0 ? (uint32_t)bitrate : 0U;
        station.votes = votes > 0 ? (uint32_t)votes : 0U;
        station.click_count = clicks > 0 ? (uint32_t)clicks : 0U;
        station.click_trend = json_integer_field(object, object_end, "clicktrend", 0);
        const int hls = json_integer_field(object, object_end, "hls", 0);
        station.hls = hls != 0 ? 1U : 0U;
        const int healthy = json_integer_field(object, object_end, "lastcheckok", 1);
        if (station.uuid[0] != '\0' && station.name[0] != '\0' && station.url[0] != '\0' &&
            normalize_supported_codec(&station) &&
            (station.hls == 0U || strcasecmp(station.codec, "AAC") == 0 ||
             strcasecmp(station.codec, "AAC+") == 0) &&
            healthy != 0)
        {
            stations[count++] = station;
        }
    }
    return count;
}

static int station_index(const radio_station_t *stations, unsigned count, const char *uuid)
{
    for (unsigned i = 0; i < count; ++i)
    {
        if (strcmp(stations[i].uuid, uuid) == 0)
            return (int)i;
    }
    return -1;
}

static unsigned json_object_count(const char *json, size_t length)
{
    const char *end = json + length;
    bool in_string = false;
    bool escaped = false;
    unsigned depth = 0U;
    unsigned count = 0U;
    for (const char *at = json; at < end; ++at)
    {
        const char value = *at;
        if (in_string)
        {
            if (escaped)
                escaped = false;
            else if (value == '\\')
                escaped = true;
            else if (value == '"')
                in_string = false;
        }
        else if (value == '"')
            in_string = true;
        else if (value == '{')
        {
            if (depth == 0U)
                ++count;
            ++depth;
        }
        else if (value == '}' && depth != 0U)
            --depth;
    }
    return count;
}

static bool valid_mirror_name(const char *name)
{
    static const char suffix[] = ".api.radio-browser.info";
    if (name == nullptr || *name == '\0')
        return false;
    const size_t length = strlen(name);
    const size_t suffix_length = sizeof(suffix) - 1U;
    if (length <= suffix_length || length >= CATALOG_MIRROR_LENGTH)
        return false;
    for (const char *at = name; *at != '\0'; ++at)
    {
        const unsigned char value = (unsigned char)*at;
        if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '.' || value == '-'))
            return false;
    }
    return strcasecmp(name + length - suffix_length, suffix) == 0;
}

static void add_mirror(const char *name)
{
    if (!valid_mirror_name(name) || g_mirror_count >= CATALOG_MAX_MIRRORS)
        return;
    for (unsigned i = 0U; i < g_mirror_count; ++i)
        if (strcasecmp(g_mirrors[i], name) == 0)
            return;
    SDL_strlcpy(g_mirrors[g_mirror_count++], name, CATALOG_MIRROR_LENGTH);
}

static void catalog_http_close(catalog_http_client_t *client)
{
    if (client->connection >= 0)
        sceHttpDeleteConnection(client->connection);
    client->connection = -1;
    client->mirror = UINT_MAX;
}

static int api_read(catalog_http_client_t *client, const char *path, char *output, size_t capacity,
                    size_t *output_size)
{
    if (client == nullptr || output == nullptr || capacity == 0U || output_size == nullptr)
        return -1;
    *output_size = 0U;
    if (g_mirror_count == 0U)
        add_mirror("all.api.radio-browser.info");
    int last_error = -2;
    for (unsigned round = 0U; round < CATALOG_API_RETRY_ROUNDS; ++round)
    {
        for (unsigned attempt = 0U; attempt < g_mirror_count; ++attempt)
        {
            const unsigned mirror = (g_mirror_index + attempt) % g_mirror_count;
            char url[1400];
            const int length = snprintf(url, sizeof(url), "https://%s%s", g_mirrors[mirror], path);
            if (length <= 0 || (size_t)length >= sizeof(url))
                return -1;
            int request = -1;
            unsigned failure_stage = 1U;
            int error = 0;
            if (client->connection < 0 || client->mirror != mirror)
            {
                catalog_http_close(client);
                client->connection = sceHttpCreateConnectionWithURL(g_http_template, url, 1);
                if (client->connection < 0)
                    error = client->connection;
                else
                    client->mirror = mirror;
            }
            if (error == 0)
                error = http_send_on_connection(client->connection, url, false, nullptr, &request,
                                                &failure_stage);
            size_t used = 0U;
            while (error == 0 && used < capacity && !SDL_AtomicGet(&g_shutting_down))
            {
                const int received = sceHttpReadData(request, output + used, capacity - used);
                if (received < 0)
                {
                    error = received == -1 ? -1250 : received;
                    failure_stage = 0U;
                }
                else if (received == 0)
                    break;
                else
                    used += (size_t)received;
            }
            if (request >= 0)
                sceHttpDeleteRequest(request);
            if (error == 0 && used != 0U && used < capacity)
            {
                g_mirror_index = mirror;
                *output_size = used;
                return 0;
            }
            if (error == -1 && failure_stage != 0U)
                last_error = -1200 - (int)failure_stage;
            else
                last_error = error != 0 ? error : -2;
            catalog_http_close(client);
            if (SDL_AtomicGet(&g_shutting_down))
                return last_error;
        }
        if (round + 1U < CATALOG_API_RETRY_ROUNDS)
            SDL_Delay(CATALOG_API_RETRY_DELAY_MS * (round + 1U));
    }
    fprintf(stderr, "[PSRadio][catalog] API request failed path=%s error=%d\n", path, last_error);
    return last_error;
}

static void refresh_mirrors(catalog_http_client_t *client, char *json)
{
    size_t length = 0U;
    if (api_read(client, "/json/servers", json, JSON_CAPACITY, &length) != 0)
        return;
    const char *end = json + length;
    const char *at = json;
    while (at < end && g_mirror_count < CATALOG_MAX_MIRRORS)
    {
        while (at < end && *at != '{')
            ++at;
        if (at == end)
            break;
        const char *object = at++;
        while (at < end && *at != '}')
            ++at;
        if (at == end)
            break;
        ++at;
        char name[CATALOG_MIRROR_LENGTH];
        if (json_string_field(object, at, "name", name, sizeof(name)))
            add_mirror(name);
    }
}

static bool append_encoded_parameter(char *path, size_t capacity, const char *key,
                                     const char *value)
{
    if (value == nullptr || *value == '\0')
        return true;
    size_t used = strlen(path);
    const int prefix = snprintf(path + used, capacity - used, "&%s=", key);
    if (prefix <= 0 || (size_t)prefix >= capacity - used)
        return false;
    used += (size_t)prefix;
    static const char hex[] = "0123456789ABCDEF";
    for (const unsigned char *at = (const unsigned char *)value; *at != 0U; ++at)
    {
        const bool plain = (*at >= 'a' && *at <= 'z') || (*at >= 'A' && *at <= 'Z') ||
                           (*at >= '0' && *at <= '9') || *at == '-' || *at == '_' || *at == '.' ||
                           *at == '~';
        const size_t required = plain ? 1U : 3U;
        if (used + required >= capacity)
            return false;
        if (plain)
            path[used++] = (char)*at;
        else
        {
            path[used++] = '%';
            path[used++] = hex[*at >> 4];
            path[used++] = hex[*at & 15U];
        }
    }
    path[used] = '\0';
    return true;
}

static bool build_search_path(char *path, size_t capacity, const char *codec,
                              const radio_catalog_query_t *query, unsigned offset)
{
    const int length =
        snprintf(path, capacity,
                 "/json/stations/search?hidebroken=true&order=clickcount&reverse=true"
                 "&limit=%u&offset=%u",
                 CATALOG_PAGE_LIMIT, offset);
    if (length <= 0 || (size_t)length >= capacity)
        return false;
    if (!append_encoded_parameter(path, capacity, "codec", codec))
        return false;
    if (query == nullptr)
        return true;
    if (!append_encoded_parameter(path, capacity, "name", query->name) ||
        !append_encoded_parameter(path, capacity, "countrycode", query->country_code) ||
        !append_encoded_parameter(path, capacity, "tag", query->tag) ||
        !append_encoded_parameter(path, capacity, "language", query->language))
        return false;
    if (query->bitrate_min != 0U)
    {
        const size_t used = strlen(path);
        const int appended =
            snprintf(path + used, capacity - used, "&bitrateMin=%u", query->bitrate_min);
        if (appended <= 0 || (size_t)appended >= capacity - used)
            return false;
    }
    return true;
}

static int sync_station_query(radio_catalog_store_t *store, const radio_catalog_query_t *query,
                              bool full_sync, bool shared_store, char *json,
                              catalog_http_client_t *client)
{
    auto *page =
        static_cast<radio_station_t *>(SDL_malloc(CATALOG_PAGE_LIMIT * sizeof(radio_station_t)));
    if (page == nullptr)
        return -1302;
    int64_t previous_sync = 0;
    if (shared_store)
        SDL_LockMutex(g_store_mutex);
    radio_catalog_store_get_meta(store, CATALOG_SYNC_META, &previous_sync);
    if (shared_store)
        SDL_UnlockMutex(g_store_mutex);
    const uint64_t sync_id =
        (uint64_t)(previous_sync < 1 ? 1 : previous_sync) + (full_sync ? 1U : 0U);

    int error = 0;
    bool ok = true;
    uint64_t total_count = 0U;
    for (unsigned offset = 0U; error == 0; offset += CATALOG_PAGE_LIMIT)
    {
        if (offset >= CATALOG_RESULT_LIMIT)
        {
            error = -5;
            break;
        }
        if (SDL_AtomicGet(&g_shutting_down))
        {
            error = -4;
            break;
        }
        char path[1200];
        if (!build_search_path(path, sizeof(path), nullptr, query, offset))
        {
            error = -1303;
            break;
        }
        size_t length = 0U;
        error = api_read(client, path, json, JSON_CAPACITY, &length);
        if (error != 0)
            break;
        const unsigned objects = json_object_count(json, length);
        if (objects == 0U)
            break;
        const unsigned count = parse_catalog(json, length, page, CATALOG_PAGE_LIMIT);
        total_count += count;
        fprintf(stderr,
                "[PSRadio][catalog] page offset=%u bytes=%zu objects=%u accepted=%u total=%llu\n",
                offset, length, objects, count, (unsigned long long)total_count);
        SDL_LockMutex(g_state_mutex);
        g_status.sync_station_count = total_count > UINT_MAX ? UINT_MAX : (unsigned)total_count;
        SDL_UnlockMutex(g_state_mutex);
        for (unsigned first = 0U; first < count && error == 0; first += CATALOG_WRITE_BATCH)
        {
            unsigned end = first + CATALOG_WRITE_BATCH;
            if (end > count)
                end = count;
            if (shared_store)
                SDL_LockMutex(g_store_mutex);
            ok = radio_catalog_store_begin(store);
            for (unsigned i = first; i < end && ok; ++i)
                ok = radio_catalog_store_upsert_station(store, &page[i], sync_id);
            if (ok)
                ok = radio_catalog_store_commit(store);
            const int store_error = ok ? 0 : radio_catalog_store_error(store);
            if (!ok)
                radio_catalog_store_rollback(store);
            if (shared_store)
                SDL_UnlockMutex(g_store_mutex);
            if (!ok)
                error = -3000 - store_error;
        }
        if (error != 0)
            break;
        if (objects < CATALOG_PAGE_LIMIT)
            break;
    }

    if (error == 0 && full_sync && total_count == 0U)
        error = -2;
    if (error == 0 && full_sync)
    {
        size_t removed = 0U;
        do
        {
            ok = radio_catalog_store_begin(store) &&
                 radio_catalog_store_prune_stations(store, sync_id, CATALOG_PRUNE_BATCH, &removed);
            if (ok)
                ok = radio_catalog_store_commit(store);
            const int store_error = ok ? 0 : radio_catalog_store_error(store);
            if (!ok)
                radio_catalog_store_rollback(store);
            if (!ok)
            {
                error = -3000 - store_error;
                break;
            }
        } while (removed == CATALOG_PRUNE_BATCH && !SDL_AtomicGet(&g_shutting_down));
    }
    if (error == 0 && full_sync && !SDL_AtomicGet(&g_shutting_down))
    {
        ok = radio_catalog_store_begin(store) &&
             radio_catalog_store_set_meta(store, CATALOG_SYNC_META, (int64_t)sync_id) &&
             radio_catalog_store_set_meta(store, CATALOG_SYNC_TIME_META, (int64_t)time(nullptr));
        if (ok)
            ok = radio_catalog_store_commit(store);
        const int store_error = ok ? 0 : radio_catalog_store_error(store);
        if (!ok)
            radio_catalog_store_rollback(store);
        if (!ok)
            error = -3000 - store_error;
    }
    if (error != 0)
        fprintf(stderr, "[PSRadio][catalog] station sync failed error=%d total=%llu\n", error,
                (unsigned long long)total_count);
    SDL_free(page);
    return error != 0 ? error : 0;
}

static void normalize_country_label(radio_facet_t *facet)
{
    static const struct
    {
        const char *code;
        const char *label;
    } aliases[] = {
        {"CD", "DR Congo"},
        {"FK", "Falkland Islands"},
        {"FM", "Micronesia"},
        {"GB", "United Kingdom"},
        {"IO", "British Indian Ocean"},
        {"KP", "North Korea"},
        {"LA", "Laos"},
        {"SH", "Saint Helena"},
        {"TF", "French Southern Territories"},
        {"UM", "U.S. Outlying Islands"},
        {"VC", "Saint Vincent"},
        {"VE", "Venezuela"},
    };
    for (size_t i = 0U; i < sizeof(aliases) / sizeof(aliases[0]); ++i)
    {
        if (strcmp(facet->value, aliases[i].code) != 0)
            continue;
        SDL_strlcpy(facet->label, aliases[i].label, sizeof(facet->label));
        return;
    }
}

static unsigned parse_facets(const char *json, size_t length, radio_facet_kind_t kind,
                             radio_facet_t *facets, unsigned capacity)
{
    const char *end = json + length;
    const char *at = json;
    unsigned count = 0U;
    while (at < end && count < capacity)
    {
        while (at < end && *at != '{')
            ++at;
        if (at == end)
            break;
        const char *object = at++;
        while (at < end && *at != '}')
            ++at;
        if (at == end)
            break;
        ++at;
        radio_facet_t facet;
        memset(&facet, 0, sizeof(facet));
        json_string_field(object, at, "name", facet.label, sizeof(facet.label));
        if (kind == RADIO_FACET_COUNTRY)
            json_string_field(object, at, "iso_3166_1", facet.value, sizeof(facet.value));
        else
            SDL_strlcpy(facet.value, facet.label, sizeof(facet.value));
        if (kind == RADIO_FACET_COUNTRY)
            normalize_country_label(&facet);
        const int stations = json_integer_field(object, at, "stationcount", 0);
        facet.station_count = stations > 0 ? (uint32_t)stations : 0U;
        if (facet.value[0] != '\0' && facet.label[0] != '\0' && facet.station_count != 0U)
            facets[count++] = facet;
    }
    return count;
}

static void sync_facets(radio_catalog_store_t *store, catalog_http_client_t *client, char *json)
{
    static const char *paths[] = {
        "/json/countries?hidebroken=true&order=stationcount&reverse=true&limit=512",
        "/json/tags?hidebroken=true&order=stationcount&reverse=true&limit=512",
        "/json/languages?hidebroken=true&order=stationcount&reverse=true&limit=512"};
    auto *facets =
        static_cast<radio_facet_t *>(SDL_malloc(RADIO_MAX_FACETS * sizeof(radio_facet_t)));
    if (facets == nullptr)
        return;
    for (unsigned kind = 0U; kind < 3U; ++kind)
    {
        size_t length = 0U;
        if (api_read(client, paths[kind], json, JSON_CAPACITY, &length) != 0)
            continue;
        const unsigned count =
            parse_facets(json, length, (radio_facet_kind_t)kind, facets, RADIO_MAX_FACETS);
        if (count == 0U)
            continue;
        radio_catalog_store_replace_facets(store, (radio_facet_kind_t)kind, facets, count);
    }
    SDL_free(facets);
}

static bool migrate_legacy_catalog(void)
{
    SDL_LockMutex(g_store_mutex);
    const size_t existing = radio_catalog_store_station_count(&g_catalog_store);
    SDL_UnlockMutex(g_store_mutex);
    if (existing != 0U)
        return true;
    auto *file = static_cast<cache_file_t *>(malloc(sizeof(cache_file_t)));
    if (file == nullptr)
        return false;
    const bool valid = read_exact(LEGACY_CACHE_PATH, file, sizeof(*file)) &&
                       file->header.magic == LEGACY_CACHE_MAGIC &&
                       file->header.version == LEGACY_CATALOG_CACHE_VERSION &&
                       file->header.count <= LEGACY_CATALOG_CAPACITY &&
                       file->header.checksum == checksum(file->stations, sizeof(file->stations));
    if (valid)
    {
        SDL_LockMutex(g_store_mutex);
        bool ok = radio_catalog_store_begin(&g_catalog_store);
        for (unsigned i = 0U; i < file->header.count && ok; ++i)
            ok = radio_catalog_store_upsert_station(&g_catalog_store, &file->stations[i], 1U);
        if (ok)
            ok = radio_catalog_store_set_meta(&g_catalog_store, CATALOG_SYNC_META, 1);
        if (ok)
            radio_catalog_store_commit(&g_catalog_store);
        else
            radio_catalog_store_rollback(&g_catalog_store);
        SDL_UnlockMutex(g_store_mutex);
    }
    free(file);
    return valid;
}

static bool load_catalog_summary(radio_catalog_state_t state)
{
    SDL_LockMutex(g_store_mutex);
    const size_t count = radio_catalog_store_station_count(&g_catalog_store);
    SDL_UnlockMutex(g_store_mutex);
    if (count == 0U || count > UINT_MAX)
        return false;
    SDL_LockMutex(g_state_mutex);
    g_status.catalog_size = (unsigned)count;
    g_status.catalog_state = state;
    ++g_status.catalog_generation;
    SDL_UnlockMutex(g_state_mutex);
    return true;
}

static void load_facet_snapshots(void)
{
    radio_facet_t facets[3][RADIO_MAX_FACETS];
    unsigned counts[3] = {0U, 0U, 0U};
    SDL_LockMutex(g_store_mutex);
    for (unsigned kind = 0U; kind < 3U; ++kind)
        counts[kind] = (unsigned)radio_catalog_store_load_facets(
            &g_catalog_store, (radio_facet_kind_t)kind, facets[kind], RADIO_MAX_FACETS);
    SDL_UnlockMutex(g_store_mutex);
    SDL_LockMutex(g_state_mutex);
    memcpy(g_facets, facets, sizeof(facets));
    memcpy(g_facet_count, counts, sizeof(counts));
    SDL_UnlockMutex(g_state_mutex);
}

static bool open_catalog_store_with_recovery(void)
{
    if (radio_catalog_store_open(&g_catalog_store, CATALOG_DATABASE_PATH) &&
        radio_catalog_store_integrity_check(&g_catalog_store))
        return true;
    radio_catalog_store_close(&g_catalog_store);
    unlink(CATALOG_DATABASE_PATH);
    return radio_catalog_store_open(&g_catalog_store, CATALOG_DATABASE_PATH) &&
           radio_catalog_store_integrity_check(&g_catalog_store);
}

static bool open_staging_store(radio_catalog_store_t *staging)
{
    unlink(CATALOG_STAGING_PATH);
    return radio_catalog_store_open(staging, CATALOG_STAGING_PATH);
}

static bool copy_favorites_to_store(radio_catalog_store_t *store)
{
    SDL_LockMutex(g_state_mutex);
    const unsigned count = g_favorite_count;
    auto *favorites =
        count == 0U
            ? nullptr
            : static_cast<decltype(g_favorites)>(malloc((size_t)count * sizeof(g_favorites[0])));
    if (favorites != nullptr)
        memcpy(favorites, g_favorites, (size_t)count * sizeof(favorites[0]));
    SDL_UnlockMutex(g_state_mutex);
    if (count != 0U && favorites == nullptr)
        return false;
    bool ok = true;
    for (unsigned i = 0U; i < count && ok; ++i)
        ok = radio_catalog_store_set_favorite(store, favorites[i], true);
    free(favorites);
    return ok;
}

static bool promote_staging_store(radio_catalog_store_t *staging)
{
    if (!radio_catalog_store_integrity_check(staging))
    {
        radio_catalog_store_close(staging);
        return false;
    }
    radio_catalog_store_close(staging);
    SDL_LockMutex(g_store_mutex);
    radio_catalog_store_close(&g_catalog_store);
    const bool replaced = rename(CATALOG_STAGING_PATH, CATALOG_DATABASE_PATH) == 0;
    const bool reopened = radio_catalog_store_open(&g_catalog_store, CATALOG_DATABASE_PATH);
    const bool valid = reopened && radio_catalog_store_integrity_check(&g_catalog_store);
    const bool favorites_copied = valid && copy_favorites_to_store(&g_catalog_store);
    SDL_UnlockMutex(g_store_mutex);
    return replaced && valid && favorites_copied;
}

static void *refresh_thread(void *task_data)
{
    catalog_task_t task = *(catalog_task_t *)task_data;
    free(task_data);
    for (;;)
    {
        SDL_LockMutex(g_state_mutex);
        g_status.searching = !task.full_sync;
        g_status.sync_station_count = 0U;
        SDL_UnlockMutex(g_state_mutex);
        radio_catalog_store_t staging;
        memset(&staging, 0, sizeof(staging));
        auto *json = static_cast<char *>(SDL_malloc(JSON_CAPACITY));
        const char *phase = "allocate response buffer";
        int error = json == nullptr ? -1301 : 0;
        phase = "open staging database";
        if (error == 0 && task.full_sync && !open_staging_store(&staging))
            error = -1304;
        if (error == 0)
        {
            catalog_http_client_t client = {-1, UINT_MAX};
            phase = "refresh mirrors";
            refresh_mirrors(&client, json);
            phase = "sync facets";
            if (task.full_sync)
                sync_facets(&staging, &client, json);
            radio_catalog_store_t *target = task.full_sync ? &staging : &g_catalog_store;
            phase = "sync stations";
            error = sync_station_query(target, task.full_sync ? nullptr : &task.query,
                                       task.full_sync, !task.full_sync, json, &client);
            catalog_http_close(&client);
            phase = "copy favorites";
            if (error == 0 && task.full_sync && !copy_favorites_to_store(&staging))
                error = -1305;
            if (error == 0)
            {
                phase = "store refresh status";
                if (!task.full_sync)
                    SDL_LockMutex(g_store_mutex);
                const bool stored =
                    radio_catalog_store_set_meta(target, CATALOG_LAST_ERROR_META, 0);
                const int store_error = stored ? 0 : radio_catalog_store_error(target);
                if (!task.full_sync)
                    SDL_UnlockMutex(g_store_mutex);
                if (!stored)
                    error = -3000 - store_error;
            }
            phase = "promote staging database";
            if (error == 0 && task.full_sync && !promote_staging_store(&staging))
                error = -1306;
            else if (error != 0 && task.full_sync)
                radio_catalog_store_close(&staging);
        }
        SDL_free(json);
        if (error != 0)
            fprintf(stderr, "[PSRadio][catalog] refresh failed phase=%s error=%d\n", phase, error);
        if (error == 0 && !SDL_AtomicGet(&g_shutting_down))
        {
            load_catalog_summary(RADIO_CATALOG_READY);
            if (task.full_sync)
                load_facet_snapshots();
            SDL_LockMutex(g_state_mutex);
            g_status.error_code = 0;
            SDL_UnlockMutex(g_state_mutex);
        }
        else if (!SDL_AtomicGet(&g_shutting_down))
        {
            SDL_LockMutex(g_state_mutex);
            g_status.catalog_state = RADIO_CATALOG_ERROR;
            g_status.error_code = error != 0 ? error : -2;
            SDL_UnlockMutex(g_state_mutex);
        }

        SDL_LockMutex(g_state_mutex);
        if (g_pending_task_ready && !SDL_AtomicGet(&g_shutting_down))
        {
            task = g_pending_task;
            g_pending_task_ready = false;
            SDL_UnlockMutex(g_state_mutex);
            continue;
        }
        g_status.refreshing = false;
        g_status.searching = false;
        SDL_UnlockMutex(g_state_mutex);
        SDL_AtomicSet(&g_refresh_running, 0);
        return nullptr;
    }
}

static void set_playback_state(radio_playback_state_t state, int error, unsigned sample_rate,
                               unsigned channels)
{
    SDL_LockMutex(g_state_mutex);
    g_status.playback_state = state;
    g_status.error_code = error;
    g_status.sample_rate = sample_rate;
    g_status.channels = channels;
    if (state == RADIO_PLAYBACK_STOPPED || state == RADIO_PLAYBACK_ERROR)
    {
        g_have_playing_station = false;
        memset(&g_playing_station, 0, sizeof(g_playing_station));
        g_status.playing_index = UINT_MAX;
    }
    SDL_UnlockMutex(g_state_mutex);
}

static void playback_request_set(int request)
{
    SDL_LockMutex(g_request_mutex);
    g_playback_request = request;
    if (request >= 0 && SDL_AtomicGet(&g_stop_playback))
    {
        sceHttpAbortRequest(request);
    }
    SDL_UnlockMutex(g_request_mutex);
}

static void playback_request_clear(int request)
{
    SDL_LockMutex(g_request_mutex);
    if (g_playback_request == request)
        g_playback_request = -1;
    SDL_UnlockMutex(g_request_mutex);
}

static size_t sink_ready_target(bool started, bool played)
{
    if (started)
        return 1U;
    return played ? AUDIO_RESTART_BLOCKS : AUDIO_START_BLOCKS;
}

static int sink_audio_thread(void *argument)
{
    auto *sink = static_cast<audio_sink_t *>(argument);
    int16_t block[AUDIO_OUT_GRAIN * 2U];
    bool started = false;
    bool played = false;

    for (;;)
    {
        SDL_LockMutex(sink->mutex);
        if (started && sink->queue.count == 0U && !sink->input_finished && !sink->cancel &&
            !SDL_AtomicGet(&g_stop_playback))
        {
            started = false;
            SDL_UnlockMutex(sink->mutex);
            if (!SDL_AtomicGet(&g_stop_playback))
            {
                set_playback_state(RADIO_PLAYBACK_BUFFERING, 0, sink->input_rate, sink->channels);
            }
            SDL_LockMutex(sink->mutex);
        }

        const size_t target = sink_ready_target(started, played);
        while (!sink->cancel && !SDL_AtomicGet(&g_stop_playback) && sink->output_result >= 0 &&
               !sink->input_finished && !pcm_queue_ready(&sink->queue, target, false))
        {
            SDL_CondWaitTimeout(sink->can_read, sink->mutex, AUDIO_WAIT_MS);
        }
        if (sink->cancel || SDL_AtomicGet(&g_stop_playback) || sink->output_result < 0 ||
            sink->queue.count == 0U)
        {
            SDL_UnlockMutex(sink->mutex);
            break;
        }

        size_t index = 0;
        pcm_queue_pop(&sink->queue, &index);
        memcpy(block, sink->queue_blocks + index * AUDIO_OUT_GRAIN * 2U, sizeof(block));
        SDL_CondSignal(sink->can_write);
        SDL_UnlockMutex(sink->mutex);

        const int result = sceAudioOutOutput(sink->handle, block);
        if (result < 0)
        {
            SDL_LockMutex(sink->mutex);
            sink->output_result = result;
            sink->cancel = true;
            SDL_CondBroadcast(sink->can_write);
            SDL_UnlockMutex(sink->mutex);
            break;
        }
        sink->output_frames += AUDIO_OUT_GRAIN;
        if (!started && !SDL_AtomicGet(&g_stop_playback))
        {
            started = true;
            played = true;
            set_playback_state(RADIO_PLAYBACK_PLAYING, 0, sink->input_rate, sink->channels);
        }
    }
    return 0;
}

static void sink_cancel(audio_sink_t *sink)
{
    if (sink == nullptr || sink->handle < 0 || sink->mutex == nullptr)
        return;
    SDL_LockMutex(sink->mutex);
    sink->cancel = true;
    SDL_CondBroadcast(sink->can_read);
    SDL_CondBroadcast(sink->can_write);
    SDL_UnlockMutex(sink->mutex);
}

static int sink_open(audio_sink_t *sink, uint32_t input_rate, uint32_t channels)
{
    memset(sink, 0, sizeof(*sink));
    sink->handle = -1;
    sink->input_rate = input_rate;
    sink->channels = channels;
    if (input_rate < 8000U || input_rate > 192000U || channels < 1U || channels > 2U)
        return -1;

    sink->queue_blocks =
        static_cast<int16_t *>(malloc(AUDIO_QUEUE_BLOCKS * AUDIO_OUT_GRAIN * 2U * sizeof(int16_t)));
    sink->mutex = SDL_CreateMutex();
    sink->can_read = SDL_CreateCond();
    sink->can_write = SDL_CreateCond();
    if (sink->queue_blocks == nullptr || sink->mutex == nullptr || sink->can_read == nullptr ||
        sink->can_write == nullptr)
        goto fail;
    pcm_queue_init(&sink->queue, AUDIO_QUEUE_BLOCKS);

    sceAudioOutInit();
    sink->handle =
        sceAudioOutOpen(0xff, 0, 0, AUDIO_OUT_GRAIN, AUDIO_OUT_RATE, AUDIO_OUT_STEREO_S16);
    if (sink->handle < 0)
        goto fail;
    int volumes[8];
    for (unsigned i = 0; i < 8; ++i)
        volumes[i] = AUDIO_OUT_VOLUME_0DB;
    sceAudioOutSetVolume(sink->handle, 3, volumes);
    /*
     * The PS5 SDL backend forwards non-null names to pthread_set_name_np.
     * That optional import is unavailable on the target runtime and resolves
     * to null, so leave the debug name unset.
     */
    sink->thread = SDL_CreateThread(sink_audio_thread, nullptr, sink);
    if (sink->thread == nullptr)
        goto fail;
    return 0;

fail:
{
    const int error = sink->handle < 0 ? sink->handle : -1;
    if (sink->handle >= 0)
    {
        sceAudioOutOutput(sink->handle, nullptr);
        sceAudioOutClose(sink->handle);
    }
    if (sink->can_write != nullptr)
        SDL_DestroyCond(sink->can_write);
    if (sink->can_read != nullptr)
        SDL_DestroyCond(sink->can_read);
    if (sink->mutex != nullptr)
        SDL_DestroyMutex(sink->mutex);
    free(sink->queue_blocks);
    memset(sink, 0, sizeof(*sink));
    sink->handle = -1;
    return error;
}
}

static int sink_queue_block(audio_sink_t *sink)
{
    SDL_LockMutex(sink->mutex);
    while (sink->queue.count == sink->queue.capacity && !sink->cancel &&
           !SDL_AtomicGet(&g_stop_playback) && sink->output_result >= 0)
    {
        SDL_CondWaitTimeout(sink->can_write, sink->mutex, AUDIO_WAIT_MS);
    }
    if (sink->cancel || SDL_AtomicGet(&g_stop_playback))
    {
        SDL_UnlockMutex(sink->mutex);
        return -1;
    }
    if (sink->output_result < 0)
    {
        const int result = sink->output_result;
        SDL_UnlockMutex(sink->mutex);
        return result;
    }

    size_t index = 0;
    if (!pcm_queue_push(&sink->queue, &index))
    {
        SDL_UnlockMutex(sink->mutex);
        return -1;
    }
    memcpy(sink->queue_blocks + index * AUDIO_OUT_GRAIN * 2U, sink->block, sizeof(sink->block));
    sink->pending = 0;
    SDL_CondSignal(sink->can_read);
    SDL_UnlockMutex(sink->mutex);
    return 0;
}

static int sink_output_frame(audio_sink_t *sink, int16_t left, int16_t right)
{
    sink->block[sink->pending++] = left;
    sink->block[sink->pending++] = right;
    return sink->pending == AUDIO_OUT_GRAIN * 2U ? sink_queue_block(sink) : 0;
}

static int sink_push_pcm(audio_sink_t *sink, const int16_t *samples, unsigned sample_count)
{
    const unsigned frames = sample_count / sink->channels;
    for (unsigned i = 0; i < frames; ++i)
    {
        const int16_t left = samples[i * sink->channels];
        const int16_t right = sink->channels == 2U ? samples[i * 2U + 1U] : left;
        if (!sink->have_previous)
        {
            sink->previous_left = left;
            sink->previous_right = right;
            sink->have_previous = true;
            sink->input_index = 0;
            continue;
        }

        ++sink->input_index;
        const uint64_t interval_end = sink->input_index * AUDIO_OUT_RATE;
        const uint64_t interval_start = (sink->input_index - 1U) * AUDIO_OUT_RATE;
        while (sink->next_output_position < interval_end)
        {
            const uint64_t fraction = sink->next_output_position - interval_start;
            const int32_t out_left =
                sink->previous_left +
                (int32_t)(((int64_t)(left - sink->previous_left) * (int64_t)fraction) /
                          (int64_t)AUDIO_OUT_RATE);
            const int32_t out_right =
                sink->previous_right +
                (int32_t)(((int64_t)(right - sink->previous_right) * (int64_t)fraction) /
                          (int64_t)AUDIO_OUT_RATE);
            const int result = sink_output_frame(sink, (int16_t)out_left, (int16_t)out_right);
            if (result < 0)
                return result;
            sink->next_output_position += sink->input_rate;
        }
        sink->previous_left = left;
        sink->previous_right = right;
    }
    return 0;
}

static uint64_t sink_close(audio_sink_t *sink)
{
    if (sink->handle < 0)
        return 0U;
    if (sink->pending != 0 && !sink->cancel && !SDL_AtomicGet(&g_stop_playback))
    {
        memset(sink->block + sink->pending, 0,
               (AUDIO_OUT_GRAIN * 2U - sink->pending) * sizeof(sink->block[0]));
        sink->pending = AUDIO_OUT_GRAIN * 2U;
        sink_queue_block(sink);
    }

    SDL_LockMutex(sink->mutex);
    sink->input_finished = true;
    sink->cancel = sink->cancel || SDL_AtomicGet(&g_stop_playback) || sink->output_result < 0;
    SDL_CondBroadcast(sink->can_read);
    SDL_CondBroadcast(sink->can_write);
    SDL_UnlockMutex(sink->mutex);
    SDL_WaitThread(sink->thread, nullptr);
    const uint64_t output_frames = sink->output_frames;

    sceAudioOutOutput(sink->handle, nullptr);
    sceAudioOutClose(sink->handle);
    SDL_DestroyCond(sink->can_write);
    SDL_DestroyCond(sink->can_read);
    SDL_DestroyMutex(sink->mutex);
    free(sink->queue_blocks);
    memset(sink, 0, sizeof(*sink));
    sink->handle = -1;
    return output_frames;
}

struct opus_playback_t
{
    opus_decoder_t decoder;
    audio_sink_t sink;
    int16_t *pcm;
    uint32_t stream_serial;
    size_t pre_skip_frames;
    uint64_t decoded_frames;
    uint64_t granule_offset;
    int result;
    bool decoder_open;
    bool stream_open;
    bool have_granule_offset;
    uint64_t output_frames;
};

using stream_read_fn = int (*)(void *context, void *data, size_t size);

static int http_stream_read(void *context, void *data, size_t size)
{
    return sceHttpReadData(*(const int *)context, data, size);
}

struct prefixed_http_reader_t
{
    stream_read_fn read_stream;
    void *read_context;
    uint8_t prefix[OGG_PROBE_BUFFER_SIZE];
    size_t prefix_at;
    size_t prefix_size;
};

enum ogg_format_t
{
    OGG_FORMAT_UNSUPPORTED = -1,
    OGG_FORMAT_NEED_MORE = 0,
    OGG_FORMAT_OPUS = 1,
    OGG_FORMAT_VORBIS = 2,
    OGG_FORMAT_FLAC = 3
};

static ogg_format_t ogg_probe(const uint8_t *data, size_t size)
{
    if (size < 27U)
        return OGG_FORMAT_NEED_MORE;
    if (memcmp(data, "OggS", 4U) != 0)
        return OGG_FORMAT_UNSUPPORTED;
    const size_t segments = data[26];
    const size_t payload = 27U + segments;
    if (segments == 0U)
        return OGG_FORMAT_UNSUPPORTED;
    if (size < payload + 8U)
        return OGG_FORMAT_NEED_MORE;
    if (data[27] < 5U)
        return OGG_FORMAT_UNSUPPORTED;
    if (memcmp(data + payload, "OpusHead", 8U) == 0)
        return OGG_FORMAT_OPUS;
    if (data[27] >= 7U && data[payload] == 1U && memcmp(data + payload + 1U, "vorbis", 6U) == 0)
        return OGG_FORMAT_VORBIS;
    if (data[payload] == 0x7fU && memcmp(data + payload + 1U, "FLAC", 4U) == 0)
        return OGG_FORMAT_FLAC;
    return OGG_FORMAT_UNSUPPORTED;
}

static int prefixed_http_open(prefixed_http_reader_t *reader, stream_read_fn read_stream,
                              void *read_context)
{
    memset(reader, 0, sizeof(*reader));
    reader->read_stream = read_stream;
    reader->read_context = read_context;
    ogg_format_t format = OGG_FORMAT_NEED_MORE;
    while (format == OGG_FORMAT_NEED_MORE && reader->prefix_size < sizeof(reader->prefix) &&
           !SDL_AtomicGet(&g_stop_playback))
    {
        const int received = read_stream(read_context, reader->prefix + reader->prefix_size,
                                         sizeof(reader->prefix) - reader->prefix_size);
        if (received < 0)
            return received;
        if (received == 0)
            return -3;
        reader->prefix_size += (size_t)received;
        format = ogg_probe(reader->prefix, reader->prefix_size);
    }
    return format == OGG_FORMAT_NEED_MORE ? OGG_FORMAT_UNSUPPORTED : (int)format;
}

static int prefixed_http_read(void *context, void *data, size_t size)
{
    auto *reader = static_cast<prefixed_http_reader_t *>(context);
    if (reader->prefix_at < reader->prefix_size)
    {
        const size_t available = reader->prefix_size - reader->prefix_at;
        const size_t copy = available < size ? available : size;
        memcpy(data, reader->prefix + reader->prefix_at, copy);
        reader->prefix_at += copy;
        return (int)copy;
    }
    return reader->read_stream(reader->read_context, data, size);
}

static bool opus_packet_is_celt(const uint8_t *data, size_t size)
{
    /* Opus TOC configurations 16-31 are CELT-only (RFC 6716, section 3.1). */
    return size > 0U && (data[0] >> 3U) >= 16U;
}

static void opus_decoder_reset(opus_playback_t *playback)
{
    opus_decoder_close(&playback->decoder);
    playback->decoder_open = false;
}

static void opus_playback_reset(opus_playback_t *playback, bool discard)
{
    if (discard)
        sink_cancel(&playback->sink);
    playback->output_frames += sink_close(&playback->sink);
    opus_decoder_reset(playback);
    playback->stream_serial = 0;
    playback->pre_skip_frames = 0;
    playback->decoded_frames = 0U;
    playback->granule_offset = 0U;
    playback->have_granule_offset = false;
    playback->stream_open = false;
}

static int opus_packet_ready(const ogg_opus_packet_t *packet, void *user_data)
{
    auto *playback = static_cast<opus_playback_t *>(user_data);
    const bool celt_packet = opus_packet_is_celt(packet->data, packet->size);
    if (!playback->stream_open || playback->stream_serial != packet->stream_serial)
    {
        if (playback->stream_open)
            opus_playback_reset(playback, false);
        playback->sink.handle = -1;
        playback->stream_serial = packet->stream_serial;
        playback->pre_skip_frames = packet->pre_skip;
        playback->stream_open = true;
        playback->result = opus_decoder_open(&playback->decoder, packet->channels, false);
        if (playback->result < 0)
            return -1;
        playback->decoder_open = true;
    }
    else if (playback->decoder.celt_only && !celt_packet)
    {
        opus_decoder_reset(playback);
        playback->result = opus_decoder_open(&playback->decoder, packet->channels, false);
        if (playback->result < 0)
            return -1;
        playback->decoder_open = true;
    }

    size_t produced = 0;
    playback->result = opus_decoder_decode(&playback->decoder, packet->data, packet->size,
                                           playback->pcm, OPUS_PCM_BUFFER_SIZE, &produced);
    if (playback->result == OPUS_RETRYABLE_ERROR && celt_packet)
    {
        const bool alternate_celt = !playback->decoder.celt_only;
        opus_decoder_reset(playback);
        playback->result = opus_decoder_open(&playback->decoder, packet->channels, alternate_celt);
        if (playback->result < 0)
            return -1;
        playback->decoder_open = true;
        playback->result = opus_decoder_decode(&playback->decoder, packet->data, packet->size,
                                               playback->pcm, OPUS_PCM_BUFFER_SIZE, &produced);
    }
    if (playback->result < 0)
        return -1;
    const size_t frame_bytes = packet->channels * sizeof(int16_t);
    if (frame_bytes == 0U || produced % frame_bytes != 0U)
    {
        playback->result = -1;
        return -1;
    }

    const size_t packet_frames = produced / frame_bytes;
    if (UINT64_MAX - playback->decoded_frames < packet_frames)
    {
        playback->result = -1;
        return -1;
    }
    const uint64_t decoded_end = playback->decoded_frames + packet_frames;
    if (packet->end_of_page && !playback->have_granule_offset &&
        packet->granule_position != UINT64_MAX)
    {
        if (packet->granule_position >= decoded_end)
        {
            playback->granule_offset = packet->granule_position - decoded_end;
        }
        else if (!packet->end_of_stream)
        {
            playback->result = -1;
            return -1;
        }
        playback->have_granule_offset = true;
    }
    if (UINT64_MAX - decoded_end < playback->granule_offset)
    {
        playback->result = -1;
        return -1;
    }

    size_t trim = 0U;
    const opus_pcm_trim_result_t trim_result =
        opus_pcm_end_trim(decoded_end + playback->granule_offset, packet->granule_position,
                          packet_frames, packet->end_of_stream, packet->end_of_page, &trim);
    if (packet->end_of_stream && packet->end_of_page && trim_result != OPUS_PCM_TRIM_VALID)
    {
        playback->result = -1;
        return -1;
    }
    playback->decoded_frames = decoded_end;

    const size_t skip =
        playback->pre_skip_frames < packet_frames ? playback->pre_skip_frames : packet_frames;
    playback->pre_skip_frames -= skip;
    if (trim > packet_frames - skip)
    {
        playback->result = -1;
        return -1;
    }
    const size_t frames = packet_frames - skip - trim;
    if (frames == 0U)
        return 0;

    playback->result = opus_pcm_apply_gain_s16(playback->pcm + skip * packet->channels, frames,
                                               packet->channels, packet->output_gain_q8);
    if (playback->result < 0)
        return -1;

    if (playback->sink.handle < 0)
    {
        playback->result = sink_open(&playback->sink, 48000U, packet->channels);
        if (playback->result < 0)
            return -1;
        set_playback_state(RADIO_PLAYBACK_BUFFERING, 0, 48000U, packet->channels);
    }
    playback->result = sink_push_pcm(&playback->sink, playback->pcm + skip * packet->channels,
                                     (unsigned)(frames * packet->channels));
    return playback->result < 0 ? -1 : 0;
}

static int play_opus_reader(stream_read_fn read_stream, void *read_context, uint64_t *output_frames)
{
    auto *stream = static_cast<uint8_t *>(malloc(STREAM_BUFFER_SIZE));
    auto *parser = static_cast<ogg_opus_parser_t *>(malloc(sizeof(ogg_opus_parser_t)));
    opus_playback_t playback;
    memset(&playback, 0, sizeof(playback));
    playback.sink.handle = -1;
    playback.pcm = static_cast<int16_t *>(malloc(OPUS_PCM_BUFFER_SIZE));
    if (stream == nullptr || parser == nullptr || playback.pcm == nullptr)
    {
        free(playback.pcm);
        free(parser);
        free(stream);
        return -1;
    }

    ogg_opus_init(parser, opus_packet_ready, &playback);
    int result = 0;
    while (!SDL_AtomicGet(&g_stop_playback))
    {
        const int received = read_stream(read_context, stream, STREAM_BUFFER_SIZE);
        if (received < 0)
        {
            result = received;
            break;
        }
        if (received == 0)
        {
            result = -3;
            break;
        }
        const ogg_opus_result_t parsed = ogg_opus_feed(parser, stream, (size_t)received);
        if (parsed != OGG_OPUS_OK)
        {
            result = parsed == OGG_OPUS_ERR_CALLBACK ? playback.result : (int)parsed;
            break;
        }
    }

    opus_playback_reset(&playback, result < 0 || SDL_AtomicGet(&g_stop_playback));
    *output_frames = playback.output_frames;
    free(playback.pcm);
    free(parser);
    free(stream);
    return SDL_AtomicGet(&g_stop_playback) ? 0 : result;
}

static int ogg_stream_playback_result(const ogg_stream_t *stream)
{
    const ogg_stream_result_t status = ogg_stream_status(stream);
    if (status == OGG_STREAM_CHAIN_END)
        return VORBIS_CHAIN_END;
    if (status == OGG_STREAM_EOF)
        return -3;
    if (status < OGG_STREAM_OK)
        return OGG_STREAM_PLAYBACK_ERROR_BASE + (int)status;
    return -1;
}

static int vorbis_read_more(ogg_stream_t *source, uint8_t *stream, size_t *buffered)
{
    if (*buffered >= VORBIS_STREAM_BUFFER_SIZE)
        return VORBIS_DECODER_BUFFER_FULL;
    const size_t remaining = VORBIS_STREAM_BUFFER_SIZE - *buffered;
    const size_t request_size =
        remaining < VORBIS_NETWORK_CHUNK_SIZE ? remaining : VORBIS_NETWORK_CHUNK_SIZE;
    const int received = ogg_stream_read(source, stream + *buffered, request_size);
    if (received <= 0)
        return ogg_stream_playback_result(source);
    *buffered += (size_t)received;
    return 0;
}

static int play_vorbis_reader(stream_read_fn read_stream, void *read_context,
                              uint64_t *output_frames)
{
    auto *stream = static_cast<uint8_t *>(malloc(VORBIS_STREAM_BUFFER_SIZE));
    auto *pcm = static_cast<int16_t *>(malloc(VORBIS_PCM_BUFFER_SAMPLES * sizeof(int16_t)));
    auto *source = static_cast<ogg_stream_t *>(malloc(sizeof(ogg_stream_t)));
    if (stream == nullptr || pcm == nullptr || source == nullptr)
    {
        free(source);
        free(pcm);
        free(stream);
        return -1;
    }
    ogg_stream_init(source, read_stream, read_context);

    vorbis_decoder_t decoder;
    memset(&decoder, 0, sizeof(decoder));
    audio_sink_t sink;
    memset(&sink, 0, sizeof(sink));
    sink.handle = -1;
    uint64_t total_output_frames = 0U;
    size_t buffered = 0U;
    int result = vorbis_read_more(source, stream, &buffered);
    while (result >= 0 && !SDL_AtomicGet(&g_stop_playback))
    {
        if (result == VORBIS_CHAIN_END)
        {
            if (decoder.handle == nullptr)
            {
                result = VORBIS_DECODER_UNSUPPORTED;
                break;
            }
            total_output_frames += sink_close(&sink);
            vorbis_decoder_close(&decoder);
            buffered = 0U;
            if (ogg_stream_next_chain(source) != OGG_STREAM_OK)
            {
                result = OGG_STREAM_PLAYBACK_ERROR_BASE + (int)ogg_stream_status(source);
                break;
            }
            result = vorbis_read_more(source, stream, &buffered);
            continue;
        }
        if (decoder.handle == nullptr)
        {
            size_t consumed = 0U;
            result = vorbis_decoder_open(&decoder, stream, buffered, &consumed);
            if (result == VORBIS_DECODER_NEED_MORE)
            {
                result = vorbis_read_more(source, stream, &buffered);
                continue;
            }
            if (result < 0)
                break;
            memmove(stream, stream + consumed, buffered - consumed);
            buffered -= consumed;
            result = sink_open(&sink, decoder.sample_rate, decoder.channels);
            if (result < 0)
                break;
            set_playback_state(RADIO_PLAYBACK_BUFFERING, 0, decoder.sample_rate, decoder.channels);
            continue;
        }

        size_t consumed = 0U;
        size_t samples = 0U;
        result = vorbis_decoder_decode(&decoder, stream, buffered, &consumed, pcm,
                                       VORBIS_PCM_BUFFER_SAMPLES, &samples);
        if (result == VORBIS_DECODER_NEED_MORE)
        {
            result = vorbis_read_more(source, stream, &buffered);
            continue;
        }
        if (result < 0)
            break;
        if (consumed != 0U)
        {
            memmove(stream, stream + consumed, buffered - consumed);
            buffered -= consumed;
        }
        if (samples != 0U)
        {
            result = sink_push_pcm(&sink, pcm, (unsigned)samples);
            if (result < 0)
                break;
        }
    }

    if (result < 0 || SDL_AtomicGet(&g_stop_playback))
        sink_cancel(&sink);
    total_output_frames += sink_close(&sink);
    *output_frames = total_output_frames;
    vorbis_decoder_close(&decoder);
    free(source);
    free(pcm);
    free(stream);
    return SDL_AtomicGet(&g_stop_playback) ? 0 : result;
}

static int play_flac_reader(stream_read_fn read_stream, void *read_context, uint64_t *output_frames)
{
    auto *pcm = static_cast<int16_t *>(malloc(FLAC_PCM_BUFFER_SAMPLES * sizeof(int16_t)));
    if (pcm == nullptr)
        return -1;

    flac_decoder_t decoder;
    memset(&decoder, 0, sizeof(decoder));
    audio_sink_t sink;
    memset(&sink, 0, sizeof(sink));
    sink.handle = -1;
    int result = flac_decoder_open(&decoder, read_stream, read_context);
    if (result >= 0)
    {
        result = sink_open(&sink, decoder.sample_rate, decoder.channels);
        if (result >= 0)
        {
            set_playback_state(RADIO_PLAYBACK_BUFFERING, 0, decoder.sample_rate, decoder.channels);
        }
    }
    while (result >= 0 && !SDL_AtomicGet(&g_stop_playback))
    {
        size_t samples = 0U;
        result = flac_decoder_read_pcm(&decoder, pcm, FLAC_PCM_BUFFER_SAMPLES, &samples);
        if (result == FLAC_DECODER_EOF)
        {
            result = -3;
            break;
        }
        if (result < 0)
            break;
        if (samples != 0U)
        {
            result = sink_push_pcm(&sink, pcm, (unsigned)samples);
        }
    }

    if (result < 0 || SDL_AtomicGet(&g_stop_playback))
        sink_cancel(&sink);
    *output_frames = sink_close(&sink);
    flac_decoder_close(&decoder);
    free(pcm);
    return SDL_AtomicGet(&g_stop_playback) ? 0 : result;
}

static int play_ogg_flac_reader(stream_read_fn read_stream, void *read_context,
                                uint64_t *output_frames)
{
    auto *source = static_cast<ogg_stream_t *>(malloc(sizeof(ogg_stream_t)));
    if (source == nullptr)
        return -1;
    ogg_stream_init(source, read_stream, read_context);
    int result = play_flac_reader(ogg_stream_read, source, output_frames);
    const ogg_stream_result_t status = ogg_stream_status(source);
    if (status < OGG_STREAM_OK)
        result = OGG_STREAM_PLAYBACK_ERROR_BASE + (int)status;
    free(source);
    return result;
}

static size_t find_adts(const uint8_t *data, size_t size)
{
    for (size_t i = 0; i + 1U < size; ++i)
    {
        if (data[i] == 0xffU && (data[i + 1U] & 0xf6U) == 0xf0U)
            return i;
    }
    return size;
}

struct hls_reader_t
{
    radio_hls_playlist_t *playlist;
    radio_ts_aac_parser_t *transport;
    uint8_t *network;
    uint8_t *output;
    size_t output_at;
    size_t output_size;
    char playlist_url[RADIO_HLS_URL_BYTES];
    uint64_t next_sequence;
    uint64_t segment_sequence;
    uint32_t reload_at;
    unsigned source_channels;
    bool discontinuity_pending;
    int connection;
    int request;
};

static void hls_request_close(hls_reader_t *reader)
{
    if (reader->request >= 0)
        playback_request_clear(reader->request);
    http_close(reader->connection, reader->request);
    reader->connection = -1;
    reader->request = -1;
}

static int hls_fetch_playlist(const char *initial_url, radio_hls_playlist_t *playlist,
                              char *media_url, size_t media_url_size, unsigned *source_channels)
{
    char current[RADIO_HLS_URL_BYTES];
    SDL_strlcpy(current, initial_url, sizeof(current));
    auto *document = static_cast<char *>(malloc(HLS_PLAYLIST_BUFFER_SIZE));
    if (document == nullptr)
        return -1;

    int result = HLS_ERROR_PLAYLIST;
    unsigned selected_channels = 0U;
    for (unsigned depth = 0U; depth <= HLS_MASTER_LIMIT; ++depth)
    {
        int connection = -1;
        int request = -1;
        result = http_open(current, true, "AAC", &connection, &request, nullptr);
        size_t document_size = 0U;
        if (result >= 0)
            result =
                read_playlist_document(request, document, HLS_PLAYLIST_BUFFER_SIZE, &document_size);
        if (request >= 0)
            playback_request_clear(request);
        http_close(connection, request);
        if (result < 0 || SDL_AtomicGet(&g_stop_playback))
            break;

        const radio_hls_result_t parsed =
            radio_hls_parse(document, document_size, current, playlist);
        if (parsed != RADIO_HLS_OK)
        {
            result = HLS_ERROR_PLAYLIST + (int)parsed;
            break;
        }
        if (playlist->kind == RADIO_HLS_MEDIA)
        {
            SDL_strlcpy(media_url, current, media_url_size);
            if (source_channels != nullptr)
                *source_channels = selected_channels;
            result = 0;
            break;
        }
        const int selected = radio_hls_select_variant(playlist);
        if (selected < 0)
        {
            result = HLS_ERROR_PLAYLIST;
            break;
        }
        if (playlist->variants[selected].source_channels != 0U)
            selected_channels = playlist->variants[selected].source_channels;
        SDL_strlcpy(current, playlist->variants[selected].url, sizeof(current));
    }
    free(document);
    return SDL_AtomicGet(&g_stop_playback) ? 0 : result;
}

static int hls_output_ready(const uint8_t *data, size_t size, void *user_data)
{
    auto *reader = static_cast<hls_reader_t *>(user_data);
    if (reader->output_size + size > HLS_OUTPUT_BUFFER_SIZE)
        return -1;
    memcpy(reader->output + reader->output_size, data, size);
    reader->output_size += size;
    return 0;
}

static unsigned hls_live_edge(const radio_hls_playlist_t *playlist)
{
    return playlist->segment_count > HLS_LIVE_EDGE_SEGMENTS
               ? playlist->segment_count - HLS_LIVE_EDGE_SEGMENTS
               : 0U;
}

static int hls_reader_open(hls_reader_t *reader, const char *url)
{
    memset(reader, 0, sizeof(*reader));
    reader->connection = -1;
    reader->request = -1;
    reader->playlist = static_cast<radio_hls_playlist_t *>(malloc(sizeof(radio_hls_playlist_t)));
    reader->transport = static_cast<radio_ts_aac_parser_t *>(malloc(sizeof(radio_ts_aac_parser_t)));
    reader->network = static_cast<uint8_t *>(malloc(HLS_NETWORK_BUFFER_SIZE));
    reader->output = static_cast<uint8_t *>(malloc(HLS_OUTPUT_BUFFER_SIZE));
    if (reader->playlist == nullptr || reader->transport == nullptr || reader->network == nullptr ||
        reader->output == nullptr)
        return -1;

    int result = hls_fetch_playlist(url, reader->playlist, reader->playlist_url,
                                    sizeof(reader->playlist_url), &reader->source_channels);
    if (result < 0 || SDL_AtomicGet(&g_stop_playback))
        return result;
    const unsigned first = reader->playlist->is_live != 0U ? hls_live_edge(reader->playlist) : 0U;
    reader->next_sequence = reader->playlist->segments[first].sequence;
    radio_ts_aac_init(reader->transport, hls_output_ready, reader);
    return 0;
}

static void hls_reader_close(hls_reader_t *reader)
{
    hls_request_close(reader);
    free(reader->output);
    free(reader->network);
    free(reader->transport);
    free(reader->playlist);
    memset(reader, 0, sizeof(*reader));
    reader->connection = -1;
    reader->request = -1;
}

static int hls_wait(unsigned milliseconds)
{
    for (unsigned waited = 0U; waited < milliseconds; waited += 25U)
    {
        if (SDL_AtomicGet(&g_stop_playback))
            return 0;
        SDL_Delay(milliseconds - waited < 25U ? milliseconds - waited : 25U);
    }
    return SDL_AtomicGet(&g_stop_playback) ? 0 : 1;
}

static int hls_reload(hls_reader_t *reader)
{
    const uint32_t now = SDL_GetTicks();
    if ((int32_t)(reader->reload_at - now) > 0 && !hls_wait(reader->reload_at - now))
        return 0;
    const int result =
        hls_fetch_playlist(reader->playlist_url, reader->playlist, reader->playlist_url,
                           sizeof(reader->playlist_url), nullptr);
    if (result < 0 || SDL_AtomicGet(&g_stop_playback))
        return result;
    const uint32_t delay = reader->playlist->target_duration_ms / 2U;
    reader->reload_at = SDL_GetTicks() + (delay < 500U ? 500U : delay);

    const uint64_t first = reader->playlist->segments[0].sequence;
    const uint64_t last = reader->playlist->segments[reader->playlist->segment_count - 1U].sequence;
    if (reader->next_sequence < first || reader->next_sequence > last + 1U)
    {
        const unsigned edge = hls_live_edge(reader->playlist);
        reader->next_sequence = reader->playlist->segments[edge].sequence;
        radio_ts_aac_reset(reader->transport);
        reader->discontinuity_pending = true;
    }
    return 1;
}

static int hls_find_segment(const hls_reader_t *reader)
{
    for (uint32_t i = 0U; i < reader->playlist->segment_count; ++i)
    {
        if (reader->playlist->segments[i].sequence == reader->next_sequence)
            return (int)i;
    }
    return -1;
}

static int hls_take_discontinuity(hls_reader_t *reader)
{
    if (!reader->discontinuity_pending)
        return 0;
    reader->discontinuity_pending = false;
    return STREAM_READ_DISCONTINUITY;
}

static int hls_stream_read(void *context, void *data, size_t capacity)
{
    auto *reader = static_cast<hls_reader_t *>(context);
    while (!SDL_AtomicGet(&g_stop_playback))
    {
        const int boundary = hls_take_discontinuity(reader);
        if (boundary != 0)
            return boundary;
        if (reader->output_at < reader->output_size)
        {
            const size_t available = reader->output_size - reader->output_at;
            const size_t copy = available < capacity ? available : capacity;
            memcpy(data, reader->output + reader->output_at, copy);
            reader->output_at += copy;
            if (reader->output_at == reader->output_size)
            {
                reader->output_at = 0U;
                reader->output_size = 0U;
            }
            return (int)copy;
        }

        if (reader->request < 0)
        {
            int segment = hls_find_segment(reader);
            if (segment < 0)
            {
                if (reader->playlist->is_live == 0U)
                    return 0;
                const int reloaded = hls_reload(reader);
                if (reloaded <= 0)
                    return reloaded;
                segment = hls_find_segment(reader);
                if (segment < 0)
                    continue;
            }
            const radio_hls_segment_t *item = &reader->playlist->segments[segment];
            if (item->discontinuity != 0U)
            {
                radio_ts_aac_reset(reader->transport);
                reader->discontinuity_pending = true;
            }
            const int result =
                http_open(item->url, true, "AAC", &reader->connection, &reader->request, nullptr);
            if (result < 0)
                return result;
            reader->segment_sequence = item->sequence;
            if (reader->discontinuity_pending)
                continue;
        }

        const int received =
            sceHttpReadData(reader->request, reader->network, HLS_NETWORK_BUFFER_SIZE);
        if (received < 0)
        {
            hls_request_close(reader);
            return received;
        }
        if (received == 0)
        {
            hls_request_close(reader);
            reader->next_sequence = reader->segment_sequence + 1U;
            continue;
        }
        reader->output_at = 0U;
        reader->output_size = 0U;
        const radio_ts_aac_result_t parsed =
            radio_ts_aac_feed(reader->transport, reader->network, (size_t)received);
        if (parsed != RADIO_TS_AAC_OK)
            return HLS_ERROR_TRANSPORT + (int)parsed;
    }
    return 0;
}

static int play_audiodec_reader(stream_read_fn read_stream, void *read_context,
                                unsigned source_channels, bool mp3, uint64_t *output_frames)
{
    auto *stream = static_cast<uint8_t *>(malloc(STREAM_BUFFER_SIZE));
    auto *pcm = static_cast<uint8_t *>(malloc(PCM_BUFFER_SIZE));
    if (stream == nullptr || pcm == nullptr)
    {
        free(stream);
        free(pcm);
        return -1;
    }

    const uint32_t codec_type = mp3 ? AUDIODEC_MP3 : AUDIODEC_AAC;
    int result = sceSysmoduleLoadModule(0x0088);
    const bool module_loaded = result >= 0;
    if (module_loaded)
        result = sceAudiodecInitLibrary(codec_type);
    const bool library_initialized = result >= 0;
    sce_audiodec_param_mp3_t mp3_param = {sizeof(mp3_param), AUDIODEC_WORD_S16};
    sce_audiodec_mp3_info_t mp3_info;
    memset(&mp3_info, 0, sizeof(mp3_info));
    mp3_info.size = sizeof(mp3_info);
    sce_audiodec_param_aac_t aac_param = {sizeof(aac_param), AUDIODEC_WORD_S16, 1, 4, 2, 1};
    sce_audiodec_aac_info_t aac_info;
    memset(&aac_info, 0, sizeof(aac_info));
    aac_info.size = sizeof(aac_info);
    sce_audiodec_au_info_t au;
    memset(&au, 0, sizeof(au));
    au.size = sizeof(au);
    sce_audiodec_pcm_item_t pcm_item;
    memset(&pcm_item, 0, sizeof(pcm_item));
    pcm_item.size = sizeof(pcm_item);
    sce_audiodec_ctrl_t ctrl = {mp3 ? (void *)&mp3_param : (void *)&aac_param,
                                mp3 ? (void *)&mp3_info : (void *)&aac_info, &au, &pcm_item};
    int decoder = -1;
    if (result >= 0)
    {
        decoder = sceAudiodecCreateDecoder(&ctrl, codec_type);
        if (decoder < 0)
            result = decoder;
    }

    audio_sink_t sink;
    memset(&sink, 0, sizeof(sink));
    sink.handle = -1;
    size_t buffered = 0;
    size_t scanned = 0;
    uint64_t completed_output_frames = 0U;
    bool reset_requested = false;
    while (result >= 0 && !SDL_AtomicGet(&g_stop_playback))
    {
        if (reset_requested)
        {
            completed_output_frames += sink_close(&sink);
            if (decoder >= 0)
                sceAudiodecDeleteDecoder(decoder);
            decoder = -1;
            aac_param.enable_he_aac = 1U;
            memset(&aac_info, 0, sizeof(aac_info));
            aac_info.size = sizeof(aac_info);
            memset(&mp3_info, 0, sizeof(mp3_info));
            mp3_info.size = sizeof(mp3_info);
            decoder = sceAudiodecCreateDecoder(&ctrl, codec_type);
            if (decoder < 0)
            {
                result = decoder;
                break;
            }
            buffered = 0U;
            scanned = 0U;
            reset_requested = false;
        }
        const size_t minimum_header = mp3 ? 4U : 7U;
        if (buffered < minimum_header)
        {
            const int read =
                read_stream(read_context, stream + buffered, STREAM_BUFFER_SIZE - buffered);
            if (read == STREAM_READ_DISCONTINUITY)
            {
                reset_requested = true;
                continue;
            }
            if (read < 0)
            {
                result = read;
                break;
            }
            if (read == 0)
            {
                result = -3;
                break;
            }
            buffered += (size_t)read;
        }

        mp3_header_t mp3_header;
        const size_t sync =
            mp3 ? mp3_header_find(stream, buffered, &mp3_header) : find_adts(stream, buffered);
        if (sync != 0)
        {
            const size_t tail = minimum_header - 1U;
            const size_t remove =
                sync == buffered ? (buffered > tail ? buffered - tail : 0U) : sync;
            if (remove != 0)
            {
                memmove(stream, stream + remove, buffered - remove);
                buffered -= remove;
                scanned += remove;
                if (scanned > 256U * 1024U)
                {
                    result = -4;
                    break;
                }
            }
            continue;
        }

        const size_t frame_length = mp3 ? mp3_header.frame_bytes
                                        : (((size_t)(stream[3] & 0x03U) << 11) |
                                           ((size_t)stream[4] << 3) | ((size_t)stream[5] >> 5));
        if (frame_length < minimum_header || frame_length > 4608U)
        {
            memmove(stream, stream + 1, --buffered);
            if (++scanned > 256U * 1024U)
                result = -4;
            continue;
        }
        while (buffered < frame_length && buffered < STREAM_BUFFER_SIZE &&
               !SDL_AtomicGet(&g_stop_playback))
        {
            const int read =
                read_stream(read_context, stream + buffered, STREAM_BUFFER_SIZE - buffered);
            if (read == STREAM_READ_DISCONTINUITY)
            {
                reset_requested = true;
                break;
            }
            if (read < 0)
            {
                result = read;
                break;
            }
            if (read == 0)
            {
                result = -3;
                break;
            }
            buffered += (size_t)read;
        }
        if (reset_requested)
            continue;
        if (result < 0 || buffered < frame_length)
            break;

        au.address = stream;
        au.length = (uint32_t)frame_length;
        pcm_item.address = pcm;
        pcm_item.length = PCM_BUFFER_SIZE;
        result = sceAudiodecDecode(decoder, &ctrl);
        if (result >= 0 && pcm_item.length > PCM_BUFFER_SIZE)
            result = -6;
        if (mp3 && result >= 0 &&
            (au.length != frame_length ||
             pcm_item.length % (mp3_header.channels * sizeof(int16_t)) != 0U))
        {
            result = -6;
        }
        if (!mp3 && result >= 0 &&
            aac_should_disable_he(stream, frame_length, source_channels,
                                  aac_info.sampling_frequency, aac_info.channel_count,
                                  aac_info.he_aac))
        {
            sceAudiodecDeleteDecoder(decoder);
            decoder = -1;
            aac_param.enable_he_aac = 0;
            memset(&aac_info, 0, sizeof(aac_info));
            aac_info.size = sizeof(aac_info);
            decoder = sceAudiodecCreateDecoder(&ctrl, codec_type);
            if (decoder < 0)
                result = decoder;
            else
            {
                au.address = stream;
                au.length = (uint32_t)frame_length;
                pcm_item.address = pcm;
                pcm_item.length = PCM_BUFFER_SIZE;
                result = sceAudiodecDecode(decoder, &ctrl);
            }
        }
        if (result >= 0 && pcm_item.length != 0)
        {
            if (sink.handle < 0)
            {
                const uint32_t pcm_rate =
                    mp3 ? mp3_header.sample_rate
                        : aac_pcm_rate(stream, frame_length, aac_info.channel_count,
                                       pcm_item.length, aac_info.sampling_frequency);
                const uint32_t channels = mp3 ? mp3_header.channels : aac_info.channel_count;
                result = sink_open(&sink, pcm_rate, channels);
                if (result >= 0)
                {
                    set_playback_state(RADIO_PLAYBACK_BUFFERING, 0, pcm_rate, channels);
                }
            }
            if (result >= 0)
            {
                result =
                    sink_push_pcm(&sink, (const int16_t *)pcm, pcm_item.length / sizeof(int16_t));
            }
        }
        if (result >= 0)
            scanned = 0U;
        memmove(stream, stream + frame_length, buffered - frame_length);
        buffered -= frame_length;
    }

    if (result < 0 || SDL_AtomicGet(&g_stop_playback))
        sink_cancel(&sink);
    *output_frames = completed_output_frames + sink_close(&sink);
    if (decoder >= 0)
        sceAudiodecDeleteDecoder(decoder);
    if (library_initialized)
        sceAudiodecTermLibrary(codec_type);
    if (module_loaded)
        sceSysmoduleUnloadModule(0x0088);
    free(pcm);
    free(stream);
    return result;
}

static int play_stream(const radio_station_t *station, uint64_t *output_frames)
{
    *output_frames = 0U;
    int connection = -1;
    int request = -1;
    char resolved_url[sizeof(station->url)];
    const int mode =
        open_resolved_stream(station, &connection, &request, resolved_url, sizeof(resolved_url));
    if (mode < 0)
        return mode;
    set_playback_state(RADIO_PLAYBACK_BUFFERING, 0, 0, 0);

    if (mode == STREAM_OPEN_HLS)
    {
        hls_reader_t reader;
        int result = hls_reader_open(&reader, resolved_url);
        if (result >= 0 && !SDL_AtomicGet(&g_stop_playback))
        {
            result = play_audiodec_reader(hls_stream_read, &reader, reader.source_channels, false,
                                          output_frames);
        }
        hls_reader_close(&reader);
        return result;
    }

    const unsigned source_channels = http_audio_channels(request);
    icy_metadata_reader_t metadata_reader;
    stream_read_fn direct_read = http_stream_read;
    void *direct_context = &request;
    const size_t metadata_interval = http_icy_metadata_interval(request);
    if (metadata_interval != 0U)
    {
        icy_metadata_reader_init(&metadata_reader, http_stream_read, &request, metadata_interval);
        direct_read = icy_metadata_read;
        direct_context = &metadata_reader;
    }
    int result;
    if (strcasecmp(station->codec, "OPUS") == 0)
        result = play_opus_reader(direct_read, direct_context, output_frames);
    else if (strcasecmp(station->codec, "VORBIS") == 0)
        result = play_vorbis_reader(direct_read, direct_context, output_frames);
    else if (strcasecmp(station->codec, "FLAC") == 0)
    {
        prefixed_http_reader_t reader;
        const int format = prefixed_http_open(&reader, direct_read, direct_context);
        if (format == OGG_FORMAT_FLAC)
            result = play_ogg_flac_reader(prefixed_http_read, &reader, output_frames);
        else
            result = play_flac_reader(prefixed_http_read, &reader, output_frames);
    }
    else if (strcasecmp(station->codec, "OGG") == 0)
    {
        prefixed_http_reader_t reader;
        const int format = prefixed_http_open(&reader, direct_read, direct_context);
        if (format == OGG_FORMAT_OPUS)
            result = play_opus_reader(prefixed_http_read, &reader, output_frames);
        else if (format == OGG_FORMAT_VORBIS)
            result = play_vorbis_reader(prefixed_http_read, &reader, output_frames);
        else if (format == OGG_FORMAT_FLAC)
            result = play_ogg_flac_reader(prefixed_http_read, &reader, output_frames);
        else
            result = format < 0 ? format : VORBIS_DECODER_UNSUPPORTED;
    }
    else
    {
        const bool mp3 = strcasecmp(station->codec, "MP3") == 0;
        result =
            play_audiodec_reader(direct_read, direct_context, source_channels, mp3, output_frames);
    }
    playback_request_clear(request);
    http_close(connection, request);
    return result;
}

static void *playback_thread(void *station_copy)
{
    auto *station = static_cast<radio_station_t *>(station_copy);
    int result = -1;
    unsigned failures = 0U;
    for (;;)
    {
        uint64_t output_frames = 0U;
        result = play_stream(station, &output_frames);
        if (result >= 0 || SDL_AtomicGet(&g_stop_playback) || SDL_AtomicGet(&g_shutting_down))
            break;
        const bool stable_playback = playback_retry_is_stable(output_frames, AUDIO_OUT_RATE);
        failures = playback_retry_next_failures(failures, stable_playback);
        if (!playback_retry_allowed(failures))
            break;
        set_playback_state(RADIO_PLAYBACK_BUFFERING, 0, 0, 0);
        const unsigned delay = playback_retry_delay_ms(failures);
        for (unsigned waited = 0;
             waited < delay && !SDL_AtomicGet(&g_stop_playback) && !SDL_AtomicGet(&g_shutting_down);
             waited += 25U)
        {
            SDL_Delay(25U);
        }
    }
    free(station);
    if (SDL_AtomicGet(&g_stop_playback) || SDL_AtomicGet(&g_shutting_down))
    {
        set_playback_state(RADIO_PLAYBACK_STOPPED, 0, 0, 0);
    }
    else
    {
        set_playback_state(RADIO_PLAYBACK_ERROR, result < 0 ? result : -5, 0, 0);
    }
    SDL_AtomicSet(&g_playback_running, 0);
    return nullptr;
}

bool radio_service_init(void)
{
    memset(&g_status, 0, sizeof(g_status));
    memset(g_stations, 0, sizeof(g_stations));
    memset(&g_playing_station, 0, sizeof(g_playing_station));
    g_have_playing_station = false;
    g_station_count = 0U;
    g_status.playing_index = UINT_MAX;
    memset(g_facets, 0, sizeof(g_facets));
    memset(g_facet_count, 0, sizeof(g_facet_count));
    memset(g_mirrors, 0, sizeof(g_mirrors));
    g_mirror_count = 0U;
    g_mirror_index = 0U;
    g_pending_task_ready = false;
    g_state_mutex = SDL_CreateMutex();
    g_request_mutex = SDL_CreateMutex();
    g_store_mutex = SDL_CreateMutex();
    if (g_state_mutex == nullptr || g_request_mutex == nullptr || g_store_mutex == nullptr)
    {
        if (g_store_mutex != nullptr)
            SDL_DestroyMutex(g_store_mutex);
        if (g_request_mutex != nullptr)
            SDL_DestroyMutex(g_request_mutex);
        if (g_state_mutex != nullptr)
            SDL_DestroyMutex(g_state_mutex);
        g_store_mutex = nullptr;
        g_request_mutex = nullptr;
        g_state_mutex = nullptr;
        return false;
    }
    SDL_AtomicSet(&g_shutting_down, 0);
    SDL_AtomicSet(&g_stop_playback, 0);
    SDL_AtomicSet(&g_playback_running, 0);
    SDL_AtomicSet(&g_refresh_running, 0);
    g_playback_request = -1;
    add_mirror("all.api.radio-browser.info");
    g_catalog_page_cache = SDL_malloc(CATALOG_PAGE_CACHE_SIZE);
    if (g_catalog_page_cache == nullptr ||
        !radio_catalog_store_global_init(g_catalog_page_cache, CATALOG_PAGE_CACHE_SIZE))
    {
        SDL_free(g_catalog_page_cache);
        g_catalog_page_cache = nullptr;
    }
    if (!open_catalog_store_with_recovery())
    {
        g_status.catalog_state = RADIO_CATALOG_ERROR;
        g_status.error_code = radio_catalog_store_error(&g_catalog_store);
        g_status.playback_state = RADIO_PLAYBACK_STOPPED;
        return false;
    }
    migrate_legacy_catalog();
    load_favorites();
    const bool cached = load_catalog_summary(RADIO_CATALOG_CACHED);
    load_facet_snapshots();
    if (!cached)
        g_status.catalog_state = RADIO_CATALOG_LOADING;
    g_status.playback_state = RADIO_PLAYBACK_STOPPED;

    const int network_error = network_init();
    if (network_error < 0)
    {
        g_status.error_code = network_error;
        if (!cached)
            g_status.catalog_state = RADIO_CATALOG_ERROR;
        return cached;
    }
    int64_t last_sync = 0;
    SDL_LockMutex(g_store_mutex);
    const bool have_sync_time =
        radio_catalog_store_get_meta(&g_catalog_store, CATALOG_SYNC_TIME_META, &last_sync);
    SDL_UnlockMutex(g_store_mutex);
    const int64_t now = (int64_t)time(nullptr);
    const bool stale = !cached || !have_sync_time || last_sync <= 0 || now <= last_sync ||
                       now - last_sync >= CATALOG_REFRESH_SECONDS;
    return (stale ? radio_service_refresh() : true) || cached;
}

void radio_service_shutdown(void)
{
    if (g_state_mutex == nullptr)
        return;
    SDL_AtomicSet(&g_shutting_down, 1);
    radio_service_stop();
    while (SDL_AtomicGet(&g_refresh_running) || SDL_AtomicGet(&g_playback_running))
    {
        SDL_Delay(10);
    }
    network_shutdown();
    SDL_LockMutex(g_store_mutex);
    radio_catalog_store_close(&g_catalog_store);
    SDL_UnlockMutex(g_store_mutex);
    radio_catalog_store_global_shutdown();
    SDL_free(g_catalog_page_cache);
    g_catalog_page_cache = nullptr;
    free(g_favorites);
    g_favorites = nullptr;
    memset(g_stations, 0, sizeof(g_stations));
    memset(&g_playing_station, 0, sizeof(g_playing_station));
    g_have_playing_station = false;
    g_favorite_count = 0U;
    g_favorite_capacity = 0U;
    g_station_count = 0U;
    SDL_DestroyMutex(g_store_mutex);
    SDL_DestroyMutex(g_request_mutex);
    SDL_DestroyMutex(g_state_mutex);
    g_store_mutex = nullptr;
    g_request_mutex = nullptr;
    g_state_mutex = nullptr;
}

void radio_service_get_status(radio_service_status_t *out_status)
{
    if (out_status == nullptr)
        return;
    SDL_LockMutex(g_state_mutex);
    *out_status = g_status;
    SDL_UnlockMutex(g_state_mutex);
}

bool radio_service_get_station(unsigned index, radio_station_t *out_station)
{
    if (out_station == nullptr)
        return false;
    SDL_LockMutex(g_state_mutex);
    const bool found = index < g_station_count;
    if (found)
        *out_station = g_stations[index];
    SDL_UnlockMutex(g_state_mutex);
    return found;
}

bool radio_service_query_page(const radio_catalog_query_t *query, radio_catalog_order_t order,
                              bool favorites_only, unsigned offset, unsigned limit,
                              unsigned *out_total)
{
    if (g_store_mutex == nullptr || limit == 0U || limit > CATALOG_VIEW_CAPACITY ||
        order < RADIO_CATALOG_ORDER_POPULAR || order > RADIO_CATALOG_ORDER_NAME)
        return false;
    radio_station_t stations[CATALOG_VIEW_CAPACITY];
    memset(stations, 0, sizeof(stations));
    SDL_LockMutex(g_store_mutex);
    const size_t total = radio_catalog_store_query_count(&g_catalog_store, query, favorites_only);
    const size_t loaded = radio_catalog_store_query_stations(
        &g_catalog_store, query, order, favorites_only, offset, stations, limit);
    const int store_error = radio_catalog_store_error(&g_catalog_store);
    SDL_UnlockMutex(g_store_mutex);
    if (store_error != 0 || total > UINT_MAX || loaded > UINT_MAX)
        return false;

    SDL_LockMutex(g_state_mutex);
    memcpy(g_stations, stations, loaded * sizeof(stations[0]));
    if (loaded < CATALOG_VIEW_CAPACITY)
        memset(g_stations + loaded, 0, (CATALOG_VIEW_CAPACITY - loaded) * sizeof(g_stations[0]));
    g_station_count = (unsigned)loaded;
    g_status.station_count = g_station_count;
    g_status.playing_index = UINT_MAX;
    if (g_have_playing_station)
    {
        const int playing = station_index(g_stations, g_station_count, g_playing_station.uuid);
        if (playing >= 0)
            g_status.playing_index = (unsigned)playing;
    }
    SDL_UnlockMutex(g_state_mutex);
    if (out_total != nullptr)
        *out_total = (unsigned)total;
    return true;
}

bool radio_service_station_is_playing(unsigned index)
{
    SDL_LockMutex(g_state_mutex);
    const bool active = g_status.playback_state != RADIO_PLAYBACK_STOPPED &&
                        g_status.playback_state != RADIO_PLAYBACK_ERROR;
    const bool playing = active && g_have_playing_station && index < g_station_count &&
                         strcmp(g_stations[index].uuid, g_playing_station.uuid) == 0;
    SDL_UnlockMutex(g_state_mutex);
    return playing;
}

bool radio_service_get_playing_station(radio_station_t *out_station)
{
    if (out_station == nullptr)
        return false;
    SDL_LockMutex(g_state_mutex);
    const bool found = g_have_playing_station;
    if (found)
        *out_station = g_playing_station;
    SDL_UnlockMutex(g_state_mutex);
    return found;
}

unsigned radio_service_get_facet_count(radio_facet_kind_t kind)
{
    if (kind < RADIO_FACET_COUNTRY || kind > RADIO_FACET_LANGUAGE)
        return 0U;
    SDL_LockMutex(g_state_mutex);
    const unsigned count = g_facet_count[(unsigned)kind];
    SDL_UnlockMutex(g_state_mutex);
    return count;
}

bool radio_service_get_facet(radio_facet_kind_t kind, unsigned index, radio_facet_t *out_facet)
{
    if (out_facet == nullptr || kind < RADIO_FACET_COUNTRY || kind > RADIO_FACET_LANGUAGE)
        return false;
    SDL_LockMutex(g_state_mutex);
    const bool found = index < g_facet_count[(unsigned)kind];
    if (found)
    {
        *out_facet = g_facets[(unsigned)kind][index];
        if (kind == RADIO_FACET_COUNTRY)
            normalize_country_label(out_facet);
    }
    SDL_UnlockMutex(g_state_mutex);
    return found;
}

bool radio_service_is_favorite(const char *uuid)
{
    if (uuid == nullptr)
        return false;
    SDL_LockMutex(g_state_mutex);
    const bool found = favorite_unlocked(uuid);
    SDL_UnlockMutex(g_state_mutex);
    return found;
}

bool radio_service_toggle_favorite(unsigned station_index)
{
    SDL_LockMutex(g_state_mutex);
    if (station_index >= g_station_count)
    {
        SDL_UnlockMutex(g_state_mutex);
        return false;
    }
    char uuid[40];
    SDL_strlcpy(uuid, g_stations[station_index].uuid, sizeof(uuid));
    const bool now_favorite = !favorite_unlocked(uuid);
    const bool have_capacity = !now_favorite || ensure_favorite_capacity(g_favorite_count + 1U);
    SDL_UnlockMutex(g_state_mutex);
    if (!have_capacity)
        return false;

    SDL_LockMutex(g_state_mutex);
    if (now_favorite)
    {
        if (!favorite_unlocked(uuid))
            SDL_strlcpy(g_favorites[g_favorite_count++], uuid, sizeof(g_favorites[0]));
    }
    else
    {
        for (unsigned i = 0U; i < g_favorite_count; ++i)
        {
            if (strcmp(g_favorites[i], uuid) != 0)
                continue;
            if (i + 1U < g_favorite_count)
                memmove(g_favorites[i], g_favorites[i + 1U],
                        (g_favorite_count - i - 1U) * sizeof(g_favorites[0]));
            --g_favorite_count;
            break;
        }
    }
    SDL_UnlockMutex(g_state_mutex);
    save_favorites_file();
    SDL_LockMutex(g_store_mutex);
    radio_catalog_store_set_favorite(&g_catalog_store, uuid, now_favorite);
    SDL_UnlockMutex(g_store_mutex);
    return now_favorite;
}

static bool start_catalog_task(const catalog_task_t *task)
{
    if (task == nullptr || g_http_template < 0 || SDL_AtomicGet(&g_shutting_down))
        return false;
    auto *thread_task = static_cast<catalog_task_t *>(malloc(sizeof(catalog_task_t)));
    if (thread_task == nullptr)
        return false;
    *thread_task = *task;

    SDL_LockMutex(g_state_mutex);
    if (SDL_AtomicGet(&g_refresh_running))
    {
        g_pending_task = *task;
        g_pending_task_ready = true;
        SDL_UnlockMutex(g_state_mutex);
        free(thread_task);
        return true;
    }
    SDL_AtomicSet(&g_refresh_running, 1);
    g_status.refreshing = true;
    g_status.searching = !task->full_sync;
    g_status.sync_station_count = 0U;
    if (g_status.catalog_size == 0U)
        g_status.catalog_state = RADIO_CATALOG_LOADING;
    g_status.error_code = 0;
    SDL_UnlockMutex(g_state_mutex);

    pthread_attr_t attributes;
    const int attr_result = pthread_attr_init(&attributes);
    const int stack_result = attr_result == 0
                                 ? pthread_attr_setstacksize(&attributes, CATALOG_THREAD_STACK_SIZE)
                                 : attr_result;
    void *thread = nullptr;
    const int create_result =
        stack_result == 0
            ? scePthreadCreate(&thread, &attributes, refresh_thread, thread_task, "radio-catalog")
            : stack_result;
    if (attr_result == 0)
        pthread_attr_destroy(&attributes);
    if (create_result != 0)
    {
        free(thread_task);
        SDL_AtomicSet(&g_refresh_running, 0);
        SDL_LockMutex(g_state_mutex);
        g_status.refreshing = false;
        if (g_status.catalog_size == 0U)
            g_status.catalog_state = RADIO_CATALOG_ERROR;
        SDL_UnlockMutex(g_state_mutex);
        return false;
    }
    scePthreadDetach(thread);
    return true;
}

bool radio_service_refresh(void)
{
    catalog_task_t task;
    memset(&task, 0, sizeof(task));
    task.full_sync = true;
    return start_catalog_task(&task);
}

bool radio_service_search(const radio_catalog_query_t *query)
{
    if (query == nullptr)
        return false;
    catalog_task_t task;
    memset(&task, 0, sizeof(task));
    task.query = *query;
    return start_catalog_task(&task);
}

void radio_service_play(unsigned station_index)
{
    if (SDL_AtomicGet(&g_playback_running))
    {
        SDL_LockMutex(g_state_mutex);
        const bool finishing = g_status.playback_state == RADIO_PLAYBACK_STOPPED ||
                               g_status.playback_state == RADIO_PLAYBACK_ERROR;
        SDL_UnlockMutex(g_state_mutex);
        if (finishing)
        {
            while (SDL_AtomicGet(&g_playback_running))
                SDL_Delay(1);
        }
        else
        {
            radio_service_stop();
            return;
        }
    }
    auto *station = static_cast<radio_station_t *>(malloc(sizeof(radio_station_t)));
    if (station == nullptr)
        return;
    SDL_LockMutex(g_state_mutex);
    if (station_index >= g_station_count)
    {
        SDL_UnlockMutex(g_state_mutex);
        free(station);
        return;
    }
    *station = g_stations[station_index];
    g_playing_station = *station;
    g_have_playing_station = true;
    g_status.playing_index = station_index;
    g_status.playback_state = RADIO_PLAYBACK_CONNECTING;
    g_status.sample_rate = 0;
    g_status.channels = 0;
    g_status.error_code = 0;
    SDL_UnlockMutex(g_state_mutex);

    SDL_AtomicSet(&g_stop_playback, 0);
    SDL_AtomicSet(&g_playback_running, 1);
    void *thread = nullptr;
    if (scePthreadCreate(&thread, nullptr, playback_thread, station, "radio-audio") != 0)
    {
        SDL_AtomicSet(&g_playback_running, 0);
        free(station);
        set_playback_state(RADIO_PLAYBACK_ERROR, -1, 0, 0);
        return;
    }
    scePthreadDetach(thread);
}

void radio_service_stop(void)
{
    if (SDL_AtomicGet(&g_playback_running))
    {
        SDL_AtomicSet(&g_stop_playback, 1);
        SDL_LockMutex(g_state_mutex);
        g_status.playback_state = RADIO_PLAYBACK_STOPPING;
        g_status.error_code = 0;
        g_status.sample_rate = 0;
        g_status.channels = 0;
        SDL_UnlockMutex(g_state_mutex);
        SDL_LockMutex(g_request_mutex);
        if (g_playback_request >= 0)
            sceHttpAbortRequest(g_playback_request);
        SDL_UnlockMutex(g_request_mutex);
    }
}
