// ProsperoRadio - Updates: is a newer release listed, and replacing the app with it.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Both are the PS5 Native App Boilerplate's self-update kit
// (third_party/update_check, see its README.md), as ProsperoEden uses it. Once
// per launch the app asks the homebrew.page catalog for its own entry, checks
// the catalog's Ed25519 signature and compares the listed content version with
// this build's (sce_sys/param.json). The update downloads the release ZIP from
// GitHub over HTTPS and streams it to the self-update helper
// (self-updater.elf in the app's folder), which the app sends to the console's
// payload loader: the helper checks and unpacks the release beside the app,
// and once the app has closed it replaces the app's files and posts a
// notification. Nothing changes before the listener confirms, and a failure
// or a cancel leaves the app as it was.

#include "app/platform.hpp"

#include "../third_party/update_check/self_update.h"
#include "../third_party/update_check/update_check.h"
#include "core/save_file.hpp"
#include "platform/ps5/system.hpp"
#include "radio_http.hpp"
#include "radio_storage.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <string>

// The kit's paths, the app's own (src/update_kit/self_update_ps5.c): with
// filesystem access /app0 and /download0 are not where the files are.
// 0 the helper in the app's folder, 1 the app's param.json, 2 the file that
// keeps the catalog's highest sequence.
extern "C" const char *radio_self_update_path(int which)
{
    static const std::string helper = std::string(radio_storage_app_dir()) + "/self-updater.elf";
    static const std::string param = std::string(radio_storage_app_dir()) + "/sce_sys/param.json";
    return which == 0   ? helper.c_str()
           : which == 1 ? param.c_str()
                        : radio_storage_file(RADIO_FILE_UPDATE_SEQUENCE);
}

namespace
{

namespace sys = hui::sys;

// libcurl and OpenSSL want more stack than a default thread of the console has.
constexpr std::size_t kStackSize = 1024U * 1024U;

pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
bool g_started = false;
bool g_found = false; // an offer nobody has been told about yet
self_update_check_result g_answer = SELF_UPDATE_UNKNOWN;
self_update_offer g_offer{};
self_update_job g_job{}; // zero until the first begin, as the kit asks
bool g_begun = false;
char g_installed[12];
char g_title[10];

struct Guard
{
    Guard()
    {
        pthread_mutex_lock(&g_lock);
    }
    ~Guard()
    {
        pthread_mutex_unlock(&g_lock);
    }
    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;
};

const char *result_name(self_update_check_result result)
{
    static const char *const names[] = {"available", "up-to-date", "unknown", "untrusted",
                                        "not-installable"};
    return static_cast<unsigned>(result) < 5U ? names[result] : "?";
}

std::string dev_file(const char *name)
{
    return std::string(radio_storage_app_dir()) + "/dev/" + name;
}

bool next_line(const std::string &text, std::size_t *at, std::string *line)
{
    if (*at >= text.size())
        return false;
    const std::size_t end = text.find('\n', *at);
    *line = text.substr(*at, end == std::string::npos ? std::string::npos : end - *at);
    while (!line->empty() && (line->back() == '\r' || line->back() == ' '))
        line->pop_back();
    *at = end == std::string::npos ? text.size() : end + 1;
    return !line->empty();
}

// A test's switch: dev/update-offer.txt in the app folder replaces the
// catalog's answer (and so skips the catalog's signature), so an update can be
// tried before the catalog lists a newer release. Five lines, as the
// boilerplate's example takes them: the new content version, the release's
// name, its ZIP on GitHub, its SHA-256, its size in bytes; any further lines
// are the release notes.
bool development_offer(self_update_offer *out)
{
    std::string text;
    if (!hui::save::read_file(dev_file("update-offer.txt"), &text))
        return false;
    std::string available;
    std::string version;
    std::string artifact;
    std::string sha256;
    std::string size;
    std::size_t at = 0;
    if (!next_line(text, &at, &available) || !next_line(text, &at, &version) ||
        !next_line(text, &at, &artifact) || !next_line(text, &at, &sha256) ||
        !next_line(text, &at, &size))
        return false;
    self_update_offer filled{};
    std::snprintf(filled.title, sizeof(filled.title), "%s", g_title);
    std::snprintf(filled.installed, sizeof(filled.installed), "%s", g_installed);
    std::snprintf(filled.name, sizeof(filled.name), "ProsperoRadio");
    std::snprintf(filled.available, sizeof(filled.available), "%s", available.c_str());
    std::snprintf(filled.version, sizeof(filled.version), "%s", version.c_str());
    std::snprintf(filled.artifact, sizeof(filled.artifact), "%s", artifact.c_str());
    std::snprintf(filled.sha256, sizeof(filled.sha256), "%s", sha256.c_str());
    filled.size = std::strtoull(size.c_str(), nullptr, 10);
    // The notes keep their blank lines: the rest of the file as it is.
    std::string notes = at < text.size() ? text.substr(at) : std::string();
    while (!notes.empty() && (notes.back() == '\n' || notes.back() == '\r'))
        notes.pop_back();
    std::snprintf(filled.notes, sizeof(filled.notes), "%s", notes.c_str());
    // Like the catalog, only a newer version is offered (content versions
    // compare as text).
    if (std::strcmp(filled.available, filled.installed) <= 0)
        return false;
    *out = filled;
    return true;
}

void *check(void *)
{
    self_update_offer result{};
    self_update_check_result state = SELF_UPDATE_UNKNOWN;
    if (radio_http_init() >= 0) // libcurl is set up once, by the service's layer
        state = self_update_check(self_update_console(), g_title, g_installed, &result);
    if (development_offer(&result))
    {
        sys::log("[RADIO] update check: dev/update-offer.txt replaces the catalog's answer");
        state = SELF_UPDATE_AVAILABLE;
    }
    sys::log("[RADIO] update check: result=%s installed=%s available=%s version=%s size=%llu",
             result_name(state), g_installed, result.available[0] != '\0' ? result.available : "-",
             result.version[0] != '\0' ? result.version : "-",
             static_cast<unsigned long long>(result.size));
    if (state == SELF_UPDATE_AVAILABLE || state == SELF_UPDATE_NOT_INSTALLABLE)
    {
        const Guard guard;
        g_answer = state;
        g_offer = result;
        g_found = true;
    }
    return nullptr;
}

radio_update_phase_t from_kit(self_update_phase phase)
{
    switch (phase)
    {
    case SELF_UPDATE_STARTING:
        return radio_update_phase_t::starting;
    case SELF_UPDATE_DOWNLOADING:
        return radio_update_phase_t::downloading;
    case SELF_UPDATE_UNPACKING:
        return radio_update_phase_t::unpacking;
    case SELF_UPDATE_READY:
        return radio_update_phase_t::ready;
    case SELF_UPDATE_APPLYING:
        return radio_update_phase_t::applying;
    case SELF_UPDATE_CANCELLED:
        return radio_update_phase_t::cancelled;
    case SELF_UPDATE_FAILED:
        return radio_update_phase_t::failed;
    default:
        return radio_update_phase_t::idle;
    }
}

} // namespace

void radio_update_check_start(const char *installed_version)
{
    {
        const Guard guard;
        if (g_started || installed_version == nullptr)
            return;
        g_started = true;
    }

    std::snprintf(g_installed, sizeof(g_installed), "%s", installed_version);
    // A test's switch: the version this launch pretends to be.
    std::string text;
    if (hui::save::read_file(dev_file("update-installed.txt"), &text))
    {
        text.erase(text.find_last_not_of(" \r\n\t") + 1);
        unsigned parts[3];
        std::snprintf(g_installed, sizeof(g_installed), "%s",
                      update_check_version_parse(text.c_str(), parts) == 1 ? text.c_str()
                                                                           : "00.000.001");
    }
    char version[12];
    if (update_check_read_param(radio_self_update_path(1), g_title, version) != 1)
    {
        sys::log("[RADIO] update check: the app's param.json could not be read");
        return;
    }

    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0)
        return;
    pthread_attr_setstacksize(&attributes, kStackSize);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    if (pthread_create(&thread, &attributes, check, nullptr) != 0)
        sys::log("[RADIO] update check: the thread could not start");
    pthread_attr_destroy(&attributes);
}

bool radio_update_take(radio_update_t *update)
{
    const Guard guard;
    if (!g_found)
        return false;
    g_found = false;
    update->installable = g_answer == SELF_UPDATE_AVAILABLE;
    update->version = g_offer.version[0] != '\0' ? g_offer.version : g_offer.available;
    update->available = g_offer.available;
    update->size = g_offer.size;
    update->page = g_offer.page;
    update->notes = g_offer.notes;
    update->notes_truncated = g_offer.notes_truncated != 0;
    return true;
}

bool radio_update_begin()
{
    const Guard guard;
    if (g_answer != SELF_UPDATE_AVAILABLE)
        return false;
    if (g_begun)
        self_update_finish(&g_job);
    g_begun = self_update_start(&g_job, self_update_console(), &g_offer) == 1;
    sys::log(g_begun ? "[RADIO] update: updating to %s" : "[RADIO] update: could not begin (%s)",
             g_offer.available);
    return g_begun;
}

radio_update_progress_t radio_update_poll()
{
    radio_update_progress_t progress;
    const Guard guard;
    if (!g_begun)
        return progress;
    self_update_status status{};
    self_update_poll(&g_job, &status);
    progress.phase = from_kit(status.phase);
    progress.done = status.done;
    progress.total = status.total;
    progress.time_left = status.time_left;
    progress.error = status.error;
    return progress;
}

void radio_update_cancel()
{
    const Guard guard;
    if (g_begun)
        self_update_cancel(&g_job);
}

bool radio_update_apply()
{
    const Guard guard;
    if (!g_begun)
        return false;
    const bool going = self_update_apply(&g_job) == 1;
    sys::log(going ? "[RADIO] update: staged; the helper replaces the files once the app has closed"
                   : "[RADIO] update: the helper did not take the go-ahead");
    return going;
}

void radio_update_finish()
{
    const Guard guard;
    if (!g_begun)
        return;
    self_update_finish(&g_job);
    g_begun = false;
}
