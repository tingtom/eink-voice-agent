#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_sleep.h"
#include "esp_idf_version.h"
#include "esp_timer.h"

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
#include "recordings.h"
#include "sdcard.h"
#include "mdns.h"

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
static bool driving_mode = false;
static bool was_charging = false;

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
            if (recording_pending_sync_count() == 0) {
                ui_show_response("Nothing to sync!");
                current_sub = SUB_RESPONSE;
                return;
            }
            ui_show_processing_screen();
            recording_sync_start();
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
    int count = recording_count();
    if (count == 0) {
        epaper_clear();
        epaper_draw_text(10, 60, "No offline recordings", 12);
        epaper_partial_refresh();
        return;
    }

    if (notes_sel < 0) notes_sel = 0;
    if (notes_sel >= count) notes_sel = count - 1;

    // Auto-scroll
    int visible = (DISPLAY_HEIGHT - 30) / 14;
    if (notes_sel < notes_list_offset) notes_list_offset = notes_sel;
    if (notes_sel >= notes_list_offset + visible)
        notes_list_offset = notes_sel - visible + 1;

    epaper_clear();
    epaper_draw_text(4, 8, "Recordings", 12);

    uint32_t free_secs = recording_capacity_seconds();
    char cap_line[24];
    if (free_secs > 0) {
        snprintf(cap_line, sizeof(cap_line), "~%lus free", (unsigned long)free_secs);
        epaper_draw_text(4, 22, cap_line, 8);
    }

    int y = 36;
    for (int i = notes_list_offset; i < count && i < notes_list_offset + visible; i++) {
        recording_info_t info;
        if (!recording_get_info(i, &info)) continue;

        char icon;
        switch (info.status) {
            case REC_STATUS_RAW:         icon = 'o'; break;
            case REC_STATUS_TRANSCRIBED: icon = 'T'; break;
            case REC_STATUS_SYNCED:      icon = '*'; break;
            default:                     icon = '?'; break;
        }

        char line[40];
        const char *short_name = info.name + 4;
        if (i == notes_sel) {
            snprintf(line, sizeof(line), ">%.11s %c", short_name, icon);
        } else {
            snprintf(line, sizeof(line), " %.11s %c", short_name, icon);
        }
        epaper_draw_text(8, y, line, 8);
        y += 14;
    }

    epaper_draw_text(4, DISPLAY_HEIGHT - 14, "o=raw T=text *=done SEL=open", 8);
    epaper_partial_refresh();
}

static void draw_note_detail(int idx)
{
    recording_info_t info;
    if (!recording_get_info(idx, &info)) return;

    epaper_clear();
    char title[32];
    const char *type_str = (info.type == REC_TYPE_NOTE) ? "NOTE" : "TODO";
    snprintf(title, sizeof(title), "%s (%s)", info.name, type_str);
    epaper_draw_text(4, 8, title, 12);

    if (info.status >= REC_STATUS_TRANSCRIBED) {
        epaper_draw_text(4, 24, info.text, 8);
    } else if (info.status == REC_STATUS_RAW) {
        epaper_draw_text(4, 30, "[Not transcribed]", 8);
        epaper_draw_text(4, 44, "Sync to transcribe", 8);
    }

    char dur[24];
    snprintf(dur, sizeof(dur), "%lums", (unsigned long)info.duration_ms);
    epaper_draw_text(4, DISPLAY_HEIGHT - 14, dur, 8);

    epaper_draw_text(100, DISPLAY_HEIGHT - 14, "BOOTlong=play BACK=back", 8);
    epaper_partial_refresh();
}

// ── Button handlers ────────────────────────────────────────

// Short press = navigation (UP/DOWN)
// Long press  = confirm (BOOT) or back/sleep (PWR)
static void handle_longpress(button_id_t btn)
{
    if (driving_mode) {
        ESP_LOGI(TAG, "Exiting driving mode");
        driving_mode = false;
        ui_set_driving_mode(false);
        audio_pipeline_set_docked(power_is_charging());
        if (current_sub == SUB_RECORDING) {
            audio_pipeline_stop_recording();
            audio_pipeline_stop_offline_recording();
        }
        finish_current_mode();
        return_home();
        return;
    }

    if (current_app_mode == APP_MODE_HOME) {
        if (btn == BUTTON_BACK) {
            // PWR long = sleep
            ESP_LOGI(TAG, "Going to sleep from menu");
            ui_show_sleep_screen();
            vTaskDelay(pdMS_TO_TICKS(100));
            power_enter_deep_sleep(0);
        } else if (btn == BUTTON_SELECT) {
            // BOOT long = enter mode
            enter_mode(menu_mode_map[menu_selection]);
        }
        return;
    }

    // Long press BOOT = CONFIRM / STOP
    if (btn == BUTTON_SELECT) {
        switch (current_app_mode) {
        case APP_MODE_GAMES:
            mode_games_do_action();
            if (!mode_games_is_active()) return_home();
            return;
        case APP_MODE_VIEW_NOTES:
            if (current_sub == SUB_NOTE_LIST) {
                current_sub = SUB_NOTE_DETAIL;
                draw_note_detail(notes_sel);
            } else {
                recording_play(notes_sel);
                draw_note_detail(notes_sel);
            }
            return;
        case APP_MODE_SYNC:
            return_home();
            return;
        default:
            break;
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
        return;
    }

    // Long press PWR = BACK / SLEEP
    if (btn == BUTTON_BACK) {
        switch (current_app_mode) {
        case APP_MODE_GAMES:
            mode_games_finish();
            return_home();
            return;
        case APP_MODE_VIEW_NOTES:
        case APP_MODE_SYNC:
            return_home();
            return;
        default:
            break;
        }

        switch (current_sub) {
        case SUB_RECORDING:
            audio_pipeline_stop_offline_recording();
            audio_pipeline_stop_recording();
            return_home();
            break;
        case SUB_PROCESSING:
        case SUB_RESPONSE:
            finish_current_mode();
            return_home();
            break;
        default:
            break;
        }
    }
}

static void handle_button(button_id_t btn)
{
    // If docked, any button wakes into driving mode
    if (power_is_charging() && !driving_mode) {
        ESP_LOGI(TAG, "Waking from docked, entering driving mode");
        driving_mode = true;
        ui_set_driving_mode(true);
        audio_pipeline_set_docked(false);
        current_app_mode = APP_MODE_VOICE_AGENT;
        current_sub = SUB_RECORDING;
        mode_voice_agent_start();
        ui_show_driving_screen();
        return;
    }

    switch (current_app_mode) {
    case APP_MODE_HOME: {
        // BOOT short = UP (wrap), PWR short = DOWN (wrap)
        if (btn == BUTTON_SELECT) {
            menu_selection = (menu_selection > 0) ? menu_selection - 1 : menu_count - 1;
            ui_show_menu(menu_items, menu_count, menu_selection);
        } else if (btn == BUTTON_BACK) {
            menu_selection = (menu_selection + 1) % menu_count;
            ui_show_menu(menu_items, menu_count, menu_selection);
        }
        break;
    }
    case APP_MODE_GAMES:
        mode_games_handle_button(btn);
        if (!mode_games_is_active()) return_home();
        break;

    case APP_MODE_VIEW_NOTES: {
        int count = recording_count();
        if (count == 0) { return_home(); break; }
        if (current_sub == SUB_NOTE_LIST) {
            // BOOT short = UP (wrap), PWR short = DOWN (wrap)
            if (btn == BUTTON_SELECT) {
                notes_sel = (notes_sel > 0) ? notes_sel - 1 : count - 1;
                draw_note_list();
            } else if (btn == BUTTON_BACK) {
                notes_sel = (notes_sel + 1) % count;
                draw_note_list();
            }
        } else if (current_sub == SUB_NOTE_DETAIL) {
            current_sub = SUB_NOTE_LIST;
            draw_note_list();
        }
        break;
    }

    case APP_MODE_SYNC:
        return_home();
        break;

    default:
        // In recording/processing/response, any short press exits
        if (current_sub == SUB_RECORDING) {
            audio_pipeline_stop_offline_recording();
            audio_pipeline_stop_recording();
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
        } else if (current_sub == SUB_RESPONSE) {
            if (driving_mode) {
                ESP_LOGI(TAG, "Driving mode loop: restarting voice agent");
                audio_pipeline_stop_processing();
                mode_voice_agent_start();
                ui_show_driving_screen();
                current_sub = SUB_RECORDING;
            } else {
                finish_current_mode();
                return_home();
            }
        } else if (current_sub == SUB_PROCESSING) {
            finish_current_mode();
            return_home();
        }
        break;
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
    if (recording_sync_is_busy()) {
        const char *sid_key = "\"session_id\":\"";
        const char *sid_start = strstr(data, sid_key);
        if (sid_start) {
            sid_start += strlen(sid_key);
            const char *sid_end = strchr(sid_start, '"');
            if (sid_end) {
                size_t sid_len = sid_end - sid_start;
                char *sid = malloc(sid_len + 1);
                if (sid) {
                    memcpy(sid, sid_start, sid_len);
                    sid[sid_len] = '\0';
                    recording_sync_handle_response(sid, text);
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
#if ESP_IDF_VERSION_MAJOR >= 6
    esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_causes();
#else
    esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();
#endif

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
    power_init();
    epaper_init();
    ui_init();
    recordings_init();

    if (!wifi_has_saved_creds()) {
        ESP_LOGI(TAG, "No WiFi credentials found, starting provisioning");
        ui_show_boot_screen("Setup Mode");
        wifi_init();
        provisioning_start_ap();
        provisioning_start_server();
        ui_show_provisioning_screen("EInk-Voice-Config", "192.168.4.1");
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

        // mDNS advertisement
        ESP_ERROR_CHECK(mdns_init());
        ESP_ERROR_CHECK(mdns_hostname_set(DEVICE_NAME));
        ESP_ERROR_CHECK(mdns_instance_name_set("EInk Voice Agent"));
        mdns_service_add("eink-voice-http", "_http", "_tcp", 80, NULL, 0);
        mdns_service_add("eink-voice-agent", "_eink-voice-agent", "_tcp", 0, NULL, 0);
        mdns_service_txt_item_set("_eink-voice-agent", "_tcp", "device_type", "eink_voice_agent");
        mdns_service_txt_item_set("_eink-voice-agent", "_tcp", "version", "1.0");
        ESP_LOGI(TAG, "mDNS started — device available as %s.local", DEVICE_NAME);
    } else {
        ESP_LOGW(TAG, "WiFi connection failed, starting provisioning");
        provisioning_start_ap();
        provisioning_start_server();
        ui_show_provisioning_screen("EInk-Voice-Config", "192.168.4.1");
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

    // If already charging at boot, enter docked state
    if (power_is_charging()) {
        ESP_LOGI(TAG, "Already charging at boot, entering docked state");
        was_charging = true;
        audio_pipeline_set_docked(true);
        ui_show_docked_screen();
    }

    ESP_LOGI(TAG, "Initialization complete, entering main loop");

    int telemetry_ticks = 0;

    while (1) {
        ws_client_reconnect();
        uint8_t battery = power_get_battery_pct();
        ui_update_battery(battery);
        ui_update_wifi_status(wifi_is_connected());

        // ── Charging state transitions ──────────────────────
        bool now_charging = power_is_charging();

        if (!now_charging && driving_mode) {
            driving_mode = false;
            ui_set_driving_mode(false);
        }

        if (now_charging != was_charging) {
            if (now_charging) {
                ESP_LOGI(TAG, "Charging detected");
                if (current_app_mode == APP_MODE_HOME && current_sub == SUB_MENU) {
                    driving_mode = false;
                    ui_set_driving_mode(false);
                    audio_pipeline_set_docked(true);
                    ui_show_docked_screen();
                }
            } else {
                ESP_LOGI(TAG, "Charging disconnected");
                audio_pipeline_set_docked(false);
                if (current_app_mode == APP_MODE_HOME) {
                    return_home();
                }
            }
            was_charging = now_charging;
            telemetry_ticks = 6; // Send telemetry immediately on state change
        }

        // ── Periodic telemetry + mDNS TXT update (every ~30s) ──
        telemetry_ticks++;
        if (telemetry_ticks >= 6 && ws_client_is_connected()) {
            telemetry_ticks = 0;

            uint8_t bat = power_get_battery_pct();
            bool chg = power_is_charging();
            int8_t rssi = wifi_get_rssi();
            int64_t uptime_s = esp_timer_get_time() / 1000000;
            int32_t wc = power_get_wake_count();
            uint64_t free_kb = sdcard_get_free_bytes() / 1024;
            uint32_t cap_sec = recording_capacity_seconds();

            char json[256];
            snprintf(json, sizeof(json),
                "{"
                "\"type\":\"telemetry\","
                "\"battery\":%u,"
                "\"charging\":%s,"
                "\"wifi_rssi\":%d,"
                "\"uptime\":%" PRIu64 ","
                "\"wake_count\":%" PRId32 ","
                "\"storage_free_kb\":%" PRIu64 ","
                "\"recording_time_remaining_sec\":%" PRIu32
                "}",
                (unsigned)bat, chg ? "true" : "false", (int)rssi,
                (uint64_t)uptime_s, wc, free_kb, cap_sec);
            ws_client_send_json(json);

            // Update mDNS TXT records with live values
            char buf[16];
            snprintf(buf, sizeof(buf), "%u", bat);
            mdns_service_txt_item_set("_eink-voice-agent", "_tcp", "battery", buf);
            snprintf(buf, sizeof(buf), "%s", chg ? "true" : "false");
            mdns_service_txt_item_set("_eink-voice-agent", "_tcp", "charging", buf);
            snprintf(buf, sizeof(buf), "%d", rssi);
            mdns_service_txt_item_set("_eink-voice-agent", "_tcp", "wifi_rssi", buf);
        }

        // Skip sleep timer when docked (on USB power)
        bool docked = now_charging && !driving_mode;
        if (!docked && power_should_sleep()) {
            ESP_LOGI(TAG, "Entering deep sleep...");
            ui_show_sleep_screen();
            vTaskDelay(pdMS_TO_TICKS(100));
            power_enter_deep_sleep(0);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
