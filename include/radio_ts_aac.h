#ifndef RADIO_TS_AAC_H
#define RADIO_TS_AAC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RADIO_TS_PACKET_BYTES 188U
#define RADIO_TS_PSI_BYTES 1024U

typedef enum {
    RADIO_TS_AAC_OK = 0,
    RADIO_TS_AAC_INVALID = -1,
    RADIO_TS_AAC_MALFORMED = -2,
    RADIO_TS_AAC_UNSUPPORTED = -3,
    RADIO_TS_AAC_CALLBACK = -4
} radio_ts_aac_result_t;

typedef int (*radio_ts_aac_output_fn)(const uint8_t * data, size_t size,
                                      void * user_data);

typedef struct {
    uint8_t data[RADIO_TS_PSI_BYTES];
    size_t size;
    size_t expected;
} radio_ts_psi_t;

typedef struct {
    radio_ts_aac_output_fn output;
    void * user_data;
    uint8_t packet[RADIO_TS_PACKET_BYTES];
    size_t packet_size;
    radio_ts_psi_t pat;
    radio_ts_psi_t pmt;
    uint16_t pmt_pid;
    uint16_t aac_pid;
    uint32_t pes_remaining;
    uint8_t pes_started;
} radio_ts_aac_parser_t;

void radio_ts_aac_init(radio_ts_aac_parser_t * parser,
                       radio_ts_aac_output_fn output, void * user_data);
void radio_ts_aac_reset(radio_ts_aac_parser_t * parser);
radio_ts_aac_result_t radio_ts_aac_feed(radio_ts_aac_parser_t * parser,
                                        const uint8_t * data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
