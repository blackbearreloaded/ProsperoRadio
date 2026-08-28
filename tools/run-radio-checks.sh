#!/usr/bin/env bash
# PS5 Radio - Host-native codec and catalogue regression checks.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

root=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

cxx=${CXX:-clang++-18}
out=build/tests/radio-checks
cxxflags=(-std=c++20 -O2 -Wall -Wextra -Werror -Iinclude)
mkdir -p -- "$out"

run_check() {
    local name=$1
    shift
    printf '==> [radio-check] %s\n' "$name"
    "$cxx" "${cxxflags[@]}" "$@" -o "$out/$name"
    "$out/$name"
}

run_check aac-timing tools/aac_timing_check.cpp
run_check mp3-header tools/mp3_header_check.cpp
run_check icy-metadata tools/icy_metadata_check.cpp src/icy_metadata.cpp
run_check pcm-queue tools/pcm_queue_check.cpp
run_check playback-retry tools/playback_retry_check.cpp
run_check radio-input tools/radio_input_check.cpp
run_check radio-playlist tools/radio_playlist_check.cpp src/radio_playlist.cpp
run_check radio-hls tools/radio_hls_check.cpp src/radio_hls.cpp src/radio_playlist.cpp
run_check radio-ts-aac tools/radio_ts_aac_check.cpp src/radio_ts_aac.cpp
run_check ogg-opus tools/ogg_opus_check.cpp src/ogg_opus.cpp src/ogg_page.cpp
run_check ogg-stream tools/ogg_stream_check.cpp src/ogg_stream.cpp src/ogg_page.cpp
run_check opus-pcm tools/opus_pcm_check.cpp src/opus_pcm.cpp -lm
run_check vorbis-decoder tools/vorbis_decoder_check.cpp src/vorbis_decoder.cpp \
    src/ogg_stream.cpp src/ogg_page.cpp -lm
run_check flac-decoder tools/flac_decoder_check.cpp src/flac_decoder.cpp
run_check radio-service-json -Ivendor/ps5/sdl/include/SDL2 -ffunction-sections -fdata-sections \
    tools/radio_service_json_check.cpp -Wl,--gc-sections

read -r -a sqlite_cflags <<< "$(pkg-config --cflags sqlite3)"
read -r -a sqlite_libs <<< "$(pkg-config --libs sqlite3)"
run_check radio-catalog-store "${sqlite_cflags[@]}" \
    tools/radio_catalog_store_check.cpp src/radio_catalog_store.cpp "${sqlite_libs[@]}"
