// ProsperoRadio - Scripted runs for hardware tests.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace radio_dev
{

// A test run asked for from a PC. <install folder>/dev/request.txt holds
// "run <token>" and then one step per line; the app plays the steps in place
// of the controller, saves pictures and a report under <data folder>/dev, and
// can close itself. A request is honoured once per token. The steps:
//
//   wait <seconds>
//   until catalog|playing|stopped <seconds>   go on when it is so, or after the time
//   press <button> [times]      up down left right cross circle square triangle
//   hold <button> <seconds>     the button kept down that long
//                               l1 r1 l2 r2 options touchpad
//   shot <name>                 a picture of the frame
//   status                      one line about the service
//   quit [seconds]              write the report, wait, close the app the clean way
class Script
{
  public:
    // False when there is no request, or it was honoured by an earlier launch.
    bool load(const std::string &request_path, const std::string &out_dir);

    bool active() const
    {
        return active_;
    }
    // Advances by one frame and returns the buttons that are down in it.
    std::uint32_t step(float dt);

    // The picture this frame is to be saved as (a path), or empty.
    const std::string &capture() const
    {
        return capture_;
    }
    void capture_done(bool ok);
    // The app is closing by itself (an update it installed): the report is
    // written now, with what the script did so far.
    void app_closing(const char *why);

    bool wants_quit() const
    {
        return quit_;
    }

  private:
    enum class Kind : std::uint8_t
    {
        wait,
        until,
        press,
        hold,
        shot,
        status,
        quit,
    };
    struct Step
    {
        Kind kind = Kind::wait;
        std::string text;
        float seconds = 0.0f;
        std::uint32_t buttons = 0;
        int times = 1;
    };

    void note(const char *format, ...) __attribute__((format(printf, 2, 3)));
    void next();
    void finish();

    std::vector<Step> steps_;
    std::vector<std::string> notes_;
    std::string out_dir_;
    std::string token_;
    std::string capture_;
    std::size_t at_ = 0;
    float clock_ = 0.0f; // seconds in the current step
    float total_ = 0.0f; // seconds since the script began
    int frame_ = 0;      // frames in the current press
    bool active_ = false;
    bool reported_ = false;
    bool quit_ = false;
};

} // namespace radio_dev
