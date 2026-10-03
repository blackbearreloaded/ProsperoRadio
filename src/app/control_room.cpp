// ProsperoRadio - Settings, after the kit's "Control Room": a rail, live controls.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/control_room.hpp"

#include "app/draw.hpp"
#include "app/platform.hpp"
#include "app/theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace radio
{

namespace
{

constexpr float kPi = 3.14159265f;
constexpr Rect kPanel{560.0f, 150.0f, 1264.0f, 760.0f};
constexpr float kPad = 40.0f;
constexpr float kSaveDelay = 0.7f; // seconds after the last change

enum Row : int
{
    kVolume = 1,
    kBass,
    kTreble,
    kBalance,
    kMono,
    kResetSound,
    kReduce,
    kSounds,
    kQuiet,
    kStations,
    kStatus,
    kRefresh,
};

constexpr const char *kTitles[] = {"Sound", "Interface", "Catalogue", "About"};

std::string decibels(float value)
{
    char text[16];
    if (std::fabs(value) < 0.5f)
        return "0 dB";
    std::snprintf(text, sizeof(text), "%+.0f dB", static_cast<double>(value));
    return text;
}

std::string balance_text(float value)
{
    char text[16];
    if (std::fabs(value) < 0.5f)
        return "Centre";
    std::snprintf(text, sizeof(text), "%s %.0f", value < 0.0f ? "L" : "R",
                  static_cast<double>(std::fabs(value)));
    return text;
}

void draw_icon(gfx::DrawList &list, int category, float cx, float cy, Color ink)
{
    switch (category)
    {
    case 0: // a speaker and two waves
    {
        list.rounded_rect({cx - 15, cy - 5, 8, 10}, 2, ink);
        const float cone[] = {cx - 9, cy - 5, cx - 1, cy - 12, cx - 1, cy + 12, cx - 9, cy + 5};
        list.polygon(cone, 4, ink);
        list.arc(cx - 1, cy, 9, 2.5f, kPi * 0.22f, kPi * 0.56f, ink);
        list.arc(cx - 1, cy, 16, 2.5f, kPi * 0.22f, kPi * 0.56f, ink);
        break;
    }
    case 1: // three sliders
        for (int i = 0; i < 3; ++i)
        {
            const float y = cy - 9.0f + 9.0f * static_cast<float>(i);
            list.line(cx - 15, y, cx + 15, y, 2.2f, ink);
            list.circle(cx - 8.0f + 8.0f * static_cast<float>((i * 2) % 3), y, 4.0f, ink);
        }
        break;
    case 2: // a list
        for (int i = 0; i < 3; ++i)
        {
            const float y = cy - 9.0f + 9.0f * static_cast<float>(i);
            list.circle(cx - 13, y, 2.5f, ink);
            list.line(cx - 6, y, cx + 15, y, 2.5f, ink);
        }
        break;
    default: // "i" in a circle
        list.ring(cx, cy, 16, 2.5f, ink);
        list.circle(cx, cy - 7, 2.2f, ink);
        list.line(cx, cy - 1, cx, cy + 8, 3.0f, ink);
        break;
    }
}

} // namespace

ControlRoom::ControlRoom(Session &session, std::string version)
    : session_(session), version_(std::move(version))
{
    const ui::Theme &theme = session.theme;
    nav_.style.theme = theme;
    nav_.style.panel = false;
    nav_.style.expanded_width = 360.0f;
    nav_.style.row_height = 64.0f;
    nav_.style.text_size = 27.0f;
    nav_.icon = [](ui::Canvas &canvas, const Rect &box, const ui::NavEntry &entry, int, float,
                   Color ink) { draw_icon(canvas.list, entry.tag, box.cx(), box.cy(), ink); };
    nav_.set_entries({{"Sound", "", "", false, false, false, kSound},
                      {"Interface", "", "", false, false, false, kInterface},
                      {"Catalogue", "", "", false, false, false, kCatalogue},
                      {"About", "", "", false, false, false, kAbout}});
    nav_.set_bounds({kMargin, 270.0f, 0.0f, 560.0f});

    form_.style.theme = theme;
    form_.style.on_page = false;
    form_.style.description_inline = false;
    form_.style.control_width = 420.0f;
    form_.style.number_width = 124.0f;
    form_.style.row_height = 62.0f;
    form_.style.label_size = 27.0f;
    form_.set_bounds(
        {kPanel.x + kPad - 16.0f, kPanel.y + 104.0f, kPanel.w - 2.0f * kPad + 32.0f, 420.0f});

    for (ui::Meter *meter : {&left_, &right_})
    {
        meter->style.theme = theme;
        meter->style.segments = 40;
        meter->style.segment_gap = 4.0f;
        meter->style.height = 14.0f;
        meter->style.zone_strip = false;
        meter->style.warning_at = 0.7f;
        meter->style.danger_at = 0.92f;
        meter->style.show_value = false;
        meter->style.text_size = 21.0f;
    }
    left_.label = "Left";
    right_.label = "Right";
    show_category(kSound);
}

void ControlRoom::open(ui::Feedback &feedback)
{
    open_ = true;
    on_rail_ = false;
    age_ = 0.0f;
    nav_.set_focus(category_, true);
    nav_.set_current(category_, true);
    nav_.enter();
    show_category(category_);
    feedback.play(audio::Cue::open);
}

void ControlRoom::show_category(int category)
{
    category_ = category;
    const Settings &s = session_.settings;
    form_.clear();
    switch (category)
    {
    case kSound:
    {
        ui::FormRow &volume = form_.add_slider(kVolume, "Volume", s.volume * 100.0f, 0, 100, 5);
        volume.unit = " %";
        volume.description = "How loud the station plays, before the console's own volume.";
        ui::FormRow &bass = form_.add_slider(kBass, "Bass", s.bass_db, -12, 12, 1);
        bass.format = decibels;
        bass.description = "Low frequencies, below 150 Hz: more body, or less boom.";
        ui::FormRow &treble = form_.add_slider(kTreble, "Treble", s.treble_db, -12, 12, 1);
        treble.format = decibels;
        treble.description = "High frequencies, above 6 kHz: more air, or less hiss.";
        ui::FormRow &balance =
            form_.add_slider(kBalance, "Balance", s.balance * 100.0f, -100, 100, 10);
        balance.format = balance_text;
        balance.description = "Moves the sound toward the left or the right speaker.";
        form_.add_toggle(kMono, "Mono", s.mono).description =
            "Both speakers play the same sound: for one speaker, or one ear.";
        form_.add_action(kResetSound, "Restore the station's own sound").description =
            "Volume full, bass and treble flat, balance centred, stereo.";
        break;
    }
    case kInterface:
        form_.add_toggle(kReduce, "Reduce motion", s.reduced_motion).description =
            "Screens fade instead of sliding.";
        form_.add_toggle(kSounds, "Interface sounds", s.interface_sounds).description =
            "The small sounds of moving and choosing.";
        form_.add_toggle(kQuiet, "Quiet while a station plays", s.quiet_while_playing).description =
            "No interface sounds over the radio.";
        form_.set_disabled(kQuiet, !s.interface_sounds);
        break;
    case kCatalogue:
        form_.add_value(kStations, "Stations", group_digits(session_.status.catalog_size));
        form_.add_value(kStatus, "State", "");
        form_.add_action(kRefresh, "Update now").description =
            "Downloads Radio Browser's list again. Listening goes on meanwhile.";
        break;
    default:
        break;
    }
    form_.set_active(!on_rail_);
    form_.enter();
}

void ControlRoom::apply(int row)
{
    Settings &s = session_.settings;
    switch (row)
    {
    case kVolume:
        s.volume = form_.slider_value(kVolume) / 100.0f;
        break;
    case kBass:
        s.bass_db = form_.slider_value(kBass);
        break;
    case kTreble:
        s.treble_db = form_.slider_value(kTreble);
        break;
    case kBalance:
        s.balance = form_.slider_value(kBalance) / 100.0f;
        break;
    case kMono:
        s.mono = form_.toggle_value(kMono);
        break;
    case kReduce:
        s.reduced_motion = form_.toggle_value(kReduce);
        break;
    case kSounds:
        s.interface_sounds = form_.toggle_value(kSounds);
        form_.set_disabled(kQuiet, !s.interface_sounds);
        break;
    case kQuiet:
        s.quiet_while_playing = form_.toggle_value(kQuiet);
        break;
    default:
        return;
    }
    session_.apply_audio();
    dirty_ = true;
    since_change_ = 0.0f;
}

void ControlRoom::reset_sound()
{
    Settings &s = session_.settings;
    s.volume = 1.0f;
    s.bass_db = 0.0f;
    s.treble_db = 0.0f;
    s.balance = 0.0f;
    s.mono = false;
    // The thumbs travel back on their own: the form animates values set from code.
    form_.set_slider(kVolume, 100.0f);
    form_.set_slider(kBass, 0.0f);
    form_.set_slider(kTreble, 0.0f);
    form_.set_slider(kBalance, 0.0f);
    form_.set_toggle(kMono, false);
    session_.apply_audio();
    dirty_ = true;
    since_change_ = 0.0f;
}

ControlRoom::Result ControlRoom::handle(const InputFrame &input, ui::Feedback &feedback)
{
    if (!open_)
        return Result::none;
    // L2 and R2 change the category from anywhere.
    const int turn = input.is_pressed(Action::jump_next)   ? 1
                     : input.is_pressed(Action::jump_prev) ? -1
                                                           : 0;
    if (turn != 0)
    {
        const int next = std::clamp(category_ + turn, 0, static_cast<int>(kAbout));
        if (next == category_)
        {
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
        }
        else
        {
            feedback.play(audio::Cue::tab, turn > 0 ? 0.96f : 1.04f);
            nav_.set_focus(next, false);
            nav_.set_current(next);
            show_category(next);
        }
        return Result::none;
    }

    if (on_rail_)
    {
        const ui::Event event = nav_.handle(input, feedback);
        if (event == ui::Event::moved)
        {
            nav_.set_current(nav_.focus());
            show_category(nav_.focus());
        }
        else if (event == ui::Event::cancelled)
        {
            open_ = false;
            return Result::close;
        }
        if ((input.is_pressed(Action::right) || event == ui::Event::activated) &&
            category_ != kAbout)
        {
            on_rail_ = false;
            nav_.set_focused(false);
            form_.set_active(true);
            feedback.play(audio::Cue::focus);
        }
        return Result::none;
    }

    if (category_ == kAbout)
    {
        if (input.is_pressed(Action::back) || input.is_pressed(Action::left))
        {
            on_rail_ = true;
            nav_.set_focused(true);
            feedback.play(audio::Cue::back);
        }
        return Result::none;
    }

    const ui::Event event = form_.handle(input, feedback);
    switch (event)
    {
    case ui::Event::changed:
        apply(form_.changed_id());
        break;
    case ui::Event::activated:
        if (form_.changed_id() == kResetSound)
            reset_sound();
        else if (form_.changed_id() == kRefresh)
            return Result::refresh;
        break;
    case ui::Event::cancelled:
        on_rail_ = true;
        form_.set_active(false);
        nav_.set_focused(true);
        break;
    case ui::Event::none:
        // Left on a row that has no use for it goes to the rail.
        if (input.is_pressed(Action::left) && !form_.uses_horizontal())
        {
            on_rail_ = true;
            form_.set_active(false);
            nav_.set_focused(true);
            feedback.play(audio::Cue::focus);
        }
        break;
    default:
        break;
    }
    return Result::none;
}

void ControlRoom::update(float dt)
{
    age_ += dt;
    amount_.target = open_ ? 1.0f : 0.0f;
    amount_.update(dt, session_.settings.reduced_motion ? 40.0f : 10.0f);
    nav_.style.reduced_motion = session_.settings.reduced_motion;
    form_.style.reduced_motion = session_.settings.reduced_motion;
    nav_.set_focused(open_ && on_rail_);
    nav_.update(dt);
    form_.update(dt);

    if (category_ == kCatalogue)
    {
        const radio_service_status_t &status = session_.status;
        form_.set_value_text(kStations, group_digits(status.catalog_size));
        form_.set_value_text(kStatus, status.refreshing                             ? "Updating"
                                      : status.catalog_state == RADIO_CATALOG_ERROR ? "Offline copy"
                                      : status.catalog_size == 0U                   ? "Empty"
                                                                                    : "Up to date");
    }

    float left = 0.0f;
    float right = 0.0f;
    radio_audio_meter(&left, &right);
    left_.set_value(left);
    right_.set_value(right);
    left_.update(dt);
    right_.update(dt);

    // Saved a moment after the last change, and when the screen closes.
    since_change_ += dt;
    saved_ = std::max(0.0f, saved_ - dt);
    if (dirty_ && (since_change_ > kSaveDelay || !open_))
    {
        session_.save_settings();
        dirty_ = false;
        saved_ = 2.0f;
    }
}

void ControlRoom::draw_meters(ui::Canvas &canvas, const Rect &box) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const ui::Theme &theme = session_.theme;
    list.rounded_rect(box, 18.0f, kWhite.with_alpha(0.04f));
    list.bordered_rect(box, 18.0f, kClear, 1.5f, kWhite.with_alpha(0.08f));
    ui::text(list, fonts.semibold, "OUTPUT LEVEL", box.x + 28.0f, box.y + 40.0f, 16.0f,
             theme.text_muted, gfx::Align::left, 3.0f);
    const char *labels[] = {"Left", "Right"};
    const ui::Meter *meters[] = {&left_, &right_};
    for (int i = 0; i < 2; ++i)
    {
        const float y = box.y + 70.0f + 40.0f * static_cast<float>(i);
        ui::text(list, fonts.regular, labels[i], box.x + 28.0f, baseline_for(y, 22.0f), 22.0f,
                 theme.text);
        // The meter draws its own label; ours sits in a column of its own.
        ui::Meter meter = *meters[i];
        meter.label.clear();
        meter.set_bounds({box.x + 150.0f, y - 10.0f, box.w - 320.0f, 20.0f});
        meter.draw(canvas);
        const float level = meters[i]->shown();
        char text[16];
        if (level < 0.001f)
            std::snprintf(text, sizeof(text), "-inf dB");
        else
            std::snprintf(text, sizeof(text), "%.0f dB",
                          static_cast<double>(20.0f * std::log10(level)));
        ui::text(list, fonts.mono, text, box.x + box.w - 28.0f, baseline_for(y, 21.0f), 21.0f,
                 theme.text_muted, gfx::Align::right);
    }
}

void ControlRoom::draw_about(ui::Canvas &canvas, const Rect &box) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const ui::Theme &theme = session_.theme;
    float y = box.y + 30.0f;
    draw_logo(list, box.x + 34.0f, y + 6.0f, 52.0f, tone::teal);
    ui::text(list, fonts.display, "ProsperoRadio", box.x + 84.0f, y + 20.0f, 40.0f, theme.text);
    ui::text(list, fonts.mono, "Version " + version_, box.x + 84.0f, y + 58.0f, 22.0f,
             theme.text_muted);
    y += 118.0f;
    ui::text(list, fonts.regular, "Brought to you by", box.x, y, 25.0f, theme.text_muted);
    ui::text(list, fonts.semibold, "BlackBearReloaded",
             box.x + fonts.regular.measure("Brought to you by ", 25.0f), y, 25.0f, tone::teal);
    y += 64.0f;
    const char *lines[] = {
        "Station catalogue, metadata, search and discovery: Radio Browser,",
        "a free and open community project. Thank you to its maintainers.",
        "www.radio-browser.info",
        "",
        "Your favourites, settings and the saved catalogue are kept in",
        "/data/prosperoradio on this console.",
    };
    for (const char *line : lines)
    {
        ui::text(list, fonts.regular, line, box.x, y, 25.0f, theme.text_muted);
        y += 40.0f;
    }
}

void ControlRoom::draw(ui::Canvas &canvas) const
{
    const float shown = amount_.value;
    if (shown <= 0.01f)
        return;
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const ui::Theme &theme = session_.theme;
    const bool reduced = session_.settings.reduced_motion;
    list.push_opacity(tween::smoothstep(shown));
    list.push_transform(reduced ? 1.0f : 0.98f + 0.02f * shown, gfx::kVirtualWidth * 0.5f,
                        gfx::kVirtualHeight * 0.5f, 0.0f, 0.0f);

    // ---- the page's title ----
    ui::text(list, fonts.semibold, "CONTROL ROOM", kMargin, 132.0f, 18.0f, tone::teal,
             gfx::Align::left, 4.0f);
    ui::text(list, fonts.display, "Settings", kMargin - 3.0f, 214.0f, 76.0f, theme.text);
    nav_.draw(canvas);
    ui::text(list, fonts.regular, "Changes apply at once and are kept for you.", kMargin, 880.0f,
             21.0f, theme.text_muted);

    // ---- the panel ----
    draw_glass(canvas, theme, kPanel, 32.0f);
    ui::text(list, fonts.semibold, kTitles[category_], kPanel.x + kPad, kPanel.y + 70.0f, 34.0f,
             theme.text);
    if (saved_ > 0.0f)
    {
        // "Saved", in the panel's corner, fading out.
        const float alpha = std::min(1.0f, saved_);
        list.push_opacity(alpha);
        const Rect chip{kPanel.x + kPanel.w - kPad - 128.0f, kPanel.y + 38.0f, 128.0f, 44.0f};
        list.rounded_rect(chip, 22.0f, tone::teal.with_alpha(0.16f));
        list.bordered_rect(chip, 22.0f, kClear, 1.5f, tone::teal.with_alpha(0.5f));
        list.circle(chip.x + 26.0f, chip.cy(), 10.0f, tone::teal);
        list.line(chip.x + 21.0f, chip.cy(), chip.x + 25.0f, chip.cy() + 4.0f, 2.5f, tone::ink);
        list.line(chip.x + 25.0f, chip.cy() + 4.0f, chip.x + 31.0f, chip.cy() - 4.0f, 2.5f,
                  tone::ink);
        ui::text(list, fonts.semibold, "Saved", chip.x + 46.0f, baseline_for(chip.cy(), 22.0f),
                 22.0f, theme.text);
        list.pop_opacity();
    }

    const Rect content{kPanel.x + kPad, kPanel.y + 108.0f, kPanel.w - 2.0f * kPad, 520.0f};
    if (category_ == kAbout)
        draw_about(canvas, content);
    else
        form_.draw(canvas);
    if (category_ == kSound)
        draw_meters(canvas, {content.x, kPanel.y + 540.0f, content.w, 136.0f});

    // ---- what the focused control does ----
    const std::string &help = category_ == kAbout || on_rail_ ? std::string() : form_.help_text();
    if (!help.empty())
    {
        const float y = kPanel.y + kPanel.h - 34.0f;
        list.rounded_rect({kPanel.x + kPad, y - 30.0f, kPanel.w - 2.0f * kPad, 1.5f}, 0.0f,
                          kWhite.with_alpha(0.08f));
        list.ring(kPanel.x + kPad + 12.0f, y, 11.0f, 2.0f, theme.text_muted);
        list.circle(kPanel.x + kPad + 12.0f, y - 4.0f, 1.8f, theme.text_muted);
        list.line(kPanel.x + kPad + 12.0f, y - 0.5f, kPanel.x + kPad + 12.0f, y + 5.0f, 2.2f,
                  theme.text_muted);
        ui::text(list, fonts.regular, help, kPanel.x + kPad + 36.0f, baseline_for(y, 22.0f), 22.0f,
                 theme.text_muted);
    }

    list.pop_transform();
    list.pop_opacity();
}

int ControlRoom::hints(ui::Hint *out, int capacity) const
{
    int count = 0;
    const auto add = [&](ui::Hint hint)
    {
        if (count < capacity)
            out[count++] = hint;
    };
    if (on_rail_)
    {
        if (category_ != kAbout)
            add({ui::Button::cross, "Open"});
        add({ui::Button::circle, "Close"});
    }
    else
    {
        add({ui::Button::dpad, "Adjust"});
        add({ui::Button::circle, "Back"});
    }
    add({ui::Button::l2, "Category", ui::Button::r2});
    return count;
}

} // namespace radio
