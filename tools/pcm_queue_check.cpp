// PSRadio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pcm_queue.hpp"

#include <assert.h>

int main(void)
{
    pcm_queue_state_t queue;
    pcm_queue_init(&queue, 4U);

    size_t index = 99U;
    assert(!pcm_queue_ready(&queue, 2U, false));
    assert(pcm_queue_push(&queue, &index) && index == 0U);
    assert(!pcm_queue_ready(&queue, 2U, false));
    assert(pcm_queue_ready(&queue, 2U, true));
    assert(pcm_queue_push(&queue, &index) && index == 1U);
    assert(pcm_queue_ready(&queue, 2U, false));
    assert(pcm_queue_pop(&queue, &index) && index == 0U);
    assert(pcm_queue_pop(&queue, &index) && index == 1U);
    assert(!pcm_queue_pop(&queue, &index));

    assert(pcm_queue_push(&queue, &index) && index == 2U);
    assert(pcm_queue_push(&queue, &index) && index == 3U);
    assert(pcm_queue_push(&queue, &index) && index == 0U);
    assert(pcm_queue_push(&queue, &index) && index == 1U);
    assert(!pcm_queue_push(&queue, &index));
    return 0;
}
