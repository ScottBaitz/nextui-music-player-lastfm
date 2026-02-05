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

// Play next episode (for USB HID button support)
// Returns true if successful, false if no next episode or episode not downloaded
bool PodcastModule_nextEpisode(void);

// Play previous episode (for USB HID button support)
// Returns true if successful, false if no previous episode or episode not downloaded
bool PodcastModule_prevEpisode(void);

#endif
