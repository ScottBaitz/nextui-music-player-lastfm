#ifndef __RADIO_ALBUM_ART_H__
#define __RADIO_ALBUM_ART_H__

#include <stdbool.h>

// Forward declaration for SDL_Surface
struct SDL_Surface;

// Initialize album art module
void radio_album_art_init(void);

// Cleanup album art module
void radio_album_art_cleanup(void);

// Fetch album art for artist/title (async, non-blocking)
// Call this when metadata changes
void radio_album_art_fetch(const char* artist, const char* title);

// Get current album art surface (NULL if none or still fetching)
struct SDL_Surface* radio_album_art_get(void);

// Check if a fetch is in progress
bool radio_album_art_is_fetching(void);

// Clear current album art and reset state
void radio_album_art_clear(void);

#endif
