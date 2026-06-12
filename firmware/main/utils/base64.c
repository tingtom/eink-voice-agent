#include <stdint.h>
#include "base64.h"

static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t base64_encode(const uint8_t *input, size_t len, char *output, size_t out_len)
{
    size_t out = 0;
    for (size_t i = 0; i < len; i += 3) {
        if (out + 4 > out_len) return 0;
        uint32_t val = (uint32_t)input[i] << 16;
        if (i + 1 < len) val |= (uint32_t)input[i + 1] << 8;
        if (i + 2 < len) val |= input[i + 2];
        output[out++] = b64[(val >> 18) & 0x3F];
        output[out++] = b64[(val >> 12) & 0x3F];
        output[out++] = (i + 1 < len) ? b64[(val >> 6) & 0x3F] : '=';
        output[out++] = (i + 2 < len) ? b64[val & 0x3F] : '=';
    }
    if (out < out_len) output[out] = '\0';
    return out;
}

size_t base64_decode(const char *input, size_t len, uint8_t *output, size_t out_len)
{
    size_t out = 0;
    uint8_t d[256] = {0};
    for (int i = 0; i < 64; i++) d[(uint8_t)b64[i]] = i;
    for (size_t i = 0; i < len && input[i] != '='; i += 4) {
        if (out + 3 > out_len) return 0;
        uint32_t val = (d[(uint8_t)input[i]] << 18) |
                       (d[(uint8_t)input[i + 1]] << 12) |
                       (d[(uint8_t)input[i + 2]] << 6) |
                       d[(uint8_t)input[i + 3]];
        output[out++] = (val >> 16) & 0xFF;
        if (input[i + 2] != '=') output[out++] = (val >> 8) & 0xFF;
        if (input[i + 3] != '=') output[out++] = val & 0xFF;
    }
    return out;
}
