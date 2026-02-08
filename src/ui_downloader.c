#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "api.h"
#include "ui_downloader.h"
#include "ui_fonts.h"
#include "ui_utils.h"
#include "ui_icons.h"
#include "module_common.h"

// Scroll text state for YouTube results (selected item)
static ScrollTextState downloader_results_scroll_text = {0};

// Scroll text state for YouTube download queue (selected item)
static ScrollTextState downloader_queue_scroll_text = {0};

// YouTube sub-menu items
static const char* youtube_menu_items[] = {"Search Music", "Download Queue", "Update yt-dlp"};
#define YOUTUBE_MENU_COUNT 3

// Label callback for queue count on Download Queue menu item
static const char* youtube_menu_get_label(int index, const char* default_label,
                                          char* buffer, int buffer_size) {
    if (index == 1) {  // Download Queue
        int qcount = Downloader_queueCount();
        if (qcount > 0) {
            snprintf(buffer, buffer_size, "Download Queue (%d)", qcount);
            return buffer;
        }
    }
    return NULL;  // Use default label
}

// Render YouTube sub-menu
void render_downloader_menu(SDL_Surface* screen, int show_setting, int menu_selected,
                         char* toast_message, uint32_t toast_time) {
    SimpleMenuConfig config = {
        .title = "Downloader",
        .items = youtube_menu_items,
        .item_count = YOUTUBE_MENU_COUNT,
        .btn_b_label = "BACK",
        .get_label = youtube_menu_get_label,
        .render_badge = NULL,
        .get_icon = NULL
    };
    render_simple_menu(screen, show_setting, menu_selected, &config);

    // Toast notification
    render_toast(screen, toast_message, toast_time);
}

// Render YouTube searching status
void render_downloader_searching(SDL_Surface* screen, int show_setting, const char* search_query) {
    GFX_clear(screen);

    int hw = screen->w;
    int hh = screen->h;

    render_screen_header(screen, "Searching...", show_setting);

    // Searching message
    char search_msg[300];
    snprintf(search_msg, sizeof(search_msg), "Searching for: %s", search_query);
    SDL_Surface* query_text = TTF_RenderUTF8_Blended(Fonts_getMedium(), search_msg, COLOR_GRAY);
    if (query_text) {
        int qx = (hw - query_text->w) / 2;
        if (qx < SCALE1(PADDING)) qx = SCALE1(PADDING);
        SDL_BlitSurface(query_text, NULL, screen, &(SDL_Rect){qx, hh / 2 - SCALE1(30)});
        SDL_FreeSurface(query_text);
    }

    // Loading indicator
    const char* loading = "Please wait...";
    SDL_Surface* load_text = TTF_RenderUTF8_Blended(Fonts_getMedium(), loading, COLOR_WHITE);
    if (load_text) {
        SDL_BlitSurface(load_text, NULL, screen, &(SDL_Rect){(hw - load_text->w) / 2, hh / 2 + SCALE1(10)});
        SDL_FreeSurface(load_text);
    }
}

// Render YouTube search results
void render_downloader_results(SDL_Surface* screen, int show_setting,
                            const char* search_query,
                            DownloaderResult* results, int result_count,
                            int selected, int* scroll,
                            char* toast_message, uint32_t toast_time, bool searching) {
    GFX_clear(screen);

    int hw = screen->w;
    int hh = screen->h;
    char truncated[256];

    // Title with search query
    char title[128];
    snprintf(title, sizeof(title), "Results: %s", search_query);
    render_screen_header(screen, title, show_setting);

    // Use common list layout calculation
    ListLayout layout = calc_list_layout(screen, 0);

    // Adjust scroll (only if there's a selection)
    if (selected >= 0) {
        adjust_list_scroll(selected, scroll, layout.items_per_page);
    }

    // Reserve space for duration on the right (format: "99:59" max)
    int dur_w, dur_h;
    TTF_SizeUTF8(Fonts_getTiny(), "99:59", &dur_w, &dur_h);
    int duration_reserved = dur_w + SCALE1(PADDING * 2);  // Duration width + gap
    int max_width = layout.max_width - duration_reserved;

    for (int i = 0; i < layout.items_per_page && *scroll + i < result_count; i++) {
        int idx = *scroll + i;
        DownloaderResult* result = &results[idx];
        bool is_selected = (idx == selected);
        bool in_queue = Downloader_isInQueue(result->video_id);

        int y = layout.list_y + i * layout.item_h;

        // Calculate indicator width if in queue
        int indicator_width = 0;
        if (in_queue) {
            int ind_w, ind_h;
            TTF_SizeUTF8(Fonts_getTiny(), "[+]", &ind_w, &ind_h);
            indicator_width = ind_w + SCALE1(4);
        }

        // Calculate text width for pill sizing
        int pill_width = Fonts_calcListPillWidth(Fonts_getMedium(), result->title, truncated, max_width, indicator_width);

        // Background pill (sized to text width)
        SDL_Rect pill_rect = {SCALE1(PADDING), y, pill_width, layout.item_h};
        Fonts_drawListItemBg(screen, &pill_rect, is_selected);

        int title_x = SCALE1(PADDING) + SCALE1(BUTTON_PADDING);
        int text_y = y + (layout.item_h - TTF_FontHeight(Fonts_getMedium())) / 2;

        // Show indicator if already in queue
        if (in_queue) {
            SDL_Surface* indicator = TTF_RenderUTF8_Blended(Fonts_getTiny(), "[+]", is_selected ? uintToColour(THEME_COLOR5_255) : COLOR_GRAY);
            if (indicator) {
                SDL_BlitSurface(indicator, NULL, screen, &(SDL_Rect){title_x, y + (layout.item_h - indicator->h) / 2});
                title_x += indicator->w + SCALE1(4);
                SDL_FreeSurface(indicator);
            }
        }

        // Title - use common text rendering with scrolling for selected items
        int title_max_w = pill_width - SCALE1(BUTTON_PADDING * 2) - indicator_width;
        render_list_item_text(screen, &downloader_results_scroll_text, result->title, Fonts_getMedium(),
                              title_x, text_y, title_max_w, is_selected);

        // Duration (always on right, outside pill)
        if (result->duration_sec > 0) {
            char dur[16];
            int m = result->duration_sec / 60;
            int s = result->duration_sec % 60;
            snprintf(dur, sizeof(dur), "%d:%02d", m, s);
            SDL_Surface* dur_text = TTF_RenderUTF8_Blended(Fonts_getTiny(), dur, COLOR_GRAY);
            if (dur_text) {
                SDL_BlitSurface(dur_text, NULL, screen, &(SDL_Rect){hw - dur_text->w - SCALE1(PADDING * 2), y + (layout.item_h - dur_text->h) / 2});
                SDL_FreeSurface(dur_text);
            }
        }
    }

    // Empty results message
    if (result_count == 0) {
        const char* msg = searching ? "Searching..." : "No results found";
        SDL_Surface* text = TTF_RenderUTF8_Blended(Fonts_getLarge(), msg, COLOR_GRAY);
        if (text) {
            SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){(hw - text->w) / 2, hh / 2 - text->h / 2});
            SDL_FreeSurface(text);
        }
    }

    // Toast notification (rendered to GPU layer above scroll text)
    render_toast(screen, toast_message, toast_time);

    // Button hints
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);

    // Dynamic hint based on queue status (only show A action if item is selected)
    if (selected >= 0 && result_count > 0) {
        const char* action_hint = "ADD";
        DownloaderResult* selected_result = &results[selected];
        if (Downloader_isInQueue(selected_result->video_id)) {
            action_hint = "REMOVE";
        }
        GFX_blitButtonGroup((char*[]){"B", "BACK", "A", (char*)action_hint, NULL}, 1, screen, 1);
    } else {
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
    }
}

// Render YouTube download queue
void render_downloader_queue(SDL_Surface* screen, int show_setting,
                          int queue_selected, int* queue_scroll) {
    GFX_clear(screen);

    int hw = screen->w;
    int hh = screen->h;
    char truncated[256];

    render_screen_header(screen, "Download Queue", show_setting);

    // Queue list
    int qcount = 0;
    DownloaderQueueItem* queue = Downloader_queueGet(&qcount);

    // Use common list layout calculation, but limit to 4 items to leave room for notices
    ListLayout layout = calc_list_layout(screen, 0);
    if (layout.items_per_page > 4) layout.items_per_page = 4;
    adjust_list_scroll(queue_selected, queue_scroll, layout.items_per_page);

    for (int i = 0; i < layout.items_per_page && *queue_scroll + i < qcount; i++) {
        int idx = *queue_scroll + i;
        DownloaderQueueItem* item = &queue[idx];
        bool selected = (idx == queue_selected);

        int y = layout.list_y + i * layout.item_h;

        // Status indicator (only for non-pending items)
        const char* status_str = NULL;
        SDL_Color status_color = COLOR_GRAY;
        switch (item->status) {
            case DOWNLOADER_STATUS_PENDING: status_str = NULL; break;  // No prefix for pending
            case DOWNLOADER_STATUS_DOWNLOADING: status_str = NULL; break;  // Show progress bar instead
            case DOWNLOADER_STATUS_COMPLETE: status_str = "[OK]"; break;
            case DOWNLOADER_STATUS_FAILED: status_str = "[X]"; break;
        }

        // Calculate status indicator width
        int status_width = 0;
        if (status_str) {
            int st_w, st_h;
            TTF_SizeUTF8(Fonts_getTiny(), status_str, &st_w, &st_h);
            status_width = st_w + SCALE1(8);
        }

        // Render pill background and get text position
        ListItemPos pos = render_list_item_pill(screen, &layout, item->title, truncated, y, selected, status_width);
        int title_x = pos.text_x;

        // Render status indicator
        if (status_str) {
            SDL_Surface* status_text = TTF_RenderUTF8_Blended(Fonts_getTiny(), status_str, selected ? uintToColour(THEME_COLOR5_255) : status_color);
            if (status_text) {
                SDL_BlitSurface(status_text, NULL, screen, &(SDL_Rect){title_x, y + (layout.item_h - status_text->h) / 2});
                title_x += status_text->w + SCALE1(8);
                SDL_FreeSurface(status_text);
            }
        }

        // Title - use common text rendering with scrolling for selected items
        int title_max_w = pos.pill_width - SCALE1(BUTTON_PADDING * 2) - status_width;
        render_list_item_text(screen, &downloader_queue_scroll_text, item->title, Fonts_getMedium(),
                              title_x, pos.text_y, title_max_w, selected);

        // Progress bar for downloading items (always on right, outside pill)
        if (item->status == DOWNLOADER_STATUS_DOWNLOADING) {
            int bar_w = SCALE1(60);
            int bar_h = SCALE1(8);
            int bar_x = hw - SCALE1(PADDING * 2) - bar_w;
            int bar_y = y + (layout.item_h - bar_h) / 2;

            // Background bar
            SDL_Rect bg_rect = {bar_x, bar_y, bar_w, bar_h};
            SDL_FillRect(screen, &bg_rect, SDL_MapRGB(screen->format, 60, 60, 60));

            // Progress fill
            int fill_w = (bar_w * item->progress_percent) / 100;
            if (fill_w > 0) {
                SDL_Rect fill_rect = {bar_x, bar_y, fill_w, bar_h};
                SDL_FillRect(screen, &fill_rect, SDL_MapRGB(screen->format, 100, 200, 100));
            }

            // Percentage text
            char pct_str[8];
            snprintf(pct_str, sizeof(pct_str), "%d%%", item->progress_percent);
            SDL_Surface* pct_text = TTF_RenderUTF8_Blended(Fonts_getTiny(), pct_str, COLOR_GRAY);
            if (pct_text) {
                SDL_BlitSurface(pct_text, NULL, screen, &(SDL_Rect){bar_x - pct_text->w - SCALE1(4), y + (layout.item_h - pct_text->h) / 2});
                SDL_FreeSurface(pct_text);
            }
        }
    }

    // Empty queue message
    if (qcount == 0) {
        // Clear any lingering scroll state when queue is empty
        downloader_queue_clear_scroll();
        const char* msg = "Queue is empty";
        SDL_Surface* text = TTF_RenderUTF8_Blended(Fonts_getLarge(), msg, COLOR_GRAY);
        if (text) {
            SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){(hw - text->w) / 2, hh / 2 - text->h / 2});
            SDL_FreeSurface(text);
        }
    }

    // Scroll indicators (custom position to account for notices)
    if (qcount > layout.items_per_page) {
        int ox = (hw - SCALE1(24)) / 2;

        // Up indicator at top
        if (*queue_scroll > 0) {
            GFX_blitAsset(ASSET_SCROLL_UP, NULL, screen, &(SDL_Rect){ox, SCALE1(PADDING + PILL_SIZE)});
        }

        // Down indicator below last pill (before notices)
        if (*queue_scroll + layout.items_per_page < qcount) {
            int last_pill_bottom = layout.list_y + layout.items_per_page * layout.item_h;
            GFX_blitAsset(ASSET_SCROLL_DOWN, NULL, screen, &(SDL_Rect){ox, last_pill_bottom + SCALE1(2)});
        }
    }

    // Notice about download reliability (shown above button hints with gap)
    const char* notice1 = "Downloads may fail due to YouTube restrictions.";
    const char* notice2 = "Retry later or update yt-dlp if issues persist.";
    int notice_base_y = hh - SCALE1(BUTTON_SIZE + BUTTON_MARGIN + PADDING + 12);

    SDL_Surface* notice2_text = TTF_RenderUTF8_Blended(Fonts_getTiny(), notice2, COLOR_GRAY);
    if (notice2_text) {
        int notice2_y = notice_base_y - notice2_text->h - SCALE1(2);
        SDL_BlitSurface(notice2_text, NULL, screen, &(SDL_Rect){(hw - notice2_text->w) / 2, notice2_y});
        SDL_FreeSurface(notice2_text);
    }

    SDL_Surface* notice1_text = TTF_RenderUTF8_Blended(Fonts_getTiny(), notice1, COLOR_GRAY);
    if (notice1_text) {
        int notice1_y = notice_base_y - notice1_text->h - SCALE1(14);
        SDL_BlitSurface(notice1_text, NULL, screen, &(SDL_Rect){(hw - notice1_text->w) / 2, notice1_y});
        SDL_FreeSurface(notice1_text);
    }

    // Button hints
    if (qcount > 0) {
        GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
        GFX_blitButtonGroup((char*[]){"X", "REMOVE", "A", "DOWNLOAD", "B", "BACK", NULL}, 1, screen, 1);
    } else {
        GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
    }
}

// Render YouTube downloading progress
void render_downloader_downloading(SDL_Surface* screen, int show_setting) {
    GFX_clear(screen);

    int hw = screen->w;
    int hh = screen->h;
    char truncated[256];

    render_screen_header(screen, "Downloading...", show_setting);

    const DownloaderDownloadStatus* status = Downloader_getDownloadStatus();

    // Get current item progress from queue
    int current_progress = 0;
    int qcount = 0;
    DownloaderQueueItem* queue = Downloader_queueGet(&qcount);
    if (queue && status->current_index >= 0 && status->current_index < qcount) {
        current_progress = queue[status->current_index].progress_percent;
    }

    // Progress info
    char progress[128];
    snprintf(progress, sizeof(progress), "%d / %d completed", status->completed_count, status->total_items);
    SDL_Surface* prog_text = TTF_RenderUTF8_Blended(Fonts_getMedium(), progress, COLOR_GRAY);
    if (prog_text) {
        SDL_BlitSurface(prog_text, NULL, screen, &(SDL_Rect){(hw - prog_text->w) / 2, hh / 2 - SCALE1(50)});
        SDL_FreeSurface(prog_text);
    }

    // Current track
    if (strlen(status->current_title) > 0) {
        GFX_truncateText(Fonts_getSmall(), status->current_title, truncated, hw - SCALE1(PADDING * 4), 0);
        SDL_Surface* curr_text = TTF_RenderUTF8_Blended(Fonts_getSmall(), truncated, COLOR_WHITE);
        if (curr_text) {
            SDL_BlitSurface(curr_text, NULL, screen, &(SDL_Rect){(hw - curr_text->w) / 2, hh / 2 - SCALE1(20)});
            SDL_FreeSurface(curr_text);
        }
    }

    // Progress bar for current download
    int bar_w = hw - SCALE1(PADDING * 8);
    int bar_h = SCALE1(16);
    int bar_x = (hw - bar_w) / 2;
    int bar_y = hh / 2 + SCALE1(10);

    // Background bar
    SDL_Rect bg_rect = {bar_x, bar_y, bar_w, bar_h};
    SDL_FillRect(screen, &bg_rect, SDL_MapRGB(screen->format, 60, 60, 60));

    // Progress fill
    int fill_w = (bar_w * current_progress) / 100;
    if (fill_w > 0) {
        SDL_Rect fill_rect = {bar_x, bar_y, fill_w, bar_h};
        SDL_FillRect(screen, &fill_rect, SDL_MapRGB(screen->format, 100, 200, 100));
    }

    // Percentage text
    char pct_str[16];
    snprintf(pct_str, sizeof(pct_str), "%d%%", current_progress);
    SDL_Surface* pct_text = TTF_RenderUTF8_Blended(Fonts_getMedium(), pct_str, COLOR_WHITE);
    if (pct_text) {
        SDL_BlitSurface(pct_text, NULL, screen, &(SDL_Rect){(hw - pct_text->w) / 2, bar_y + bar_h + SCALE1(8)});
        SDL_FreeSurface(pct_text);
    }

    // Button hints
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    GFX_blitButtonGroup((char*[]){"B", "CANCEL", NULL}, 1, screen, 1);
}

// Check if YouTube results list has active scrolling (for refresh optimization)
bool downloader_results_needs_scroll_refresh(void) {
    return ScrollText_isScrolling(&downloader_results_scroll_text);
}

// Check if results scroll needs a render to transition (delay phase)
bool downloader_results_scroll_needs_render(void) {
    return ScrollText_needsRender(&downloader_results_scroll_text);
}

// Check if YouTube queue list has active scrolling (for refresh optimization)
bool downloader_queue_needs_scroll_refresh(void) {
    return ScrollText_isScrolling(&downloader_queue_scroll_text);
}

// Check if queue scroll needs a render to transition (delay phase)
bool downloader_queue_scroll_needs_render(void) {
    return ScrollText_needsRender(&downloader_queue_scroll_text);
}

// Animate YouTube results scroll only (GPU mode, no screen redraw needed)
void downloader_results_animate_scroll(void) {
    ScrollText_animateOnly(&downloader_results_scroll_text);
}

// Animate YouTube queue scroll only (GPU mode, no screen redraw needed)
void downloader_queue_animate_scroll(void) {
    ScrollText_animateOnly(&downloader_queue_scroll_text);
}

// Clear YouTube queue scroll state (call when queue items are removed)
void downloader_queue_clear_scroll(void) {
    memset(&downloader_queue_scroll_text, 0, sizeof(downloader_queue_scroll_text));
    GFX_clearLayers(LAYER_SCROLLTEXT);
}

// Clear YouTube results scroll state and toast
void downloader_results_clear_scroll(void) {
    memset(&downloader_results_scroll_text, 0, sizeof(downloader_results_scroll_text));
    GFX_clearLayers(LAYER_SCROLLTEXT);
    clear_toast();
}

// Render YouTube yt-dlp update progress
void render_downloader_updating(SDL_Surface* screen, int show_setting) {
    GFX_clear(screen);

    int hw = screen->w;
    int hh = screen->h;

    render_screen_header(screen, "Updating yt-dlp", show_setting);

    const DownloaderUpdateStatus* status = Downloader_getUpdateStatus();

    // Current version
    char ver_str[128];
    snprintf(ver_str, sizeof(ver_str), "Current: %s", status->current_version);
    SDL_Surface* ver_text = TTF_RenderUTF8_Blended(Fonts_getMedium(), ver_str, COLOR_GRAY);
    if (ver_text) {
        SDL_BlitSurface(ver_text, NULL, screen, &(SDL_Rect){(hw - ver_text->w) / 2, hh / 2 - SCALE1(50)});
        SDL_FreeSurface(ver_text);
    }

    // Status message - progress stages:
    // 0-10: connectivity check, 10-30: fetching GitHub API, 30-50: parsing version,
    // 50-80: downloading binary, 80-100: installing
    const char* status_msg = "Checking connection...";
    if (status->progress_percent >= 15 && status->progress_percent < 30) {
        status_msg = "Fetching version info...";
    } else if (status->progress_percent >= 30 && status->progress_percent < 50) {
        status_msg = "Checking for updates...";
    } else if (status->progress_percent >= 50 && status->progress_percent < 80) {
        status_msg = "Downloading yt-dlp...";
    } else if (status->progress_percent >= 80 && status->progress_percent < 100) {
        status_msg = "Installing update...";
    } else if (!status->updating && !status->update_available && status->progress_percent >= 100) {
        status_msg = "Already up to date!";
    } else if (status->progress_percent >= 100) {
        status_msg = "Update complete!";
    } else if (!status->updating && strlen(status->error_message) > 0) {
        status_msg = status->error_message;
    }

    SDL_Surface* status_text = TTF_RenderUTF8_Blended(Fonts_getMedium(), status_msg, COLOR_WHITE);
    if (status_text) {
        SDL_BlitSurface(status_text, NULL, screen, &(SDL_Rect){(hw - status_text->w) / 2, hh / 2});
        SDL_FreeSurface(status_text);
    }

    // Latest version (if known)
    if (strlen(status->latest_version) > 0) {
        snprintf(ver_str, sizeof(ver_str), "Latest: %s", status->latest_version);
        SDL_Surface* latest_text = TTF_RenderUTF8_Blended(Fonts_getSmall(), ver_str, COLOR_GRAY);
        if (latest_text) {
            SDL_BlitSurface(latest_text, NULL, screen, &(SDL_Rect){(hw - latest_text->w) / 2, hh / 2 + SCALE1(30)});
            SDL_FreeSurface(latest_text);
        }
    }

    // Progress bar
    if (status->updating) {
        int bar_w = hw - SCALE1(PADDING * 8);
        int bar_h = SCALE1(12);
        int bar_x = SCALE1(PADDING * 4);
        int bar_y = hh / 2 + SCALE1(55);

        // Background
        SDL_Rect bg_rect = {bar_x, bar_y, bar_w, bar_h};
        SDL_FillRect(screen, &bg_rect, SDL_MapRGB(screen->format, 64, 64, 64));

        // Progress fill
        int prog_w = (bar_w * status->progress_percent) / 100;
        if (prog_w > 0) {
            SDL_Rect prog_rect = {bar_x, bar_y, prog_w, bar_h};
            SDL_FillRect(screen, &prog_rect, SDL_MapRGB(screen->format, 100, 200, 100));
        }

        // Download detail text (e.g., "2.5 MB / 5.0 MB" or "2.5 MB downloaded")
        if (strlen(status->status_detail) > 0) {
            SDL_Surface* detail_text = TTF_RenderUTF8_Blended(Fonts_getSmall(), status->status_detail, COLOR_GRAY);
            if (detail_text) {
                SDL_BlitSurface(detail_text, NULL, screen, &(SDL_Rect){(hw - detail_text->w) / 2, bar_y + bar_h + SCALE1(6)});
                SDL_FreeSurface(detail_text);
            }
        }

        // Percentage text
        char pct_str[16];
        snprintf(pct_str, sizeof(pct_str), "%d%%", status->progress_percent);
        SDL_Surface* pct_text = TTF_RenderUTF8_Blended(Fonts_getTiny(), pct_str, COLOR_WHITE);
        if (pct_text) {
            // Draw percentage inside the bar if there's room, otherwise to the right
            int pct_x = bar_x + (bar_w - pct_text->w) / 2;
            int pct_y = bar_y + (bar_h - pct_text->h) / 2;
            SDL_BlitSurface(pct_text, NULL, screen, &(SDL_Rect){pct_x, pct_y});
            SDL_FreeSurface(pct_text);
        }
    }

    // Button hints
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    if (status->updating) {
        GFX_blitButtonGroup((char*[]){"B", "CANCEL", NULL}, 1, screen, 1);
    } else {
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
    }
}
