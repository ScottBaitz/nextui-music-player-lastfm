#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "module_common.h"
#include "module_menu.h"
#include "ui_main.h"

// Menu item count
#define MENU_ITEM_COUNT 5

// Toast message state
static char menu_toast_message[128] = "";
static uint32_t menu_toast_time = 0;

// Toast duration (shared with other modules)
#define TOAST_DURATION 3000

int MenuModule_run(SDL_Surface* screen) {
    int menu_selected = 0;
    int dirty = 1;
    int show_setting = 0;

    while (1) {
        uint32_t frame_start = SDL_GetTicks();
        PAD_poll();

        // Handle global input first (volume, START dialogs, power)
        GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, 0);
        if (global.should_quit) {
            return MENU_QUIT;
        }
        if (global.input_consumed) {
            if (global.dirty) dirty = 1;
            GFX_sync();
            continue;
        }

        // Menu navigation
        if (PAD_justRepeated(BTN_UP)) {
            menu_selected = (menu_selected > 0) ? menu_selected - 1 : MENU_ITEM_COUNT - 1;
            dirty = 1;
        }
        else if (PAD_justRepeated(BTN_DOWN)) {
            menu_selected = (menu_selected < MENU_ITEM_COUNT - 1) ? menu_selected + 1 : 0;
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_A)) {
            // Return selected menu item
            return menu_selected;
        }
        else if (PAD_justPressed(BTN_B)) {
            // Exit app from main menu
            return MENU_QUIT;
        }

        // Handle power management
        PWR_update(&dirty, &show_setting, NULL, NULL);

        // Render
        if (dirty) {
            render_menu(screen, show_setting, menu_selected,
                        menu_toast_message, menu_toast_time);

            if (show_setting) {
                GFX_blitHardwareHints(screen, show_setting);
            }

            GFX_flip(screen);
            dirty = 0;

            // Keep refreshing while toast is visible
            if (menu_toast_message[0] != '\0') {
                if (SDL_GetTicks() - menu_toast_time < TOAST_DURATION) {
                    dirty = 1;
                } else {
                    menu_toast_message[0] = '\0';
                }
            }
        } else {
            GFX_sync();
        }
    }
}

// Set toast message (called by modules that return to menu with a message)
void MenuModule_setToast(const char* message) {
    strncpy(menu_toast_message, message, sizeof(menu_toast_message) - 1);
    menu_toast_message[sizeof(menu_toast_message) - 1] = '\0';
    menu_toast_time = SDL_GetTicks();
}
