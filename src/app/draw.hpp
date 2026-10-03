// ProsperoRadio - Drawing the screens share: station tiles, chips, icons, text.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/kit.hpp"
#include "radio_service.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace radio
{

class Session;

// ---- words ----
// 56248 -> "56,248".
std::string group_digits(unsigned value);
// The first `limit` entries of a comma separated list, trimmed.
std::vector<std::string> split_list(const char *values, int limit);
// "United States · California · English": where a station is and speaks.
std::string place_line(const radio_station_t &station);
// "AAC · 320 kbps"; the bitrate is left out when the catalogue has none.
std::string codec_text(const radio_station_t &station);
// The same without the dot, for the monospaced line of a tile: "AAC 320 kbps".
std::string codec_short(const radio_station_t &station);

// ---- small parts ----
float chip_width(const ui::Painter &paint, std::string_view label);
// A chip as wide as its label, its left edge at x. Returns its width.
float draw_chip(ui::Painter &paint, float x, float cy, std::string_view label,
                float selected = 0.0f, float height = 40.0f);
// Four bars that move with the music; `bottom` is where they stand.
void draw_equalizer(gfx::DrawList &list, const Session &session, float x, float bottom,
                    float height, Color color);
// The app's mark: a transmitter and two waves.
void draw_logo(gfx::DrawList &list, float cx, float cy, float size, Color color);
// The frosted panel every floating part of a screen rests on.
void draw_glass(ui::Canvas &canvas, const ui::Theme &theme, const Rect &r, float radius = -1.0f);

// ---- icons, centred on (cx, cy), `size` tall ----
void draw_play_icon(gfx::DrawList &list, float cx, float cy, float size, Color color);
void draw_stop_icon(gfx::DrawList &list, float cx, float cy, float size, Color color);
// direction -1 is "previous", 1 is "next".
void draw_skip_icon(gfx::DrawList &list, float cx, float cy, float size, float direction,
                    Color color);
void draw_list_icon(gfx::DrawList &list, float cx, float cy, float size, Color color);

// ---- the station tile of the grids ----
// `cell` is where it goes, focus 0..1. rank ("#2") is drawn in the accent
// before detail ("497 plays"); either may be empty.
void draw_station_tile(ui::Canvas &canvas, const Session &session, const Rect &cell,
                       const radio_station_t &station, std::string_view rank,
                       std::string_view detail, bool favorite, float focus);
// The same shape while its page of the catalogue is still being read.
void draw_tile_placeholder(ui::Canvas &canvas, const Session &session, const Rect &cell);

} // namespace radio
