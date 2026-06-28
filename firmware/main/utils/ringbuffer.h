// Ring buffer for audio
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "freertos/semphr.h"
typedef struct {
    int16_t *buffer;
    size_t size;
    size_t head;
    size_t tail;
    size_t count;
    SemaphoreHandle_t mutex;
} ringbuffer_t;
bool ringbuffer_init(ringbuffer_t *rb, size_t size);
void ringbuffer_free(ringbuffer_t *rb);
bool ringbuffer_write(ringbuffer_t *rb, const int16_t *data, size_t len);
bool ringbuffer_read(ringbuffer_t *rb, int16_t *data, size_t len);
size_t ringbuffer_available(const ringbuffer_t *rb);
void ringbuffer_clear(ringbuffer_t *rb);
