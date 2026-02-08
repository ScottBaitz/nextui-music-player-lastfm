#include "settings.h"
#include "defines.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Settings file path (in shared userdata directory)
#define SETTINGS_FILE SHARED_USERDATA_PATH "/music-player/settings.cfg"
#define SETTINGS_DIR SHARED_USERDATA_PATH "/music-player"

// Valid screen off timeout values (in seconds)
// 0 means off (no auto screen off)
static const int screen_off_values[] = {60, 90, 120, 0};
#define SCREEN_OFF_VALUE_COUNT 4
#define DEFAULT_SCREEN_OFF_INDEX 0  // Default to 60s

// Current settings
static struct {
    int screen_off_timeout;  // seconds, 0 = off
    bool lyrics_enabled;     // true = show lyrics
} current_settings;

// Find index of current screen off value in the values array
static int get_screen_off_index(void) {
    for (int i = 0; i < SCREEN_OFF_VALUE_COUNT; i++) {
        if (screen_off_values[i] == current_settings.screen_off_timeout) {
            return i;
        }
    }
    return DEFAULT_SCREEN_OFF_INDEX;
}

void Settings_init(void) {
    // Set defaults
    current_settings.screen_off_timeout = screen_off_values[DEFAULT_SCREEN_OFF_INDEX];
    current_settings.lyrics_enabled = true;

    // Try to load from file
    FILE* f = fopen(SETTINGS_FILE, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        int value;
        if (sscanf(line, "screen_off_timeout=%d", &value) == 1) {
            // Validate the value
            for (int i = 0; i < SCREEN_OFF_VALUE_COUNT; i++) {
                if (screen_off_values[i] == value) {
                    current_settings.screen_off_timeout = value;
                    break;
                }
            }
        }
        if (sscanf(line, "lyrics_enabled=%d", &value) == 1) {
            current_settings.lyrics_enabled = (value != 0);
        }
    }
    fclose(f);
}

void Settings_quit(void) {
    Settings_save();
}

int Settings_getScreenOffTimeout(void) {
    return current_settings.screen_off_timeout;
}

void Settings_setScreenOffTimeout(int seconds) {
    // Validate the value
    for (int i = 0; i < SCREEN_OFF_VALUE_COUNT; i++) {
        if (screen_off_values[i] == seconds) {
            current_settings.screen_off_timeout = seconds;
            Settings_save();
            return;
        }
    }
    // Invalid value, ignore
}

void Settings_cycleScreenOffNext(void) {
    int index = get_screen_off_index();
    index = (index + 1) % SCREEN_OFF_VALUE_COUNT;
    current_settings.screen_off_timeout = screen_off_values[index];
    Settings_save();
}

void Settings_cycleScreenOffPrev(void) {
    int index = get_screen_off_index();
    index = (index - 1 + SCREEN_OFF_VALUE_COUNT) % SCREEN_OFF_VALUE_COUNT;
    current_settings.screen_off_timeout = screen_off_values[index];
    Settings_save();
}

const char* Settings_getScreenOffDisplayStr(void) {
    switch (current_settings.screen_off_timeout) {
        case 60:  return "60s";
        case 90:  return "90s";
        case 120: return "120s";
        case 0:   return "Off";
        default:  return "60s";
    }
}

void Settings_save(void) {
    // Ensure directory exists
    char mkdir_cmd[512];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", SETTINGS_DIR);
    system(mkdir_cmd);

    FILE* f = fopen(SETTINGS_FILE, "w");
    if (!f) return;

    fprintf(f, "screen_off_timeout=%d\n", current_settings.screen_off_timeout);
    fprintf(f, "lyrics_enabled=%d\n", current_settings.lyrics_enabled ? 1 : 0);
    fclose(f);
}

bool Settings_getLyricsEnabled(void) {
    return current_settings.lyrics_enabled;
}

void Settings_setLyricsEnabled(bool enabled) {
    current_settings.lyrics_enabled = enabled;
    Settings_save();
}

void Settings_toggleLyrics(void) {
    current_settings.lyrics_enabled = !current_settings.lyrics_enabled;
    Settings_save();
}
