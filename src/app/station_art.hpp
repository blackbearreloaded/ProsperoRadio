// ProsperoRadio - Generated artwork: every station gets a colour and an emblem.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/kit.hpp"
#include "radio_service.hpp"

namespace radio
{

// Radio Browser stations carry no artwork the app can rely on, so the art is
// drawn: a two-tone ground, one of six emblems and the country code, all
// chosen by the station's id. The same station always looks the same.
struct ArtColors
{
    Color top;
    Color bottom;
    Color accent; // the light it casts: glows, the backdrop's lean, the visualizer
};

ArtColors art_colors(const char *uuid);

// Draws the artwork into the square r. It is shapes only (no texture and no
// clip), so a grid of them stays inside one draw call.
void draw_station_art(gfx::DrawList &list, const ui::Fonts &fonts, const Rect &r, float radius,
                      const radio_station_t &station);

} // namespace radio
