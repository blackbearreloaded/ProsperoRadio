// ProsperoRadio - A shoulder button held down: one step, then more, faster.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/kit.hpp"

namespace radio
{

// L2 / R2 turn pages and jump letters. A press is one step; held, the steps
// go on after a pause and come faster, the way a held direction repeats.
// Call step() once a frame with that frame's input.
class HeldStep
{
  public:
    // -1 (L2), 1 (R2) or 0.
    int step(const InputFrame &input)
    {
        const int pressed = input.is_pressed(Action::jump_next)   ? 1
                            : input.is_pressed(Action::jump_prev) ? -1
                                                                  : 0;
        if (pressed != 0)
        {
            direction_ = pressed;
            frames_ = 0;
            repeats_ = 0;
            return pressed;
        }
        const bool held = (direction_ > 0 && input.is_held(Action::jump_next)) ||
                          (direction_ < 0 && input.is_held(Action::jump_prev));
        if (!held)
        {
            direction_ = 0;
            return 0;
        }
        ++frames_;
        // About a third of a second, then every 110 ms, then every 50 ms.
        const int wait = repeats_ == 0 ? 22 : (repeats_ < 6 ? 7 : 3);
        if (frames_ < wait)
            return 0;
        frames_ = 0;
        ++repeats_;
        return direction_;
    }
    // A step that came from holding, not from a press (quieter cues).
    bool repeating() const
    {
        return repeats_ > 0;
    }

  private:
    int direction_ = 0;
    int frames_ = 0;
    int repeats_ = 0;
};

} // namespace radio
