// SPDX-License-Identifier: GPL-3.0-or-later

#include <assert.h>
#include <stdint.h>

#include "../src/radio_input.c"

int scePadInit(void) { return 0; }
int scePadOpen(int32_t user_id, int32_t port_type, int32_t index, const void * param)
{
    (void)user_id; (void)port_type; (void)index; (void)param;
    return 1;
}
int scePadClose(int32_t handle) { (void)handle; return 0; }
int scePadRead(int32_t handle, void * data, int32_t num)
{
    (void)handle; (void)data; (void)num;
    return 0;
}
int sceUserServiceInitialize(void * init_params) { (void)init_params; return 0; }
int sceUserServiceGetInitialUser(int32_t * user_id) { *user_id = 1; return 0; }
int sceUserServiceTerminate(void) { return 0; }
uint64_t SDL_GetTicks64(void) { return 1000; }

static radio_input_event_t next_event(void)
{
    radio_input_event_t event;
    assert(radio_input_next(&event));
    return event;
}

int main(void)
{
    assert(stick_direction(128, 128) == -1);
    assert(stick_direction(20, 128) == RADIO_INPUT_LEFT);
    assert(stick_direction(235, 128) == RADIO_INPUT_RIGHT);
    assert(stick_direction(128, 20) == RADIO_INPUT_UP);
    assert(stick_direction(128, 235) == RADIO_INPUT_DOWN);
    assert(stick_direction(50, 210) == RADIO_INPUT_DOWN);

    unsigned char sample[PAD_SAMPLE_SIZE] = {0};
    sample[4] = 128;
    sample[5] = 20;
    sample[6] = 128;
    sample[7] = 128;
    sample[76] = 1;
    process_sample(sample);
    radio_input_event_t event = next_event();
    assert(event.key == RADIO_INPUT_UP && event.pressed);

    sample[5] = 128;
    sample[6] = 230;
    process_sample(sample);
    event = next_event();
    assert(event.key == RADIO_INPUT_UP && !event.pressed);
    assert(!radio_input_next(&event));

    sample[6] = 128;
    sample[4] = 230;
    process_sample(sample);
    event = next_event();
    assert(event.key == RADIO_INPUT_RIGHT && event.pressed);
    sample[4] = 128;
    process_sample(sample);
    event = next_event();
    assert(event.key == RADIO_INPUT_RIGHT && !event.pressed);
    assert(!radio_input_next(&event));
    return 0;
}
