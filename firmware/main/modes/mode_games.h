// Mode: Tomagotchi
#pragma once
#include <stdbool.h>

void mode_games_start(void);
void mode_games_handle_button(int btn);
void mode_games_finish(void);
bool mode_games_is_active(void);
