// ProsperoRadio - Drawing the screens share: station tiles, chips, icons, text.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/draw.hpp"

#include "app/session.hpp"
#include "app/station_art.hpp"
#include "app/theme.hpp"
#include "ui/components/overlay.hpp"
#include "ui/components/progress.hpp"

#include <algorithm>
#include <cstdio>

namespace radio
{

namespace
{

constexpr const char *kDot = "  \xC2\xB7  "; // a middle dot between two facts

std::string capitalised(std::string text)
{
    if (!text.empty() && text[0] >= 'a' && text[0] <= 'z')
        text[0] = static_cast<char>(text[0] - 'a' + 'A');
    return text;
}

// A triangle pointing left (-1) or right (1). The kit's triangle only points
// up and polygons are not anti-aliased, so it is three thick round lines.
void side_triangle(gfx::DrawList &list, float cx, float cy, float size, float direction,
                   Color color)
{
    const float stroke = size * 0.4f;
    const float half = size * 0.3f;
    const float back = cx - direction * size * 0.22f;
    const float tip = cx + direction * size * 0.3f;
    list.line(back, cy - half, back, cy + half, stroke, color);
    list.line(back, cy - half, tip, cy, stroke, color);
    list.line(back, cy + half, tip, cy, stroke, color);
}

} // namespace

std::string group_digits(unsigned value)
{
    char digits[16];
    std::snprintf(digits, sizeof(digits), "%u", value);
    std::string text(digits);
    for (int at = static_cast<int>(text.size()) - 3; at > 0; at -= 3)
        text.insert(static_cast<std::size_t>(at), ",");
    return text;
}

std::vector<std::string> split_list(const char *values, int limit)
{
    std::vector<std::string> parts;
    std::string current;
    const auto flush = [&]()
    {
        while (!current.empty() && current.back() == ' ')
            current.pop_back();
        if (!current.empty() && static_cast<int>(parts.size()) < limit)
            parts.push_back(current);
        current.clear();
    };
    for (; values != nullptr && *values != '\0'; ++values)
    {
        if (*values == ',')
            flush();
        else if (*values != ' ' || !current.empty())
            current.push_back(*values);
    }
    flush();
    return parts;
}

std::string place_line(const radio_station_t &station)
{
    std::string line = station.country[0] != '\0' ? station.country : "Worldwide";
    if (station.state[0] != '\0')
        line += std::string(kDot) + station.state;
    const std::vector<std::string> languages = split_list(station.language, 1);
    if (!languages.empty())
        line += std::string(kDot) + capitalised(languages[0]);
    return line;
}

std::string codec_text(const radio_station_t &station)
{
    std::string text = station.codec[0] != '\0' ? ui::upper(station.codec) : "Stream";
    if (station.bitrate != 0U)
    {
        char rate[24];
        std::snprintf(rate, sizeof(rate), "%u kbps", station.bitrate);
        text += std::string(kDot) + rate;
    }
    return text;
}

std::string codec_short(const radio_station_t &station)
{
    std::string text = station.codec[0] != '\0' ? ui::upper(station.codec) : "Stream";
    if (station.bitrate != 0U)
    {
        char rate[24];
        std::snprintf(rate, sizeof(rate), " %u kbps", station.bitrate);
        text += rate;
    }
    return text;
}

float chip_width(const ui::Painter &paint, std::string_view label)
{
    return paint.label_width(label, 19.0f) + 34.0f;
}

float draw_chip(ui::Painter &paint, float x, float cy, std::string_view label, float selected,
                float height)
{
    const float width = chip_width(paint, label);
    paint.chip({x, cy - height * 0.5f, width, height}, label, selected, {});
    return width;
}

void draw_equalizer(gfx::DrawList &list, const Session &session, float x, float bottom,
                    float height, Color color)
{
    constexpr int kSource[4] = {2, 8, 14, 21};
    const float width = height * 0.2f;
    for (int i = 0; i < 4; ++i)
    {
        const float level = session.levels[static_cast<std::size_t>(kSource[i])];
        const float h = width + (height - width) * level;
        list.rounded_rect({x + static_cast<float>(i) * width * 1.7f, bottom - h, width, h},
                          width * 0.5f, color);
    }
}

void draw_logo(gfx::DrawList &list, float cx, float cy, float size, Color color)
{
    const float s = size;
    list.ring(cx, cy, s * 0.5f, s * 0.045f, color.with_alpha(0.5f));
    const float base = cy + s * 0.2f;
    list.arc(cx, base, s * 0.36f, s * 0.06f, -0.85f, 1.7f, color.with_alpha(0.7f));
    list.arc(cx, base, s * 0.22f, s * 0.06f, -0.85f, 1.7f, color);
    list.circle(cx, base, s * 0.07f, color);
}

void draw_glass(ui::Canvas &canvas, const ui::Theme &theme, const Rect &r, float radius)
{
    ui::draw_overlay_panel(canvas, theme, r, true, 0.55f, radius);
}

void draw_play_icon(gfx::DrawList &list, float cx, float cy, float size, Color color)
{
    side_triangle(list, cx + size * 0.05f, cy, size, 1.0f, color);
}

void draw_stop_icon(gfx::DrawList &list, float cx, float cy, float size, Color color)
{
    const float side = size * 0.78f;
    list.rounded_rect({cx - side * 0.5f, cy - side * 0.5f, side, side}, side * 0.16f, color);
}

void draw_skip_icon(gfx::DrawList &list, float cx, float cy, float size, float direction,
                    Color color)
{
    side_triangle(list, cx - direction * size * 0.25f, cy, size, direction, color);
    list.line(cx + direction * size * 0.5f, cy - size * 0.38f, cx + direction * size * 0.5f,
              cy + size * 0.38f, size * 0.19f, color);
}

void draw_list_icon(gfx::DrawList &list, float cx, float cy, float size, Color color)
{
    const float half = size * 0.55f;
    const float step = size * 0.34f;
    for (int i = -1; i <= 1; ++i)
    {
        const float y = cy + static_cast<float>(i) * step;
        list.line(cx - half, y, cx + (i == 1 ? half * 0.3f : half), y, size * 0.13f, color);
    }
}

void draw_station_tile(ui::Canvas &canvas, const Session &session, const Rect &cell,
                       const radio_station_t &station, std::string_view rank,
                       std::string_view detail, bool favorite, float focus)
{
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const ui::Theme &theme = session.theme;
    constexpr float kRadius = 20.0f;
    constexpr float kArt = 112.0f;

    // At rest a tile is a veil over the page. In focus it turns solid: the
    // light the grid puts around it must not shine through and wash the text.
    list.rounded_rect(cell, kRadius,
                      gfx::mix(kWhite.with_alpha(0.06f), Color::rgb(0x1c2e39, 0.97f), focus));
    list.bordered_rect(cell, kRadius, kClear, 1.5f, kWhite.with_alpha(0.12f + 0.14f * focus));
    const Rect art{cell.x + 24.0f, cell.cy() - kArt * 0.5f, kArt, kArt};
    draw_station_art(list, fonts, art, 18.0f, station);

    const float x = art.x + art.w + 24.0f;
    const float room = cell.x + cell.w - 24.0f - x - (favorite ? 30.0f : 0.0f);
    ui::text(list, fonts.semibold, fonts.semibold.font->fit(station.name, 26.0f, room), x,
             cell.cy() - 18.0f, 26.0f, theme.text);
    std::string meta = station.country[0] != '\0' ? station.country : "Worldwide";
    const std::vector<std::string> tags = split_list(station.tags, 1);
    if (!tags.empty())
        meta += std::string(kDot) + tags[0];
    ui::text(list, fonts.regular, fonts.regular.font->fit(meta, 20.0f, room + 30.0f), x,
             cell.cy() + 14.0f, 20.0f, theme.text_muted);

    const bool current = session.is_current(station);
    float row = x;
    if (!rank.empty())
        row += ui::text(list, fonts.mono, rank, row, cell.cy() + 46.0f, 18.0f, tone::teal) + 12.0f;
    if (current)
    {
        // On air, the line says so instead of repeating a number.
        const float baseline = cell.cy() + 46.0f;
        draw_equalizer(list, session, row, baseline + 1.0f, 18.0f, tone::live);
        ui::text(list, fonts.semibold, session.on_air() ? "LIVE" : "TUNING IN", row + 32.0f,
                 baseline, 15.0f, tone::live, gfx::Align::left, 2.0f);
    }
    else if (!detail.empty())
    {
        const float limit = cell.x + cell.w - 24.0f - row;
        ui::text(list, fonts.mono, fonts.mono.font->fit(detail, 18.0f, limit), row,
                 cell.cy() + 46.0f, 18.0f, theme.text_muted);
    }

    if (favorite)
        list.star(cell.x + cell.w - 32.0f, cell.y + 30.0f, 11.0f, tone::gold);
}

void draw_tile_placeholder(ui::Canvas &canvas, const Session &session, const Rect &cell)
{
    gfx::DrawList &list = canvas.list;
    constexpr float kRadius = 20.0f;
    const Color bone = kWhite.with_alpha(0.09f);
    list.rounded_rect(cell, kRadius, kWhite.with_alpha(0.04f));
    list.bordered_rect(cell, kRadius, kClear, 1.5f, kWhite.with_alpha(0.08f));
    list.rounded_rect({cell.x + 24.0f, cell.cy() - 56.0f, 112.0f, 112.0f}, 18.0f, bone);
    const float x = cell.x + 160.0f;
    list.rounded_rect({x, cell.cy() - 38.0f, (cell.w - 190.0f) * 0.78f, 22.0f}, 8.0f, bone);
    list.rounded_rect({x, cell.cy() - 2.0f, (cell.w - 190.0f) * 0.5f, 16.0f}, 6.0f, bone);
    list.rounded_rect({x, cell.cy() + 30.0f, (cell.w - 190.0f) * 0.32f, 14.0f}, 6.0f, bone);
    if (!session.settings.reduced_motion)
    {
        // One band of light crosses every waiting tile at the same pace.
        const float period = 1.7f;
        const float phase =
            (session.clock -
             period * static_cast<float>(static_cast<int>(session.clock / period))) /
            period;
        const float centre = cell.x - cell.w * 0.3f + phase * cell.w * 1.6f;
        ui::draw_sweep(list, centre, cell.w * 0.18f, cell.x + 24.0f, cell.x + cell.w - 24.0f,
                       cell.cy() - 56.0f, 112.0f, kWhite.with_alpha(0.07f));
    }
}

} // namespace radio
