// ProsperoRadio - Application entry point.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Opens the display and the controller, starts the radio service, then runs
// the interface every frame: read input, update, draw, present. Lifecycle
// markers and frame pacing are logged for hardware runs.

#include "app/app.hpp"
#include "core/frame_stats.hpp"
#include "core/input.hpp"
#include "core/save_file.hpp"
#include "core/version.hpp"
#include "gfx/renderer.hpp"
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/pad.hpp"
#include "platform/ps5/system.hpp"
#include "gfx/canvas.hpp"
#include "gfx/system_fonts.hpp"
#include "radio_dev.hpp"
#include "radio_ime.hpp"
#include "radio_input.hpp"
#include "radio_runtime.hpp"
#include "radio_service.hpp"
#include "radio_storage.hpp"

#include <GL/glcorearb.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

extern "C" void hui_heap_stats(std::size_t *live_bytes, std::size_t *peak_bytes,
                               std::size_t *blocks, std::size_t *failures);
extern "C" int sceSystemServiceParamGetInt(int parameter, int *value);
// The system's launch picture stays up until this is called (runtime_shims.c).
extern "C" void radio_release_splash(void);

namespace
{

// Whether Cross is down right now, whatever it is mapped to.
bool g_cross_held = false;

// Pictures of a scripted run: half size is enough to read every label.
constexpr int kCaptureWidth = 960;
constexpr int kCaptureHeight = 540;

std::int64_t g_started = 0;

// How long the start-up has taken so far, for the log.
void mark(const char *what)
{
    hui::sys::log("[RADIO] start %s at %lld ms", what,
                  static_cast<long long>((hui::sys::monotonic_us() - g_started) / 1000));
}

// The bound framebuffer, bottom row first, as a 24-bit BMP.
bool save_picture(const std::string &path, int width, int height)
{
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const std::uint32_t row = (static_cast<std::uint32_t>(width) * 3 + 3) & ~3u;
    const std::uint32_t size = 54 + row * static_cast<std::uint32_t>(height);
    unsigned char header[54] = {'B', 'M'};
    const auto put = [&](int at, std::uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
            header[at + i] = static_cast<unsigned char>(value >> (8 * i));
    };
    put(2, size);
    put(10, 54);
    put(14, 40);
    put(18, static_cast<std::uint32_t>(width));
    put(22, static_cast<std::uint32_t>(height));
    header[26] = 1;
    header[28] = 24;
    put(34, size - 54);
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr)
        return false;
    bool ok = std::fwrite(header, 1, sizeof(header), file) == sizeof(header);
    std::vector<unsigned char> line(row);
    for (int y = 0; ok && y < height; ++y)
    {
        const unsigned char *in = pixels.data() + static_cast<std::size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x)
        {
            line[static_cast<std::size_t>(x) * 3 + 0] = in[x * 4 + 2];
            line[static_cast<std::size_t>(x) * 3 + 1] = in[x * 4 + 1];
            line[static_cast<std::size_t>(x) * 3 + 2] = in[x * 4 + 0];
        }
        ok = std::fwrite(line.data(), 1, line.size(), file) == line.size();
    }
    return std::fclose(file) == 0 && ok;
}

void log_heap(std::uint64_t frames)
{
    std::size_t live = 0;
    std::size_t peak = 0;
    std::size_t blocks = 0;
    std::size_t failures = 0;
    hui_heap_stats(&live, &peak, &blocks, &failures);
    hui::sys::log("[RADIO] heap frames=%llu live=%zu peak=%zu blocks=%zu failures=%zu",
                  static_cast<unsigned long long>(frames), live, peak, blocks, failures);
}

bool load_font(hui::gfx::Renderer &renderer, const char *name, hui::gfx::Font *font,
               hui::ui::FontRef *ref)
{
    std::string data;
    const std::string path = std::string(radio_storage_app_dir()) + "/assets/fonts/" + name;
    if (!hui::save::read_file(path, &data) || !font->load(data))
    {
        hui::sys::log("[RADIO] font %s failed: %s", name, font->error().c_str());
        return false;
    }
    ref->font = font;
    ref->texture = renderer.batch().create_font_texture(*font);
    return true;
}

// The console's language as a tag ("ja-JP", "zh-Hans", ...): the fonts for
// Japanese, Chinese and Korean share characters, and the console's language
// decides whose forms they take.
std::string system_language_tag()
{
    int language = -1;
    if (sceSystemServiceParamGetInt(1, &language) != 0)
        return "en-US";
    switch (language)
    {
    case 0:
        return "ja-JP";
    case 9:
        return "ko-KR";
    case 10:
        return "zh-Hant";
    case 11:
        return "zh-Hans";
    case 21:
        return "ar";
    case 27:
        return "th-TH";
    default:
        return "en-US";
    }
}

} // namespace

// The keyboard waits for Cross to be let go before it opens, so the press
// that asked for it is not typed into it. This is all it asks of the pad.
bool radio_input_pressed(radio_input_key_t key)
{
    return key == RADIO_INPUT_CROSS && g_cross_held;
}

int main()
{
    using namespace hui;
    g_started = sys::monotonic_us();
    // Filesystem access first, while the process has one thread: every path
    // below, the log's included, depends on the answer.
    radio_storage_init();
    mark("storage");
    if (!radio_runtime_init())
    {
        sys::log("[RADIO] fatal: runtime init failed");
        sys::park();
    }

    // 1080p for now: it is what the app has always presented at. The kit can
    // open 1440p and 4K; that becomes a setting once this build has run.
    ps5::Display display;
    if (!display.open(1920, 1080))
    {
        sys::log("[RADIO] fatal: display open failed");
        sys::park();
    }
    mark("display");

    gfx::Renderer renderer;
    gfx::Font regular;
    gfx::Font semibold;
    gfx::Font display_font;
    gfx::Font mono;
    ui::Fonts fonts;
    if (!renderer.init() ||
        !load_font(renderer, "inter-regular.huifont", &regular, &fonts.regular) ||
        !load_font(renderer, "inter-semibold.huifont", &semibold, &fonts.semibold) ||
        !load_font(renderer, "montserrat-medium.huifont", &display_font, &fonts.display) ||
        !load_font(renderer, "dejavu-sans-mono.huifont", &mono, &fonts.mono))
    {
        sys::log("[RADIO] fatal: renderer init failed");
        sys::park();
    }
    // Station names come in every script; the baked faces hold ASCII. What
    // they lack comes from the console's own fonts (ProsperoEden's engine:
    // read when first needed, shaped with HarfBuzz, right-to-left ordered).
    const std::string language = system_language_tag();
    for (const std::string &folder : gfx::system_font_folders())
    {
        std::vector<std::string> files = gfx::system_font_files(folder, language);
        if (files.empty())
            continue;
        for (gfx::Font *face : {&regular, &semibold, &display_font})
            face->use_system_fonts(files, language);
        sys::log("[RADIO] system fonts: %zu files in %s for %s", files.size(), folder.c_str(),
                 language.c_str());
        break;
    }
    // Night Signal uses four faces; the other two slots are for the scripts
    // the station names need.
    fonts.pixel = fonts.mono;
    fonts.hand = fonts.regular;
    mark("renderer");

    // The controller opens the user service, which the keyboard needs too.
    ps5::Pad pad;
    const bool pad_ready = pad.open();
    const bool ime_ready = pad_ready && radio_ime_init();
    mark("controller");
    const bool service_ready = radio_service_init();
    mark("service");
    sys::log("[RADIO] pad=%d keyboard=%d service=%d", pad_ready ? 1 : 0, ime_ready ? 1 : 0,
             service_ready ? 1 : 0);

    const std::string version =
        read_content_version(std::string(radio_storage_app_dir()) + "/sce_sys/param.json");
    radio::App app(fonts, renderer.glass_texture(), version.empty() ? "unknown" : version);
    sys::log("[RADIO] version %s", version.empty() ? "unknown" : version.c_str());

    // A request left by a PC turns this launch into a scripted test run.
    radio_dev::Script script;
    if (script.load(std::string(radio_storage_app_dir()) + "/dev/request.txt",
                    std::string(radio_storage_data_dir()) + "/dev"))
        sys::log("[RADIO] scripted run: the controller is not read");
    gfx::Canvas capture;
    const std::string quit_path = std::string(radio_storage_app_dir()) + "/dev/quit.txt";
    std::string quit_note;

    InputTracker tracker;
    ui::Feedback feedback;
    radio::Frame frame;
    FrameStats stats;
    PadSample samples[64];
    std::uint64_t frames = 0;
    std::int64_t previous = sys::monotonic_us();
    std::int64_t last_frame_start = previous;
    for (;;)
    {
        const std::int64_t now = sys::monotonic_us();
        // Animation time is start-to-start (one full frame), and a hitch must
        // not teleport the animations.
        float dt = frames == 0 ? 1.0f / 60.0f : static_cast<float>(now - last_frame_start) / 1e6f;
        last_frame_start = now;
        if (dt > 0.05f)
            dt = 0.05f;
        std::size_t count = pad.read(samples);
        if (script.active())
        {
            // The script is the controller: one sample a frame, nothing else.
            samples[0] = PadSample{};
            samples[0].buttons = script.step(dt);
            samples[0].connected = true;
            samples[0].timestamp_us = static_cast<std::uint64_t>(now);
            count = 1;
        }
        const InputFrame input = tracker.update(std::span<const PadSample>(samples, count),
                                                static_cast<std::uint64_t>(now));
        g_cross_held = count > 0 && (samples[count - 1].buttons & pad_bits::kCross) != 0;

        if (ime_ready)
            radio_ime_poll();
        feedback.clear();
        app.update(input, dt, feedback);
        // Interface sounds are not wired in this build: the cues are dropped.
        if (feedback.rumble_strength > 0.0f)
            pad.rumble(feedback.rumble_strength, feedback.rumble_seconds);
        pad.tick(dt);

        app.draw(frame);
        // Glyphs the frame's text needed from the console's fonts.
        renderer.batch().sync_font_texture(fonts.regular.texture, regular);
        renderer.batch().sync_font_texture(fonts.semibold.texture, semibold);
        renderer.batch().sync_font_texture(fonts.display.texture, display_font);
        renderer.begin();
        renderer.backdrop(frame.backdrop);
        renderer.draw(frame.scene);
        renderer.glass();
        renderer.draw(frame.overlay);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        renderer.present(0, display.width(), display.height());
        if (!script.capture().empty())
        {
            // The same frame once more, into a small off-screen target:
            // reading the display surface back is slow.
            if (capture.texture() == 0)
                capture.create(kCaptureWidth, kCaptureHeight, 1);
            capture.bind();
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            renderer.present(capture.framebuffer(), kCaptureWidth, kCaptureHeight);
            glBindFramebuffer(GL_FRAMEBUFFER, capture.framebuffer());
            const bool saved = save_picture(script.capture(), kCaptureWidth, kCaptureHeight);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            script.capture_done(saved);
            last_frame_start = sys::monotonic_us(); // saving is slow; the frame was not
        }
        if (!display.swap())
        {
            sys::log("[RADIO] fatal: swap failed frame=%llu error=%s",
                     static_cast<unsigned long long>(frames),
                     ps5::egl_error_name(display.last_error()));
            sys::park();
        }
        ++frames;
        const std::int64_t presented = sys::monotonic_us();
        if (frames == 1)
        {
            sys::log("[RADIO] first-swap ok shapes=%zu draws=%zu", renderer.last_instances(),
                     renderer.last_draw_calls());
            mark("first frame");
            // The launch picture gives way to a frame, never to a black screen.
            radio_release_splash();
            const bool hidden = sys::hide_splash_screen();
            sys::log("[RADIO] ready splash_hidden=%d", hidden ? 1 : 0);
            log_heap(frames);
        }
        else
        {
            stats.add(static_cast<double>(presented - previous) / 1000.0);
        }
        previous = presented;

        // A test can end a run from the PC: dev/quit.txt beside the app.
        const bool asked_from_pc = frames % 60 == 0 && save::read_file(quit_path, &quit_note, 64);
        if (app.wants_quit() || script.wants_quit() || asked_from_pc)
        {
            sys::log("[RADIO] closing: %s", asked_from_pc         ? "asked for by a test"
                                            : script.wants_quit() ? "the test script ended"
                                                                  : "asked from the menu");
            if (app.wants_quit())
                script.app_closing("asked by the app");
            if (ime_ready)
                radio_ime_shutdown();
            radio_service_shutdown();
            pad.close();
            sys::quit();
        }
        if (stats.count() == 600)
        {
            char summary[160];
            stats.format(summary, sizeof(summary));
            sys::log("[RADIO] %s draws=%zu shapes=%zu", summary, renderer.last_draw_calls(),
                     renderer.last_instances());
            stats.reset();
            if (frames % 3600 < 600)
                log_heap(frames);
        }
    }
}
