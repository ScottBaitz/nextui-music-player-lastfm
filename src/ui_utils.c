#include <stdio.h>
#include <string.h>
#include "ui_utils.h"

// Scroll text animation parameters
#define SCROLL_PAUSE_MS 1500    // Pause before scrolling starts (milliseconds)
#define SCROLL_SPEED 50         // Scroll speed (pixels per second)
#define SCROLL_GAP 50           // Gap between text end and restart (pixels)

// Format duration as MM:SS
void format_time(char* buf, int ms) {
    int total_secs = ms / 1000;
    int mins = total_secs / 60;
    int secs = total_secs % 60;
    sprintf(buf, "%02d:%02d", mins, secs);
}

// Get format name string
const char* get_format_name(AudioFormat format) {
    switch (format) {
        case AUDIO_FORMAT_MP3: return "MP3";
        case AUDIO_FORMAT_FLAC: return "FLAC";
        case AUDIO_FORMAT_OGG: return "OGG";
        case AUDIO_FORMAT_WAV: return "WAV";
        case AUDIO_FORMAT_MOD: return "MOD";
        default: return "---";
    }
}

// Reset scroll state for new text
void ScrollText_reset(ScrollTextState* state, const char* text, TTF_Font* font, int max_width) {
    strncpy(state->text, text, sizeof(state->text) - 1);
    state->text[sizeof(state->text) - 1] = '\0';
    int text_h = 0;
    TTF_SizeUTF8(font, state->text, &state->text_width, &text_h);
    state->max_width = max_width;
    state->start_time = SDL_GetTicks();
    state->needs_scroll = (state->text_width > max_width);
}

// Check if scrolling is active (text needs to scroll)
bool ScrollText_isScrolling(ScrollTextState* state) {
    return state->needs_scroll;
}

// Render scrolling text (call every frame)
void ScrollText_render(ScrollTextState* state, TTF_Font* font, SDL_Color color,
                       SDL_Surface* screen, int x, int y) {
    if (!state->text[0]) return;

    // If text fits, render normally without scrolling
    if (!state->needs_scroll) {
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, state->text, color);
        if (surf) {
            SDL_BlitSurface(surf, NULL, screen, &(SDL_Rect){x, y, 0, 0});
            SDL_FreeSurface(surf);
        }
        return;
    }

    // Calculate scroll offset based on elapsed time
    uint32_t elapsed = SDL_GetTicks() - state->start_time;
    int offset = 0;

    if (elapsed > SCROLL_PAUSE_MS) {
        // Total distance for one complete scroll cycle
        int scroll_distance = state->text_width - state->max_width + SCROLL_GAP;
        if (scroll_distance < 1) scroll_distance = 1;
        // Calculate current offset within the cycle
        int scroll_time = elapsed - SCROLL_PAUSE_MS;
        offset = (scroll_time * SCROLL_SPEED / 1000) % (scroll_distance + SCROLL_GAP);

        // Add pause at the end before looping
        if (offset > scroll_distance) {
            offset = scroll_distance;
        }
    }

    // Render full text surface
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, state->text, color);
    if (surf) {
        // Clip source rect based on offset
        SDL_Rect src = {offset, 0, state->max_width, surf->h};
        SDL_Rect dst = {x, y, 0, 0};
        SDL_BlitSurface(surf, &src, screen, &dst);
        SDL_FreeSurface(surf);
    }
}

// Unified update: checks for text change, resets if needed, and renders
void ScrollText_update(ScrollTextState* state, const char* text, TTF_Font* font,
                       int max_width, SDL_Color color, SDL_Surface* screen, int x, int y) {
    // Check if text changed - use existing state->text for comparison
    if (strcmp(state->text, text) != 0) {
        ScrollText_reset(state, text, font, max_width);
    }
    ScrollText_render(state, font, color, screen, x, y);
}
