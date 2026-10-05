// ProsperoRadio - Scripted runs for hardware tests.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "radio_dev.hpp"

#include "app/levels.hpp"
#include "core/input.hpp"
#include "core/save_file.hpp"
#include "platform/ps5/system.hpp"
#include "radio_service.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace radio_dev
{

namespace
{

// A press is held for a few frames, then let go for long enough that the
// interface has moved before the next one.
constexpr int kPressFrames = 4;
constexpr int kPressCycle = 20;

struct Name
{
    const char *name;
    std::uint32_t bit;
};
constexpr Name kButtons[] = {
    {"up", hui::pad_bits::kUp},           {"down", hui::pad_bits::kDown},
    {"left", hui::pad_bits::kLeft},       {"right", hui::pad_bits::kRight},
    {"cross", hui::pad_bits::kCross},     {"circle", hui::pad_bits::kCircle},
    {"square", hui::pad_bits::kSquare},   {"triangle", hui::pad_bits::kTriangle},
    {"l1", hui::pad_bits::kL1},           {"r1", hui::pad_bits::kR1},
    {"l2", hui::pad_bits::kL2},           {"r2", hui::pad_bits::kR2},
    {"options", hui::pad_bits::kOptions}, {"touchpad", hui::pad_bits::kTouchpad},
};

std::uint32_t button(const std::string &name)
{
    for (const Name &entry : kButtons)
    {
        if (name == entry.name)
            return entry.bit;
    }
    return 0;
}

bool reached(const std::string &what)
{
    radio_service_status_t status{};
    radio_service_get_status(&status);
    if (what == "catalog")
        return status.catalog_size != 0U && !status.refreshing &&
               (status.catalog_state == RADIO_CATALOG_READY ||
                status.catalog_state == RADIO_CATALOG_CACHED);
    if (what == "playing")
        return status.playback_state == RADIO_PLAYBACK_PLAYING;
    if (what == "stopped")
        return status.playback_state == RADIO_PLAYBACK_STOPPED;
    return false;
}

// The wait cannot end well any more: the service has given up.
bool hopeless(const std::string &what)
{
    radio_service_status_t status{};
    radio_service_get_status(&status);
    if (what == "catalog")
        return status.catalog_state == RADIO_CATALOG_ERROR && !status.refreshing;
    if (what == "playing")
        return status.playback_state == RADIO_PLAYBACK_ERROR;
    return false;
}

} // namespace

bool Script::load(const std::string &request_path, const std::string &out_dir)
{
    std::string request;
    if (!hui::save::read_file(request_path, &request, 16384))
        return false;
    std::vector<std::string> lines;
    for (std::size_t from = 0; from < request.size();)
    {
        std::size_t to = request.find('\n', from);
        if (to == std::string::npos)
            to = request.size();
        std::string line = request.substr(from, to - from);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(std::move(line));
        from = to + 1;
    }
    char verb[16] = {};
    char word[64] = {};
    if (lines.empty() || std::sscanf(lines[0].c_str(), "%15s %63s", verb, word) != 2 ||
        std::strcmp(verb, "run") != 0)
        return false;
    token_ = word;

    hui::save::ensure_directory(out_dir);
    const std::string handled_path = out_dir + "/handled.txt";
    std::string handled;
    hui::save::read_file(handled_path, &handled, 128);
    if (handled == token_)
        return false;
    hui::save::write_atomic(handled_path, token_);

    out_dir_ = out_dir;
    for (std::size_t i = 1; i < lines.size(); ++i)
    {
        Step step;
        float number = 0.0f;
        word[0] = '\0';
        const int fields = std::sscanf(lines[i].c_str(), "%15s %63s %f", verb, word, &number);
        if (fields < 1 || verb[0] == '#')
            continue;
        const std::string what = verb;
        step.text = word;
        if (what == "wait" && fields >= 2)
        {
            step.kind = Kind::wait;
            step.seconds = static_cast<float>(std::atof(word));
        }
        else if (what == "until" && fields == 3)
        {
            step.kind = Kind::until;
            step.seconds = number;
        }
        else if (what == "press" && fields >= 2 && (step.buttons = button(step.text)) != 0)
        {
            step.kind = Kind::press;
            step.times = fields == 3 && number >= 1.0f ? static_cast<int>(number) : 1;
        }
        else if (what == "hold" && fields == 3 && (step.buttons = button(step.text)) != 0)
        {
            step.kind = Kind::hold;
            step.seconds = number;
        }
        else if (what == "shot" && fields >= 2)
            step.kind = Kind::shot;
        else if (what == "status")
            step.kind = Kind::status;
        else if (what == "quit")
        {
            step.kind = Kind::quit;
            step.seconds = fields >= 2 ? static_cast<float>(std::atof(word)) : 0.0f;
        }
        else
        {
            note("step not understood: %s", lines[i].c_str());
            continue;
        }
        steps_.push_back(std::move(step));
    }
    active_ = true;
    note("script token=%s steps=%zu", token_.c_str(), steps_.size());
    return true;
}

void Script::note(const char *format, ...)
{
    char text[320];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    char line[352];
    std::snprintf(line, sizeof(line), "t=%.1f %s", static_cast<double>(total_), text);
    notes_.emplace_back(line);
    hui::sys::log("[RADIO] dev %s", line);
}

void Script::next()
{
    ++at_;
    clock_ = 0.0f;
    frame_ = 0;
}

void Script::finish()
{
    std::string report = "finished " + token_ + "\n";
    for (const std::string &line : notes_)
        report += line + "\n";
    hui::save::write_atomic(out_dir_ + "/report.txt", report);
    reported_ = true;
}

void Script::app_closing(const char *why)
{
    if (!active_ || reported_)
        return;
    note("the app closes: %s", why);
    finish();
    active_ = false;
}

void Script::capture_done(bool ok)
{
    note("shot %s %s", capture_.substr(capture_.rfind('/') + 1).c_str(), ok ? "saved" : "FAILED");
    capture_.clear();
}

std::uint32_t Script::step(float dt)
{
    if (!active_)
        return 0;
    total_ += dt;
    for (;;)
    {
        if (at_ >= steps_.size())
        {
            note("script done");
            finish();
            active_ = false; // the controller is read again
            return 0;
        }
        const Step &step = steps_[at_];
        switch (step.kind)
        {
        case Kind::wait:
            clock_ += dt;
            if (clock_ < step.seconds)
                return 0;
            next();
            break;
        case Kind::until:
        {
            clock_ += dt;
            const bool so = reached(step.text);
            if (!so && clock_ < step.seconds && !hopeless(step.text))
                return 0;
            note("until %s: %s after %.1f s", step.text.c_str(), so ? "reached" : "NOT REACHED",
                 static_cast<double>(clock_));
            next();
            break;
        }
        case Kind::press:
        {
            const int frame = frame_++;
            if (frame >= kPressCycle * step.times)
            {
                note("press %s x%d", step.text.c_str(), step.times);
                next();
                break;
            }
            return frame % kPressCycle < kPressFrames ? step.buttons : 0;
        }
        case Kind::hold:
            clock_ += dt;
            if (clock_ < step.seconds)
                return step.buttons;
            note("hold %s %.1f s", step.text.c_str(), static_cast<double>(step.seconds));
            next();
            return 0; // let go
        case Kind::shot:
            capture_ = out_dir_ + "/" + step.text + ".bmp";
            next();
            return 0; // the frame drawn now is the one that is saved
        case Kind::status:
        {
            radio_service_status_t status{};
            radio_service_get_status(&status);
            float bands[8] = {};
            const bool live = radio_levels(bands, 8);
            note("status catalog=%d size=%u stations=%u playback=%d rate=%u channels=%u sync=%u "
                 "error=%d refreshing=%d levels=%d peak=%.2f",
                 static_cast<int>(status.catalog_state), status.catalog_size, status.station_count,
                 static_cast<int>(status.playback_state), status.sample_rate, status.channels,
                 status.sync_station_count, status.error_code, status.refreshing ? 1 : 0,
                 live ? 1 : 0, static_cast<double>(*std::max_element(bands, bands + 8)));
            next();
            break;
        }
        case Kind::quit:
            // The report first, then the wait: without filesystem access the
            // output is in the title's storage, which goes when the app does.
            if (!reported_)
            {
                note("quit in %.0f s", static_cast<double>(step.seconds));
                finish();
            }
            clock_ += dt;
            if (clock_ < step.seconds)
                return 0;
            active_ = false;
            quit_ = true;
            return 0;
        }
    }
}

} // namespace radio_dev
