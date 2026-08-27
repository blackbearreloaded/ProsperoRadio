#include "radio_service.h"

#include "aac_timing.h"
#include "mp3_header.h"
#include "ogg_opus.h"
#include "opus_decoder.h"
#include "pcm_queue.h"
#include "radio_hls.h"
#include "radio_playlist.h"
#include "radio_ts_aac.h"

#include "SDL.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_MAGIC UINT32_C(0x52424331)
#define FAVORITES_MAGIC UINT32_C(0x52424631)
#define CATALOG_CACHE_VERSION 7U
#define FAVORITES_VERSION 2U
#define FAVORITES_LEGACY_VERSION 1U
#define FAVORITES_LEGACY_CAPACITY 100U
#define CACHE_PATH "/download0/radio-browser-cache.bin"
#define CACHE_TEMP_PATH "/download0/radio-browser-cache.tmp"
#define FAVORITES_PATH "/download0/radio-browser-favorites.bin"
#define FAVORITES_TEMP_PATH "/download0/radio-browser-favorites.tmp"
#define CATALOG_FEED_LIMIT 80U
#define CATALOG_URL(codec, order) "https://all.api.radio-browser.info/json/stations/search?codec=" codec "&hidebroken=true&order=" order "&reverse=true&limit=80"
#define USER_AGENT "PSRadio/0.2.0 (+https://www.radio-browser.info/)"
#define JSON_CAPACITY (1024U * 1024U)
#define STREAM_BUFFER_SIZE (64U * 1024U)
#define PCM_BUFFER_SIZE (2048U * 2U * 2U)
#define OPUS_PCM_BUFFER_SIZE (5760U * 2U * sizeof(int16_t))
#define OPEN_READ_ONLY 0x0000
#define OPEN_WRITE_CREATE_TRUNCATE 0x0601
#define FILE_MODE_0666 0x01b6
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
#define AUDIO_QUEUE_BLOCKS 375U
#define AUDIO_START_BLOCKS 188U
#define AUDIO_RESTART_BLOCKS 94U
#define AUDIO_WAIT_MS 20U
#define PLAYBACK_RETRY_COUNT 3U
#define PLAYBACK_RETRY_BASE_MS 250U
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
#define OPUS_RETRYABLE_ERROR (-502)

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t checksum;
} file_header_t;

typedef struct {
    file_header_t header;
    radio_station_t stations[RADIO_MAX_STATIONS];
} cache_file_t;

typedef struct {
    file_header_t header;
    char uuids[RADIO_MAX_STATIONS][40];
} favorites_file_t;

typedef struct {
    file_header_t header;
    char uuids[FAVORITES_LEGACY_CAPACITY][40];
} favorites_legacy_file_t;

typedef struct {
    uint32_t size;
    void * address;
    uint32_t length;
} sce_audiodec_au_info_t;

typedef struct {
    uint32_t size;
    void * address;
    uint32_t length;
} sce_audiodec_pcm_item_t;

typedef struct {
    void * param;
    void * stream_info;
    sce_audiodec_au_info_t * au_info;
    sce_audiodec_pcm_item_t * pcm_item;
} sce_audiodec_ctrl_t;

typedef struct {
    uint32_t size;
    int32_t word_size;
} sce_audiodec_param_mp3_t;

typedef struct {
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
} sce_audiodec_mp3_info_t;

typedef struct {
    uint32_t size;
    int32_t word_size;
    uint32_t config_number;
    uint32_t sampling_frequency_index;
    uint32_t max_channels;
    uint32_t enable_he_aac;
} sce_audiodec_param_aac_t;

typedef struct {
    uint32_t size;
    uint32_t sampling_frequency;
    uint32_t channel_count;
    uint32_t he_aac;
    int32_t result;
} sce_audiodec_aac_info_t;

_Static_assert(sizeof(sce_audiodec_au_info_t) == 24U,
               "SceAudiodecAuInfo ABI mismatch");
_Static_assert(sizeof(sce_audiodec_pcm_item_t) == 24U,
               "SceAudiodecPcmItem ABI mismatch");
_Static_assert(sizeof(sce_audiodec_ctrl_t) == 32U,
               "SceAudiodecCtrl ABI mismatch");
_Static_assert(sizeof(sce_audiodec_param_mp3_t) == 8U,
               "SceAudiodecParamMp3 ABI mismatch");
_Static_assert(sizeof(sce_audiodec_mp3_info_t) == 20U,
               "SceAudiodecMp3Info ABI mismatch");

typedef struct {
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
    int16_t * queue_blocks;
    pcm_queue_state_t queue;
    SDL_mutex * mutex;
    SDL_cond * can_read;
    SDL_cond * can_write;
    SDL_Thread * thread;
    bool input_finished;
    bool cancel;
    int output_result;
} audio_sink_t;

typedef enum {
    FEED_POPULAR,
    FEED_TRENDING,
    FEED_VOTED
} catalog_feed_t;

typedef struct {
    const char * url;
    catalog_feed_t kind;
} catalog_feed_source_t;

static const catalog_feed_source_t CATALOG_FEEDS[] = {
    {CATALOG_URL("AAC", "clickcount"), FEED_POPULAR},
    {CATALOG_URL("MP3", "clickcount"), FEED_POPULAR},
    {CATALOG_URL("OGG", "clickcount"), FEED_POPULAR},
    {CATALOG_URL("AAC", "clicktrend"), FEED_TRENDING},
    {CATALOG_URL("MP3", "clicktrend"), FEED_TRENDING},
    {CATALOG_URL("OGG", "clicktrend"), FEED_TRENDING},
    {CATALOG_URL("AAC", "votes"), FEED_VOTED},
    {CATALOG_URL("MP3", "votes"), FEED_VOTED},
    {CATALOG_URL("OGG", "votes"), FEED_VOTED},
};

extern int sceKernelOpen(const char * path, int flags, uint16_t mode);
extern int sceKernelClose(int descriptor);
extern int64_t sceKernelRead(int descriptor, void * buffer, size_t length);
extern int64_t sceKernelWrite(int descriptor, const void * buffer, size_t length);
extern int sceKernelRename(const char * from, const char * to);
extern int sceKernelUnlink(const char * path);
extern int scePthreadCreate(void ** thread, const void * attributes,
                            void * (*entry)(void *), void * argument,
                            const char * name);
extern int scePthreadDetach(void * thread);

extern int sceNetPoolCreate(const char * name, int size, int flags);
extern int sceNetPoolDestroy(int mem_id);
extern int sceSslInit(size_t pool_size);
extern int sceSslTerm(int ssl_context_id);
extern int sceHttpInit(int net_mem_id, int ssl_context_id, size_t pool_size);
extern int sceHttpTerm(int http_context_id);
extern int sceHttpCreateTemplate(int http_context_id, const char * user_agent,
                                 int version, int auto_proxy);
extern int sceHttpDeleteTemplate(int template_id);
extern int sceHttpCreateConnectionWithURL(int template_id, const char * url,
                                          int keep_alive);
extern int sceHttpDeleteConnection(int connection_id);
extern int sceHttpCreateRequestWithURL(int connection_id, int method,
                                       const char * url, uint64_t content_length);
extern int sceHttpDeleteRequest(int request_id);
extern int sceHttpAddRequestHeader(int request_id, const char * name,
                                   const char * value, uint32_t mode);
extern int sceHttpSetAutoRedirect(int id, int enabled);
extern int sceHttpSetConnectTimeOut(int id, uint32_t usec);
extern int sceHttpSetRecvTimeOut(int id, uint32_t usec);
extern int sceHttpSetSendTimeOut(int id, uint32_t usec);
extern int sceHttpSetResolveTimeOut(int id, uint32_t usec);
extern int sceHttpSendRequest(int request_id, const void * data, size_t size);
extern int sceHttpGetStatusCode(int request_id, int * status_code);
extern int sceHttpGetAllResponseHeaders(int request_id, char ** headers, size_t * size);
extern int sceHttpReadData(int request_id, void * data, size_t size);
extern int sceHttpAbortRequest(int request_id);

extern int sceSysmoduleLoadModule(uint16_t id);
extern int sceSysmoduleUnloadModule(uint16_t id);
extern int sceAudiodecInitLibrary(uint32_t codec_type);
extern int sceAudiodecTermLibrary(uint32_t codec_type);
extern int sceAudiodecCreateDecoder(sce_audiodec_ctrl_t * ctrl, uint32_t codec_type);
extern int sceAudiodecDeleteDecoder(int handle);
extern int sceAudiodecDecode(int handle, sce_audiodec_ctrl_t * ctrl);
extern int sceAudioOutInit(void);
extern int sceAudioOutOpen(int user_id, int type, int index, uint32_t length,
                           uint32_t frequency, uint32_t format);
extern int sceAudioOutClose(int handle);
extern int sceAudioOutOutput(int handle, const void * samples);
extern int sceAudioOutSetVolume(int handle, int flags, const int * volumes);

static radio_station_t g_stations[RADIO_MAX_STATIONS];
static char g_favorites[RADIO_MAX_STATIONS][40];
static unsigned g_station_count;
static unsigned g_favorite_count;
static radio_service_status_t g_status;
static SDL_mutex * g_state_mutex;
static SDL_mutex * g_request_mutex;
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

static uint32_t checksum(const void * data, size_t size)
{
    const uint8_t * bytes = data;
    uint32_t value = UINT32_C(2166136261);
    for(size_t i = 0; i < size; ++i) {
        value = (value ^ bytes[i]) * UINT32_C(16777619);
    }
    return value;
}

static bool read_exact(const char * path, void * data, size_t size)
{
    const int fd = sceKernelOpen(path, OPEN_READ_ONLY, 0);
    if(fd < 0) return false;

    size_t done = 0;
    while(done < size) {
        const int64_t count = sceKernelRead(fd, (uint8_t *)data + done, size - done);
        if(count <= 0) break;
        done += (size_t)count;
    }
    sceKernelClose(fd);
    return done == size;
}

static bool write_atomic(const char * temporary, const char * path,
                         const void * data, size_t size)
{
    const int fd = sceKernelOpen(temporary, OPEN_WRITE_CREATE_TRUNCATE, FILE_MODE_0666);
    if(fd < 0) return false;

    size_t done = 0;
    while(done < size) {
        const int64_t count = sceKernelWrite(fd, (const uint8_t *)data + done, size - done);
        if(count <= 0) break;
        done += (size_t)count;
    }
    sceKernelClose(fd);
    if(done != size) {
        sceKernelUnlink(temporary);
        return false;
    }
    if(sceKernelRename(temporary, path) < 0) {
        sceKernelUnlink(path);
        if(sceKernelRename(temporary, path) < 0) {
            sceKernelUnlink(temporary);
            return false;
        }
    }
    return true;
}

static bool favorite_unlocked(const char * uuid)
{
    for(unsigned i = 0; i < g_favorite_count; ++i) {
        if(strcmp(g_favorites[i], uuid) == 0) return true;
    }
    return false;
}

static bool save_favorites_unlocked(void);

static void load_favorites(void)
{
    favorites_file_t file;
    if(read_exact(FAVORITES_PATH, &file, sizeof(file)) &&
       file.header.magic == FAVORITES_MAGIC &&
       file.header.version == FAVORITES_VERSION &&
       file.header.count <= RADIO_MAX_STATIONS &&
       file.header.checksum == checksum(file.uuids, sizeof(file.uuids))) {
        g_favorite_count = file.header.count;
        memcpy(g_favorites, file.uuids, sizeof(g_favorites));
        return;
    }

    favorites_legacy_file_t legacy;
    if(read_exact(FAVORITES_PATH, &legacy, sizeof(legacy)) &&
       legacy.header.magic == FAVORITES_MAGIC &&
       legacy.header.version == FAVORITES_LEGACY_VERSION &&
       legacy.header.count <= FAVORITES_LEGACY_CAPACITY &&
       legacy.header.checksum == checksum(legacy.uuids, sizeof(legacy.uuids))) {
        g_favorite_count = legacy.header.count;
        memcpy(g_favorites, legacy.uuids, sizeof(legacy.uuids));
        save_favorites_unlocked();
    }
}

static bool save_favorites_unlocked(void)
{
    favorites_file_t file;
    memset(&file, 0, sizeof(file));
    file.header.magic = FAVORITES_MAGIC;
    file.header.version = FAVORITES_VERSION;
    file.header.count = g_favorite_count;
    memcpy(file.uuids, g_favorites, sizeof(g_favorites));
    file.header.checksum = checksum(file.uuids, sizeof(file.uuids));
    return write_atomic(FAVORITES_TEMP_PATH, FAVORITES_PATH, &file, sizeof(file));
}

static bool load_cache(void)
{
    cache_file_t * file = malloc(sizeof(*file));
    if(file == NULL) return false;
    const bool valid = read_exact(CACHE_PATH, file, sizeof(*file)) &&
        file->header.magic == CACHE_MAGIC &&
        file->header.version == CATALOG_CACHE_VERSION &&
        file->header.count <= RADIO_MAX_STATIONS &&
        file->header.checksum == checksum(file->stations, sizeof(file->stations));
    if(valid) {
        g_station_count = file->header.count;
        memcpy(g_stations, file->stations, sizeof(g_stations));
    }
    free(file);
    return valid && g_station_count != 0;
}

static void save_cache(const radio_station_t * stations, unsigned count)
{
    cache_file_t * file = calloc(1, sizeof(*file));
    if(file == NULL) return;
    file->header.magic = CACHE_MAGIC;
    file->header.version = CATALOG_CACHE_VERSION;
    file->header.count = count;
    memcpy(file->stations, stations, count * sizeof(*stations));
    file->header.checksum = checksum(file->stations, sizeof(file->stations));
    write_atomic(CACHE_TEMP_PATH, CACHE_PATH, file, sizeof(*file));
    free(file);
}

static int network_init(void)
{
    g_net_pool = sceNetPoolCreate("ps5_radio_http", 0x4000, 0);
    if(g_net_pool < 0) return g_net_pool;
    g_ssl_context = sceSslInit(304U * 1024U);
    if(g_ssl_context < 0) return g_ssl_context;
    g_http_context = sceHttpInit(g_net_pool, g_ssl_context, 0x10000);
    if(g_http_context < 0) return g_http_context;
    g_http_template = sceHttpCreateTemplate(g_http_context, USER_AGENT,
                                             HTTP_VERSION_11, 1);
    if(g_http_template < 0) return g_http_template;
    sceHttpSetAutoRedirect(g_http_template, 1);
    sceHttpSetResolveTimeOut(g_http_template, 5000000U);
    sceHttpSetConnectTimeOut(g_http_template, 5000000U);
    sceHttpSetSendTimeOut(g_http_template, 5000000U);
    sceHttpSetRecvTimeOut(g_http_template, 5000000U);
    return 0;
}

static void network_shutdown(void)
{
    if(g_http_template >= 0) sceHttpDeleteTemplate(g_http_template);
    if(g_http_context >= 0) sceHttpTerm(g_http_context);
    if(g_ssl_context >= 0) sceSslTerm(g_ssl_context);
    if(g_net_pool >= 0) sceNetPoolDestroy(g_net_pool);
    g_http_template = -1;
    g_http_context = -1;
    g_ssl_context = -1;
    g_net_pool = -1;
}

static int http_open(const char * url, bool streaming, const char * codec,
                     int * connection,
                     int * request)
{
    *connection = sceHttpCreateConnectionWithURL(g_http_template, url, streaming ? 0 : 1);
    if(*connection < 0) return *connection;
    *request = sceHttpCreateRequestWithURL(*connection, HTTP_METHOD_GET, url, 0);
    if(*request < 0) {
        const int error = *request;
        sceHttpDeleteConnection(*connection);
        *connection = -1;
        return error;
    }
    sceHttpSetAutoRedirect(*request, 1);
    sceHttpSetResolveTimeOut(*request, 5000000U);
    sceHttpSetConnectTimeOut(*request, 5000000U);
    sceHttpSetSendTimeOut(*request, 5000000U);
    sceHttpSetRecvTimeOut(*request, streaming ? 2000000U : 5000000U);
    const char * accept = "application/json";
    if(streaming) {
        if(codec != NULL && strcasecmp(codec, "OPUS") == 0)
            accept = "audio/ogg, audio/opus, */*";
        else if(codec != NULL && strcasecmp(codec, "MP3") == 0)
            accept = "audio/mpeg, audio/mp3, */*";
        else accept = "application/vnd.apple.mpegurl, application/x-mpegURL, "
                      "video/mp2t, audio/aac, audio/aacp, */*";
    }
    sceHttpAddRequestHeader(*request, "Accept", accept,
                            HTTP_HEADER_OVERWRITE);
    if(streaming) {
        sceHttpAddRequestHeader(*request, "Icy-MetaData", "0", HTTP_HEADER_OVERWRITE);
        playback_request_set(*request);
    }
    int result = sceHttpSendRequest(*request, NULL, 0);
    if(result >= 0) {
        int status = 0;
        result = sceHttpGetStatusCode(*request, &status);
        if(result >= 0 && (status < 200 || status >= 300)) result = -status;
    }
    if(result < 0) {
        if(streaming) playback_request_clear(*request);
        sceHttpDeleteRequest(*request);
        sceHttpDeleteConnection(*connection);
        *request = -1;
        *connection = -1;
    }
    return result;
}

static void http_close(int connection, int request)
{
    if(request >= 0) sceHttpDeleteRequest(request);
    if(connection >= 0) sceHttpDeleteConnection(connection);
}

static unsigned http_audio_channels(int request)
{
    char * headers = NULL;
    size_t size = 0;
    if(sceHttpGetAllResponseHeaders(request, &headers, &size) < 0 || headers == NULL) return 0;
    static const char key[] = "channels=";
    for(size_t i = 0; i + sizeof(key) < size; ++i) {
        size_t match = 0;
        while(match + 1U < sizeof(key)) {
            char value = headers[i + match];
            if(value >= 'A' && value <= 'Z') value = (char)(value + ('a' - 'A'));
            if(value != key[match]) break;
            ++match;
        }
        if(match + 1U == sizeof(key)) {
            const char value = headers[i + match];
            return value == '1' || value == '2' ? (unsigned)(value - '0') : 0U;
        }
    }
    return 0;
}

static radio_playlist_kind_t http_playlist_kind(int request, const char * url)
{
    const radio_playlist_kind_t url_kind = radio_playlist_kind_from_url(url);
    if(url_kind != RADIO_PLAYLIST_NONE) return url_kind;
    char * headers = NULL;
    size_t size = 0;
    if(sceHttpGetAllResponseHeaders(request, &headers, &size) < 0 ||
       headers == NULL) return RADIO_PLAYLIST_NONE;
    return radio_playlist_kind_from_headers(headers, size);
}

static int read_playlist_document(int request, char * data, size_t capacity,
                                  size_t * size)
{
    *size = 0;
    while(*size < capacity && !SDL_AtomicGet(&g_stop_playback)) {
        const int received = sceHttpReadData(request, data + *size,
                                             capacity - *size);
        if(received < 0) return received;
        if(received == 0) return *size == 0 ? -2 : 0;
        *size += (size_t)received;
    }
    return SDL_AtomicGet(&g_stop_playback) ? 0 : -2;
}

static int open_resolved_stream(const radio_station_t * station,
                                int * connection, int * request,
                                char * resolved_url, size_t resolved_capacity)
{
    char current[sizeof(station->url)];
    SDL_strlcpy(current, station->url, sizeof(current));
    char * document = malloc(PLAYLIST_BUFFER_SIZE);
    if(document == NULL) return -1;

    int result = -2;
    for(unsigned depth = 0; depth < PLAYLIST_REDIRECT_LIMIT; ++depth) {
        result = http_open(current, true, station->codec, connection, request);
        if(result < 0) break;
        radio_playlist_kind_t kind = station->hls != 0U && depth == 0U
            ? RADIO_PLAYLIST_HLS : http_playlist_kind(*request, current);
        if(kind == RADIO_PLAYLIST_NONE) {
            SDL_strlcpy(resolved_url, current, resolved_capacity);
            free(document);
            return STREAM_OPEN_DIRECT;
        }
        if(kind == RADIO_PLAYLIST_HLS) {
            SDL_strlcpy(resolved_url, current, resolved_capacity);
            playback_request_clear(*request);
            http_close(*connection, *request);
            *connection = -1;
            *request = -1;
            free(document);
            return STREAM_OPEN_HLS;
        }

        size_t document_size = 0;
        result = read_playlist_document(*request, document,
                                        PLAYLIST_BUFFER_SIZE, &document_size);
        playback_request_clear(*request);
        http_close(*connection, *request);
        *connection = -1;
        *request = -1;
        if(result < 0) break;
        const radio_playlist_kind_t body_kind =
            radio_playlist_kind_from_body(document, document_size);
        if(body_kind == RADIO_PLAYLIST_HLS) {
            SDL_strlcpy(resolved_url, current, resolved_capacity);
            free(document);
            return STREAM_OPEN_HLS;
        }
        if(body_kind == RADIO_PLAYLIST_M3U || body_kind == RADIO_PLAYLIST_PLS)
            kind = body_kind;

        char resolved[sizeof(current)];
        result = (int)radio_playlist_first_url(
            kind, document, document_size, current,
            resolved, sizeof(resolved));
        if(result < 0) break;
        SDL_strlcpy(current, resolved, sizeof(current));
    }
    free(document);
    return result < 0 ? result : -2;
}

static int hex_value(char value)
{
    if(value >= '0' && value <= '9') return value - '0';
    if(value >= 'a' && value <= 'f') return value - 'a' + 10;
    if(value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static void append_utf8(char * output, size_t capacity, size_t * written,
                        unsigned codepoint)
{
    uint8_t bytes[3];
    unsigned count;
    if(codepoint < 0x80U) {
        bytes[0] = (uint8_t)codepoint;
        count = 1;
    }
    else if(codepoint < 0x800U) {
        bytes[0] = (uint8_t)(0xc0U | (codepoint >> 6));
        bytes[1] = (uint8_t)(0x80U | (codepoint & 0x3fU));
        count = 2;
    }
    else {
        bytes[0] = (uint8_t)(0xe0U | (codepoint >> 12));
        bytes[1] = (uint8_t)(0x80U | ((codepoint >> 6) & 0x3fU));
        bytes[2] = (uint8_t)(0x80U | (codepoint & 0x3fU));
        count = 3;
    }
    for(unsigned i = 0; i < count && *written + 1 < capacity; ++i) {
        output[(*written)++] = (char)bytes[i];
    }
}

static bool json_string_field(const char * begin, const char * end,
                              const char * field, char * output, size_t capacity)
{
    const size_t field_length = strlen(field);
    for(const char * at = begin; at + field_length + 2 < end; ++at) {
        if(*at != '"' || memcmp(at + 1, field, field_length) != 0 ||
           at[1 + field_length] != '"') continue;
        const char * value = at + field_length + 2;
        while(value < end && (*value == ' ' || *value == '\t' || *value == ':')) ++value;
        if(value >= end || *value != '"') return false;
        ++value;
        size_t written = 0;
        while(value < end && *value != '"') {
            if(*value != '\\') {
                if(written + 1 < capacity) output[written++] = *value;
                ++value;
                continue;
            }
            ++value;
            if(value >= end) break;
            unsigned codepoint;
            const char escape = *value++;
            if(escape == 'u' && value + 4 <= end) {
                codepoint = 0;
                bool valid = true;
                for(unsigned i = 0; i < 4; ++i) {
                    const int hex = hex_value(value[i]);
                    if(hex < 0) valid = false;
                    codepoint = (codepoint << 4) | (unsigned)(hex < 0 ? 0 : hex);
                }
                value += 4;
                if(!valid || (codepoint >= 0xd800U && codepoint <= 0xdfffU)) codepoint = '?';
            }
            else if(escape == 'n' || escape == 'r' || escape == 't') codepoint = ' ';
            else if(escape == 'b' || escape == 'f') continue;
            else codepoint = (uint8_t)escape;
            append_utf8(output, capacity, &written, codepoint);
        }
        if(capacity != 0) output[written] = '\0';
        return true;
    }
    if(capacity != 0) output[0] = '\0';
    return false;
}

static int json_integer_field(const char * begin, const char * end,
                              const char * field, int fallback)
{
    const size_t field_length = strlen(field);
    for(const char * at = begin; at + field_length + 2 < end; ++at) {
        if(*at != '"' || memcmp(at + 1, field, field_length) != 0 ||
           at[1 + field_length] != '"') continue;
        const char * value = at + field_length + 2;
        while(value < end && (*value == ' ' || *value == '\t' || *value == ':')) ++value;
        const bool negative = value < end && *value == '-';
        if(negative) ++value;
        int result = 0;
        bool found = false;
        while(value < end && *value >= '0' && *value <= '9') {
            found = true;
            if(result <= (INT_MAX - 9) / 10) result = result * 10 + (*value - '0');
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

static bool contains_ascii_case_insensitive(const char * text, const char * needle)
{
    if(text == NULL || needle == NULL || *needle == '\0') return false;
    for(; *text != '\0'; ++text) {
        const char * hay = text;
        const char * find = needle;
        while(*hay != '\0' && *find != '\0' &&
              ascii_lower(*hay) == ascii_lower(*find)) {
            ++hay;
            ++find;
        }
        if(*find == '\0') return true;
    }
    return false;
}

static bool normalize_supported_codec(radio_station_t * station)
{
    if(strcasecmp(station->codec, "AAC") == 0 ||
       strcasecmp(station->codec, "MP3") == 0) return true;
    if(strcasecmp(station->codec, "OGG") == 0 &&
       contains_ascii_case_insensitive(station->url, "opus")) {
        SDL_strlcpy(station->codec, "OPUS", sizeof(station->codec));
        return true;
    }
    return false;
}

static unsigned parse_catalog(const char * json, size_t length,
                              radio_station_t * stations, unsigned capacity)
{
    const char * end = json + length;
    const char * at = json;
    unsigned count = 0;
    while(at < end && count < capacity) {
        while(at < end && *at != '{') ++at;
        if(at == end) break;
        const char * object = at++;
        bool in_string = false;
        bool escaped = false;
        unsigned depth = 1;
        while(at < end && depth != 0) {
            const char value = *at++;
            if(in_string) {
                if(escaped) escaped = false;
                else if(value == '\\') escaped = true;
                else if(value == '"') in_string = false;
            }
            else if(value == '"') in_string = true;
            else if(value == '{') ++depth;
            else if(value == '}') --depth;
        }
        if(depth != 0) break;
        const char * object_end = at;
        radio_station_t station;
        memset(&station, 0, sizeof(station));
        station.popular_rank = RADIO_RANK_NONE;
        station.trending_rank = RADIO_RANK_NONE;
        station.voted_rank = RADIO_RANK_NONE;
        json_string_field(object, object_end, "stationuuid", station.uuid, sizeof(station.uuid));
        json_string_field(object, object_end, "name", station.name, sizeof(station.name));
        json_string_field(object, object_end, "url_resolved", station.url, sizeof(station.url));
        if(station.url[0] == '\0') {
            json_string_field(object, object_end, "url", station.url, sizeof(station.url));
        }
        json_string_field(object, object_end, "country", station.country, sizeof(station.country));
        json_string_field(object, object_end, "countrycode", station.country_code, sizeof(station.country_code));
        json_string_field(object, object_end, "state", station.state, sizeof(station.state));
        json_string_field(object, object_end, "language", station.language, sizeof(station.language));
        json_string_field(object, object_end, "tags", station.tags, sizeof(station.tags));
        json_string_field(object, object_end, "codec", station.codec, sizeof(station.codec));
        station.bitrate = (uint32_t)json_integer_field(object, object_end, "bitrate", 0);
        station.votes = (uint32_t)json_integer_field(object, object_end, "votes", 0);
        station.click_count = (uint32_t)json_integer_field(object, object_end, "clickcount", 0);
        station.click_trend = json_integer_field(object, object_end, "clicktrend", 0);
        const int hls = json_integer_field(object, object_end, "hls", 0);
        station.hls = hls != 0 ? 1U : 0U;
        const int healthy = json_integer_field(object, object_end, "lastcheckok", 1);
        if(station.uuid[0] != '\0' && station.name[0] != '\0' && station.url[0] != '\0' &&
           normalize_supported_codec(&station) &&
           (station.hls == 0U || strcasecmp(station.codec, "AAC") == 0) &&
           healthy != 0) {
            stations[count++] = station;
        }
    }
    return count;
}

static int station_index(const radio_station_t * stations, unsigned count,
                         const char * uuid)
{
    for(unsigned i = 0; i < count; ++i) {
        if(strcmp(stations[i].uuid, uuid) == 0) return (int)i;
    }
    return -1;
}

static void merge_feed(radio_station_t * catalog, unsigned * catalog_count,
                       const radio_station_t * feed, unsigned feed_count,
                       catalog_feed_t kind)
{
    for(unsigned rank = 0; rank < feed_count; ++rank) {
        int index = station_index(catalog, *catalog_count, feed[rank].uuid);
        if(index < 0) {
            if(*catalog_count >= RADIO_MAX_STATIONS) break;
            index = (int)(*catalog_count)++;
            catalog[index] = feed[rank];
        }
        else {
            catalog[index].votes = feed[rank].votes;
            catalog[index].click_count = feed[rank].click_count;
            catalog[index].click_trend = feed[rank].click_trend;
        }
        if(kind == FEED_POPULAR) catalog[index].popular_rank = 0;
        else if(kind == FEED_TRENDING) catalog[index].trending_rank = 0;
        else catalog[index].voted_rank = 0;
    }
}

static uint16_t * station_rank(radio_station_t * station, catalog_feed_t kind)
{
    if(kind == FEED_POPULAR) return &station->popular_rank;
    if(kind == FEED_TRENDING) return &station->trending_rank;
    return &station->voted_rank;
}

static int64_t station_metric(const radio_station_t * station, catalog_feed_t kind)
{
    if(kind == FEED_POPULAR) return station->click_count;
    if(kind == FEED_TRENDING) return station->click_trend;
    return station->votes;
}

static void assign_feed_ranks(radio_station_t * catalog, unsigned count)
{
    for(catalog_feed_t kind = FEED_POPULAR; kind <= FEED_VOTED; ++kind) {
        for(unsigned i = 0; i < count; ++i) {
            uint16_t * rank = station_rank(&catalog[i], kind);
            if(*rank == RADIO_RANK_NONE) continue;
            uint16_t value = 0;
            const int64_t metric = station_metric(&catalog[i], kind);
            for(unsigned j = 0; j < count; ++j) {
                if(*station_rank(&catalog[j], kind) == RADIO_RANK_NONE) continue;
                const int64_t other = station_metric(&catalog[j], kind);
                if(other > metric || (other == metric && strcmp(catalog[j].uuid, catalog[i].uuid) < 0))
                    ++value;
            }
            *rank = value;
        }
    }
}

static int fetch_catalog_feed(const catalog_feed_source_t * source,
                              radio_station_t * catalog, unsigned * catalog_count)
{
    char * json = malloc(JSON_CAPACITY);
    radio_station_t * feed = calloc(CATALOG_FEED_LIMIT, sizeof(*feed));
    if(json == NULL || feed == NULL) {
        free(feed);
        free(json);
        return -1;
    }

    size_t used = 0;
    int connection = -1;
    int request = -1;
    int error = http_open(source->url, false, NULL, &connection, &request);
    while(error == 0 && used < JSON_CAPACITY && !SDL_AtomicGet(&g_shutting_down)) {
        const int received = sceHttpReadData(request, json + used, JSON_CAPACITY - used);
        if(received < 0) error = received;
        else if(received == 0) break;
        else used += (size_t)received;
    }
    http_close(connection, request);

    if(error == 0 && used != 0 && used < JSON_CAPACITY) {
        const unsigned count = parse_catalog(json, used, feed, CATALOG_FEED_LIMIT);
        if(count != 0) merge_feed(catalog, catalog_count, feed, count, source->kind);
        else error = -2;
    }
    else if(error == 0) error = -2;
    free(feed);
    free(json);
    return error;
}

static void retain_local_stations(radio_station_t * catalog, unsigned * count)
{
    SDL_LockMutex(g_state_mutex);
    for(unsigned i = 0; i < g_station_count && *count < RADIO_MAX_STATIONS; ++i) {
        const bool currently_playing = SDL_AtomicGet(&g_playback_running) &&
                                       g_status.playing_index == i;
        if((favorite_unlocked(g_stations[i].uuid) || currently_playing) &&
           station_index(catalog, *count, g_stations[i].uuid) < 0) {
            radio_station_t retained = g_stations[i];
            retained.popular_rank = RADIO_RANK_NONE;
            retained.trending_rank = RADIO_RANK_NONE;
            retained.voted_rank = RADIO_RANK_NONE;
            catalog[(*count)++] = retained;
        }
    }
    SDL_UnlockMutex(g_state_mutex);
}

static void * refresh_thread(void * unused)
{
    (void)unused;
    radio_station_t * parsed = calloc(RADIO_MAX_STATIONS, sizeof(*parsed));
    unsigned count = 0;
    int error = parsed == NULL ? -1 : 0;
    unsigned successful_feeds = 0;
    if(parsed != NULL) {
        for(unsigned i = 0; i < sizeof(CATALOG_FEEDS) / sizeof(CATALOG_FEEDS[0]); ++i) {
            if(SDL_AtomicGet(&g_shutting_down)) break;
            const int feed_error = fetch_catalog_feed(&CATALOG_FEEDS[i], parsed, &count);
            if(feed_error == 0) ++successful_feeds;
            else error = feed_error;
        }
    }

    if(successful_feeds != 0 && count != 0 && !SDL_AtomicGet(&g_shutting_down)) {
        assign_feed_ranks(parsed, count);
        retain_local_stations(parsed, &count);
        save_cache(parsed, count);
        SDL_LockMutex(g_state_mutex);
        char playing_uuid[40] = {0};
        if(SDL_AtomicGet(&g_playback_running) && g_status.playing_index < g_station_count) {
            SDL_strlcpy(playing_uuid, g_stations[g_status.playing_index].uuid,
                        sizeof(playing_uuid));
        }
        memcpy(g_stations, parsed, count * sizeof(*parsed));
        if(count < RADIO_MAX_STATIONS) {
            memset(g_stations + count, 0,
                   (RADIO_MAX_STATIONS - count) * sizeof(*parsed));
        }
        g_station_count = count;
        g_status.station_count = count;
        if(playing_uuid[0] != '\0') {
            const int playing = station_index(g_stations, count, playing_uuid);
            if(playing >= 0) g_status.playing_index = (unsigned)playing;
        }
        g_status.catalog_state = RADIO_CATALOG_READY;
        ++g_status.catalog_generation;
        g_status.error_code = 0;
        g_status.refreshing = false;
        SDL_UnlockMutex(g_state_mutex);
    }
    else if(!SDL_AtomicGet(&g_shutting_down)) {
        SDL_LockMutex(g_state_mutex);
        g_status.catalog_state = RADIO_CATALOG_ERROR;
        g_status.error_code = error != 0 ? error : -2;
        ++g_status.catalog_generation;
        g_status.refreshing = false;
        SDL_UnlockMutex(g_state_mutex);
    }
    free(parsed);
    SDL_AtomicSet(&g_refresh_running, 0);
    return NULL;
}

static void set_playback_state(radio_playback_state_t state, int error,
                               unsigned sample_rate, unsigned channels)
{
    SDL_LockMutex(g_state_mutex);
    g_status.playback_state = state;
    g_status.error_code = error;
    g_status.sample_rate = sample_rate;
    g_status.channels = channels;
    SDL_UnlockMutex(g_state_mutex);
}

static void playback_request_set(int request)
{
    SDL_LockMutex(g_request_mutex);
    g_playback_request = request;
    if(request >= 0 && SDL_AtomicGet(&g_stop_playback)) {
        sceHttpAbortRequest(request);
    }
    SDL_UnlockMutex(g_request_mutex);
}

static void playback_request_clear(int request)
{
    SDL_LockMutex(g_request_mutex);
    if(g_playback_request == request) g_playback_request = -1;
    SDL_UnlockMutex(g_request_mutex);
}

static size_t sink_ready_target(bool started, bool played)
{
    if(started) return 1U;
    return played ? AUDIO_RESTART_BLOCKS : AUDIO_START_BLOCKS;
}

static int sink_audio_thread(void * argument)
{
    audio_sink_t * sink = argument;
    int16_t block[AUDIO_OUT_GRAIN * 2U];
    bool started = false;
    bool played = false;

    for(;;) {
        SDL_LockMutex(sink->mutex);
        if(started && sink->queue.count == 0U && !sink->input_finished &&
           !sink->cancel && !SDL_AtomicGet(&g_stop_playback)) {
            started = false;
            SDL_UnlockMutex(sink->mutex);
            if(!SDL_AtomicGet(&g_stop_playback)) {
                set_playback_state(RADIO_PLAYBACK_BUFFERING, 0,
                                   sink->input_rate, sink->channels);
            }
            SDL_LockMutex(sink->mutex);
        }

        const size_t target = sink_ready_target(started, played);
        while(!sink->cancel && !SDL_AtomicGet(&g_stop_playback) &&
              sink->output_result >= 0 && !sink->input_finished &&
              !pcm_queue_ready(&sink->queue, target, false)) {
            SDL_CondWaitTimeout(sink->can_read, sink->mutex, AUDIO_WAIT_MS);
        }
        if(sink->cancel || SDL_AtomicGet(&g_stop_playback) ||
           sink->output_result < 0 || sink->queue.count == 0U) {
            SDL_UnlockMutex(sink->mutex);
            break;
        }

        size_t index = 0;
        pcm_queue_pop(&sink->queue, &index);
        memcpy(block, sink->queue_blocks + index * AUDIO_OUT_GRAIN * 2U,
               sizeof(block));
        SDL_CondSignal(sink->can_write);
        SDL_UnlockMutex(sink->mutex);

        const int result = sceAudioOutOutput(sink->handle, block);
        if(result < 0) {
            SDL_LockMutex(sink->mutex);
            sink->output_result = result;
            sink->cancel = true;
            SDL_CondBroadcast(sink->can_write);
            SDL_UnlockMutex(sink->mutex);
            break;
        }
        if(!started && !SDL_AtomicGet(&g_stop_playback)) {
            started = true;
            played = true;
            set_playback_state(RADIO_PLAYBACK_PLAYING, 0,
                               sink->input_rate, sink->channels);
        }
    }
    return 0;
}

static void sink_cancel(audio_sink_t * sink)
{
    if(sink == NULL || sink->handle < 0 || sink->mutex == NULL) return;
    SDL_LockMutex(sink->mutex);
    sink->cancel = true;
    SDL_CondBroadcast(sink->can_read);
    SDL_CondBroadcast(sink->can_write);
    SDL_UnlockMutex(sink->mutex);
}

static int sink_open(audio_sink_t * sink, uint32_t input_rate, uint32_t channels)
{
    memset(sink, 0, sizeof(*sink));
    sink->handle = -1;
    sink->input_rate = input_rate;
    sink->channels = channels;
    if(input_rate < 8000U || input_rate > 192000U || channels < 1U || channels > 2U) return -1;

    sink->queue_blocks = malloc(AUDIO_QUEUE_BLOCKS * AUDIO_OUT_GRAIN * 2U * sizeof(int16_t));
    sink->mutex = SDL_CreateMutex();
    sink->can_read = SDL_CreateCond();
    sink->can_write = SDL_CreateCond();
    if(sink->queue_blocks == NULL || sink->mutex == NULL ||
       sink->can_read == NULL || sink->can_write == NULL) goto fail;
    pcm_queue_init(&sink->queue, AUDIO_QUEUE_BLOCKS);

    sceAudioOutInit();
    sink->handle = sceAudioOutOpen(0xff, 0, 0, AUDIO_OUT_GRAIN,
                                   AUDIO_OUT_RATE, AUDIO_OUT_STEREO_S16);
    if(sink->handle < 0) goto fail;
    int volumes[8];
    for(unsigned i = 0; i < 8; ++i) volumes[i] = AUDIO_OUT_VOLUME_0DB;
    sceAudioOutSetVolume(sink->handle, 3, volumes);
    /*
     * The PS5 SDL backend forwards non-null names to pthread_set_name_np.
     * That optional import is unavailable on the target runtime and resolves
     * to null, so leave the debug name unset.
     */
    sink->thread = SDL_CreateThread(sink_audio_thread, NULL, sink);
    if(sink->thread == NULL) goto fail;
    return 0;

fail:
    {
        const int error = sink->handle < 0 ? sink->handle : -1;
        if(sink->handle >= 0) {
            sceAudioOutOutput(sink->handle, NULL);
            sceAudioOutClose(sink->handle);
        }
        if(sink->can_write != NULL) SDL_DestroyCond(sink->can_write);
        if(sink->can_read != NULL) SDL_DestroyCond(sink->can_read);
        if(sink->mutex != NULL) SDL_DestroyMutex(sink->mutex);
        free(sink->queue_blocks);
        memset(sink, 0, sizeof(*sink));
        sink->handle = -1;
        return error;
    }
}

static int sink_queue_block(audio_sink_t * sink)
{
    SDL_LockMutex(sink->mutex);
    while(sink->queue.count == sink->queue.capacity && !sink->cancel &&
          !SDL_AtomicGet(&g_stop_playback) && sink->output_result >= 0) {
        SDL_CondWaitTimeout(sink->can_write, sink->mutex, AUDIO_WAIT_MS);
    }
    if(sink->cancel || SDL_AtomicGet(&g_stop_playback)) {
        SDL_UnlockMutex(sink->mutex);
        return -1;
    }
    if(sink->output_result < 0) {
        const int result = sink->output_result;
        SDL_UnlockMutex(sink->mutex);
        return result;
    }

    size_t index = 0;
    if(!pcm_queue_push(&sink->queue, &index)) {
        SDL_UnlockMutex(sink->mutex);
        return -1;
    }
    memcpy(sink->queue_blocks + index * AUDIO_OUT_GRAIN * 2U,
           sink->block, sizeof(sink->block));
    sink->pending = 0;
    SDL_CondSignal(sink->can_read);
    SDL_UnlockMutex(sink->mutex);
    return 0;
}

static int sink_output_frame(audio_sink_t * sink, int16_t left, int16_t right)
{
    sink->block[sink->pending++] = left;
    sink->block[sink->pending++] = right;
    return sink->pending == AUDIO_OUT_GRAIN * 2U ? sink_queue_block(sink) : 0;
}

static int sink_push_pcm(audio_sink_t * sink, const int16_t * samples,
                         unsigned sample_count)
{
    const unsigned frames = sample_count / sink->channels;
    for(unsigned i = 0; i < frames; ++i) {
        const int16_t left = samples[i * sink->channels];
        const int16_t right = sink->channels == 2U ? samples[i * 2U + 1U] : left;
        if(!sink->have_previous) {
            sink->previous_left = left;
            sink->previous_right = right;
            sink->have_previous = true;
            sink->input_index = 0;
            continue;
        }

        ++sink->input_index;
        const uint64_t interval_end = sink->input_index * AUDIO_OUT_RATE;
        const uint64_t interval_start = (sink->input_index - 1U) * AUDIO_OUT_RATE;
        while(sink->next_output_position < interval_end) {
            const uint64_t fraction = sink->next_output_position - interval_start;
            const int32_t out_left = sink->previous_left +
                (int32_t)(((int64_t)(left - sink->previous_left) * (int64_t)fraction) /
                          (int64_t)AUDIO_OUT_RATE);
            const int32_t out_right = sink->previous_right +
                (int32_t)(((int64_t)(right - sink->previous_right) * (int64_t)fraction) /
                          (int64_t)AUDIO_OUT_RATE);
            const int result = sink_output_frame(sink, (int16_t)out_left, (int16_t)out_right);
            if(result < 0) return result;
            sink->next_output_position += sink->input_rate;
        }
        sink->previous_left = left;
        sink->previous_right = right;
    }
    return 0;
}

static void sink_close(audio_sink_t * sink)
{
    if(sink->handle < 0) return;
    if(sink->pending != 0 && !sink->cancel &&
       !SDL_AtomicGet(&g_stop_playback)) {
        memset(sink->block + sink->pending, 0,
               (AUDIO_OUT_GRAIN * 2U - sink->pending) * sizeof(sink->block[0]));
        sink->pending = AUDIO_OUT_GRAIN * 2U;
        sink_queue_block(sink);
    }

    SDL_LockMutex(sink->mutex);
    sink->input_finished = true;
    sink->cancel = sink->cancel || SDL_AtomicGet(&g_stop_playback) ||
                   sink->output_result < 0;
    SDL_CondBroadcast(sink->can_read);
    SDL_CondBroadcast(sink->can_write);
    SDL_UnlockMutex(sink->mutex);
    SDL_WaitThread(sink->thread, NULL);

    sceAudioOutOutput(sink->handle, NULL);
    sceAudioOutClose(sink->handle);
    SDL_DestroyCond(sink->can_write);
    SDL_DestroyCond(sink->can_read);
    SDL_DestroyMutex(sink->mutex);
    free(sink->queue_blocks);
    memset(sink, 0, sizeof(*sink));
    sink->handle = -1;
}

typedef struct {
    opus_decoder_t decoder;
    audio_sink_t sink;
    int16_t * pcm;
    uint32_t stream_serial;
    size_t pre_skip_frames;
    int result;
    bool decoder_open;
    bool stream_open;
} opus_playback_t;

static bool opus_packet_is_celt(const uint8_t * data, size_t size)
{
    /* Opus TOC configurations 16-31 are CELT-only (RFC 6716, section 3.1). */
    return size > 0U && (data[0] >> 3U) >= 16U;
}

static void opus_decoder_reset(opus_playback_t * playback)
{
    opus_decoder_close(&playback->decoder);
    playback->decoder_open = false;
}

static void opus_playback_reset(opus_playback_t * playback, bool discard)
{
    if(discard) sink_cancel(&playback->sink);
    sink_close(&playback->sink);
    opus_decoder_reset(playback);
    playback->stream_serial = 0;
    playback->pre_skip_frames = 0;
    playback->stream_open = false;
}

static int opus_packet_ready(const ogg_opus_packet_t * packet, void * user_data)
{
    opus_playback_t * playback = user_data;
    const bool celt_packet = opus_packet_is_celt(packet->data, packet->size);
    if(!playback->stream_open || playback->stream_serial != packet->stream_serial) {
        if(playback->stream_open) opus_playback_reset(playback, true);
        playback->sink.handle = -1;
        playback->stream_serial = packet->stream_serial;
        playback->pre_skip_frames = packet->pre_skip;
        playback->stream_open = true;
        playback->result = opus_decoder_open(&playback->decoder,
                                             packet->channels, false);
        if(playback->result < 0) return -1;
        playback->decoder_open = true;
    }
    else if(playback->decoder.celt_only && !celt_packet) {
        opus_decoder_reset(playback);
        playback->result = opus_decoder_open(&playback->decoder,
                                             packet->channels, false);
        if(playback->result < 0) return -1;
        playback->decoder_open = true;
    }

    size_t produced = 0;
    playback->result = opus_decoder_decode(
        &playback->decoder, packet->data, packet->size,
        playback->pcm, OPUS_PCM_BUFFER_SIZE, &produced);
    if(playback->result == OPUS_RETRYABLE_ERROR && celt_packet) {
        const bool alternate_celt = !playback->decoder.celt_only;
        opus_decoder_reset(playback);
        playback->result = opus_decoder_open(&playback->decoder,
                                             packet->channels, alternate_celt);
        if(playback->result < 0) return -1;
        playback->decoder_open = true;
        playback->result = opus_decoder_decode(
            &playback->decoder, packet->data, packet->size,
            playback->pcm, OPUS_PCM_BUFFER_SIZE, &produced);
    }
    if(playback->result < 0) return -1;
    const size_t frame_bytes = packet->channels * sizeof(int16_t);
    if(frame_bytes == 0U || produced % frame_bytes != 0U) {
        playback->result = -1;
        return -1;
    }

    size_t frames = produced / frame_bytes;
    size_t skip = playback->pre_skip_frames < frames ? playback->pre_skip_frames : frames;
    playback->pre_skip_frames -= skip;
    frames -= skip;
    if(frames == 0U) return 0;

    if(playback->sink.handle < 0) {
        playback->result = sink_open(&playback->sink, 48000U, packet->channels);
        if(playback->result < 0) return -1;
        set_playback_state(RADIO_PLAYBACK_BUFFERING, 0, 48000U, packet->channels);
    }
    playback->result = sink_push_pcm(
        &playback->sink, playback->pcm + skip * packet->channels,
        (unsigned)(frames * packet->channels));
    return playback->result < 0 ? -1 : 0;
}

static int play_opus_request(int request)
{
    uint8_t * stream = malloc(STREAM_BUFFER_SIZE);
    ogg_opus_parser_t * parser = malloc(sizeof(*parser));
    opus_playback_t playback;
    memset(&playback, 0, sizeof(playback));
    playback.sink.handle = -1;
    playback.pcm = malloc(OPUS_PCM_BUFFER_SIZE);
    if(stream == NULL || parser == NULL || playback.pcm == NULL) {
        free(playback.pcm);
        free(parser);
        free(stream);
        return -1;
    }

    ogg_opus_init(parser, opus_packet_ready, &playback);
    int result = 0;
    while(!SDL_AtomicGet(&g_stop_playback)) {
        const int received = sceHttpReadData(request, stream, STREAM_BUFFER_SIZE);
        if(received < 0) {
            result = received;
            break;
        }
        if(received == 0) {
            result = -3;
            break;
        }
        const ogg_opus_result_t parsed = ogg_opus_feed(parser, stream, (size_t)received);
        if(parsed != OGG_OPUS_OK) {
            result = parsed == OGG_OPUS_ERR_CALLBACK ? playback.result : (int)parsed;
            break;
        }
    }

    opus_playback_reset(&playback,
                        result < 0 || SDL_AtomicGet(&g_stop_playback));
    free(playback.pcm);
    free(parser);
    free(stream);
    return SDL_AtomicGet(&g_stop_playback) ? 0 : result;
}

static size_t find_adts(const uint8_t * data, size_t size)
{
    for(size_t i = 0; i + 1U < size; ++i) {
        if(data[i] == 0xffU && (data[i + 1U] & 0xf6U) == 0xf0U) return i;
    }
    return size;
}

typedef int (*stream_read_fn)(void * context, void * data, size_t size);

typedef struct {
    radio_hls_playlist_t * playlist;
    radio_ts_aac_parser_t * transport;
    uint8_t * network;
    uint8_t * output;
    size_t output_at;
    size_t output_size;
    char playlist_url[RADIO_HLS_URL_BYTES];
    uint64_t next_sequence;
    uint64_t segment_sequence;
    uint32_t reload_at;
    unsigned source_channels;
    int connection;
    int request;
} hls_reader_t;

static int http_stream_read(void * context, void * data, size_t size)
{
    return sceHttpReadData(*(const int *)context, data, size);
}

static void hls_request_close(hls_reader_t * reader)
{
    if(reader->request >= 0) playback_request_clear(reader->request);
    http_close(reader->connection, reader->request);
    reader->connection = -1;
    reader->request = -1;
}

static int hls_fetch_playlist(const char * initial_url,
                              radio_hls_playlist_t * playlist,
                              char * media_url, size_t media_url_size,
                              unsigned * source_channels)
{
    char current[RADIO_HLS_URL_BYTES];
    SDL_strlcpy(current, initial_url, sizeof(current));
    char * document = malloc(HLS_PLAYLIST_BUFFER_SIZE);
    if(document == NULL) return -1;

    int result = HLS_ERROR_PLAYLIST;
    unsigned selected_channels = 0U;
    for(unsigned depth = 0U; depth <= HLS_MASTER_LIMIT; ++depth) {
        int connection = -1;
        int request = -1;
        result = http_open(current, true, "AAC", &connection, &request);
        size_t document_size = 0U;
        if(result >= 0) result = read_playlist_document(
            request, document, HLS_PLAYLIST_BUFFER_SIZE, &document_size);
        if(request >= 0) playback_request_clear(request);
        http_close(connection, request);
        if(result < 0 || SDL_AtomicGet(&g_stop_playback)) break;

        const radio_hls_result_t parsed = radio_hls_parse(
            document, document_size, current, playlist);
        if(parsed != RADIO_HLS_OK) {
            result = HLS_ERROR_PLAYLIST + (int)parsed;
            break;
        }
        if(playlist->kind == RADIO_HLS_MEDIA) {
            SDL_strlcpy(media_url, current, media_url_size);
            if(source_channels != NULL) *source_channels = selected_channels;
            result = 0;
            break;
        }
        const int selected = radio_hls_select_variant(playlist);
        if(selected < 0) {
            result = HLS_ERROR_PLAYLIST;
            break;
        }
        if(playlist->variants[selected].source_channels != 0U)
            selected_channels = playlist->variants[selected].source_channels;
        SDL_strlcpy(current, playlist->variants[selected].url,
                    sizeof(current));
    }
    free(document);
    return SDL_AtomicGet(&g_stop_playback) ? 0 : result;
}

static int hls_output_ready(const uint8_t * data, size_t size,
                            void * user_data)
{
    hls_reader_t * reader = user_data;
    if(reader->output_size + size > HLS_OUTPUT_BUFFER_SIZE) return -1;
    memcpy(reader->output + reader->output_size, data, size);
    reader->output_size += size;
    return 0;
}

static unsigned hls_live_edge(const radio_hls_playlist_t * playlist)
{
    return playlist->segment_count > HLS_LIVE_EDGE_SEGMENTS
        ? playlist->segment_count - HLS_LIVE_EDGE_SEGMENTS : 0U;
}

static int hls_reader_open(hls_reader_t * reader, const char * url)
{
    memset(reader, 0, sizeof(*reader));
    reader->connection = -1;
    reader->request = -1;
    reader->playlist = malloc(sizeof(*reader->playlist));
    reader->transport = malloc(sizeof(*reader->transport));
    reader->network = malloc(HLS_NETWORK_BUFFER_SIZE);
    reader->output = malloc(HLS_OUTPUT_BUFFER_SIZE);
    if(reader->playlist == NULL || reader->transport == NULL ||
       reader->network == NULL || reader->output == NULL) return -1;

    int result = hls_fetch_playlist(url, reader->playlist,
                                    reader->playlist_url,
                                    sizeof(reader->playlist_url),
                                    &reader->source_channels);
    if(result < 0 || SDL_AtomicGet(&g_stop_playback)) return result;
    const unsigned first = reader->playlist->is_live != 0U
        ? hls_live_edge(reader->playlist) : 0U;
    reader->next_sequence = reader->playlist->segments[first].sequence;
    radio_ts_aac_init(reader->transport, hls_output_ready, reader);
    return 0;
}

static void hls_reader_close(hls_reader_t * reader)
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
    for(unsigned waited = 0U; waited < milliseconds;
        waited += 25U) {
        if(SDL_AtomicGet(&g_stop_playback)) return 0;
        SDL_Delay(milliseconds - waited < 25U ? milliseconds - waited : 25U);
    }
    return SDL_AtomicGet(&g_stop_playback) ? 0 : 1;
}

static int hls_reload(hls_reader_t * reader)
{
    const uint32_t now = SDL_GetTicks();
    if((int32_t)(reader->reload_at - now) > 0 &&
       !hls_wait(reader->reload_at - now)) return 0;
    const int result = hls_fetch_playlist(
        reader->playlist_url, reader->playlist,
        reader->playlist_url, sizeof(reader->playlist_url), NULL);
    if(result < 0 || SDL_AtomicGet(&g_stop_playback)) return result;
    const uint32_t delay = reader->playlist->target_duration_ms / 2U;
    reader->reload_at = SDL_GetTicks() + (delay < 500U ? 500U : delay);

    const uint64_t first = reader->playlist->segments[0].sequence;
    const uint64_t last = reader->playlist->segments[
        reader->playlist->segment_count - 1U].sequence;
    if(reader->next_sequence < first || reader->next_sequence > last + 1U) {
        const unsigned edge = hls_live_edge(reader->playlist);
        reader->next_sequence = reader->playlist->segments[edge].sequence;
        radio_ts_aac_reset(reader->transport);
    }
    return 1;
}

static int hls_find_segment(const hls_reader_t * reader)
{
    for(uint32_t i = 0U; i < reader->playlist->segment_count; ++i) {
        if(reader->playlist->segments[i].sequence == reader->next_sequence)
            return (int)i;
    }
    return -1;
}

static int hls_stream_read(void * context, void * data, size_t capacity)
{
    hls_reader_t * reader = context;
    while(!SDL_AtomicGet(&g_stop_playback)) {
        if(reader->output_at < reader->output_size) {
            const size_t available = reader->output_size - reader->output_at;
            const size_t copy = available < capacity ? available : capacity;
            memcpy(data, reader->output + reader->output_at, copy);
            reader->output_at += copy;
            if(reader->output_at == reader->output_size) {
                reader->output_at = 0U;
                reader->output_size = 0U;
            }
            return (int)copy;
        }

        if(reader->request < 0) {
            int segment = hls_find_segment(reader);
            if(segment < 0) {
                if(reader->playlist->is_live == 0U) return 0;
                const int reloaded = hls_reload(reader);
                if(reloaded <= 0) return reloaded;
                segment = hls_find_segment(reader);
                if(segment < 0) continue;
            }
            const radio_hls_segment_t * item =
                &reader->playlist->segments[segment];
            if(item->discontinuity != 0U)
                radio_ts_aac_reset(reader->transport);
            const int result = http_open(item->url, true, "AAC",
                                         &reader->connection,
                                         &reader->request);
            if(result < 0) return result;
            reader->segment_sequence = item->sequence;
        }

        const int received = sceHttpReadData(reader->request, reader->network,
                                             HLS_NETWORK_BUFFER_SIZE);
        if(received < 0) {
            hls_request_close(reader);
            return received;
        }
        if(received == 0) {
            hls_request_close(reader);
            reader->next_sequence = reader->segment_sequence + 1U;
            continue;
        }
        reader->output_at = 0U;
        reader->output_size = 0U;
        const radio_ts_aac_result_t parsed = radio_ts_aac_feed(
            reader->transport, reader->network, (size_t)received);
        if(parsed != RADIO_TS_AAC_OK) return HLS_ERROR_TRANSPORT + (int)parsed;
    }
    return 0;
}

static int play_audiodec_reader(stream_read_fn read_stream,
                                void * read_context,
                                unsigned source_channels, bool mp3)
{
    uint8_t * stream = malloc(STREAM_BUFFER_SIZE);
    uint8_t * pcm = malloc(PCM_BUFFER_SIZE);
    if(stream == NULL || pcm == NULL) {
        free(stream);
        free(pcm);
        return -1;
    }

    const uint32_t codec_type = mp3 ? AUDIODEC_MP3 : AUDIODEC_AAC;
    int result = sceSysmoduleLoadModule(0x0088);
    const bool module_loaded = result >= 0;
    if(module_loaded) result = sceAudiodecInitLibrary(codec_type);
    const bool library_initialized = result >= 0;
    sce_audiodec_param_mp3_t mp3_param = {
        sizeof(mp3_param), AUDIODEC_WORD_S16
    };
    sce_audiodec_mp3_info_t mp3_info;
    memset(&mp3_info, 0, sizeof(mp3_info));
    mp3_info.size = sizeof(mp3_info);
    sce_audiodec_param_aac_t aac_param = {
        sizeof(aac_param), AUDIODEC_WORD_S16, 1, 4, 2, 1
    };
    sce_audiodec_aac_info_t aac_info;
    memset(&aac_info, 0, sizeof(aac_info));
    aac_info.size = sizeof(aac_info);
    sce_audiodec_au_info_t au;
    memset(&au, 0, sizeof(au));
    au.size = sizeof(au);
    sce_audiodec_pcm_item_t pcm_item;
    memset(&pcm_item, 0, sizeof(pcm_item));
    pcm_item.size = sizeof(pcm_item);
    sce_audiodec_ctrl_t ctrl = {
        mp3 ? (void *)&mp3_param : (void *)&aac_param,
        mp3 ? (void *)&mp3_info : (void *)&aac_info,
        &au, &pcm_item
    };
    int decoder = -1;
    if(result >= 0) {
        decoder = sceAudiodecCreateDecoder(&ctrl, codec_type);
        if(decoder < 0) result = decoder;
    }

    audio_sink_t sink;
    memset(&sink, 0, sizeof(sink));
    sink.handle = -1;
    size_t buffered = 0;
    size_t scanned = 0;
    while(result >= 0 && !SDL_AtomicGet(&g_stop_playback)) {
        const size_t minimum_header = mp3 ? 4U : 7U;
        if(buffered < minimum_header) {
            const int read = read_stream(read_context, stream + buffered,
                                         STREAM_BUFFER_SIZE - buffered);
            if(read < 0) {
                result = read;
                break;
            }
            if(read == 0) {
                result = -3;
                break;
            }
            buffered += (size_t)read;
        }

        mp3_header_t mp3_header;
        const size_t sync = mp3 ? mp3_header_find(stream, buffered, &mp3_header)
                                : find_adts(stream, buffered);
        if(sync != 0) {
            const size_t tail = minimum_header - 1U;
            const size_t remove = sync == buffered
                ? (buffered > tail ? buffered - tail : 0U) : sync;
            if(remove != 0) {
                memmove(stream, stream + remove, buffered - remove);
                buffered -= remove;
                scanned += remove;
                if(scanned > 256U * 1024U) {
                    result = -4;
                    break;
                }
            }
            continue;
        }

        const size_t frame_length = mp3 ? mp3_header.frame_bytes :
            (((size_t)(stream[3] & 0x03U) << 11) |
             ((size_t)stream[4] << 3) | ((size_t)stream[5] >> 5));
        if(frame_length < minimum_header || frame_length > 4608U) {
            memmove(stream, stream + 1, --buffered);
            continue;
        }
        while(buffered < frame_length && buffered < STREAM_BUFFER_SIZE &&
              !SDL_AtomicGet(&g_stop_playback)) {
            const int read = read_stream(read_context, stream + buffered,
                                         STREAM_BUFFER_SIZE - buffered);
            if(read < 0) {
                result = read;
                break;
            }
            if(read == 0) {
                result = -3;
                break;
            }
            buffered += (size_t)read;
        }
        if(result < 0 || buffered < frame_length) break;

        au.address = stream;
        au.length = (uint32_t)frame_length;
        pcm_item.address = pcm;
        pcm_item.length = PCM_BUFFER_SIZE;
        result = sceAudiodecDecode(decoder, &ctrl);
        if(result >= 0 && pcm_item.length > PCM_BUFFER_SIZE) result = -6;
        if(mp3 && result >= 0 &&
           (au.length != frame_length ||
            pcm_item.length % (mp3_header.channels * sizeof(int16_t)) != 0U)) {
            result = -6;
        }
        if(!mp3 && result >= 0 && aac_should_disable_he(
                stream, frame_length, source_channels, aac_info.sampling_frequency,
                aac_info.channel_count, aac_info.he_aac)) {
            sceAudiodecDeleteDecoder(decoder);
            decoder = -1;
            aac_param.enable_he_aac = 0;
            memset(&aac_info, 0, sizeof(aac_info));
            aac_info.size = sizeof(aac_info);
            decoder = sceAudiodecCreateDecoder(&ctrl, codec_type);
            if(decoder < 0) result = decoder;
            else {
                au.address = stream;
                au.length = (uint32_t)frame_length;
                pcm_item.address = pcm;
                pcm_item.length = PCM_BUFFER_SIZE;
                result = sceAudiodecDecode(decoder, &ctrl);
            }
        }
        if(result >= 0 && pcm_item.length != 0) {
            if(sink.handle < 0) {
                const uint32_t pcm_rate = mp3 ? mp3_header.sample_rate :
                    aac_pcm_rate(stream, frame_length, aac_info.channel_count,
                                 pcm_item.length, aac_info.sampling_frequency);
                const uint32_t channels = mp3 ? mp3_header.channels
                                              : aac_info.channel_count;
                result = sink_open(&sink, pcm_rate, channels);
                if(result >= 0) {
                    set_playback_state(RADIO_PLAYBACK_BUFFERING, 0,
                                       pcm_rate, channels);
                }
            }
            if(result >= 0) {
                result = sink_push_pcm(&sink, (const int16_t *)pcm,
                                       pcm_item.length / sizeof(int16_t));
            }
        }
        memmove(stream, stream + frame_length, buffered - frame_length);
        buffered -= frame_length;
    }

    if(result < 0 || SDL_AtomicGet(&g_stop_playback)) sink_cancel(&sink);
    sink_close(&sink);
    if(decoder >= 0) sceAudiodecDeleteDecoder(decoder);
    if(library_initialized) sceAudiodecTermLibrary(codec_type);
    if(module_loaded) sceSysmoduleUnloadModule(0x0088);
    free(pcm);
    free(stream);
    return result;
}

static int play_stream(const radio_station_t * station)
{
    int connection = -1;
    int request = -1;
    char resolved_url[sizeof(station->url)];
    const int mode = open_resolved_stream(
        station, &connection, &request, resolved_url, sizeof(resolved_url));
    if(mode < 0) return mode;
    set_playback_state(RADIO_PLAYBACK_BUFFERING, 0, 0, 0);

    if(mode == STREAM_OPEN_HLS) {
        hls_reader_t reader;
        int result = hls_reader_open(&reader, resolved_url);
        if(result >= 0 && !SDL_AtomicGet(&g_stop_playback)) {
            result = play_audiodec_reader(hls_stream_read, &reader,
                                          reader.source_channels, false);
        }
        hls_reader_close(&reader);
        return result;
    }

    const unsigned source_channels = http_audio_channels(request);
    int result;
    if(strcasecmp(station->codec, "OPUS") == 0)
        result = play_opus_request(request);
    else {
        const bool mp3 = strcasecmp(station->codec, "MP3") == 0;
        result = play_audiodec_reader(http_stream_read, &request,
                                      source_channels, mp3);
    }
    playback_request_clear(request);
    http_close(connection, request);
    return result;
}

static void * playback_thread(void * station_copy)
{
    radio_station_t * station = station_copy;
    int result = -1;
    for(unsigned attempt = 0; attempt < PLAYBACK_RETRY_COUNT; ++attempt) {
        result = play_stream(station);
        if(result >= 0 || SDL_AtomicGet(&g_stop_playback) ||
           SDL_AtomicGet(&g_shutting_down)) break;
        if(attempt + 1U < PLAYBACK_RETRY_COUNT) {
            set_playback_state(RADIO_PLAYBACK_BUFFERING, 0, 0, 0);
            const unsigned delay = PLAYBACK_RETRY_BASE_MS << attempt;
            for(unsigned waited = 0; waited < delay &&
                !SDL_AtomicGet(&g_stop_playback) &&
                !SDL_AtomicGet(&g_shutting_down); waited += 25U) {
                SDL_Delay(25U);
            }
        }
    }
    free(station);
    SDL_AtomicSet(&g_playback_running, 0);
    if(SDL_AtomicGet(&g_stop_playback) || SDL_AtomicGet(&g_shutting_down)) {
        set_playback_state(RADIO_PLAYBACK_STOPPED, 0, 0, 0);
    }
    else {
        set_playback_state(RADIO_PLAYBACK_ERROR, result < 0 ? result : -5, 0, 0);
    }
    return NULL;
}

bool radio_service_init(void)
{
    memset(&g_status, 0, sizeof(g_status));
    g_state_mutex = SDL_CreateMutex();
    g_request_mutex = SDL_CreateMutex();
    if(g_state_mutex == NULL || g_request_mutex == NULL) {
        if(g_request_mutex != NULL) SDL_DestroyMutex(g_request_mutex);
        if(g_state_mutex != NULL) SDL_DestroyMutex(g_state_mutex);
        g_request_mutex = NULL;
        g_state_mutex = NULL;
        return false;
    }
    SDL_AtomicSet(&g_shutting_down, 0);
    SDL_AtomicSet(&g_stop_playback, 0);
    SDL_AtomicSet(&g_playback_running, 0);
    SDL_AtomicSet(&g_refresh_running, 0);
    g_playback_request = -1;
    load_favorites();
    const bool cached = load_cache();
    g_status.catalog_state = cached ? RADIO_CATALOG_CACHED : RADIO_CATALOG_LOADING;
    g_status.catalog_generation = cached ? 1U : 0U;
    g_status.station_count = g_station_count;
    g_status.playback_state = RADIO_PLAYBACK_STOPPED;

    const int network_error = network_init();
    if(network_error < 0) {
        g_status.error_code = network_error;
        if(!cached) g_status.catalog_state = RADIO_CATALOG_ERROR;
        return cached;
    }
    return radio_service_refresh() || cached;
}

void radio_service_shutdown(void)
{
    if(g_state_mutex == NULL) return;
    SDL_AtomicSet(&g_shutting_down, 1);
    radio_service_stop();
    while(SDL_AtomicGet(&g_refresh_running) || SDL_AtomicGet(&g_playback_running)) {
        SDL_Delay(10);
    }
    network_shutdown();
    SDL_DestroyMutex(g_request_mutex);
    SDL_DestroyMutex(g_state_mutex);
    g_request_mutex = NULL;
    g_state_mutex = NULL;
}

void radio_service_get_status(radio_service_status_t * out_status)
{
    if(out_status == NULL) return;
    SDL_LockMutex(g_state_mutex);
    *out_status = g_status;
    SDL_UnlockMutex(g_state_mutex);
}

bool radio_service_get_station(unsigned index, radio_station_t * out_station)
{
    if(out_station == NULL) return false;
    SDL_LockMutex(g_state_mutex);
    const bool found = index < g_station_count;
    if(found) *out_station = g_stations[index];
    SDL_UnlockMutex(g_state_mutex);
    return found;
}

bool radio_service_is_favorite(const char * uuid)
{
    if(uuid == NULL) return false;
    SDL_LockMutex(g_state_mutex);
    const bool found = favorite_unlocked(uuid);
    SDL_UnlockMutex(g_state_mutex);
    return found;
}

bool radio_service_toggle_favorite(unsigned station_index)
{
    SDL_LockMutex(g_state_mutex);
    if(station_index >= g_station_count) {
        SDL_UnlockMutex(g_state_mutex);
        return false;
    }
    const char * uuid = g_stations[station_index].uuid;
    bool now_favorite = true;
    for(unsigned i = 0; i < g_favorite_count; ++i) {
        if(strcmp(g_favorites[i], uuid) == 0) {
            if(i + 1U < g_favorite_count) {
                memmove(g_favorites[i], g_favorites[i + 1U],
                        (g_favorite_count - i - 1U) * sizeof(g_favorites[0]));
            }
            --g_favorite_count;
            memset(g_favorites[g_favorite_count], 0, sizeof(g_favorites[0]));
            now_favorite = false;
            save_favorites_unlocked();
            SDL_UnlockMutex(g_state_mutex);
            return now_favorite;
        }
    }
    if(g_favorite_count < RADIO_MAX_STATIONS) {
        SDL_strlcpy(g_favorites[g_favorite_count++], uuid, sizeof(g_favorites[0]));
        save_favorites_unlocked();
    }
    SDL_UnlockMutex(g_state_mutex);
    return now_favorite;
}

bool radio_service_refresh(void)
{
    if(g_http_template < 0 || SDL_AtomicGet(&g_shutting_down) ||
       !SDL_AtomicCAS(&g_refresh_running, 0, 1)) return false;

    SDL_LockMutex(g_state_mutex);
    g_status.refreshing = true;
    if(g_station_count == 0) g_status.catalog_state = RADIO_CATALOG_LOADING;
    g_status.error_code = 0;
    SDL_UnlockMutex(g_state_mutex);

    void * thread = NULL;
    if(scePthreadCreate(&thread, NULL, refresh_thread, NULL, "radio-catalog") != 0) {
        SDL_AtomicSet(&g_refresh_running, 0);
        SDL_LockMutex(g_state_mutex);
        g_status.refreshing = false;
        if(g_station_count == 0) g_status.catalog_state = RADIO_CATALOG_ERROR;
        SDL_UnlockMutex(g_state_mutex);
        return false;
    }
    scePthreadDetach(thread);
    return true;
}

void radio_service_play(unsigned station_index)
{
    if(SDL_AtomicGet(&g_playback_running)) {
        radio_service_stop();
        return;
    }
    radio_station_t * station = malloc(sizeof(*station));
    if(station == NULL) return;
    SDL_LockMutex(g_state_mutex);
    if(station_index >= g_station_count) {
        SDL_UnlockMutex(g_state_mutex);
        free(station);
        return;
    }
    *station = g_stations[station_index];
    g_status.playing_index = station_index;
    g_status.playback_state = RADIO_PLAYBACK_CONNECTING;
    g_status.sample_rate = 0;
    g_status.channels = 0;
    g_status.error_code = 0;
    SDL_UnlockMutex(g_state_mutex);

    SDL_AtomicSet(&g_stop_playback, 0);
    SDL_AtomicSet(&g_playback_running, 1);
    void * thread = NULL;
    if(scePthreadCreate(&thread, NULL, playback_thread, station, "radio-audio") != 0) {
        SDL_AtomicSet(&g_playback_running, 0);
        free(station);
        set_playback_state(RADIO_PLAYBACK_ERROR, -1, 0, 0);
        return;
    }
    scePthreadDetach(thread);
}

void radio_service_stop(void)
{
    if(SDL_AtomicGet(&g_playback_running)) {
        SDL_AtomicSet(&g_stop_playback, 1);
        SDL_LockMutex(g_state_mutex);
        g_status.playback_state = RADIO_PLAYBACK_STOPPING;
        g_status.error_code = 0;
        g_status.sample_rate = 0;
        g_status.channels = 0;
        SDL_UnlockMutex(g_state_mutex);
        SDL_LockMutex(g_request_mutex);
        if(g_playback_request >= 0) sceHttpAbortRequest(g_playback_request);
        SDL_UnlockMutex(g_request_mutex);
    }
}
