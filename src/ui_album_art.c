#include <stdint.h>
#include <stdbool.h>
#include "ui_album_art.h"

// Cache for album art background rendering
static SDL_Surface* cached_bg_surface = NULL;
static SDL_Surface* cached_bg_art_ptr = NULL;  // Track pointer for cache invalidation
static int cached_bg_art_w = 0;  // Track source dimensions for cache invalidation
static int cached_bg_art_h = 0;
static int cached_screen_w = 0;  // Track screen dimensions
static int cached_screen_h = 0;

// Render album art as a triangular background with fade effect
void render_album_art_background(SDL_Surface* screen, SDL_Surface* album_art) {
    if (!album_art || !screen) return;
    if (album_art->w <= 0 || album_art->h <= 0) return;

    int hw = screen->w;
    int hh = screen->h;

    // Check if we need to regenerate the cached surface
    // Track both pointer and dimensions to detect changes
    // Pointer is safe to compare here because caller verified !is_fetching
    bool needs_regen = (cached_bg_surface == NULL) ||
                       (cached_bg_art_ptr != album_art) ||
                       (cached_bg_art_w != album_art->w) ||
                       (cached_bg_art_h != album_art->h) ||
                       (cached_screen_w != hw) ||
                       (cached_screen_h != hh);

    if (needs_regen) {
        // Free old cached surface
        if (cached_bg_surface) {
            SDL_FreeSurface(cached_bg_surface);
            cached_bg_surface = NULL;
        }

        // Calculate dimensions: square background matching screen height
        // Positioned at bottom-right corner
        int bg_size = hh;  // Square size = screen height (for square album art)
        int bg_width = bg_size;
        int bg_height = bg_size;
        int bg_x = hw - bg_width;   // Right edge
        int bg_y = 0;               // Top edge (full height)

        // Create RGBA surface for the masked background
        cached_bg_surface = SDL_CreateRGBSurfaceWithFormat(0, hw, hh, 32, SDL_PIXELFORMAT_RGBA8888);
        if (!cached_bg_surface) return;

        // Fill with transparent
        SDL_FillRect(cached_bg_surface, NULL, 0);

        // Scale album art to cover the square region
        SDL_Surface* scaled_art = SDL_CreateRGBSurfaceWithFormat(0, bg_width, bg_height, 32, SDL_PIXELFORMAT_RGBA8888);
        if (!scaled_art) {
            SDL_FreeSurface(cached_bg_surface);
            cached_bg_surface = NULL;
            return;
        }

        // Scale the album art to fill the region (crop if needed)
        int src_w = album_art->w;
        int src_h = album_art->h;

        // Calculate scale to fill (crop excess)
        float scale_w = (float)bg_width / src_w;
        float scale_h = (float)bg_height / src_h;
        float scale = (scale_w > scale_h) ? scale_w : scale_h;

        int crop_w = (int)(bg_width / scale);
        int crop_h = (int)(bg_height / scale);
        int crop_x = (src_w - crop_w) / 2;
        int crop_y = (src_h - crop_h) / 2;

        if (crop_x < 0) crop_x = 0;
        if (crop_y < 0) crop_y = 0;
        if (crop_x + crop_w > src_w) crop_w = src_w - crop_x;
        if (crop_y + crop_h > src_h) crop_h = src_h - crop_y;

        SDL_Rect src_rect = {crop_x, crop_y, crop_w, crop_h};
        SDL_Rect dst_rect = {0, 0, bg_width, bg_height};
        SDL_BlitScaled(album_art, &src_rect, scaled_art, &dst_rect);

        // Apply triangular alpha mask with gradient
        // Lock surfaces for pixel access
        if (SDL_LockSurface(scaled_art) != 0 || SDL_LockSurface(cached_bg_surface) != 0) {
            SDL_FreeSurface(scaled_art);
            SDL_FreeSurface(cached_bg_surface);
            cached_bg_surface = NULL;
            return;
        }

        uint32_t* src_pixels = (uint32_t*)scaled_art->pixels;
        uint32_t* dst_pixels = (uint32_t*)cached_bg_surface->pixels;
        int src_pitch = scaled_art->pitch / 4;
        int dst_pitch = cached_bg_surface->pitch / 4;

        float max_opacity = 0.80f;  // 80% max opacity at right edge
        float feather_width = bg_width * 0.20f;  // Feather zone: 20% of background width for soft edge

        for (int y = 0; y < bg_height; y++) {
            // Diagonal in SCREEN coordinates: from bottom-left of square to middle-top of square
            // Square is positioned at (bg_x, bg_y) with size bg_size
            float t = (float)y / bg_height;

            // Screen coordinates of the diagonal line
            // At y=0 (top of square): screen_diag_x = bg_x + bg_width/2 (middle of square)
            // At y=bg_height (bottom of square): screen_diag_x = bg_x (left edge of square)
            float screen_diag = bg_x + (bg_width * 0.5f) * (1.0f - t);

            // Convert to local coordinates (relative to bg_x)
            float diag_x = screen_diag - bg_x;
            if (diag_x < 0) diag_x = 0;  // Clamp to region bounds

            for (int x = 0; x < bg_width; x++) {
                // Calculate distance from diagonal line (can be negative for feathering)
                float dist_from_diag = (float)x - diag_x;

                // Calculate opacity based on distance from diagonal with feathering
                float opacity = 0.0f;

                // Total fade distance: from (diag_x - feather_width) to right edge
                // This creates one continuous smooth gradient
                float total_width = (bg_width - diag_x) + feather_width;
                float adjusted_dist = dist_from_diag + feather_width;  // Shift so feather zone starts at 0

                if (adjusted_dist > 0 && total_width > 0) {
                    float fade = adjusted_dist / total_width;
                    if (fade > 1.0f) fade = 1.0f;

                    // Apply smooth easing (smoothstep) for seamless transition
                    fade = fade * fade * (3.0f - 2.0f * fade);

                    opacity = fade * max_opacity;
                }

                if (opacity > 0.001f) {
                    // Get source pixel
                    uint32_t src_pixel = src_pixels[y * src_pitch + x];

                    // Extract RGB components (SDL_PIXELFORMAT_RGBA8888: R bits 24-31, G 16-23, B 8-15, A 0-7)
                    uint8_t r = (src_pixel >> 24) & 0xFF;
                    uint8_t g = (src_pixel >> 16) & 0xFF;
                    uint8_t b = (src_pixel >> 8) & 0xFF;

                    uint8_t alpha = (uint8_t)(opacity * 255.0f);

                    // Write to destination at correct screen position (RGBA8888 format)
                    int dst_x = bg_x + x;
                    int dst_y = bg_y + y;
                    if (dst_x >= 0 && dst_x < hw && dst_y >= 0 && dst_y < hh) {
                        dst_pixels[dst_y * dst_pitch + dst_x] = (r << 24) | (g << 16) | (b << 8) | alpha;
                    }
                }
            }
        }

        SDL_UnlockSurface(cached_bg_surface);
        SDL_UnlockSurface(scaled_art);
        SDL_FreeSurface(scaled_art);

        // Remember source info for cache validation
        cached_bg_art_ptr = album_art;
        cached_bg_art_w = album_art->w;
        cached_bg_art_h = album_art->h;
        cached_screen_w = hw;
        cached_screen_h = hh;
    }

    // Blit the cached background surface
    if (cached_bg_surface) {
        SDL_SetSurfaceBlendMode(cached_bg_surface, SDL_BLENDMODE_BLEND);
        SDL_BlitSurface(cached_bg_surface, NULL, screen, NULL);
    }
}

// Cleanup cached background surface (call on exit or when album art changes)
void cleanup_album_art_background(void) {
    if (cached_bg_surface) {
        SDL_FreeSurface(cached_bg_surface);
        cached_bg_surface = NULL;
    }
    cached_bg_art_ptr = NULL;
    cached_bg_art_w = 0;
    cached_bg_art_h = 0;
    cached_screen_w = 0;
    cached_screen_h = 0;
}
