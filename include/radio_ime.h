#ifndef RADIO_IME_H
#define RADIO_IME_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*radio_ime_result_fn)(const char * text, void * user_data);

bool radio_ime_init(void);
void radio_ime_request(const char * initial_text, radio_ime_result_fn callback,
                       void * user_data);
void radio_ime_poll(void);
void radio_ime_cancel(void);
void radio_ime_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
