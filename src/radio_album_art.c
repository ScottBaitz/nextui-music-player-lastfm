#define _GNU_SOURCE
#include "radio_album_art.h"
#include "radio_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>

#include "defines.h"
#include "api.h"
#include "include/parson/parson.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

// Album art module state
typedef struct {
    SDL_Surface* album_art;
    char last_art_artist[256];
    char last_art_title[256];
    bool art_fetch_in_progress;
} AlbumArtContext;

static AlbumArtContext art_ctx = {0};

// Simple hash function for cache filename
static unsigned int simple_hash(const char* str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

// Get album art cache directory path
static void get_cache_dir(char* path, int path_size) {
    const char* home = getenv("HOME");
    if (home) {
        snprintf(path, path_size, "%s/.cache/albumart", home);
    } else {
        snprintf(path, path_size, "/tmp/albumart_cache");
    }
}

// Ensure cache directory exists
static void ensure_cache_dir(void) {
    char cache_dir[512];
    get_cache_dir(cache_dir, sizeof(cache_dir));

    // Create parent .cache directory
    char parent_dir[512];
    const char* home = getenv("HOME");
    if (home) {
        snprintf(parent_dir, sizeof(parent_dir), "%s/.cache", home);
        mkdir(parent_dir, 0755);
    }

    // Create albumart cache directory
    mkdir(cache_dir, 0755);
}

// Clean up old cache files (older than 7 days)
static void cleanup_old_cache(void) {
    char cache_dir[512];
    get_cache_dir(cache_dir, sizeof(cache_dir));

    DIR* dir = opendir(cache_dir);
    if (!dir) return;

    time_t now = time(NULL);
    time_t max_age = 7 * 24 * 60 * 60;  // 7 days in seconds

    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char filepath[768];
        snprintf(filepath, sizeof(filepath), "%s/%s", cache_dir, ent->d_name);

        struct stat st;
        if (stat(filepath, &st) == 0) {
            if (now - st.st_mtime > max_age) {
                unlink(filepath);
            }
        }
    }
    closedir(dir);
}

// Get cache file path for artist+title
static void get_cache_filepath(const char* artist, const char* title, char* path, int path_size) {
    char cache_dir[512];
    get_cache_dir(cache_dir, sizeof(cache_dir));

    // Create hash from artist+title
    char combined[512];
    snprintf(combined, sizeof(combined), "%s_%s", artist ? artist : "", title ? title : "");
    unsigned int hash = simple_hash(combined);

    snprintf(path, path_size, "%s/%08x.jpg", cache_dir, hash);
}

// Load album art from cache file
static SDL_Surface* load_cached_album_art(const char* cache_path) {
    FILE* f = fopen(cache_path, "rb");
    if (!f) return NULL;

    // Get file size
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 2 * 1024 * 1024) {  // Max 2MB
        fclose(f);
        return NULL;
    }

    uint8_t* data = (uint8_t*)malloc(size);
    if (!data) {
        fclose(f);
        return NULL;
    }

    if (fread(data, 1, size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);

    SDL_RWops* rw = SDL_RWFromConstMem(data, size);
    SDL_Surface* art = NULL;
    if (rw) {
        art = IMG_Load_RW(rw, 1);
    }
    free(data);

    return art;
}

// Save album art to cache file
static void save_album_art_to_cache(const char* cache_path, const uint8_t* data, int size) {
    FILE* f = fopen(cache_path, "wb");
    if (!f) return;

    fwrite(data, 1, size, f);
    fclose(f);
}

// URL encode a string for use in query parameters
static void url_encode(const char* src, char* dst, int dst_size) {
    const char* hex = "0123456789ABCDEF";
    int j = 0;
    for (int i = 0; src[i] && j < dst_size - 4; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[j++] = c;
        } else if (c == ' ') {
            dst[j++] = '+';
        } else {
            dst[j++] = '%';
            dst[j++] = hex[c >> 4];
            dst[j++] = hex[c & 0x0F];
        }
    }
    dst[j] = '\0';
}

void radio_album_art_init(void) {
    memset(&art_ctx, 0, sizeof(AlbumArtContext));
}

void radio_album_art_cleanup(void) {
    if (art_ctx.album_art) {
        SDL_FreeSurface(art_ctx.album_art);
        art_ctx.album_art = NULL;
    }
    art_ctx.last_art_artist[0] = '\0';
    art_ctx.last_art_title[0] = '\0';
    art_ctx.art_fetch_in_progress = false;
}

void radio_album_art_clear(void) {
    if (art_ctx.album_art) {
        SDL_FreeSurface(art_ctx.album_art);
        art_ctx.album_art = NULL;
    }
    art_ctx.last_art_artist[0] = '\0';
    art_ctx.last_art_title[0] = '\0';
}

SDL_Surface* radio_album_art_get(void) {
    return art_ctx.album_art;
}

bool radio_album_art_is_fetching(void) {
    return art_ctx.art_fetch_in_progress;
}

// Fetch album art from iTunes Search API (with disk caching)
void radio_album_art_fetch(const char* artist, const char* title) {
    if (!artist || !title || (artist[0] == '\0' && title[0] == '\0')) {
        return;
    }

    // Check if we already fetched art for this track
    if (strcmp(art_ctx.last_art_artist, artist) == 0 &&
        strcmp(art_ctx.last_art_title, title) == 0) {
        return;  // Already fetched
    }

    // Mark as fetching and save current track
    art_ctx.art_fetch_in_progress = true;
    strncpy(art_ctx.last_art_artist, artist, sizeof(art_ctx.last_art_artist) - 1);
    strncpy(art_ctx.last_art_title, title, sizeof(art_ctx.last_art_title) - 1);

    // Ensure cache directory exists
    ensure_cache_dir();

    // Periodically clean up old cache (check ~once per hour based on static counter)
    static int cleanup_counter = 0;
    if (++cleanup_counter >= 60) {
        cleanup_counter = 0;
        cleanup_old_cache();
    }

    // Check disk cache first
    char cache_path[768];
    get_cache_filepath(artist, title, cache_path, sizeof(cache_path));

    SDL_Surface* cached_art = load_cached_album_art(cache_path);
    if (cached_art) {
        // Free previous art
        if (art_ctx.album_art) {
            SDL_FreeSurface(art_ctx.album_art);
        }
        art_ctx.album_art = cached_art;
        art_ctx.art_fetch_in_progress = false;
        return;
    }

    // Build search query using iTunes API
    char encoded_artist[512];
    char encoded_title[512];
    url_encode(artist, encoded_artist, sizeof(encoded_artist));
    url_encode(title, encoded_title, sizeof(encoded_title));

    char search_url[1024];
    if (artist[0] && title[0]) {
        snprintf(search_url, sizeof(search_url),
            "https://itunes.apple.com/search?term=%s+%s&media=music&limit=1",
            encoded_artist, encoded_title);
    } else if (artist[0]) {
        snprintf(search_url, sizeof(search_url),
            "https://itunes.apple.com/search?term=%s&media=music&limit=1",
            encoded_artist);
    } else {
        snprintf(search_url, sizeof(search_url),
            "https://itunes.apple.com/search?term=%s&media=music&limit=1",
            encoded_title);
    }

    // Fetch iTunes API response
    uint8_t* response_buf = (uint8_t*)malloc(32 * 1024);  // 32KB for JSON
    if (!response_buf) {
        art_ctx.art_fetch_in_progress = false;
        return;
    }

    int bytes = radio_net_fetch(search_url, response_buf, 32 * 1024, NULL, 0);
    if (bytes <= 0) {
        LOG_error("Failed to fetch iTunes search results\n");
        free(response_buf);
        art_ctx.art_fetch_in_progress = false;
        return;
    }

    response_buf[bytes] = '\0';

    // Parse JSON response
    JSON_Value* root = json_parse_string((const char*)response_buf);
    free(response_buf);

    if (!root) {
        LOG_error("Failed to parse iTunes JSON response\n");
        art_ctx.art_fetch_in_progress = false;
        return;
    }

    JSON_Object* obj = json_value_get_object(root);
    if (!obj) {
        json_value_free(root);
        art_ctx.art_fetch_in_progress = false;
        return;
    }

    // Get results array
    JSON_Array* results = json_object_get_array(obj, "results");
    if (!results || json_array_get_count(results) == 0) {
        json_value_free(root);
        art_ctx.art_fetch_in_progress = false;
        return;
    }

    // Get first result
    JSON_Object* track = json_array_get_object(results, 0);
    if (!track) {
        json_value_free(root);
        art_ctx.art_fetch_in_progress = false;
        return;
    }

    // Get artwork URL (100x100 by default, we'll request 300x300)
    const char* artwork_url = json_object_get_string(track, "artworkUrl100");
    if (!artwork_url) {
        json_value_free(root);
        art_ctx.art_fetch_in_progress = false;
        return;
    }

    // Modify URL to get larger image and convert HTTPS to HTTP for better compatibility
    char large_artwork_url[512];
    if (strncmp(artwork_url, "https://", 8) == 0) {
        // Convert to HTTP and remove -ssl from hostname
        const char* after_https = artwork_url + 8;
        char* ssl_pos = strstr(after_https, "-ssl.");
        if (ssl_pos) {
            // Build HTTP URL without -ssl
            int prefix_len = ssl_pos - after_https;
            snprintf(large_artwork_url, sizeof(large_artwork_url), "http://%.*s%s",
                     prefix_len, after_https, ssl_pos + 4);
        } else {
            // Just convert https to http
            snprintf(large_artwork_url, sizeof(large_artwork_url), "http://%s", after_https);
        }
    } else {
        strncpy(large_artwork_url, artwork_url, sizeof(large_artwork_url) - 1);
    }
    large_artwork_url[sizeof(large_artwork_url) - 1] = '\0';

    // Replace 100x100 with 300x300 for larger image
    char* size_str = strstr(large_artwork_url, "100x100");
    if (size_str) {
        memcpy(size_str, "300x300", 7);
    }

    json_value_free(root);

    // Download the image - use larger buffer for high-res images
    uint8_t* image_buf = (uint8_t*)malloc(1024 * 1024);  // 1MB for image
    if (!image_buf) {
        art_ctx.art_fetch_in_progress = false;
        return;
    }

    int image_bytes = radio_net_fetch(large_artwork_url, image_buf, 1024 * 1024, NULL, 0);
    if (image_bytes <= 0) {
        LOG_error("Failed to download album art image (bytes=%d)\n", image_bytes);
        free(image_buf);
        art_ctx.art_fetch_in_progress = false;
        return;
    }

    // Load image into SDL_Surface
    SDL_RWops* rw = SDL_RWFromConstMem(image_buf, image_bytes);
    if (rw) {
        SDL_Surface* art = IMG_Load_RW(rw, 1);  // 1 = auto-close RWops
        if (art) {
            // Free previous art
            if (art_ctx.album_art) {
                SDL_FreeSurface(art_ctx.album_art);
            }
            art_ctx.album_art = art;

            // Save to disk cache for future use
            save_album_art_to_cache(cache_path, image_buf, image_bytes);
        } else {
            LOG_error("Failed to load album art image: %s\n", IMG_GetError());
        }
    }

    free(image_buf);
    art_ctx.art_fetch_in_progress = false;
}
