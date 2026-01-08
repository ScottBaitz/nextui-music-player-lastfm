#ifndef __UI_MUSIC_H__
#define __UI_MUSIC_H__

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "browser.h"
#include "player.h"

// Render the file browser screen
void render_browser(SDL_Surface* screen, int show_setting, BrowserContext* browser);

// Render the now playing screen
void render_playing(SDL_Surface* screen, int show_setting, BrowserContext* browser,
                    bool shuffle_enabled, bool repeat_enabled);

// Render the main menu
void render_menu(SDL_Surface* screen, int show_setting, int menu_selected);

// Render quit confirmation dialog overlay
void render_quit_confirm(SDL_Surface* screen);

#endif
