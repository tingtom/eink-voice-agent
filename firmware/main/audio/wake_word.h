#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void wake_word_init(void);
bool wake_word_detect(const int16_t *audio, size_t samples);
void wake_word_set_sensitivity(float sensitivity);
void wake_word_reset(void);

#ifdef __cplusplus
}
#endif
