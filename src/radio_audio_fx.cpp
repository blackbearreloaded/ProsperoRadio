// ProsperoRadio - The station's sound on its way out: volume, tone, balance.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The settings are written by the interface and read by the audio output
// thread once a block; only that thread touches the filters. Bass and treble
// are shelving filters (Robert Bristow-Johnson's cookbook) at 150 Hz and
// 6 kHz. A boost can push a loud block past full scale, so the last stage
// bends the top of the range instead of clipping it.

#include "app/platform.hpp"
#include "core/save_file.hpp"
#include "radio_audio_tap.hpp"
#include "radio_storage.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace
{

constexpr float kRate = 48000.0f;
constexpr float kPi = 3.14159265358979f;

std::atomic<float> g_volume{1.0f};
std::atomic<float> g_bass{0.0f};
std::atomic<float> g_treble{0.0f};
std::atomic<float> g_balance{0.0f};
std::atomic<bool> g_mono{false};
std::atomic<unsigned> g_version{1};
std::atomic<float> g_meter_left{0.0f};
std::atomic<float> g_meter_right{0.0f};

struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float x1[2] = {}, x2[2] = {}, y1[2] = {}, y2[2] = {};

    // low: a low shelf, else a high shelf.
    void shelf(bool low, float frequency, float gain_db)
    {
        const float a = std::pow(10.0f, gain_db / 40.0f);
        const float w = 2.0f * kPi * frequency / kRate;
        const float cosw = std::cos(w);
        const float alpha = std::sin(w) / 2.0f * std::sqrt(2.0f); // slope 1
        const float root = 2.0f * std::sqrt(a) * alpha;
        float n0, n1, n2, d0, d1, d2;
        if (low)
        {
            n0 = a * ((a + 1) - (a - 1) * cosw + root);
            n1 = 2 * a * ((a - 1) - (a + 1) * cosw);
            n2 = a * ((a + 1) - (a - 1) * cosw - root);
            d0 = (a + 1) + (a - 1) * cosw + root;
            d1 = -2 * ((a - 1) + (a + 1) * cosw);
            d2 = (a + 1) + (a - 1) * cosw - root;
        }
        else
        {
            n0 = a * ((a + 1) + (a - 1) * cosw + root);
            n1 = -2 * a * ((a - 1) + (a + 1) * cosw);
            n2 = a * ((a + 1) + (a - 1) * cosw - root);
            d0 = (a + 1) - (a - 1) * cosw + root;
            d1 = 2 * ((a - 1) - (a + 1) * cosw);
            d2 = (a + 1) - (a - 1) * cosw - root;
        }
        b0 = n0 / d0;
        b1 = n1 / d0;
        b2 = n2 / d0;
        a1 = d1 / d0;
        a2 = d2 / d0;
    }

    float run(int channel, float x)
    {
        const float y =
            b0 * x + b1 * x1[channel] + b2 * x2[channel] - a1 * y1[channel] - a2 * y2[channel];
        x2[channel] = x1[channel];
        x1[channel] = x;
        y2[channel] = y1[channel];
        y1[channel] = y;
        return y;
    }
};

// The output thread's own state.
struct Chain
{
    unsigned version = 0;
    Biquad bass;
    Biquad treble;
    bool bass_on = false;
    bool treble_on = false;
    float gain[2] = {1.0f, 1.0f}; // where the last block ended, per side
    bool mono = false;
    float peak[2] = {};
};
Chain g_chain;

// Above 0.8 of full scale the curve bends smoothly toward 1.
float bend(float x)
{
    const float a = std::fabs(x);
    if (a <= 0.8f)
        return x;
    const float over = (a - 0.8f) / 0.2f;
    const float y = 0.8f + 0.2f * over / (1.0f + over);
    return x < 0.0f ? -y : y;
}

} // namespace

void radio_audio_configure(const radio_audio_settings_t &settings)
{
    g_volume.store(std::clamp(settings.volume, 0.0f, 1.0f));
    g_bass.store(std::clamp(settings.bass_db, -12.0f, 12.0f));
    g_treble.store(std::clamp(settings.treble_db, -12.0f, 12.0f));
    g_balance.store(std::clamp(settings.balance, -1.0f, 1.0f));
    g_mono.store(settings.mono);
    g_version.fetch_add(1);
}

void radio_audio_meter(float *left, float *right)
{
    *left = g_meter_left.load(std::memory_order_relaxed);
    *right = g_meter_right.load(std::memory_order_relaxed);
}

void radio_audio_process(int16_t *stereo, unsigned frames)
{
    Chain &c = g_chain;
    const unsigned version = g_version.load(std::memory_order_acquire);
    if (version != c.version)
    {
        c.version = version;
        const float bass = g_bass.load();
        const float treble = g_treble.load();
        c.bass_on = std::fabs(bass) > 0.05f;
        c.treble_on = std::fabs(treble) > 0.05f;
        if (c.bass_on)
            c.bass.shelf(true, 150.0f, bass);
        if (c.treble_on)
            c.treble.shelf(false, 6000.0f, treble);
        c.mono = g_mono.load();
    }
    // The volume is a curve (a slider's middle sounds like half), the balance
    // turns one side down.
    const float volume = g_volume.load();
    const float level = volume * volume;
    const float balance = g_balance.load();
    const float target[2] = {level * std::min(1.0f, 1.0f - balance),
                             level * std::min(1.0f, 1.0f + balance)};
    float peak[2] = {};
    for (unsigned i = 0; i < frames; ++i)
    {
        // The gain glides over the block: a change never clicks.
        const float t = static_cast<float>(i + 1) / static_cast<float>(frames);
        float s[2] = {stereo[i * 2] / 32768.0f, stereo[i * 2 + 1] / 32768.0f};
        if (c.mono)
            s[0] = s[1] = 0.5f * (s[0] + s[1]);
        for (int ch = 0; ch < 2; ++ch)
        {
            float x = s[ch];
            if (c.bass_on)
                x = c.bass.run(ch, x);
            if (c.treble_on)
                x = c.treble.run(ch, x);
            x *= c.gain[ch] + (target[ch] - c.gain[ch]) * t;
            x = bend(x);
            peak[ch] = std::max(peak[ch], std::fabs(x));
            stereo[i * 2 + ch] = static_cast<int16_t>(std::lrint(x * 32767.0f));
        }
    }
    for (int ch = 0; ch < 2; ++ch)
    {
        c.gain[ch] = target[ch];
        // A meter falls back over about a third of a second.
        c.peak[ch] = std::max(peak[ch], c.peak[ch] * 0.92f);
    }
    g_meter_left.store(c.peak[0], std::memory_order_relaxed);
    g_meter_right.store(c.peak[1], std::memory_order_relaxed);
}

bool radio_settings_load(std::string *text)
{
    return hui::save::read_file(radio_storage_file(RADIO_FILE_SETTINGS), text, 16384);
}

bool radio_settings_save(const std::string &text)
{
    return hui::save::write_atomic(radio_storage_file(RADIO_FILE_SETTINGS), text).empty();
}
