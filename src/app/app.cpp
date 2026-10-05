// ProsperoRadio - The interface: tabs, the screens, and everything that floats.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/app.hpp"

#include "app/draw.hpp"
#include "app/station_art.hpp"
#include "app/theme.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace radio
{

namespace
{

constexpr Rect kBar{kMargin, 928.0f, gfx::kVirtualWidth - 2.0f * kMargin, 84.0f};
constexpr float kHeaderY = 76.0f; // the centre line of the top row

enum MenuTag : int
{
    kMenuPlayer,
    kMenuRefresh,
    kMenuSettings,
    kMenuAbout,
    kMenuClose,
};

constexpr const char *kCredits =
    "ProsperoRadio's station catalogue, metadata, search and discovery are powered by the "
    "free and open Radio Browser community.\n"
    "Thank you to its maintainers and contributors. ProsperoRadio would not be possible "
    "without their work.\n"
    "www.radio-browser.info";

} // namespace

App::App(const ui::Fonts &fonts, std::uint32_t glass_texture, std::string version)
    : session_(fonts), home_(session_), letters_(session_), search_(session_), player_(session_),
      room_(session_, version), update_(session_), glass_texture_(glass_texture),
      version_(std::move(version))
{
    const ui::Theme &theme = session_.theme;

    tabs_.style.theme = theme;
    tabs_.style.kind = ui::TabKind::underline;
    tabs_.style.text_size = 26.0f;
    tabs_.style.padding = 14.0f;
    tabs_.style.gap = 16.0f;
    tabs_.style.track = false;
    tabs_.style.focus_ring = false;
    tabs_.style.on_page = true;
    tabs_.set_tabs({{"Popular"}, {"Trending"}, {"Top rated"}, {"Favorites"}, {"Discover"}});
    tabs_.set_bounds({486.0f, kHeaderY - 28.0f, 760.0f, 56.0f});
    tabs_.set_focused(false);
    radio_update_check_start(version_.c_str());
    tabs_.set_active(0, true);

    menu_.style.theme = theme;
    menu_.style.width = 440.0f;
    menu_.style.row_height = 64.0f;
    menu_.style.text_size = 26.0f;
    menu_.style.scrim = 0.35f;
    menu_.set_bounds({kMargin, 56.0f, gfx::kVirtualWidth - 2.0f * kMargin, 900.0f});

    about_.style.theme = theme;
    about_.style.width = 1000.0f;
    about_.style.body_lines = 9;
    closing_.style.theme = theme;

    offline_.style.theme = theme;
    offline_.style.kind = ui::StatusKind::warning;
    offline_.style.dismissable = false;
    offline_.style.min_height = 40.0f;
    offline_.style.padding = 8.0f;
    offline_.style.icon_size = 24.0f;
    offline_.style.title_size = 21.0f;
    offline_.style.radius = 14.0f;
    offline_.title =
        "Radio Browser cannot be reached. Showing the catalogue saved on this console.";
    // Between the grid and the bar, where only the next row peeks in: the
    // notice costs the screen nothing.
    offline_.set_bounds({kMargin, kBar.y - 46.0f, gfx::kVirtualWidth - 2.0f * kMargin, 0.0f});

    loader_.style.theme = theme;
    loader_.style.layout = ui::LoadingLayout::center;
    loader_.style.percent = false;
    loader_.set_stages({{"Downloading the station list", 1.0f}});
    loader_.style.title_size = 58.0f;
    loader_.style.bar_width = 900.0f;
    loader_.style.tip_label.clear();
    loader_.style.tips_by_hand = false;
    loader_.title = "Building your station catalogue";
    loader_.set_tips({"This happens once. Later launches open straight from the copy saved on "
                      "this console.",
                      "Keep the console online until it finishes."});

    lean_.snap(tone::teal);
    lean_dark_.snap(Color::rgb(0x263c8a));
    tab_changed();
    // How the listener left the app last time, the sound included.
    session_.load_settings();
}

void App::set_time(int hour, int minute)
{
    session_.hour = hour;
    session_.minute = minute;
}

void App::show_tab(int index, bool glide)
{
    if (index == tabs_.active())
        return;
    tabs_.set_active(index, !glide);
    tab_changed();
}

void App::tab_changed()
{
    if (discovering())
    {
        session_.browse.set_spec(search_.spec());
        search_.enter();
        return;
    }
    ListSpec spec;
    spec.view = static_cast<View>(tabs_.active());
    session_.browse.set_spec(spec);
    if (lettered())
        letters_.reset();
    else
        home_.reset();
}

void App::open_player(ui::Feedback &feedback)
{
    if (!session_.has_playing || player_open_)
        return;
    player_open_ = true;
    player_.enter();
    feedback.play(audio::Cue::open);
}

void App::open_menu(ui::Feedback &feedback)
{
    std::vector<ui::MenuItem> items;
    const auto add = [&](const char *label, int tag)
    {
        ui::MenuItem item;
        item.label = label;
        item.tag = tag;
        items.push_back(std::move(item));
    };
    if (session_.has_playing && !player_open_)
        add("Now Playing", kMenuPlayer);
    add("Refresh catalogue", kMenuRefresh);
    add("Settings", kMenuSettings);
    add("About and credits", kMenuAbout);
    ui::MenuItem line;
    line.separator = true;
    items.push_back(line);
    add("Close ProsperoRadio", kMenuClose);
    items.back().danger = true;
    menu_.set_items(std::move(items));
    menu_.open({gfx::kVirtualWidth - kMargin - 160.0f, kHeaderY - 20.0f, 160.0f, 40.0f}, feedback);
}

void App::refresh(ui::Feedback &feedback)
{
    if (session_.status.refreshing)
    {
        session_.toasts.push(ui::StatusKind::info, "The catalogue is already being updated");
        return;
    }
    if (radio_service_refresh())
    {
        session_.toasts.push(ui::StatusKind::info, "Updating the catalogue",
                             "You can keep listening while it runs.");
    }
    else
    {
        feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
        session_.toasts.push(ui::StatusKind::warning, "The update could not start",
                             "Try again in a moment.");
    }
}

void App::run_menu(int tag, ui::Feedback &feedback)
{
    switch (tag)
    {
    case kMenuPlayer:
        open_player(feedback);
        break;
    case kMenuRefresh:
        refresh(feedback);
        break;
    case kMenuSettings:
        room_.open(feedback);
        break;
    case kMenuAbout:
    {
        ui::DialogContent content;
        content.icon = ui::StatusKind::info;
        content.title = "Powered by Radio Browser";
        content.body = std::string(kCredits) + "\nProsperoRadio " + version_ +
                       ", brought to you by BlackBearReloaded.";
        content.buttons = {{"Back to ProsperoRadio", ui::ButtonKind::primary}};
        about_.open(std::move(content), feedback);
        break;
    }
    default:
    {
        ui::DialogContent content;
        content.icon = ui::StatusKind::question;
        content.title = "Close ProsperoRadio?";
        content.body = session_.active() ? "The station stops playing." : "";
        content.buttons = {{"Stay"}, {"Close", ui::ButtonKind::primary, true}};
        closing_.open(std::move(content), feedback);
        break;
    }
    }
}

void App::handle_browse(const InputFrame &input, ui::Feedback &feedback)
{
    const bool modal = discovering() && search_.modal();
    if (!modal)
    {
        if (input.is_pressed(Action::menu))
        {
            open_menu(feedback);
            return;
        }
        if (input.is_pressed(Action::touch) && session_.has_playing)
        {
            open_player(feedback);
            return;
        }
        const int turn = input.is_pressed(Action::page_next)   ? 1
                         : input.is_pressed(Action::page_prev) ? -1
                                                               : 0;
        if (turn != 0)
        {
            if (tabs_.step(turn, input, feedback) == ui::Event::changed)
                tab_changed();
            return;
        }
        if (input.is_pressed(Action::north) && !discovering())
        {
            feedback.play(audio::Cue::tab);
            show_tab(static_cast<int>(View::discover), true);
            return;
        }
        // Back always leads to the first tab, then has nowhere further to go.
        if (input.is_pressed(Action::back) && tabs_.active() != 0)
        {
            feedback.play(audio::Cue::back);
            show_tab(0, true);
            return;
        }
    }
    if (discovering())
    {
        if (search_.handle(input, feedback) == SearchScreen::Result::open_player)
            open_player(feedback);
        return;
    }
    if (lettered())
    {
        switch (letters_.handle(input, feedback))
        {
        case LetterScreen::Result::open_player:
            open_player(feedback);
            break;
        case LetterScreen::Result::browse_popular:
            show_tab(0, true);
            break;
        default:
            break;
        }
        return;
    }
    switch (home_.handle(input, feedback))
    {
    case HomeScreen::Result::open_player:
        open_player(feedback);
        break;
    case HomeScreen::Result::browse_popular:
        show_tab(0, true);
        break;
    case HomeScreen::Result::try_again:
        refresh(feedback);
        break;
    default:
        break;
    }
}

void App::follow_station(float dt)
{
    const radio_station_t *station = nullptr;
    if (player_open_ && session_.has_playing)
        station = &session_.playing;
    else if (discovering())
        station = search_.focused();
    else
        station = home_.focused();
    if (station != nullptr)
    {
        const ArtColors colors = art_colors(station->uuid);
        lean_.target(colors.accent);
        lean_dark_.target(colors.top);
    }
    lean_amount_.target = station == nullptr ? 0.0f : (player_open_ ? 0.85f : 0.55f);
    lean_.update(dt, 3.5f);
    lean_dark_.update(dt, 3.5f);
    lean_amount_.update(dt, 3.5f);
    if (!session_.settings.reduced_motion)
        drift_ += dt;
}

void App::update(const InputFrame &input, float dt, ui::Feedback &feedback)
{
    session_.poll(dt);
    const radio_service_status_t &status = session_.status;
    const bool reduced = session_.settings.reduced_motion;
    if (session_.revision != seen_revision_)
    {
        seen_revision_ = session_.revision;
        search_.facets_changed();
    }

    // ---- states that announce themselves ----
    // Nothing to browse yet: the first sync, or the saved catalogue being read.
    const bool building = status.catalog_size == 0U &&
                          (status.catalog_state == RADIO_CATALOG_LOADING || status.refreshing);
    if (building && !loader_.is_open())
        loader_.show(feedback);
    else if (!building && loader_.is_open())
        loader_.hide();
    if (loader_.is_open())
    {
        loader_.subtitle = status.sync_station_count != 0U
                               ? group_digits(status.sync_station_count) + " stations so far"
                               : "Contacting Radio Browser";
        loader_.set_progress(-1.0f);
    }
    const bool offline = status.catalog_state == RADIO_CATALOG_ERROR && status.catalog_size != 0U &&
                         !discovering() && !player_open_;
    if (offline && !offline_.is_shown())
        offline_.show(feedback);
    else if (!offline && offline_.is_shown())
        offline_.set_shown(false);

    // ---- input goes to whatever is on top ----
    if (loader_.is_open())
    {
        loader_.handle(input, feedback);
    }
    else if (update_.is_open())
    {
        update_.handle(input, feedback);
    }
    else if (closing_.is_open())
    {
        if (closing_.handle(input, feedback) == ui::Event::activated && closing_.choice() == 1)
        {
            session_.stop();
            quit_ = true;
        }
    }
    else if (about_.is_open())
    {
        about_.handle(input, feedback);
    }
    else if (room_.is_open())
    {
        if (room_.handle(input, feedback) == ControlRoom::Result::refresh)
            refresh(feedback);
    }
    else if (menu_.is_open())
    {
        if (menu_.handle(input, feedback) == ui::Event::activated)
            run_menu(menu_.items()[static_cast<std::size_t>(menu_.focus())].tag, feedback);
    }
    else if (player_open_)
    {
        if (input.is_pressed(Action::menu))
            open_menu(feedback);
        else if (player_.handle(input, feedback) == NowPlayingScreen::Result::close)
        {
            player_open_ = false;
            // Back on the main window, the hero keeps showing what plays.
            home_.show_player();
        }
    }
    else
    {
        handle_browse(input, feedback);
    }

    // ---- everything moves every frame ----
    const auto restyle = [&](ui::ComponentStyle &style) { style.reduced_motion = reduced; };
    restyle(tabs_.style);
    restyle(menu_.style);
    restyle(about_.style);
    restyle(closing_.style);
    restyle(offline_.style);
    restyle(loader_.style);
    restyle(session_.toasts.style);

    player_amount_.target = player_open_ ? 1.0f : 0.0f;
    player_amount_.update(dt, reduced ? 40.0f : 11.0f);
    follow_station(dt);

    tabs_.update(dt);
    if (discovering())
        search_.update(dt);
    else if (lettered())
        letters_.update(dt);
    else
        home_.update(dt);
    player_.update(dt);
    menu_.update(dt);
    room_.update(dt);
    about_.update(dt);
    closing_.update(dt);
    offline_.update(dt);
    loader_.update(dt);
    // A newer release: said once, when there is something to look at behind it.
    radio_update_t newer;
    if (!loader_.is_open() && !closing_.is_open() && radio_update_take(&newer))
    {
        // The app installs it itself when it can; otherwise it only says so.
        if (newer.installable)
            update_.offer(newer, feedback);
        else
            session_.toasts.push(ui::StatusKind::info, "Update available",
                                 "Version " + newer.version + " is on homebrew.page",
                                 kUpdateNoticeSeconds);
    }
    update_.update(dt, feedback);
    if (update_.wants_quit() && !quit_)
    {
        session_.stop();
        quit_ = true;
    }
    session_.toasts.update(dt, feedback);

    // The radio is what the player came to hear.
    if (!session_.settings.interface_sounds ||
        (session_.settings.quiet_while_playing && session_.active()))
        feedback.cues.clear();
}

void App::draw_status(ui::Canvas &canvas) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const ui::Theme &theme = session_.theme;
    const radio_service_status_t &status = session_.status;
    float right = gfx::kVirtualWidth - kMargin;

    if (session_.hour >= 0)
    {
        char time[8];
        std::snprintf(time, sizeof(time), "%02d:%02d", session_.hour, session_.minute);
        right -= ui::text(list, fonts.mono, time, right, baseline_for(kHeaderY, 24.0f), 24.0f,
                          theme.text, gfx::Align::right) +
                 22.0f;
    }

    std::string text;
    Color dot = theme.success;
    if (status.refreshing)
    {
        text = status.searching ? "Searching" : "Updating";
        if (status.sync_station_count != 0U)
            text += "  \xC2\xB7  " + group_digits(status.sync_station_count) + " found";
        dot = theme.warning;
    }
    else if (status.catalog_state == RADIO_CATALOG_ERROR)
    {
        text = status.catalog_size != 0U ? "Offline copy" : "No catalogue";
        dot = theme.danger;
    }
    else if (status.catalog_state == RADIO_CATALOG_LOADING)
    {
        text = "Loading";
        dot = theme.warning;
    }
    else
    {
        text = group_digits(status.catalog_size) + " stations";
    }
    const float width = fonts.semibold.measure(text, 19.0f) + 54.0f;
    const Rect chip{right - width, kHeaderY - 20.0f, width, 40.0f};
    list.rounded_rect(chip, 20.0f, kWhite.with_alpha(0.09f));
    list.bordered_rect(chip, 20.0f, kClear, 1.5f, kWhite.with_alpha(0.16f));
    list.circle(chip.x + 21.0f, kHeaderY, 5.0f, dot);
    ui::text(list, fonts.semibold, text, chip.x + 36.0f, baseline_for(kHeaderY, 19.0f), 19.0f,
             theme.text);
}

void App::draw_header(ui::Canvas &canvas) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    draw_logo(list, kMargin + 22.0f, kHeaderY, 44.0f, tone::teal);
    ui::text(list, fonts.display, "ProsperoRadio", kMargin + 58.0f, baseline_for(kHeaderY, 30.0f),
             30.0f, session_.theme.text);

    tabs_.draw(canvas);
    const ui::GlyphStyle glyphs = ui::GlyphStyle::dark();
    const Rect first = tabs_.tab_rect(fonts, 0);
    const Rect last = tabs_.tab_rect(fonts, static_cast<int>(View::discover));
    const float l1 = ui::button_width(ui::Button::l1, 34.0f);
    ui::draw_button(list, fonts, glyphs, ui::Button::l1, first.x - l1 - 20.0f, kHeaderY, 34.0f);
    ui::draw_button(list, fonts, glyphs, ui::Button::r1, last.x + last.w + 20.0f, kHeaderY, 34.0f);
    draw_status(canvas);
}

// The station on air, small, along the bottom of every browsing screen.
// Returns the width it used.
float App::draw_mini_player(ui::Canvas &canvas, const Rect &bar) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const ui::Theme &theme = session_.theme;
    const float cy = bar.cy();
    const Rect art{bar.x + 14.0f, cy - 28.0f, 56.0f, 56.0f};
    if (!session_.has_playing)
    {
        list.rounded_rect(art, 12.0f, kWhite.with_alpha(0.08f));
        draw_logo(list, art.cx(), art.cy(), 30.0f, kWhite.with_alpha(0.4f));
        return 90.0f + ui::text(list, fonts.regular, "Nothing playing", art.x + 74.0f,
                                baseline_for(cy, 23.0f), 23.0f, theme.text_muted);
    }

    const radio_station_t &station = session_.playing;
    draw_station_art(list, fonts, art, 12.0f, station);
    const char *state = "STOPPED";
    Color tint = theme.text_muted;
    switch (session_.status.playback_state)
    {
    case RADIO_PLAYBACK_PLAYING:
        state = "NOW PLAYING";
        tint = tone::teal;
        break;
    case RADIO_PLAYBACK_CONNECTING:
    case RADIO_PLAYBACK_BUFFERING:
        state = "TUNING IN";
        tint = theme.warning;
        break;
    case RADIO_PLAYBACK_ERROR:
        state = "NO SIGNAL";
        tint = theme.danger;
        break;
    default:
        break;
    }
    const float x = art.x + 74.0f;
    ui::text(list, fonts.semibold, state, x, cy - 10.0f, 14.0f, tint, gfx::Align::left, 3.0f);
    float at =
        x + ui::text(list, fonts.semibold, fonts.semibold.font->fit(station.name, 23.0f, 380.0f), x,
                     cy + 20.0f, 23.0f, theme.text);
    if (session_.on_air())
    {
        draw_equalizer(list, session_, at + 18.0f, cy + 20.0f, 20.0f, tone::live);
        at += 18.0f + 30.0f;
    }
    // How to get to the full screen from anywhere.
    at += 26.0f;
    ui::draw_button(list, fonts, ui::GlyphStyle::dark(), ui::Button::touchpad, at, cy, 32.0f);
    at += ui::button_width(ui::Button::touchpad, 32.0f) + 12.0f;
    at += ui::text(list, fonts.regular, "Player", at, baseline_for(cy, 22.0f), 22.0f,
                   theme.text_muted);
    return at - bar.x;
}

void App::draw_bottom_bar(ui::Canvas &canvas) const
{
    draw_glass(canvas, session_.theme, kBar, 26.0f);
    draw_mini_player(canvas, kBar);

    ui::Hint hints[6];
    const int count = discovering() ? search_.hints(hints, 6)
                      : lettered()  ? letters_.hints(hints, 6)
                                    : home_.hints(hints, 6);
    ui::HintLayout layout;
    layout.size = 36.0f;
    layout.text_size = 24.0f;
    layout.cy = kBar.cy();
    layout.item_gap = 36.0f;
    ui::draw_hints(canvas.list, canvas.fonts, ui::GlyphStyle::dark(), hints, count,
                   kBar.x + kBar.w - 30.0f, true, layout);
}

void App::draw(Frame &frame) const
{
    frame.reset();
    frame.glass_texture = glass_texture_;
    frame.backdrop =
        night_sky(lean_.value(), lean_dark_.value(), lean_amount_.value, drift_ * 0.6f);

    gfx::DrawList &list = frame.overlay;
    ui::Canvas canvas{list, session_.fonts, glass_texture_, session_.clock};
    const float p = tween::clamp01(player_amount_.value);
    const bool reduced = session_.settings.reduced_motion;
    // Settings take the whole screen: what was there steps back.
    const float room = tween::smoothstep(room_.shown());
    list.push_opacity(1.0f - room);

    // The browsing screens sink back while Now Playing comes forward.
    if (p < 0.99f)
    {
        list.push_opacity(1.0f - tween::smoothstep(p * 1.7f));
        list.push_transform(reduced ? 1.0f : 1.0f - 0.03f * p, gfx::kVirtualWidth * 0.5f,
                            gfx::kVirtualHeight * 0.5f, 0.0f, 0.0f);
        draw_header(canvas);
        if (discovering())
            search_.draw(canvas);
        else if (lettered())
            letters_.draw(canvas);
        else
            home_.draw(canvas);
        offline_.draw(canvas);
        draw_bottom_bar(canvas);
        if (discovering())
            search_.draw_popovers(canvas);
        list.pop_transform();
        list.pop_opacity();
    }
    if (p > 0.01f)
    {
        list.push_opacity(tween::smoothstep((p - 0.2f) / 0.8f));
        list.push_transform(reduced ? 1.0f : 0.97f + 0.03f * p, gfx::kVirtualWidth * 0.5f,
                            gfx::kVirtualHeight * 0.5f, 0.0f, 0.0f);
        player_.draw(canvas);
        ui::Hint hints[6];
        const int count = player_.hints(hints, 6);
        ui::draw_hints(list, session_.fonts, ui::GlyphStyle::dark(), hints, count,
                       gfx::kVirtualWidth - kMargin, true);
        list.pop_transform();
        list.pop_opacity();
    }
    list.pop_opacity();

    if (room > 0.01f)
    {
        room_.draw(canvas);
        ui::Hint hints[4];
        const int count = room_.hints(hints, 4);
        list.push_opacity(room);
        ui::draw_hints(list, session_.fonts, ui::GlyphStyle::dark(), hints, count,
                       gfx::kVirtualWidth - kMargin, true);
        list.pop_opacity();
    }

    session_.toasts.draw(canvas);
    menu_.draw(canvas);
    about_.draw(canvas);
    closing_.draw(canvas);
    update_.draw(canvas);
    loader_.draw(canvas);
}

} // namespace radio
