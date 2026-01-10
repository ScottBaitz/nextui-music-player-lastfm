#ifndef __UI_UTILS_H__
#define __UI_UTILS_H__

#include <stdbool.h>
#include <stdint.h>
#include "defines.h"  // Brings in SDL2 via platform.h -> sdl.h
#include "api.h"      // For SDL types and TTF
#include "player.h"   // For AudioFormat

// Format duration as MM:SS
void format_time(char* buf, int ms);

// Get format name string
const char* get_format_name(AudioFormat format);

// Scrolling text state for marquee animation
typedef struct {
    char text[512];         // Text to display
    int text_width;         // Full text width in pixels
    int max_width;          // Maximum display width
    uint32_t start_time;    // Animation start time
    bool needs_scroll;      // True if text is wider than max_width
} ScrollTextState;

// Reset scroll state for new text
void ScrollText_reset(ScrollTextState* state, const char* text, TTF_Font* font, int max_width);

// Check if scrolling is active (text needs to scroll)
bool ScrollText_isScrolling(ScrollTextState* state);

// Render scrolling text (call every frame)
void ScrollText_render(ScrollTextState* state, TTF_Font* font, SDL_Color color,
                       SDL_Surface* screen, int x, int y);

// Unified update: checks for text change, resets if needed, and renders
void ScrollText_update(ScrollTextState* state, const char* text, TTF_Font* font,
                       int max_width, SDL_Color color, SDL_Surface* screen, int x, int y);

#endif
