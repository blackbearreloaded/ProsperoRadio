// ProsperoRadio - Asks homebrew.page once per launch whether a newer release exists.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The parsing and the decision are the boilerplate's update-check kit
// (third_party/update_check); the request goes through the app's own HTTP
// layer, on a thread of its own so the interface never waits for it.

#include "app/platform.hpp"

#include "../third_party/update_check/update_check.h"
#include "core/save_file.hpp"
#include "platform/ps5/system.hpp"
#include "radio_http.hpp"
#include "radio_storage.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <string>

namespace
{

namespace sys = hui::sys;

constexpr std::uint32_t kTimeoutUs = 10U * 1000U * 1000U;
constexpr std::size_t kStackSize = 1024U * 1024U;

pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
bool g_started = false;
bool g_found = false; // a newer release nobody has been told about yet
radio_update_t g_update;
char g_installed[12];
char g_title[10];

int fetch(const char *url, const char *user_agent, char *body, size_t capacity, size_t *length,
          int *http_status)
{
    *length = 0;
    *http_status = 0;
    int result = radio_http_init();
    if (result < 0)
        return result;
    const int model = radio_http_create_template(user_agent);
    if (model < 0)
        return model;
    radio_http_set_connect_timeout(model, kTimeoutUs);
    radio_http_set_receive_timeout(model, kTimeoutUs);
    const int connection = radio_http_create_connection(model);
    const int request = connection < 0 ? connection : radio_http_create_request(connection, url);
    result = request < 0 ? request : radio_http_send(request);
    if (result >= 0)
        result = radio_http_status(request, http_status);
    while (result >= 0)
    {
        if (*length == capacity)
        {
            char more = 0;
            const int extra = radio_http_read(request, &more, 1);
            result = extra > 0 ? UPDATE_CHECK_FETCH_TOO_LARGE : extra;
            break;
        }
        const int count = radio_http_read(request, body + *length, capacity - *length);
        if (count <= 0)
        {
            result = count;
            break;
        }
        *length += static_cast<size_t>(count);
    }
    if (request >= 0)
        radio_http_delete_request(request);
    if (connection >= 0)
        radio_http_delete_connection(connection);
    radio_http_delete_template(model);
    return result;
}

void *check(void *)
{
    update_check_result result;
    update_check_run_with(fetch, g_title, g_installed, &result);
    sys::log("[RADIO] update check: state=%d (%s) installed=%s catalog=%s http=%d error=%d",
             static_cast<int>(result.state), update_check_reason_text(result.reason),
             result.installed, result.available, result.http_status, result.platform_error);
    if (result.state == UPDATE_CHECK_AVAILABLE)
    {
        pthread_mutex_lock(&g_lock);
        g_update.version = result.version[0] != '\0' ? result.version : result.available;
        g_update.page = result.page;
        g_found = true;
        pthread_mutex_unlock(&g_lock);
    }
    return nullptr;
}

} // namespace

void radio_update_check_start(const char *installed_version)
{
    pthread_mutex_lock(&g_lock);
    const bool again = g_started;
    g_started = true;
    pthread_mutex_unlock(&g_lock);
    if (again || installed_version == nullptr)
        return;

    const std::string app_dir = radio_storage_app_dir();
    std::snprintf(g_installed, sizeof(g_installed), "%s", installed_version);
    // A test's switch: the version this launch pretends to be.
    std::string text;
    if (hui::save::read_file(app_dir + "/dev/update-installed.txt", &text))
    {
        text.erase(text.find_last_not_of(" \r\n\t") + 1);
        unsigned parts[3];
        std::snprintf(g_installed, sizeof(g_installed), "%s",
                      update_check_version_parse(text.c_str(), parts) == 1 ? text.c_str()
                                                                           : "00.000.001");
    }

    std::string param;
    if (!hui::save::read_file(app_dir + "/sce_sys/param.json", &param) ||
        update_check_json_string(param.data(), param.size(), "titleId", g_title, sizeof(g_title)) !=
            1)
    {
        sys::log("[RADIO] update check: the title ID could not be read");
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
    pthread_mutex_lock(&g_lock);
    const bool found = g_found;
    if (found)
    {
        *update = g_update;
        g_found = false;
    }
    pthread_mutex_unlock(&g_lock);
    return found;
}
