// Mode: Games
#pragma once
typedef enum {
    GAME_TRIVIA,
    GAME_WORD,
    GAME_20Q,
    GAME_MATH,
    GAME_COUNT
} game_id_t;
void mode_games_start(void);
void mode_games_select(game_id_t game);
void mode_games_handle_input(const char *input);
