// ProsperoRadio - Process-level runtime shims for the OpenGL runtime.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Adapted from ps5-opengl native-app/runtime_shims.c: the never-return main
// policy, and libc entry points the clean-room libc shim does not provide but
// the statically linked Mesa runtime references.

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

extern int sceKernelUsleep(uint32_t microseconds);

/* The log is opened by radio_storage_init(), first thing in main: where it
 * goes depends on whether the app was given filesystem access. */

/* The OpenGL runtime hides the system's launch picture when the display opens,
 * a second or more before there is a frame to show: the screen would be black
 * in between. The linker routes every call here (--wrap), and the picture
 * stays until main has presented its first frame. */
extern int __real_sceSystemServiceHideSplashScreen(void);
static int g_splash_released;

int __wrap_sceSystemServiceHideSplashScreen(void)
{
    return g_splash_released ? __real_sceSystemServiceHideSplashScreen() : 0;
}

void radio_release_splash(void)
{
    g_splash_released = 1;
}

/* System modules are found by a path inside the title's sandbox. Once the app
 * has filesystem access its root is the console's, and a module asked for
 * later is not found. The ones the app uses are loaded before that, and kept:
 * a later load of one of them succeeds at once, an unload leaves it alone. */
extern int __real_sceSysmoduleLoadModule(uint16_t id);
extern int __real_sceSysmoduleUnloadModule(uint16_t id);
extern int __real_sceSysmoduleLoadModuleInternal(uint32_t id);
extern int __real_sceSysmoduleUnloadModuleInternal(uint32_t id);

static struct
{
    uint32_t id;
    int internal;
    int result;
    int kept;
} g_modules[] = {
    {0x0096, 0, 0, 0},     /* keyboard dialog */
    {0x0088, 0, 0, 0},     /* MP3 and AAC decoders */
    {0x80000069, 1, 0, 0}, /* Opus decoder */
    {0x80000044, 1, 0, 0}, /* Opus CELT decoder */
};
#define MODULE_COUNT (sizeof(g_modules) / sizeof(g_modules[0]))

static int module_kept(uint32_t id, int internal)
{
    for (unsigned i = 0; i < MODULE_COUNT; ++i)
    {
        if (g_modules[i].id == id && g_modules[i].internal == internal)
            return g_modules[i].kept;
    }
    return 0;
}

/* Returns how many of the modules loaded; results[] gets each load's status. */
int radio_preload_modules(int *results, unsigned capacity)
{
    int loaded = 0;
    for (unsigned i = 0; i < MODULE_COUNT; ++i)
    {
        g_modules[i].result = g_modules[i].internal
                                  ? __real_sceSysmoduleLoadModuleInternal(g_modules[i].id)
                                  : __real_sceSysmoduleLoadModule((uint16_t)g_modules[i].id);
        g_modules[i].kept = g_modules[i].result >= 0;
        loaded += g_modules[i].kept;
        if (i < capacity)
            results[i] = g_modules[i].result;
    }
    return loaded;
}

int __wrap_sceSysmoduleLoadModule(uint16_t id)
{
    return module_kept(id, 0) ? 0 : __real_sceSysmoduleLoadModule(id);
}

int __wrap_sceSysmoduleUnloadModule(uint16_t id)
{
    return module_kept(id, 0) ? 0 : __real_sceSysmoduleUnloadModule(id);
}

int __wrap_sceSysmoduleLoadModuleInternal(uint32_t id)
{
    return module_kept(id, 1) ? 0 : __real_sceSysmoduleLoadModuleInternal(id);
}

int __wrap_sceSysmoduleUnloadModuleInternal(uint32_t id)
{
    return module_kept(id, 1) ? 0 : __real_sceSysmoduleUnloadModuleInternal(id);
}

/* Returning from main or calling exit() crashes a native title; stay alive
 * until the shell closes the title. */
__attribute__((noreturn)) void catchReturnFromMain(int status)
{
    printf("[RADIO] main returned status=%d\n", status);
    fflush(NULL);
    for (;;)
        sceKernelUsleep(100000);
}

void radio_glapi_tls_context_init(void) __asm__("_ZTH23_mesa_glapi_tls_Context");

void radio_glapi_tls_context_init(void)
{
}

__attribute__((noreturn)) void __assert(const char *function, const char *file, int line,
                                        const char *expression)
{
    fprintf(stderr, "[RADIO] assertion failed: %s (%s:%d, %s)\n", expression, file, line, function);
    abort();
}

int mkstemps(char *template_name, int suffix_length)
{
    (void)template_name;
    (void)suffix_length;
    errno = ENOSYS;
    return -1;
}

/* libcurl asks for the user's home folder (for .netrc and the like): there is
 * no user database here. */
struct passwd;
int getpwuid_r(unsigned user, struct passwd *entry, char *buffer, size_t size,
               struct passwd **result)
{
    (void)user;
    (void)entry;
    (void)buffer;
    (void)size;
    *result = NULL;
    return ENOENT;
}

/* What libcurl and OpenSSL (PacBrew builds, made for the payload SDK's libc)
 * ask of the C library that this app's libc does not export. */
__asm__(".text\n"
        ".globl _setjmp\n"
        "_setjmp:\n"
        "    jmp *setjmp@GOTPCREL(%rip)\n"
        ".globl _longjmp\n"
        "_longjmp:\n"
        "    jmp *longjmp@GOTPCREL(%rip)\n");

void closelog(void)
{
}

int dladdr(const void *address, void *info)
{
    (void)address;
    (void)info;
    return 0;
}

unsigned if_nametoindex(const char *name)
{
    (void)name;
    return 0;
}

int pipe2(int descriptors[2], int flags)
{
    if (pipe(descriptors) != 0)
        return -1;
    for (int i = 0; i < 2; ++i)
    {
        if ((flags & O_NONBLOCK) != 0)
            fcntl(descriptors[i], F_SETFL, fcntl(descriptors[i], F_GETFL) | O_NONBLOCK);
        if ((flags & O_CLOEXEC) != 0)
            fcntl(descriptors[i], F_SETFD, FD_CLOEXEC);
    }
    return 0;
}

/* Only QUIC uses these; curl falls back when they are not there. */
int recvmmsg(int socket, void *messages, size_t count, int flags, const void *timeout)
{
    (void)socket;
    (void)messages;
    (void)count;
    (void)flags;
    (void)timeout;
    errno = ENOSYS;
    return -1;
}

int sendmmsg(int socket, void *messages, size_t count, int flags)
{
    (void)socket;
    (void)messages;
    (void)count;
    (void)flags;
    errno = ENOSYS;
    return -1;
}

/* zstd's optional tracing hooks: a begin that returns 0 means "not traced". */
unsigned long long ZSTD_trace_compress_begin(const void *context)
{
    (void)context;
    return 0;
}

void ZSTD_trace_compress_end(unsigned long long trace, const void *record)
{
    (void)trace;
    (void)record;
}

unsigned long long ZSTD_trace_decompress_begin(const void *context)
{
    (void)context;
    return 0;
}

void ZSTD_trace_decompress_end(unsigned long long trace, const void *record)
{
    (void)trace;
    (void)record;
}

/* Certificate dates are checked with this: it has to be right. Days since
 * 1970 to a civil date (Howard Hinnant's algorithm). */
struct tm *gmtime_r(const time_t *when, struct tm *out)
{
    long long seconds = (long long)*when;
    long long days = seconds / 86400;
    long long rest = seconds % 86400;
    if (rest < 0)
    {
        rest += 86400;
        --days;
    }
    memset(out, 0, sizeof(*out));
    out->tm_hour = (int)(rest / 3600);
    out->tm_min = (int)(rest % 3600 / 60);
    out->tm_sec = (int)(rest % 60);
    out->tm_wday = (int)(((days % 7) + 11) % 7);
    const long long z = days + 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const long long doe = z - era * 146097;
    const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long long mp = (5 * doy + 2) / 153;
    const long long month = mp < 10 ? mp + 3 : mp - 9;
    const long long year = yoe + era * 400 + (month <= 2 ? 1 : 0);
    out->tm_mday = (int)(doy - (153 * mp + 2) / 5 + 1);
    out->tm_mon = (int)(month - 1);
    out->tm_year = (int)(year - 1900);
    const int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    static const int before[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    out->tm_yday = before[out->tm_mon] + out->tm_mday - 1 + (leap && out->tm_mon > 1 ? 1 : 0);
    return out;
}

void openlog(const char *identifier, int option, int facility)
{
    (void)identifier;
    (void)option;
    (void)facility;
}

FILE *popen(const char *command, const char *mode)
{
    (void)command;
    (void)mode;
    errno = ENOSYS;
    return NULL;
}

int pclose(FILE *stream)
{
    (void)stream;
    errno = ENOSYS;
    return -1;
}
