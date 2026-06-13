#include <math.h>
#include <stdbool.h>
#include "esp_log.h"
#include "app_config.h"
#include "mic_driver.h"

static const char *TAG = "VAD";

#define VAD_FRAME_MS      32
#define VAD_FRAME_SAMPLES ((AUDIO_SAMPLE_RATE * VAD_FRAME_MS) / 1000)
#define VAD_ENERGY_HIGH   5000
#define VAD_ENERGY_LOW    2000
#define VAD_HANGOVER      8

static int hangover_count = 0;
static bool voice_active = false;

void vad_init(void)
{
    hangover_count = 0;
    voice_active = false;
    ESP_LOGI(TAG, "VAD initialized (frame=%d samples, threshold_high=%d, threshold_low=%d)",
             VAD_FRAME_SAMPLES, VAD_ENERGY_HIGH, VAD_ENERGY_LOW);
}

int32_t vad_compute_energy(const int16_t *samples, size_t count)
{
    int64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t s = samples[i];
        sum += (int64_t)s * s;
    }
    int32_t rms = (int32_t)(sqrt((double)sum / count) + 0.5);
    return rms;
}

bool vad_process_frame(const int16_t *samples, size_t count)
{
    int32_t energy = vad_compute_energy(samples, count);

    if (energy >= VAD_ENERGY_HIGH) {
        voice_active = true;
        hangover_count = VAD_HANGOVER;
        return true;
    }

    if (voice_active && energy >= VAD_ENERGY_LOW) {
        hangover_count = VAD_HANGOVER;
        return true;
    }

    if (hangover_count > 0) {
        hangover_count--;
        if (hangover_count > 0) return true;
    }

    voice_active = false;
    return false;
}

bool vad_is_active(void)
{
    return voice_active;
}

void vad_reset(void)
{
    hangover_count = 0;
    voice_active = false;
}
