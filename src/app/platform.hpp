// ProsperoRadio - What the interface asks of the platform besides the radio.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "radio_service.hpp"

#include <string>

// The sound of the station, shaped on its way out: the console applies it in
// the audio output thread, the PC stand-in only remembers it.
struct radio_audio_settings_t
{
    float volume = 1.0f;    // 0..1
    float bass_db = 0.0f;   // low shelf, -12..12
    float treble_db = 0.0f; // high shelf, -12..12
    float balance = 0.0f;   // -1 left .. 1 right
    bool mono = false;      // both speakers play the sum
};

// Takes effect within one audio block; any thread.
void radio_audio_configure(const radio_audio_settings_t &settings);
// What left the console a moment ago, 0..1 per side (peaks that fall back).
void radio_audio_meter(float *left, float *right);

// How many stations of the alphabetical list start under each mark of the
// letter rail: [0] digits and signs that sort before A, [1..26] A to Z, [27]
// signs after Z and other scripts, in the list's own order. False when unknown.
constexpr int kInitialBuckets = 28;
bool radio_catalog_initials(bool favorites_only, unsigned counts[kInitialBuckets]);
// For a list in any order: the position of the first station under each
// mark, UINT_MAX when there is none. False when unknown.
bool radio_catalog_letter_starts(const radio_catalog_query_t *query, radio_catalog_order_t order,
                                 bool favorites_only, unsigned starts[kInitialBuckets]);

// A newer release on homebrew.page. The check runs once per launch, on its own
// thread; a failed check says nothing.
struct radio_update_t
{
    std::string version; // the release's name
    std::string page;    // where it is listed
};
void radio_update_check_start(const char *installed_version);
// True once, when a newer release was found.
bool radio_update_take(radio_update_t *update);

// The settings file: text the app writes and reads back, nothing more.
bool radio_settings_load(std::string *text);
bool radio_settings_save(const std::string &text);
