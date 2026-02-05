#ifndef __MODULE_PLAYER_H__
#define __MODULE_PLAYER_H__

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "module_common.h"

// Run the local files player module
// Handles: File browser, music playback, playlist, shuffle/repeat
ModuleExitReason PlayerModule_run(SDL_Surface* screen);

// Check if music player module is active (playing/paused)
bool PlayerModule_isActive(void);

// Play next track (for USB HID button support)
void PlayerModule_nextTrack(void);

// Play previous track (for USB HID button support)
void PlayerModule_prevTrack(void);

#endif
