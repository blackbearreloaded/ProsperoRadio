#ifndef PSRADIO_PCM_QUEUE_H
#define PSRADIO_PCM_QUEUE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    size_t capacity;
    size_t read;
    size_t count;
} pcm_queue_state_t;

static inline void pcm_queue_init(pcm_queue_state_t * queue, size_t capacity)
{
    queue->capacity = capacity;
    queue->read = 0;
    queue->count = 0;
}

static inline bool pcm_queue_push(pcm_queue_state_t * queue, size_t * index)
{
    if(queue->count == queue->capacity) return false;
    *index = (queue->read + queue->count) % queue->capacity;
    ++queue->count;
    return true;
}

static inline bool pcm_queue_pop(pcm_queue_state_t * queue, size_t * index)
{
    if(queue->count == 0) return false;
    *index = queue->read;
    queue->read = (queue->read + 1U) % queue->capacity;
    --queue->count;
    return true;
}

static inline bool pcm_queue_ready(const pcm_queue_state_t * queue,
                                   size_t target, bool input_finished)
{
    return queue->count != 0U &&
           (queue->count >= target || input_finished);
}

#endif
