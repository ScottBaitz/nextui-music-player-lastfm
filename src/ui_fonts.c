#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#include "defines.h"
#include "api.h"
#include "ui_fonts.h"

// Custom fonts for the interface (except buttons)
// Different font variants for different elements
typedef struct {
    TTF_Font* title;      // Track title (Regular)
    TTF_Font* artist;     // Artist name (Medium)
    TTF_Font* album;      // Album name (Bold)
    TTF_Font* time_large; // Time display (Regular large)
    TTF_Font* badge;      // Format badge, small text (Regular small)
    TTF_Font* tiny;       // Genre, bitrate (Regular tiny)
    bool loaded;          // True if custom fonts were loaded
} CustomFonts;

static CustomFonts custom_font = {0};

// Load custom fonts from pak folder
// Uses different font variants:
// - JetBrainsMono-Regular.ttf for title
// - JetBrainsMono-Medium.ttf for artist
// - JetBrainsMono-Bold.ttf for album
void load_custom_fonts(void) {
    char regular_path[512], medium_path[512], bold_path[512];

    // Try pak folder first, then current directory
    const char* search_paths[] = {
        "%s/.system/tg5040/paks/Emus/Music Player.pak",
        "."
    };

    bool found = false;
    const char* base_path = NULL;
    (void)base_path;  // Unused but kept for potential future use

    for (int i = 0; i < 2 && !found; i++) {
        char test_path[512];
        if (i == 0) {
            snprintf(test_path, sizeof(test_path), search_paths[0], SDCARD_PATH);
        } else {
            strcpy(test_path, search_paths[1]);
        }

        snprintf(regular_path, sizeof(regular_path), "%s/fonts/JetBrainsMono-Regular.ttf", test_path);
        snprintf(medium_path, sizeof(medium_path), "%s/fonts/JetBrainsMono-Medium.ttf", test_path);
        snprintf(bold_path, sizeof(bold_path), "%s/fonts/JetBrainsMono-Bold.ttf", test_path);

        if (access(regular_path, F_OK) == 0 &&
            access(medium_path, F_OK) == 0 &&
            access(bold_path, F_OK) == 0) {
            found = true;
            base_path = test_path;
        }
    }

    if (!found) {
        custom_font.loaded = false;
        return;
    }

    // Load font variants at appropriate sizes
    // Title uses Regular at extra large size (2x artist size)
    custom_font.title = TTF_OpenFont(regular_path, SCALE1(28));
    // Artist uses Medium at medium size
    custom_font.artist = TTF_OpenFont(medium_path, SCALE1(FONT_MEDIUM));
    // Album uses Bold at small size
    custom_font.album = TTF_OpenFont(bold_path, SCALE1(FONT_SMALL));
    // Time display uses Regular at medium size
    custom_font.time_large = TTF_OpenFont(regular_path, SCALE1(FONT_MEDIUM));
    // Badge/small text uses Regular at small size
    custom_font.badge = TTF_OpenFont(regular_path, SCALE1(FONT_SMALL));
    // Tiny text uses Regular at tiny size
    custom_font.tiny = TTF_OpenFont(regular_path, SCALE1(FONT_TINY));

    if (custom_font.title && custom_font.artist && custom_font.album &&
        custom_font.time_large && custom_font.badge && custom_font.tiny) {
        custom_font.loaded = true;
    } else {
        // Failed to load, cleanup partial loads
        if (custom_font.title) { TTF_CloseFont(custom_font.title); custom_font.title = NULL; }
        if (custom_font.artist) { TTF_CloseFont(custom_font.artist); custom_font.artist = NULL; }
        if (custom_font.album) { TTF_CloseFont(custom_font.album); custom_font.album = NULL; }
        if (custom_font.time_large) { TTF_CloseFont(custom_font.time_large); custom_font.time_large = NULL; }
        if (custom_font.badge) { TTF_CloseFont(custom_font.badge); custom_font.badge = NULL; }
        if (custom_font.tiny) { TTF_CloseFont(custom_font.tiny); custom_font.tiny = NULL; }
        custom_font.loaded = false;
    }
}

// Cleanup custom fonts
void unload_custom_fonts(void) {
    if (custom_font.title) { TTF_CloseFont(custom_font.title); custom_font.title = NULL; }
    if (custom_font.artist) { TTF_CloseFont(custom_font.artist); custom_font.artist = NULL; }
    if (custom_font.album) { TTF_CloseFont(custom_font.album); custom_font.album = NULL; }
    if (custom_font.time_large) { TTF_CloseFont(custom_font.time_large); custom_font.time_large = NULL; }
    if (custom_font.badge) { TTF_CloseFont(custom_font.badge); custom_font.badge = NULL; }
    if (custom_font.tiny) { TTF_CloseFont(custom_font.tiny); custom_font.tiny = NULL; }
    custom_font.loaded = false;
}

// Get font for specific element (custom or system fallback)
// Title font (Regular large) - for track title
TTF_Font* get_font_title(void) {
    return custom_font.loaded ? custom_font.title : font.large;
}

// Artist font (Medium) - for artist name
TTF_Font* get_font_artist(void) {
    return custom_font.loaded ? custom_font.artist : font.medium;
}

// Album font (Bold medium) - for album name
TTF_Font* get_font_album(void) {
    return custom_font.loaded ? custom_font.album : font.medium;
}

// Large font for general use (menus, time display)
TTF_Font* get_font_large(void) {
    return custom_font.loaded ? custom_font.time_large : font.large;
}

// Medium font for general use (lists, info)
TTF_Font* get_font_medium(void) {
    return custom_font.loaded ? custom_font.artist : font.medium;
}

// Small font (badges, secondary text)
TTF_Font* get_font_small(void) {
    return custom_font.loaded ? custom_font.badge : font.small;
}

// Tiny font (genre, bitrate)
TTF_Font* get_font_tiny(void) {
    return custom_font.loaded ? custom_font.tiny : font.tiny;
}
