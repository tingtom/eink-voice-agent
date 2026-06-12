// Wake word detection (Edge Impulse)
#pragma once
#include <stdbool.h>
void wake_word_init(void);
bool wake_word_detect(const int16_t *audio, size_t samples);
void wake_word_set_sensitivity(float sensitivity);
