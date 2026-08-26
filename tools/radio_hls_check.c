#include "radio_hls.h"

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
    radio_hls_playlist_t playlist;
    const char master[] =
        "#EXTM3U\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=128000,CODECS=\"mp4a.40.2\"\n"
        "high/audio.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=64000,CODECS=\"mp4a.40.2\"\n"
        "low/audio.m3u8?token=x\n";
    check(radio_hls_parse(master, sizeof(master) - 1U,
                          "https://radio.test/live/master.m3u8", &playlist) ==
          RADIO_HLS_OK, "audio-only master parses");
    check(playlist.kind == RADIO_HLS_MASTER && playlist.variant_count == 2U,
          "master variants retained");
    const int selected = radio_hls_select_variant(&playlist);
    check(selected == 1 && strcmp(playlist.variants[selected].url,
                                  "https://radio.test/live/low/audio.m3u8?token=x") == 0,
          "lowest-bandwidth audio variant selected");

    const char media[] =
        "#EXTM3U\r\n#EXT-X-TARGETDURATION:6\r\n"
        "#EXT-X-MEDIA-SEQUENCE:42\r\n"
        "#EXTINF:6.0,\r\nseg42.ts\r\n"
        "#EXT-X-DISCONTINUITY\r\n"
        "#EXTINF:6.0,\r\n../seg43.ts?key=one\r\n";
    check(radio_hls_parse(media, sizeof(media) - 1U,
                          "https://radio.test/live/audio/index.m3u8", &playlist) ==
          RADIO_HLS_OK, "live media playlist parses");
    check(playlist.kind == RADIO_HLS_MEDIA && playlist.is_live == 1U &&
          playlist.target_duration_ms == 6000U && playlist.segment_count == 2U,
          "media metadata retained");
    check(playlist.segments[0].sequence == 42U &&
          playlist.segments[1].sequence == 43U &&
          playlist.segments[1].discontinuity == 1U,
          "sequence and discontinuity retained");

    const char video[] =
        "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=500000,CODECS=\"avc1.4d401f,mp4a.40.2\"\n"
        "video.m3u8\n";
    check(radio_hls_parse(video, sizeof(video) - 1U,
                          "https://radio.test/master.m3u8", &playlist) ==
          RADIO_HLS_UNSUPPORTED, "video variant rejected");
    const char encrypted[] =
        "#EXTM3U\n#EXT-X-TARGETDURATION:6\n"
        "#EXT-X-KEY:METHOD=AES-128,URI=\"key.bin\"\n"
        "#EXTINF:6,\nseg.ts\n";
    check(radio_hls_parse(encrypted, sizeof(encrypted) - 1U,
                          "https://radio.test/index.m3u8", &playlist) ==
          RADIO_HLS_UNSUPPORTED, "encrypted media rejected");

    puts("radio_hls_check: PASS");
    return 0;
}
