#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include "defines.h"
#include "api.h"
#include "podcast.h"
#include "ui_podcast.h"
#include "ui_fonts.h"
#include "ui_utils.h"
#include "ui_album_art.h"
#include "radio_net.h"

// Toast duration constant
#define PODCAST_TOAST_DURATION 3000  // 3 seconds

// Toast GPU layer (layer 5 is highest available, same as LAYER_SPECTRUM but not used in podcast screens)
#define LAYER_TOAST 5

// Max artwork size (1MB to match radio album art buffer)
#define PODCAST_ARTWORK_MAX_SIZE (1024 * 1024)

// Album art cache directory path on SD card (shared with radio)
#define ALBUMART_CACHE_DIR SDCARD_PATH "/.cache/albumart"
#define CACHE_PARENT_DIR SDCARD_PATH "/.cache"

// Scroll state for selected item title in lists
static ScrollTextState podcast_title_scroll = {0};

// Scroll state for playing screen episode title
static ScrollTextState podcast_playing_title_scroll = {0};

// Podcast artwork state
static SDL_Surface* podcast_artwork = NULL;
static char podcast_artwork_url[512] = {0};

// Helper to convert surface to ARGB8888 for proper scaling
static SDL_Surface* convert_to_argb8888(SDL_Surface* src) {
    if (!src) return NULL;

    SDL_Surface* converted = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(src);
    return converted;
}

// Fetch podcast artwork from URL (simple sync fetch, cached)
static void podcast_fetch_artwork(const char* artwork_url) {
    if (!artwork_url || !artwork_url[0]) return;

    // Already have this artwork
    if (strcmp(podcast_artwork_url, artwork_url) == 0 && podcast_artwork) return;

    // Clear old artwork and invalidate album art background cache
    if (podcast_artwork) {
        SDL_FreeSurface(podcast_artwork);
        podcast_artwork = NULL;
        cleanup_album_art_background();
    }
    strncpy(podcast_artwork_url, artwork_url, sizeof(podcast_artwork_url) - 1);

    // Create cache directory on SD card (same location as radio album art)
    mkdir(CACHE_PARENT_DIR, 0755);
    mkdir(ALBUMART_CACHE_DIR, 0755);

    // Simple hash for cache filename
    unsigned int hash = 5381;
    const char* p = artwork_url;
    while (*p) hash = ((hash << 5) + hash) + (unsigned char)*p++;

    char cache_path[768];
    snprintf(cache_path, sizeof(cache_path), "%s/%08x.jpg", ALBUMART_CACHE_DIR, hash);

    // Try to load from cache first
    FILE* f = fopen(cache_path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (size > 0 && size < PODCAST_ARTWORK_MAX_SIZE) {
            uint8_t* data = (uint8_t*)malloc(size);
            if (data && fread(data, 1, size, f) == (size_t)size) {
                SDL_RWops* rw = SDL_RWFromConstMem(data, size);
                if (rw) {
                    SDL_Surface* loaded = IMG_Load_RW(rw, 1);
                    podcast_artwork = convert_to_argb8888(loaded);
                }
            }
            free(data);
        }
        fclose(f);
        if (podcast_artwork) return;
    }

    // Fetch from network using static buffer
    static uint8_t artwork_buffer[PODCAST_ARTWORK_MAX_SIZE];
    int size = radio_net_fetch(artwork_url, artwork_buffer, PODCAST_ARTWORK_MAX_SIZE, NULL, 0);

    if (size > 0) {
        // Save to cache
        f = fopen(cache_path, "wb");
        if (f) {
            fwrite(artwork_buffer, 1, size, f);
            fclose(f);
        }

        // Load as SDL surface and convert to ARGB8888 for proper scaling
        SDL_RWops* rw = SDL_RWFromConstMem(artwork_buffer, size);
        if (rw) {
            SDL_Surface* loaded = IMG_Load_RW(rw, 1);
            podcast_artwork = convert_to_argb8888(loaded);
        }
    }
}

// Clear podcast artwork (call when leaving playing screen)
void Podcast_clearArtwork(void) {
    if (podcast_artwork) {
        SDL_FreeSurface(podcast_artwork);
        podcast_artwork = NULL;
    }
    podcast_artwork_url[0] = '\0';
    memset(&podcast_playing_title_scroll, 0, sizeof(podcast_playing_title_scroll));
}

// Helper to render toast notification (renders to GPU layer for highest z-index)
static void render_podcast_toast(SDL_Surface* screen, const char* message, uint32_t toast_time) {
    if (!message || message[0] == '\0') {
        PLAT_clearLayers(LAYER_TOAST);
        return;
    }

    uint32_t now = SDL_GetTicks();
    if (now - toast_time >= PODCAST_TOAST_DURATION) {
        PLAT_clearLayers(LAYER_TOAST);
        return;
    }

    int hw = screen->w;
    int hh = screen->h;

    SDL_Surface* toast_text = TTF_RenderUTF8_Blended(get_font_medium(), message, COLOR_WHITE);
    if (toast_text) {
        int border = SCALE1(2);
        int toast_w = toast_text->w + SCALE1(PADDING * 3);
        int toast_h = toast_text->h + SCALE1(12);
        int toast_x = (hw - toast_w) / 2;
        // Move up to avoid button hints
        int toast_y = hh - SCALE1(BUTTON_SIZE + BUTTON_MARGIN + PADDING * 3) - toast_h;

        // Total surface size including border
        int surface_w = toast_w + border * 2;
        int surface_h = toast_h + border * 2;

        // Create surface for GPU layer rendering
        SDL_Surface* toast_surface = SDL_CreateRGBSurfaceWithFormat(0,
            surface_w, surface_h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (toast_surface) {
            // Disable blending so fills are opaque
            SDL_SetSurfaceBlendMode(toast_surface, SDL_BLENDMODE_NONE);

            // Draw light gray border (outer rect) - fills entire surface first
            SDL_FillRect(toast_surface, NULL, SDL_MapRGBA(toast_surface->format, 200, 200, 200, 255));

            // Draw dark grey background (inner rect)
            SDL_Rect bg_rect = {border, border, toast_w, toast_h};
            SDL_FillRect(toast_surface, &bg_rect, SDL_MapRGBA(toast_surface->format, 40, 40, 40, 255));

            // Draw text centered within the toast (blend text onto surface)
            SDL_SetSurfaceBlendMode(toast_surface, SDL_BLENDMODE_BLEND);
            int text_x = border + (toast_w - toast_text->w) / 2;
            int text_y = border + (toast_h - toast_text->h) / 2;
            SDL_BlitSurface(toast_text, NULL, toast_surface, &(SDL_Rect){text_x, text_y});

            // Render to GPU layer at the correct screen position
            PLAT_clearLayers(LAYER_TOAST);
            PLAT_drawOnLayer(toast_surface, toast_x - border, toast_y - border,
                            surface_w, surface_h, 1.0f, false, LAYER_TOAST);

            SDL_FreeSurface(toast_surface);
        }
        SDL_FreeSurface(toast_text);
    }
}


// Management menu item labels (Y button menu)
static const char* podcast_manage_items[] = {
    "Search",
    "Top Shows",
    "Subscriptions"
};

// Format duration as HH:MM:SS or MM:SS
static void format_duration(char* buf, int seconds) {
    if (seconds <= 0) {
        strcpy(buf, "--:--");
        return;
    }
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;
    if (h > 0) {
        sprintf(buf, "%d:%02d:%02d", h, m, s);
    } else {
        sprintf(buf, "%d:%02d", m, s);
    }
}

// Format date as relative time or date string
static void format_date(char* buf, uint32_t timestamp) {
    if (timestamp == 0) {
        strcpy(buf, "");
        return;
    }

    time_t now = time(NULL);
    time_t pub = (time_t)timestamp;
    int days = (now - pub) / (24 * 3600);

    if (days == 0) {
        strcpy(buf, "Today");
    } else if (days == 1) {
        strcpy(buf, "Yesterday");
    } else if (days < 7) {
        sprintf(buf, "%d days ago", days);
    } else if (days < 30) {
        sprintf(buf, "%d weeks ago", days / 7);
    } else {
        struct tm* tm = localtime(&pub);
        strftime(buf, 32, "%b %d", tm);
    }
}

// Render the main podcast list (shows subscribed podcasts, like radio stations)
void render_podcast_list(SDL_Surface* screen, int show_setting,
                         int selected, int* scroll) {
    GFX_clear(screen);

    int hw = screen->w;
    char truncated[256];

    render_screen_header(screen, "Podcasts", show_setting);

    int count = 0;
    PodcastFeed* feeds = Podcast_getSubscriptions(&count);

    // Empty state
    if (count == 0) {
        int center_y = screen->h / 2 - SCALE1(15);

        const char* msg1 = "No podcasts subscribed";
        SDL_Surface* text1 = TTF_RenderUTF8_Blended(get_font_medium(), msg1, COLOR_WHITE);
        if (text1) {
            SDL_BlitSurface(text1, NULL, screen, &(SDL_Rect){(hw - text1->w) / 2, center_y - SCALE1(15)});
            SDL_FreeSurface(text1);
        }

        const char* msg2 = "Press Y to manage podcasts";
        SDL_Surface* text2 = TTF_RenderUTF8_Blended(get_font_small(), msg2, COLOR_GRAY);
        if (text2) {
            SDL_BlitSurface(text2, NULL, screen, &(SDL_Rect){(hw - text2->w) / 2, center_y + SCALE1(10)});
            SDL_FreeSurface(text2);
        }

        GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
        GFX_blitButtonGroup((char*[]){"B", "BACK", "Y", "MANAGE", NULL}, 1, screen, 1);
        return;
    }

    // List layout
    ListLayout layout = calc_list_layout(screen, 0);
    adjust_list_scroll(selected, scroll, layout.items_per_page);

    for (int i = 0; i < layout.items_per_page && *scroll + i < count; i++) {
        int idx = *scroll + i;
        PodcastFeed* feed = &feeds[idx];
        bool is_selected = (idx == selected);

        int y = layout.list_y + i * layout.item_h;

        // Render pill
        ListItemPos pos = render_list_item_pill(screen, &layout, feed->title, truncated, y, is_selected, 0);

        // Title
        render_list_item_text(screen, NULL, feed->title, get_font_medium(),
                              pos.text_x, pos.text_y, layout.max_width - SCALE1(50), is_selected);

        // Episode count on right
        char ep_count[32];
        snprintf(ep_count, sizeof(ep_count), "%d", feed->episode_count);
        SDL_Color count_color = is_selected ? COLOR_GRAY : COLOR_DARK_TEXT;
        SDL_Surface* count_surf = TTF_RenderUTF8_Blended(get_font_tiny(), ep_count, count_color);
        if (count_surf) {
            SDL_BlitSurface(count_surf, NULL, screen,
                            &(SDL_Rect){hw - count_surf->w - SCALE1(PADDING * 2),
                                        y + (layout.item_h - count_surf->h) / 2});
            SDL_FreeSurface(count_surf);
        }
    }

    render_scroll_indicators(screen, *scroll, layout.items_per_page, count);

    // Left: START CONTROLS, Right: B BACK A SELECT
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "SELECT", NULL}, 1, screen, 1);
}

// Render the podcast management menu (Y button opens this)
void render_podcast_manage(SDL_Surface* screen, int show_setting,
                           int menu_selected, int subscription_count) {
    GFX_clear(screen);

    char truncated[256];
    char label_buf[128];

    render_screen_header(screen, "Manage Podcasts", show_setting);

    // Use common list layout
    ListLayout layout = calc_list_layout(screen, 0);

    for (int i = 0; i < PODCAST_MANAGE_COUNT; i++) {
        bool selected = (i == menu_selected);
        const char* item_label = podcast_manage_items[i];

        // Customize label for items with counts
        if (i == PODCAST_MANAGE_SUBSCRIPTIONS && subscription_count > 0) {
            snprintf(label_buf, sizeof(label_buf), "Subscriptions (%d)", subscription_count);
            item_label = label_buf;
        }

        // Render menu item pill
        MenuItemPos pos = render_menu_item_pill(screen, &layout, item_label, truncated, i, selected, 0);

        // Render text using standard list item text (consistent colors and font)
        render_list_item_text(screen, NULL, truncated, get_font_large(),
                              pos.text_x, pos.text_y, layout.max_width, selected);
    }

    // Button hints
    GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "SELECT", NULL}, 1, screen, 1);
}

// Render subscriptions list
void render_podcast_subscriptions(SDL_Surface* screen, int show_setting,
                                   int selected, int* scroll) {
    GFX_clear(screen);

    int hw = screen->w;
    char truncated[256];

    render_screen_header(screen, "Subscriptions", show_setting);

    int count = 0;
    PodcastFeed* feeds = Podcast_getSubscriptions(&count);

    // Empty state
    if (count == 0) {
        int center_y = screen->h / 2 - SCALE1(15);

        const char* msg1 = "No subscriptions yet";
        SDL_Surface* text1 = TTF_RenderUTF8_Blended(get_font_medium(), msg1, COLOR_WHITE);
        if (text1) {
            SDL_BlitSurface(text1, NULL, screen, &(SDL_Rect){(hw - text1->w) / 2, center_y - SCALE1(15)});
            SDL_FreeSurface(text1);
        }

        const char* msg2 = "Search or browse Top Shows to subscribe";
        SDL_Surface* text2 = TTF_RenderUTF8_Blended(get_font_small(), msg2, COLOR_GRAY);
        if (text2) {
            SDL_BlitSurface(text2, NULL, screen, &(SDL_Rect){(hw - text2->w) / 2, center_y + SCALE1(10)});
            SDL_FreeSurface(text2);
        }

        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
        return;
    }

    // List layout
    ListLayout layout = calc_list_layout(screen, 0);
    adjust_list_scroll(selected, scroll, layout.items_per_page);

    for (int i = 0; i < layout.items_per_page && *scroll + i < count; i++) {
        int idx = *scroll + i;
        PodcastFeed* feed = &feeds[idx];
        bool is_selected = (idx == selected);

        int y = layout.list_y + i * layout.item_h;

        // Render pill
        ListItemPos pos = render_list_item_pill(screen, &layout, feed->title, truncated, y, is_selected, 0);

        // Title
        render_list_item_text(screen, NULL, feed->title, get_font_medium(),
                              pos.text_x, pos.text_y, layout.max_width - SCALE1(80), is_selected);

        // Episode count on right
        char ep_count[32];
        snprintf(ep_count, sizeof(ep_count), "%d eps", feed->episode_count);
        SDL_Color count_color = is_selected ? COLOR_GRAY : COLOR_DARK_TEXT;
        SDL_Surface* count_surf = TTF_RenderUTF8_Blended(get_font_tiny(), ep_count, count_color);
        if (count_surf) {
            SDL_BlitSurface(count_surf, NULL, screen,
                            &(SDL_Rect){hw - count_surf->w - SCALE1(PADDING * 2),
                                        y + (layout.item_h - count_surf->h) / 2});
            SDL_FreeSurface(count_surf);
        }
    }

    render_scroll_indicators(screen, *scroll, layout.items_per_page, count);

    GFX_blitButtonGroup((char*[]){"B", "BACK", "X", "UNSUB", "A", "OPEN", NULL}, 1, screen, 1);
}

// Render Top Shows list
void render_podcast_top_shows(SDL_Surface* screen, int show_setting,
                               int selected, int* scroll,
                               const char* toast_message, uint32_t toast_time) {
    GFX_clear(screen);

    int hw = screen->w;
    char truncated[256];

    render_screen_header(screen, "Top Shows", show_setting);

    const PodcastChartsStatus* status = Podcast_getChartsStatus();

    // Loading state
    if (status->loading) {
        int center_y = screen->h / 2;
        const char* msg = "Loading...";
        SDL_Surface* text = TTF_RenderUTF8_Blended(get_font_medium(), msg, COLOR_WHITE);
        if (text) {
            SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){(hw - text->w) / 2, center_y});
            SDL_FreeSurface(text);
        }
        return;
    }

    int count = 0;
    PodcastChartItem* items = Podcast_getTopShows(&count);

    // Empty state
    if (count == 0) {
        int center_y = screen->h / 2 - SCALE1(15);
        const char* msg = status->error_message[0] ? status->error_message : "No shows available";
        SDL_Surface* text = TTF_RenderUTF8_Blended(get_font_medium(), msg, COLOR_WHITE);
        if (text) {
            SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){(hw - text->w) / 2, center_y});
            SDL_FreeSurface(text);
        }
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
        return;
    }

    // List layout
    ListLayout layout = calc_list_layout(screen, 0);
    adjust_list_scroll(selected, scroll, layout.items_per_page);

    // Rank prefix width - smaller gap
    int rank_width = SCALE1(22);

    for (int i = 0; i < layout.items_per_page && *scroll + i < count; i++) {
        int idx = *scroll + i;
        PodcastChartItem* item = &items[idx];
        bool is_selected = (idx == selected);

        int y = layout.list_y + i * layout.item_h;

        // Rank prefix
        char rank[8];
        snprintf(rank, sizeof(rank), "#%d", idx + 1);

        // Render pill with rank width
        ListItemPos pos = render_list_item_pill(screen, &layout, item->title, truncated, y, is_selected, rank_width);

        // Rank number
        SDL_Color rank_color = is_selected ? COLOR_GRAY : COLOR_DARK_TEXT;
        SDL_Surface* rank_surf = TTF_RenderUTF8_Blended(get_font_tiny(), rank, rank_color);
        if (rank_surf) {
            SDL_BlitSurface(rank_surf, NULL, screen, &(SDL_Rect){pos.text_x, pos.text_y + SCALE1(3)});
            SDL_FreeSurface(rank_surf);
        }

        // Title - use scroll state for selected item
        render_list_item_text(screen, is_selected ? &podcast_title_scroll : NULL,
                              item->title, get_font_medium(),
                              pos.text_x + rank_width, pos.text_y,
                              layout.max_width - rank_width - SCALE1(90), is_selected);

        // Author on right
        if (item->author[0]) {
            char author_truncated[64];
            GFX_truncateText(get_font_tiny(), item->author, author_truncated, SCALE1(80), 0);
            SDL_Color author_color = is_selected ? COLOR_GRAY : COLOR_DARK_TEXT;
            SDL_Surface* author_surf = TTF_RenderUTF8_Blended(get_font_tiny(), author_truncated, author_color);
            if (author_surf) {
                SDL_BlitSurface(author_surf, NULL, screen,
                                &(SDL_Rect){hw - author_surf->w - SCALE1(PADDING * 2),
                                            y + (layout.item_h - author_surf->h) / 2});
                SDL_FreeSurface(author_surf);
            }
        }
    }

    render_scroll_indicators(screen, *scroll, layout.items_per_page, count);

    // Check if selected item is already subscribed (by iTunes ID)
    bool selected_is_subscribed = false;
    if (selected < count && items[selected].itunes_id[0]) {
        selected_is_subscribed = Podcast_isSubscribedByItunesId(items[selected].itunes_id);
    }

    // Show subscribe button only if not already subscribed
    if (selected_is_subscribed) {
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
    } else {
        GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "SUBSCRIBE", NULL}, 1, screen, 1);
    }

    // Toast notification
    render_podcast_toast(screen, toast_message, toast_time);
}

// Render search input screen
void render_podcast_search(SDL_Surface* screen, int show_setting) {
    GFX_clear(screen);

    int hw = screen->w;
    int hh = screen->h;

    render_screen_header(screen, "Search Podcasts", show_setting);

    int center_y = hh / 2 - SCALE1(20);

    const char* msg1 = "Press A to open keyboard";
    SDL_Surface* text1 = TTF_RenderUTF8_Blended(get_font_medium(), msg1, COLOR_WHITE);
    if (text1) {
        SDL_BlitSurface(text1, NULL, screen, &(SDL_Rect){(hw - text1->w) / 2, center_y});
        SDL_FreeSurface(text1);
    }

    const char* msg2 = "Search iTunes podcast directory";
    SDL_Surface* text2 = TTF_RenderUTF8_Blended(get_font_small(), msg2, COLOR_GRAY);
    if (text2) {
        SDL_BlitSurface(text2, NULL, screen, &(SDL_Rect){(hw - text2->w) / 2, center_y + SCALE1(25)});
        SDL_FreeSurface(text2);
    }

    GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "SEARCH", NULL}, 1, screen, 1);
}

// Render search results
void render_podcast_search_results(SDL_Surface* screen, int show_setting,
                                    int selected, int* scroll,
                                    const char* toast_message, uint32_t toast_time) {
    GFX_clear(screen);

    int hw = screen->w;
    char truncated[256];

    render_screen_header(screen, "Search Results", show_setting);

    const PodcastSearchStatus* status = Podcast_getSearchStatus();

    // Searching state
    if (status->searching) {
        int center_y = screen->h / 2;
        const char* msg = "Searching...";
        SDL_Surface* text = TTF_RenderUTF8_Blended(get_font_medium(), msg, COLOR_WHITE);
        if (text) {
            SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){(hw - text->w) / 2, center_y});
            SDL_FreeSurface(text);
        }
        return;
    }

    int count = 0;
    PodcastSearchResult* results = Podcast_getSearchResults(&count);

    // Empty/error state
    if (count == 0) {
        int center_y = screen->h / 2 - SCALE1(15);
        const char* msg = status->error_message[0] ? status->error_message : "No results found";
        SDL_Surface* text = TTF_RenderUTF8_Blended(get_font_medium(), msg, COLOR_WHITE);
        if (text) {
            SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){(hw - text->w) / 2, center_y});
            SDL_FreeSurface(text);
        }
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
        return;
    }

    // List layout
    ListLayout layout = calc_list_layout(screen, 0);
    adjust_list_scroll(selected, scroll, layout.items_per_page);

    // Check if selected item is already subscribed
    bool selected_is_subscribed = false;
    if (selected < count && results[selected].feed_url[0]) {
        selected_is_subscribed = Podcast_isSubscribed(results[selected].feed_url);
    }

    for (int i = 0; i < layout.items_per_page && *scroll + i < count; i++) {
        int idx = *scroll + i;
        PodcastSearchResult* result = &results[idx];
        bool is_selected = (idx == selected);

        int y = layout.list_y + i * layout.item_h;

        // Render pill
        ListItemPos pos = render_list_item_pill(screen, &layout, result->title, truncated, y, is_selected, 0);

        // Title - use scroll state for selected item
        render_list_item_text(screen, is_selected ? &podcast_title_scroll : NULL,
                              result->title, get_font_medium(),
                              pos.text_x, pos.text_y, layout.max_width - SCALE1(100), is_selected);

        // Author on right (with gap from title)
        if (result->author[0]) {
            char author_truncated[64];
            GFX_truncateText(get_font_tiny(), result->author, author_truncated, SCALE1(80), 0);
            SDL_Color author_color = is_selected ? COLOR_GRAY : COLOR_DARK_TEXT;
            SDL_Surface* author_surf = TTF_RenderUTF8_Blended(get_font_tiny(), author_truncated, author_color);
            if (author_surf) {
                SDL_BlitSurface(author_surf, NULL, screen,
                                &(SDL_Rect){hw - author_surf->w - SCALE1(PADDING * 2),
                                            y + (layout.item_h - author_surf->h) / 2});
                SDL_FreeSurface(author_surf);
            }
        }
    }

    render_scroll_indicators(screen, *scroll, layout.items_per_page, count);

    // Show subscribe button only if not already subscribed
    if (selected_is_subscribed) {
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
    } else {
        GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "SUBSCRIBE", NULL}, 1, screen, 1);
    }

    // Toast notification
    render_podcast_toast(screen, toast_message, toast_time);
}

// Render episode list for a feed
void render_podcast_episodes(SDL_Surface* screen, int show_setting,
                              int feed_index, int selected, int* scroll,
                              const char* toast_message, uint32_t toast_time) {
    GFX_clear(screen);

    int hw = screen->w;
    char truncated[256];

    PodcastFeed* feed = Podcast_getSubscription(feed_index);
    if (!feed) {
        render_screen_header(screen, "Episodes", show_setting);
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
        return;
    }

    render_screen_header(screen, feed->title, show_setting);

    int count = feed->episode_count;

    // Empty state
    if (count == 0) {
        int center_y = screen->h / 2 - SCALE1(15);
        const char* msg = "No episodes available";
        SDL_Surface* text = TTF_RenderUTF8_Blended(get_font_medium(), msg, COLOR_WHITE);
        if (text) {
            SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){(hw - text->w) / 2, center_y});
            SDL_FreeSurface(text);
        }
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
        return;
    }

    // List layout
    ListLayout layout = calc_list_layout(screen, 0);
    adjust_list_scroll(selected, scroll, layout.items_per_page);

    // Check download status and file existence of selected episode for button hints
    int selected_download_status = -1;
    int selected_progress = 0;
    bool selected_is_downloaded = false;
    if (selected < count) {
        PodcastEpisode* sel_ep = Podcast_getEpisode(feed_index, selected);
        if (sel_ep) {
            selected_download_status = Podcast_getEpisodeDownloadStatus(feed->feed_url, sel_ep->guid, &selected_progress);
            selected_is_downloaded = Podcast_episodeFileExists(feed, selected);
        }
    }

    for (int i = 0; i < layout.items_per_page && *scroll + i < count; i++) {
        int idx = *scroll + i;
        PodcastEpisode* ep = Podcast_getEpisode(feed_index, idx);
        if (!ep) continue;

        bool is_selected = (idx == selected);

        int y = layout.list_y + i * layout.item_h;

        // Check episode download status
        int dl_progress = 0;
        int dl_status = Podcast_getEpisodeDownloadStatus(feed->feed_url, ep->guid, &dl_progress);

        // Determine prefix (downloaded indicator) - check if file exists
        int prefix_width = 0;
        bool is_downloaded = Podcast_episodeFileExists(feed, idx);
        if (is_downloaded) {
            prefix_width = SCALE1(18);
        }

        // Render pill
        ListItemPos pos = render_list_item_pill(screen, &layout, ep->title, truncated, y, is_selected, prefix_width);

        // Downloaded indicator
        if (is_downloaded) {
            SDL_Color check_color = is_selected ? COLOR_WHITE : COLOR_GRAY;
            SDL_Surface* check = TTF_RenderUTF8_Blended(get_font_tiny(), "[D]", check_color);
            if (check) {
                SDL_BlitSurface(check, NULL, screen, &(SDL_Rect){pos.text_x, pos.text_y + SCALE1(3)});
                SDL_FreeSurface(check);
            }
        }

        // Title (reserve more space on right for duration/progress bar)
        render_list_item_text(screen, is_selected ? &podcast_title_scroll : NULL,
                              ep->title, get_font_medium(),
                              pos.text_x + prefix_width, pos.text_y,
                              layout.max_width - SCALE1(85) - prefix_width, is_selected);

        // Right side: Duration, progress bar, or "Queued" based on download state
        int right_x = hw - SCALE1(PADDING * 2);
        int right_y = y + (layout.item_h) / 2;

        if (dl_status == PODCAST_DOWNLOAD_DOWNLOADING) {
            // Show progress bar
            int bar_w = SCALE1(50);
            int bar_h = SCALE1(4);
            int bar_x = right_x - bar_w;
            int bar_y = right_y - bar_h / 2;

            // Background
            SDL_Rect bar_bg = {bar_x, bar_y, bar_w, bar_h};
            SDL_FillRect(screen, &bar_bg, SDL_MapRGB(screen->format, 60, 60, 60));

            // Progress fill
            int fill_w = (bar_w * dl_progress) / 100;
            if (fill_w > 0) {
                SDL_Rect bar_fill = {bar_x, bar_y, fill_w, bar_h};
                SDL_FillRect(screen, &bar_fill, SDL_MapRGB(screen->format, 255, 255, 255));
            }
        } else if (dl_status == PODCAST_DOWNLOAD_PENDING) {
            // Show "Queued" text
            SDL_Color queued_color = is_selected ? COLOR_GRAY : COLOR_DARK_TEXT;
            SDL_Surface* queued_surf = TTF_RenderUTF8_Blended(get_font_tiny(), "Queued", queued_color);
            if (queued_surf) {
                SDL_BlitSurface(queued_surf, NULL, screen,
                                &(SDL_Rect){right_x - queued_surf->w, right_y - queued_surf->h / 2});
                SDL_FreeSurface(queued_surf);
            }
        } else {
            // Show duration
            char duration[16];
            format_duration(duration, ep->duration_sec);
            SDL_Color dur_color = is_selected ? COLOR_GRAY : COLOR_DARK_TEXT;
            SDL_Surface* dur_surf = TTF_RenderUTF8_Blended(get_font_tiny(), duration, dur_color);
            if (dur_surf) {
                SDL_BlitSurface(dur_surf, NULL, screen,
                                &(SDL_Rect){right_x - dur_surf->w, right_y - dur_surf->h / 2});
                SDL_FreeSurface(dur_surf);
            }
        }
    }

    render_scroll_indicators(screen, *scroll, layout.items_per_page, count);

    // Dynamic button hints based on selected episode's state
    if (selected_download_status == PODCAST_DOWNLOAD_DOWNLOADING ||
        selected_download_status == PODCAST_DOWNLOAD_PENDING) {
        // Downloading or queued - show cancel button
        GFX_blitButtonGroup((char*[]){"B", "BACK", "X", "CANCEL", NULL}, 1, screen, 1);
    } else if (selected_is_downloaded) {
        // Downloaded - show play button
        GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "PLAY", NULL}, 1, screen, 1);
    } else {
        // Not downloaded - show download button
        GFX_blitButtonGroup((char*[]){"B", "BACK", "A", "DOWNLOAD", NULL}, 1, screen, 1);
    }

    // Toast notification
    render_podcast_toast(screen, toast_message, toast_time);
}

// Render now playing screen for podcast (matches radio/music player style)
void render_podcast_playing(SDL_Surface* screen, int show_setting,
                             int feed_index, int episode_index) {
    GFX_clear(screen);

    int hw = screen->w;
    int hh = screen->h;
    char truncated[256];

    PodcastFeed* feed = Podcast_getSubscription(feed_index);
    PodcastEpisode* ep = Podcast_getEpisode(feed_index, episode_index);

    if (!feed || !ep) {
        render_screen_header(screen, "Now Playing", show_setting);
        GFX_blitButtonGroup((char*[]){"B", "BACK", NULL}, 1, screen, 1);
        return;
    }

    bool is_streaming = Podcast_isStreaming();

    // Fetch and render album art background (if available)
    if (feed->artwork_url[0]) {
        podcast_fetch_artwork(feed->artwork_url);
        if (podcast_artwork && podcast_artwork->w > 0 && podcast_artwork->h > 0) {
            render_album_art_background(screen, podcast_artwork);
        }
    }

    // === TOP BAR ===
    int top_y = SCALE1(PADDING);

    // Badge - show "STREAMING" for streams, "PODCAST" for local (like format badge in music player)
    const char* badge_text = is_streaming ? "STREAM" : "PODCAST";
    SDL_Surface* badge_surf = TTF_RenderUTF8_Blended(get_font_tiny(), badge_text, COLOR_GRAY);
    int badge_h = badge_surf ? badge_surf->h + SCALE1(4) : SCALE1(16);
    int badge_x = SCALE1(PADDING);
    int badge_w = 0;

    if (badge_surf) {
        badge_w = badge_surf->w + SCALE1(10);
        // Draw border (gray)
        SDL_Rect border = {badge_x, top_y, badge_w, badge_h};
        SDL_FillRect(screen, &border, RGB_GRAY);
        SDL_Rect inner = {badge_x + 1, top_y + 1, badge_w - 2, badge_h - 2};
        SDL_FillRect(screen, &inner, RGB_BLACK);
        SDL_BlitSurface(badge_surf, NULL, screen, &(SDL_Rect){badge_x + SCALE1(5), top_y + SCALE1(2)});
        SDL_FreeSurface(badge_surf);
    }

    // Episode counter "01 / 67" (like track counter in music player)
    char ep_counter[32];
    snprintf(ep_counter, sizeof(ep_counter), "%02d / %02d", episode_index + 1, feed->episode_count);
    SDL_Surface* counter_surf = TTF_RenderUTF8_Blended(get_font_tiny(), ep_counter, COLOR_GRAY);
    if (counter_surf) {
        int counter_x = badge_x + badge_w + SCALE1(8);
        int counter_y = top_y + (badge_h - counter_surf->h) / 2;
        SDL_BlitSurface(counter_surf, NULL, screen, &(SDL_Rect){counter_x, counter_y});
        SDL_FreeSurface(counter_surf);
    }

    // Hardware status (clock, battery) on right
    GFX_blitHardwareGroup(screen, show_setting);

    // === PODCAST INFO SECTION (like music player artist/title/album) ===
    int info_y = SCALE1(PADDING + 45);
    int max_w_text = hw - SCALE1(PADDING * 2);

    // Podcast name (like Artist in music player) - gray, artist font
    GFX_truncateText(get_font_artist(), feed->title, truncated, max_w_text, 0);
    SDL_Surface* podcast_surf = TTF_RenderUTF8_Blended(get_font_artist(), truncated, COLOR_GRAY);
    if (podcast_surf) {
        SDL_BlitSurface(podcast_surf, NULL, screen, &(SDL_Rect){SCALE1(PADDING), info_y});
        info_y += podcast_surf->h + SCALE1(2);
        SDL_FreeSurface(podcast_surf);
    } else {
        info_y += SCALE1(18);
    }

    // Episode title (like Title in music player) - white, title font, with scrolling
    const char* title = ep->title[0] ? ep->title : "Unknown Episode";
    int title_y = info_y;

    // Check if text changed and reset scroll state
    if (strcmp(podcast_playing_title_scroll.text, title) != 0) {
        ScrollText_reset(&podcast_playing_title_scroll, title, get_font_title(), max_w_text, true);
    }

    // If text needs scrolling, use GPU layer
    if (podcast_playing_title_scroll.needs_scroll) {
        ScrollText_renderGPU_NoBg(&podcast_playing_title_scroll, get_font_title(), COLOR_WHITE, SCALE1(PADDING), title_y);
    } else {
        // Static text - render to screen surface
        PLAT_clearLayers(LAYER_SCROLLTEXT);
        SDL_Surface* title_surf = TTF_RenderUTF8_Blended(get_font_title(), title, COLOR_WHITE);
        if (title_surf) {
            SDL_BlitSurface(title_surf, NULL, screen, &(SDL_Rect){SCALE1(PADDING), title_y, 0, 0});
            SDL_FreeSurface(title_surf);
        }
    }
    info_y += TTF_FontHeight(get_font_title()) + SCALE1(2);

    // Publication date (like Album in music player) - gray, album font
    char date_str[32];
    format_date(date_str, ep->pub_date);
    if (date_str[0]) {
        SDL_Surface* date_surf = TTF_RenderUTF8_Blended(get_font_album(), date_str, COLOR_GRAY);
        if (date_surf) {
            SDL_BlitSurface(date_surf, NULL, screen, &(SDL_Rect){SCALE1(PADDING), info_y});
            SDL_FreeSurface(date_surf);
        }
    }

    // === PROGRESS BAR SECTION ===
    int bar_y = hh - SCALE1(PADDING + BUTTON_SIZE + BUTTON_MARGIN + 35);
    int bar_h = SCALE1(4);
    int bar_margin = SCALE1(PADDING);
    int bar_w = hw - bar_margin * 2;

    // Background
    SDL_Rect bar_bg = {bar_margin, bar_y, bar_w, bar_h};
    SDL_FillRect(screen, &bar_bg, RGB_DARK_GRAY);

    // Progress fill
    int position = Podcast_getPosition();
    int duration = Podcast_getDuration();

    if (is_streaming) {
        // For streaming, show buffer level
        float buffer = Podcast_getBufferLevel();
        int fill_w = (int)(bar_w * buffer);
        if (fill_w > 0) {
            SDL_Rect bar_fill = {bar_margin, bar_y, fill_w, bar_h};
            SDL_FillRect(screen, &bar_fill, RGB_WHITE);
        }
    } else if (duration > 0) {
        // For local files, show playback position
        int fill_w = (bar_w * position) / duration;
        if (fill_w > 0) {
            SDL_Rect bar_fill = {bar_margin, bar_y, fill_w, bar_h};
            SDL_FillRect(screen, &bar_fill, RGB_WHITE);
        }
    }

    // Time display
    char time_cur[16], time_dur[16];
    int time_y = bar_y + SCALE1(8);

    if (is_streaming) {
        strcpy(time_cur, "LIVE");
        if (ep->duration_sec > 0) {
            format_duration(time_dur, ep->duration_sec);
        } else {
            strcpy(time_dur, "--:--");
        }
    } else {
        format_duration(time_cur, position / 1000);
        format_duration(time_dur, duration / 1000);
    }

    SDL_Surface* cur_surf = TTF_RenderUTF8_Blended(get_font_tiny(), time_cur, COLOR_GRAY);
    if (cur_surf) {
        SDL_BlitSurface(cur_surf, NULL, screen, &(SDL_Rect){bar_margin, time_y});
        SDL_FreeSurface(cur_surf);
    }

    SDL_Surface* dur_surf = TTF_RenderUTF8_Blended(get_font_tiny(), time_dur, COLOR_GRAY);
    if (dur_surf) {
        SDL_BlitSurface(dur_surf, NULL, screen, &(SDL_Rect){hw - bar_margin - dur_surf->w, time_y});
        SDL_FreeSurface(dur_surf);
    }

    // Button hints
    GFX_blitButtonGroup((char*[]){"START", "CONTROLS", NULL}, 0, screen, 0);
    if (is_streaming) {
        GFX_blitButtonGroup((char*[]){"B", "STOP", NULL}, 1, screen, 1);
    } else {
        GFX_blitButtonGroup((char*[]){"B", "STOP", "A", "PAUSE", NULL}, 1, screen, 1);
    }
}

// Render buffering screen (shows while streaming episode is buffering)
void render_podcast_buffering(SDL_Surface* screen, int show_setting,
                               int feed_index, int episode_index, int buffer_percent) {
    GFX_clear(screen);

    int hw = screen->w;
    int hh = screen->h;

    // "PODCAST" badge
    int top_y = SCALE1(PADDING);
    const char* badge_text = "PODCAST";
    SDL_Surface* badge_surf = TTF_RenderUTF8_Blended(get_font_tiny(), badge_text, COLOR_GRAY);
    if (badge_surf) {
        int badge_w = badge_surf->w + SCALE1(10);
        int badge_h = badge_surf->h + SCALE1(4);
        int badge_x = SCALE1(PADDING);

        // Border
        SDL_Rect border = {badge_x, top_y, badge_w, badge_h};
        SDL_FillRect(screen, &border, RGB_GRAY);
        SDL_Rect inner = {badge_x + 1, top_y + 1, badge_w - 2, badge_h - 2};
        SDL_FillRect(screen, &inner, RGB_BLACK);
        SDL_BlitSurface(badge_surf, NULL, screen, &(SDL_Rect){badge_x + SCALE1(5), top_y + SCALE1(2)});
        SDL_FreeSurface(badge_surf);
    }

    // Hardware status
    GFX_blitHardwareGroup(screen, show_setting);

    // Center area
    int center_y = hh / 2 - SCALE1(40);

    // "Buffering..." text
    char buf_text[64];
    if (buffer_percent > 0) {
        snprintf(buf_text, sizeof(buf_text), "Buffering %d%%...", buffer_percent);
    } else {
        snprintf(buf_text, sizeof(buf_text), "Connecting...");
    }
    SDL_Surface* buf_surf = TTF_RenderUTF8_Blended(get_font_medium(), buf_text, COLOR_WHITE);
    if (buf_surf) {
        SDL_BlitSurface(buf_surf, NULL, screen, &(SDL_Rect){(hw - buf_surf->w) / 2, center_y});
        SDL_FreeSurface(buf_surf);
    }

    // Episode title (if available)
    PodcastEpisode* ep = Podcast_getEpisode(feed_index, episode_index);
    if (ep) {
        char ep_truncated[256];
        GFX_truncateText(get_font_small(), ep->title, ep_truncated, hw - SCALE1(PADDING * 4), 0);
        SDL_Surface* ep_surf = TTF_RenderUTF8_Blended(get_font_small(), ep_truncated, COLOR_GRAY);
        if (ep_surf) {
            SDL_BlitSurface(ep_surf, NULL, screen, &(SDL_Rect){(hw - ep_surf->w) / 2, center_y + SCALE1(30)});
            SDL_FreeSurface(ep_surf);
        }
    }

    // Progress bar (shows buffer progress)
    int bar_y = center_y + SCALE1(60);
    int bar_h = SCALE1(6);
    int bar_margin = SCALE1(PADDING * 4);
    int bar_w = hw - bar_margin * 2;

    // Background
    SDL_Rect bar_bg = {bar_margin, bar_y, bar_w, bar_h};
    SDL_FillRect(screen, &bar_bg, RGB_DARK_GRAY);

    // Progress fill
    if (buffer_percent > 0) {
        int fill_w = (bar_w * buffer_percent) / 100;
        if (fill_w > 0) {
            SDL_Rect bar_fill = {bar_margin, bar_y, fill_w, bar_h};
            SDL_FillRect(screen, &bar_fill, RGB_WHITE);
        }
    }

    // Button hints
    GFX_blitButtonGroup((char*[]){"B", "CANCEL", NULL}, 1, screen, 1);
}

// Render loading screen
void render_podcast_loading(SDL_Surface* screen, const char* message) {
    GFX_clear(screen);

    int hw = screen->w;
    int hh = screen->h;

    const char* msg = message ? message : "Loading...";
    SDL_Surface* text = TTF_RenderUTF8_Blended(get_font_medium(), msg, COLOR_WHITE);
    if (text) {
        SDL_BlitSurface(text, NULL, screen, &(SDL_Rect){(hw - text->w) / 2, hh / 2});
        SDL_FreeSurface(text);
    }
}

// Check if podcast title is currently scrolling (list or playing screen)
bool Podcast_isTitleScrolling(void) {
    return ScrollText_isScrolling(&podcast_title_scroll) ||
           ScrollText_isScrolling(&podcast_playing_title_scroll);
}

// Animate podcast title scroll only (GPU mode, no screen redraw needed)
void Podcast_animateTitleScroll(void) {
    if (ScrollText_isScrolling(&podcast_title_scroll)) {
        ScrollText_animateOnly(&podcast_title_scroll);
    }
    if (ScrollText_isScrolling(&podcast_playing_title_scroll)) {
        ScrollText_renderGPU_NoBg(&podcast_playing_title_scroll,
                                   podcast_playing_title_scroll.last_font,
                                   podcast_playing_title_scroll.last_color,
                                   podcast_playing_title_scroll.last_x,
                                   podcast_playing_title_scroll.last_y);
    }
}

// Clear podcast title scroll state (call when selection changes)
void Podcast_clearTitleScroll(void) {
    memset(&podcast_title_scroll, 0, sizeof(podcast_title_scroll));
    GFX_clearLayers(LAYER_SCROLLTEXT);
    GFX_resetScrollText();  // Also reset NextUI's internal scroll state
}
