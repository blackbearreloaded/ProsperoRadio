#ifndef RADIO_SERVICE_H
#define RADIO_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RADIO_MAX_STATIONS 480
#define RADIO_RANK_NONE UINT16_MAX

typedef struct {
    char uuid[40];
    char name[112];
    char url[512];
    char country[56];
    char country_code[4];
    char state[64];
    char language[80];
    char tags[112];
    char codec[12];
    uint32_t bitrate;
    uint32_t votes;
    uint32_t click_count;
    int32_t click_trend;
    uint16_t popular_rank;
    uint16_t trending_rank;
    uint16_t voted_rank;
} radio_station_t;

typedef enum {
    RADIO_CATALOG_LOADING,
    RADIO_CATALOG_CACHED,
    RADIO_CATALOG_READY,
    RADIO_CATALOG_ERROR
} radio_catalog_state_t;

typedef enum {
    RADIO_PLAYBACK_STOPPED,
    RADIO_PLAYBACK_CONNECTING,
    RADIO_PLAYBACK_BUFFERING,
    RADIO_PLAYBACK_PLAYING,
    RADIO_PLAYBACK_STOPPING,
    RADIO_PLAYBACK_ERROR
} radio_playback_state_t;

typedef struct {
    radio_catalog_state_t catalog_state;
    radio_playback_state_t playback_state;
    unsigned catalog_generation;
    unsigned station_count;
    unsigned playing_index;
    unsigned sample_rate;
    unsigned channels;
    int error_code;
    bool refreshing;
} radio_service_status_t;

bool radio_service_init(void);
void radio_service_shutdown(void);
void radio_service_get_status(radio_service_status_t * out_status);
bool radio_service_get_station(unsigned index, radio_station_t * out_station);
bool radio_service_is_favorite(const char * uuid);
bool radio_service_toggle_favorite(unsigned station_index);
bool radio_service_refresh(void);
void radio_service_play(unsigned station_index);
void radio_service_stop(void);

#ifdef __cplusplus
}
#endif

#endif

