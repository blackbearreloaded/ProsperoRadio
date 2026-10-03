// ProsperoRadio - HTTP for the radio service, on libcurl.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>
#include <stdint.h>

// Every request the service makes (the catalogue and the audio streams) goes
// through these: libcurl with OpenSSL, certificates checked against the
// console's own list of authorities. The system's HTTP library is not used:
// with filesystem access the app runs under the system's identity, and that
// library then refuses every public certificate.
//
// A template holds the defaults, a connection keeps a socket and its TLS
// session for the requests made on it, a request is one GET. Ids are small
// positive numbers; a negative return is a failure. Failures of a transfer
// are -(10000 + the libcurl code): -10028 is a timeout, -10060 a certificate
// that could not be verified, -10042 an abort.

// Sets libcurl up; call once before anything else. 0 or a failure.
int radio_http_init(void);

int radio_http_create_template(const char *user_agent);
int radio_http_delete_template(int template_id);

int radio_http_create_connection(int template_id);
int radio_http_delete_connection(int connection_id);

int radio_http_create_request(int connection_id, const char *url);
int radio_http_delete_request(int request_id);
int radio_http_add_header(int request_id, const char *name, const char *value);

// On a template, a connection or a request. Times are in microseconds; the
// connect limit covers the name lookup too.
int radio_http_set_redirect(int id, int enabled);
int radio_http_set_connect_timeout(int id, uint32_t usec);
int radio_http_set_receive_timeout(int id, uint32_t usec);

// Sends the request and waits for the response's headers.
int radio_http_send(int request_id);
int radio_http_status(int request_id, int *status_code);
// The final response's raw headers; they stay valid until the request is deleted.
int radio_http_headers(int request_id, char **headers, size_t *size);
// Up to `size` bytes of the body: waits for at least one, 0 at its end.
int radio_http_read(int request_id, void *data, size_t size);
// May be called from another thread: the request's send or read gives up.
int radio_http_abort(int request_id);
