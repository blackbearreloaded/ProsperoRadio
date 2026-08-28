// PS5 Radio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "radio_playlist.hpp"

#include <stdbool.h>
#include <string.h>

struct text_slice_t
{
    const char *data;
    size_t size;
};

static char ascii_lower(char value)
{
    return value >= 'A' && value <= 'Z' ? (char)(value + ('a' - 'A')) : value;
}

static bool equal_case(text_slice_t value, const char *text)
{
    const size_t length = strlen(text);
    if (value.size != length)
        return false;
    for (size_t i = 0; i < length; ++i)
    {
        if (ascii_lower(value.data[i]) != ascii_lower(text[i]))
            return false;
    }
    return true;
}

static bool starts_case(text_slice_t value, const char *text)
{
    const size_t length = strlen(text);
    if (value.size < length)
        return false;
    for (size_t i = 0; i < length; ++i)
    {
        if (ascii_lower(value.data[i]) != ascii_lower(text[i]))
            return false;
    }
    return true;
}

static bool contains_case(const char *data, size_t size, const char *text)
{
    const size_t length = strlen(text);
    if (length == 0 || length > size)
        return false;
    for (size_t at = 0; at + length <= size; ++at)
    {
        size_t matched = 0;
        while (matched < length && ascii_lower(data[at + matched]) == ascii_lower(text[matched]))
        {
            ++matched;
        }
        if (matched == length)
            return true;
    }
    return false;
}

static text_slice_t trim(text_slice_t value)
{
    while (value.size && (value.data[0] == ' ' || value.data[0] == '\t'))
    {
        ++value.data;
        --value.size;
    }
    while (value.size &&
           (value.data[value.size - 1U] == ' ' || value.data[value.size - 1U] == '\t' ||
            value.data[value.size - 1U] == '\r'))
    {
        --value.size;
    }
    return value;
}

static bool valid_url_text(const char *value, size_t size)
{
    if (size == 0)
        return false;
    for (size_t i = 0; i < size; ++i)
    {
        const unsigned char byte = (unsigned char)value[i];
        if (byte <= 0x20U || byte == 0x7fU || value[i] == '#' || value[i] == '\\')
            return false;
    }
    return true;
}

static bool http_url(text_slice_t value)
{
    return starts_case(value, "http://") || starts_case(value, "https://");
}

static bool extension_is(const char *url, const char *extension)
{
    size_t end = 0;
    while (url[end] != '\0' && url[end] != '?' && url[end] != '#')
        ++end;
    const size_t length = strlen(extension);
    return end >= length && equal_case((text_slice_t){url + end - length, length}, extension);
}

radio_playlist_kind_t radio_playlist_kind_from_url(const char *url)
{
    if (url == nullptr)
        return RADIO_PLAYLIST_NONE;
    if (extension_is(url, ".m3u8"))
        return RADIO_PLAYLIST_HLS;
    if (extension_is(url, ".m3u"))
        return RADIO_PLAYLIST_M3U;
    if (extension_is(url, ".pls"))
        return RADIO_PLAYLIST_PLS;
    return RADIO_PLAYLIST_NONE;
}

radio_playlist_kind_t radio_playlist_kind_from_headers(const char *headers, size_t size)
{
    if (headers == nullptr || size == 0)
        return RADIO_PLAYLIST_NONE;
    if (contains_case(headers, size, "application/vnd.apple.mpegurl"))
        return RADIO_PLAYLIST_HLS;
    if (contains_case(headers, size, "audio/x-scpls") ||
        contains_case(headers, size, "application/pls"))
        return RADIO_PLAYLIST_PLS;
    if (contains_case(headers, size, "audio/mpegurl") ||
        contains_case(headers, size, "audio/x-mpegurl") ||
        contains_case(headers, size, "application/x-mpegurl"))
        return RADIO_PLAYLIST_M3U;
    return RADIO_PLAYLIST_NONE;
}

radio_playlist_kind_t radio_playlist_kind_from_body(const char *data, size_t size)
{
    if (data == nullptr || size == 0)
        return RADIO_PLAYLIST_NONE;
    if (size >= 3U && memcmp(data, "\xef\xbb\xbf", 3U) == 0)
    {
        data += 3U;
        size -= 3U;
    }
    if (contains_case(data, size, "#EXT-X-"))
        return RADIO_PLAYLIST_HLS;
    if (contains_case(data, size, "[playlist]") && contains_case(data, size, "file1="))
        return RADIO_PLAYLIST_PLS;
    if (contains_case(data, size, "#EXTM3U"))
        return RADIO_PLAYLIST_M3U;
    return RADIO_PLAYLIST_NONE;
}

static radio_playlist_result_t copy_url(text_slice_t value, char *output, size_t output_size)
{
    if (!valid_url_text(value.data, value.size))
        return RADIO_PLAYLIST_INVALID;
    if (value.size + 1U > output_size)
        return RADIO_PLAYLIST_LIMIT;
    memcpy(output, value.data, value.size);
    output[value.size] = '\0';
    return RADIO_PLAYLIST_OK;
}

radio_playlist_result_t radio_playlist_resolve_url(const char *base_url, const char *reference,
                                                   char *output, size_t output_size)
{
    if (base_url == nullptr || reference == nullptr || output == nullptr || output_size == 0)
        return RADIO_PLAYLIST_INVALID;

    text_slice_t ref = trim((text_slice_t){reference, strlen(reference)});
    if (!valid_url_text(ref.data, ref.size))
        return RADIO_PLAYLIST_INVALID;
    if (http_url(ref))
        return copy_url(ref, output, output_size);
    for (size_t i = 0; i < ref.size; ++i)
    {
        if (ref.data[i] == ':')
            return RADIO_PLAYLIST_INVALID;
        if (ref.data[i] == '/' || ref.data[i] == '?')
            break;
    }

    const char *scheme = strstr(base_url, "://");
    if (scheme == nullptr || (!starts_case((text_slice_t){base_url, strlen(base_url)}, "http://") &&
                              !starts_case((text_slice_t){base_url, strlen(base_url)}, "https://")))
        return RADIO_PLAYLIST_INVALID;
    const char *authority = scheme + 3;
    const char *base_end = base_url + strlen(base_url);
    const char *path = authority;
    while (path < base_end && *path != '/' && *path != '?')
        ++path;
    if (path == authority)
        return RADIO_PLAYLIST_INVALID;

    size_t prefix = 0;
    bool insert_slash = false;
    if (ref.size >= 2U && ref.data[0] == '/' && ref.data[1] == '/')
    {
        prefix = (size_t)(scheme - base_url) + 1U;
    }
    else if (ref.data[0] == '/')
    {
        prefix = (size_t)(path - base_url);
    }
    else if (ref.data[0] == '?')
    {
        const char *query = path;
        while (query < base_end && *query != '?')
            ++query;
        prefix = (size_t)(query - base_url);
    }
    else
    {
        const char *end = path;
        while (end < base_end && *end != '?')
            ++end;
        const char *slash = end;
        while (slash > path && slash[-1] != '/')
            --slash;
        prefix = slash > path ? (size_t)(slash - base_url) : (size_t)(path - base_url);
        insert_slash = slash == path;
    }

    if (prefix + (insert_slash ? 1U : 0U) + ref.size + 1U > output_size)
        return RADIO_PLAYLIST_LIMIT;
    memcpy(output, base_url, prefix);
    if (insert_slash)
        output[prefix++] = '/';
    memcpy(output + prefix, ref.data, ref.size);
    output[prefix + ref.size] = '\0';
    return http_url((text_slice_t){output, prefix + ref.size}) ? RADIO_PLAYLIST_OK
                                                               : RADIO_PLAYLIST_INVALID;
}

static bool pls_entry(text_slice_t line, text_slice_t *value)
{
    size_t equals = 0;
    while (equals < line.size && line.data[equals] != '=')
        ++equals;
    if (equals == line.size)
        return false;
    text_slice_t key = trim((text_slice_t){line.data, equals});
    if (key.size <= 4U || !starts_case(key, "file"))
        return false;
    for (size_t i = 4U; i < key.size; ++i)
    {
        if (key.data[i] < '0' || key.data[i] > '9')
            return false;
    }
    *value = trim((text_slice_t){line.data + equals + 1U, line.size - equals - 1U});
    return value->size != 0;
}

radio_playlist_result_t radio_playlist_first_url(radio_playlist_kind_t kind, const char *data,
                                                 size_t size, const char *playlist_url,
                                                 char *output, size_t output_size)
{
    if ((kind != RADIO_PLAYLIST_M3U && kind != RADIO_PLAYLIST_PLS) || data == nullptr ||
        size == 0 || playlist_url == nullptr || output == nullptr)
        return RADIO_PLAYLIST_INVALID;
    if (radio_playlist_kind_from_body(data, size) == RADIO_PLAYLIST_HLS)
        return RADIO_PLAYLIST_IS_HLS;
    if (memchr(data, '\0', size) != nullptr)
        return RADIO_PLAYLIST_INVALID;

    size_t position = 0;
    bool first = true;
    while (position < size)
    {
        size_t end = position;
        while (end < size && data[end] != '\n')
            ++end;
        text_slice_t line = trim((text_slice_t){data + position, end - position});
        if (first && line.size >= 3U && memcmp(line.data, "\xef\xbb\xbf", 3U) == 0)
        {
            line.data += 3U;
            line.size -= 3U;
            line = trim(line);
        }
        first = false;

        text_slice_t entry = {nullptr, 0};
        if (kind == RADIO_PLAYLIST_M3U)
        {
            if (line.size != 0 && line.data[0] != '#')
                entry = line;
        }
        else if (pls_entry(line, &entry))
        {
            /* Entry populated by pls_entry. */
        }
        if (entry.size != 0)
        {
            char reference[1024];
            if (entry.size + 1U > sizeof(reference))
                return RADIO_PLAYLIST_LIMIT;
            memcpy(reference, entry.data, entry.size);
            reference[entry.size] = '\0';
            return radio_playlist_resolve_url(playlist_url, reference, output, output_size);
        }
        position = end < size ? end + 1U : size;
    }
    return RADIO_PLAYLIST_NO_ENTRY;
}
