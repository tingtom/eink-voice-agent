#include "esp_log.h"
#include "app_config.h"

static const char *TAG = "WAKE_WORD";

static float sensitivity = 0.7f;
static bool initialized = false;

void wake_word_init(void)
{
    ESP_LOGI(TAG, "Wake word model: '%s' (sensitivity=%.1f)", WAKE_WORD, sensitivity);
    ESP_LOGW(TAG, "Wake word stub loaded - replace with actual Edge Impulse model");
    initialized = true;
}

bool wake_word_detect(const int16_t *audio, size_t samples)
{
    (void)audio;
    (void)samples;
    return false;
}

void wake_word_set_sensitivity(float sens)
{
    sensitivity = sens;
    ESP_LOGI(TAG, "Sensitivity set to %.1f", sensitivity);
}
