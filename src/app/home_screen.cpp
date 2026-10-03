// ProsperoRadio - Home: the station in focus as a hero, the list as a grid.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/home_screen.hpp"

#include "app/draw.hpp"
#include "app/station_art.hpp"
#include "app/theme.hpp"

#include <algorithm>
#include <cstdio>

namespace radio
{

namespace
{

constexpr int kColumns = 4;
constexpr int kRowsInView = 2;
constexpr float kTileHeight = 160.0f;
constexpr float kGridTop = 520.0f;
constexpr float kGridPad = 20.0f; // room for the focused tile to grow and wear its ring
// The letter rail takes the right edge; everything else ends before it.
constexpr float kRailWidth = 44.0f;
constexpr float kRight = gfx::kVirtualWidth - kMargin - kRailWidth - 32.0f;
constexpr Rect kHeroArt{kRight - 332.0f, 132.0f, 332.0f, 332.0f};
constexpr float kHeroRadius = 36.0f;
constexpr float kHeroText = 1240.0f; // the width the hero's words may take

// The hero while a station plays.
constexpr Rect kPlayerArt{kMargin, 118.0f, 330.0f, 330.0f};
constexpr float kPlayerX = kMargin + 330.0f + 56.0f; // where its words start
constexpr float kPlayerRight = kRight;
constexpr float kBarsBase = 362.0f;
constexpr float kBarsHeight = 72.0f;
constexpr float kTransportY = 420.0f;
constexpr float kButtonRadius = 30.0f;
constexpr float kMainRadius = 38.0f;
constexpr int kPlayerButtons = 5;
enum PlayerButton : int
{
    kPrevious,
    kMain,
    kNext,
    kStar,
    kFull,
};

void mm_ss(char *out, std::size_t size, float seconds)
{
    const int total = static_cast<int>(seconds);
    if (total >= 3600)
        std::snprintf(out, size, "%d:%02d:%02d", total / 3600, total / 60 % 60, total % 60);
    else
        std::snprintf(out, size, "%d:%02d", total / 60, total % 60);
}

constexpr const char *kListTitle[] = {"Popular stations", "Trending now", "Top rated",
                                      "Your favorites", "Results"};

} // namespace

HomeScreen::HomeScreen(Session &session) : session_(session), rail_(session.theme)
{
    rail_.set_bounds({gfx::kVirtualWidth - kMargin - kRailWidth, 132.0f, kRailWidth, 772.0f});
    grid_.style.theme = session.theme;
    grid_.style.columns = kColumns;
    grid_.style.cell_height = kTileHeight;
    grid_.style.gap_x = 24.0f;
    grid_.style.gap_y = 20.0f;
    grid_.style.padding = kGridPad;
    grid_.style.card.radius = 20.0f;
    grid_.style.card.focus_scale = 1.03f;
    grid_.style.card.lift = 4.0f;
    grid_.style.card.glow = true;
    grid_.set_bounds({kMargin - kGridPad, kGridTop, kRight - kMargin + 2.0f * kGridPad,
                      kRowsInView * (kTileHeight + 20.0f) + 2.0f * kGridPad + 8.0f});
    grid_.content =
        [this](ui::Canvas &canvas, const Rect &cell, const ui::CardItem &, int index, float focus)
    {
        const Catalog &list = session_.browse;
        const unsigned at = static_cast<unsigned>(index);
        const radio_station_t *station = list.peek(at);
        if (station == nullptr)
        {
            draw_tile_placeholder(canvas, session_, cell);
            return;
        }
        char rank[16] = "";
        std::string detail;
        switch (list.spec().view)
        {
        case View::popular:
            std::snprintf(rank, sizeof(rank), "#%u", at + 1U);
            detail = group_digits(station->click_count) + " plays";
            break;
        case View::trending:
        {
            std::snprintf(rank, sizeof(rank), "#%u", at + 1U);
            char trend[24];
            std::snprintf(trend, sizeof(trend), "%+d today", station->click_trend);
            detail = trend;
            break;
        }
        case View::voted:
            std::snprintf(rank, sizeof(rank), "#%u", at + 1U);
            detail = group_digits(station->votes) + " votes";
            break;
        default:
            detail = codec_short(*station);
            break;
        }
        draw_station_tile(canvas, session_, cell, *station, rank, detail, list.favorite(at), focus);
    };
    grid_.accent = [this](int index)
    {
        const radio_station_t *station = session_.browse.peek(static_cast<unsigned>(index));
        return station != nullptr ? art_colors(station->uuid).accent : kClear;
    };

    empty_.style.theme = session.theme;
    empty_.style.title_size = 44.0f;
    empty_.style.body_size = 26.0f;
    empty_.style.hint_size = 26.0f;
    empty_.style.max_text_width = 760.0f;
    empty_.set_bounds({460.0f, 250.0f, 1000.0f, 520.0f});
    empty_.icon = [](ui::Canvas &canvas, const Rect &area)
    { canvas.list.star(area.cx(), area.cy(), area.w * 0.46f, tone::teal, 3.5f); };
}

void HomeScreen::enter()
{
    age_ = 0.0f;
    grid_.enter();
    empty_.enter();
}

void HomeScreen::reset()
{
    grid_.set_focus(0, true);
    has_previous_ = false;
    swap_.running = false;
    enter();
}

void HomeScreen::show_player()
{
    if (session_.has_playing)
        player_mode_ = true;
}

bool HomeScreen::player_shown() const
{
    return player_mode_ && session_.has_playing && grid_.count() > 0;
}

Rect HomeScreen::player_button(int index) const
{
    float x = kPlayerX + kButtonRadius;
    for (int i = 0; i < index; ++i)
    {
        const float here = i == kMain ? kMainRadius : kButtonRadius;
        const float next = i + 1 == kMain ? kMainRadius : kButtonRadius;
        x += here + 24.0f + next;
    }
    const float r = index == kMain ? kMainRadius : kButtonRadius;
    return {x - r, kTransportY - r, 2.0f * r, 2.0f * r};
}

HomeScreen::Result HomeScreen::handle_player(const InputFrame &input, ui::Feedback &feedback)
{
    if (input.is_pressed(Action::back) || input.nav == Direction::down)
    {
        player_focus_ = false;
        grid_.set_active(true);
        feedback.play(audio::Cue::focus, 0.96f);
        return Result::none;
    }
    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        const int step = input.nav == Direction::left ? -1 : 1;
        const int next = button_ + step;
        if (next < 0 || next >= kPlayerButtons)
        {
            if (!input.nav_repeat)
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
        }
        else
        {
            button_ = next;
            feedback.play(audio::Cue::focus, 1.0f, 0.4f * static_cast<float>(step));
        }
        return Result::none;
    }
    if (!input.is_pressed(Action::confirm))
        return Result::none;
    press_.trigger();
    switch (button_)
    {
    case kPrevious:
    case kNext:
    {
        const int direction = button_ == kNext ? 1 : -1;
        if (!session_.can_zap(direction))
        {
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            break;
        }
        session_.zap(direction);
        feedback.play(audio::Cue::tab, direction > 0 ? 0.96f : 1.04f);
        break;
    }
    case kMain:
        if (session_.active())
        {
            session_.stop();
            feedback.play(audio::Cue::back);
        }
        else
        {
            session_.resume();
            feedback.play(audio::Cue::select);
        }
        break;
    case kStar:
        if (session_.has_context())
        {
            session_.toggle_favorite(session_.play_list(), session_.play_index(), feedback);
            star_.trigger();
        }
        break;
    default:
        feedback.play(audio::Cue::select);
        return Result::open_player;
    }
    return Result::none;
}

const radio_station_t *HomeScreen::focused() const
{
    if (grid_.count() == 0)
        return nullptr;
    return session_.browse.peek(static_cast<unsigned>(grid_.focus()));
}

float HomeScreen::appear(int order) const
{
    return tween::stagger(age_, order, 0.06f, 0.5f);
}

void HomeScreen::begin_swap(int from)
{
    const radio_station_t *station = session_.browse.peek(static_cast<unsigned>(from));
    has_previous_ = station != nullptr;
    if (station != nullptr)
    {
        previous_ = *station;
        previous_index_ = static_cast<unsigned>(from);
        previous_favorite_ = session_.browse.favorite(previous_index_);
    }
    travel_ = grid_.focus() >= from ? 1.0f : -1.0f;
    swap_.start(0.34f);
}

bool HomeScreen::unreachable() const
{
    const radio_service_status_t &status = session_.status;
    return status.catalog_state == RADIO_CATALOG_ERROR && status.catalog_size == 0U &&
           !status.refreshing && session_.browse.spec().view != View::favorites;
}

HomeScreen::Result HomeScreen::handle(const InputFrame &input, ui::Feedback &feedback)
{
    Catalog &list = session_.browse;
    if (grid_.count() == 0)
    {
        if (list.known() && list.spec().view == View::favorites &&
            input.is_pressed(Action::confirm))
        {
            feedback.play(audio::Cue::select);
            return Result::browse_popular;
        }
        if (unreachable() && input.is_pressed(Action::confirm))
        {
            feedback.play(audio::Cue::select);
            return Result::try_again;
        }
        return Result::none;
    }

    if (player_focus_)
        return handle_player(input, feedback);
    if (rail_.focused())
    {
        bool leave = false;
        const int target = rail_.handle(input, feedback, &leave);
        if (target >= 0)
        {
            begin_swap(grid_.focus());
            grid_.set_focus_at_top(target);
            player_mode_ = false;
        }
        if (leave)
        {
            rail_.give_back();
            grid_.set_active(true);
        }
        return Result::none;
    }
    // Right from the last column goes to the letters.
    if (input.nav == Direction::right && !input.nav_repeat && rail_.ready() &&
        grid_.focus() % kColumns == kColumns - 1)
    {
        rail_.take_focus(feedback);
        grid_.set_active(false);
        return Result::none;
    }
    // Up from the first row, while something plays: to the transport.
    if (session_.has_playing && input.nav == Direction::up && !input.nav_repeat &&
        grid_.focus() < kColumns)
    {
        player_mode_ = true;
        player_focus_ = true;
        button_ = kMain;
        ring_x_.snap(player_button(button_).cx());
        grid_.set_active(false);
        feedback.play(audio::Cue::focus, 1.04f);
        return Result::none;
    }

    const int before = grid_.focus();
    const ui::Event event = grid_.handle(input, feedback);
    if (event == ui::Event::moved)
    {
        begin_swap(before);
        player_mode_ = false; // the list again: the hero follows the focus
    }
    if (event == ui::Event::activated)
    {
        const radio_station_t *station = focused();
        if (station != nullptr && session_.is_current(*station))
            return Result::open_player;
        session_.play(list, static_cast<unsigned>(grid_.focus()));
        player_mode_ = true;
    }
    if (input.is_pressed(Action::west) && focused() != nullptr)
    {
        session_.toggle_favorite(list, static_cast<unsigned>(grid_.focus()), feedback);
        star_.trigger();
    }
    // L2 and R2 turn a whole screenful; held, they keep turning.
    const int turn = held_.step(input);
    if (turn != 0)
    {
        const int target = std::clamp(before + turn * kColumns * kRowsInView, 0, grid_.count() - 1);
        if (target == before)
        {
            if (!held_.repeating())
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
        }
        else
        {
            grid_.set_focus(target, false);
            feedback.play(audio::Cue::tab, turn > 0 ? 0.96f : 1.04f);
            begin_swap(before);
            player_mode_ = false;
        }
    }
    return Result::none;
}

void HomeScreen::update(float dt)
{
    age_ += dt;
    Catalog &list = session_.browse;
    const int focus = grid_.focus();
    // The rows in view and one screenful either way.
    list.prefetch(static_cast<unsigned>(std::max(focus - 12, 0)),
                  static_cast<unsigned>(focus + 20));
    grid_.set_count(list.known() ? static_cast<int>(list.total()) : 0);
    grid_.style.reduced_motion = session_.settings.reduced_motion;
    empty_.style.reduced_motion = session_.settings.reduced_motion;
    grid_.update(dt);
    empty_.update(dt);
    swap_.update(dt);
    star_.update(dt, 5.0f);
    press_.update(dt, 6.0f);
    rail_.track(list, session_.revision, dt);
    rail_.follow(focused());
    rail_.update(dt);
    if (!session_.has_playing)
    {
        player_mode_ = false;
        if (player_focus_)
            grid_.set_active(true);
        player_focus_ = false;
    }
    player_amount_.target = player_shown() ? 1.0f : 0.0f;
    player_amount_.update(dt, session_.settings.reduced_motion ? 40.0f : 9.0f);
    ring_x_.target = player_button(button_).cx();
    ring_x_.update(dt, 18.0f);

    if (list.spec().view == View::favorites)
    {
        empty_.title = "No favorites yet";
        empty_.body = "Press Square on any station to keep it here.";
        empty_.action = "Browse popular";
    }
    else if (unreachable())
    {
        empty_.title = "Radio Browser cannot be reached";
        empty_.body = "The station list could not be downloaded. Check that the console is "
                      "online, then try again.";
        empty_.action = "Try again";
    }
    else
    {
        empty_.title = "No stations yet";
        empty_.body = "The catalogue is still being built. Stations appear here as soon as "
                      "it is ready.";
        empty_.action.clear();
    }
}

void HomeScreen::draw_hero_text(ui::Canvas &canvas, const radio_station_t &station, unsigned index,
                                bool favorite, float alpha, float dx) const
{
    if (alpha <= 0.01f)
        return;
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const ui::Theme &theme = session_.theme;
    const bool reduced = session_.settings.reduced_motion;
    ui::Painter paint(list, fonts, theme, canvas.glass);
    const float x = kMargin + dx;
    const auto rise = [&](int order) { return reduced ? 0.0f : 16.0f * (1.0f - appear(order)); };
    char text[160];

    list.push_opacity(alpha);

    // ---- which place it holds in this list ----
    list.push_opacity(appear(0));
    switch (session_.browse.spec().view)
    {
    case View::popular:
        std::snprintf(text, sizeof(text), "POPULAR  \xC2\xB7  NO. %u TODAY", index + 1U);
        break;
    case View::trending:
        std::snprintf(text, sizeof(text), "TRENDING  \xC2\xB7  NO. %u", index + 1U);
        break;
    case View::voted:
        std::snprintf(text, sizeof(text), "TOP RATED  \xC2\xB7  NO. %u", index + 1U);
        break;
    default:
        std::snprintf(text, sizeof(text), "FAVORITE");
        break;
    }
    ui::text(list, fonts.semibold, text, x, 164.0f + rise(0), 18.0f, tone::teal, gfx::Align::left,
             4.0f);
    list.pop_opacity();

    // ---- the name: a long one first drops a size, then takes an ellipsis ----
    list.push_opacity(appear(1));
    const float size = fonts.display.measure(station.name, 76.0f) <= kHeroText ? 76.0f : 60.0f;
    ui::text(list, fonts.display, fonts.display.font->fit(station.name, size, kHeroText), x - 3.0f,
             244.0f + rise(1), size, theme.text);
    list.pop_opacity();

    list.push_opacity(appear(2));
    ui::text(list, fonts.regular, fonts.regular.font->fit(place_line(station), 26.0f, kHeroText), x,
             296.0f + rise(2), 26.0f, theme.text_muted);
    list.pop_opacity();

    // ---- what it sends, what it plays, how it is doing ----
    list.push_opacity(appear(3));
    const float chips_y = 350.0f + rise(3);
    float at = x;
    at += draw_chip(paint, at, chips_y, codec_text(station), 1.0f) + 12.0f;
    for (const std::string &tag : split_list(station.tags, 2))
        at += draw_chip(paint, at, chips_y, fonts.semibold.font->fit(tag, 19.0f, 220.0f)) + 12.0f;
    std::snprintf(text, sizeof(text), "%s plays today \xC2\xB7 %s votes",
                  group_digits(station.click_count).c_str(), group_digits(station.votes).c_str());
    ui::text(list, fonts.mono, text, at + 14.0f, baseline_for(chips_y, 20.0f), 20.0f,
             theme.text_muted);
    list.pop_opacity();

    // ---- what Cross and Square will do ----
    list.push_opacity(appear(4));
    const float cy = 426.0f + rise(4);
    const bool current = session_.is_current(station);
    const char *label = current ? (session_.on_air() ? "Now Playing" : "Tuning in") : "Play";
    const float width = 16.0f + 40.0f + 14.0f + fonts.semibold.measure(label, 27.0f) + 34.0f;
    const Rect pill{x, cy - 34.0f, width, 68.0f};
    list.shadow({pill.x, pill.y + 10.0f, pill.w, pill.h}, 34.0f, 24.0f, Color::rgb(0x000000, 0.4f));
    list.rounded_rect(pill, 34.0f, kWhite);
    ui::draw_button(list, fonts, ui::GlyphStyle::dark(), ui::Button::cross, pill.x + 14.0f, cy,
                    40.0f);
    ui::text(list, fonts.semibold, label, pill.x + 70.0f, baseline_for(cy, 27.0f), 27.0f,
             tone::ink);
    const float star_x = pill.x + pill.w + 18.0f + 34.0f;
    list.circle(star_x, cy, 34.0f, kWhite.with_alpha(0.1f));
    list.ring(star_x, cy, 34.0f, 1.5f, kWhite.with_alpha(0.22f));
    list.star(star_x, cy - 1.0f, 15.0f + 8.0f * star_.value, favorite ? tone::gold : kWhite,
              favorite ? 0.0f : 2.5f);
    list.pop_opacity();

    list.pop_opacity();
}

void HomeScreen::draw_hero_art(ui::Canvas &canvas, const radio_station_t &station,
                               float alpha) const
{
    if (alpha <= 0.01f)
        return;
    gfx::DrawList &list = canvas.list;
    const float in = appear(1);
    const Color accent = art_colors(station.uuid).accent;
    list.push_opacity(alpha * in);
    list.shadow({kHeroArt.x, kHeroArt.y + 26.0f, kHeroArt.w, kHeroArt.h}, kHeroRadius, 56.0f,
                Color::rgb(0x000000, 0.5f));
    list.glow(kHeroArt.inset(-4.0f), kHeroRadius + 4.0f, 80.0f, accent.with_alpha(0.26f));
    draw_station_art(list, canvas.fonts, kHeroArt, kHeroRadius, station);
    list.pop_opacity();
}

void HomeScreen::draw_waiting(ui::Canvas &canvas) const
{
    // The shape of the screen while the first page of the catalogue is read.
    gfx::DrawList &list = canvas.list;
    const Color bone = kWhite.with_alpha(0.09f);
    list.rounded_rect({kMargin, 148.0f, 300.0f, 18.0f}, 8.0f, bone);
    list.rounded_rect({kMargin, 190.0f, 880.0f, 64.0f}, 16.0f, bone);
    list.rounded_rect({kMargin, 276.0f, 520.0f, 26.0f}, 10.0f, bone);
    list.rounded_rect({kMargin, 330.0f, 640.0f, 40.0f}, 20.0f, bone);
    list.rounded_rect(kHeroArt, kHeroRadius, bone);
    for (int i = 0; i < kColumns * kRowsInView; ++i)
    {
        const float w = (gfx::kVirtualWidth - 2.0f * kMargin - 3.0f * 24.0f) / kColumns;
        draw_tile_placeholder(
            canvas, session_,
            {kMargin + static_cast<float>(i % kColumns) * (w + 24.0f),
             kGridTop + kGridPad + static_cast<float>(i / kColumns) * (kTileHeight + 20.0f), w,
             kTileHeight});
    }
}

void HomeScreen::draw_player(ui::Canvas &canvas, float alpha) const
{
    if (alpha <= 0.01f)
        return;
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const ui::Theme &theme = session_.theme;
    const radio_station_t &station = session_.playing;
    const Color accent = art_colors(station.uuid).accent;
    const Color bright = gfx::mix(accent, kWhite, 0.35f);
    const bool on_air = session_.on_air();
    const bool reduced = session_.settings.reduced_motion;
    list.push_opacity(alpha);

    // ---- the artwork, breathing with the bass ----
    const float grow = reduced ? 0.0f : 0.012f * session_.energy;
    const Rect art = kPlayerArt.inset(-kPlayerArt.w * grow);
    list.shadow({art.x, art.y + 26.0f, art.w, art.h}, kHeroRadius, 56.0f,
                Color::rgb(0x000000, 0.5f));
    list.glow(art.inset(-4.0f), kHeroRadius + 4.0f, 70.0f + 30.0f * session_.energy,
              accent.with_alpha(on_air ? 0.34f : 0.18f));
    draw_station_art(list, fonts, art, kHeroRadius, station);

    // ---- what plays ----
    char text[160];
    const char *state = on_air              ? "NOW PLAYING  \xC2\xB7  LIVE"
                        : session_.active() ? "NOW PLAYING  \xC2\xB7  TUNING IN"
                                            : "LAST PLAYED";
    ui::text(list, fonts.semibold, state, kPlayerX, 160.0f, 18.0f, on_air ? bright : tone::teal,
             gfx::Align::left, 4.0f);
    const float width = kPlayerRight - kPlayerX;
    const float size = fonts.display.measure(station.name, 72.0f) <= width ? 72.0f : 56.0f;
    ui::text(list, fonts.display, fonts.display.font->fit(station.name, size, width),
             kPlayerX - 3.0f, 232.0f, size, theme.text);
    ui::text(list, fonts.regular, fonts.regular.font->fit(place_line(station), 26.0f, width),
             kPlayerX, 278.0f, 26.0f, theme.text_muted);

    // ---- the visualizer, its floor a mirror ----
    constexpr int kBars = Session::kBands;
    const float bar = width / static_cast<float>(kBars) * 0.36f;
    const float pitch = (width - bar) / static_cast<float>(kBars - 1);
    const Color top = gfx::mix(accent, kWhite, 0.45f);
    const Color bottom = gfx::mix(Color::rgb(0x0b1220), accent, 0.5f);
    for (int i = 0; i < kBars; ++i)
    {
        const float level = session_.levels[static_cast<std::size_t>(i)];
        const float h = bar + level * (kBarsHeight - bar);
        const float x = kPlayerX + static_cast<float>(i) * pitch;
        list.gradient_rect({x, kBarsBase - h, bar, h}, bar * 0.5f,
                           gfx::mix(bottom, top, 0.35f + 0.65f * level), bottom);
        list.gradient_rect({x, kBarsBase + 6.0f, bar, bar + (h - bar) * 0.35f}, bar * 0.5f,
                           bottom.with_alpha(0.28f), bottom.with_alpha(0.0f));
    }

    // ---- the transport ----
    for (int i = 0; i < kPlayerButtons; ++i)
    {
        const Rect b = player_button(i);
        const float cx = b.cx();
        const bool focus = player_focus_ && i == button_;
        const float squeeze = focus ? 1.0f - 0.08f * press_.value : 1.0f;
        list.push_transform(squeeze, cx, kTransportY, 0.0f, 0.0f);
        if (i == kMain)
        {
            list.glow(b, kMainRadius, 18.0f + 12.0f * session_.energy,
                      bright.with_alpha(on_air ? 0.35f : 0.15f));
            list.circle(cx, kTransportY, kMainRadius, kWhite);
            if (session_.active())
                draw_stop_icon(list, cx, kTransportY, 32.0f, tone::ink);
            else
                draw_play_icon(list, cx, kTransportY, 34.0f, tone::ink);
        }
        else
        {
            const bool dead =
                (i == kPrevious && !session_.can_zap(-1)) || (i == kNext && !session_.can_zap(1));
            list.circle(cx, kTransportY, kButtonRadius, kWhite.with_alpha(focus ? 0.18f : 0.08f));
            list.ring(cx, kTransportY, kButtonRadius, 1.5f, kWhite.with_alpha(0.16f));
            const Color ink = dead ? Color::rgb(0x6c7480) : (focus ? kWhite : Color::rgb(0xd4d7e2));
            if (i == kStar)
            {
                const bool on = radio_service_is_favorite(station.uuid);
                list.star(cx, kTransportY - 1.0f, 13.0f + 6.0f * star_.value, on ? tone::gold : ink,
                          on ? 0.0f : 2.2f);
            }
            else if (i == kFull)
            {
                // Two corners: the full screen.
                list.line(cx - 10.0f, kTransportY - 10.0f, cx - 3.0f, kTransportY - 10.0f, 2.5f,
                          ink);
                list.line(cx - 10.0f, kTransportY - 10.0f, cx - 10.0f, kTransportY - 3.0f, 2.5f,
                          ink);
                list.line(cx + 10.0f, kTransportY + 10.0f, cx + 3.0f, kTransportY + 10.0f, 2.5f,
                          ink);
                list.line(cx + 10.0f, kTransportY + 10.0f, cx + 10.0f, kTransportY + 3.0f, 2.5f,
                          ink);
            }
            else
            {
                draw_skip_icon(list, cx, kTransportY, 20.0f, i == kNext ? 1.0f : -1.0f, ink);
            }
        }
        list.pop_transform();
    }
    if (player_focus_)
    {
        const float r = button_ == kMain ? kMainRadius : kButtonRadius;
        list.ring(ring_x_.value, kTransportY, r + 7.0f, 3.5f, kWhite);
    }

    // ---- time on air ----
    char clock[24];
    mm_ss(clock, sizeof(clock), session_.listening);
    if (on_air)
        std::snprintf(text, sizeof(text), "%s on air", clock);
    else
        std::snprintf(text, sizeof(text), "%s",
                      session_.active() ? "Waiting for the stream" : "Stopped");
    const float after = player_button(kFull).x + player_button(kFull).w + 36.0f;
    ui::text(list, fonts.mono, text, after, baseline_for(kTransportY, 22.0f), 22.0f,
             kWhite.with_alpha(0.8f));
    ui::text(list, fonts.semibold, codec_text(station), kPlayerRight,
             baseline_for(kTransportY, 21.0f), 21.0f, theme.text_muted, gfx::Align::right);
    list.pop_opacity();
}

void HomeScreen::draw(ui::Canvas &canvas) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const ui::Theme &theme = session_.theme;
    const Catalog &catalog = session_.browse;

    if (!catalog.known())
    {
        draw_waiting(canvas);
        return;
    }
    if (grid_.count() == 0)
    {
        empty_.draw(canvas);
        return;
    }

    const unsigned index = static_cast<unsigned>(grid_.focus());
    const radio_station_t *station = catalog.peek(index);
    const float player = tween::smoothstep(player_amount_.value);
    draw_player(canvas, player);
    list.push_opacity(1.0f - player);
    if (station != nullptr && player < 0.99f)
    {
        if (swap_.running && has_previous_)
        {
            // The old lines leave quickly; the new ones land a beat later.
            const float t = swap_.progress();
            const float distance = session_.settings.reduced_motion ? 0.0f : 1.0f;
            const float leave = tween::clamp01(t * 3.0f);
            const float arrive = tween::clamp01((t - 0.24f) / 0.76f);
            draw_hero_art(canvas, previous_, 1.0f - tween::smoothstep(leave));
            draw_hero_art(canvas, *station, tween::smoothstep(arrive));
            draw_hero_text(canvas, previous_, previous_index_, previous_favorite_,
                           1.0f - tween::smoothstep(leave),
                           -travel_ * 36.0f * distance * tween::cubic_in(leave));
            draw_hero_text(canvas, *station, index, catalog.favorite(index),
                           tween::smoothstep(arrive),
                           travel_ * 48.0f * distance * (1.0f - tween::quint_out(arrive)));
        }
        else
        {
            draw_hero_art(canvas, *station, 1.0f);
            draw_hero_text(canvas, *station, index, catalog.favorite(index), 1.0f, 0.0f);
        }
    }
    list.pop_opacity();

    list.push_opacity(appear(5));
    ui::text(list, fonts.semibold, kListTitle[static_cast<int>(catalog.spec().view)], kMargin,
             508.0f, 27.0f, theme.text);
    char position[48];
    std::snprintf(position, sizeof(position), "%s of %s", group_digits(index + 1U).c_str(),
                  group_digits(catalog.total()).c_str());
    ui::text(list, fonts.mono, position, kRight, 508.0f, 20.0f, theme.text_muted,
             gfx::Align::right);
    list.pop_opacity();

    grid_.draw(canvas);
    rail_.draw(canvas);
}

int HomeScreen::hints(ui::Hint *out, int capacity) const
{
    int count = 0;
    const auto add = [&](ui::Hint hint)
    {
        if (count < capacity)
            out[count++] = hint;
    };
    if (rail_.focused())
    {
        add({ui::Button::dpad, "Letter"});
        add({ui::Button::cross, "Back to the list"});
        return count;
    }
    if (player_focus_)
    {
        static constexpr const char *kLabels[] = {"Previous", "Stop", "Next", "Favorite",
                                                  "Full screen"};
        const char *label = button_ == kMain && !session_.active() ? "Play" : kLabels[button_];
        add({ui::Button::cross, label});
        add({ui::Button::circle, "Back to the list"});
        add({ui::Button::options, "Menu"});
        return count;
    }
    const radio_station_t *station = focused();
    if (station != nullptr)
    {
        add({ui::Button::cross, session_.is_current(*station) ? "Now Playing" : "Play"});
        add({ui::Button::square, session_.browse.favorite(static_cast<unsigned>(grid_.focus()))
                                     ? "Unfavorite"
                                     : "Favorite"});
    }
    else if (session_.browse.known() && session_.browse.spec().view == View::favorites)
    {
        add({ui::Button::cross, "Browse popular"});
    }
    add({ui::Button::triangle, "Search"});
    add({ui::Button::options, "Menu"});
    return count;
}

} // namespace radio
