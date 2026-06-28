#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "app_config.h"
#include "app_config.h"
#include "sdcard.h"
#include "speaker_driver.h"
#include "http_client.h"
#include "wifi_manager.h"
#include "recordings.h"

static const char *TAG = "RECORDINGS";

// ── Binary .meta format ─────────────────────────────
// [ts:u32][duration_ms:u32][type:u8][status:u8][text_len:u16][text...]
#define META_HEADER_SIZE 10  // 4+4+1+1+2

// ── In-memory index ────────────────────────────────
static recording_info_t notes[REC_MAX_NOTES];
static int note_count = 0;

// ── Recording state ────────────────────────────────
static FILE *rec_file = NULL;
static char rec_name[REC_NAME_LEN];
static bool recording = false;
static rec_type_t rec_type;
static uint32_t rec_start_us;
static size_t rec_bytes_written = 0;
static uint32_t rec_writes = 0;

// ── Sync state ─────────────────────────────────────
static bool sync_busy = false;
static int sync_current = -1;
static SemaphoreHandle_t sync_sem = NULL;

// ── Helpers ────────────────────────────────────────

static void ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        mkdir(path, 0777);
    }
}

static void make_rec_path(const char *name, const char *ext, char *out, size_t sz)
{
    snprintf(out, sz, REC_DIR "/%s%s", name, ext);
}

static uint32_t read_counter(void)
{
    char path[64];
    snprintf(path, sizeof(path), REC_DIR "/counter");
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint32_t val = 0;
    fread(&val, sizeof(val), 1, f);
    fclose(f);
    return val;
}

static void write_counter(uint32_t val)
{
    char path[64];
    snprintf(path, sizeof(path), REC_DIR "/counter");
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(&val, sizeof(val), 1, f);
    fclose(f);
}

// ── Index rebuild ──────────────────────────────────

static void rebuild_index(void)
{
    note_count = 0;
    DIR *dir = opendir(REC_DIR);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && note_count < REC_MAX_NOTES) {
        const char *d = entry->d_name;
        if (strncmp(d, "rec_", 4) != 0) continue;
        char *dot = strrchr(d, '.');
        if (!dot || strcmp(dot, ".pcm") != 0) continue;

        // Extract name without .pcm
        size_t len = dot - d;
        if (len >= REC_NAME_LEN) len = REC_NAME_LEN - 1;
        memcpy(notes[note_count].name, d, len);
        notes[note_count].name[len] = '\0';

        // Load .meta
        char meta_path[64];
        make_rec_path(notes[note_count].name, ".meta", meta_path, sizeof(meta_path));
        FILE *mf = fopen(meta_path, "rb");
        if (mf) {
            uint8_t hdr[META_HEADER_SIZE + REC_TEXT_MAX];
            size_t got = fread(hdr, 1, META_HEADER_SIZE, mf);
            if (got == META_HEADER_SIZE) {
                uint32_t ts = 0;
                ts |= (uint32_t)hdr[0];
                ts |= (uint32_t)hdr[1] << 8;
                ts |= (uint32_t)hdr[2] << 16;
                ts |= (uint32_t)hdr[3] << 24;
                notes[note_count].timestamp = ts;

                uint32_t dur = 0;
                dur |= (uint32_t)hdr[4];
                dur |= (uint32_t)hdr[5] << 8;
                dur |= (uint32_t)hdr[6] << 16;
                dur |= (uint32_t)hdr[7] << 24;
                notes[note_count].duration_ms = dur;

                notes[note_count].type = (rec_type_t)hdr[8];
                notes[note_count].status = (rec_status_t)hdr[9];

                uint16_t tlen = (uint16_t)hdr[10] | ((uint16_t)hdr[11] << 8);
                if (tlen > 0 && tlen <= REC_TEXT_MAX) {
                    size_t tgot = fread(notes[note_count].text, 1, tlen, mf);
                    notes[note_count].text[tgot] = '\0';
                } else {
                    notes[note_count].text[0] = '\0';
                }
            } else {
                // Corrupt meta — treat as empty
                notes[note_count].timestamp = 0;
                notes[note_count].duration_ms = 0;
                notes[note_count].type = REC_TYPE_NOTE;
                notes[note_count].status = REC_STATUS_RAW;
                notes[note_count].text[0] = '\0';
            }
            fclose(mf);
        } else {
            notes[note_count].timestamp = 0;
            notes[note_count].duration_ms = 0;
            notes[note_count].type = REC_TYPE_NOTE;
            notes[note_count].status = REC_STATUS_RAW;
            notes[note_count].text[0] = '\0';
        }
        note_count++;
    }
    closedir(dir);
}

// ── Write metadata to SD ───────────────────────────

static void write_meta(const recording_info_t *n)
{
    char path[64];
    make_rec_path(n->name, ".meta", path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (!f) return;

    uint8_t hdr[META_HEADER_SIZE + REC_TEXT_MAX];
    hdr[0]  = n->timestamp & 0xff;
    hdr[1]  = (n->timestamp >> 8) & 0xff;
    hdr[2]  = (n->timestamp >> 16) & 0xff;
    hdr[3]  = (n->timestamp >> 24) & 0xff;
    hdr[4]  = n->duration_ms & 0xff;
    hdr[5]  = (n->duration_ms >> 8) & 0xff;
    hdr[6]  = (n->duration_ms >> 16) & 0xff;
    hdr[7]  = (n->duration_ms >> 24) & 0xff;
    hdr[8]  = (uint8_t)n->type;
    hdr[9]  = (uint8_t)n->status;
    size_t tlen = strlen(n->text);
    if (tlen > REC_TEXT_MAX) tlen = REC_TEXT_MAX;
    hdr[10] = tlen & 0xff;
    hdr[11] = (tlen >> 8) & 0xff;
    fwrite(hdr, 1, META_HEADER_SIZE, f);
    if (tlen > 0) {
        fwrite(n->text, 1, tlen, f);
    }
    fclose(f);
}

// ── Public API ─────────────────────────────────────

bool recordings_init(void)
{
    if (!sdcard_mount()) {
        ESP_LOGE(TAG, "SD card mount failed — no offline recordings");
        return false;
    }

    ensure_dir(REC_DIR);
    rebuild_index();
    ESP_LOGI(TAG, "%d recordings found on SD card", note_count);

    // Compute capacity estimate
    uint32_t cap = recording_capacity_seconds();
    ESP_LOGI(TAG, "~%" PRIu32 "s recording space remaining", cap);

    sync_sem = xSemaphoreCreateBinary();
    return true;
}

void recordings_init_audio(void)
{
    rec_bytes_written = 0;
    rec_writes = 0;
    if (rec_file) {
        fclose(rec_file);
        rec_file = NULL;
    }
    recording = false;
    ESP_LOGI(TAG, "Audio recording state initialized");
}

// ── Recording ───────────────────────────────────────

bool recording_start(rec_type_t type)
{
    if (recording) return false;
    if (!sdcard_is_mounted()) {
        ESP_LOGW(TAG, "SD not mounted, cannot record");
        if (!sdcard_mount()) return false;
    }

    uint32_t id = read_counter() + 1;
    write_counter(id);

    snprintf(rec_name, sizeof(rec_name), "rec_%05" PRIu32, id);
    rec_type = type;

    char path[64];
    make_rec_path(rec_name, ".pcm", path, sizeof(path));
    rec_file = fopen(path, "wb");
    if (!rec_file) {
        ESP_LOGE(TAG, "Failed to open %s for writing", path);
        return false;
    }

    recording = true;
    rec_start_us = (uint32_t)(esp_timer_get_time());
    rec_bytes_written = 0;
    rec_writes = 0;
    ESP_LOGI(TAG, "Recording %s (type=%d) to %s",
             rec_name, (int)rec_type, path);
    return true;
}

void recording_write_audio(const int16_t *data, size_t samples)
{
    if (!recording || !rec_file) return;
    size_t written = fwrite(data, sizeof(int16_t), samples, rec_file);
    rec_writes++;
    rec_bytes_written += written * sizeof(int16_t);
    if (written != samples) {
        ESP_LOGE(TAG, "write short: samples=%u written=%u total_writes=%u total_bytes=%u",
                 (unsigned)samples, (unsigned)written, (unsigned)rec_writes, (unsigned)rec_bytes_written);
    }
    if (rec_writes % 50 == 0) {
        ESP_LOGI(TAG, "recording progress: writes=%u bytes=%u",
                 (unsigned)rec_writes, (unsigned)rec_bytes_written);
    }
}

void recording_stop(void)
{
    if (!recording) return;

    if (rec_file) {
        fclose(rec_file);
        rec_file = NULL;
    }

    uint32_t wall_ms = ((uint32_t)esp_timer_get_time() - rec_start_us) / 1000;
    uint32_t audio_ms = (uint32_t)((uint64_t)rec_bytes_written * 1000 / PCM_BYTES_PER_SEC);

    ESP_LOGI(TAG, "Write stats for %s: writes=%u bytes=%u wall=%lums audio=%lums",
             rec_name, (unsigned)rec_writes, (unsigned)rec_bytes_written,
             (unsigned long)wall_ms, (unsigned long)audio_ms);

    // Add to index
    if (note_count < REC_MAX_NOTES) {
        recording_info_t *n = &notes[note_count];
        strncpy(n->name, rec_name, REC_NAME_LEN - 1);
        n->name[REC_NAME_LEN - 1] = '\0';
        n->timestamp = (uint32_t)(esp_timer_get_time() / 1000000);
        n->duration_ms = audio_ms;
        n->type = rec_type;
        n->status = REC_STATUS_RAW;
        n->text[0] = '\0';
        write_meta(n);
        note_count++;
        ESP_LOGI(TAG, "Saved %s (%lums)", rec_name, (unsigned long)audio_ms);
    }

    recording = false;
}

bool recording_is_active(void) { return recording; }

// ── Listing ─────────────────────────────────────────

int  recording_count(void) { return note_count; }

bool recording_get_info(int idx, recording_info_t *info)
{
    if (idx < 0 || idx >= note_count) return false;
    *info = notes[idx];
    return true;
}

int  recording_pending_sync_count(void)
{
    int n = 0;
    for (int i = 0; i < note_count; i++) {
        if (notes[i].status <= REC_STATUS_TRANSCRIBED) n++;
    }
    return n;
}

// ── Status updates ──────────────────────────────────

void recording_set_text(int idx, const char *text)
{
    if (idx < 0 || idx >= note_count) return;
    strncpy(notes[idx].text, text, REC_TEXT_MAX - 1);
    notes[idx].text[REC_TEXT_MAX - 1] = '\0';
    if (notes[idx].status == REC_STATUS_RAW) {
        notes[idx].status = REC_STATUS_TRANSCRIBED;
    }
    write_meta(&notes[idx]);
}

void recording_set_synced(int idx)
{
    if (idx < 0 || idx >= note_count) return;
    notes[idx].status = REC_STATUS_SYNCED;
    write_meta(&notes[idx]);
}

// ── Capacity ────────────────────────────────────────

uint32_t recording_capacity_seconds(void)
{
    uint64_t free_bytes = sdcard_get_free_bytes();
    if (free_bytes == 0) return 0;
    return (uint32_t)(free_bytes / PCM_BYTES_PER_SEC);
}

uint32_t recording_avg_duration_seconds(void)
{
    if (note_count == 0) return 10; // default estimate
    uint64_t total_ms = 0;
    for (int i = 0; i < note_count; i++) {
        total_ms += notes[i].duration_ms;
    }
    uint32_t avg_s = (uint32_t)(total_ms / note_count / 1000);
    if (avg_s == 0) avg_s = 10; // minimum floor
    return avg_s;
}

// ── Playback ────────────────────────────────────────

void recording_play(int idx)
{
    if (idx < 0 || idx >= note_count) return;

    ESP_LOGI(TAG, "TONE TEST: playing 1000Hz/1s debug tone instead of recording");
    speaker_play_tone(1000, 1000);
}

// ── Sync ────────────────────────────────────────────

static void sync_send_recording(recording_info_t *n)
{
    char path[64];
    make_rec_path(n->name, ".pcm", path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Cannot open %s for sync", path);
        return;
    }

    int16_t buf[PCM_CHUNK_SAMPLES];
    size_t total_sent = 0;

    const char *mode = n->type == REC_TYPE_NOTE ? "note" : "todo";

    while (1) {
        size_t read = fread(buf, sizeof(int16_t), PCM_CHUNK_SAMPLES, f);
        if (read == 0) break;

        char url[256];
        snprintf(url, sizeof(url), "%s/api/device/audio?mode=%s&session_id=sync_%s",
                 HERMES_HTTP_URL, mode, n->name);

        char resp[64];
        esp_err_t ret = http_post_binary(url, (const uint8_t *)buf, read * sizeof(int16_t), resp, sizeof(resp));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "sync: chunk POST failed: %s", esp_err_to_name(ret));
            break;
        }
        total_sent += read;
    }
    fclose(f);

    ESP_LOGI(TAG, "Sent %zu samples from %s", total_sent, n->name);

    // Send end marker
    char url[256];
    snprintf(url, sizeof(url), "%s/api/device/audio/end?mode=%s", HERMES_HTTP_URL, mode);
    char json[128];
    snprintf(json, sizeof(json),
             "{\"session_id\":\"sync_%s\"}", n->name);
    char resp[64];
    http_post_json(url, json, resp, sizeof(resp));
}

static void sync_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Sync started");

    for (int i = 0; i < note_count; i++) {
        if (notes[i].status >= REC_STATUS_SYNCED) continue;

        sync_current = i;
        sync_send_recording(&notes[i]);

        // Wait for response with timeout
        if (xSemaphoreTake(sync_sem, pdMS_TO_TICKS(15000)) == pdTRUE) {
            ESP_LOGI(TAG, "Sync response received for %s", notes[i].name);
        } else {
            ESP_LOGW(TAG, "Sync timeout for %s", notes[i].name);
        }

        write_meta(&notes[i]);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    sync_current = -1;
    sync_busy = false;
    ESP_LOGI(TAG, "Sync complete");
    vTaskDelete(NULL);
}

void recording_sync_start(void)
{
    if (sync_busy) return;
    if (recording_pending_sync_count() == 0) return;
    if (!wifi_is_connected()) return;

    sync_busy = true;
    xTaskCreate(sync_task, "rec_sync", 4096, NULL, 3, NULL);
}

bool recording_sync_is_busy(void) { return sync_busy; }

void recording_sync_handle_response(const char *session, const char *text)
{
    (void)session;
    if (sync_current < 0 || sync_current >= note_count) return;

    strncpy(notes[sync_current].text, text, REC_TEXT_MAX - 1);
    notes[sync_current].text[REC_TEXT_MAX - 1] = '\0';
    if (notes[sync_current].status == REC_STATUS_RAW) {
        notes[sync_current].status = REC_STATUS_TRANSCRIBED;
    }
    notes[sync_current].status = REC_STATUS_SYNCED;

    if (sync_sem) xSemaphoreGive(sync_sem);
}

// ── Delete ──────────────────────────────────────────

void recording_delete(int idx)
{
    if (idx < 0 || idx >= note_count) return;

    char path[64];
    make_rec_path(notes[idx].name, ".pcm", path, sizeof(path));
    remove(path);

    make_rec_path(notes[idx].name, ".meta", path, sizeof(path));
    remove(path);

    // Shift array
    for (int i = idx; i < note_count - 1; i++) notes[i] = notes[i + 1];
    note_count--;
    ESP_LOGI(TAG, "Deleted recording %d", idx);
}
