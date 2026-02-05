#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include "defines.h"
#include "api.h"
#include "ui_icons.h"

// Icon paths (relative to pak root)
#define ICON_PATH "res"
#define ICON_FOLDER    ICON_PATH "/icon-folder.png"
#define ICON_AUDIO     ICON_PATH "/icon-audio.png"
#define ICON_PLAY_ALL  ICON_PATH "/icon-play-all.png"
#define ICON_MP3       ICON_PATH "/icon-mp3.png"
#define ICON_FLAC      ICON_PATH "/icon-flac.png"
#define ICON_OGG       ICON_PATH "/icon-ogg.png"
#define ICON_WAV       ICON_PATH "/icon-wav.png"
#define ICON_M4A       ICON_PATH "/icon-m4a.png"
// Menu icons
#define ICON_MENU_LOCAL    ICON_PATH "/icon-menu-local.png"
#define ICON_MENU_RADIO    ICON_PATH "/icon-menu-radio.png"
#define ICON_MENU_DOWNLOAD ICON_PATH "/icon-menu-download.png"
#define ICON_MENU_ABOUT    ICON_PATH "/icon-menu-about.png"
// YouTube/Downloader menu icons
#define ICON_SEARCH        ICON_PATH "/icon-search.png"
#define ICON_UPDATE        ICON_PATH "/icon-update.png"

// Icon storage - original (black) and inverted (white) versions
typedef struct {
    SDL_Surface* folder;
    SDL_Surface* folder_inv;
    SDL_Surface* audio;
    SDL_Surface* audio_inv;
    SDL_Surface* play_all;
    SDL_Surface* play_all_inv;
    SDL_Surface* mp3;
    SDL_Surface* mp3_inv;
    SDL_Surface* flac;
    SDL_Surface* flac_inv;
    SDL_Surface* ogg;
    SDL_Surface* ogg_inv;
    SDL_Surface* wav;
    SDL_Surface* wav_inv;
    SDL_Surface* m4a;
    SDL_Surface* m4a_inv;
    // Menu icons
    SDL_Surface* menu_local;
    SDL_Surface* menu_local_inv;
    SDL_Surface* menu_radio;
    SDL_Surface* menu_radio_inv;
    SDL_Surface* menu_download;
    SDL_Surface* menu_download_inv;
    SDL_Surface* menu_about;
    SDL_Surface* menu_about_inv;
    // YouTube/Downloader menu icons
    SDL_Surface* search;
    SDL_Surface* search_inv;
    SDL_Surface* update;
    SDL_Surface* update_inv;
    bool loaded;
} IconSet;

static IconSet icons = {0};

// Invert colors of a surface (black <-> white)
// Creates a new surface with inverted colors, preserving alpha
static SDL_Surface* invert_surface(SDL_Surface* src) {
    if (!src) return NULL;

    // Create a new surface with same format
    SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(
        0, src->w, src->h, 32, SDL_PIXELFORMAT_RGBA32);

    if (!dst) return NULL;

    // Lock surfaces for direct pixel access
    SDL_LockSurface(src);
    SDL_LockSurface(dst);

    Uint32* src_pixels = (Uint32*)src->pixels;
    Uint32* dst_pixels = (Uint32*)dst->pixels;
    int pixel_count = src->w * src->h;

    for (int i = 0; i < pixel_count; i++) {
        Uint8 r, g, b, a;
        SDL_GetRGBA(src_pixels[i], src->format, &r, &g, &b, &a);

        // Invert RGB, keep alpha
        r = 255 - r;
        g = 255 - g;
        b = 255 - b;

        dst_pixels[i] = SDL_MapRGBA(dst->format, r, g, b, a);
    }

    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);

    return dst;
}

// Load an icon and create its inverted version
static void load_icon_pair(const char* path, SDL_Surface** original, SDL_Surface** inverted) {
    *original = IMG_Load(path);
    if (*original) {
        // Convert to RGBA32 for consistent pixel access
        SDL_Surface* converted = SDL_ConvertSurfaceFormat(*original, SDL_PIXELFORMAT_RGBA32, 0);
        if (converted) {
            SDL_FreeSurface(*original);
            *original = converted;
        }
        *inverted = invert_surface(*original);
    } else {
        *inverted = NULL;
    }
}

// Initialize icons
void Icons_init(void) {
    if (icons.loaded) return;

    load_icon_pair(ICON_FOLDER, &icons.folder, &icons.folder_inv);
    load_icon_pair(ICON_AUDIO, &icons.audio, &icons.audio_inv);
    load_icon_pair(ICON_PLAY_ALL, &icons.play_all, &icons.play_all_inv);
    load_icon_pair(ICON_MP3, &icons.mp3, &icons.mp3_inv);
    load_icon_pair(ICON_FLAC, &icons.flac, &icons.flac_inv);
    load_icon_pair(ICON_OGG, &icons.ogg, &icons.ogg_inv);
    load_icon_pair(ICON_WAV, &icons.wav, &icons.wav_inv);
    load_icon_pair(ICON_M4A, &icons.m4a, &icons.m4a_inv);
    // Menu icons
    load_icon_pair(ICON_MENU_LOCAL, &icons.menu_local, &icons.menu_local_inv);
    load_icon_pair(ICON_MENU_RADIO, &icons.menu_radio, &icons.menu_radio_inv);
    load_icon_pair(ICON_MENU_DOWNLOAD, &icons.menu_download, &icons.menu_download_inv);
    load_icon_pair(ICON_MENU_ABOUT, &icons.menu_about, &icons.menu_about_inv);
    // YouTube/Downloader menu icons
    load_icon_pair(ICON_SEARCH, &icons.search, &icons.search_inv);
    load_icon_pair(ICON_UPDATE, &icons.update, &icons.update_inv);

    // Consider loaded if at least folder icon exists
    icons.loaded = (icons.folder != NULL);

    if (!icons.loaded) {
    }
}

// Cleanup icons
void Icons_quit(void) {
    if (icons.folder) { SDL_FreeSurface(icons.folder); icons.folder = NULL; }
    if (icons.folder_inv) { SDL_FreeSurface(icons.folder_inv); icons.folder_inv = NULL; }
    if (icons.audio) { SDL_FreeSurface(icons.audio); icons.audio = NULL; }
    if (icons.audio_inv) { SDL_FreeSurface(icons.audio_inv); icons.audio_inv = NULL; }
    if (icons.play_all) { SDL_FreeSurface(icons.play_all); icons.play_all = NULL; }
    if (icons.play_all_inv) { SDL_FreeSurface(icons.play_all_inv); icons.play_all_inv = NULL; }
    if (icons.mp3) { SDL_FreeSurface(icons.mp3); icons.mp3 = NULL; }
    if (icons.mp3_inv) { SDL_FreeSurface(icons.mp3_inv); icons.mp3_inv = NULL; }
    if (icons.flac) { SDL_FreeSurface(icons.flac); icons.flac = NULL; }
    if (icons.flac_inv) { SDL_FreeSurface(icons.flac_inv); icons.flac_inv = NULL; }
    if (icons.ogg) { SDL_FreeSurface(icons.ogg); icons.ogg = NULL; }
    if (icons.ogg_inv) { SDL_FreeSurface(icons.ogg_inv); icons.ogg_inv = NULL; }
    if (icons.wav) { SDL_FreeSurface(icons.wav); icons.wav = NULL; }
    if (icons.wav_inv) { SDL_FreeSurface(icons.wav_inv); icons.wav_inv = NULL; }
    if (icons.m4a) { SDL_FreeSurface(icons.m4a); icons.m4a = NULL; }
    if (icons.m4a_inv) { SDL_FreeSurface(icons.m4a_inv); icons.m4a_inv = NULL; }
    // Menu icons
    if (icons.menu_local) { SDL_FreeSurface(icons.menu_local); icons.menu_local = NULL; }
    if (icons.menu_local_inv) { SDL_FreeSurface(icons.menu_local_inv); icons.menu_local_inv = NULL; }
    if (icons.menu_radio) { SDL_FreeSurface(icons.menu_radio); icons.menu_radio = NULL; }
    if (icons.menu_radio_inv) { SDL_FreeSurface(icons.menu_radio_inv); icons.menu_radio_inv = NULL; }
    if (icons.menu_download) { SDL_FreeSurface(icons.menu_download); icons.menu_download = NULL; }
    if (icons.menu_download_inv) { SDL_FreeSurface(icons.menu_download_inv); icons.menu_download_inv = NULL; }
    if (icons.menu_about) { SDL_FreeSurface(icons.menu_about); icons.menu_about = NULL; }
    if (icons.menu_about_inv) { SDL_FreeSurface(icons.menu_about_inv); icons.menu_about_inv = NULL; }
    // YouTube/Downloader menu icons
    if (icons.search) { SDL_FreeSurface(icons.search); icons.search = NULL; }
    if (icons.search_inv) { SDL_FreeSurface(icons.search_inv); icons.search_inv = NULL; }
    if (icons.update) { SDL_FreeSurface(icons.update); icons.update = NULL; }
    if (icons.update_inv) { SDL_FreeSurface(icons.update_inv); icons.update_inv = NULL; }
    icons.loaded = false;
}

// Check if icons are loaded
bool Icons_isLoaded(void) {
    return icons.loaded;
}

// Get folder icon
SDL_Surface* Icons_getFolder(bool selected) {
    if (!icons.loaded) return NULL;
    return selected ? icons.folder : icons.folder_inv;
}

// Get generic audio icon
SDL_Surface* Icons_getAudio(bool selected) {
    if (!icons.loaded) return NULL;
    return selected ? icons.audio : icons.audio_inv;
}

// Get play all icon
SDL_Surface* Icons_getPlayAll(bool selected) {
    if (!icons.loaded) return NULL;
    return selected ? icons.play_all : icons.play_all_inv;
}

// Get icon for specific audio format
// Falls back to generic audio icon if format-specific icon not available
SDL_Surface* Icons_getForFormat(AudioFormat format, bool selected) {
    if (!icons.loaded) return NULL;

    SDL_Surface* icon = NULL;
    SDL_Surface* icon_inv = NULL;

    switch (format) {
        case AUDIO_FORMAT_MP3:
            icon = icons.mp3;
            icon_inv = icons.mp3_inv;
            break;
        case AUDIO_FORMAT_FLAC:
            icon = icons.flac;
            icon_inv = icons.flac_inv;
            break;
        case AUDIO_FORMAT_OGG:
            icon = icons.ogg;
            icon_inv = icons.ogg_inv;
            break;
        case AUDIO_FORMAT_WAV:
            icon = icons.wav;
            icon_inv = icons.wav_inv;
            break;
        case AUDIO_FORMAT_M4A:
            icon = icons.m4a;
            icon_inv = icons.m4a_inv;
            break;
        default:
            // Fall back to generic audio icon
            icon = icons.audio;
            icon_inv = icons.audio_inv;
            break;
    }

    // If format-specific icon not loaded, fall back to generic
    if (!icon) {
        icon = icons.audio;
        icon_inv = icons.audio_inv;
    }

    return selected ? icon : icon_inv;
}
