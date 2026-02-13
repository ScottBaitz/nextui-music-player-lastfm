#ifndef __UI_MAIN_H__
#define __UI_MAIN_H__

#include <stdint.h>
#include <SDL2/SDL.h>

// Render the main menu (Library, Online Radio, Downloader, About)
void render_menu(SDL_Surface* screen, int show_setting, int menu_selected,
                 char* toast_message, uint32_t toast_time);

// Render confirmation dialog overlay (title + optional content + "A: Yes  B: No")
void render_confirmation_dialog(SDL_Surface* screen, const char* content, const char* title);

// Render controls help dialog overlay
void render_controls_help(SDL_Surface* screen, int app_state);

// Render screen off hint message
void render_screen_off_hint(SDL_Surface* screen);

#endif
