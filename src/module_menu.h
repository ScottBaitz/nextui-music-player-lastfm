#ifndef __MODULE_MENU_H__
#define __MODULE_MENU_H__

#include <SDL2/SDL.h>

// Menu selection results
#define MENU_LIBRARY        0
#define MENU_RADIO          1
#define MENU_PODCAST        2
#define MENU_SETTINGS       3
#define MENU_QUIT          -1

// Run the main menu
// Returns: menu item index (0-5) or MENU_QUIT (-1) if user wants to exit
int MenuModule_run(SDL_Surface* screen);

// Set toast message (called by modules returning to menu with a message)
void MenuModule_setToast(const char* message);

#endif
