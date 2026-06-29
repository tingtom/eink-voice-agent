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
#define TFLITE_MODEL_ARENA_SIZE 130284  // Reduced by 32KB to make room for audio_buf in BSS
#include "model_ops.h"
#include "model_metadata.h"
#include "model_mfe.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

static const char *TAG = "WAKE_WORD";

#define INPUT_QUANT_SCALE  0.00390625f
#define INPUT_QUANT_ZP     (-128)

// audio_buf in static BSS to avoid heap fragmentation (32KB contiguous unavailable)
static int16_t audio_buf[MFE_INPUT_SAMPLES];
static size_t audio_count = 0;

// Arena + FFT buffers in static BSS.  Total ~134KB BSS, leaving ~78KB heap.
static uint8_t tflite_arena[TFLITE_MODEL_ARENA_SIZE];

static const tflite::Model *tflite_model = NULL;
static tflite::MicroMutableOpResolver<7> resolver;
static tflite::MicroInterpreter *interpreter = NULL;
static TfLiteTensor *input_tensor = NULL;
static TfLiteTensor *output_tensor = NULL;

static float re_buf[MFE_FFT_SIZE];
static float im_buf[MFE_FFT_SIZE];

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

// Stream MFE: compute preemphasis on-the-fly per frame, quantize directly
// to int8 output.  Eliminates preemph_buf (64KB) and feat_buf (16KB).
static int extract_mfe(const int16_t *samples, int8_t *features_out)
{
    const float noise_floor = MFE_NOISE_FLOOR * -1.0f;
    const float noise_scale = 1.0f / (noise_floor + 12.0f);
    const float inv_scale = 1.0f / INPUT_QUANT_SCALE;
    const int zp = INPUT_QUANT_ZP;

    for (int f = 0; f < MFE_NUM_FRAMES; f++) {
        int start = f * MFE_FRAME_STRIDE;
        for (int i = 0; i < MFE_FFT_SIZE; i++) {
            int idx = start + i;
            float cur = (float)samples[idx] / 32768.0f;
            float prev = (idx > 0) ? 0.98f * (float)samples[idx - 1] / 32768.0f : 0.0f;
            re_buf[i] = (cur - prev) * mfe_hann_window[i];
        }
        memset(im_buf, 0, MFE_FFT_SIZE * sizeof(float));
        fft_256(re_buf, im_buf);

        for (int m = 0; m < MFE_NUM_FILTERS; m++) {
            float sum = 0.0f;
            for (int k = 0; k < 129; k++) {
                float power = (1.0f / MFE_FFT_SIZE) * (re_buf[k] * re_buf[k] + im_buf[k] * im_buf[k]);
                sum += mfe_filterbank[m][k] * power;
            }
            if (sum < 1e-30f) sum = 1e-30f;
            float db_val = 10.0f * log10f(sum);

            float normalized = (db_val + noise_floor) * noise_scale;
            if (normalized < 0.0f) normalized = 0.0f;
            if (normalized > 1.0f) normalized = 1.0f;
            normalized = roundf(normalized * 256.0f) / 256.0f;

            int q = (int)(roundf(normalized * inv_scale)) + zp;
            if (q < -128) q = -128;
            if (q > 127) q = 127;
            features_out[f * MFE_NUM_FILTERS + m] = (int8_t)q;
        }
    }
    return MFE_NUM_FRAMES * MFE_NUM_FILTERS;
}

static bool setup_tflite(void)
{
    tflite_model = tflite::GetModel(tflite_learn_1037720_5_model);
    if (!tflite_model) {
        ESP_LOGE(TAG, "Failed to get model");
        return false;
    }
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

    TfLiteStatus alloc_status = interpreter->AllocateTensors();
    if (alloc_status != kTfLiteOk) {
        ESP_LOGE(TAG, "Failed to allocate tensors: status=%d", alloc_status);
        return false;
    }

    input_tensor = interpreter->input(0);
    output_tensor = interpreter->output(0);

    if (!input_tensor || !output_tensor) {
        ESP_LOGE(TAG, "Failed to get input/output tensors");
        return false;
    }

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

    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    ESP_LOGI(TAG, "Free heap: %zu bytes (audio_buf in BSS, arena=%d)",
             free_heap, TFLITE_MODEL_ARENA_SIZE);

    if (!setup_tflite()) {
        ESP_LOGE(TAG, "TFLite setup failed");
        return;
    }

    initialized = true;
    ESP_LOGI(TAG, "Wake word initialized");
}

extern "C" void wake_word_deinit(void)
{
    initialized = false;
    interpreter = NULL;
    input_tensor = NULL;
    output_tensor = NULL;

    audio_count = 0;
    ESP_LOGI(TAG, "Wake word deinitialized");
}

extern "C" bool wake_word_detect(const int16_t *audio, size_t samples)
{
    if (!initialized) {
        ESP_LOGE(TAG, "wake_word_detect called but not initialized");
        return false;
    }

    size_t to_copy = samples;
    if (audio_count + to_copy > MFE_INPUT_SAMPLES) {
        to_copy = MFE_INPUT_SAMPLES - audio_count;
    }
    memcpy(audio_buf + audio_count, audio, to_copy * sizeof(int16_t));
    audio_count += to_copy;

    if (audio_count < MFE_INPUT_SAMPLES) {
        return false;
    }

    ESP_LOGD(TAG, "MFE buffer full (%d samples), running inference", MFE_INPUT_SAMPLES);
    int8_t *input_data = input_tensor->data.int8;
    extract_mfe(audio_buf, input_data);

    ESP_LOGI(TAG, "Running inference (%d samples accumulated)", MFE_INPUT_SAMPLES);
    if (!interpreter) {
        ESP_LOGE(TAG, "Interpreter is null");
        audio_count = 0;
        return false;
    }
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

extern "C" void wake_word_reset(void)
{
    audio_count = 0;
    ESP_LOGD(TAG, "Wake word buffer reset");
}
