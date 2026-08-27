#ifndef OGG_PAGE_H
#define OGG_PAGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OGG_PAGE_FIXED_HEADER_SIZE 27U
#define OGG_PAGE_MAX_SEGMENTS 255U
#define OGG_PAGE_MAX_BODY_SIZE (OGG_PAGE_MAX_SEGMENTS * 255U)
#define OGG_PAGE_MAX_SIZE \
    (OGG_PAGE_FIXED_HEADER_SIZE + OGG_PAGE_MAX_SEGMENTS + OGG_PAGE_MAX_BODY_SIZE)

typedef enum {
    OGG_PAGE_OK = 0,
    OGG_PAGE_ERR_ARGUMENT = -1,
    OGG_PAGE_ERR_CAPTURE = -2,
    OGG_PAGE_ERR_VERSION = -3,
    OGG_PAGE_ERR_FLAGS = -4,
    OGG_PAGE_ERR_CHECKSUM = -5,
    OGG_PAGE_ERR_CALLBACK = -6,
    OGG_PAGE_ERR_TRUNCATED = -7
} ogg_page_result_t;

typedef struct {
    const uint8_t * data;
    size_t size;
    const uint8_t * laces;
    size_t lace_count;
    const uint8_t * body;
    size_t body_size;
    uint64_t granule_position;
    uint32_t stream_serial;
    uint32_t sequence;
    uint8_t flags;
} ogg_page_t;

/* Page data remains valid only for the duration of the callback. */
typedef int (*ogg_page_fn)(const ogg_page_t * page, void * user_data);

/* Caller-owned state; callbacks receive only complete, CRC-validated pages. */
typedef struct {
    uint8_t data[OGG_PAGE_MAX_SIZE];
    size_t used;
    size_t target;
    uint8_t stage;
    ogg_page_result_t error;
    ogg_page_fn on_page;
    void * user_data;
} ogg_page_parser_t;

void ogg_page_init(ogg_page_parser_t * parser,
                   ogg_page_fn on_page, void * user_data);
void ogg_page_reset(ogg_page_parser_t * parser);
ogg_page_result_t ogg_page_feed(ogg_page_parser_t * parser,
                                const void * data, size_t size);
ogg_page_result_t ogg_page_finish(ogg_page_parser_t * parser);

#ifdef __cplusplus
}
#endif

#endif
