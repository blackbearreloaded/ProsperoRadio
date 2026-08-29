// ProsperoRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>

using radio_ime_result_fn = void (*)(const char * text, void * user_data);

bool radio_ime_init(void);
void radio_ime_request(const char * initial_text, radio_ime_result_fn callback,
                       void * user_data);
void radio_ime_poll(void);
void radio_ime_cancel(void);
void radio_ime_shutdown(void);
