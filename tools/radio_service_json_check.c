#include "../src/radio_service.c"

#include <assert.h>

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
    return 0;
}
