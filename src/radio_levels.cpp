// ProsperoRadio - The level of what is playing, for the visualizer.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The output thread drops every block it plays into a small ring; once a
// frame the interface takes the newest 1024 samples and measures them. The
// two threads share nothing but that ring and never wait for each other.

#include "app/levels.hpp"
#include "platform/ps5/system.hpp"
#include "radio_levels_tap.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace
{

constexpr unsigned kRing = 4096;   // mono samples kept; a power of two
constexpr unsigned kWindow = 1024; // samples measured: 21 ms at 48 kHz
constexpr float kRate = 48000.0f;
constexpr std::int64_t kStaleUs = 250000; // older than this, nothing is playing

std::atomic<std::int16_t> g_ring[kRing];
std::atomic<std::uint32_t> g_written{0};
std::atomic<std::int64_t> g_fed_us{0};

// In-place radix-2 transform of kWindow complex points.
void transform(float *real, float *imaginary)
{
    for (unsigned i = 1, j = 0; i < kWindow; ++i)
    {
        unsigned bit = kWindow >> 1;
        for (; (j & bit) != 0U; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
        {
            std::swap(real[i], real[j]);
            std::swap(imaginary[i], imaginary[j]);
        }
    }
    for (unsigned length = 2; length <= kWindow; length <<= 1)
    {
        const float angle = -6.2831853f / static_cast<float>(length);
        const float step_real = std::cos(angle);
        const float step_imaginary = std::sin(angle);
        for (unsigned start = 0; start < kWindow; start += length)
        {
            float turn_real = 1.0f;
            float turn_imaginary = 0.0f;
            for (unsigned k = 0; k < length / 2; ++k)
            {
                const unsigned a = start + k;
                const unsigned b = a + length / 2;
                const float r = real[b] * turn_real - imaginary[b] * turn_imaginary;
                const float i = real[b] * turn_imaginary + imaginary[b] * turn_real;
                real[b] = real[a] - r;
                imaginary[b] = imaginary[a] - i;
                real[a] += r;
                imaginary[a] += i;
                const float next = turn_real * step_real - turn_imaginary * step_imaginary;
                turn_imaginary = turn_real * step_imaginary + turn_imaginary * step_real;
                turn_real = next;
            }
        }
    }
}

} // namespace

void radio_levels_feed(const int16_t *stereo, unsigned frames)
{
    const std::uint32_t at = g_written.load(std::memory_order_relaxed);
    for (unsigned i = 0; i < frames; ++i)
    {
        const int mono = (static_cast<int>(stereo[i * 2U]) + stereo[i * 2U + 1U]) / 2;
        g_ring[(at + i) & (kRing - 1U)].store(static_cast<std::int16_t>(mono),
                                              std::memory_order_relaxed);
    }
    g_written.store(at + frames, std::memory_order_release);
    g_fed_us.store(hui::sys::monotonic_us(), std::memory_order_relaxed);
}

bool radio_levels(float *bands, unsigned count)
{
    std::fill(bands, bands + count, 0.0f);
    const std::uint32_t end = g_written.load(std::memory_order_acquire);
    if (count == 0U || end < kWindow ||
        hui::sys::monotonic_us() - g_fed_us.load(std::memory_order_relaxed) > kStaleUs)
        return false;

    static float real[kWindow];
    static float imaginary[kWindow];
    for (unsigned i = 0; i < kWindow; ++i)
    {
        const std::int16_t sample =
            g_ring[(end - kWindow + i) & (kRing - 1U)].load(std::memory_order_relaxed);
        // A Hann window: the block's edges must not ring through every band.
        const float window =
            0.5f - 0.5f * std::cos(6.2831853f * static_cast<float>(i) / (kWindow - 1U));
        real[i] = static_cast<float>(sample) / 32768.0f * window;
        imaginary[i] = 0.0f;
    }
    transform(real, imaginary);

    // Bands a constant ratio apart from 50 Hz to 16 kHz, as the ear hears them.
    constexpr float kLow = 50.0f;
    constexpr float kHigh = 16000.0f;
    const float ratio = std::pow(kHigh / kLow, 1.0f / static_cast<float>(count));
    float edge = kLow;
    for (unsigned band = 0; band < count; ++band)
    {
        const float next = edge * ratio;
        const unsigned first =
            std::clamp(static_cast<unsigned>(edge * kWindow / kRate), 1U, kWindow / 2U - 1U);
        const unsigned last =
            std::clamp(static_cast<unsigned>(next * kWindow / kRate), first, kWindow / 2U - 1U);
        float peak = 0.0f;
        for (unsigned bin = first; bin <= last; ++bin)
            peak = std::max(peak, real[bin] * real[bin] + imaginary[bin] * imaginary[bin]);
        // A full-scale tone reads kWindow / 4 through a Hann window.
        const float decibels = 10.0f * std::log10(peak / (kWindow * kWindow / 16.0f) + 1e-12f);
        // Music carries less energy the higher it goes: lean the scale so the
        // right of the spectrum moves as much as the left.
        const float lean = 20.0f * static_cast<float>(band) / static_cast<float>(count);
        bands[band] = std::clamp((decibels + lean + 60.0f) / 54.0f, 0.0f, 1.0f);
        edge = next;
    }
    return true;
}
