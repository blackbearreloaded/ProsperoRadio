// ProsperoRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../src/radio_service.cpp"

#include <assert.h>

size_t SDL_strlcpy(char * destination, const char * source, size_t capacity)
{
    const size_t length = strlen(source);
    if(capacity != 0U) {
        const size_t copy = length < capacity - 1U ? length : capacity - 1U;
        memcpy(destination, source, copy);
        destination[copy] = '\0';
    }
    return length;
}

int main(void)
{
    static const char raw[] = "{\"name\":\"Радио Москва\"}";
    static const char escaped[] =
        "{\"name\":\"\\u0420\\u0430\\u0434\\u0438\\u043e Москва\"}";
    char name[112];

    assert(json_string_field(raw, raw + sizeof(raw) - 1U,
                             "name", name, sizeof(name)));
    assert(strcmp(name, "Радио Москва") == 0);
    assert(json_string_field(escaped, escaped + sizeof(escaped) - 1U,
                             "name", name, sizeof(name)));
    assert(strcmp(name, "Радио Москва") == 0);

    static const char mp3_catalog[] =
        "[{\"stationuuid\":\"mp3-test\",\"name\":\"MP3 Test\","
        "\"url_resolved\":\"https://example.invalid/live.mp3\","
        "\"codec\":\"MP3\",\"hls\":0,\"lastcheckok\":1}]";
    radio_station_t station;
    assert(parse_catalog(mp3_catalog, sizeof(mp3_catalog) - 1U, &station, 1U) == 1U);
    assert(strcmp(station.codec, "MP3") == 0);

    static const char aac_plus_catalog[] =
        "[{\"stationuuid\":\"aac-plus-test\",\"name\":\"AAC+ Test\","
        "\"url_resolved\":\"https://example.invalid/live.aac\","
        "\"codec\":\"AAC+\",\"hls\":0,\"lastcheckok\":1}]";
    assert(parse_catalog(aac_plus_catalog, sizeof(aac_plus_catalog) - 1U,
                         &station, 1U) == 1U);
    assert(strcmp(station.codec, "AAC+") == 0);

    static const char vorbis_catalog[] =
        "[{\"stationuuid\":\"vorbis-test\",\"name\":\"Vorbis Test\","
        "\"url_resolved\":\"https://example.invalid/live.ogg\","
        "\"codec\":\"OGG\",\"hls\":0,\"lastcheckok\":1}]";
    assert(parse_catalog(vorbis_catalog, sizeof(vorbis_catalog) - 1U,
                         &station, 1U) == 1U);
    assert(strcmp(station.codec, "OGG") == 0);

    static const char opus_catalog[] =
        "[{\"stationuuid\":\"opus-test\",\"name\":\"Opus Test\","
        "\"url_resolved\":\"https://example.invalid/live-opus.ogg\","
        "\"codec\":\"OGG\",\"hls\":0,\"lastcheckok\":1}]";
    assert(parse_catalog(opus_catalog, sizeof(opus_catalog) - 1U,
                         &station, 1U) == 1U);
    assert(strcmp(station.codec, "OPUS") == 0);

    static const char ogg_flac_catalog[] =
        "[{\"stationuuid\":\"flac-test\",\"name\":\"Lossless FLAC\","
        "\"url_resolved\":\"https://example.invalid/lossless.ogg\","
        "\"codec\":\"OGG\",\"hls\":0,\"lastcheckok\":1}]";
    assert(parse_catalog(ogg_flac_catalog, sizeof(ogg_flac_catalog) - 1U,
                         &station, 1U) == 1U);
    assert(strcmp(station.codec, "FLAC") == 0);

    static const char native_flac_catalog[] =
        "[{\"stationuuid\":\"native-flac-test\",\"name\":\"Native FLAC\","
        "\"url_resolved\":\"https://example.invalid/lossless.flac\","
        "\"codec\":\"FLAC\",\"hls\":0,\"lastcheckok\":1}]";
    assert(parse_catalog(native_flac_catalog,
                         sizeof(native_flac_catalog) - 1U,
                         &station, 1U) == 1U);
    assert(strcmp(station.codec, "FLAC") == 0);

    static const char mislabeled_mp3_catalog[] =
        "[{\"stationuuid\":\"bad-ogg\",\"name\":\"Mislabeled OGG\","
        "\"url_resolved\":\"https://example.invalid/live.mp3\","
        "\"codec\":\"OGG\",\"hls\":0,\"lastcheckok\":1}]";
    assert(parse_catalog(mislabeled_mp3_catalog,
                         sizeof(mislabeled_mp3_catalog) - 1U,
                         &station, 1U) == 0U);

    uint8_t opus_page[36] = {};
    memcpy(opus_page, "OggS", 4U);
    opus_page[26] = 1U;
    opus_page[27] = 8U;
    memcpy(opus_page + 28U, "OpusHead", 8U);
    assert(ogg_probe(opus_page, 20U) == OGG_FORMAT_NEED_MORE);
    assert(ogg_probe(opus_page, sizeof(opus_page)) == OGG_FORMAT_OPUS);
    uint8_t vorbis_page[36] = {};
    memcpy(vorbis_page, "OggS", 4U);
    vorbis_page[26] = 1U;
    vorbis_page[27] = 8U;
    vorbis_page[28] = 1U;
    memcpy(vorbis_page + 29U, "vorbis", 6U);
    assert(ogg_probe(vorbis_page, sizeof(vorbis_page)) == OGG_FORMAT_VORBIS);
    uint8_t flac_page[36] = {};
    memcpy(flac_page, "OggS", 4U);
    flac_page[26] = 1U;
    flac_page[27] = 8U;
    flac_page[28] = 0x7fU;
    memcpy(flac_page + 29U, "FLAC", 4U);
    assert(ogg_probe(flac_page, sizeof(flac_page)) == OGG_FORMAT_FLAC);

    static const char hls_catalog[] =
        "[{\"stationuuid\":\"hls-test\",\"name\":\"HLS Test\","
        "\"url_resolved\":\"https://example.invalid/live.m3u8\","
        "\"codec\":\"AAC\",\"hls\":1,\"lastcheckok\":1}]";
    assert(parse_catalog(hls_catalog, sizeof(hls_catalog) - 1U,
                         &station, 1U) == 1U);
    assert(strcmp(station.codec, "AAC") == 0 && station.hls == 1U);

    radio_catalog_query_t query = {};
    strcpy(query.name, "Radio São");
    strcpy(query.country_code, "BR");
    strcpy(query.tag, "jazz fusion");
    strcpy(query.language, "português");
    query.bitrate_min = 128U;
    char path[1200];
    assert(build_search_path(path, sizeof(path), "AAC+", &query, 10000U));
    assert(strstr(path, "limit=10000&offset=10000") != nullptr);
    assert(strstr(path, "codec=AAC%2B") != nullptr);
    assert(strstr(path, "name=Radio%20S%C3%A3o") != nullptr);
    assert(strstr(path, "countrycode=BR") != nullptr);
    assert(strstr(path, "tag=jazz%20fusion") != nullptr);
    assert(strstr(path, "language=portugu%C3%AAs") != nullptr);
    assert(strstr(path, "bitrateMin=128") != nullptr);
    assert(json_object_count("[{\"a\":{}},{\"b\":2}]", 20U) == 2U);
    assert(valid_mirror_name("de1.api.radio-browser.info"));
    assert(!valid_mirror_name("radio-browser.info/evil"));
    assert(!valid_mirror_name("de1.api.radio-browser.info.attacker.invalid"));

    static const char country_facets[] =
        "[{\"name\":\"Germany\",\"iso_3166_1\":\"DE\","
        "\"stationcount\":1234}]";
    radio_facet_t facet;
    assert(parse_facets(country_facets, sizeof(country_facets) - 1U,
                        RADIO_FACET_COUNTRY, &facet, 1U) == 1U);
    assert(strcmp(facet.value, "DE") == 0);
    assert(strcmp(facet.label, "Germany") == 0);
    assert(facet.station_count == 1234U);

    static const char long_country_facet[] =
        "[{\"name\":\"The United Kingdom Of Great Britain And Northern Ireland\","
        "\"iso_3166_1\":\"GB\",\"stationcount\":2192}]";
    assert(parse_facets(long_country_facet,
                        sizeof(long_country_facet) - 1U,
                        RADIO_FACET_COUNTRY, &facet, 1U) == 1U);
    assert(strcmp(facet.value, "GB") == 0);
    assert(strcmp(facet.label, "United Kingdom") == 0);
    assert(facet.station_count == 2192U);

    assert(sink_ready_target(false, false) == AUDIO_START_BLOCKS);
    assert(sink_ready_target(false, true) == AUDIO_RESTART_BLOCKS);
    assert(sink_ready_target(true, false) == 1U);
    static const uint8_t celt_packet[] = {0xf8U};
    static const uint8_t silk_packet[] = {0x78U};
    assert(opus_packet_is_celt(celt_packet, sizeof(celt_packet)));
    assert(!opus_packet_is_celt(silk_packet, sizeof(silk_packet)));
    assert(!opus_packet_is_celt(nullptr, 0U));

    hls_reader_t hls_reader;
    memset(&hls_reader, 0, sizeof(hls_reader));
    hls_reader.discontinuity_pending = true;
    assert(hls_take_discontinuity(&hls_reader) == STREAM_READ_DISCONTINUITY);
    assert(!hls_reader.discontinuity_pending);
    assert(hls_take_discontinuity(&hls_reader) == 0);
    return 0;
}
