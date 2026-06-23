#include <math.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "app_config.h"
#include "wake_word.h"
#include "tflite_learn_1037720_5.h"
#undef TFLITE_MODEL_ARENA_SIZE
#define TFLITE_MODEL_ARENA_SIZE 61440  // 60KB — reduced from 162284
#include "model_ops.h"
#include "model_metadata.h"
#include "model_mfe.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

static const char *TAG = "WAKE_WORD";

#define INPUT_QUANT_SCALE  0.00390625f
#define INPUT_QUANT_ZP     (-128)

static int16_t audio_buf_storage[MFE_INPUT_SAMPLES];
static int16_t *audio_buf = audio_buf_storage;
static size_t audio_count = 0;

static uint8_t tflite_arena_storage[TFLITE_MODEL_ARENA_SIZE];
static uint8_t *tflite_arena = tflite_arena_storage;
static const tflite::Model *tflite_model = NULL;
static tflite::MicroMutableOpResolver<7> resolver;
static tflite::MicroInterpreter *interpreter = NULL;
static TfLiteTensor *input_tensor = NULL;
static TfLiteTensor *output_tensor = NULL;

static float sensitivity = WAKE_WORD_SENSITIVITY;
static bool initialized = false;

static void bit_reverse_256(float *re, float *im)
{
    for (int i = 1, j = 0; i < 255; i++) {
        int bit = 128;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float tr = re[i]; re[i] = re[j]; re[j] = tr;
            float ti = im[i]; im[i] = im[j]; im[j] = ti;
        }
    }
}

static void fft_256(float *re, float *im)
{
    bit_reverse_256(re, im);
    for (int len = 2; len <= 256; len <<= 1) {
        int half = len >> 1;
        float w_re = cosf((float)M_PI / half);
        float w_im = -sinf((float)M_PI / half);
        for (int i = 0; i < 256; i += len) {
            float wr = 1.0f, wi = 0.0f;
            for (int j = 0; j < half; j++) {
                int i0 = i + j;
                int i1 = i0 + half;
                float tr = wr * re[i1] - wi * im[i1];
                float ti = wr * im[i1] + wi * re[i1];
                re[i1] = re[i0] - tr;
                im[i1] = im[i0] - ti;
                re[i0] += tr;
                im[i0] += ti;
                float nwr = wr * w_re - wi * w_im;
                wi = wr * w_im + wi * w_re;
                wr = nwr;
            }
        }
    }
}

static int extract_mfe(const int16_t *samples, int8_t *features_out)
{
    static float re[MFE_FFT_SIZE];
    static float im[MFE_FFT_SIZE];
    static float mel_energy[MFE_NUM_FILTERS];
    static float feat_buf[MFE_NUM_FRAMES * MFE_NUM_FILTERS];

    const float min_power = powf(10.0f, MFE_NOISE_FLOOR / 10.0f);
    const float inv_scale = 1.0f / INPUT_QUANT_SCALE;
    const int zp = INPUT_QUANT_ZP;

    for (int f = 0; f < MFE_NUM_FRAMES; f++) {
        int start = f * MFE_FRAME_STRIDE;
        for (int i = 0; i < MFE_FFT_SIZE; i++) {
            float s = (float)samples[start + i] / 32768.0f;
            re[i] = s * mfe_hann_window[i];
        }
        memset(im, 0, sizeof(im));
        fft_256(re, im);

        for (int m = 0; m < MFE_NUM_FILTERS; m++) {
            float sum = 0.0f;
            for (int k = 0; k < 129; k++) {
                sum += mfe_filterbank[m][k] * (re[k] * re[k] + im[k] * im[k]);
            }
            mel_energy[m] = 10.0f * log10f(fmaxf(sum, min_power));
        }

        float max_db = mel_energy[0];
        for (int m = 1; m < MFE_NUM_FILTERS; m++) {
            if (mel_energy[m] > max_db) max_db = mel_energy[m];
        }

        for (int m = 0; m < MFE_NUM_FILTERS; m++) {
            float normalized = (mel_energy[m] - MFE_NOISE_FLOOR) /
                               (max_db - MFE_NOISE_FLOOR);
            if (normalized < 0.0f) normalized = 0.0f;
            if (normalized > 1.0f) normalized = 1.0f;
            feat_buf[f * MFE_NUM_FILTERS + m] = normalized;
        }
    }

    for (int i = 0; i < MFE_NUM_FRAMES * MFE_NUM_FILTERS; i++) {
        float val = feat_buf[i];
        int q = (int)(roundf(val * inv_scale)) + zp;
        if (q < -128) q = -128;
        if (q > 127) q = 127;
        features_out[i] = (int8_t)q;
    }
    return MFE_NUM_FRAMES * MFE_NUM_FILTERS;
}

static bool setup_tflite(void)
{
    tflite_model = tflite::GetModel(tflite_learn_1037720_5_model);
    if (tflite_model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema version %d != %d",
                 (int)tflite_model->version(), (int)TFLITE_SCHEMA_VERSION);
        return false;
    }

    resolver.AddReshape();
    resolver.AddConv2D();
    resolver.AddDepthwiseConv2D();
    resolver.AddAdd();
    resolver.AddFullyConnected();
    resolver.AddSoftmax();
    resolver.AddMean();

    static tflite::MicroInterpreter static_interp(
        tflite_model, resolver, tflite_arena, TFLITE_MODEL_ARENA_SIZE);
    interpreter = &static_interp;

    if (interpreter->AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "Failed to allocate tensors");
        return false;
    }

    input_tensor = interpreter->input(0);
    output_tensor = interpreter->output(0);

    if ((size_t)input_tensor->bytes != WAKE_WORD_MODEL_INPUT_SIZE) {
        ESP_LOGE(TAG, "Input size mismatch: expected %d, got %zu",
                 WAKE_WORD_MODEL_INPUT_SIZE, input_tensor->bytes);
        return false;
    }

    ESP_LOGI(TAG, "TFLite initialized (arena=%d, input=%zu, output=%zu)",
             TFLITE_MODEL_ARENA_SIZE, input_tensor->bytes, output_tensor->bytes);
    return true;
}

extern "C" void wake_word_init(void)
{
    ESP_LOGI(TAG, "Initializing wake word model: '%s' (sensitivity=%.1f)",
             WAKE_WORD, sensitivity);

    if (!setup_tflite()) {
        ESP_LOGE(TAG, "TFLite setup failed");
        return;
    }

    initialized = true;
    ESP_LOGI(TAG, "Wake word initialized");
}

extern "C" bool wake_word_detect(const int16_t *audio, size_t samples)
{
    if (!initialized || !audio_buf) return false;

    size_t to_copy = samples;
    if (audio_count + to_copy > MFE_INPUT_SAMPLES) {
        to_copy = MFE_INPUT_SAMPLES - audio_count;
    }
    memcpy(audio_buf + audio_count, audio, to_copy * sizeof(int16_t));
    audio_count += to_copy;

    if (audio_count < MFE_INPUT_SAMPLES) {
        return false;
    }

    int8_t *input_data = input_tensor->data.int8;
    extract_mfe(audio_buf, input_data);

    ESP_LOGI(TAG, "Running inference (%d samples accumulated)", MFE_INPUT_SAMPLES);
    if (interpreter->Invoke() != kTfLiteOk) {
        ESP_LOGE(TAG, "Inference failed");
        audio_count = 0;
        return false;
    }

    int8_t *output_data = output_tensor->data.int8;
    float out_scale = output_tensor->params.scale;
    int out_zp = output_tensor->params.zero_point;

    float scores[WAKE_WORD_MODEL_OUTPUT_COUNT];
    for (int i = 0; i < WAKE_WORD_MODEL_OUTPUT_COUNT; i++) {
        scores[i] = ((float)output_data[i] - (float)out_zp) * out_scale;
    }

    audio_count = 0;

    for (int i = 0; i < WAKE_WORD_MODEL_OUTPUT_COUNT; i++) {
        ESP_LOGI(TAG, "  %s: %.4f",
                 i == 0 ? "hi_jeff"
                        : (i == 1 ? "noise" : "unknown"),
                 (double)scores[i]);
    }

    if (scores[WAKE_WORD_LABEL_HI_JEFF] >= sensitivity) {
        ESP_LOGI(TAG, "Wake word detected! (score=%.4f, threshold=%.2f)",
                 (double)scores[WAKE_WORD_LABEL_HI_JEFF], sensitivity);
        return true;
    }

    return false;
}

extern "C" void wake_word_set_sensitivity(float sens)
{
    sensitivity = sens;
    ESP_LOGI(TAG, "Sensitivity set to %.1f", sensitivity);
}
