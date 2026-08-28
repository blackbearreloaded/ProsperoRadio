// PS5 Radio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "radio_playlist.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int condition, const char * message)
{
    if(condition) return;
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
}

int main(void)
{
    char url[512];
    const char m3u[] =
        "\xef\xbb\xbf#EXTM3U\r\n"
        "#EXTINF:-1,Example\r\n"
        "../live/stream.aac?token=one\r\n";
    check(radio_playlist_kind_from_url("https://radio.test/list.m3u?x=1") ==
          RADIO_PLAYLIST_M3U, "M3U URL detection");
    check(radio_playlist_first_url(
        RADIO_PLAYLIST_M3U, m3u, sizeof(m3u) - 1U,
        "https://radio.test/path/list.m3u", url, sizeof(url)) ==
          RADIO_PLAYLIST_OK, "M3U entry parsing");
    check(strcmp(url, "https://radio.test/path/../live/stream.aac?token=one") == 0,
          "relative M3U URL resolution");

    const char pls[] =
        "[playlist]\nNumberOfEntries=2\n"
        "Title1=Example\nFile1=https://stream.test/live.mp3\nVersion=2\n";
    check(radio_playlist_kind_from_body(pls, sizeof(pls) - 1U) ==
          RADIO_PLAYLIST_PLS, "PLS body detection");
    check(radio_playlist_first_url(
        RADIO_PLAYLIST_PLS, pls, sizeof(pls) - 1U,
        "https://radio.test/list.pls", url, sizeof(url)) ==
          RADIO_PLAYLIST_OK, "PLS entry parsing");
    check(strcmp(url, "https://stream.test/live.mp3") == 0,
          "absolute PLS URL retained");
    check(radio_playlist_resolve_url(
        "https://radio.test", "stream.aac", url, sizeof(url)) ==
          RADIO_PLAYLIST_OK &&
          strcmp(url, "https://radio.test/stream.aac") == 0,
          "relative URL resolves from origin root");

    const char hls[] =
        "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXTINF:6,\nsegment.ts\n";
    check(radio_playlist_kind_from_body(hls, sizeof(hls) - 1U) ==
          RADIO_PLAYLIST_HLS, "HLS body detection");
    check(radio_playlist_first_url(
        RADIO_PLAYLIST_M3U, hls, sizeof(hls) - 1U,
        "https://radio.test/live.m3u", url, sizeof(url)) ==
          RADIO_PLAYLIST_IS_HLS, "HLS is not treated as generic M3U");

    const char headers[] =
        "HTTP/1.1 200 OK\r\nContent-Type: audio/x-scpls\r\n";
    check(radio_playlist_kind_from_headers(headers, sizeof(headers) - 1U) ==
          RADIO_PLAYLIST_PLS, "PLS content type detection");
    check(radio_playlist_resolve_url(
        "https://radio.test/a/list.m3u", "ftp://bad.test/a",
        url, sizeof(url)) == RADIO_PLAYLIST_INVALID,
          "non-HTTP scheme rejected");

    puts("radio_playlist_check: PASS");
    return 0;
}
