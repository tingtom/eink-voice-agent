#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "speaker_driver.h"
#include "ws_client.h"

#include "offline_notes.h"

static const char *TAG = "OFFLINE_NOTES";

// ── Note metadata (stored alongside .pcm as .meta) ──────────
// Binary: [ts:u32][synced:u8][text_len:u16][text...]

#define META_HEADER_SIZE 7

typedef struct {
    char     name[NOTE_NAME_LEN];    // "note_00001"
    uint32_t timestamp;
    bool     synced;
    char     text[NOTE_TEXT_MAX];
} note_entry_t;

static note_entry_t notes[MAX_NOTES];
static int note_count = 0;

// Recording state
static FILE *rec_file = NULL;
static char rec_name[NOTE_NAME_LEN];
static bool recording = false;

// Sync state
static bool sync_busy = false;
static int sync_current = -1;
static SemaphoreHandle_t sync_sem = NULL;

// ── Filesystem helpers ──────────────────────────────────────

static void ensure_notes_dir(void)
{
    struct stat st;
    if (stat(NOTES_DIR "/notes", &st) != 0) {
        mkdir(NOTES_DIR "/notes", 0777);
    }
}

static void make_note_path(const char *name, const char *ext, char *out, size_t sz)
{
    snprintf(out, sz, NOTES_DIR "/notes/%s%s", name, ext);
}

static uint32_t read_counter(void)
{
    FILE *f = fopen(NOTES_DIR "/counter", "rb");
    if (!f) return 0;
    uint32_t val = 0;
    fread(&val, sizeof(val), 1, f);
    fclose(f);
    return val;
}

static void write_counter(uint32_t val)
{
    FILE *f = fopen(NOTES_DIR "/counter", "wb");
    if (!f) return;
    fwrite(&val, sizeof(val), 1, f);
    fclose(f);
}

// ── Index rebuild: scan /spiffs/notes/ for .pcm files ───────

static void rebuild_index(void)
{
    note_count = 0;
    DIR *dir = opendir(NOTES_DIR "/notes");
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && note_count < MAX_NOTES) {
        const char *d = entry->d_name;
        // Match "note_NNNNN.pcm"
        if (strncmp(d, "note_", 5) != 0) continue;
        char *dot = strrchr(d, '.');
        if (!dot || strcmp(dot, ".pcm") != 0) continue;

        // Extract name without extension
        size_t len = dot - d;
        if (len >= NOTE_NAME_LEN) len = NOTE_NAME_LEN - 1;
        memcpy(notes[note_count].name, d, len);
        notes[note_count].name[len] = '\0';

        // Load companion .meta
        char meta_path[64];
        make_note_path(notes[note_count].name, ".meta", meta_path, sizeof(meta_path));
        FILE *mf = fopen(meta_path, "rb");
        if (mf) {
            uint8_t hdr[META_HEADER_SIZE + NOTE_TEXT_MAX];
            fread(hdr, 1, META_HEADER_SIZE, mf);
            notes[note_count].timestamp = (uint32_t)hdr[0]
                                        | ((uint32_t)hdr[1] << 8)
                                        | ((uint32_t)hdr[2] << 16)
                                        | ((uint32_t)hdr[3] << 24);
            notes[note_count].synced = hdr[4] != 0;
            uint16_t tlen = (uint16_t)hdr[5] | ((uint16_t)hdr[6] << 8);
            if (tlen > 0 && tlen <= NOTE_TEXT_MAX) {
                size_t got = fread(notes[note_count].text, 1, tlen, mf);
                notes[note_count].text[got] = '\0';
            } else {
                notes[note_count].text[0] = '\0';
            }
            fclose(mf);
        } else {
            notes[note_count].timestamp = 0;
            notes[note_count].synced = false;
            notes[note_count].text[0] = '\0';
        }

        note_count++;
    }
    closedir(dir);
}

static void write_meta(const note_entry_t *n)
{
    char path[64];
    make_note_path(n->name, ".meta", path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (!f) return;

    uint8_t hdr[META_HEADER_SIZE];
    hdr[0] = n->timestamp & 0xff;
    hdr[1] = (n->timestamp >> 8) & 0xff;
    hdr[2] = (n->timestamp >> 16) & 0xff;
    hdr[3] = (n->timestamp >> 24) & 0xff;
    hdr[4] = n->synced ? 1 : 0;
    size_t tlen = strlen(n->text);
    if (tlen > NOTE_TEXT_MAX) tlen = NOTE_TEXT_MAX;
    hdr[5] = tlen & 0xff;
    hdr[6] = (tlen >> 8) & 0xff;
    fwrite(hdr, 1, META_HEADER_SIZE, f);
    if (tlen > 0) fwrite(n->text, 1, tlen, f);
    fclose(f);
}

// ── Init ────────────────────────────────────────────────────

void offline_notes_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = NOTES_DIR,
        .partition_label = "storage",
        .max_files = 8,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return;
    }
    size_t total = 0, used = 0;
    esp_spiffs_info("storage", &total, &used);
    ESP_LOGI(TAG, "SPIFFS mounted: %dKB total, %dKB used", total / 1024, used / 1024);

    ensure_notes_dir();
    rebuild_index();
    ESP_LOGI(TAG, "%d offline notes found", note_count);

    sync_sem = xSemaphoreCreateBinary();
}

// ── Recording ───────────────────────────────────────────────

bool offline_note_start(void)
{
    if (recording) return false;

    uint32_t id = read_counter() + 1;
    write_counter(id);

    snprintf(rec_name, sizeof(rec_name), "note_%05" PRIu32, id);

    char path[64];
    make_note_path(rec_name, ".pcm", path, sizeof(path));
    rec_file = fopen(path, "wb");
    if (!rec_file) {
        ESP_LOGE(TAG, "Failed to open %s for writing", path);
        return false;
    }

    recording = true;
    ESP_LOGI(TAG, "Offline recording to %s", path);
    return true;
}

void offline_note_write_audio(const int16_t *data, size_t samples)
{
    if (!recording || !rec_file) return;
    fwrite(data, sizeof(int16_t), samples, rec_file);
}

void offline_note_stop(void)
{
    if (!recording) return;

    if (rec_file) {
        fclose(rec_file);
        rec_file = NULL;
    }

    // Add to index
    if (note_count < MAX_NOTES) {
        note_entry_t *n = &notes[note_count];
        strncpy(n->name, rec_name, NOTE_NAME_LEN - 1);
        n->name[NOTE_NAME_LEN - 1] = '\0';
        n->timestamp = (uint32_t)(esp_timer_get_time() / 1000000);
        n->synced = false;
        n->text[0] = '\0';
        write_meta(n);
        note_count++;
        ESP_LOGI(TAG, "Saved offline note %s", rec_name);
    }

    recording = false;
    ESP_LOGI(TAG, "Offline recording stopped");
}

bool offline_note_is_recording(void) { return recording; }

// ── Listing ─────────────────────────────────────────────────

int  offline_note_count(void) { return note_count; }
int  offline_note_pending_sync_count(void)
{
    int n = 0;
    for (int i = 0; i < note_count; i++) if (!notes[i].synced) n++;
    return n;
}

bool offline_note_get_name(int idx, char *name, size_t name_sz)
{
    if (idx < 0 || idx >= note_count) return false;
    snprintf(name, name_sz, "%s", notes[idx].name);
    return true;
}

bool offline_note_get_text(int idx, char *text, size_t text_sz)
{
    if (idx < 0 || idx >= note_count) return false;
    snprintf(text, text_sz, "%s", notes[idx].text);
    return true;
}

bool offline_note_is_synced(int idx)
{
    if (idx < 0 || idx >= note_count) return false;
    return notes[idx].synced;
}

uint32_t offline_note_get_timestamp(int idx)
{
    if (idx < 0 || idx >= note_count) return 0;
    return notes[idx].timestamp;
}

// ── Playback ────────────────────────────────────────────────

void offline_note_play(int idx)
{
    if (idx < 0 || idx >= note_count) return;

    char path[64];
    make_note_path(notes[idx].name, ".pcm", path, sizeof(path));
    ESP_LOGI(TAG, "Playing %s", path);

    speaker_play_file(path);
}

// ── Sync ────────────────────────────────────────────────────

static void sync_send_note(note_entry_t *n)
{
    char path[64];
    make_note_path(n->name, ".pcm", path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Cannot open %s for sync", path);
        return;
    }

    // Read and send in chunks
    int16_t buf[PCM_CHUNK_SAMPLES];
    size_t total_sent = 0;
    bool ws_ok = true;

    // Create a unique session for this sync
    char session[32];
    snprintf(session, sizeof(session), "sync_%s", n->name);

    while (1) {
        size_t read = fread(buf, sizeof(int16_t), PCM_CHUNK_SAMPLES, f);
        if (read == 0) break;

        if (ws_ok && ws_client_is_connected()) {
            esp_err_t ret = ws_client_send_audio_mode(
                (uint8_t *)buf, read * sizeof(int16_t), "note");
            if (ret != ESP_OK) ws_ok = false;
        }
        total_sent += read;
    }
    fclose(f);

    ESP_LOGI(TAG, "Sent %zu samples from %s", total_sent, n->name);

    // Send end marker
    if (ws_ok && ws_client_is_connected()) {
        char msg[192];
        snprintf(msg, sizeof(msg),
                 "{\"type\":\"end\",\"mode\":\"note\",\"session_id\":\"%s\"}",
                 session);
        ws_client_send_json(msg);
    }
}

static void sync_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Sync started");

    for (int i = 0; i < note_count; i++) {
        if (notes[i].synced) continue;

        sync_current = i;
        sync_send_note(&notes[i]);

        // Wait for response with timeout
        if (xSemaphoreTake(sync_sem, pdMS_TO_TICKS(15000)) == pdTRUE) {
            ESP_LOGI(TAG, "Sync response received for %s", notes[i].name);
            // Text already stored in notes[i].text by the callback
        } else {
            ESP_LOGW(TAG, "Sync timeout for %s", notes[i].name);
        }

        write_meta(&notes[i]);
        vTaskDelay(pdMS_TO_TICKS(500)); // small gap between notes
    }

    sync_current = -1;
    sync_busy = false;
    ESP_LOGI(TAG, "Sync complete");
    vTaskDelete(NULL);
}

void offline_note_sync_start(void)
{
    if (sync_busy) return;
    if (offline_note_pending_sync_count() == 0) return;
    if (!ws_client_is_connected()) return;

    sync_busy = true;
    xTaskCreate(sync_task, "sync_notes", 4096, NULL, 3, NULL);
}

bool offline_note_sync_is_busy(void) { return sync_busy; }

// Called from main.c ws_message callback when in sync mode
void offline_note_sync_handle_response(const char *session, const char *text)
{
    if (sync_current < 0 || sync_current >= note_count) return;

    // Store text in the note
    strncpy(notes[sync_current].text, text, NOTE_TEXT_MAX - 1);
    notes[sync_current].text[NOTE_TEXT_MAX - 1] = '\0';
    notes[sync_current].synced = true;

    xSemaphoreGive(sync_sem);
}

// ── Delete ──────────────────────────────────────────────────

void offline_note_delete(int idx)
{
    if (idx < 0 || idx >= note_count) return;

    char path[64];
    make_note_path(notes[idx].name, ".pcm", path, sizeof(path));
    remove(path);

    make_note_path(notes[idx].name, ".meta", path, sizeof(path));
    remove(path);

    // Shift array
    for (int i = idx; i < note_count - 1; i++) notes[i] = notes[i + 1];
    note_count--;
}
