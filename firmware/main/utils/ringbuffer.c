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
    rb->mutex = xSemaphoreCreateMutex();
    if (!rb->mutex) {
        free(rb->buffer);
        rb->buffer = NULL;
        return false;
    }
    return true;
}

void ringbuffer_free(ringbuffer_t *rb)
{
    if (rb->mutex) {
        vSemaphoreDelete(rb->mutex);
        rb->mutex = NULL;
    }
    free(rb->buffer);
    rb->buffer = NULL;
}

bool ringbuffer_write(ringbuffer_t *rb, const int16_t *data, size_t len)
{
    if (!rb->mutex || !rb->buffer) return false;
    if (xSemaphoreTake(rb->mutex, portMAX_DELAY) != pdTRUE) return false;
    if (rb->count + len > rb->size) {
        xSemaphoreGive(rb->mutex);
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        rb->buffer[rb->head] = data[i];
        rb->head = (rb->head + 1) % rb->size;
    }
    rb->count += len;
    xSemaphoreGive(rb->mutex);
    return true;
}

bool ringbuffer_read(ringbuffer_t *rb, int16_t *data, size_t len)
{
    if (!rb->mutex || !rb->buffer) return false;
    if (xSemaphoreTake(rb->mutex, portMAX_DELAY) != pdTRUE) return false;
    if (rb->count < len) {
        xSemaphoreGive(rb->mutex);
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        data[i] = rb->buffer[rb->tail];
        rb->tail = (rb->tail + 1) % rb->size;
    }
    rb->count -= len;
    xSemaphoreGive(rb->mutex);
    return true;
}

size_t ringbuffer_available(const ringbuffer_t *rb)
{
    if (!rb->mutex || !rb->buffer) return 0;
    if (xSemaphoreTake(rb->mutex, portMAX_DELAY) != pdTRUE) return 0;
    size_t c = rb->count;
    xSemaphoreGive(rb->mutex);
    return c;
}

void ringbuffer_clear(ringbuffer_t *rb)
{
    if (!rb->mutex || !rb->buffer) return;
    if (xSemaphoreTake(rb->mutex, portMAX_DELAY) != pdTRUE) return;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
    xSemaphoreGive(rb->mutex);
}
