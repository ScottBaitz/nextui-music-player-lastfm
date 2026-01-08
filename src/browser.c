#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "defines.h"
#include "api.h"
#include "browser.h"

// Check if file is a supported audio format
bool Browser_isAudioFile(const char* filename) {
    AudioFormat fmt = Player_detectFormat(filename);
    return fmt != AUDIO_FORMAT_UNKNOWN;
}

// Free browser entries
void Browser_freeEntries(BrowserContext* ctx) {
    if (ctx->entries) {
        free(ctx->entries);
        ctx->entries = NULL;
    }
    ctx->entry_count = 0;
}

// Compare function for sorting entries (directories first, then alphabetical)
static int compare_entries(const void* a, const void* b) {
    const FileEntry* ea = (const FileEntry*)a;
    const FileEntry* eb = (const FileEntry*)b;

    // Directories come first
    if (ea->is_dir && !eb->is_dir) return -1;
    if (!ea->is_dir && eb->is_dir) return 1;

    // Alphabetical
    return strcasecmp(ea->name, eb->name);
}

// Load directory contents
void Browser_loadDirectory(BrowserContext* ctx, const char* path, const char* music_root) {
    Browser_freeEntries(ctx);

    strncpy(ctx->current_path, path, sizeof(ctx->current_path) - 1);
    ctx->selected = 0;
    ctx->scroll_offset = 0;

    // Create music folder if it doesn't exist
    if (strcmp(path, music_root) == 0) {
        mkdir(path, 0755);
    }

    DIR* dir = opendir(path);
    if (!dir) {
        LOG_error("Failed to open directory: %s\n", path);
        return;
    }

    // First pass: count entries
    int count = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;  // Skip hidden files

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, ent->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode) || Browser_isAudioFile(ent->d_name)) {
            count++;
        }
    }

    // Add parent directory entry if not at root
    bool has_parent = (strcmp(path, music_root) != 0);
    if (has_parent) count++;

    // Allocate
    ctx->entries = malloc(sizeof(FileEntry) * count);
    if (!ctx->entries) {
        closedir(dir);
        return;
    }

    int idx = 0;

    // Add parent directory
    if (has_parent) {
        strcpy(ctx->entries[idx].name, "..");
        char* last_slash = strrchr(ctx->current_path, '/');
        if (last_slash) {
            strncpy(ctx->entries[idx].path, ctx->current_path, last_slash - ctx->current_path);
            ctx->entries[idx].path[last_slash - ctx->current_path] = '\0';
        } else {
            strncpy(ctx->entries[idx].path, music_root, sizeof(ctx->entries[idx].path) - 1);
        }
        ctx->entries[idx].is_dir = true;
        ctx->entries[idx].format = AUDIO_FORMAT_UNKNOWN;
        idx++;
    }

    // Second pass: fill entries
    rewinddir(dir);
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, ent->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        bool is_dir = S_ISDIR(st.st_mode);
        AudioFormat fmt = AUDIO_FORMAT_UNKNOWN;

        if (!is_dir) {
            fmt = Player_detectFormat(ent->d_name);
            if (fmt == AUDIO_FORMAT_UNKNOWN) continue;
        }

        strncpy(ctx->entries[idx].name, ent->d_name, sizeof(ctx->entries[idx].name) - 1);
        strncpy(ctx->entries[idx].path, full_path, sizeof(ctx->entries[idx].path) - 1);
        ctx->entries[idx].is_dir = is_dir;
        ctx->entries[idx].format = fmt;
        idx++;
    }

    closedir(dir);
    ctx->entry_count = idx;

    // Sort entries (but keep ".." at top if present)
    int sort_start = has_parent ? 1 : 0;
    if (ctx->entry_count > sort_start + 1) {
        qsort(&ctx->entries[sort_start], ctx->entry_count - sort_start,
              sizeof(FileEntry), compare_entries);
    }
}

// Get display name for file (without extension)
void Browser_getDisplayName(const char* filename, char* out, int max_len) {
    strncpy(out, filename, max_len - 1);
    out[max_len - 1] = '\0';

    // Remove extension for audio files
    char* dot = strrchr(out, '.');
    if (dot && dot != out) {
        *dot = '\0';
    }
}

// Count audio files in browser for "X OF Y" display
int Browser_countAudioFiles(const BrowserContext* ctx) {
    int count = 0;
    for (int i = 0; i < ctx->entry_count; i++) {
        if (!ctx->entries[i].is_dir) count++;
    }
    return count;
}

// Get current track number (1-based)
int Browser_getCurrentTrackNumber(const BrowserContext* ctx) {
    int num = 0;
    for (int i = 0; i <= ctx->selected && i < ctx->entry_count; i++) {
        if (!ctx->entries[i].is_dir) num++;
    }
    return num;
}
