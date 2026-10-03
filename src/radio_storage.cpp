// ProsperoRadio - Where the app keeps its files.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "radio_storage.hpp"

#include "elevation/elevation.hpp"

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <sys/stat.h>
#include <unistd.h>

extern "C" int sceKernelDebugOutText(int channel, const char *text);
// runtime_shims.c: loads the system modules the app uses and keeps them.
extern "C" int radio_preload_modules(int *results, unsigned capacity);

namespace
{

constexpr const char *kTitleId = "PPSA99001";
constexpr const char *kDataParent = "/data";
constexpr const char *kDataDir = "/data/prosperoradio";
constexpr const char *kInstallDir = "/data/homebrew/PPSA99001";
// A title's sandbox, seen from the console's own root.
constexpr const char *kSandboxApp = "/mnt/sandbox/PPSA99001_000/app0";
constexpr const char *kSandboxData = "/mnt/sandbox/PPSA99001_000/download0";

struct Entry
{
    const char *folder; // under the data folder, when the app has filesystem access
    const char *name;
};
constexpr Entry kFiles[RADIO_FILE_COUNT] = {
    {"catalog", "radio-browser.sqlite3"},
    {"catalog", "radio-browser-next.sqlite3"},
    {"config", "radio-browser-favorites.bin"},
    {"config", "radio-browser-favorites.tmp"},
    {"catalog", "radio-browser-cache.bin"},
    {"logs", "prosperoradio.log"},
    {"config", "settings.txt"},
};

int g_status = -1;
char g_app_dir[96] = "/app0";
char g_data_dir[96] = "/download0";
char g_paths[RADIO_FILE_COUNT][160];

bool is_file(const char *path)
{
    struct stat info
    {
    };
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

// Copies a file the sandbox kept, unless the new place already has one.
bool bring_over(const char *name, const char *to)
{
    char from[200];
    std::snprintf(from, sizeof(from), "%s/%s", kSandboxData, name);
    if (is_file(to) || !is_file(from))
        return false;
    std::FILE *input = std::fopen(from, "rb");
    if (input == nullptr)
        return false;
    char partial[200];
    std::snprintf(partial, sizeof(partial), "%s.part", to);
    std::FILE *output = std::fopen(partial, "wb");
    bool ok = output != nullptr;
    static char buffer[256 * 1024];
    while (ok)
    {
        const std::size_t count = std::fread(buffer, 1, sizeof(buffer), input);
        if (count == 0)
            break;
        ok = std::fwrite(buffer, 1, count, output) == count;
    }
    ok = ok && std::ferror(input) == 0;
    std::fclose(input);
    if (output != nullptr)
        ok = std::fclose(output) == 0 && ok;
    // A copy that stopped half way must not be taken for the file.
    if (ok)
        ok = std::rename(partial, to) == 0;
    else
        std::remove(partial);
    return ok;
}

void open_log(const char *path)
{
    // Keep the previous session's log: a failure is read after the app is reopened.
    char previous[176];
    std::snprintf(previous, sizeof(previous), "%.*s.prev.log",
                  static_cast<int>(std::strlen(path) - 4), path);
    std::rename(path, previous);
    std::FILE *stream = std::freopen(path, "w", stdout);
    // Append-only and written line by line: the log survives a close or a GPU
    // stop, and lines from two threads do not run into each other.
    if (stream != nullptr)
        stream = std::freopen(path, "a", stdout);
    if (stream != nullptr)
        std::setvbuf(stream, nullptr, _IOLBF, 1024);
    stream = std::freopen(path, "a", stderr);
    if (stream != nullptr)
        std::setvbuf(stream, nullptr, _IOLBF, 1024);
}

void say(const char *line)
{
    std::fputs(line, stdout);
    sceKernelDebugOutText(0, line);
}

} // namespace

void radio_storage_init(void)
{
    // The system finds its modules by a path inside the sandbox: asked for
    // after the process has left it, a module does not load. So the ones the
    // app uses come first.
    int modules[4] = {};
    const int modules_loaded = radio_preload_modules(modules, 4);
    // A test can ask for a start that stays in the sandbox, or bring a helper
    // of its own.
    const char *helper =
        is_file("/app0/dev/helper.elf") ? "/app0/dev/helper.elf" : "/app0/sandbox-elevator.elf";
    g_status =
        is_file("/app0/dev/no-elevation.txt")
            ? -2
            : static_cast<int>(elevation::request(elevation::Capability::filesystem, helper));
    // Granted, and the console's own root is what the process now sees.
    struct stat root_probe
    {
    };
    const bool granted = g_status == 0 && stat(kDataParent, &root_probe) == 0;
    // Elevation leaves the effective group apart from the real one, and the
    // OpenGL runtime turns its shader cache off for such a process.
    const bool group_matched = getegid() == getgid() || setegid(getgid()) == 0;

    if (granted)
    {
        // The console's root has no /app0: the sandbox mounts it from the
        // install folder (or from the image the app was installed as).
        char probe[160];
        std::snprintf(probe, sizeof(probe), "%s/eboot.bin", kInstallDir);
        std::snprintf(g_app_dir, sizeof(g_app_dir), "%s",
                      is_file(probe) ? kInstallDir : kSandboxApp);
        std::snprintf(g_data_dir, sizeof(g_data_dir), "%s", kDataDir);
        mkdir(kDataDir, 0777);
        for (const char *folder : {"catalog", "config", "logs"})
        {
            char path[160];
            std::snprintf(path, sizeof(path), "%s/%s", kDataDir, folder);
            mkdir(path, 0777);
        }
    }
    for (int i = 0; i < RADIO_FILE_COUNT; ++i)
    {
        if (granted)
            std::snprintf(g_paths[i], sizeof(g_paths[i]), "%s/%s/%s", kDataDir, kFiles[i].folder,
                          kFiles[i].name);
        else
            std::snprintf(g_paths[i], sizeof(g_paths[i]), "/download0/%s", kFiles[i].name);
    }
    open_log(g_paths[RADIO_FILE_LOG]);

    char line[320];
    std::snprintf(line, sizeof(line),
                  "[RADIO] storage title=%s status=%d app=%s data=%s uid=%d/%d gid=%d/%d "
                  "group_matched=%d\n",
                  kTitleId, g_status, g_app_dir, g_data_dir, static_cast<int>(getuid()),
                  static_cast<int>(geteuid()), static_cast<int>(getgid()),
                  static_cast<int>(getegid()), group_matched ? 1 : 0);
    say(line);
    std::snprintf(line, sizeof(line), "[RADIO] system modules kept: %d/4 (0x%x 0x%x 0x%x 0x%x)\n",
                  modules_loaded, static_cast<unsigned>(modules[0]),
                  static_cast<unsigned>(modules[1]), static_cast<unsigned>(modules[2]),
                  static_cast<unsigned>(modules[3]));
    say(line);

    if (granted)
    {
        // First start with filesystem access: what an earlier version kept in
        // the sandbox comes along. The sandbox copy stays where it is.
        for (const radio_file_t file : {RADIO_FILE_FAVORITES, RADIO_FILE_CATALOG})
        {
            if (bring_over(kFiles[file].name, g_paths[file]))
            {
                std::snprintf(line, sizeof(line), "[RADIO] storage brought over %s\n",
                              kFiles[file].name);
                say(line);
            }
        }
    }
}

int radio_storage_status(void)
{
    return g_status;
}

const char *radio_storage_app_dir(void)
{
    return g_app_dir;
}

const char *radio_storage_data_dir(void)
{
    return g_data_dir;
}

const char *radio_storage_file(radio_file_t file)
{
    return g_paths[file];
}
