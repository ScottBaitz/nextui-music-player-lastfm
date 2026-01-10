#ifndef __UI_YOUTUBE_H__
#define __UI_YOUTUBE_H__

#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdbool.h>
#include "youtube.h"

// Render YouTube sub-menu
void render_youtube_menu(SDL_Surface* screen, int show_setting, int menu_selected);

// Render YouTube searching status
void render_youtube_searching(SDL_Surface* screen, int show_setting, const char* search_query);

// Render YouTube search results
void render_youtube_results(SDL_Surface* screen, int show_setting,
                            const char* search_query,
                            YouTubeResult* results, int result_count,
                            int selected, int* scroll,
                            char* toast_message, uint32_t toast_time, bool searching);

// Render YouTube download queue
void render_youtube_queue(SDL_Surface* screen, int show_setting,
                          int queue_selected, int* queue_scroll);

// Render YouTube downloading progress
void render_youtube_downloading(SDL_Surface* screen, int show_setting);

// Render YouTube yt-dlp update progress
void render_youtube_updating(SDL_Surface* screen, int show_setting);

// Check if YouTube results list has active scrolling (for refresh optimization)
bool youtube_results_needs_scroll_refresh(void);

// Check if YouTube queue list has active scrolling (for refresh optimization)
bool youtube_queue_needs_scroll_refresh(void);

#endif
