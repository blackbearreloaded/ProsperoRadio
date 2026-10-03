// ProsperoRadio - Now Playing: the station on air, a visualizer and what is next.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/now_playing_screen.hpp"

#include "app/draw.hpp"
#include "app/station_art.hpp"
#include "app/theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace radio
{

namespace
{

constexpr float kWideMargin = 218.0f; // side margin when the list is hidden
constexpr Rect kListPanel{1404.0f, 96.0f, 420.0f, 800.0f};
constexpr float kListRadius = 28.0f;
constexpr float kArt = 520.0f;
constexpr float kArtY = 160.0f;
constexpr float kArtRadius = 40.0f;
constexpr float kRestScale = 0.94f; // artwork that is not on air sits back
constexpr float kColumnGap = 84.0f;
constexpr float kBarsBase = 640.0f;
constexpr float kBarsHeight = 170.0f;
constexpr float kFactsY = 742.0f;
constexpr float kLineY = 812.0f;
constexpr float kTransportY = 924.0f;
constexpr float kMainRadius = 56.0f;
constexpr float kButtonRadius = 34.0f;
constexpr float kControlOffset[] = {-230.0f, -130.0f, 0.0f, 130.0f, 230.0f};
constexpr unsigned kWindow = 40; // stations the list holds around the one on air
constexpr unsigned kLead = 3;    // ... of which this many come before it

constexpr const char *kListName[] = {"Popular", "Trending", "Top rated", "Favorites",
                                     "Search results"};

void format_time(char *out, std::size_t size, float seconds)
{
    const int whole = std::max(0, static_cast<int>(seconds));
    if (whole >= 3600)
        std::snprintf(out, size, "%d:%02d:%02d", whole / 3600, whole / 60 % 60, whole % 60);
    else
        std::snprintf(out, size, "%d:%02d", whole / 60, whole % 60);
}

} // namespace

NowPlayingScreen::NowPlayingScreen(Session &session) : session_(session)
{
    const ui::Theme &theme = session.theme;
    up_next_.style.theme = theme;
    up_next_.style.row_height = 66.0f;
    up_next_.style.gap = 2.0f;
    up_next_.style.padding = 16.0f;
    up_next_.style.title_size = 24.0f;
    up_next_.style.leading_width = 52.0f;
    up_next_.style.highlight.kind = ui::HighlightKind::tint;
    up_next_.style.focus_shift = 0.0f;
    up_next_.set_bounds({kListPanel.x + 14.0f, kListPanel.y + 98.0f, kListPanel.w - 28.0f,
                         kListPanel.h - 98.0f - 16.0f});
    up_next_.leading =
        [this](ui::Canvas &canvas, const Rect &box, const ui::ListItem &item, int, float)
    {
        const unsigned at = static_cast<unsigned>(item.tag);
        if (session_.has_context() && at == session_.play_index() && session_.active())
        {
            draw_equalizer(canvas.list, session_, box.x + 8.0f, box.cy() + 11.0f, 22.0f,
                           gfx::mix(accent_.value(), kWhite, 0.3f));
            return;
        }
        char number[12];
        std::snprintf(number, sizeof(number), "%u", at + 1U);
        ui::text(canvas.list, canvas.fonts.mono, canvas.fonts.mono.font->fit(number, 18.0f, box.w),
                 box.cx(), baseline_for(box.cy(), 18.0f), 18.0f, session_.theme.text_muted,
                 gfx::Align::center);
    };
    up_next_.trailing =
        [this](ui::Canvas &canvas, const Rect &row, const ui::ListItem &item, int, float)
    {
        const unsigned at = static_cast<unsigned>(item.tag);
        const float right = row.x + row.w - 18.0f;
        ui::text(canvas.list, canvas.fonts.mono, item.value, right, baseline_for(row.cy(), 18.0f),
                 18.0f, session_.theme.text_muted, gfx::Align::right);
        if (session_.play_list().favorite(at))
            canvas.list.star(right - 46.0f, row.cy(), 8.0f, tone::gold);
    };

    spinner_.style.theme = theme;
    spinner_.style.kind = ui::SpinnerKind::arc;
    spinner_.style.track = false;
    spinner_.style.color = kWhite;
    spinner_.set_spinning(false, true);

    list_amount_.snap(1.0f);
    live_.snap(0.0f);
    ring_.snap(ring_target());
    accent_.snap(tone::teal);
    body_.snap(Color::rgb(0x0b3140));
}

void NowPlayingScreen::enter()
{
    age_ = 0.0f;
    zone_ = Zone::transport;
    button_ = kMain;
    ring_.snap(ring_target());
    up_next_.enter();
    if (has_shown_)
        is_favorite_ = radio_service_is_favorite(shown_.uuid);
}

NowPlayingScreen::Layout NowPlayingScreen::layout() const
{
    const float q = list_amount_.value;
    Layout l;
    l.x = tween::lerp(kWideMargin, kMargin, q);
    l.w = tween::lerp(gfx::kVirtualWidth - 2.0f * kWideMargin, kListPanel.x - 64.0f - kMargin, q);
    l.cx = l.x + l.w * 0.5f;
    l.art = {l.x, kArtY, kArt, kArt};
    l.column_x = l.x + kArt + kColumnGap;
    l.column_w = l.w - kArt - kColumnGap;
    return l;
}

float NowPlayingScreen::appear(int order) const
{
    return tween::stagger(age_, order, 0.07f, 0.55f);
}

float NowPlayingScreen::slide(float in, float distance) const
{
    return session_.settings.reduced_motion ? 0.0f : distance * (1.0f - in);
}

// The focus ring's target as a circle: x is the centre relative to the row's
// centre (so the ring rides along when the layout re-centres), w the diameter.
Rect NowPlayingScreen::ring_target() const
{
    const float radius = (button_ == kMain ? kMainRadius : kButtonRadius) + 9.0f;
    return {kControlOffset[button_], kTransportY, radius * 2.0f, radius * 2.0f};
}

const char *NowPlayingScreen::state_word() const
{
    switch (session_.status.playback_state)
    {
    case RADIO_PLAYBACK_CONNECTING:
        return "TUNING IN";
    case RADIO_PLAYBACK_BUFFERING:
        return "BUFFERING";
    case RADIO_PLAYBACK_PLAYING:
        return "NOW PLAYING";
    case RADIO_PLAYBACK_STOPPING:
        return "STOPPING";
    case RADIO_PLAYBACK_ERROR:
        return "NO SIGNAL";
    default:
        return "STOPPED";
    }
}

void NowPlayingScreen::refuse(ui::Feedback &feedback, float direction)
{
    feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
    feedback.rumble(0.25f, 0.05f);
    nudge_direction_ = direction;
    if (!session_.settings.reduced_motion)
        nudge_.trigger();
}

void NowPlayingScreen::turn(int direction, ui::Feedback &feedback)
{
    if (!session_.can_zap(direction))
    {
        refuse(feedback, static_cast<float>(direction));
        return;
    }
    press_[direction < 0 ? kPrevious : kNext].trigger();
    feedback.play(audio::Cue::tab, direction > 0 ? 1.04f : 0.96f,
                  ui::pan_for_x(layout().cx + kControlOffset[direction < 0 ? kPrevious : kNext]));
    session_.zap(direction);
}

void NowPlayingScreen::favorite(ui::Feedback &feedback)
{
    if (!session_.has_context())
    {
        refuse(feedback, 0.0f);
        return;
    }
    is_favorite_ = session_.toggle_favorite(session_.play_list(), session_.play_index(), feedback);
    press_[kFavorite].trigger();
    star_.trigger();
}

void NowPlayingScreen::activate(int control, ui::Feedback &feedback)
{
    switch (control)
    {
    case kFavorite:
        favorite(feedback);
        break;
    case kPrevious:
        turn(-1, feedback);
        break;
    case kNext:
        turn(1, feedback);
        break;
    case kMain:
        press_[kMain].trigger();
        if (session_.active())
        {
            feedback.play(audio::Cue::back);
            session_.stop();
        }
        else if (session_.has_context())
        {
            feedback.play(audio::Cue::select);
            feedback.rumble(0.3f, 0.04f);
            session_.resume();
        }
        else
        {
            refuse(feedback, 0.0f);
        }
        break;
    default:
        press_[kList].trigger();
        list_open_ = !list_open_;
        feedback.play(list_open_ ? audio::Cue::open : audio::Cue::modal_close);
        break;
    }
}

NowPlayingScreen::Result NowPlayingScreen::handle(const InputFrame &input, ui::Feedback &feedback)
{
    if (input.is_pressed(Action::back))
    {
        feedback.play(audio::Cue::back);
        if (zone_ == Zone::list)
        {
            zone_ = Zone::transport;
            return Result::none;
        }
        return Result::close;
    }
    if (input.is_pressed(Action::west))
        favorite(feedback);
    if (input.is_pressed(Action::jump_next))
        turn(1, feedback);
    else if (input.is_pressed(Action::jump_prev))
        turn(-1, feedback);

    if (zone_ == Zone::list)
    {
        if (input.nav == Direction::left)
        {
            zone_ = Zone::transport;
            feedback.play(audio::Cue::focus, 1.0f, 0.2f);
            return Result::none;
        }
        if (up_next_.handle(input, feedback) == ui::Event::activated && !up_next_.items().empty())
        {
            const unsigned at = static_cast<unsigned>(
                up_next_.items()[static_cast<std::size_t>(up_next_.focus())].tag);
            if (at != session_.play_index() || !session_.active())
                session_.play(session_.play_list(), at);
        }
        return Result::none;
    }

    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        const int step = input.nav == Direction::right ? 1 : -1;
        const int next = button_ + step;
        if (next >= kControls && list_open_ && !up_next_.items().empty())
        {
            zone_ = Zone::list;
            feedback.play(audio::Cue::focus, 1.0f, 0.45f);
        }
        else if (next < 0 || next >= kControls)
        {
            if (!input.nav_repeat)
                refuse(feedback, static_cast<float>(step));
        }
        else
        {
            button_ = next;
            feedback.play(audio::Cue::focus, 1.0f,
                          ui::pan_for_x(layout().cx + kControlOffset[button_]));
        }
    }
    if (input.is_pressed(Action::confirm))
        activate(button_, feedback);
    return Result::none;
}

void NowPlayingScreen::rebuild_list()
{
    Catalog &play = session_.play_list();
    if (!session_.has_context())
    {
        if (window_signature_ != 0U)
        {
            up_next_.set_items({});
            window_signature_ = 0U;
        }
        return;
    }
    const unsigned index = session_.play_index();
    const unsigned base = index >= kLead ? index - kLead : 0U;
    play.prefetch(base, base + kWindow - 1U);
    const unsigned total = play.known() ? play.total() : index + 1U;
    const unsigned count = std::min(kWindow, total > base ? total - base : 0U);
    unsigned loaded = 0;
    for (unsigned i = 0; i < count; ++i)
        loaded += play.peek(base + i) != nullptr ? 1U : 0U;
    const unsigned signature = 1U + index * 31U + total * 7U + loaded * 131U +
                               session_.revision * 977U + static_cast<unsigned>(play.spec().view);
    if (signature == window_signature_)
        return;

    // The focus keeps to its station while the window moves under it.
    const bool had_focus = zone_ == Zone::list && !up_next_.items().empty();
    const unsigned held =
        had_focus ? static_cast<unsigned>(
                        up_next_.items()[static_cast<std::size_t>(up_next_.focus())].tag)
                  : index;
    std::vector<ui::ListItem> items;
    items.reserve(count);
    for (unsigned i = 0; i < count; ++i)
    {
        const radio_station_t *station = play.peek(base + i);
        ui::ListItem item;
        item.title = station != nullptr ? station->name : "...";
        item.value = station != nullptr ? ui::upper(station->country_code) : "";
        item.tag = static_cast<int>(base + i);
        items.push_back(std::move(item));
    }
    up_next_.set_items(std::move(items));
    window_base_ = base;
    window_signature_ = signature;
    if (count > 0U)
        up_next_.set_focus(static_cast<int>(std::clamp(held, base, base + count - 1U) - base),
                           true);
}

void NowPlayingScreen::update(float dt)
{
    age_ += dt;
    const bool reduced = session_.settings.reduced_motion;

    // ---- a change of station cross-fades ----
    if (session_.has_playing &&
        (!has_shown_ || std::strcmp(shown_.uuid, session_.playing.uuid) != 0))
    {
        if (has_shown_)
        {
            previous_ = shown_;
            has_previous_ = true;
            travel_ = session_.play_index() >= shown_index_ ? 1.0f : -1.0f;
            swap_.start(0.5f);
        }
        shown_ = session_.playing;
        shown_index_ = session_.play_index();
        is_favorite_ = radio_service_is_favorite(shown_.uuid);
        const ArtColors colors = art_colors(shown_.uuid);
        accent_.target(colors.accent);
        body_.target(colors.bottom);
        if (!has_shown_)
        {
            accent_.snap(colors.accent);
            body_.snap(colors.bottom);
        }
        has_shown_ = true;
    }
    swap_.update(dt);

    live_.target = session_.on_air() ? 1.0f : 0.0f;
    live_.update(dt, reduced ? 40.0f : 7.0f);
    glow_.target = session_.energy;
    glow_.update(dt, 9.0f);
    list_amount_.target = list_open_ ? 1.0f : 0.0f;
    list_amount_.update(dt, reduced ? 40.0f : 11.0f);
    if (!list_open_ && zone_ == Zone::list)
        zone_ = Zone::transport;
    list_focus_.target = zone_ == Zone::list ? 1.0f : 0.0f;
    list_focus_.update(dt, 16.0f);
    ring_.target(ring_target());
    ring_.update(dt, 18.0f);
    accent_.update(dt, 4.0f);
    body_.update(dt, 4.0f);
    for (ui::Pulse &press : press_)
        press.update(dt, 10.0f);
    nudge_.update(dt, 9.0f);
    star_.update(dt, 5.0f);

    const bool waiting = session_.status.playback_state == RADIO_PLAYBACK_CONNECTING ||
                         session_.status.playback_state == RADIO_PLAYBACK_BUFFERING;
    const Layout l = layout();
    const float reach = kMainRadius + 14.0f;
    spinner_.style.reduced_motion = reduced;
    spinner_.set_bounds({l.cx - reach, kTransportY - reach, reach * 2.0f, reach * 2.0f});
    spinner_.set_spinning(waiting);
    spinner_.update(dt);

    rebuild_list();
    up_next_.style.reduced_motion = reduced;
    up_next_.set_active(zone_ == Zone::list);
    // Left alone, the list rests on the station that is playing.
    if (zone_ != Zone::list && !up_next_.items().empty() && session_.play_index() >= window_base_)
        up_next_.set_focus(static_cast<int>(session_.play_index() - window_base_), false);
    up_next_.update(dt);
}

void NowPlayingScreen::draw_header(gfx::DrawList &list, const Layout &l) const
{
    const ui::Fonts &fonts = session_.fonts;
    const float in = appear(0);
    list.push_opacity(in);
    const float y = 104.0f - slide(in, 12.0f);
    const float w = ui::text(list, fonts.display, "Now Playing", l.x, y, 34.0f, kWhite);
    if (session_.has_context())
    {
        char from[64];
        std::snprintf(from, sizeof(from), "from %s",
                      kListName[static_cast<int>(session_.play_list().spec().view)]);
        ui::text(list, fonts.regular, from, l.x + w + 24.0f, y, 22.0f, kWhite.with_alpha(0.6f));
    }
    list.pop_opacity();
}

void NowPlayingScreen::draw_artwork(gfx::DrawList &list, const ui::Fonts &fonts,
                                    const Layout &l) const
{
    const bool reduced = session_.settings.reduced_motion;
    const float in = appear(1);
    const float live = live_.value;
    const float pulse = glow_.value;
    Rect art = l.art;
    art.x -= slide(in, 36.0f);

    // Off air it rests at 94 %; on air it is full size and breathes with the bass.
    const float scale = tween::lerp(kRestScale, 1.0f, live) + (reduced ? 0.0f : 0.008f * pulse);
    list.push_opacity(in);
    list.push_transform(scale, art.cx(), art.cy(), 0.0f, 0.0f);
    const float lit = (0.14f + 0.36f * pulse) * (0.3f + 0.7f * live);
    list.glow(art.inset(-4.0f), kArtRadius + 4.0f, 90.0f + 60.0f * pulse,
              gfx::mix(accent_.value(), body_.value(), 0.25f).with_alpha(lit));
    const auto cover = [&](const radio_station_t &station, float alpha, float dx)
    {
        if (alpha <= 0.01f)
            return;
        const Rect r{art.x + dx, art.y, art.w, art.h};
        list.push_opacity(alpha);
        list.shadow({r.x, r.y + 28.0f, r.w, r.h}, kArtRadius, 50.0f, Color::rgb(0x000000, 0.5f));
        draw_station_art(list, fonts, r, kArtRadius, station);
        // Artwork that is not on air also loses a little light.
        list.rounded_rect(r, kArtRadius, Color::rgb(0x05080c, 0.3f * (1.0f - live)));
        list.pop_opacity();
    };
    if (swap_.running && has_previous_)
    {
        const float t = swap_.progress();
        const float leave = tween::clamp01(t * 2.3f);
        const float arrive = tween::clamp01((t - 0.2f) / 0.8f);
        const float distance = reduced ? 0.0f : 150.0f;
        cover(previous_, 1.0f - tween::smoothstep(leave),
              -travel_ * distance * tween::cubic_in(leave));
        cover(shown_, tween::smoothstep(arrive * 1.6f),
              travel_ * distance * (1.0f - tween::quint_out(arrive)));
    }
    else
    {
        cover(shown_, 1.0f, 0.0f);
    }
    list.pop_transform();
    list.pop_opacity();
}

void NowPlayingScreen::draw_station_text(ui::Canvas &canvas, const Layout &l,
                                         const radio_station_t &station, float alpha,
                                         float dx) const
{
    if (alpha <= 0.01f)
        return;
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    ui::Painter paint(list, fonts, session_.theme, canvas.glass);
    const float x = l.column_x + dx;
    list.push_opacity(alpha);
    // A name that fits takes one large line. A long one takes two smaller
    // lines, and what hangs under it moves down to make room.
    float below = 0.0f;
    if (fonts.display.measure(station.name, 84.0f) <= l.column_w)
    {
        ui::text(list, fonts.display, station.name, x - 3.0f, 300.0f, 84.0f, kWhite);
    }
    else
    {
        const std::vector<std::string> lines =
            fonts.display.font->wrap(station.name, 56.0f, l.column_w);
        const std::size_t shown = std::min<std::size_t>(lines.size(), 2);
        for (std::size_t i = 0; i < shown; ++i)
        {
            // A third line is cut: the second one says so.
            const std::string line =
                i == 1 && lines.size() > 2 ? lines[i] + " \xE2\x80\xA6" : lines[i];
            ui::text(list, fonts.display, fonts.display.font->fit(line, 56.0f, l.column_w),
                     x - 2.0f, 276.0f + 62.0f * static_cast<float>(i), 56.0f, kWhite);
        }
        below = shown > 1 ? 36.0f : 0.0f;
    }
    ui::text(list, fonts.regular, fonts.regular.font->fit(place_line(station), 26.0f, l.column_w),
             x, 352.0f + below, 26.0f, kWhite.with_alpha(0.64f));
    float at = x;
    for (const std::string &tag : split_list(station.tags, 3))
    {
        const std::string label = fonts.semibold.font->fit(tag, 19.0f, 200.0f);
        if (at + chip_width(paint, label) > x + l.column_w)
            break;
        at += draw_chip(paint, at, 406.0f + below, label) + 12.0f;
    }
    list.pop_opacity();
}

void NowPlayingScreen::draw_info(ui::Canvas &canvas, const Layout &l) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const ui::Theme &theme = session_.theme;
    const float in = appear(2);
    const float enter = slide(in, 30.0f);
    list.push_opacity(in);

    // ---- the state line ----
    const float x = l.column_x + enter;
    const float cy = 199.0f;
    float word_x = x;
    Color word_color = kWhite.with_alpha(0.7f);
    switch (session_.status.playback_state)
    {
    case RADIO_PLAYBACK_PLAYING:
    {
        const Rect chip{x, cy - 18.0f, 98.0f, 36.0f};
        list.bordered_rect(chip, 18.0f, tone::live.with_alpha(0.1f), 1.5f,
                           tone::live.with_alpha(0.6f));
        list.circle(chip.x + 20.0f, cy, 5.0f, tone::live);
        ui::text(list, fonts.semibold, "LIVE", chip.x + 34.0f, baseline_for(cy, 16.0f), 16.0f,
                 tone::live, gfx::Align::left, 2.0f);
        word_x = x + chip.w + 18.0f;
        word_color = gfx::mix(accent_.value(), kWhite, 0.3f);
        break;
    }
    case RADIO_PLAYBACK_CONNECTING:
    case RADIO_PLAYBACK_BUFFERING:
        word_color = theme.warning;
        break;
    case RADIO_PLAYBACK_ERROR:
        word_color = theme.danger;
        break;
    default:
        break;
    }
    ui::text(list, fonts.semibold, state_word(), word_x, baseline_for(cy, 18.0f), 18.0f, word_color,
             gfx::Align::left, 4.0f);
    if (session_.has_context() && session_.play_list().known())
    {
        char place[48];
        std::snprintf(place, sizeof(place), "%s / %s",
                      group_digits(session_.play_index() + 1U).c_str(),
                      group_digits(session_.play_list().total()).c_str());
        ui::text(list, fonts.mono, place, l.column_x + l.column_w + enter, baseline_for(cy, 20.0f),
                 20.0f, kWhite.with_alpha(0.6f), gfx::Align::right);
    }

    if (swap_.running && has_previous_)
    {
        const float t = swap_.progress();
        const float distance = session_.settings.reduced_motion ? 0.0f : 1.0f;
        const float leave = tween::clamp01(t * 3.2f);
        const float arrive = tween::clamp01((t - 0.26f) / 0.74f);
        draw_station_text(canvas, l, previous_, 1.0f - tween::smoothstep(leave),
                          -travel_ * 40.0f * distance * tween::cubic_in(leave));
        draw_station_text(canvas, l, shown_, tween::smoothstep(arrive),
                          travel_ * 56.0f * distance * (1.0f - tween::quint_out(arrive)));
    }
    else
    {
        draw_station_text(canvas, l, shown_, 1.0f, enter);
    }
    list.pop_opacity();
}

void NowPlayingScreen::draw_visualizer(gfx::DrawList &list, const Layout &l) const
{
    constexpr int kBars = Session::kBands;
    const float in = appear(3);
    const Color top = gfx::mix(accent_.value(), kWhite, 0.4f);
    const Color bottom = gfx::mix(body_.value(), accent_.value(), 0.45f);
    const float width = l.column_w / static_cast<float>(kBars) * 0.56f;
    const float pitch = (l.column_w - width) / static_cast<float>(kBars - 1);
    list.push_opacity(in);
    for (int i = 0; i < kBars; ++i)
    {
        const float grow = tween::stagger(age_, i, 0.012f, 0.5f);
        const float level = session_.levels[static_cast<std::size_t>(i)] * grow;
        const float h = width + level * (kBarsHeight - width);
        const float x = l.column_x + static_cast<float>(i) * pitch;
        // A tall bar is also a brighter one.
        const Color tip = gfx::mix(bottom, top, 0.35f + 0.65f * level);
        list.gradient_rect({x, kBarsBase - h, width, h}, width * 0.5f, tip, bottom);
        // The floor is a mirror: a short, fading copy below the line.
        const float mirror = width + (h - width) * 0.42f;
        list.gradient_rect({x, kBarsBase + 8.0f, width, mirror}, width * 0.5f,
                           bottom.with_alpha(0.3f), bottom.with_alpha(0.0f));
        const float peak = session_.peaks[static_cast<std::size_t>(i)] * grow;
        const float lifted = peak - level;
        if (lifted > 0.02f)
        {
            const float y = kBarsBase - width - peak * (kBarsHeight - width) - 8.0f;
            list.rounded_rect({x, y, width, 3.0f}, 1.5f,
                              top.with_alpha(tween::clamp01(lifted * 8.0f) * 0.55f * live_.value));
        }
    }
    list.pop_opacity();
}

void NowPlayingScreen::draw_on_air(ui::Canvas &canvas, const Layout &l) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    ui::Painter paint(list, fonts, session_.theme, canvas.glass);
    const float in = appear(4);
    const float live = live_.value;
    const Color accent = gfx::mix(accent_.value(), kWhite, 0.25f);
    list.push_opacity(in);

    // ---- what the stream is ----
    float at = l.x;
    const float facts = kFactsY + slide(in, 20.0f);
    at += draw_chip(paint, at, facts, shown_.codec[0] != '\0' ? ui::upper(shown_.codec) : "Stream",
                    1.0f) +
          12.0f;
    char text[48];
    if (shown_.bitrate != 0U)
    {
        std::snprintf(text, sizeof(text), "%u kbps", shown_.bitrate);
        at += draw_chip(paint, at, facts, text) + 12.0f;
    }
    if (session_.active() && session_.status.sample_rate != 0U)
    {
        std::snprintf(text, sizeof(text), "%.4g kHz",
                      static_cast<double>(session_.status.sample_rate) / 1000.0);
        at += draw_chip(paint, at, facts, text) + 12.0f;
        at +=
            draw_chip(paint, at, facts, session_.status.channels == 1U ? "Mono" : "Stereo") + 12.0f;
    }
    if (shown_.hls != 0U)
        draw_chip(paint, at, facts, "HLS");

    // ---- the line: a live stream has no position, only time on air ----
    const float y = kLineY + slide(in, 24.0f);
    const Rect bar{l.x, y - 3.0f, l.w, 6.0f};
    list.rounded_rect(bar, 3.0f, kWhite.with_alpha(0.14f));
    if (live > 0.01f)
    {
        list.gradient_rect_h(bar, 3.0f, accent.with_alpha(0.12f * live), accent.with_alpha(live));
        const float end = bar.x + bar.w;
        list.glow({end - 10.0f, y - 10.0f, 20.0f, 20.0f}, 10.0f, 14.0f + 10.0f * glow_.value,
                  accent.with_alpha(0.5f * live));
        list.circle(end, y, 10.0f, kWhite.with_alpha(live));
    }
    if (session_.on_air())
    {
        char clock[24];
        format_time(clock, sizeof(clock), session_.listening);
        std::snprintf(text, sizeof(text), "%s on air", clock);
    }
    else
    {
        std::snprintf(text, sizeof(text), "%s",
                      session_.status.playback_state == RADIO_PLAYBACK_ERROR
                          ? "The station did not answer"
                      : session_.active() ? "Waiting for the stream"
                                          : "Not playing");
    }
    ui::text(list, fonts.mono, text, l.x, y + 40.0f, 22.0f, kWhite.with_alpha(0.86f));
    const char *end = session_.on_air() ? "LIVE" : (session_.active() ? "TUNING" : "OFF AIR");
    ui::text(list, fonts.mono, end, l.x + l.w, y + 40.0f, 22.0f,
             session_.on_air() ? accent : kWhite.with_alpha(0.5f), gfx::Align::right);
    list.pop_opacity();
}

void NowPlayingScreen::draw_transport(ui::Canvas &canvas, const Layout &l) const
{
    gfx::DrawList &list = canvas.list;
    const float in = appear(5);
    const Color accent = gfx::mix(accent_.value(), kWhite, 0.3f);
    list.push_opacity(in);
    const float y = kTransportY + slide(in, 28.0f);

    for (int i = 0; i < kControls; ++i)
    {
        const float cx = l.cx + kControlOffset[i];
        const bool focused = zone_ == Zone::transport && i == button_;
        list.push_transform(1.0f - 0.08f * press_[static_cast<std::size_t>(i)].value, cx, y, 0.0f,
                            0.0f);
        if (i == kMain)
        {
            // The primary button: a white disc whose halo keeps the beat.
            list.glow({cx - kMainRadius, y - kMainRadius, kMainRadius * 2.0f, kMainRadius * 2.0f},
                      kMainRadius, 22.0f + 14.0f * glow_.value,
                      accent.with_alpha(0.2f + 0.3f * glow_.value * live_.value));
            list.shadow(
                {cx - kMainRadius, y - kMainRadius + 8.0f, kMainRadius * 2.0f, kMainRadius * 2.0f},
                kMainRadius, 18.0f, Color::rgb(0x000000, 0.4f));
            list.circle(cx, y, kMainRadius, kWhite);
            if (session_.active())
                draw_stop_icon(list, cx, y, 38.0f, tone::ink);
            else
                draw_play_icon(list, cx, y, 40.0f, tone::ink);
        }
        else
        {
            const bool lit = i == kList && list_open_;
            const bool dead =
                (i == kPrevious && !session_.can_zap(-1)) || (i == kNext && !session_.can_zap(1));
            list.circle(cx, y, kButtonRadius, kWhite.with_alpha(focused ? 0.18f : 0.08f));
            list.ring(cx, y, kButtonRadius, 1.5f, kWhite.with_alpha(0.16f));
            // Opaque on purpose: an icon is several strokes that overlap.
            const Color ink = dead  ? Color::rgb(0x6c7480)
                              : lit ? accent
                                    : (focused ? kWhite : Color::rgb(0xd4d7e2));
            if (i == kFavorite)
                list.star(cx, y - 1.0f, 15.0f + 8.0f * star_.value, is_favorite_ ? tone::gold : ink,
                          is_favorite_ ? 0.0f : 2.5f);
            else if (i == kList)
                draw_list_icon(list, cx, y, 22.0f, ink);
            else
                draw_skip_icon(list, cx, y, 24.0f, i == kNext ? 1.0f : -1.0f, ink);
            if (lit)
                list.circle(cx, y + kButtonRadius + 14.0f, 4.0f, accent);
        }
        list.pop_transform();
    }
    list.pop_opacity();
}

// One ring for the transport row: its light goes under the buttons and its
// line over them, so the glow never tints the white disc.
void NowPlayingScreen::draw_focus_ring(gfx::DrawList &list, const Layout &l, bool light) const
{
    const float alpha = appear(5) * (1.0f - list_focus_.value);
    if (alpha <= 0.01f)
        return;
    const Rect ring = ring_.value();
    const float cx =
        l.cx + ring.x + ui::shake(nudge_.value, session_.clock, 12.0f) * nudge_direction_;
    const float cy =
        ring.y + slide(appear(5), 28.0f) +
        (nudge_direction_ == 0.0f ? ui::shake(nudge_.value, session_.clock, 8.0f) : 0.0f);
    const float radius = ring.w * 0.5f;
    list.push_opacity(alpha);
    if (light)
    {
        const Color accent = gfx::mix(accent_.value(), kWhite, 0.3f);
        list.glow({cx - radius, cy - radius, radius * 2.0f, radius * 2.0f}, radius, 18.0f,
                  accent.with_alpha(0.34f + 0.2f * ui::breathe(session_.clock)));
    }
    else
    {
        list.ring(cx, cy, radius, 3.5f, kWhite);
    }
    list.pop_opacity();
}

void NowPlayingScreen::draw_up_next(ui::Canvas &canvas) const
{
    const float amount = list_amount_.value;
    if (amount <= 0.01f)
        return;
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const float in = appear(6);
    const float open = amount * (session_.settings.reduced_motion ? 1.0f : in);
    const Color accent = gfx::mix(accent_.value(), kWhite, 0.3f);
    // The panel slides in from beyond the right edge, the list with it.
    const float dx = (1.0f - open) * (kListPanel.w + kMargin + 60.0f);
    list.push_opacity(tween::clamp01(amount * 1.5f) * in);
    list.push_transform(1.0f, 0.0f, 0.0f, dx, 0.0f);
    draw_glass(canvas, session_.theme, kListPanel, kListRadius);
    ui::text(list, fonts.semibold, "UP NEXT", kListPanel.x + 32.0f, kListPanel.y + 56.0f, 18.0f,
             accent, gfx::Align::left, 4.0f);
    if (session_.has_context())
        ui::text(list, fonts.regular, kListName[static_cast<int>(session_.play_list().spec().view)],
                 kListPanel.x + kListPanel.w - 32.0f, kListPanel.y + 56.0f, 20.0f,
                 kWhite.with_alpha(0.6f), gfx::Align::right);
    list.rounded_rect({kListPanel.x + 32.0f, kListPanel.y + 84.0f, kListPanel.w - 64.0f, 1.5f},
                      0.75f, kWhite.with_alpha(0.12f));
    if (up_next_.items().empty())
    {
        ui::paragraph(list, fonts.regular,
                      "Start a station from a list to see its neighbours here.",
                      kListPanel.x + 32.0f, kListPanel.y + 140.0f, 22.0f, kListPanel.w - 64.0f,
                      32.0f, kWhite.with_alpha(0.6f), 3);
    }
    else
    {
        up_next_.draw(canvas);
    }
    list.pop_transform();
    list.pop_opacity();
}

void NowPlayingScreen::draw(ui::Canvas &canvas) const
{
    if (!has_shown_)
        return;
    gfx::DrawList &list = canvas.list;
    const Layout l = layout();
    draw_header(list, l);
    draw_artwork(list, canvas.fonts, l);
    draw_info(canvas, l);
    draw_visualizer(list, l);
    draw_on_air(canvas, l);
    draw_focus_ring(list, l, true);
    draw_transport(canvas, l);
    spinner_.draw(canvas);
    draw_focus_ring(list, l, false);
    draw_up_next(canvas);
}

int NowPlayingScreen::hints(ui::Hint *out, int capacity) const
{
    constexpr const char *kLabels[kControls] = {"Favorite", "Previous station", "Stop",
                                                "Next station", "Hide list"};
    int count = 0;
    const auto add = [&](ui::Hint hint)
    {
        if (count < capacity)
            out[count++] = hint;
    };
    if (zone_ == Zone::list)
    {
        add({ui::Button::cross, "Play station"});
        add({ui::Button::circle, "Back"});
        return count;
    }
    const char *label = kLabels[button_];
    if (button_ == kMain && !session_.active())
        label = "Play";
    else if (button_ == kList && !list_open_)
        label = "Show list";
    else if (button_ == kFavorite && is_favorite_)
        label = "Unfavorite";
    add({ui::Button::cross, label});
    add({ui::Button::l2, "Previous / next", ui::Button::r2});
    add({ui::Button::circle, "Back"});
    return count;
}

} // namespace radio
