// ProsperoRadio - The update dialog: a newer release, its download, and the close that ends it.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/update_dialog.hpp"

#include "app/draw.hpp"
#include "app/session.hpp"
#include "app/theme.hpp"
#include "ui/glyphs.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <utility>

namespace radio
{

namespace
{

constexpr Rect kPanel{560.0f, 196.0f, 800.0f, 688.0f};
constexpr float kWorkingHeight = 604.0f;
constexpr float kCenterX = 960.0f;
constexpr float kRingY = 384.0f;
constexpr float kRingRadius = 96.0f;
constexpr float kRingWidth = 10.0f;
constexpr float kButtonsTop = 704.0f;
constexpr float kButtonWidth = 340.0f;
constexpr float kButtonHeight = 76.0f;
constexpr float kButtonLeft = 612.0f;
constexpr float kButtonGap = 356.0f;
constexpr float kClosingSeconds = 3.0f;
constexpr float kPi = 3.14159265f;
constexpr float kTop = -kPi * 0.5f; // twelve o'clock
const Color kPale = Color::rgb(0xc9f4f3);
const Color kAmber = Color::rgb(0xf5a524);

std::string megabytes(std::uint64_t bytes)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

// A stroke of the ring, measured on its centre line like the ring behind it.
void arc(gfx::DrawList &list, float start, float sweep, Color color)
{
    if (sweep > 0.001f)
        list.arc(kCenterX, kRingY, kRingRadius + kRingWidth * 0.5f, kRingWidth, start, sweep,
                 color);
}

} // namespace

UpdateDialog::UpdateDialog(Session &session) : session_(session)
{
    height_.snap(kPanel.h);
}

float UpdateDialog::motion() const
{
    return session_.settings.reduced_motion ? 0.0f : 1.0f;
}

void UpdateDialog::offer(const radio_update_t &update, ui::Feedback &feedback)
{
    update_ = update;
    progress_ = {};
    stage_ = Stage::offer;
    stage_time_ = 0.0f;
    choice_ = 0;
    choice_x_.snap(0.0f);
    height_.snap(kPanel.h);
    fraction_.snap(0.0f);
    open_ = true;
    shown_.target = 1.0f;
    feedback.play(audio::Cue::modal_open);
}

void UpdateDialog::dismiss(ui::Feedback &feedback)
{
    open_ = false;
    shown_.target = 0.0f;
    feedback.play(audio::Cue::modal_close);
}

void UpdateDialog::fail(std::string why, ui::Feedback &feedback)
{
    radio_update_finish();
    stage_ = Stage::failed;
    stage_time_ = 0.0f;
    if (!why.empty())
        progress_.error = std::move(why);
    choice_ = 0;
    choice_x_.snap(0.0f);
    feedback.play(audio::Cue::error);
}

void UpdateDialog::begin(ui::Feedback &feedback)
{
    progress_ = {};
    fraction_.snap(0.0f);
    stage_time_ = 0.0f;
    if (radio_update_begin())
    {
        stage_ = Stage::working;
        feedback.play(audio::Cue::select);
        return;
    }
    fail("The update helper could not start", feedback);
}

void UpdateDialog::handle(const InputFrame &input, ui::Feedback &feedback)
{
    if (!open_)
        return;
    const bool confirm = input.is_pressed(Action::confirm);
    const bool back = input.is_pressed(Action::back);
    switch (stage_)
    {
    case Stage::offer:
    case Stage::failed:
        if (input.nav == Direction::left || input.nav == Direction::right)
        {
            const int choice = input.nav == Direction::right ? 1 : 0;
            if (choice != choice_)
            {
                choice_ = choice;
                feedback.play(audio::Cue::focus);
            }
            return;
        }
        if (confirm)
            press_ = 1.0f;
        if (confirm && choice_ == 0)
            return begin(feedback);
        if (confirm || back)
            dismiss(feedback);
        return;
    case Stage::working:
        if (back)
        {
            radio_update_cancel();
            stage_ = Stage::cancelling;
            stage_time_ = 0.0f;
            feedback.play(audio::Cue::back);
        }
        return;
    case Stage::cancelling:
    case Stage::closing:
        return;
    }
}

void UpdateDialog::update(float dt, ui::Feedback &feedback)
{
    shown_.update(dt, 14.0f);
    choice_x_.target = static_cast<float>(choice_);
    choice_x_.update(dt, 18.0f);
    press_ = std::max(0.0f, press_ - dt * 5.0f);
    if (!open_)
        return;
    const bool buttons = stage_ == Stage::offer || stage_ == Stage::failed;
    height_.target = buttons ? kPanel.h : kWorkingHeight;
    height_.update(dt, 13.0f);
    stage_time_ += dt;
    spin_ += dt;

    if (stage_ == Stage::working || stage_ == Stage::cancelling)
    {
        const std::string said = progress_.error;
        progress_ = radio_update_poll();
        switch (progress_.phase)
        {
        case radio_update_phase_t::cancelled:
            radio_update_finish();
            dismiss(feedback);
            return;
        case radio_update_phase_t::failed:
            fail(progress_.error.empty() ? said : progress_.error, feedback);
            return;
        case radio_update_phase_t::ready:
            if (stage_ == Stage::working)
            {
                if (radio_update_apply())
                {
                    stage_ = Stage::closing;
                    stage_time_ = 0.0f;
                    fraction_.target = 1.0f;
                    feedback.play(audio::Cue::notify);
                }
                else
                {
                    fail("The update helper did not answer", feedback);
                }
                return;
            }
            break;
        default:
            break;
        }
        if (progress_.total > 0)
            fraction_.target = static_cast<float>(static_cast<double>(progress_.done) /
                                                  static_cast<double>(progress_.total));
        else if (progress_.phase == radio_update_phase_t::unpacking)
            fraction_.target = 1.0f;
    }
    fraction_.update(dt, 9.0f);

    if (stage_ == Stage::closing && stage_time_ >= kClosingSeconds)
        quit_ = true;
}

void UpdateDialog::draw(ui::Canvas &canvas) const
{
    const float open = tween::clamp01(shown_.value);
    if (open < 0.01f)
        return;
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const ui::Theme &theme = session_.theme;

    // The screens behind rest under a veil.
    list.rounded_rect({0.0f, 0.0f, gfx::kVirtualWidth, gfx::kVirtualHeight}, 0.0f,
                      tone::night.with_alpha(0.62f * open));

    list.push_opacity(open);
    list.push_transform(1.0f - 0.04f * (1.0f - open) * motion(), kCenterX, 540.0f, 0.0f,
                        (1.0f - open) * 30.0f * motion());
    const float height = std::clamp(height_.value, kWorkingHeight - 20.0f, kPanel.h + 20.0f);
    list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, (kPanel.h - height) * 0.5f);
    const Rect panel{kPanel.x, kPanel.y, kPanel.w, height};
    list.shadow({panel.x, panel.y + 24.0f, panel.w, panel.h}, 28.0f, 70.0f,
                Color::rgb(0x000000, 0.5f));
    draw_glass(canvas, theme, panel, 28.0f);
    const float hints_cy = kPanel.y + height - 44.0f;

    const float t = stage_time_;
    const float breathe = 0.5f + 0.5f * std::sin(canvas.time * 2.4f);
    const bool failed = stage_ == Stage::failed;
    const Color accent = failed ? kAmber : tone::teal;
    const Color title = theme.text;
    const Color copy = theme.text_muted;

    list.glow(
        {kCenterX - kRingRadius, kRingY - kRingRadius, kRingRadius * 2.0f, kRingRadius * 2.0f},
        kRingRadius, 60.0f, accent.with_alpha(0.10f + 0.08f * breathe));
    list.circle(kCenterX, kRingY, kRingRadius - kRingWidth * 0.5f, tone::night.with_alpha(0.55f));
    list.ring(kCenterX, kRingY, kRingRadius + kRingWidth * 0.5f, kRingWidth,
              kWhite.with_alpha(0.12f));

    const auto centred =
        [&](const ui::FontRef &font, const std::string &value, float cy, float size, Color color)
    {
        ui::text(list, font, font.font->fit(value, size, kPanel.w - 96.0f), kCenterX,
                 baseline_for(cy, size), size, color, gfx::Align::center);
    };
    const auto buttons = [&](const char *first, const char *second)
    {
        for (int i = 0; i < 2; ++i)
        {
            const Rect r{kButtonLeft + kButtonGap * static_cast<float>(i), kButtonsTop,
                         kButtonWidth, kButtonHeight};
            list.bordered_rect(r, kButtonHeight * 0.5f, kWhite.with_alpha(0.08f), 1.5f,
                               kWhite.with_alpha(0.16f));
        }
        const float x = kButtonLeft + kButtonGap * choice_x_.value;
        const float squeeze = 1.0f - 0.04f * press_;
        list.push_transform(squeeze, x + kButtonWidth * 0.5f, kButtonsTop + kButtonHeight * 0.5f,
                            0.0f, 0.0f);
        list.glow({x, kButtonsTop, kButtonWidth, kButtonHeight}, kButtonHeight * 0.5f, 26.0f,
                  accent.with_alpha(0.30f));
        list.rounded_rect({x, kButtonsTop, kButtonWidth, kButtonHeight}, kButtonHeight * 0.5f,
                          kWhite);
        list.pop_transform();
        for (int i = 0; i < 2; ++i)
        {
            const float left = kButtonLeft + kButtonGap * static_cast<float>(i);
            const float near =
                1.0f - std::min(1.0f, std::abs(choice_x_.value - static_cast<float>(i)));
            ui::text(list, fonts.semibold, i == 0 ? first : second, left + kButtonWidth * 0.5f,
                     baseline_for(kButtonsTop + kButtonHeight * 0.5f, 26.0f), 26.0f,
                     near > 0.5f ? tone::ink : title, gfx::Align::center);
        }
    };
    const auto hints = [&](const ui::Hint *items, int count)
    {
        ui::HintLayout layout;
        layout.size = 32.0f;
        layout.text_size = 22.0f;
        layout.cy = hints_cy;
        layout.item_gap = 36.0f;
        const float width = ui::measure_hints(fonts, items, count, layout);
        ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), items, count, kCenterX - width * 0.5f,
                       false, layout);
    };

    switch (stage_)
    {
    case Stage::offer:
    {
        arc(list, kTop, 2.0f * kPi * tween::cubic_out(t / 0.9f), accent);
        // An arrow that drops into its tray, then bobs.
        const float drop = (1.0f - tween::back_out(t / 0.7f)) * -26.0f * motion();
        const float bob = std::sin(canvas.time * 2.2f) * 3.0f * motion();
        const float ay = kRingY - 4.0f + drop + bob;
        list.line(kCenterX, ay - 30.0f, kCenterX, ay + 16.0f, 6.0f, accent);
        list.line(kCenterX - 18.0f, ay - 2.0f, kCenterX, ay + 16.0f, 6.0f, accent);
        list.line(kCenterX + 18.0f, ay - 2.0f, kCenterX, ay + 16.0f, 6.0f, accent);
        list.line(kCenterX - 30.0f, kRingY + 38.0f, kCenterX + 30.0f, kRingY + 38.0f, 6.0f,
                  accent.with_alpha(0.85f));

        centred(fonts.display, "Update available", 536.0f, 46.0f, title);
        centred(fonts.regular, "Version " + update_.version + " is ready to install.", 587.0f,
                24.0f, title.with_alpha(0.86f));
        if (update_.size > 0)
            centred(fonts.regular, "Download size: " + megabytes(update_.size), 621.0f, 20.0f,
                    kPale);
        centred(fonts.regular, "Your favorites, settings and catalogue are kept.", 651.0f, 20.0f,
                copy);
        centred(fonts.regular, "ProsperoRadio closes to finish the update.", 679.0f, 20.0f, copy);
        buttons("Update now", "Skip");
        static constexpr ui::Hint kOffer[] = {{ui::Button::dpad, "Navigate"},
                                              {ui::Button::cross, "Select"},
                                              {ui::Button::circle, "Skip"}};
        hints(kOffer, 3);
        break;
    }
    case Stage::working:
    case Stage::cancelling:
    {
        const radio_update_phase_t phase = progress_.phase;
        const bool measured = phase == radio_update_phase_t::downloading && progress_.total > 0 &&
                              stage_ == Stage::working;
        const float share = tween::clamp01(fraction_.value);
        if (measured)
        {
            arc(list, kTop, 2.0f * kPi * share, accent);
            const float a = kTop + 2.0f * kPi * share;
            list.circle(kCenterX + kRingRadius * std::cos(a), kRingY + kRingRadius * std::sin(a),
                        kRingWidth * 0.9f, kPale.with_alpha(0.95f));
            const int percent = std::min(100, static_cast<int>(share * 100.0f + 0.5f));
            ui::text(list, fonts.display, std::to_string(percent) + "%", kCenterX,
                     baseline_for(kRingY, 46.0f), 46.0f, title, gfx::Align::center);
        }
        else
        {
            // Nothing to measure yet: the stroke runs round, three dots wave.
            const float sweep = kPi * (0.55f + 0.45f * std::sin(spin_ * 2.1f));
            arc(list, spin_ * 4.2f, sweep, stage_ == Stage::cancelling ? kAmber : accent);
            for (int i = 0; i < 3; ++i)
            {
                const float wave =
                    0.5f + 0.5f * std::sin(spin_ * 6.0f - static_cast<float>(i) * 0.9f);
                list.circle(kCenterX - 26.0f + 26.0f * static_cast<float>(i),
                            kRingY - wave * 8.0f * motion(), 7.0f,
                            kPale.with_alpha(0.35f + 0.6f * wave));
            }
        }

        const char *headline = stage_ == Stage::cancelling                  ? "Cancelling"
                               : phase == radio_update_phase_t::downloading ? "Downloading"
                               : phase == radio_update_phase_t::unpacking   ? "Unpacking"
                                                                            : "Preparing";
        centred(fonts.display, headline, 535.0f, 38.0f, title);
        centred(fonts.regular, "Version " + update_.version, 575.0f, 20.0f, copy);

        const Rect bar{kPanel.x + 96.0f, 618.0f, kPanel.w - 192.0f, 8.0f};
        list.rounded_rect(bar, 4.0f, kWhite.with_alpha(0.12f));
        if (measured || phase == radio_update_phase_t::unpacking)
        {
            list.rounded_rect({bar.x, bar.y, std::max(bar.h, bar.w * share), bar.h}, 4.0f, accent);
            // A sheen runs along what is filled.
            const float sheen = std::fmod(spin_ * 0.6f, 1.0f);
            list.push_clip({bar.x, bar.y, bar.w * share, bar.h});
            list.rounded_rect({bar.x + bar.w * share * sheen - 40.0f, bar.y, 80.0f, bar.h}, 4.0f,
                              kPale.with_alpha(0.45f));
            list.pop_clip();
        }
        else
        {
            const float run = std::fmod(spin_ * 0.8f, 1.4f) - 0.2f;
            list.push_clip(bar);
            list.rounded_rect({bar.x + bar.w * run - 90.0f, bar.y, 180.0f, bar.h}, 4.0f,
                              accent.with_alpha(0.8f));
            list.pop_clip();
        }

        if (measured)
        {
            std::string line = megabytes(progress_.done) + "  /  " + megabytes(progress_.total);
            if (!progress_.time_left.empty() && stage_time_ > 1.5f)
            {
                std::string left = progress_.time_left;
                left[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(left[0])));
                line += "  \xc2\xb7  " + left;
            }
            centred(fonts.regular, line, 662.0f, 20.0f, kPale);
        }
        if (stage_ == Stage::working)
        {
            static constexpr ui::Hint kWorking[] = {{ui::Button::circle, "Cancel"}};
            hints(kWorking, 1);
        }
        break;
    }
    case Stage::closing:
    {
        arc(list, kTop, 2.0f * kPi, accent);
        // The tick pops in and draws itself in two strokes.
        const float pop = tween::back_out(t / 0.5f);
        list.push_transform(0.6f + 0.4f * pop, kCenterX, kRingY, 0.0f, 0.0f);
        list.circle(kCenterX, kRingY, kRingRadius - kRingWidth - 10.0f, accent.with_alpha(0.16f));
        const float stroke = tween::cubic_out((t - 0.15f) / 0.45f);
        const float x0 = kCenterX - 34.0f;
        const float y0 = kRingY + 2.0f;
        const float x1 = kCenterX - 10.0f;
        const float y1 = kRingY + 26.0f;
        const float x2 = kCenterX + 38.0f;
        const float y2 = kRingY - 26.0f;
        const float first = tween::clamp01(stroke / 0.4f);
        const float second = tween::clamp01((stroke - 0.4f) / 0.6f);
        if (first > 0.0f)
            list.line(x0, y0, x0 + (x1 - x0) * first, y0 + (y1 - y0) * first, 9.0f, accent);
        if (second > 0.0f)
            list.line(x1, y1, x1 + (x2 - x1) * second, y1 + (y2 - y1) * second, 9.0f, accent);
        list.pop_transform();

        centred(fonts.display, "Update ready", 536.0f, 46.0f, title);
        centred(fonts.regular, "ProsperoRadio closes now.", 585.0f, 24.0f, title.with_alpha(0.86f));
        centred(fonts.regular, "Open it again to use version " + update_.version + ".", 617.0f,
                20.0f, copy);
        const float left = 1.0f - tween::clamp01(t / kClosingSeconds);
        list.rounded_rect({kPanel.x + 96.0f, 646.0f, (kPanel.w - 192.0f) * left, 4.0f}, 2.0f,
                          accent.with_alpha(0.7f));
        break;
    }
    case Stage::failed:
    {
        arc(list, kTop, 2.0f * kPi * tween::cubic_out(t / 0.6f), accent);
        // An exclamation mark that shakes its head once.
        const float shake =
            std::sin(t * 38.0f) * 10.0f * (1.0f - tween::clamp01(t / 0.45f)) * motion();
        list.line(kCenterX + shake, kRingY - 40.0f, kCenterX + shake, kRingY + 12.0f, 10.0f,
                  accent);
        list.circle(kCenterX + shake, kRingY + 38.0f, 7.0f, accent);

        centred(fonts.display, "The update could not finish", 536.0f, 38.0f, title);
        centred(fonts.regular, "ProsperoRadio was not changed.", 587.0f, 24.0f,
                title.with_alpha(0.86f));
        if (!progress_.error.empty())
            centred(fonts.regular, progress_.error, 627.0f, 20.0f, copy);
        buttons("Try again", "Close");
        static constexpr ui::Hint kFailed[] = {{ui::Button::dpad, "Navigate"},
                                               {ui::Button::cross, "Select"},
                                               {ui::Button::circle, "Close"}};
        hints(kFailed, 3);
        break;
    }
    }
    list.pop_transform();
    list.pop_transform();
    list.pop_opacity();
}

} // namespace radio
