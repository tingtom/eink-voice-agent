#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "ui_manager.h"
#include "mode_games.h"

static const char *TAG = "MODE_GAMES";
static bool active = false;
static game_id_t current_game = GAME_TRIVIA;

static const char *game_names[GAME_COUNT] = {
    "Trivia Quiz",
    "Word Game",
    "20 Questions",
    "Math Challenge",
};

void mode_games_start(void)
{
    ESP_LOGI(TAG, "Games mode started");
    active = true;
    ui_show_menu(game_names, GAME_COUNT, current_game);
}

void mode_games_select(game_id_t game)
{
    current_game = game;
    ESP_LOGI(TAG, "Selected game: %d", game);

    char msg[64];
    snprintf(msg, sizeof(msg), "%s selected!\nSay 'start' to play.", game_names[game]);
    ui_show_response(msg);
}

void mode_games_handle_input(const char *input)
{
    (void)input;
    if (!active) return;
    ui_show_processing_screen();
}
