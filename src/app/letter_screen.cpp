// ProsperoRadio - A–Z and Favorites: an alphabetical list with a letter rail.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/letter_screen.hpp"

#include "app/draw.hpp"
#include "app/station_art.hpp"
#include "app/theme.hpp"

#include <algorithm>
#include <cstdio>

namespace radio
{

namespace
{

constexpr float kTop = 132.0f;
constexpr float kRowHeight = 84.0f;
constexpr float kRowGap = 10.0f;
constexpr float kPad = 16.0f; // room for the focused row to grow
constexpr Rect kList{kMargin - kPad, kTop - kPad, 1060.0f + 2.0f * kPad, 772.0f + 2.0f * kPad};
constexpr float kDetailX = 1222.0f;
constexpr float kDetailW = 470.0f;
constexpr Rect kArt{kDetailX, kTop + 4.0f, 300.0f, 300.0f};
constexpr float kRailX = gfx::kVirtualWidth - kMargin - 44.0f;

// Some names begin with spaces; the list is sorted without them, and shown so.
const char *trimmed(const char *name)
{
    while (*name == ' ' || *name == '\t')
        ++name;
    return name;
}

// The rail's marks, in the list's order: the service sorts names without
// regard to case, so digits and signs come first and other scripts last.
constexpr const char *kMarks[kInitialBuckets] = {
    "#", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "\xE2\x80\xA6"};

} // namespace

LetterScreen::LetterScreen(Session &session) : session_(session)
{
    const ui::Theme &theme = session.theme;
    list_.style.theme = theme;
    list_.style.columns = 1;
    list_.style.cell_height = kRowHeight;
    list_.style.gap_y = kRowGap;
    list_.style.padding = kPad;
    list_.style.card.radius = 18.0f;
    list_.style.card.focus_scale = 1.01f;
    list_.style.card.lift = 2.0f;
    list_.style.card.glow = true;
    list_.set_bounds(kList);
    list_.content =
        [this](ui::Canvas &canvas, const Rect &cell, const ui::CardItem &, int index, float focus)
    {
        gfx::DrawList &list = canvas.list;
        const ui::Fonts &fonts = canvas.fonts;
        const ui::Theme &theme = session_.theme;
        const radio_station_t *station = session_.browse.peek(static_cast<unsigned>(index));
        // Opaque: the card's accent glow stays around the row, not under the words.
        const Color tint = station != nullptr ? art_colors(station->uuid).accent : kWhite;
        list.rounded_rect(
            cell, 18.0f,
            gfx::mix(Color::rgb(0x121b27), gfx::mix(Color::rgb(0x121b27), tint, 0.3f), focus));
        if (station == nullptr)
        {
            list.rounded_rect({cell.x + 14.0f, cell.y + 12.0f, 60.0f, 60.0f}, 12.0f,
                              kWhite.with_alpha(0.07f));
            list.rounded_rect({cell.x + 92.0f, cell.y + 22.0f, 360.0f, 20.0f}, 8.0f,
                              kWhite.with_alpha(0.07f));
            return;
        }
        draw_station_art(list, fonts, {cell.x + 14.0f, cell.y + 12.0f, 60.0f, 60.0f}, 12.0f,
                         *station);
        const float x = cell.x + 92.0f;
        const bool current = session_.is_current(*station);
        float right = cell.x + cell.w - 22.0f;
        if (session_.browse.favorite(static_cast<unsigned>(index)))
        {
            list.star(right - 10.0f, cell.cy(), 11.0f, tone::gold, 0.0f);
            right -= 34.0f;
        }
        const std::string codec = codec_short(*station);
        right -= ui::text(list, fonts.mono, codec, right, baseline_for(cell.cy(), 19.0f), 19.0f,
                          theme.text_muted, gfx::Align::right) +
                 22.0f;
        const float room = right - x;
        ui::text(list, fonts.semibold,
                 fonts.semibold.font->fit(trimmed(station->name), 26.0f, room), x, cell.y + 38.0f,
                 26.0f, theme.text);
        float after = x;
        if (current && session_.on_air())
        {
            draw_equalizer(list, session_, x + 2.0f, cell.y + 66.0f, 16.0f, tone::live);
            after += 30.0f;
        }
        ui::text(list, fonts.regular,
                 fonts.regular.font->fit(place_line(*station), 20.0f, room - (after - x)), after,
                 cell.y + 66.0f, 20.0f, current ? tone::teal : theme.text_muted);
    };
    list_.accent = [this](int index)
    {
        const radio_station_t *station = session_.browse.peek(static_cast<unsigned>(index));
        return station != nullptr ? art_colors(station->uuid).accent : kClear;
    };

    rail_.style.theme = theme;
    rail_.style.item_size = 26.0f;
    rail_.style.thickness = 44.0f;
    rail_.style.text_size = 17.0f;
    rail_.style.exits.left = true;
    std::vector<ui::JumpEntry> marks;
    for (int i = 0; i < kInitialBuckets; ++i)
        marks.push_back({kMarks[i], true, i});
    rail_.set_entries(std::move(marks));
    rail_.set_bounds({kRailX, kTop, 44.0f, 772.0f});
    rail_.set_focused(false);

    empty_.style.theme = theme;
    empty_.style.title_size = 44.0f;
    empty_.style.body_size = 26.0f;
    empty_.style.hint_size = 26.0f;
    empty_.style.max_text_width = 760.0f;
    empty_.set_bounds({460.0f, 250.0f, 1000.0f, 520.0f});
    empty_.icon = [](ui::Canvas &canvas, const Rect &area)
    { canvas.list.star(area.cx(), area.cy(), area.w * 0.46f, tone::teal, 3.5f); };
}

void LetterScreen::enter()
{
    age_ = 0.0f;
    list_.enter();
    empty_.enter();
    counted_ = false; // the list may have changed while away
}

void LetterScreen::reset()
{
    list_.set_focus(0, true);
    on_rail_ = false;
    rail_.set_focused(false);
    list_.set_active(true);
    enter();
}

void LetterScreen::count_letters()
{
    const bool favorites = session_.browse.spec().view == View::favorites;
    counted_ = radio_catalog_initials(favorites, counts_);
    counted_total_ = session_.browse.total();
    counted_revision_ = session_.revision;
    offsets_[0] = 0;
    for (int i = 0; i < kInitialBuckets; ++i)
    {
        offsets_[i + 1] = offsets_[i] + counts_[i];
        rail_.set_enabled(i, counts_[i] != 0U);
    }
}

int LetterScreen::first_of(int bucket) const
{
    if (bucket < 0 || bucket >= kInitialBuckets || counts_[bucket] == 0U)
        return -1;
    return static_cast<int>(offsets_[bucket]);
}

int LetterScreen::bucket_at(int index) const
{
    for (int i = kInitialBuckets - 1; i >= 0; --i)
    {
        if (counts_[i] != 0U && static_cast<unsigned>(index) >= offsets_[i])
            return i;
    }
    return 0;
}

void LetterScreen::jump_to_bucket(int bucket)
{
    const int first = first_of(bucket);
    // At once: gliding through thousands of rows would show none of them.
    if (first >= 0 && first < list_.count())
        list_.set_focus_at_top(first);
}

LetterScreen::Result LetterScreen::handle(const InputFrame &input, ui::Feedback &feedback)
{
    Catalog &list = session_.browse;
    const int turn = held_.step(input);
    if (list_.count() == 0)
    {
        if (list.known() && list.spec().view == View::favorites &&
            input.is_pressed(Action::confirm))
        {
            feedback.play(audio::Cue::select);
            return Result::browse_popular;
        }
        return Result::none;
    }

    // L2 and R2: the previous and next letter that has stations, from
    // anywhere; held, they go on. The rail follows and shows its bubble.
    if (turn != 0 && counted_)
    {
        const int here = bucket_at(list_.focus());
        int target = -1;
        if (turn < 0 && list_.focus() > first_of(here))
            target = here; // back to the start of this letter first
        for (int b = here + turn; target < 0 && b >= 0 && b < kInitialBuckets; b += turn)
        {
            if (counts_[b] != 0U)
                target = b;
        }
        if (target < 0)
        {
            if (!held_.repeating())
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
        }
        else if (target == here)
        {
            jump_to_bucket(target);
            feedback.play(audio::Cue::tab, 1.04f);
        }
        else
        {
            // The rail takes the step (its cue, and its bubble shows where
            // it went); it steps over letters without stations, as we did.
            rail_.set_current(here, true);
            rail_.step(turn, input, feedback);
            jump_to_bucket(rail_.current());
        }
        return Result::none;
    }

    if (on_rail_)
    {
        const ui::Event event = rail_.handle(input, feedback);
        if (event == ui::Event::changed)
            jump_to_bucket(rail_.current());
        if (event == ui::Event::cancelled || event == ui::Event::activated ||
            rail_.exit() == Direction::left)
        {
            on_rail_ = false;
            rail_.set_focused(false);
            list_.set_active(true);
        }
        return Result::none;
    }

    // Right goes to the letters.
    if (input.nav == Direction::right && !input.nav_repeat && counted_)
    {
        on_rail_ = true;
        rail_.set_focused(true);
        list_.set_active(false);
        feedback.play(audio::Cue::focus, 1.04f);
        return Result::none;
    }

    const ui::Event event = list_.handle(input, feedback);
    if (event == ui::Event::activated)
    {
        const radio_station_t *station = list.peek(static_cast<unsigned>(list_.focus()));
        if (station != nullptr && session_.is_current(*station))
            return Result::open_player;
        session_.play(list, static_cast<unsigned>(list_.focus()));
    }
    if (input.is_pressed(Action::west) && list.peek(static_cast<unsigned>(list_.focus())))
    {
        session_.toggle_favorite(list, static_cast<unsigned>(list_.focus()), feedback);
        star_.trigger();
        counted_ = false; // Favorites may have one station more or less
    }
    return Result::none;
}

void LetterScreen::update(float dt)
{
    age_ += dt;
    Catalog &list = session_.browse;
    const int focus = list_.focus();
    list.prefetch(static_cast<unsigned>(std::max(focus - 10, 0)),
                  static_cast<unsigned>(focus + 14));
    list_.set_count(list.known() ? static_cast<int>(list.total()) : 0);
    // The counts are read again when the list changed under them.
    // A failed count is tried again a little later, not every frame.
    retry_ = std::max(0.0f, retry_ - dt);
    if (list.known() && retry_ <= 0.0f &&
        (!counted_ || counted_total_ != list.total() || counted_revision_ != session_.revision))
    {
        count_letters();
        if (!counted_)
            retry_ = 2.0f;
    }
    if (counted_ && !on_rail_ && list_.count() > 0)
        rail_.set_current(bucket_at(focus));
    list_.style.reduced_motion = session_.settings.reduced_motion;
    rail_.style.reduced_motion = session_.settings.reduced_motion;
    empty_.style.reduced_motion = session_.settings.reduced_motion;
    list_.update(dt);
    rail_.update(dt);
    empty_.update(dt);
    star_.update(dt, 5.0f);

    if (list.spec().view == View::favorites)
    {
        empty_.title = "No favorites yet";
        empty_.body = "Press Square on any station to keep it here.";
        empty_.action = "Browse popular";
    }
    else
    {
        empty_.title = "No stations yet";
        empty_.body = "The catalogue is still being built.";
        empty_.action.clear();
    }
}

void LetterScreen::draw_details(ui::Canvas &canvas, const radio_station_t &station,
                                unsigned index) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const ui::Theme &theme = session_.theme;
    ui::Painter paint(list, fonts, theme, canvas.glass);
    const Color accent = art_colors(station.uuid).accent;
    list.shadow({kArt.x, kArt.y + 22.0f, kArt.w, kArt.h}, 30.0f, 48.0f, Color::rgb(0x000000, 0.5f));
    list.glow(kArt.inset(-4.0f), 34.0f, 70.0f, accent.with_alpha(0.24f));
    draw_station_art(list, fonts, kArt, 30.0f, station);

    float y = kArt.y + kArt.h + 62.0f;
    const bool current = session_.is_current(station);
    const char *label = current ? (session_.on_air() ? "NOW PLAYING" : "TUNING IN") : "FAVORITE";
    ui::text(list, fonts.semibold, label, kDetailX, y - 26.0f, 17.0f,
             current ? tone::live : tone::teal, gfx::Align::left, 4.0f);
    const char *name = trimmed(station.name);
    const float size = fonts.display.measure(name, 44.0f) <= kDetailW ? 44.0f : 34.0f;
    ui::text(list, fonts.display, fonts.display.font->fit(name, size, kDetailW), kDetailX,
             y + 22.0f, size, theme.text);
    y += 64.0f;
    ui::text(list, fonts.regular, fonts.regular.font->fit(place_line(station), 22.0f, kDetailW),
             kDetailX, y, 22.0f, theme.text_muted);
    y += 46.0f;
    float at = kDetailX;
    at += draw_chip(paint, at, y, codec_text(station), 1.0f, 36.0f) + 10.0f;
    for (const std::string &tag : split_list(station.tags, 1))
        draw_chip(paint, at, y, fonts.semibold.font->fit(tag, 19.0f, 180.0f), 0.0f, 36.0f);
    y += 46.0f;
    char text[96];
    std::snprintf(text, sizeof(text), "%s plays today \xC2\xB7 %s votes",
                  group_digits(station.click_count).c_str(), group_digits(station.votes).c_str());
    ui::text(list, fonts.mono, text, kDetailX, y, 19.0f, theme.text_muted);
    (void)index;
}

void LetterScreen::draw(ui::Canvas &canvas) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const ui::Theme &theme = session_.theme;
    const Catalog &catalog = session_.browse;
    if (!catalog.known())
        return;
    if (list_.count() == 0)
    {
        empty_.draw(canvas);
        return;
    }
    list_.draw(canvas);
    const unsigned index = static_cast<unsigned>(list_.focus());
    if (const radio_station_t *station = catalog.peek(index))
        draw_details(canvas, *station, index);
    char position[48];
    std::snprintf(position, sizeof(position), "%s of %s", group_digits(index + 1U).c_str(),
                  group_digits(catalog.total()).c_str());
    ui::text(list, fonts.mono, position, kDetailX, kTop + kList.h - 34.0f, 20.0f, theme.text_muted);
    rail_.draw(canvas);
}

int LetterScreen::hints(ui::Hint *out, int capacity) const
{
    int count = 0;
    const auto add = [&](ui::Hint hint)
    {
        if (count < capacity)
            out[count++] = hint;
    };
    if (on_rail_)
    {
        add({ui::Button::cross, "Back to the list"});
        add({ui::Button::l2, "Letter", ui::Button::r2});
        return count;
    }
    const radio_station_t *station = session_.browse.peek(static_cast<unsigned>(list_.focus()));
    if (station != nullptr)
    {
        add({ui::Button::cross, session_.is_current(*station) ? "Now Playing" : "Play"});
        add({ui::Button::square, session_.browse.favorite(static_cast<unsigned>(list_.focus()))
                                     ? "Unfavorite"
                                     : "Favorite"});
        add({ui::Button::l2, "Letter", ui::Button::r2});
    }
    else if (session_.browse.known() && session_.browse.spec().view == View::favorites)
    {
        add({ui::Button::cross, "Browse popular"});
    }
    add({ui::Button::triangle, "Search"});
    return count;
}

} // namespace radio
