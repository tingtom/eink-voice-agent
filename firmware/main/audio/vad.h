#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

void vad_init(void);
int32_t vad_compute_energy(const int16_t *samples, size_t count);
bool vad_process_frame(const int16_t *samples, size_t count);
bool vad_is_active(void);
void vad_reset(void);
