#ifndef __MODULE_SYSTEM_H__
#define __MODULE_SYSTEM_H__

#include <SDL2/SDL.h>
#include "module_common.h"

// Run the system/about module
// Handles: About screen, app updates
ModuleExitReason SystemModule_run(SDL_Surface* screen);

#endif
