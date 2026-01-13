#ifndef __UI_ICONS_H__
#define __UI_ICONS_H__

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "player.h"  // For AudioFormat

// Initialize icons (load from files and create inverted versions)
void Icons_init(void);

// Cleanup icons
void Icons_quit(void);

// Get icon for a file type (returns inverted or original based on selected state)
// selected=false -> white icon (inverted, for black background)
// selected=true -> black icon (original, for white/light selected background)
SDL_Surface* Icons_getFolder(bool selected);
SDL_Surface* Icons_getAudio(bool selected);
SDL_Surface* Icons_getPlayAll(bool selected);
SDL_Surface* Icons_getForFormat(AudioFormat format, bool selected);

// Get menu icons
SDL_Surface* Icons_getMenuLocal(bool selected);
SDL_Surface* Icons_getMenuRadio(bool selected);
SDL_Surface* Icons_getMenuDownload(bool selected);
SDL_Surface* Icons_getMenuAbout(bool selected);
SDL_Surface* Icons_getMenuByIndex(int index, bool selected);

// Get YouTube/Downloader menu icons
SDL_Surface* Icons_getSearch(bool selected);
SDL_Surface* Icons_getUpdate(bool selected);
SDL_Surface* Icons_getYouTubeMenuByIndex(int index, bool selected);

// Check if icons are loaded
bool Icons_isLoaded(void);

#endif // __UI_ICONS_H__
