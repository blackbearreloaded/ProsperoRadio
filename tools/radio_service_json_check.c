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
    return 0;
}
