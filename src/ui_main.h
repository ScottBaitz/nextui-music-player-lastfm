#ifndef __UI_MAIN_H__
#define __UI_MAIN_H__

#include <stdint.h>
#include <SDL2/SDL.h>

// Render the main menu (Local Files, Online Radio, Downloader, About)
void render_menu(SDL_Surface* screen, int show_setting, int menu_selected,
                 char* toast_message, uint32_t toast_time);

// Render quit confirmation dialog overlay
void render_quit_confirm(SDL_Surface* screen);

// Render delete confirmation dialog overlay
void render_delete_confirm(SDL_Surface* screen, const char* filename);

// Render controls help dialog overlay
void render_controls_help(SDL_Surface* screen, int app_state);

// Render screen off hint message
void render_screen_off_hint(SDL_Surface* screen);

#endif
