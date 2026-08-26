#include "radio_hls.h"

#include "radio_playlist.h"

#include <stdbool.h>
#include <string.h>

#define HLS_MAX_INPUT (128U * 1024U)
#define HLS_MAX_LINE 2048U

typedef struct {
    const char * data;
    size_t size;
} hls_slice_t;

static char ascii_lower(char value)
{
    return value >= 'A' && value <= 'Z' ? (char)(value + ('a' - 'A')) : value;
}

static hls_slice_t trim(hls_slice_t value)
{
    while(value.size && (value.data[0] == ' ' || value.data[0] == '\t')) {
        ++value.data;
        --value.size;
    }
    while(value.size && (value.data[value.size - 1U] == ' ' ||
                         value.data[value.size - 1U] == '\t' ||
                         value.data[value.size - 1U] == '\r')) {
        --value.size;
    }
    return value;
}

static bool equal(hls_slice_t value, const char * text)
{
    const size_t length = strlen(text);
    return value.size == length && memcmp(value.data, text, length) == 0;
}

static bool equal_case(hls_slice_t value, const char * text)
{
    const size_t length = strlen(text);
    if(value.size != length) return false;
    for(size_t i = 0; i < length; ++i) {
        if(ascii_lower(value.data[i]) != ascii_lower(text[i])) return false;
    }
    return true;
}

static bool starts(hls_slice_t value, const char * text)
{
    const size_t length = strlen(text);
    return value.size >= length && memcmp(value.data, text, length) == 0;
}

static bool starts_case(hls_slice_t value, const char * text)
{
    const size_t length = strlen(text);
    if(value.size < length) return false;
    for(size_t i = 0; i < length; ++i) {
        if(ascii_lower(value.data[i]) != ascii_lower(text[i])) return false;
    }
    return true;
}

static bool tag_value(hls_slice_t line, const char * tag, hls_slice_t * value)
{
    if(!starts(line, tag)) return false;
    value->data = line.data + strlen(tag);
    value->size = line.size - strlen(tag);
    *value = trim(*value);
    return true;
}

static bool parse_u64(hls_slice_t value, uint64_t * output)
{
    value = trim(value);
    if(value.size == 0) return false;
    uint64_t result = 0;
    for(size_t i = 0; i < value.size; ++i) {
        if(value.data[i] < '0' || value.data[i] > '9') return false;
        const uint64_t digit = (uint64_t)(value.data[i] - '0');
        if(result > (UINT64_MAX - digit) / 10U) return false;
        result = result * 10U + digit;
    }
    *output = result;
    return true;
}

static bool attribute(hls_slice_t list, const char * wanted,
                      hls_slice_t * output)
{
    size_t position = 0;
    while(position < list.size) {
        while(position < list.size && (list.data[position] == ' ' ||
                                       list.data[position] == '\t')) ++position;
        const size_t key_start = position;
        while(position < list.size && list.data[position] != '=' &&
              list.data[position] != ',') ++position;
        if(position >= list.size || list.data[position] != '=') return false;
        hls_slice_t key = trim((hls_slice_t){list.data + key_start,
                                             position - key_start});
        ++position;
        hls_slice_t value;
        if(position < list.size && list.data[position] == '"') {
            const size_t start = ++position;
            while(position < list.size && list.data[position] != '"') ++position;
            if(position >= list.size) return false;
            value = (hls_slice_t){list.data + start, position - start};
            ++position;
        }
        else {
            const size_t start = position;
            while(position < list.size && list.data[position] != ',') ++position;
            value = trim((hls_slice_t){list.data + start, position - start});
        }
        if(equal(key, wanted)) {
            *output = value;
            return value.size != 0;
        }
        while(position < list.size && list.data[position] != ',') ++position;
        if(position < list.size) ++position;
    }
    return false;
}

static bool audio_only_codecs(hls_slice_t attributes)
{
    hls_slice_t codecs;
    if(!attribute(attributes, "CODECS", &codecs)) return true;
    size_t position = 0;
    bool found_aac = false;
    while(position < codecs.size) {
        const size_t start = position;
        while(position < codecs.size && codecs.data[position] != ',') ++position;
        hls_slice_t codec = trim((hls_slice_t){codecs.data + start,
                                               position - start});
        if(!starts_case(codec, "mp4a")) return false;
        found_aac = true;
        if(position < codecs.size) ++position;
    }
    return found_aac;
}

static bool fmp4_url(hls_slice_t value)
{
    size_t end = 0;
    while(end < value.size && value.data[end] != '?') ++end;
    const char * extensions[] = {".m4s", ".mp4", ".cmfa", ".cmfv"};
    for(size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); ++i) {
        const size_t length = strlen(extensions[i]);
        if(end >= length && starts_case(
            (hls_slice_t){value.data + end - length, length}, extensions[i]))
            return true;
    }
    return false;
}

static radio_hls_result_t resolve(hls_slice_t reference,
                                  const char * playlist_url,
                                  char * output, size_t output_size)
{
    char text[RADIO_HLS_URL_BYTES];
    if(reference.size + 1U > sizeof(text)) return RADIO_HLS_LIMIT;
    memcpy(text, reference.data, reference.size);
    text[reference.size] = '\0';
    const radio_playlist_result_t result = radio_playlist_resolve_url(
        playlist_url, text, output, output_size);
    return result == RADIO_PLAYLIST_OK ? RADIO_HLS_OK :
           result == RADIO_PLAYLIST_LIMIT ? RADIO_HLS_LIMIT : RADIO_HLS_INVALID;
}

radio_hls_result_t radio_hls_parse(const char * data, size_t size,
                                   const char * playlist_url,
                                   radio_hls_playlist_t * playlist)
{
    if(data == NULL || size == 0 || playlist_url == NULL || playlist == NULL)
        return RADIO_HLS_INVALID;
    if(size > HLS_MAX_INPUT || memchr(data, '\0', size) != NULL)
        return RADIO_HLS_LIMIT;
    memset(playlist, 0, sizeof(*playlist));
    playlist->is_live = 1U;

    bool header = false;
    bool pending_variant = false;
    bool pending_segment = false;
    bool discontinuity = false;
    uint64_t sequence = 0;
    uint64_t variant_bandwidth = 0;

    size_t position = 0;
    while(position < size) {
        size_t end = position;
        while(end < size && data[end] != '\n') ++end;
        if(end - position > HLS_MAX_LINE) return RADIO_HLS_LIMIT;
        hls_slice_t line = trim((hls_slice_t){data + position, end - position});
        if(!header && line.size >= 3U &&
           memcmp(line.data, "\xef\xbb\xbf", 3U) == 0) {
            line.data += 3U;
            line.size -= 3U;
            line = trim(line);
        }
        position = end < size ? end + 1U : size;
        if(line.size == 0) continue;
        if(!header) {
            if(!equal(line, "#EXTM3U")) return RADIO_HLS_MALFORMED;
            header = true;
            continue;
        }

        hls_slice_t value;
        if(tag_value(line, "#EXT-X-STREAM-INF:", &value)) {
            if(playlist->kind == RADIO_HLS_MEDIA || pending_variant ||
               !attribute(value, "BANDWIDTH", &line) ||
               !parse_u64(line, &variant_bandwidth) || variant_bandwidth == 0 ||
               !audio_only_codecs(value)) return RADIO_HLS_UNSUPPORTED;
            playlist->kind = RADIO_HLS_MASTER;
            pending_variant = true;
        }
        else if(tag_value(line, "#EXT-X-MEDIA-SEQUENCE:", &value)) {
            if(playlist->kind == RADIO_HLS_MASTER ||
               !parse_u64(value, &sequence)) return RADIO_HLS_MALFORMED;
            playlist->kind = RADIO_HLS_MEDIA;
            playlist->media_sequence = sequence;
        }
        else if(tag_value(line, "#EXT-X-TARGETDURATION:", &value)) {
            uint64_t seconds = 0;
            if(playlist->kind == RADIO_HLS_MASTER || !parse_u64(value, &seconds) ||
               seconds == 0 || seconds > UINT32_MAX / 1000U)
                return RADIO_HLS_MALFORMED;
            playlist->kind = RADIO_HLS_MEDIA;
            playlist->target_duration_ms = (uint32_t)seconds * 1000U;
        }
        else if(tag_value(line, "#EXTINF:", &value)) {
            if(playlist->kind == RADIO_HLS_MASTER || pending_segment)
                return RADIO_HLS_MALFORMED;
            playlist->kind = RADIO_HLS_MEDIA;
            pending_segment = true;
        }
        else if(equal(line, "#EXT-X-DISCONTINUITY")) {
            if(playlist->kind == RADIO_HLS_MASTER) return RADIO_HLS_MALFORMED;
            playlist->kind = RADIO_HLS_MEDIA;
            discontinuity = true;
        }
        else if(equal(line, "#EXT-X-ENDLIST")) {
            playlist->is_live = 0U;
        }
        else if(tag_value(line, "#EXT-X-KEY:", &value)) {
            hls_slice_t method;
            if(!attribute(value, "METHOD", &method) ||
               !equal_case(method, "NONE")) return RADIO_HLS_UNSUPPORTED;
        }
        else if(tag_value(line, "#EXT-X-DISCONTINUITY-SEQUENCE:", &value)) {
            uint64_t ignored = 0;
            if(!parse_u64(value, &ignored)) return RADIO_HLS_MALFORMED;
        }
        else if(starts(line, "#EXT-X-BYTERANGE:") ||
                starts(line, "#EXT-X-MAP:") ||
                starts(line, "#EXT-X-PART:") ||
                starts(line, "#EXT-X-PRELOAD-HINT:") ||
                starts(line, "#EXT-X-SKIP:") ||
                starts(line, "#EXT-X-MEDIA:")) {
            return RADIO_HLS_UNSUPPORTED;
        }
        else if(starts(line, "#EXT-X-") &&
                !starts(line, "#EXT-X-VERSION:") &&
                !starts(line, "#EXT-X-PROGRAM-DATE-TIME:") &&
                !starts(line, "#EXT-X-PLAYLIST-TYPE:") &&
                !starts(line, "#EXT-X-ALLOW-CACHE:") &&
                !starts(line, "#EXT-X-START:") &&
                !starts(line, "#EXT-X-INDEPENDENT-SEGMENTS")) {
            return RADIO_HLS_UNSUPPORTED;
        }
        else if(line.data[0] != '#') {
            if(pending_variant) {
                if(playlist->variant_count >= RADIO_HLS_MAX_VARIANTS)
                    return RADIO_HLS_LIMIT;
                radio_hls_variant_t * variant =
                    &playlist->variants[playlist->variant_count++];
                variant->bandwidth = variant_bandwidth;
                const radio_hls_result_t result = resolve(
                    line, playlist_url, variant->url, sizeof(variant->url));
                if(result != RADIO_HLS_OK) return result;
                pending_variant = false;
            }
            else if(pending_segment) {
                if(playlist->segment_count >= RADIO_HLS_MAX_SEGMENTS)
                    return RADIO_HLS_LIMIT;
                if(fmp4_url(line)) return RADIO_HLS_UNSUPPORTED;
                radio_hls_segment_t * segment =
                    &playlist->segments[playlist->segment_count++];
                segment->sequence = sequence++;
                segment->discontinuity = discontinuity ? 1U : 0U;
                discontinuity = false;
                const radio_hls_result_t result = resolve(
                    line, playlist_url, segment->url, sizeof(segment->url));
                if(result != RADIO_HLS_OK) return result;
                pending_segment = false;
            }
            else return RADIO_HLS_MALFORMED;
        }
    }
    if(!header || pending_variant || pending_segment ||
       (playlist->kind == RADIO_HLS_MASTER && playlist->variant_count == 0) ||
       (playlist->kind == RADIO_HLS_MEDIA && playlist->segment_count == 0))
        return RADIO_HLS_MALFORMED;
    if(playlist->kind == RADIO_HLS_MEDIA && playlist->target_duration_ms == 0)
        return RADIO_HLS_MALFORMED;
    return RADIO_HLS_OK;
}

int radio_hls_select_variant(const radio_hls_playlist_t * playlist)
{
    if(playlist == NULL || playlist->kind != RADIO_HLS_MASTER ||
       playlist->variant_count == 0) return RADIO_HLS_NO_VARIANT;
    uint32_t selected = 0;
    for(uint32_t i = 1; i < playlist->variant_count; ++i) {
        if(playlist->variants[i].bandwidth <
           playlist->variants[selected].bandwidth) selected = i;
    }
    return (int)selected;
}
