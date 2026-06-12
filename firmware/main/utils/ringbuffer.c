#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ringbuffer.h"

bool ringbuffer_init(ringbuffer_t *rb, size_t size)
{
    rb->buffer = (int16_t *)malloc(size * sizeof(int16_t));
    if (!rb->buffer) return false;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
    return true;
}

void ringbuffer_free(ringbuffer_t *rb)
{
    free(rb->buffer);
    rb->buffer = NULL;
}

bool ringbuffer_write(ringbuffer_t *rb, const int16_t *data, size_t len)
{
    if (rb->count + len > rb->size) return false;
    for (size_t i = 0; i < len; i++) {
        rb->buffer[rb->head] = data[i];
        rb->head = (rb->head + 1) % rb->size;
    }
    rb->count += len;
    return true;
}

bool ringbuffer_read(ringbuffer_t *rb, int16_t *data, size_t len)
{
    if (rb->count < len) return false;
    for (size_t i = 0; i < len; i++) {
        data[i] = rb->buffer[rb->tail];
        rb->tail = (rb->tail + 1) % rb->size;
    }
    rb->count -= len;
    return true;
}

size_t ringbuffer_available(const ringbuffer_t *rb)
{
    return rb->count;
}

void ringbuffer_clear(ringbuffer_t *rb)
{
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
}
