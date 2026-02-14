/*
 * Rockbox-style Last.fm Scrobbler Log
 * 
 * Creates a .scrobbler.log file in the Audioscrobbler 1.1 format
 * that can be submitted to Last.fm using external tools like:
 * - QTScrobbler
 * - Universal Scrobbler (web)
 * - Various command-line tools
 *
 * Format specification:
 * #AUDIOSCROBBLER/1.1
 * #TZ/[UNKNOWN]
 * #CLIENT/<client>/<version>
 * <artist>\t<album>\t<title>\t<tracknumber>\t<duration>\t<rating>\t<timestamp>\t<mbid>
 *
 * Rating: L = Listened/Loved, S = Skipped
 * Duration: in seconds
 * Timestamp: Unix epoch (seconds since 1970-01-01)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#include "defines.h"
#include "scrobbler.h"
#include "player.h"
#include "settings.h"

// Scrobbler log file location (standard Rockbox location on SD card root)
#define SCROBBLER_LOG_PATH SDCARD_PATH "/.scrobbler.log"

// Client identifier for the log file header
#define SCROBBLER_CLIENT "NextUI Music Player"
#define SCROBBLER_VERSION "1.0"

// Minimum percentage of track that must be played to scrobble (Last.fm standard)
#define SCROBBLE_MIN_PERCENT 50

// Minimum track length in seconds to be scrobbled (Last.fm requires 30s)
#define SCROBBLE_MIN_LENGTH 30

// Current track state
typedef struct {
    char artist[256];
    char album[256];
    char title[256];
    char filepath[512];
    int duration_sec;       // Track duration in seconds
    time_t start_time;      // Unix timestamp when playback started
    int start_position_ms;  // Position in track when started (for resume)
    bool active;            // Whether we're tracking a song
} ScrobblerTrack;

static ScrobblerTrack current_track = {0};
static bool initialized = false;

// Ensure the log file has the proper header
static void ensure_log_header(void) {
    FILE* f = fopen(SCROBBLER_LOG_PATH, "r");
    if (f) {
        // File exists, check if it has the header
        char line[64];
        if (fgets(line, sizeof(line), f)) {
            fclose(f);
            if (strncmp(line, "#AUDIOSCROBBLER", 15) == 0) {
                // Header exists, we're good
                return;
            }
        } else {
            fclose(f);
        }
        // File exists but no header, need to prepend
        // For simplicity, we'll just append header (existing entries still valid)
    }
    
    // Create or append header
    f = fopen(SCROBBLER_LOG_PATH, "a");
    if (f) {
        // Only write header if file is empty
        fseek(f, 0, SEEK_END);
        if (ftell(f) == 0) {
            fprintf(f, "#AUDIOSCROBBLER/1.1\n");
            fprintf(f, "#TZ/UNKNOWN\n");
            fprintf(f, "#CLIENT/%s/%s\n", SCROBBLER_CLIENT, SCROBBLER_VERSION);
        }
        fclose(f);
    }
}

// Escape special characters for log format (tabs and newlines)
static void escape_field(const char* src, char* dest, size_t dest_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dest_size - 1; i++) {
        if (src[i] == '\t') {
            // Replace tabs with spaces
            dest[j++] = ' ';
        } else if (src[i] == '\n' || src[i] == '\r') {
            // Skip newlines
            continue;
        } else {
            dest[j++] = src[i];
        }
    }
    dest[j] = '\0';
}

void Scrobbler_init(void) {
    if (initialized) return;
    
    memset(&current_track, 0, sizeof(current_track));
    ensure_log_header();
    initialized = true;
}

void Scrobbler_quit(void) {
    // If there's an active track, we could optionally scrobble it here
    // But typically we want explicit completion
    memset(&current_track, 0, sizeof(current_track));
    initialized = false;
}

void Scrobbler_trackStarted(const TrackInfo* info, const char* filepath) {
    if (!initialized || !info) return;
    
    // Clear previous track state
    memset(&current_track, 0, sizeof(current_track));
    
    // Copy track info
    snprintf(current_track.artist, sizeof(current_track.artist), "%s", 
             info->artist[0] ? info->artist : "Unknown Artist");
    snprintf(current_track.album, sizeof(current_track.album), "%s", 
             info->album[0] ? info->album : "Unknown Album");
    snprintf(current_track.title, sizeof(current_track.title), "%s", 
             info->title[0] ? info->title : "Unknown Title");
    snprintf(current_track.filepath, sizeof(current_track.filepath), "%s",
             filepath ? filepath : "");
    
    current_track.duration_sec = info->duration_ms / 1000;
    current_track.start_time = time(NULL);
    current_track.start_position_ms = 0;
    current_track.active = true;
}

void Scrobbler_trackCompleted(void) {
    if (!initialized || !current_track.active) return;
    
    // Check if scrobbling is enabled in settings
    if (!Settings_getScrobblingEnabled()) {
        current_track.active = false;
        return;
    }
    
    // Check minimum track length (Last.fm requires 30 seconds)
    if (current_track.duration_sec < SCROBBLE_MIN_LENGTH) {
        current_track.active = false;
        return;
    }
    
    // Calculate how long the track was played
    time_t now = time(NULL);
    int played_sec = (int)(now - current_track.start_time);
    
    // Check if played for at least 50% of track (or 4 minutes, whichever is less)
    // This is the Last.fm standard: 50% or 4 minutes
    int min_play_time = current_track.duration_sec * SCROBBLE_MIN_PERCENT / 100;
    if (min_play_time > 240) min_play_time = 240;  // 4 minutes cap
    
    if (played_sec < min_play_time) {
        // Not played long enough, don't scrobble
        current_track.active = false;
        return;
    }
    
    // Write scrobble entry
    FILE* f = fopen(SCROBBLER_LOG_PATH, "a");
    if (f) {
        char artist_esc[256], album_esc[256], title_esc[256];
        escape_field(current_track.artist, artist_esc, sizeof(artist_esc));
        escape_field(current_track.album, album_esc, sizeof(album_esc));
        escape_field(current_track.title, title_esc, sizeof(title_esc));
        
        // Format: artist\talbum\ttitle\ttrack#\tduration\trating\ttimestamp\tmbid
        // Track number is empty (not available in metadata)
        // Rating is 'L' for listened
        // MBID is empty (not available)
        fprintf(f, "%s\t%s\t%s\t\t%d\tL\t%ld\t\n",
                artist_esc,
                album_esc,
                title_esc,
                current_track.duration_sec,
                (long)current_track.start_time);
        fclose(f);
    }
    
    current_track.active = false;
}

void Scrobbler_trackSkipped(void) {
    if (!initialized) return;
    
    // Simply mark the track as inactive without scrobbling
    current_track.active = false;
}

bool Scrobbler_hasPendingScrobbles(void) {
    FILE* f = fopen(SCROBBLER_LOG_PATH, "r");
    if (!f) return false;
    
    // Count non-header lines
    int count = 0;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] != '#' && line[0] != '\n' && line[0] != '\0') {
            count++;
        }
    }
    fclose(f);
    
    return count > 0;
}

const char* Scrobbler_getLogPath(void) {
    return SCROBBLER_LOG_PATH;
}
