// Base64 encoding for audio streaming
#pragma once
#include <stddef.h>
size_t base64_encode(const uint8_t *input, size_t len, char *output, size_t out_len);
size_t base64_decode(const char *input, size_t len, uint8_t *output, size_t out_len);
