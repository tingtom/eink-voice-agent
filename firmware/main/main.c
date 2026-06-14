#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_sleep.h"

#include "app_config.h"
#include "nvs_flash.h"
#include "system_init.h"
#include "wifi_manager.h"
#include "epaper_driver.h"
#include "ui_manager.h"
#include "buttons.h"
#include "audio_pipeline.h"
#include "vad.h"
#include "mic_driver.h"
#include "ws_client.h"
#include "power_mgmt.h"
#include "provisioning.h"
#include "mode_voice_agent.h"
#include "mode_transcribe.h"
#include "mode_note.h"
#include "mode_todo.h"
#include "mode_dashboard.h"
#include "mode_games.h"
#include "offline_notes.h"

static const char *TAG = "MAIN";

#define VAD_BURST_POLL_US  (200 * 1000)
#define VAD_BURST_SAMPLES  256

typedef enum {
    APP_MODE_HOME,
    APP_MODE_VOICE_AGENT,
    APP_MODE_TRANSCRIBE,
    APP_MODE_NOTE,
    APP_MODE_TODO,
    APP_MODE_DASHBOARD,
    APP_MODE_GAMES,
    APP_MODE_VIEW_NOTES,
    APP_MODE_SYNC,
} app_mode_t;

typedef enum {
    SUB_MENU,
    SUB_RECORDING,
    SUB_PROCESSING,
    SUB_RESPONSE,
    SUB_NOTE_LIST,
    SUB_NOTE_DETAIL,
} sub_state_t;

static app_mode_t current_app_mode = APP_MODE_HOME;
static sub_state_t current_sub = SUB_MENU;
static int menu_selection = 0;

// View notes state
static int notes_list_offset = 0;
static int notes_sel = 0;

static const char *menu_items[] = {
    "Voice Agent",
    "Transcribe",
    "Voice Note",
    "Todo List",
    "Dashboard",
    "Games",
    "View Notes",
    "Sync",
};
static const int menu_count = sizeof(menu_items) / sizeof(menu_items[0]);

static app_mode_t menu_mode_map[] = {
    APP_MODE_VOICE_AGENT,
    APP_MODE_TRANSCRIBE,
    APP_MODE_NOTE,
    APP_MODE_TODO,
    APP_MODE_DASHBOARD,
    APP_MODE_GAMES,
    APP_MODE_VIEW_NOTES,
    APP_MODE_SYNC,
};

static void enter_mode(app_mode_t mode)
{
    current_app_mode = mode;
    switch (mode) {
        case APP_MODE_VOICE_AGENT:
            mode_voice_agent_start();
            current_sub = SUB_RECORDING;
            break;
        case APP_MODE_TRANSCRIBE:
            mode_transcribe_start();
            current_sub = SUB_RECORDING;
            break;
        case APP_MODE_NOTE:
            mode_note_start();
            current_sub = SUB_RECORDING;
            break;
        case APP_MODE_TODO:
            mode_todo_start();
            current_sub = SUB_RECORDING;
            break;
        case APP_MODE_DASHBOARD:
            mode_dashboard_start();
            current_sub = SUB_RESPONSE;
            break;
        case APP_MODE_GAMES:
            mode_games_start();
            break;
        case APP_MODE_VIEW_NOTES:
            notes_sel = 0;
            notes_list_offset = 0;
            current_sub = SUB_NOTE_LIST;
            // Fall through to draw
            ui_show_menu((const char **)NULL, 0, 0); // clear screen
            // Will draw note list on next button press
            break;
        case APP_MODE_SYNC:
            if (!ws_client_is_connected()) {
                ui_show_error("No WiFi\nCan't sync");
                current_app_mode = APP_MODE_HOME;
                current_sub = SUB_MENU;
                return;
            }
            if (offline_note_pending_sync_count() == 0) {
                ui_show_response("Nothing to sync!");
                current_sub = SUB_RESPONSE;
                return;
            }
            ui_show_processing_screen();
            offline_note_sync_start();
            break;
        default:
            break;
    }
}

static void return_home(void)
{
    current_app_mode = APP_MODE_HOME;
    current_sub = SUB_MENU;
    menu_selection = 0;
    ui_show_home_screen();
}

static void finish_current_mode(void)
{
    switch (current_app_mode) {
        case APP_MODE_VOICE_AGENT: mode_voice_agent_finish(); break;
        case APP_MODE_TRANSCRIBE:  mode_transcribe_finish();  break;
        case APP_MODE_NOTE:        mode_note_finish();        break;
        case APP_MODE_TODO:        mode_todo_finish();        break;
        default: break;
    }
}

// ── Draw note list screen ───────────────────────────────────

static void draw_note_list(void)
{
    int count = offline_note_count();
    if (count == 0) {
        epaper_clear();
        epaper_draw_text(10, 60, "No offline notes", 12);
        epaper_partial_refresh();
        return;
    }

    if (notes_sel < 0) notes_sel = 0;
    if (notes_sel >= count) notes_sel = count - 1;

    // Auto-scroll
    int visible = (DISPLAY_HEIGHT - 30) / 14; // ~12 items
    if (notes_sel < notes_list_offset) notes_list_offset = notes_sel;
    if (notes_sel >= notes_list_offset + visible)
        notes_list_offset = notes_sel - visible + 1;

    epaper_clear();
    epaper_draw_text(4, 8, "Offline Notes", 12);

    int y = 26;
    for (int i = notes_list_offset; i < count && i < notes_list_offset + visible; i++) {
        char synced = offline_note_is_synced(i) ? '*' : ' ';
        char line[32];
        if (i == notes_sel) {
            char name[NOTE_NAME_LEN];
            offline_note_get_name(i, name, sizeof(name));
            snprintf(line, sizeof(line), ">%s %c", name + 5, synced);
        } else {
            char name[NOTE_NAME_LEN];
            offline_note_get_name(i, name, sizeof(name));
            snprintf(line, sizeof(line), " %s %c", name + 5, synced);
        }
        epaper_draw_text(8, y, line, 8);
        y += 14;
    }

    epaper_draw_text(4, DISPLAY_HEIGHT - 14, "*=synced  SEL=play", 8);
    epaper_partial_refresh();
}

static void draw_note_detail(int idx)
{
    char name[NOTE_NAME_LEN], text[NOTE_TEXT_MAX];
    offline_note_get_name(idx, name, sizeof(name));
    offline_note_get_text(idx, text, sizeof(text));

    epaper_clear();
    epaper_draw_text(4, 8, name, 12);

    if (offline_note_is_synced(idx)) {
        epaper_draw_text(4, 24, text, 8);
    } else {
        epaper_draw_text(4, 30, "Not transcribed", 8);
    }

    epaper_draw_text(4, DISPLAY_HEIGHT - 14, "SEL=play  long=back", 8);
    epaper_partial_refresh();
}

// ── Button handler ─────────────────────────────────────────

static void handle_longpress(button_id_t btn)
{
    (void)btn;

    if (current_app_mode == APP_MODE_HOME) {
        ESP_LOGI(TAG, "Going to sleep from menu");
        ui_show_sleep_screen();
        vTaskDelay(pdMS_TO_TICKS(100));
        power_enter_deep_sleep(0);
        return;
    }

    if (current_app_mode == APP_MODE_GAMES) {
        mode_games_finish();
        return_home();
        return;
    }

    if (current_app_mode == APP_MODE_VIEW_NOTES || current_app_mode == APP_MODE_SYNC) {
        return_home();
        return;
    }

    switch (current_sub) {
    case SUB_RECORDING:
        audio_pipeline_stop_offline_recording();
        audio_pipeline_stop_recording();
        return_home();
        break;
    case SUB_PROCESSING:
        finish_current_mode();
        return_home();
        break;
    case SUB_RESPONSE:
        finish_current_mode();
        return_home();
        break;
    default:
        break;
    }
}

static void handle_button(button_id_t btn)
{
    if (current_app_mode == APP_MODE_HOME) {
        if (btn == BUTTON_UP && menu_selection > 0) {
            menu_selection--;
            ui_show_menu(menu_items, menu_count, menu_selection);
        } else if (btn == BUTTON_DOWN && menu_selection < menu_count - 1) {
            menu_selection++;
            ui_show_menu(menu_items, menu_count, menu_selection);
        } else if (btn == BUTTON_SELECT) {
            enter_mode(menu_mode_map[menu_selection]);
        }
        return;
    }

    if (current_app_mode == APP_MODE_GAMES) {
        mode_games_handle_button(btn);
        if (!mode_games_is_active()) {
            return_home();
        }
        return;
    }

    if (current_app_mode == APP_MODE_VIEW_NOTES) {
        int count = offline_note_count();
        if (count == 0) { return_home(); return; }

        if (current_sub == SUB_NOTE_LIST) {
            if (btn == BUTTON_UP) { notes_sel--; draw_note_list(); }
            else if (btn == BUTTON_DOWN) { notes_sel++; draw_note_list(); }
            else if (btn == BUTTON_SELECT) {
                current_sub = SUB_NOTE_DETAIL;
                draw_note_detail(notes_sel);
            }
        } else if (current_sub == SUB_NOTE_DETAIL) {
            if (btn == BUTTON_SELECT) {
                offline_note_play(notes_sel);
                draw_note_detail(notes_sel);
            } else if (btn == BUTTON_UP || btn == BUTTON_DOWN) {
                current_sub = SUB_NOTE_LIST;
                draw_note_list();
            }
        }
        return;
    }

    if (current_app_mode == APP_MODE_SYNC) {
        if (btn == BUTTON_SELECT) {
            return_home();
        }
        return;
    }

    if (current_sub == SUB_RECORDING) {
        if (btn == BUTTON_SELECT) {
            switch (current_app_mode) {
                case APP_MODE_VOICE_AGENT:
                    mode_voice_agent_stop();
                    break;
                case APP_MODE_TRANSCRIBE:
                    mode_transcribe_stop();
                    break;
                case APP_MODE_NOTE:
                    mode_note_stop();
                    break;
                case APP_MODE_TODO:
                    mode_todo_stop();
                    break;
                default:
                    break;
            }
            current_sub = SUB_PROCESSING;
        }
        return;
    }

    if (current_sub == SUB_RESPONSE) {
        if (btn == BUTTON_SELECT) {
            finish_current_mode();
            return_home();
        }
        return;
    }
}

// ── WebSocket message handler ───────────────────────────────

static void handle_ws_message(const char *data, size_t len)
{
    (void)len;
    const char *type_key = "\"type\":\"";
    const char *type_start = strstr(data, type_key);
    if (!type_start) return;
    type_start += strlen(type_key);
    if (strncmp(type_start, "response", 8) != 0) return;

    const char *data_key = "\"data\":\"";
    const char *data_start = strstr(data, data_key);
    if (!data_start) return;
    data_start += strlen(data_key);

    const char *end = strchr(data_start, '"');
    if (!end) return;

    size_t text_len = end - data_start;
    if (text_len == 0) return;

    char *text = malloc(text_len + 1);
    if (!text) return;
    memcpy(text, data_start, text_len);
    text[text_len] = '\0';

    // Route to sync handler when syncing
    if (offline_note_sync_is_busy()) {
        // Find session_id if present
        const char *sid_key = "\"session_id\":\"";
        const char *sid_start = strstr(data, sid_key);
        if (sid_start) {
            sid_start += strlen(sid_key);
            const char *sid_end = strchr(sid_start, '"');
            if (sid_end) {
                size_t sid_len = sid_end - sid_start;
                // Session IDs for sync notes are "sync_note_XXXXX"
                char *sid = malloc(sid_len + 1);
                if (sid) {
                    memcpy(sid, sid_start, sid_len);
                    sid[sid_len] = '\0';
                    // The offline_notes module handles it
                    extern void offline_note_sync_handle_response(const char *session, const char *txt);
                    offline_note_sync_handle_response(sid, text);
                    free(sid);
                }
            }
        }
        free(text);
        return;
    }

    switch (current_app_mode) {
        case APP_MODE_VOICE_AGENT:
            mode_voice_agent_handle_response(text);
            break;
        case APP_MODE_TRANSCRIBE:
            mode_transcribe_handle_response(text);
            break;
        case APP_MODE_NOTE:
            mode_note_save(text);
            break;
        case APP_MODE_TODO:
            mode_todo_handle_response(text);
            break;
        default:
            free(text);
            return;
    }
    free(text);
    current_sub = SUB_RESPONSE;
}

// ── VAD burst for timer wake ────────────────────────────────

static bool handle_vad_burst(void)
{
    epaper_init();
    mic_init();
    mic_start();
    vad_init();

    int16_t buf[VAD_BURST_SAMPLES];
    int energy_ok = 0;

    for (int i = 0; i < 6; i++) {
        size_t read = 0;
        if (mic_read(buf, VAD_BURST_SAMPLES, &read) == ESP_OK && read > 0) {
            int32_t energy = vad_compute_energy(buf, read);
            if (energy >= AUDIO_VAD_THRESHOLD) energy_ok++;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    mic_stop();
    return energy_ok >= 3;
}

// ── Main ────────────────────────────────────────────────────

void app_main(void)
{
    esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();

    if (wake_cause == ESP_SLEEP_WAKEUP_TIMER) {
        if (handle_vad_burst()) {
            ESP_LOGI(TAG, "Voice detected, full wake");
            epaper_sleep();
            esp_restart();
        } else {
            ESP_LOGI(TAG, "No voice, back to sleep");
            power_enter_deep_sleep(VAD_BURST_POLL_US);
            return;
        }
    } else if (wake_cause == ESP_SLEEP_WAKEUP_GPIO) {
        ESP_LOGI(TAG, "Button wake");
    }

    ESP_LOGI(TAG, "Device: %s", DEVICE_NAME);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    system_init();
    epaper_init();
    ui_init();
    offline_notes_init();

    if (!wifi_has_saved_creds()) {
        ESP_LOGI(TAG, "No WiFi credentials found, starting provisioning");
        ui_show_boot_screen("Setup Mode");
        wifi_init();
        provisioning_start_ap();
        provisioning_start_server();
        ui_show_error("Connect to WiFi:\nEInk-Voice-Config\nhttp://192.168.4.1");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(60000));
        }
    }

    ui_show_boot_screen(DEVICE_NAME);

    char ssid[64], password[64];
    wifi_load_creds(ssid, sizeof(ssid), password, sizeof(password));
    wifi_init();
    wifi_connect(ssid, password);

    int retries = 0;
    while (!wifi_is_connected() && retries < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retries++;
    }

    if (wifi_is_connected()) {
        ESP_LOGI(TAG, "WiFi connected");
        ui_update_status_bar(true, power_get_battery_pct());
    } else {
        ESP_LOGW(TAG, "WiFi connection failed, starting provisioning");
        provisioning_start_ap();
        provisioning_start_server();
        ui_show_error("WiFi failed\nConnect to:\nEInk-Voice-Config\nhttp://192.168.4.1");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(60000));
        }
    }

    audio_pipeline_init();
    ws_client_init(HERMES_WS_URL, DEVICE_AUTH_TOKEN);
    button_set_callback(handle_button);
    button_set_longpress_callback(handle_longpress, 1500);
    ws_client_set_callback(handle_ws_message);
    buttons_init();
    ui_show_home_screen();

    ESP_LOGI(TAG, "Initialization complete, entering main loop");

    while (1) {
        uint8_t battery = power_get_battery_pct();
        ui_update_battery(battery);
        ui_update_wifi_status(wifi_is_connected());

        if (power_should_sleep()) {
            ESP_LOGI(TAG, "Entering deep sleep...");
            ui_show_sleep_screen();
            vTaskDelay(pdMS_TO_TICKS(100));
            power_enter_deep_sleep(0);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
