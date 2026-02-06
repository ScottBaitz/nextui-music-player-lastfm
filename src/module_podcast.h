#ifndef __MODULE_PODCAST_H__
#define __MODULE_PODCAST_H__

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "module_common.h"

// Run the podcast module
// Handles: Subscriptions, search, top shows, episodes, playback
ModuleExitReason PodcastModule_run(SDL_Surface* screen);

// Check if podcast module is active (playing)
bool PodcastModule_isActive(void);

#endif
