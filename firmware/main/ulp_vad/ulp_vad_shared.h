#pragma once
#include <stdint.h>

#define ULP_VAD_POLL_MS        50
#define ULP_VAD_TICK_INTERVAL  4

#define ULP_WAKE_NONE    0
#define ULP_WAKE_BUTTON  1
#define ULP_WAKE_TIMER   2

#define ULP_FLAG_VAD_ENABLED       (1 << 0)
#define ULP_FLAG_CPU_WAKE_REQUEST  (1 << 1)
#define ULP_FLAG_CPU_ACK          (1 << 2)

typedef struct {
    uint32_t button_state;
    uint32_t battery_raw;
    uint32_t wake_reason;
    int32_t  audio_energy;
    uint32_t flags;
    uint32_t vad_ticks;
} ulp_vad_shared_t;

extern RTC_SLOW_ATTR ulp_vad_shared_t ulp_vad_data;
