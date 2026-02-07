#ifndef __MODULE_COMMON_H__
#define __MODULE_COMMON_H__

#include <SDL2/SDL.h>
#include <stdbool.h>

// Toast duration for all modules (3 seconds)
#define TOAST_DURATION 3000

// Screen off hint duration (time hint is shown before screen turns off)
#define SCREEN_OFF_HINT_DURATION_MS 4000

// Module exit reasons
typedef enum {
    MODULE_EXIT_TO_MENU,    // User pressed B, return to main menu
    MODULE_EXIT_QUIT        // User confirmed quit, exit app entirely
} ModuleExitReason;

// Result from global input handling
typedef struct {
    bool input_consumed;    // True if global input was handled (dialog shown, etc.)
    bool should_quit;       // True if quit was confirmed
    bool dirty;             // True if screen needs redraw
} GlobalInputResult;

// Initialize module common (call once at app startup)
void ModuleCommon_init(void);

// Handle global input (START dialogs, volume, power management)
// Call at the start of each module's input loop
// Parameters:
//   screen - SDL surface for rendering dialogs
//   show_setting - pointer to show_setting flag (for power hints)
//   app_state - current app state (for controls help context)
GlobalInputResult ModuleCommon_handleGlobalInput(SDL_Surface* screen, int* show_setting, int app_state);

// Check if screen is currently off (for modules that need to know)
bool ModuleCommon_isScreenOff(void);

// Set screen off state (for modules that support screen off during playback)
void ModuleCommon_setScreenOff(bool off);

// Disable/enable autosleep (for modules with active playback)
void ModuleCommon_setAutosleepDisabled(bool disabled);

// Record last input time (for screen off timeout)
void ModuleCommon_recordInputTime(void);

// Check if screen off hint is active
bool ModuleCommon_isScreenOffHintActive(void);

// Start screen off hint countdown
void ModuleCommon_startScreenOffHint(void);

// Reset (cancel) screen off hint
void ModuleCommon_resetScreenOffHint(void);

// Check screen off hint timeout using dual SDL tick + wallclock check.
// If timed out: deactivates hint and disables backlight. Returns true.
// If still counting down or hint not active: returns false.
bool ModuleCommon_processScreenOffHintTimeout(void);

// Clean up module common resources (call at app exit)
void ModuleCommon_quit(void);

// PWR_update wrapper with overlay auto-hide on button release
// Call this instead of PWR_update directly in modules
void ModuleCommon_PWR_update(int* dirty, int* show_setting);

#endif
