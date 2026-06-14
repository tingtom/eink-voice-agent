#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.h"
#include "epaper_driver.h"
#include "mode_games.h"

static const char *TAG = "TOMAGOTCHI";

// ── Pet ─────────────────────────────────────────────────────

typedef enum {
    ACTION_FEED,
    ACTION_PLAY,
    ACTION_SLEEP,
    ACTION_CLEAN,
    ACTION_EXIT,
    ACTION_COUNT
} action_t;

static const char *action_names[] = {"Feed", "Play", "Sleep", "Clean", "Exit"};

typedef struct {
    uint8_t hunger;
    uint8_t happiness;
    uint8_t energy;
    uint8_t health;
    uint32_t age_sec;
    int64_t last_tick_us;
    bool alive;
} pet_save_t;

static pet_save_t pet;
static bool active = false;
static action_t sel = 0;
static int64_t game_start_us = 0;
static char result_buf[32] = {0};
static bool show_result = false;
static int64_t result_time_us = 0;
static int eye_frame = 0; // blink animation counter

#define DECAY_INTERVAL_US (30000000LL)   // 30s real-time decay
#define RESULT_SHOW_US    (2000000LL)    // 2s result banner
#define NVS_NAMESPACE     "tomagotchi"
#define NVS_KEY           "pet"

// ── NVS persistence ─────────────────────────────────────────

static void pet_save(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_set_blob(nvs, NVS_KEY, &pet, sizeof(pet));
    nvs_commit(nvs);
    nvs_close(nvs);
}

static bool pet_load(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) return false;
    size_t size = sizeof(pet);
    esp_err_t err = nvs_get_blob(nvs, NVS_KEY, &pet, &size);
    nvs_close(nvs);
    if (err != ESP_OK || size != sizeof(pet)) return false;
    // Adjust monotonic timer to current boot
    pet.last_tick_us = esp_timer_get_time();
    return true;
}

// ── Stat management ─────────────────────────────────────────

static void clamp_100(uint8_t *v)
{
    if (*v > 100) *v = 100;
}

static void decay_stats(void)
{
    int64_t now = esp_timer_get_time();
    if (now - pet.last_tick_us < DECAY_INTERVAL_US) return;
    pet.last_tick_us = now;

    // Hunger drops 2 per tick
    if (pet.hunger > 0) pet.hunger -= 2;
    // Happiness drops 1 per tick
    if (pet.happiness > 0) pet.happiness -= 1;
    // Energy recovers 1 per tick while awake
    if (pet.energy < 100) pet.energy += 1;
    clamp_100(&pet.energy);

    // Health penalty for starvation or misery
    if (pet.hunger == 0 || pet.happiness == 0) {
        if (pet.health > 2) pet.health -= 2;
        else pet.health = 0;
    } else if (pet.hunger > 50 && pet.happiness > 50 && pet.health < 100) {
        // Slow recovery when well cared for
        pet.health += 1;
        clamp_100(&pet.health);
    }
}

// ── Actions ─────────────────────────────────────────────────

static void do_action(action_t a)
{
    switch (a) {
    case ACTION_FEED:
        if (pet.hunger >= 100) {
            snprintf(result_buf, sizeof(result_buf), "Full!");
        } else {
            pet.hunger += 15;
            clamp_100(&pet.hunger);
            snprintf(result_buf, sizeof(result_buf), "Yum! +15");
        }
        break;

    case ACTION_PLAY:
        if (pet.energy < 10) {
            snprintf(result_buf, sizeof(result_buf), "Too tired!");
        } else {
            pet.happiness += 15;
            pet.energy -= 10;
            clamp_100(&pet.happiness);
            snprintf(result_buf, sizeof(result_buf), "Fun! +15");
        }
        break;

    case ACTION_SLEEP:
        pet.energy += 25;
        pet.happiness += 5;
        clamp_100(&pet.energy);
        clamp_100(&pet.happiness);
        snprintf(result_buf, sizeof(result_buf), "Zzz... +25");
        break;

    case ACTION_CLEAN:
        pet.health += 15;
        pet.happiness += 5;
        clamp_100(&pet.health);
        clamp_100(&pet.happiness);
        snprintf(result_buf, sizeof(result_buf), "Clean! +15");
        break;

    case ACTION_EXIT:
        pet_save();
        active = false;
        return;

    default:
        return;
    }

    show_result = true;
    result_time_us = esp_timer_get_time();
    pet_save();
}

// ── Drawing ─────────────────────────────────────────────────

static void draw_pet_face(void)
{
    int bx = 16, by = 28;  // body top-left

    // ── Antenna ──
    epaper_draw_line(bx + 12, by - 8, bx + 14, by);
    epaper_draw_line(bx + 22, by - 8, bx + 20, by);

    // ── Body ──
    epaper_draw_rect(bx, by, 36, 36, 0);
    epaper_draw_rect(bx + 1, by + 1, 34, 34, 1);

    // ── Sick bandage ──
    if (pet.health <= 30) {
        epaper_draw_rect(bx + 12, by - 4, 12, 10, 1);
        epaper_draw_rect(bx + 11, by, 14, 2, 0);
        epaper_draw_rect(bx + 11, by + 4, 14, 2, 0);
    }

    // ── Eyes ──
    int ex1 = bx + 8, ey1 = by + 11;
    int ex2 = bx + 23, ey2 = by + 11;

    if (pet.happiness >= 50) {
        // Happy open eyes
        epaper_draw_rect(ex1, ey1, 6, 6, 1);
        epaper_draw_rect(ex2, ey2, 6, 6, 1);
        epaper_draw_pixel(ex1 + 1, ey1 + 1, 0);
        epaper_draw_pixel(ex2 + 1, ey2 + 1, 0);
        // Blush
        epaper_draw_rect(bx - 2, by + 18, 5, 3, 1);
        epaper_draw_rect(bx + 33, by + 18, 5, 3, 1);
    } else if (pet.happiness >= 20) {
        // Neutral eyes
        if (eye_frame < 3) {
            epaper_draw_rect(ex1 + 1, ey1 + 2, 4, 4, 1);
            epaper_draw_rect(ex2 + 1, ey2 + 2, 4, 4, 1);
        } else {
            // Blink
            epaper_draw_rect(ex1 + 1, ey1 + 3, 4, 2, 1);
            epaper_draw_rect(ex2 + 1, ey2 + 3, 4, 2, 1);
        }
    } else {
        // Sad / crying eyes
        epaper_draw_line(ex1, ey1 + 2, ex1 + 5, ey1 + 5);
        epaper_draw_line(ex1 + 5, ey1 + 2, ex1, ey1 + 5);
        epaper_draw_line(ex2, ey2 + 2, ex2 + 5, ey2 + 5);
        epaper_draw_line(ex2 + 5, ey2 + 2, ex2, ey2 + 5);
        epaper_draw_pixel(ex1 + 2, ey1 + 8, 1);
        epaper_draw_pixel(ex2 + 3, ey2 + 8, 1);
    }

    // ── Mouth ──
    int mx = bx + 8, my = by + 27;
    if (pet.happiness >= 60) {
        epaper_draw_pixel(mx + 2, my, 1);
        epaper_draw_pixel(mx + 3, my - 1, 1);
        for (int i = 4; i < 16; i++) epaper_draw_pixel(mx + i, my, 1);
        epaper_draw_pixel(mx + 16, my - 1, 1);
        epaper_draw_pixel(mx + 17, my, 1);
        epaper_draw_rect(mx + 7, my + 1, 5, 3, 1);
    } else if (pet.happiness >= 30) {
        epaper_draw_line(mx + 2, my + 1, mx + 15, my + 1);
    } else {
        epaper_draw_pixel(mx + 4, my + 1, 1);
        for (int i = 5; i < 8; i++) epaper_draw_pixel(mx + i, my + 2, 1);
        epaper_draw_pixel(mx + 8, my + 3, 1);
        epaper_draw_pixel(mx + 9, my + 2, 1);
        for (int i = 10; i < 13; i++) epaper_draw_pixel(mx + i, my + 1, 1);
    }
}

static void draw_stats_panel(void)
{
    int sx = 68, sy = 32;
    const char *lbl[] = {"HUN", "HAP", "ENG", "HLT"};
    uint8_t *val[] = {&pet.hunger, &pet.happiness, &pet.energy, &pet.health};

    epaper_draw_text(sx, sy - 8, "Age", 8);
    char age[16];
    uint32_t mins = pet.age_sec / 60;
    snprintf(age, sizeof(age), "%" PRIu32 "m", mins);
    epaper_draw_text(sx + 22, sy - 8, age, 8);

    for (int i = 0; i < 4; i++) {
        int y = sy + i * 18;
        epaper_draw_text(sx, y, lbl[i], 8);
        epaper_draw_rect(sx + 24, y + 1, 48, 6, 0);
        int fill = (int)(*val[i]) * 46 / 100;
        if (fill > 0) {
            epaper_draw_rect(sx + 25, y + 2, fill, 4, 1);
        }
        char pct[8];
        snprintf(pct, sizeof(pct), "%d", *val[i]);
        epaper_draw_text(sx + 76, y - 1, pct, 8);
    }
}

static void draw_action_bar(void)
{
    int ax = 8, ay = 118;

    // Divider line
    epaper_draw_line(4, ay - 4, DISPLAY_WIDTH - 4, ay - 4);

    for (int i = 0; i < ACTION_COUNT; i++) {
        char line[10];
        if (i == sel) {
            snprintf(line, sizeof(line), "[%s]", action_names[i]);
        } else {
            snprintf(line, sizeof(line), " %s ", action_names[i]);
        }
        epaper_draw_text(ax + i * 38, ay, line, 8);
        if (i == sel) {
            epaper_draw_line(ax + i * 38, ay + 10, ax + i * 38 + 32, ay + 10);
        }
    }

    if (show_result) {
        int64_t now = esp_timer_get_time();
        if (now - result_time_us < RESULT_SHOW_US) {
            epaper_draw_text(8, 140, result_buf, 12);
        } else {
            show_result = false;
            epaper_draw_text(8, 140, "UP/DOWN=nav SELECT=act", 8);
        }
    } else {
        epaper_draw_text(8, 140, "UP/DOWN=nav SELECT=act", 8);
    }
    epaper_draw_text(8, 160, "long SELECT=exit", 8);
}

static void draw_game_screen(void)
{
    epaper_clear();

    // Title
    epaper_draw_text(4, 10, "Tomagotchi", 12);

    draw_pet_face();
    draw_stats_panel();
    draw_action_bar();

    // Died message
    if (!pet.alive) {
        epaper_draw_text(40, 75, "DIED", 16);
    }

    epaper_partial_refresh();
}

// ── Public API ──────────────────────────────────────────────

void mode_games_start(void)
{
    ESP_LOGI(TAG, "Tomagotchi started");
    active = true;

    if (!pet_load()) {
        // New pet
        memset(&pet, 0, sizeof(pet));
        pet.hunger = 60;
        pet.happiness = 70;
        pet.energy = 80;
        pet.health = 100;
        pet.alive = true;
        pet.last_tick_us = esp_timer_get_time();
        pet_save();
    }

    game_start_us = esp_timer_get_time();
    sel = 0;
    show_result = false;
    eye_frame = 0;

    draw_game_screen();
}

bool mode_games_is_active(void)
{
    return active;
}

void mode_games_handle_button(int btn)
{
    if (!active) return;

    decay_stats();
    pet.age_sec = (uint32_t)((esp_timer_get_time() - game_start_us) / 1000000);

    if (pet.health == 0) {
        pet.alive = false;
        draw_game_screen();
        return;
    }

    eye_frame++;

    switch (btn) {
    case 0: // BUTTON_UP
        sel = (sel > 0) ? sel - 1 : ACTION_COUNT - 1;
        draw_game_screen();
        break;

    case 1: // BUTTON_DOWN
        sel = (sel < ACTION_COUNT - 1) ? sel + 1 : 0;
        draw_game_screen();
        break;

    case 2: // BUTTON_SELECT
        do_action(sel);
        if (active) draw_game_screen();
        // If action was EXIT, active is now false; caller checks this
        break;
    }
}

void mode_games_finish(void)
{
    pet_save();
    active = false;
}
