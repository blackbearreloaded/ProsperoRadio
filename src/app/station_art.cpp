// ProsperoRadio - Generated artwork: every station gets a colour and an emblem.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/station_art.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>

namespace radio
{

namespace
{

struct Ground
{
    std::uint32_t top;
    std::uint32_t bottom;
    std::uint32_t accent;
};

constexpr Ground kGrounds[] = {
    {0x219395, 0x0b3140, 0x6cdbd9}, // teal
    {0xc9683c, 0x471b13, 0xffb38a}, // ember
    {0x3fa474, 0x0f3325, 0x8ff0c0}, // green
    {0x8059b8, 0x231846, 0xc9a8ff}, // violet
    {0xc3923c, 0x3a280e, 0xffd98a}, // amber
    {0x3f7dc0, 0x102446, 0x9cc8ff}, // blue
    {0xb85a82, 0x3a1526, 0xffa8c8}, // rose
    {0x62a267, 0x19301d, 0xb4eeb8}, // moss
};

constexpr int kEmblems = 6;

// FNV-1a: stable across builds, which a std::hash is not promised to be.
std::uint32_t hash(const char *text)
{
    std::uint32_t value = 2166136261u;
    for (; text != nullptr && *text != '\0'; ++text)
        value = (value ^ static_cast<unsigned char>(*text)) * 16777619u;
    // Ids that differ in one digit must not end up neighbours: stir the bits.
    value ^= value >> 16;
    value *= 0x85ebca6bu;
    value ^= value >> 13;
    value *= 0xc2b2ae35u;
    value ^= value >> 16;
    return value;
}

// Every emblem keeps inside the middle of the square: there is no rounded
// clip, so nothing may reach the corners.
void draw_emblem(gfx::DrawList &list, int emblem, const Rect &r, std::uint32_t seed)
{
    const float s = r.w;
    const float cx = r.cx();
    const Color light = kWhite;
    switch (emblem)
    {
    case 0: // a signal fanning out from a transmitter
    {
        const float cy = r.y + s * 0.66f;
        for (int i = 0; i < 4; ++i)
        {
            const float radius = s * (0.17f + 0.095f * static_cast<float>(i));
            list.arc(cx, cy, radius, s * 0.034f, -1.0f, 2.0f,
                     light.with_alpha(0.78f - 0.17f * static_cast<float>(i)));
        }
        list.circle(cx, cy, s * 0.05f, light.with_alpha(0.92f));
        break;
    }
    case 1: // a tuning dial
    {
        const float cy = r.y + s * 0.43f;
        list.ring(cx, cy, s * 0.27f, s * 0.026f, light.with_alpha(0.4f));
        list.arc(cx, cy, s * 0.27f, s * 0.026f, -0.6f, 1.9f, light.with_alpha(0.85f));
        list.circle(cx, cy, s * 0.13f, light.with_alpha(0.9f));
        break;
    }
    case 2: // slanted rays
        for (int i = 0; i < 5; ++i)
        {
            const float x = r.x + s * (0.2f + 0.125f * static_cast<float>(i));
            const float foot = r.y + s * (0.64f - 0.07f * static_cast<float>(i % 2));
            list.line(x, foot, x + s * 0.13f, r.y + s * 0.2f, s * (i % 2 == 0 ? 0.05f : 0.032f),
                      light.with_alpha(i % 2 == 0 ? 0.62f : 0.36f));
        }
        break;
    case 3: // a lattice of dots, a few of them lit
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 4; ++column)
            {
                const std::uint32_t bit = (seed >> (row * 4 + column)) & 3u;
                list.circle(r.x + s * (0.23f + 0.18f * static_cast<float>(column)),
                            r.y + s * (0.22f + 0.17f * static_cast<float>(row)),
                            s * (0.035f + 0.012f * static_cast<float>(bit)),
                            light.with_alpha(bit == 3u ? 0.9f : 0.42f));
            }
        break;
    case 4: // level bars
        for (int i = 0; i < 7; ++i)
        {
            const float level = 0.3f + 0.7f * static_cast<float>((seed >> (i * 3)) & 7u) / 7.0f;
            const float w = s * 0.055f;
            const float h = s * 0.4f * level;
            list.rounded_rect(
                {r.x + s * (0.2f + 0.094f * static_cast<float>(i)), r.y + s * 0.64f - h, w, h},
                w * 0.5f, light.with_alpha(0.3f + 0.5f * level));
        }
        break;
    default: // a sun over a horizon
    {
        const float cy = r.y + s * 0.52f;
        list.arc(cx, cy, s * 0.22f, s * 0.22f, -1.5708f, 3.14159f, light.with_alpha(0.86f), false);
        list.line(r.x + s * 0.2f, cy + s * 0.035f, r.x + s * 0.8f, cy + s * 0.035f, s * 0.022f,
                  light.with_alpha(0.7f));
        list.line(r.x + s * 0.3f, cy + s * 0.1f, r.x + s * 0.7f, cy + s * 0.1f, s * 0.016f,
                  light.with_alpha(0.4f));
        break;
    }
    }
}

} // namespace

ArtColors art_colors(const char *uuid)
{
    const Ground &ground = kGrounds[hash(uuid) % std::size(kGrounds)];
    return {Color::rgb(ground.top), Color::rgb(ground.bottom), Color::rgb(ground.accent)};
}

void draw_station_art(gfx::DrawList &list, const ui::Fonts &fonts, const Rect &r, float radius,
                      const radio_station_t &station)
{
    const std::uint32_t seed = hash(station.uuid);
    const ArtColors colors = art_colors(station.uuid);
    list.gradient_rect(r, radius, colors.top, colors.bottom);
    draw_emblem(list, static_cast<int>((seed >> 8) % kEmblems), r, seed >> 11);
    if (station.country_code[0] != '\0')
    {
        const float size = std::clamp(r.w * 0.11f, 13.0f, 26.0f);
        ui::text(list, fonts.mono, ui::upper(station.country_code), r.x + r.w * 0.1f,
                 r.y + r.h * 0.9f, size, kWhite.with_alpha(0.85f), gfx::Align::left, size * 0.12f);
    }
    // A hairline of light is what makes a flat square read as an object.
    list.bordered_rect(r, radius, kClear, std::max(1.5f, r.w * 0.006f), kWhite.with_alpha(0.16f));
}

} // namespace radio
