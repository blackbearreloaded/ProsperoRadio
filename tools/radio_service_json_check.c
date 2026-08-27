#include "../src/radio_service.c"

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
                         &station, 1U) == 0U);

    static const char mislabeled_mp3_catalog[] =
        "[{\"stationuuid\":\"bad-ogg\",\"name\":\"Mislabeled OGG\","
        "\"url_resolved\":\"https://example.invalid/live.mp3\","
        "\"codec\":\"OGG\",\"hls\":0,\"lastcheckok\":1}]";
    assert(parse_catalog(mislabeled_mp3_catalog,
                         sizeof(mislabeled_mp3_catalog) - 1U,
                         &station, 1U) == 0U);

    uint8_t opus_page[36] = {0};
    memcpy(opus_page, "OggS", 4U);
    opus_page[26] = 1U;
    opus_page[27] = 8U;
    memcpy(opus_page + 28U, "OpusHead", 8U);
    assert(ogg_probe(opus_page, 20U) == OGG_FORMAT_NEED_MORE);
    assert(ogg_probe(opus_page, sizeof(opus_page)) == OGG_FORMAT_OPUS);
    uint8_t vorbis_page[36] = {0};
    memcpy(vorbis_page, "OggS", 4U);
    vorbis_page[26] = 1U;
    vorbis_page[27] = 8U;
    vorbis_page[28] = 1U;
    memcpy(vorbis_page + 29U, "vorbis", 6U);
    assert(ogg_probe(vorbis_page, sizeof(vorbis_page)) == OGG_FORMAT_VORBIS);

    static const char hls_catalog[] =
        "[{\"stationuuid\":\"hls-test\",\"name\":\"HLS Test\","
        "\"url_resolved\":\"https://example.invalid/live.m3u8\","
        "\"codec\":\"AAC\",\"hls\":1,\"lastcheckok\":1}]";
    assert(parse_catalog(hls_catalog, sizeof(hls_catalog) - 1U,
                         &station, 1U) == 1U);
    assert(strcmp(station.codec, "AAC") == 0 && station.hls == 1U);

    assert(sink_ready_target(false, false) == AUDIO_START_BLOCKS);
    assert(sink_ready_target(false, true) == AUDIO_RESTART_BLOCKS);
    assert(sink_ready_target(true, false) == 1U);
    static const uint8_t celt_packet[] = {0xf8U};
    static const uint8_t silk_packet[] = {0x78U};
    assert(opus_packet_is_celt(celt_packet, sizeof(celt_packet)));
    assert(!opus_packet_is_celt(silk_packet, sizeof(silk_packet)));
    assert(!opus_packet_is_celt(NULL, 0U));
    return 0;
}
