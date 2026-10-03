// ProsperoRadio - Discover: a search box and filters beside their live results.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/search_screen.hpp"

#include "app/draw.hpp"
#include "app/station_art.hpp"
#include "app/theme.hpp"
#include "radio_ime.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace radio
{

namespace
{

constexpr Rect kPanel{kMargin, 140.0f, 600.0f, 764.0f};
constexpr float kInner = kPanel.x + 30.0f;
constexpr float kInnerWidth = kPanel.w - 60.0f;
constexpr int kResultColumns = 2;
constexpr int kResultRows = 4;
constexpr float kResultHeight = 152.0f;
constexpr float kResultsLeft = 744.0f;
constexpr float kResultsTop = 180.0f;
constexpr float kPad = 20.0f;
constexpr float kRailWidth = 44.0f; // the letter rail at the right edge

constexpr unsigned kBitrates[] = {0, 64, 128, 192, 256};
constexpr const char *kQuick[] = {"pop",        "rock", "jazz",    "news",   "classical",
                                  "electronic", "talk", "hip hop", "ambient"};

bool equal_nocase(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; ++a, ++b)
    {
        const char x = *a >= 'A' && *a <= 'Z' ? static_cast<char>(*a - 'A' + 'a') : *a;
        const char y = *b >= 'A' && *b <= 'Z' ? static_cast<char>(*b - 'A' + 'a') : *b;
        if (x != y)
            return false;
    }
    return *a == *b;
}

std::vector<ui::SelectOption> options_of(const char *any, const std::vector<radio_facet_t> &facets)
{
    std::vector<ui::SelectOption> options;
    options.reserve(facets.size() + 1);
    options.emplace_back(any);
    for (const radio_facet_t &facet : facets)
        options.emplace_back(facet.label[0] != '\0' ? facet.label : facet.value);
    return options;
}

// The option a filter value stands for: 0 is "any", -1 a value no facet has.
int index_of(const std::vector<radio_facet_t> &facets, const std::string &value)
{
    if (value.empty())
        return 0;
    for (std::size_t i = 0; i < facets.size(); ++i)
    {
        if (equal_nocase(facets[i].value, value.c_str()))
            return static_cast<int>(i) + 1;
    }
    return -1;
}

void style_select(ui::Select &select, const ui::Theme &theme, const char *label)
{
    select.style.theme = theme;
    select.style.max_rows = 7;
    select.style.label_size = 20.0f;
    // Wide enough for "United Kingdom", whatever the field's own width.
    select.style.popover_width = 440.0f;
    select.set_label(label);
    select.set_limits({kMargin, 120.0f, gfx::kVirtualWidth - 2.0f * kMargin, 790.0f});
}

} // namespace

SearchScreen::SearchScreen(Session &session) : session_(session), rail_(session.theme)
{
    rail_.set_bounds({gfx::kVirtualWidth - kMargin - kRailWidth, kResultsTop, kRailWidth, 724.0f});
    const ui::Theme &theme = session.theme;
    field_.style.theme = theme;
    field_.style.field_height = 64.0f;
    field_.style.remember = false;
    field_.style.exits.down = true;
    field_.style.exits.right = true;
    field_.set_placeholder("Station, genre, place");
    field_.set_bounds({kInner, 216.0f, kInnerWidth, 64.0f});

    style_select(country_, theme, "Country");
    style_select(language_, theme, "Language");
    style_select(genre_, theme, "Genre");
    const float half = (kInnerWidth - 18.0f) * 0.5f;
    country_.set_bounds({kInner, 304.0f, half, country_.preferred_height()});
    language_.set_bounds({kInner + half + 18.0f, 304.0f, half, language_.preferred_height()});
    genre_.set_bounds({kInner, 410.0f, kInnerWidth, genre_.preferred_height()});

    bitrate_.style.theme = theme;
    bitrate_.set_options({"Any", "64 kbps", "128 kbps", "192 kbps", "256 kbps"});
    bitrate_.set_bounds({kInner, 548.0f, kInnerWidth, 60.0f});

    quick_.style.theme = theme;
    quick_.style.single = true;
    quick_.style.check = false;
    quick_.style.counter = false;
    quick_.style.exits.up = true;
    quick_.style.exits.right = true;
    quick_.set_title("Quick genres");
    std::vector<ui::TagOption> chips;
    for (const char *name : kQuick)
        chips.emplace_back(name);
    quick_.set_options(std::move(chips));
    quick_.set_bounds({kInner, 632.0f, kInnerWidth, 250.0f});

    results_.style.theme = theme;
    results_.style.columns = kResultColumns;
    results_.style.cell_height = kResultHeight;
    results_.style.gap_x = 24.0f;
    results_.style.gap_y = 20.0f;
    results_.style.padding = kPad;
    results_.style.card.radius = 20.0f;
    results_.style.card.focus_scale = 1.03f;
    results_.style.card.lift = 4.0f;
    results_.style.card.glow = true;
    results_.style.exits.left = true;
    results_.set_bounds(
        {kResultsLeft - kPad, kResultsTop,
         gfx::kVirtualWidth - kMargin - kRailWidth - 32.0f - kResultsLeft + 2.0f * kPad,
         kResultRows * (kResultHeight + 20.0f) + 2.0f * kPad + 14.0f});
    results_.content =
        [this](ui::Canvas &canvas, const Rect &cell, const ui::CardItem &, int index, float focus)
    {
        const unsigned at = static_cast<unsigned>(index);
        const radio_station_t *station = session_.browse.peek(at);
        if (station == nullptr)
        {
            draw_tile_placeholder(canvas, session_, cell);
            return;
        }
        char rank[16];
        std::snprintf(rank, sizeof(rank), "#%u", at + 1U);
        draw_station_tile(canvas, session_, cell, *station, rank, codec_short(*station),
                          session_.browse.favorite(at), focus);
    };
    results_.accent = [this](int index)
    {
        const radio_station_t *station = session_.browse.peek(static_cast<unsigned>(index));
        return station != nullptr ? art_colors(station->uuid).accent : kClear;
    };

    empty_.style.theme = theme;
    empty_.style.title_size = 38.0f;
    empty_.style.body_size = 24.0f;
    empty_.style.hint_size = 24.0f;
    empty_.style.max_text_width = 640.0f;
    empty_.title = "No stations match";
    empty_.body = "Try fewer filters, or check the spelling.";
    empty_.action = "Reset filters";
    empty_.action_button = ui::Button::square;
    empty_.set_bounds({kResultsLeft, 260.0f, gfx::kVirtualWidth - kMargin - kResultsLeft, 480.0f});

    facets_changed();
}

void SearchScreen::enter()
{
    age_ = 0.0f;
    results_.enter();
    empty_.enter();
}

void SearchScreen::facets_changed()
{
    country_.set_options(options_of("Any country", session_.countries));
    language_.set_options(options_of("Any language", session_.languages));
    genre_.set_options(options_of("Any genre", session_.genres));
    country_.set_index(index_of(session_.countries, country_code_));
    language_.set_index(index_of(session_.languages, language_value_));
    genre_.set_index(index_of(session_.genres, genre_value_));
    genre_.set_placeholder(genre_value_.empty() ? "Any genre" : genre_value_);
}

ListSpec SearchScreen::spec() const
{
    ListSpec spec;
    spec.view = View::discover;
    std::snprintf(spec.query.name, sizeof(spec.query.name), "%s", text_.c_str());
    std::snprintf(spec.query.country_code, sizeof(spec.query.country_code), "%s",
                  country_code_.c_str());
    std::snprintf(spec.query.tag, sizeof(spec.query.tag), "%s", genre_value_.c_str());
    std::snprintf(spec.query.language, sizeof(spec.query.language), "%s", language_value_.c_str());
    spec.query.bitrate_min = bitrate_min_;
    return spec;
}

void SearchScreen::apply()
{
    const ListSpec wanted = spec();
    if (same_spec(wanted, session_.browse.spec()))
        return;
    session_.browse.set_spec(wanted);
    results_.set_focus(0, true);
    results_.enter();
    // The local catalogue answers at once; Radio Browser is asked as well, and
    // what it adds arrives through the same list.
    if (has_filters(wanted.query))
        radio_service_search(&wanted.query);
}

void SearchScreen::typed(const char *text, void *self)
{
    SearchScreen *screen = static_cast<SearchScreen *>(self);
    if (screen == nullptr || text == nullptr)
        return;
    screen->text_ = text;
    screen->field_.set_text(screen->text_);
    screen->apply();
}

void SearchScreen::sync_quick()
{
    for (int i = 0; i < static_cast<int>(std::size(kQuick)); ++i)
        quick_.set_selected(i, equal_nocase(kQuick[i], genre_value_.c_str()));
}

void SearchScreen::reset_filters(ui::Feedback &feedback)
{
    text_.clear();
    country_code_.clear();
    genre_value_.clear();
    language_value_.clear();
    bitrate_min_ = 0;
    field_.set_text("");
    bitrate_.set_index(0);
    facets_changed();
    sync_quick();
    feedback.play(audio::Cue::erase);
    apply();
}

void SearchScreen::go(Zone zone, ui::Feedback &feedback)
{
    if (zone == zone_)
        return;
    zone_ = zone;
    if (zone != Zone::results)
        panel_zone_ = zone;
    feedback.play(audio::Cue::focus, 1.0f, zone == Zone::results ? 0.2f : -0.35f);
}

bool SearchScreen::modal() const
{
    return country_.is_open() || language_.is_open() || genre_.is_open();
}

const radio_station_t *SearchScreen::focused() const
{
    if (zone_ != Zone::results || results_.count() == 0)
        return nullptr;
    return session_.browse.peek(static_cast<unsigned>(results_.focus()));
}

SearchScreen::Result SearchScreen::handle_results(const InputFrame &input, ui::Feedback &feedback)
{
    Catalog &list = session_.browse;
    if (results_.count() == 0)
    {
        go(panel_zone_, feedback);
        return Result::none;
    }
    if (rail_.focused())
    {
        bool leave = false;
        const int target = rail_.handle(input, feedback, &leave);
        if (target >= 0)
            results_.set_focus_at_top(target);
        if (leave)
        {
            rail_.give_back();
            results_.set_active(true);
        }
        return Result::none;
    }
    // Right from the last column goes to the letters.
    if (input.nav == Direction::right && !input.nav_repeat && rail_.ready() &&
        results_.focus() % kResultColumns == kResultColumns - 1)
    {
        rail_.take_focus(feedback);
        results_.set_active(false);
        return Result::none;
    }
    const int before = results_.focus();
    const ui::Event event = results_.handle(input, feedback);
    if (results_.exit() == Direction::left)
    {
        go(panel_zone_, feedback);
        return Result::none;
    }
    if (event == ui::Event::activated)
    {
        const radio_station_t *station = list.peek(static_cast<unsigned>(results_.focus()));
        if (station != nullptr && session_.is_current(*station))
            return Result::open_player;
        session_.play(list, static_cast<unsigned>(results_.focus()));
    }
    if (input.is_pressed(Action::west) &&
        list.peek(static_cast<unsigned>(results_.focus())) != nullptr)
        session_.toggle_favorite(list, static_cast<unsigned>(results_.focus()), feedback);
    // L2 and R2 turn a screenful; held, they keep turning.
    const int turn = held_.step(input);
    if (turn != 0)
    {
        const int target =
            std::clamp(before + turn * kResultColumns * kResultRows, 0, results_.count() - 1);
        if (target != before)
        {
            results_.set_focus(target, false);
            feedback.play(audio::Cue::tab, turn > 0 ? 0.96f : 1.04f);
        }
        else if (!held_.repeating())
        {
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
        }
    }
    return Result::none;
}

SearchScreen::Result SearchScreen::handle(const InputFrame &input, ui::Feedback &feedback)
{
    // ---- an open dropdown takes everything ----
    struct Dropdown
    {
        ui::Select *select;
        const std::vector<radio_facet_t> *facets;
        std::string *value;
    };
    const Dropdown dropdowns[] = {{&country_, &session_.countries, &country_code_},
                                  {&language_, &session_.languages, &language_value_},
                                  {&genre_, &session_.genres, &genre_value_}};
    for (const Dropdown &dropdown : dropdowns)
    {
        const bool focused = (dropdown.select == &country_ && zone_ == Zone::country) ||
                             (dropdown.select == &language_ && zone_ == Zone::language) ||
                             (dropdown.select == &genre_ && zone_ == Zone::genre);
        // Closed, a dropdown only wants the confirm that opens it.
        if (!dropdown.select->is_open() && !(focused && input.is_pressed(Action::confirm)))
            continue;
        if (dropdown.select->handle(input, feedback) == ui::Event::changed)
        {
            const int index = dropdown.select->index();
            *dropdown.value =
                index > 0 ? (*dropdown.facets)[static_cast<std::size_t>(index - 1)].value : "";
            if (dropdown.select == &genre_)
            {
                genre_.set_placeholder("Any genre");
                sync_quick();
            }
            apply();
        }
        return Result::none;
    }

    if (zone_ == Zone::results)
    {
        if (input.is_pressed(Action::west) && results_.count() == 0)
        {
            reset_filters(feedback);
            return Result::none;
        }
        return handle_results(input, feedback);
    }
    if (input.is_pressed(Action::west))
    {
        reset_filters(feedback);
        return Result::none;
    }

    const Direction nav = input.nav;
    switch (zone_)
    {
    case Zone::field:
    {
        const ui::Event event = field_.handle(input, feedback);
        if (event == ui::Event::activated && !field_.has_pick())
            radio_ime_request(text_.c_str(), &SearchScreen::typed, this);
        if (field_.text() != text_)
        {
            // The field's own clear button.
            text_ = field_.text();
            apply();
        }
        if (field_.exit() == Direction::down)
            go(Zone::country, feedback);
        else if (field_.exit() == Direction::right)
            go(Zone::results, feedback);
        break;
    }
    case Zone::country:
        if (nav == Direction::up)
            go(Zone::field, feedback);
        else if (nav == Direction::down)
            go(Zone::genre, feedback);
        else if (nav == Direction::right)
            go(Zone::language, feedback);
        break;
    case Zone::language:
        if (nav == Direction::up)
            go(Zone::field, feedback);
        else if (nav == Direction::down)
            go(Zone::genre, feedback);
        else if (nav == Direction::left)
            go(Zone::country, feedback);
        else if (nav == Direction::right)
            go(Zone::results, feedback);
        break;
    case Zone::genre:
        if (nav == Direction::up)
            go(Zone::country, feedback);
        else if (nav == Direction::down)
            go(Zone::bitrate, feedback);
        else if (nav == Direction::right)
            go(Zone::results, feedback);
        break;
    case Zone::bitrate:
        if (nav == Direction::up)
        {
            go(Zone::genre, feedback);
        }
        else if (nav == Direction::down)
        {
            go(Zone::quick, feedback);
        }
        else if (bitrate_.handle(input, feedback) == ui::Event::changed)
        {
            bitrate_min_ = kBitrates[std::clamp(bitrate_.index(), 0,
                                                static_cast<int>(std::size(kBitrates)) - 1)];
            apply();
        }
        break;
    case Zone::quick:
    {
        const ui::Event event = quick_.handle(input, feedback);
        if (event == ui::Event::changed)
        {
            const std::vector<int> picked = quick_.selection();
            genre_value_ = picked.empty() ? "" : kQuick[picked.front()];
            genre_.set_index(index_of(session_.genres, genre_value_));
            genre_.set_placeholder(genre_value_.empty() ? "Any genre" : genre_value_);
            apply();
        }
        if (quick_.exit() == Direction::up)
            go(Zone::bitrate, feedback);
        else if (quick_.exit() == Direction::right)
            go(Zone::results, feedback);
        break;
    }
    case Zone::results:
        break;
    }
    return Result::none;
}

void SearchScreen::update(float dt)
{
    age_ += dt;
    Catalog &list = session_.browse;
    const bool reduced = session_.settings.reduced_motion;
    const int focus = results_.focus();
    list.prefetch(static_cast<unsigned>(std::max(focus - 8, 0)), static_cast<unsigned>(focus + 16));
    results_.set_count(list.known() ? static_cast<int>(list.total()) : 0);
    if (zone_ == Zone::results && list.known() && list.total() == 0)
        zone_ = panel_zone_;

    field_.set_active(zone_ == Zone::field);
    field_.set_busy(session_.status.searching);
    country_.set_active(zone_ == Zone::country);
    language_.set_active(zone_ == Zone::language);
    genre_.set_active(zone_ == Zone::genre);
    bitrate_.set_active(zone_ == Zone::bitrate);
    quick_.set_active(zone_ == Zone::quick);
    results_.set_active(zone_ == Zone::results);
    // One focus on screen: a grid that is not in use shows no ring at all.
    results_.style.card.ring = zone_ == Zone::results;
    results_.style.card.glow = zone_ == Zone::results;

    field_.style.reduced_motion = reduced;
    country_.style.reduced_motion = reduced;
    language_.style.reduced_motion = reduced;
    genre_.style.reduced_motion = reduced;
    bitrate_.style.reduced_motion = reduced;
    quick_.style.reduced_motion = reduced;
    results_.style.reduced_motion = reduced;
    empty_.style.reduced_motion = reduced;

    field_.update(dt);
    country_.update(dt);
    language_.update(dt);
    genre_.update(dt);
    bitrate_.update(dt);
    quick_.update(dt);
    results_.update(dt);
    rail_.track(session_.browse, session_.revision, dt);
    rail_.follow(results_.count() > 0
                     ? session_.browse.peek(static_cast<unsigned>(results_.focus()))
                     : nullptr);
    rail_.update(dt);
    empty_.update(dt);
}

std::string SearchScreen::summary() const
{
    std::string text;
    const auto add = [&](const std::string &part)
    {
        if (part.empty())
            return;
        if (!text.empty())
            text += "  \xC2\xB7  ";
        text += part;
    };
    add(text_);
    if (!country_code_.empty())
        add(country_.value());
    add(genre_value_);
    if (!language_value_.empty())
        add(language_.value());
    if (bitrate_min_ != 0U)
    {
        char rate[32];
        std::snprintf(rate, sizeof(rate), "%u kbps and up", bitrate_min_);
        add(rate);
    }
    add(text.empty() ? "everything, by popularity" : "by popularity");
    return text;
}

void SearchScreen::draw(ui::Canvas &canvas) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const ui::Theme &theme = session_.theme;
    const Catalog &catalog = session_.browse;
    ui::Painter paint(list, fonts, theme, canvas.glass);
    const float in = tween::stagger(age_, 0, 0.06f, 0.45f);
    const float slide = session_.settings.reduced_motion ? 0.0f : 24.0f * (1.0f - in);

    // ---- the panel ----
    list.push_opacity(in);
    list.push_transform(1.0f, 0.0f, 0.0f, -slide, 0.0f);
    draw_glass(canvas, theme, kPanel);
    paint.heading("Search and filter", kInner, 196.0f, 32.0f);
    field_.draw(canvas);
    country_.draw(canvas);
    language_.draw(canvas);
    genre_.draw(canvas);
    paint.label("Minimum bitrate", kInner, 536.0f, 20.0f, theme.text_muted);
    bitrate_.draw(canvas);
    quick_.draw(canvas);
    list.pop_transform();
    list.pop_opacity();

    // ---- the answer ----
    list.push_opacity(tween::stagger(age_, 1, 0.06f, 0.45f));
    if (!catalog.known())
    {
        ui::text(list, fonts.semibold, "Reading the catalogue", kResultsLeft, 166.0f, 27.0f,
                 theme.text);
        const float w = (gfx::kVirtualWidth - kMargin - kResultsLeft - 24.0f) / kResultColumns;
        for (int i = 0; i < kResultColumns * kResultRows; ++i)
            draw_tile_placeholder(
                canvas, session_,
                {kResultsLeft + static_cast<float>(i % kResultColumns) * (w + 24.0f),
                 kResultsTop + kPad +
                     static_cast<float>(i / kResultColumns) * (kResultHeight + 20.0f),
                 w, kResultHeight});
        list.pop_opacity();
        return;
    }
    const std::string count =
        group_digits(catalog.total()) + (catalog.total() == 1 ? " station" : " stations");
    const float count_width =
        ui::text(list, fonts.semibold, count, kResultsLeft, 166.0f, 27.0f, theme.text);
    const float room = gfx::kVirtualWidth - kMargin - kResultsLeft - count_width - 24.0f;
    ui::text(list, fonts.regular,
             fonts.regular.font->fit(session_.status.searching ? "asking Radio Browser for more"
                                                               : summary(),
                                     22.0f, room),
             kResultsLeft + count_width + 24.0f, 166.0f, 22.0f, theme.text_muted);
    if (catalog.total() == 0)
    {
        empty_.draw(canvas);
    }
    else
    {
        results_.draw(canvas);
        rail_.draw(canvas);
    }
    list.pop_opacity();
}

void SearchScreen::draw_popovers(ui::Canvas &canvas) const
{
    country_.draw_popover(canvas);
    language_.draw_popover(canvas);
    genre_.draw_popover(canvas);
}

int SearchScreen::hints(ui::Hint *out, int capacity) const
{
    int count = 0;
    const auto add = [&](ui::Hint hint)
    {
        if (count < capacity)
            out[count++] = hint;
    };
    if (modal())
    {
        add({ui::Button::cross, "Choose"});
        add({ui::Button::circle, "Close"});
        return count;
    }
    if (zone_ == Zone::results && rail_.focused())
    {
        add({ui::Button::dpad, "Letter"});
        add({ui::Button::cross, "Back to the results"});
        return count;
    }
    switch (zone_)
    {
    case Zone::field:
        add({ui::Button::cross, "Type"});
        break;
    case Zone::country:
    case Zone::language:
    case Zone::genre:
        add({ui::Button::cross, "Open list"});
        break;
    case Zone::bitrate:
        add({ui::Button::dpad, "Change"});
        break;
    case Zone::quick:
        add({ui::Button::cross, "Pick"});
        break;
    case Zone::results:
    {
        const radio_station_t *station = focused();
        add({ui::Button::cross,
             station != nullptr && session_.is_current(*station) ? "Now Playing" : "Play"});
        add({ui::Button::square, session_.browse.favorite(static_cast<unsigned>(results_.focus()))
                                     ? "Unfavorite"
                                     : "Favorite"});
        break;
    }
    }
    if (zone_ != Zone::results)
        add({ui::Button::square, "Reset"});
    add({ui::Button::circle, "Back"});
    return count;
}

} // namespace radio
