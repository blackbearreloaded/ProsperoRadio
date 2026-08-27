#ifndef ICY_METADATA_H
#define ICY_METADATA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*icy_metadata_read_fn)(void * context, void * data, size_t size);

typedef struct {
    icy_metadata_read_fn read;
    void * context;
    size_t interval;
    size_t audio_remaining;
    size_t metadata_remaining;
} icy_metadata_reader_t;

size_t icy_metadata_interval_from_headers(const char * headers, size_t size);
void icy_metadata_reader_init(icy_metadata_reader_t * reader,
                              icy_metadata_read_fn read, void * context,
                              size_t interval);
int icy_metadata_read(void * context, void * data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
