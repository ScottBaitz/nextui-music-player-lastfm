#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "module_common.h"
#include "module_menu.h"
#include "ui_main.h"
#include "resume.h"

// Toast message state
static char menu_toast_message[128] = "";
static uint32_t menu_toast_time = 0;

int MenuModule_run(SDL_Surface* screen) {
    int menu_selected = 0;
    int dirty = 1;
    int show_setting = 0;

    while (1) {
        PAD_poll();

        bool has_resume = Resume_isAvailable();
        int item_count = has_resume ? 5 : 4;

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
            menu_selected = (menu_selected > 0) ? menu_selected - 1 : item_count - 1;
            GFX_clearLayers(LAYER_SCROLLTEXT);
            dirty = 1;
        }
        else if (PAD_justRepeated(BTN_DOWN)) {
            menu_selected = (menu_selected < item_count - 1) ? menu_selected + 1 : 0;
            GFX_clearLayers(LAYER_SCROLLTEXT);
            dirty = 1;
        }
        else if (PAD_justPressed(BTN_A)) {
            GFX_clearLayers(LAYER_SCROLLTEXT);
            // Adjust selection to match MENU_* constants
            int selection = menu_selected;
            if (!has_resume) selection += 1;  // Skip MENU_RESUME slot
            return selection;
        }
        else if (PAD_justPressed(BTN_X)) {
            // Clear resume history when X pressed on Resume item
            if (has_resume && menu_selected == 0) {
                Resume_clear();
                GFX_clearLayers(LAYER_SCROLLTEXT);
                menu_selected = 0;
                dirty = 1;
            }
        }
        else if (PAD_justPressed(BTN_B)) {
            GFX_clearLayers(LAYER_SCROLLTEXT);
            // Exit app from main menu
            return MENU_QUIT;
        }

        // Handle power management
        ModuleCommon_PWR_update(&dirty, &show_setting);

        // Render
        if (dirty) {
            render_menu(screen, show_setting, menu_selected,
                        menu_toast_message, menu_toast_time, has_resume);

            if (show_setting) {
                GFX_blitHardwareHints(screen, show_setting);
            }

            GFX_flip(screen);
            dirty = 0;

            // Keep refreshing while toast is visible
            ModuleCommon_tickToast(menu_toast_message, menu_toast_time, &dirty);
        } else {
            // Software scroll needs continuous redraws
            if (menu_needs_scroll_redraw()) dirty = 1;
            GFX_sync();
        }
    }
}

// Set toast message (called by modules that return to menu with a message)
void MenuModule_setToast(const char* message) {
    snprintf(menu_toast_message, sizeof(menu_toast_message), "%s", message);
    menu_toast_time = SDL_GetTicks();
}
