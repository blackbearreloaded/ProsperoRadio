// PS5 Radio - Native PlayStation 5 radio application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stddef.h>

struct pcm_queue_state_t {
    size_t capacity;
    size_t read;
    size_t count;
};

inline void pcm_queue_init(pcm_queue_state_t * queue, size_t capacity)
{
    queue->capacity = capacity;
    queue->read = 0;
    queue->count = 0;
}

inline bool pcm_queue_push(pcm_queue_state_t * queue, size_t * index)
{
    if(queue->count == queue->capacity) return false;
    *index = (queue->read + queue->count) % queue->capacity;
    ++queue->count;
    return true;
}

inline bool pcm_queue_pop(pcm_queue_state_t * queue, size_t * index)
{
    if(queue->count == 0) return false;
    *index = queue->read;
    queue->read = (queue->read + 1U) % queue->capacity;
    --queue->count;
    return true;
}

inline bool pcm_queue_ready(const pcm_queue_state_t * queue,
                            size_t target, bool input_finished)
{
    return queue->count != 0U &&
           (queue->count >= target || input_finished);
}
