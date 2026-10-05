/* ProsperoRadio - fcntl on the console's sockets.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* ---- fcntl on sockets ----------------------------------------------------------------------
 * In a sandboxed title the console's libc refuses fcntl on sockets with EINVAL. curl then fails
 * every connect ("fcntl set CLOEXEC: Invalid argument") and its sockets stay blocking. Linked
 * with --wrap=fcntl, every fcntl call in the app and the archives comes here; only a refused
 * call is treated as a socket. (From the PS5 Native App Boilerplate's console_curl.c.) */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/socket.h>

extern int __real_fcntl(int descriptor, int command, ...);

static int radio_socket_flags(int socket, int command, int value)
{
    enum
    {
        RADIO_SO_NBIO = 0x1200 /* the console's own non-blocking socket option */
    };
    switch (command)
    {
    case F_GETFD:
    case F_SETFD:
        return 0; /* a title never execs another program */
    case F_SETFL:
    {
        const int on = (value & O_NONBLOCK) != 0;
        return setsockopt(socket, SOL_SOCKET, RADIO_SO_NBIO, &on, sizeof(on));
    }
    case F_GETFL:
    {
        int on = 0;
        socklen_t length = sizeof(on);
        if (getsockopt(socket, SOL_SOCKET, RADIO_SO_NBIO, &on, &length) < 0)
            return -1;
        return O_RDWR | (on ? O_NONBLOCK : 0);
    }
    default:
        errno = EINVAL;
        return -1;
    }
}

int __wrap_fcntl(int descriptor, int command, ...)
{
    va_list arguments;
    va_start(arguments, command);
    /* Every command curl and its libraries use takes an int or a pointer. */
    const intptr_t argument = va_arg(arguments, intptr_t);
    va_end(arguments);
    const int result = __real_fcntl(descriptor, command, argument);
    if (result >= 0 || errno != EINVAL)
        return result;
    if (command == F_GETFD || command == F_SETFD || command == F_SETFL || command == F_GETFL)
        return radio_socket_flags(descriptor, command, (int)argument);
    return result;
}
