// ProsperoRadio - The update dialog: a newer release, its notes, its download.
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
#include <string_view>
#include <utility>

namespace radio
{

namespace
{

// The panel with buttons; while the update works it is shorter, and with the
// notes taller. It stays centred on the screen.
constexpr Rect kPanel{560.0f, 196.0f, 800.0f, 688.0f};
constexpr float kWorkingHeight = 604.0f;
constexpr float kNotesHeight = 864.0f;
constexpr float kCenterX = 960.0f;
constexpr float kRingY = 384.0f;
constexpr float kRingRadius = 96.0f;
constexpr float kRingWidth = 10.0f;
constexpr float kButtonsTop = 704.0f;
constexpr float kButtonHeight = 76.0f;
constexpr float kButtonsLeft = 612.0f;
constexpr float kButtonsWidth = 696.0f;
constexpr float kButtonGap = 16.0f;
// The notes view: the text's window, under the title and above the buttons.
constexpr float kNotesLeft = kPanel.x + 64.0f;
constexpr float kNotesWidth = kPanel.w - 128.0f - 18.0f; // room for the scrollbar
constexpr float kNotesTop = kPanel.y + 132.0f;
constexpr float kNotesBottomRoom = 214.0f; // buttons and hints under the window
constexpr float kNotesWindow = kNotesHeight - (kNotesTop - kPanel.y) - kNotesBottomRoom;
constexpr float kNoteSize = 21.0f;
constexpr float kNoteHeadingSize = 25.0f;
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

// One of `count` buttons in a row; `index` may lie between two of them.
Rect button_rect(int count, float index, float top)
{
    const float width =
        (kButtonsWidth - kButtonGap * static_cast<float>(count - 1)) / static_cast<float>(count);
    return {kButtonsLeft + (width + kButtonGap) * index, top, width, kButtonHeight};
}

bool starts_with(std::string_view text, std::string_view prefix)
{
    return text.substr(0, prefix.size()) == prefix;
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
    layout_notes();
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

// ---- the notes ----

// The notes are plain text as the catalog gives them: lines split by '\n',
// list items starting "- ", callouts starting "Warning:" or "Note:", and short
// lines without closing punctuation that read as headings.
void UpdateDialog::layout_notes()
{
    notes_lines_.clear();
    notes_boxes_.clear();
    notes_height_ = 0.0f;
    const ui::Fonts &fonts = session_.fonts;
    float y = 0.0f;
    bool gap_before = false;
    const std::string &all = update_.notes;
    std::size_t at = 0;
    while (at < all.size())
    {
        const std::size_t end = std::min(all.find('\n', at), all.size());
        std::string_view line = std::string_view{all}.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && (line.back() == ' ' || line.back() == '\r'))
            line.remove_suffix(1);
        if (line.empty())
        {
            gap_before = !notes_lines_.empty();
            continue;
        }
        const bool bullet = starts_with(line, "- ");
        if (bullet)
            line.remove_prefix(2);
        const bool warning =
            !bullet && (starts_with(line, "Warning:") || starts_with(line, "Caution:") ||
                        starts_with(line, "Important:"));
        const bool note = !bullet && (starts_with(line, "Note:") || starts_with(line, "Tip:"));
        const char last = line.back();
        const bool heading = !bullet && !warning && !note && line.size() <= 48 && last != '.' &&
                             last != ':' && last != '!' && last != '?' && last != ',' &&
                             last != ';' && last != ')';

        const float size = heading ? kNoteHeadingSize : kNoteSize;
        const float pitch = std::round(size * (heading ? 1.45f : 1.6f));
        const bool boxed = warning || note;
        const float indent = bullet ? 30.0f : boxed ? 26.0f : 0.0f;
        const float width = kNotesWidth - indent - (boxed ? 22.0f : 0.0f);
        const ui::FontRef &font = heading ? fonts.semibold : fonts.regular;

        if (!notes_lines_.empty())
            y += heading ? 22.0f : boxed ? 18.0f : gap_before ? 14.0f : bullet ? 4.0f : 8.0f;
        gap_before = false;
        const float block_top = y;
        if (boxed)
            y += 14.0f;
        bool first = true;
        for (std::string &piece : font.font->wrap(line, size, width))
        {
            NoteLine out;
            out.text = std::move(piece);
            out.y = y;
            out.height = pitch;
            out.size = size;
            out.indent = indent;
            out.heading = heading;
            out.bullet = bullet && first;
            notes_lines_.push_back(std::move(out));
            y += pitch;
            first = false;
        }
        if (boxed)
        {
            y += 14.0f;
            notes_boxes_.push_back({block_top, y, warning});
        }
    }
    if (update_.notes_truncated && !notes_lines_.empty())
    {
        y += 20.0f;
        NoteLine out;
        out.text = "The rest is on the app's page on homebrew.page.";
        out.y = y;
        out.height = std::round(kNoteSize * 1.6f);
        out.size = kNoteSize;
        out.muted = true;
        notes_lines_.push_back(std::move(out));
        y += std::round(kNoteSize * 1.6f);
    }
    notes_height_ = y;
}

float UpdateDialog::notes_max_scroll() const
{
    return std::max(0.0f, notes_height_ - kNotesWindow);
}

void UpdateDialog::scroll_notes(float by, ui::Feedback &feedback)
{
    const float target = std::clamp(notes_target_ + by, 0.0f, notes_max_scroll());
    if (target == notes_target_)
    {
        // Already at that end: the text gives a little and comes back.
        notes_bounce_.value = by > 0.0f ? 18.0f : -18.0f;
        notes_bounce_.velocity = 0.0f;
        return;
    }
    notes_target_ = target;
    feedback.play(audio::Cue::focus);
}

void UpdateDialog::open_notes(ui::Feedback &feedback)
{
    stage_ = Stage::notes;
    stage_time_ = 0.0f;
    notes_target_ = 0.0f;
    notes_scroll_.snap(0.0f);
    notes_bounce_.snap(0.0f);
    choice_ = 0;
    choice_x_.snap(0.0f);
    feedback.play(audio::Cue::open);
}

void UpdateDialog::close_notes(ui::Feedback &feedback)
{
    stage_ = Stage::offer;
    // Back on the offer, with the ring already nearly full and the highlight
    // on What's new.
    stage_time_ = 0.6f;
    choice_ = 1;
    choice_x_.snap(1.0f);
    feedback.play(audio::Cue::back);
}

// ---- input ----

void UpdateDialog::handle(const InputFrame &input, ui::Feedback &feedback)
{
    if (!open_)
        return;
    const bool confirm = input.is_pressed(Action::confirm);
    const bool back = input.is_pressed(Action::back);
    if (confirm)
        press_ = 1.0f;
    switch (stage_)
    {
    case Stage::offer:
    case Stage::failed:
    {
        // The offer has What's new between its buttons when the release has notes.
        const bool notes_button = stage_ == Stage::offer && has_notes();
        const int count = notes_button ? 3 : 2;
        if (input.nav == Direction::left || input.nav == Direction::right)
        {
            const int choice =
                std::clamp(choice_ + (input.nav == Direction::right ? 1 : -1), 0, count - 1);
            if (choice != choice_)
            {
                choice_ = choice;
                feedback.play(audio::Cue::focus);
            }
            return;
        }
        if (notes_button && input.is_pressed(Action::north))
            return open_notes(feedback);
        if (confirm && choice_ == 0)
            return begin(feedback);
        if (confirm && notes_button && choice_ == 1)
            return open_notes(feedback);
        // Skipped: asked again the next time the app opens.
        if (confirm || back)
            dismiss(feedback);
        return;
    }
    case Stage::notes:
    {
        const float line = std::round(kNoteSize * 1.6f);
        if (input.nav == Direction::up || input.nav == Direction::down)
            return scroll_notes((input.nav == Direction::down ? 3.0f : -3.0f) * line, feedback);
        if (input.is_pressed(Action::page_next) || input.is_pressed(Action::page_prev))
            return scroll_notes((input.is_pressed(Action::page_next) ? 1.0f : -1.0f) *
                                    (kNotesWindow - 2.0f * line),
                                feedback);
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
            return choice_ == 0 ? begin(feedback) : close_notes(feedback);
        if (back)
            close_notes(feedback);
        return;
    }
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
    height_.target = stage_ == Stage::notes ? kNotesHeight : buttons ? kPanel.h : kWorkingHeight;
    height_.update(dt, 13.0f);
    notes_scroll_.target = notes_target_;
    notes_scroll_.update(dt, session_.settings.reduced_motion ? 60.0f : 15.0f);
    notes_bounce_.target = 0.0f;
    notes_bounce_.update(dt, 16.0f);
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

// ---- drawing ----

void UpdateDialog::draw_buttons(ui::Canvas &canvas, const char *const *labels, int count, float top,
                                Color accent) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    for (int i = 0; i < count; ++i)
        list.bordered_rect(button_rect(count, static_cast<float>(i), top), kButtonHeight * 0.5f,
                           kWhite.with_alpha(0.08f), 1.5f, kWhite.with_alpha(0.16f));
    const Rect focus = button_rect(count, choice_x_.value, top);
    list.push_transform(1.0f - 0.04f * press_, focus.cx(), focus.cy(), 0.0f, 0.0f);
    list.glow(focus, kButtonHeight * 0.5f, 26.0f, accent.with_alpha(0.30f));
    list.rounded_rect(focus, kButtonHeight * 0.5f, kWhite);
    list.pop_transform();
    for (int i = 0; i < count; ++i)
    {
        const Rect r = button_rect(count, static_cast<float>(i), top);
        const float near = 1.0f - std::min(1.0f, std::abs(choice_x_.value - static_cast<float>(i)));
        const float size = count > 2 ? 24.0f : 26.0f;
        ui::text(list, fonts.semibold, fonts.semibold.font->fit(labels[i], size, r.w - 32.0f),
                 r.cx(), baseline_for(r.cy(), size), size,
                 near > 0.5f ? tone::ink : session_.theme.text, gfx::Align::center);
    }
}

void UpdateDialog::draw_notes(ui::Canvas &canvas, float height) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const ui::Theme &theme = session_.theme;
    const Color accent = tone::teal;
    const float t = stage_time_;
    const float arrive = tween::cubic_out(t / 0.35f);

    // The title, with a small mark: a page with lines on it.
    list.push_opacity(arrive);
    list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, (1.0f - arrive) * 10.0f * motion());
    const float mark_x = kNotesLeft + 22.0f;
    const float mark_y = kPanel.y + 66.0f;
    list.circle(mark_x, mark_y, 24.0f, accent.with_alpha(0.16f));
    list.ring(mark_x, mark_y, 24.0f, 2.0f, accent.with_alpha(0.55f));
    for (int i = 0; i < 3; ++i)
    {
        const float ly = mark_y - 8.0f + 8.0f * static_cast<float>(i);
        list.line(mark_x - 9.0f, ly, mark_x + (i == 2 ? 3.0f : 9.0f), ly, 2.6f, accent);
    }
    const std::string title = "What's new in version " + update_.version;
    ui::text(list, fonts.display, fonts.display.font->fit(title, 34.0f, kPanel.w - 128.0f - 64.0f),
             kNotesLeft + 64.0f, baseline_for(mark_y, 34.0f), 34.0f, theme.text);
    list.rounded_rect({kNotesLeft, kPanel.y + 112.0f, kPanel.w - 128.0f, 1.0f}, 0.0f,
                      kWhite.with_alpha(0.14f));
    list.pop_transform();
    list.pop_opacity();

    // The text, in its window, moved by the scroll (and a little more at the ends).
    const Rect area{kNotesLeft - 6.0f, kNotesTop, kNotesWidth + 12.0f, kNotesWindow};
    const float scroll = notes_scroll_.value + notes_bounce_.value * 0.6f;
    list.push_clip(area);
    for (const NoteBox &box : notes_boxes_)
    {
        const float top = kNotesTop + box.top - scroll;
        const float bottom = kNotesTop + box.bottom - scroll;
        if (bottom < area.y || top > area.y + area.h)
            continue;
        const Color tint = box.warning ? kAmber : accent;
        list.push_opacity(arrive);
        list.rounded_rect({kNotesLeft, top, kNotesWidth, bottom - top}, 14.0f,
                          tint.with_alpha(0.09f));
        list.rounded_rect({kNotesLeft, top + 10.0f, 4.0f, bottom - top - 20.0f}, 2.0f,
                          tint.with_alpha(0.85f));
        list.pop_opacity();
    }
    int shown = 0;
    for (const NoteLine &line : notes_lines_)
    {
        const float top = kNotesTop + line.y - scroll;
        if (top + line.height < area.y || top > area.y + area.h)
            continue;
        // The lines first shown come in one after another.
        const float delay = 0.10f + 0.03f * static_cast<float>(std::min(shown++, 14));
        const float in = t > 1.2f ? 1.0f : tween::cubic_out((t - delay) / 0.32f);
        if (in <= 0.0f)
            continue;
        list.push_opacity(in);
        list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, (1.0f - in) * 14.0f * motion());
        if (line.bullet)
            list.circle(kNotesLeft + 11.0f, top + line.height * 0.5f, 3.6f,
                        accent.with_alpha(0.9f));
        ui::text(list, line.heading ? fonts.semibold : fonts.regular, line.text,
                 kNotesLeft + line.indent, baseline_for(top + line.height * 0.5f, line.size),
                 line.size,
                 line.heading ? theme.text
                 : line.muted ? theme.text_muted
                              : theme.text.with_alpha(0.86f));
        list.pop_transform();
        list.pop_opacity();
    }
    list.pop_clip();

    // The scrollbar: a track and a thumb that follows the scroll.
    const float max_scroll = notes_max_scroll();
    if (max_scroll > 0.0f)
    {
        const float track_x = kNotesLeft + kNotesWidth + 14.0f;
        const float thumb = std::max(48.0f, kNotesWindow * kNotesWindow / notes_height_);
        const float place = tween::clamp01(scroll / max_scroll);
        list.push_opacity(arrive);
        list.rounded_rect({track_x, area.y, 4.0f, kNotesWindow}, 2.0f, kWhite.with_alpha(0.12f));
        list.rounded_rect({track_x - 1.0f, area.y + (kNotesWindow - thumb) * place, 6.0f, thumb},
                          3.0f, accent.with_alpha(0.85f));
        list.pop_opacity();
    }

    // The buttons and the hints under the window.
    list.push_opacity(arrive);
    static constexpr const char *kLabels[] = {"Update now", "Back"};
    draw_buttons(canvas, kLabels, 2, kPanel.y + height - 178.0f, accent);
    static constexpr ui::Hint kHints[] = {{ui::Button::dpad, "Scroll"},
                                          {ui::Button::l1, "Page", ui::Button::r1},
                                          {ui::Button::cross, "Select"},
                                          {ui::Button::circle, "Back"}};
    ui::HintLayout layout;
    layout.size = 32.0f;
    layout.text_size = 22.0f;
    layout.cy = kPanel.y + height - 44.0f;
    layout.item_gap = 36.0f;
    const float width = ui::measure_hints(fonts, kHints, 4, layout);
    ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), kHints, 4, kCenterX - width * 0.5f, false,
                   layout);
    list.pop_opacity();
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
    const float height = std::clamp(height_.value, kWorkingHeight - 20.0f, kNotesHeight + 20.0f);
    list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, (kPanel.h - height) * 0.5f);
    const Rect panel{kPanel.x, kPanel.y, kPanel.w, height};
    list.shadow({panel.x, panel.y + 24.0f, panel.w, panel.h}, 28.0f, 70.0f,
                Color::rgb(0x000000, 0.5f));
    draw_glass(canvas, theme, panel, 28.0f);
    if (stage_ == Stage::notes)
    {
        draw_notes(canvas, height);
        list.pop_transform();
        list.pop_transform();
        list.pop_opacity();
        return;
    }
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
    case Stage::notes:
        break;
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
        if (has_notes())
        {
            static constexpr const char *kLabels[] = {"Update now", "What's new", "Skip"};
            draw_buttons(canvas, kLabels, 3, kButtonsTop, accent);
            static constexpr ui::Hint kOffer[] = {{ui::Button::dpad, "Navigate"},
                                                  {ui::Button::cross, "Select"},
                                                  {ui::Button::triangle, "What's new"},
                                                  {ui::Button::circle, "Skip"}};
            hints(kOffer, 4);
        }
        else
        {
            static constexpr const char *kLabels[] = {"Update now", "Skip"};
            draw_buttons(canvas, kLabels, 2, kButtonsTop, accent);
            static constexpr ui::Hint kOffer[] = {{ui::Button::dpad, "Navigate"},
                                                  {ui::Button::cross, "Select"},
                                                  {ui::Button::circle, "Skip"}};
            hints(kOffer, 3);
        }
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
        static constexpr const char *kLabels[] = {"Try again", "Close"};
        draw_buttons(canvas, kLabels, 2, kButtonsTop, accent);
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
