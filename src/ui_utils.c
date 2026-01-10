#include <stdio.h>
#include <string.h>
#include "ui_utils.h"
#include "ui_fonts.h"

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

// Scroll gap for software scrolling
#define SCROLL_GAP 30

// Reset scroll state for new text
void ScrollText_reset(ScrollTextState* state, const char* text, TTF_Font* font, int max_width, bool use_gpu) {
    // Clear the scroll layer when text changes to avoid ghost text
    GFX_clearLayers(LAYER_SCROLLTEXT);

    // Free old cached surface if exists
    if (state->cached_scroll_surface) {
        SDL_FreeSurface(state->cached_scroll_surface);
        state->cached_scroll_surface = NULL;
    }

    strncpy(state->text, text, sizeof(state->text) - 1);
    state->text[sizeof(state->text) - 1] = '\0';
    int text_h = 0;
    TTF_SizeUTF8(font, state->text, &state->text_width, &text_h);
    state->max_width = max_width;
    state->start_time = SDL_GetTicks();
    state->scroll_offset = 0;
    state->use_gpu_scroll = use_gpu;

    if (use_gpu) {
        // Use NextUI's reset function for GPU scrolling
        state->needs_scroll = GFX_resetScrollText(font, state->text, max_width);
    } else {
        // Software scrolling - just check if text is wider
        state->needs_scroll = (state->text_width > max_width);
    }

    // Pre-create cached scroll surface for GPU scroll without background
    if (state->needs_scroll && use_gpu) {
        int padding = SCALE1(SCROLL_GAP);
        int total_width = state->text_width * 2 + padding;
        int height = TTF_FontHeight(font);

        state->cached_scroll_surface = SDL_CreateRGBSurfaceWithFormat(0,
            total_width, height, 32, SDL_PIXELFORMAT_RGBA8888);

        if (state->cached_scroll_surface) {
            // Clear to transparent
            SDL_FillRect(state->cached_scroll_surface, NULL, 0);

            // Render text twice for seamless looping
            SDL_Color white = {255, 255, 255, 255};  // Will be overwritten by actual color
            SDL_Surface* text_surf = TTF_RenderUTF8_Blended(font, state->text, white);
            if (text_surf) {
                SDL_SetSurfaceBlendMode(text_surf, SDL_BLENDMODE_NONE);
                SDL_BlitSurface(text_surf, NULL, state->cached_scroll_surface, &(SDL_Rect){0, 0, 0, 0});
                SDL_BlitSurface(text_surf, NULL, state->cached_scroll_surface, &(SDL_Rect){state->text_width + padding, 0, 0, 0});
                SDL_FreeSurface(text_surf);
            }
        }
    }
}

// Check if scrolling is active (text needs to scroll)
bool ScrollText_isScrolling(ScrollTextState* state) {
    return state->needs_scroll;
}

// Update scroll animation only (for GPU mode, doesn't redraw screen)
// Call this when dirty=0 but scrolling is active - uses saved position from last render
void ScrollText_animateOnly(ScrollTextState* state) {
    if (!state->text[0] || !state->needs_scroll || !state->use_gpu_scroll) return;
    if (!state->last_font) return;  // Never rendered yet

    // Just update the scroll layer - don't redraw main screen
    GFX_clearLayers(LAYER_SCROLLTEXT);
    GFX_scrollTextTexture(
        state->last_font,
        state->text,
        state->last_x, state->last_y,
        state->max_width,
        TTF_FontHeight(state->last_font),
        state->last_color,
        1.0f
    );
}

// Render scrolling text - GPU mode for lists, software mode for player
void ScrollText_render(ScrollTextState* state, TTF_Font* font, SDL_Color color,
                       SDL_Surface* screen, int x, int y) {
    if (!state->text[0]) return;

    // Save position info for animate-only mode
    state->last_x = x;
    state->last_y = y;
    state->last_font = font;
    state->last_color = color;

    // If text fits, render normally without scrolling
    if (!state->needs_scroll) {
        // Clear scroll layer to remove any previous scrolling text
        GFX_clearLayers(LAYER_SCROLLTEXT);
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, state->text, color);
        if (surf) {
            SDL_BlitSurface(surf, NULL, screen, &(SDL_Rect){x, y, 0, 0});
            SDL_FreeSurface(surf);
        }
        return;
    }

    if (state->use_gpu_scroll) {
        // GPU mode: Use NextUI's scroll text (has pill background)
        GFX_clearLayers(LAYER_SCROLLTEXT);
        GFX_scrollTextTexture(
            font,
            state->text,
            x, y,
            state->max_width,
            TTF_FontHeight(font),
            color,
            1.0f
        );
    } else {
        // Software mode: No background, smooth scrolling for player title
        GFX_clearLayers(LAYER_SCROLLTEXT);

        // Render text surface
        SDL_Surface* single_surf = TTF_RenderUTF8_Blended(font, state->text, color);
        if (!single_surf) return;

        // Create combined surface with two text copies for seamless loop
        SDL_Surface* full_surf = SDL_CreateRGBSurfaceWithFormat(0,
            state->text_width * 2 + SCROLL_GAP, single_surf->h, 32, SDL_PIXELFORMAT_RGBA8888);
        if (!full_surf) {
            SDL_FreeSurface(single_surf);
            return;
        }

        SDL_FillRect(full_surf, NULL, 0);
        SDL_SetSurfaceBlendMode(single_surf, SDL_BLENDMODE_NONE);
        SDL_BlitSurface(single_surf, NULL, full_surf, &(SDL_Rect){0, 0, 0, 0});
        SDL_BlitSurface(single_surf, NULL, full_surf, &(SDL_Rect){state->text_width + SCROLL_GAP, 0, 0, 0});
        SDL_FreeSurface(single_surf);

        // Simple per-frame increment like NextUI
        state->scroll_offset += 2;
        if (state->scroll_offset >= state->text_width + SCROLL_GAP) {
            state->scroll_offset = 0;
        }

        // Blit the visible portion
        SDL_SetSurfaceBlendMode(full_surf, SDL_BLENDMODE_BLEND);
        SDL_Rect src = {state->scroll_offset, 0, state->max_width, full_surf->h};
        SDL_Rect dst = {x, y, 0, 0};
        SDL_BlitSurface(full_surf, &src, screen, &dst);
        SDL_FreeSurface(full_surf);
    }
}

// Unified update: checks for text change, resets if needed, and renders
// use_gpu: true for lists (GPU layer with pill bg), false for player (software, no bg)
void ScrollText_update(ScrollTextState* state, const char* text, TTF_Font* font,
                       int max_width, SDL_Color color, SDL_Surface* screen, int x, int y, bool use_gpu) {
    // Check if text changed - use existing state->text for comparison
    if (strcmp(state->text, text) != 0) {
        ScrollText_reset(state, text, font, max_width, use_gpu);
    }
    ScrollText_render(state, font, color, screen, x, y);
}

// GPU scroll without background (for player title)
// Uses PLAT_drawOnLayer to render to GPU layer without pill background
void ScrollText_renderGPU_NoBg(ScrollTextState* state, TTF_Font* font,
                                SDL_Color color, int x, int y) {
    if (!state->text[0] || !state->needs_scroll || !state->cached_scroll_surface) {
        // Static text or no scroll needed - just clear layer
        PLAT_clearLayers(LAYER_SCROLLTEXT);
        return;
    }

    // Save render info
    state->last_x = x;
    state->last_y = y;
    state->last_font = font;
    state->last_color = color;

    int padding = SCALE1(SCROLL_GAP);
    int height = state->cached_scroll_surface->h;

    // Create clipped view at current scroll offset (only this is created per-frame)
    SDL_Surface* clipped = SDL_CreateRGBSurfaceWithFormat(0,
        state->max_width, height, 32, SDL_PIXELFORMAT_RGBA8888);
    if (!clipped) return;

    SDL_FillRect(clipped, NULL, 0);
    SDL_SetSurfaceBlendMode(state->cached_scroll_surface, SDL_BLENDMODE_NONE);
    SDL_Rect src = {state->scroll_offset, 0, state->max_width, height};
    SDL_BlitSurface(state->cached_scroll_surface, &src, clipped, NULL);

    // Render to GPU layer
    PLAT_clearLayers(LAYER_SCROLLTEXT);
    PLAT_drawOnLayer(clipped, x, y, state->max_width, height, 1.0f, false, LAYER_SCROLLTEXT);
    SDL_FreeSurface(clipped);

    // Advance scroll offset (1 pixel per frame for smooth, slower scrolling)
    state->scroll_offset += 1;
    if (state->scroll_offset >= state->text_width + padding) {
        state->scroll_offset = 0;
    }

    PLAT_GPU_Flip();
}

// Render standard screen header (title pill + hardware status)
void render_screen_header(SDL_Surface* screen, const char* title, int show_setting) {
    int hw = screen->w;
    char truncated[256];

    int title_width = GFX_truncateText(get_font_medium(), title, truncated, hw - SCALE1(PADDING * 4), SCALE1(BUTTON_PADDING * 2));
    GFX_blitPill(ASSET_BLACK_PILL, screen, &(SDL_Rect){SCALE1(PADDING), SCALE1(PADDING), title_width, SCALE1(PILL_SIZE)});

    SDL_Surface* title_text = TTF_RenderUTF8_Blended(get_font_medium(), truncated, COLOR_GRAY);
    if (title_text) {
        SDL_BlitSurface(title_text, NULL, screen, &(SDL_Rect){SCALE1(PADDING) + SCALE1(BUTTON_PADDING), SCALE1(PADDING + 4)});
        SDL_FreeSurface(title_text);
    }

    if (hw >= SCALE1(320)) {
        GFX_blitHardwareGroup(screen, show_setting);
    }
}

// Adjust scroll offset to keep selected item visible
void adjust_list_scroll(int selected, int* scroll, int items_per_page) {
    if (selected < *scroll) {
        *scroll = selected;
    }
    if (selected >= *scroll + items_per_page) {
        *scroll = selected - items_per_page + 1;
    }
}

// Render scroll up/down indicators for lists
void render_scroll_indicators(SDL_Surface* screen, int scroll, int items_per_page, int total_count) {
    if (total_count <= items_per_page) return;

    int hw = screen->w;
    int hh = screen->h;
    int ox = (hw - SCALE1(24)) / 2;

    if (scroll > 0) {
        GFX_blitAsset(ASSET_SCROLL_UP, NULL, screen, &(SDL_Rect){ox, SCALE1(PADDING + PILL_SIZE)});
    }
    if (scroll + items_per_page < total_count) {
        GFX_blitAsset(ASSET_SCROLL_DOWN, NULL, screen, &(SDL_Rect){ox, hh - SCALE1(PADDING + PILL_SIZE + BUTTON_SIZE)});
    }
}

// ============================================
// Generic List Rendering Helpers
// ============================================

// Calculate standard list layout based on screen dimensions
ListLayout calc_list_layout(SDL_Surface* screen, int offset_y) {
    int hw = screen->w;
    int hh = screen->h;

    ListLayout layout;
    layout.list_y = SCALE1(PADDING + PILL_SIZE + BUTTON_MARGIN) + offset_y;
    layout.list_h = hh - layout.list_y - SCALE1(PADDING + BUTTON_SIZE + BUTTON_MARGIN);
    layout.item_h = SCALE1(PILL_SIZE);
    layout.items_per_page = layout.list_h / layout.item_h;
    layout.max_width = hw - SCALE1(PADDING * 2);

    return layout;
}

// Render a list item's text with optional scrolling for selected items
void render_list_item_text(SDL_Surface* screen, ScrollTextState* scroll_state,
                           const char* text, TTF_Font* font_param,
                           int text_x, int text_y, int max_text_width,
                           bool selected) {
    SDL_Color text_color = get_list_text_color(selected);

    if (selected && scroll_state) {
        // Selected item: use scrolling text (GPU mode with pill bg)
        ScrollText_update(scroll_state, text, font_param, max_text_width,
                          text_color, screen, text_x, text_y, true);
    } else {
        // Non-selected items: static rendering with clipping
        SDL_Surface* text_surf = TTF_RenderUTF8_Blended(font_param, text, text_color);
        if (text_surf) {
            SDL_Rect src = {0, 0, text_surf->w > max_text_width ? max_text_width : text_surf->w, text_surf->h};
            SDL_BlitSurface(text_surf, &src, screen, &(SDL_Rect){text_x, text_y, 0, 0});
            SDL_FreeSurface(text_surf);
        }
    }
}

// Render a list item's pill background and calculate text position
ListItemPos render_list_item_pill(SDL_Surface* screen, ListLayout* layout,
                                   const char* text, char* truncated,
                                   int y, bool selected, int prefix_width) {
    ListItemPos pos;

    // Calculate text width for pill sizing (list items use medium font)
    pos.pill_width = calc_list_pill_width(get_font_medium(), text, truncated, layout->max_width, prefix_width);

    // Background pill (sized to text width)
    SDL_Rect pill_rect = {SCALE1(PADDING), y, pos.pill_width, layout->item_h};
    draw_list_item_bg(screen, &pill_rect, selected);

    // Calculate text position
    pos.text_x = SCALE1(PADDING) + SCALE1(BUTTON_PADDING);
    pos.text_y = y + (layout->item_h - TTF_FontHeight(get_font_medium())) / 2;

    return pos;
}

// Render a menu item's pill background and calculate text position
// Menu items have larger spacing (PILL_SIZE + BUTTON_MARGIN) but pill height is just PILL_SIZE
MenuItemPos render_menu_item_pill(SDL_Surface* screen, ListLayout* layout,
                                   const char* text, char* truncated,
                                   int index, bool selected) {
    MenuItemPos pos;

    // Menu items have larger spacing between them
    int item_h = SCALE1(PILL_SIZE + BUTTON_MARGIN);
    pos.item_y = layout->list_y + index * item_h;

    // Calculate text width for pill sizing
    pos.pill_width = calc_list_pill_width(get_font_large(), text, truncated, layout->max_width, 0);

    // Background pill (pill height is PILL_SIZE, not item_h)
    SDL_Rect pill_rect = {SCALE1(PADDING), pos.item_y, pos.pill_width, SCALE1(PILL_SIZE)};
    draw_list_item_bg(screen, &pill_rect, selected);

    // Calculate text position (centered within PILL_SIZE, not item_h)
    pos.text_x = SCALE1(PADDING) + SCALE1(BUTTON_PADDING);
    pos.text_y = pos.item_y + (SCALE1(PILL_SIZE) - TTF_FontHeight(get_font_large())) / 2;

    return pos;
}

// ============================================
// Generic Simple Menu Rendering
// ============================================

// Render a simple menu with optional customization callbacks
void render_simple_menu(SDL_Surface* screen, int show_setting, int menu_selected,
                        const SimpleMenuConfig* config) {
    GFX_clear(screen);
    char truncated[256];
    char label_buffer[256];

    render_screen_header(screen, config->title, show_setting);
    ListLayout layout = calc_list_layout(screen, 0);

    for (int i = 0; i < config->item_count; i++) {
        bool selected = (i == menu_selected);

        // Get label (use callback if provided)
        const char* label = config->items[i];
        if (config->get_label) {
            const char* custom = config->get_label(i, label, label_buffer, sizeof(label_buffer));
            if (custom) label = custom;
        }

        // Render pill and text
        MenuItemPos pos = render_menu_item_pill(screen, &layout, label, truncated, i, selected);
        render_list_item_text(screen, NULL, truncated, get_font_large(),
                              pos.text_x, pos.text_y, layout.max_width, selected);

        // Render badge if callback provided
        if (config->render_badge) {
            config->render_badge(screen, i, selected, pos.item_y, SCALE1(PILL_SIZE));
        }
    }

    // Button hints
    GFX_blitButtonGroup((char*[]){"U/D", "SELECT", NULL}, 0, screen, 0);
    GFX_blitButtonGroup((char*[]){"B", (char*)config->btn_b_label, "A", "OPEN", NULL}, 1, screen, 1);
}
