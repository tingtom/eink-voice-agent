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
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_wifi.h"

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
#include "es8311.h"
#include "power_mgmt.h"
#include "provisioning.h"
#include "mode_voice_agent.h"
#include "mode_transcribe.h"
#include "mode_note.h"
#include "mode_todo.h"
#include "mode_dashboard.h"
#include "mode_games.h"
#include "recordings.h"
#include "http_client.h"
#include "sdcard.h"
#include "mdns.h"
#include "driver/spi_common.h"
#include "esp_log.h"

static const char *TAG = "MAIN";

#define NVS_NS_BOD  "bod_diag"
#define NVS_KEY_LAST  "last_stage"

static const char *g_last_stage = NULL;

static void bod_save_stage(const char *stage)
{
    g_last_stage = stage;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_BOD, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, NVS_KEY_LAST, stage);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void bod_check_prior(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_BOD, NVS_READONLY, &h) == ESP_OK) {
        char buf[32] = {0};
        size_t len = sizeof(buf);
        if (nvs_get_str(h, NVS_KEY_LAST, buf, &len) == ESP_OK) {
            esp_reset_reason_t reason = esp_reset_reason();
            const char *cause = (reason == ESP_RST_BROWNOUT) ? "BOD RESET" : "unknown reset";
            ESP_LOGW(TAG, "Prior boot ended at stage: '%s' (%s)", buf, cause);
            if (strstr(buf, "home") || strstr(buf, "WiFi") || strstr(buf, "EPD")) {
                ESP_LOGW(TAG, ">>> High-current stage before crash — battery BOD likely");
                ESP_LOGW(TAG, ">>> Fix: lower BOD threshold in menuconfig, or use stronger battery");
            }
        }
        nvs_close(h);
    }
}

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
static bool was_charging = false;
static bool wake_failed_pending = false;
static bool docked_user_exited = false;
static bool charging_debounced = false;
static int charging_unchanged_count = 0;

static bool get_charging_state(void)
{
    bool raw = power_is_charging();
    if (raw == charging_debounced) {
        charging_unchanged_count++;
    } else {
        charging_unchanged_count = 1;
        charging_debounced = raw;
    }
    return charging_debounced;
}

// View notes state
static int notes_list_offset = 0;
static int notes_sel = 0;

static const char *menu_items[] = {
    "Voice Agent",
    "Transcribe",
    "Voice Note",
    "Todo List",
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
    APP_MODE_GAMES,
    APP_MODE_VIEW_NOTES,
    APP_MODE_SYNC,
};

#define MAX_VISIBLE_MENU 7
static const char *visible_menu_items[MAX_VISIBLE_MENU];
static app_mode_t visible_menu_modes[MAX_VISIBLE_MENU];
static int visible_menu_count = 0;

static void rebuild_visible_menu(void)
{
    visible_menu_count = 0;
    for (int i = 0; i < menu_count; i++) {
        if (strcmp(menu_items[i], "Voice Agent") == 0) {
            if (!wifi_is_connected() || !ui_is_hermes_connected()) continue;
        } else if (strcmp(menu_items[i], "Sync") == 0) {
            if (!wifi_is_connected() || !ui_is_hermes_connected()) continue;
        }
        visible_menu_items[visible_menu_count] = menu_items[i];
        visible_menu_modes[visible_menu_count] = menu_mode_map[i];
        visible_menu_count++;
    }
    if (menu_selection >= visible_menu_count) {
        menu_selection = visible_menu_count > 0 ? visible_menu_count - 1 : 0;
    }
}

static void draw_note_list(void);
static void draw_note_detail(int idx);

static void on_wake_failed(void)
{
    if (current_app_mode == APP_MODE_HOME && current_sub == SUB_MENU) {
        wake_failed_pending = true;
    }
}

static void on_recording_ended(void)
{
    // Already handled by mode stop functions
}

static void on_response(const char *text)
{
    if (strcmp(text, "Response timed out") == 0) {
        audio_pipeline_stop_processing();
        ui_show_error("Request timed out");
        return;
    }
    switch (current_app_mode) {
        case APP_MODE_VOICE_AGENT: mode_voice_agent_handle_response(text); break;
        case APP_MODE_TRANSCRIBE:  mode_transcribe_handle_response(text);  break;
        case APP_MODE_TODO:        mode_todo_handle_response(text);        break;
        default: break;
    }
}

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
            draw_note_list();
            break;
        case APP_MODE_SYNC:
            if (!wifi_is_connected()) {
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

static void return_home(bool from_button)
{
    current_app_mode = APP_MODE_HOME;
    current_sub = SUB_MENU;
    menu_selection = 0;
    if (from_button) {
        docked_user_exited = true;
    }
    rebuild_visible_menu();
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
        int tw = epaper_text_width("No offline recordings", 12);
        epaper_draw_text((DISPLAY_WIDTH - tw) / 2, DISPLAY_HEIGHT / 2 - 8, "No offline recordings", 12);
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
        uint32_t dur_s = info.duration_ms / 1000;
        if (i == notes_sel) {
            snprintf(line, sizeof(line), ">%.11s %c %u:%02u", short_name, icon, (unsigned)(dur_s / 60), (unsigned)(dur_s % 60));
        } else {
            snprintf(line, sizeof(line), " %.11s %c %u:%02u", short_name, icon, (unsigned)(dur_s / 60), (unsigned)(dur_s % 60));
        }
        epaper_draw_text(8, y, line, 8);
        y += 14;
    }

    int tw;
    tw = epaper_text_width("back -", 8);
    epaper_draw_text(DISPLAY_WIDTH - tw - 4, DISPLAY_HEIGHT - 24, "back -", 8);
    tw = epaper_text_width("sel -", 8);
    epaper_draw_text(DISPLAY_WIDTH - tw - 4, DISPLAY_HEIGHT - 16, "sel -", 8);

    uint32_t free_secs = recording_capacity_seconds();
    if (free_secs > 0) {
        char cap_line[24];
        snprintf(cap_line, sizeof(cap_line), "%lum", (unsigned long)(free_secs / 60));
        epaper_draw_text(4, DISPLAY_HEIGHT - 14, cap_line, 8);
    }
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
    snprintf(dur, sizeof(dur), "%lus", (unsigned long)(info.duration_ms / 1000));
    epaper_draw_text(4, DISPLAY_HEIGHT - 14, dur, 8);
    ui_draw_button_help("hold to play -", "back -");
    epaper_partial_refresh();
}

// ── Button handlers ────────────────────────────────────────

// Short press = navigation (UP/DOWN)
// Long press  = confirm (BOOT) or back/sleep (PWR)
static void handle_longpress(button_id_t btn)
{
    power_mark_activity();
    if (audio_pipeline_is_docked()) {
        ESP_LOGI(TAG, "Exiting docked mode via button press");
        audio_pipeline_set_docked(false);
        return_home(true);
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
            enter_mode(visible_menu_modes[menu_selection]);
        }
        return;
    }

    // Long press BOOT = CONFIRM / STOP
    if (btn == BUTTON_SELECT) {
        switch (current_app_mode) {
        case APP_MODE_GAMES:
            mode_games_do_action();
            if (!mode_games_is_active()) return_home(false);
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
            return_home(false);
            return;
        default:
            break;
        }

        switch (current_sub) {
        case SUB_RECORDING:
            audio_pipeline_stop_offline_recording();
            audio_pipeline_stop_recording();
            return_home(false);
            break;
        case SUB_PROCESSING:
            finish_current_mode();
            return_home(false);
            break;
        case SUB_RESPONSE:
            finish_current_mode();
            return_home(false);
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
            return_home(false);
            return;
        case APP_MODE_VIEW_NOTES:
        case APP_MODE_SYNC:
            return_home(false);
            return;
        default:
            break;
        }

        switch (current_sub) {
        case SUB_RECORDING:
            audio_pipeline_stop_offline_recording();
            audio_pipeline_stop_recording();
            return_home(false);
            break;
        case SUB_PROCESSING:
        case SUB_RESPONSE:
            finish_current_mode();
            return_home(false);
            break;
        default:
            break;
        }
    }
}

static void handle_button(button_id_t btn)
{
    power_mark_activity();
    if (audio_pipeline_is_docked()) {
        ESP_LOGI(TAG, "Exiting docked mode via button press");
        audio_pipeline_set_docked(false);
        return_home(true);
        return;
    }

    switch (current_app_mode) {
    case APP_MODE_HOME: {
        rebuild_visible_menu();
        // BOOT short = UP (wrap), PWR short = DOWN (wrap)
        if (btn == BUTTON_SELECT) {
            menu_selection = (menu_selection > 0) ? menu_selection - 1 : visible_menu_count - 1;
            ui_show_menu((const char **)visible_menu_items, visible_menu_count, menu_selection);
        } else if (btn == BUTTON_BACK) {
            menu_selection = (menu_selection + 1) % visible_menu_count;
            ui_show_menu((const char **)visible_menu_items, visible_menu_count, menu_selection);
        }
        break;
    }
    case APP_MODE_GAMES:
        mode_games_handle_button(btn);
        if (!mode_games_is_active()) return_home(false);
        break;

    case APP_MODE_VIEW_NOTES: {
        int count = recording_count();
        if (count == 0) { return_home(false); break; }
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
        return_home(false);
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
            finish_current_mode();
            return_home(false);
        } else if (current_sub == SUB_PROCESSING) {
            finish_current_mode();
            return_home(false);
        }
        break;
    }
}

// ── VAD burst for timer wake ────────────────────────────────

static bool handle_vad_burst(void)
{
    epaper_init();
    i2c_bus_init();
    board_power_audio_on();
    es8311_init();
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

    es8311_deinit();
    board_power_audio_off();
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

    // Report prior boot mode before any NVS access
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_BROWNOUT) {
        ESP_LOGW(TAG, "!!! PREVIOUS BOOT ended in BROWN-OUT RESET");
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    bod_check_prior();

    system_init();
    bod_save_stage("system_init");
    ESP_LOGI(TAG, "Stage 2 OK: I/O expander + power rails");
    uint8_t bat_pct = power_get_battery_pct();
    ESP_LOGI(TAG, "Battery: %d%%", bat_pct);

    power_init();
    bod_save_stage("power_init");
    ESP_LOGI(TAG, "Stage 3 OK: ADC + power mgmt");

    // Pre-init shared SPI bus with full config (incl. MISO) before e-paper
    // claims it. E-paper driver omits MISO (write-only), which breaks SD card.
    {
        spi_bus_config_t bus = {
            .mosi_io_num = GPIO_NUM_5,
            .miso_io_num = GPIO_NUM_4,
            .sclk_io_num = GPIO_NUM_6,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 8192,
        };
        esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        }
    }

    epaper_init();
    ESP_LOGI(TAG, "Stage 4 OK: E-paper display initialized");
    bod_save_stage("epaper_init");
    ui_init();
    recordings_init();
    ESP_LOGI(TAG, "Stage 5 OK: UI + storage initialized");

    if (!wifi_has_saved_creds()) {
        ESP_LOGI(TAG, "No WiFi credentials found, starting provisioning");
        ui_show_boot_screen("Setup Mode");
        ESP_LOGI(TAG, "Stage 6 OK: boot screen shown");
        wifi_init();
        esp_log_level_set("wifi", ESP_LOG_ERROR);
        provisioning_start_ap();
        provisioning_start_server();
        ui_show_provisioning_screen("EInk-Voice-Config", "192.168.4.1");
        ESP_LOGI(TAG, "PROVISIONING MODE");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(60000));
        }
    }

    ui_show_boot_screen(DEVICE_NAME);
    ESP_LOGI(TAG, "Stage 6 OK: boot screen shown");

    char ssid[64], password[64];
    wifi_load_creds(ssid, sizeof(ssid), password, sizeof(password));
    ESP_LOGI(TAG, "Stage 7 OK: WiFi creds loaded");
    bod_save_stage("wifi_start");

    wifi_init();
    esp_log_level_set("wifi", ESP_LOG_ERROR);
    ESP_LOGI(TAG, "Stage 8 OK: WiFi init done");

    // Allocate I2S DMA buffers between wifi_init and wifi_connect.
    // wifi_init creates control structures (small), wifi_connect calls
    // esp_wifi_start which allocates ~17KB of static DMA RX buffers.
    // I2S needs its ~4KB of DMA before that pool is consumed.
    i2c_bus_init();
    es8311_init();

    // On battery: cap WiFi TX power to reduce peak-current brownout risk.
    // esp_wifi_set_max_tx_power value is in 0.25dBm units; default is ~78 (+19.5dBm).
    // Setting to 8 caps at +2dBm — sufficient to connect, cuts peak current significantly.
    int bat_mv = power_read_battery_mv();
    if (bat_mv < 3900) {
        ESP_LOGW(TAG, "Low battery (%dmV), capping WiFi TX power to +2dBm", bat_mv);
        esp_wifi_set_max_tx_power(8);
    }

    wifi_connect(ssid, password);
    ESP_LOGI(TAG, "Stage 9: WiFi connecting (up to 30s)...");

    int retries = 0;
    while (!wifi_is_connected() && retries < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retries++;
    }

    if (wifi_is_connected()) {
        ESP_LOGI(TAG, "Stage 10 OK: WiFi connected (%s)", wifi_get_ip());
        bod_save_stage("wifi_connected");

        ESP_LOGI(TAG, "Stage 11: Authenticating with Hermes server...");
        char auth_url[128];
        snprintf(auth_url, sizeof(auth_url), "%s/api/device/auth", HERMES_HTTP_URL);
        char auth_body[128];
        snprintf(auth_body, sizeof(auth_body), "{\"device_id\":\"%s\"}", DEVICE_ID);
        char auth_resp[64];
        esp_err_t auth_ret = http_post_json(auth_url, auth_body, auth_resp, sizeof(auth_resp));
        if (auth_ret == ESP_OK) {
            ESP_LOGI(TAG, "Stage 11 OK: Hermes auth success");
        } else {
            ESP_LOGW(TAG, "Stage 11: Hermes auth failed: %s", esp_err_to_name(auth_ret));
        }
        bod_save_stage("hermes_auth");

        ESP_LOGI(TAG, "Stage 12: Finalizing audio + buttons...");
        recordings_init_audio();
        audio_pipeline_init();
        audio_pipeline_set_wake_failed_cb(on_wake_failed);
        audio_pipeline_set_recording_ended_cb(on_recording_ended);
        audio_pipeline_set_response_cb(on_response);
        button_set_callback(handle_button);
        button_set_longpress_callback(handle_longpress, 1500);
        buttons_init();
        ESP_LOGI(TAG, "Stage 13 OK: Audio, buttons ready");
        bod_save_stage("ready_for_home");
    } else {
        ESP_LOGW(TAG, "Stage 10: WiFi not connected, starting provisioning");
        esp_wifi_stop();
        provisioning_start_ap();
        provisioning_start_server();
        ui_show_provisioning_screen("EInk-Voice-Config", "192.168.4.1");
        ESP_LOGI(TAG, "PROVISIONING MODE — waiting for WiFi");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(60000));
        }
    }
    if (get_charging_state()) {
        ESP_LOGI(TAG, "Already charging at boot, entering docked state");
        was_charging = true;
        audio_pipeline_set_docked(true);
        ui_show_docked_screen();
    } else {
        ui_show_home_screen();
        rebuild_visible_menu();
    }

    ESP_LOGI(TAG, "Initialization complete, entering main loop");
    bod_save_stage("main_loop");

    int telemetry_ticks = 0;
    int bod_watchdog = 0;

    while (1) {
        bod_watchdog++;
        if (bod_watchdog % 12 == 0) bod_save_stage("main_loop_idle");
        uint8_t battery = power_get_battery_pct();
        ui_update_battery(battery);
        ui_update_wifi_status(wifi_is_connected());
        ui_update_hermes_status(wifi_is_connected());

        // Handle wake word failure - return to home screen
        if (wake_failed_pending) {
            wake_failed_pending = false;
            if (current_app_mode == APP_MODE_HOME && current_sub == SUB_MENU) {
                ui_show_home_screen();
            }
        }

        // ── Charging state transitions ──────────────────────
        bool now_charging = get_charging_state();

        if (now_charging != was_charging) {
            if (now_charging) {
                ESP_LOGI(TAG, "Charging detected");
                if (current_app_mode == APP_MODE_HOME && current_sub == SUB_MENU && !docked_user_exited) {
                    audio_pipeline_set_docked(true);
                    ui_show_docked_screen();
                }
            } else {
                ESP_LOGI(TAG, "Charging disconnected");
                docked_user_exited = false;
                if (audio_pipeline_is_docked() && current_app_mode == APP_MODE_HOME) {
                    audio_pipeline_set_docked(false);
                    return_home(false);
                }
            }
            was_charging = now_charging;
            telemetry_ticks = 6; // Send telemetry immediately on state change
        }

        // ── Periodic telemetry + mDNS TXT update (every ~30s) ──
        telemetry_ticks++;
        if (telemetry_ticks >= 6 && wifi_is_connected()) {
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

            char url[128];
            snprintf(url, sizeof(url), "%s/api/device/telemetry", HERMES_HTTP_URL);
            char resp[64];
            http_post_json(url, json, resp, sizeof(resp));

            // Update mDNS TXT records with live values
            char buf[16];
            snprintf(buf, sizeof(buf), "%u", bat);
            mdns_service_txt_item_set("_eink-voice-gateway", "_tcp", "battery", buf);
            snprintf(buf, sizeof(buf), "%s", chg ? "true" : "false");
            mdns_service_txt_item_set("_eink-voice-gateway", "_tcp", "charging", buf);
            snprintf(buf, sizeof(buf), "%d", rssi);
            mdns_service_txt_item_set("_eink-voice-gateway", "_tcp", "wifi_rssi", buf);
        }

        // Docked mode (sleep timer suppressed) only when TCA9554 is present AND
        // charger pin is active. Without the expander we cannot detect USB, so
        // the device conservatively follows normal battery sleep behaviour.
        bool docked = system_tca9554_present() && now_charging;
        if (!docked && power_should_sleep()) {
            ESP_LOGI(TAG, "Entering deep sleep...");
            ui_show_sleep_screen();
            vTaskDelay(pdMS_TO_TICKS(100));
            power_enter_deep_sleep(0);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
