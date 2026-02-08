#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "defines.h"
#include "api.h"
#include "config.h"
#include "module_common.h"
#include "module_player.h"
#include "player.h"
#include "spectrum.h"
#include "browser.h"
#include "playlist.h"
#include "ui_music.h"
#include "ui_album_art.h"
#include "ui_main.h"

// Music folder path
#define MUSIC_PATH SDCARD_PATH "/Music"

// Internal states
typedef enum {
    PLAYER_INTERNAL_BROWSER,
    PLAYER_INTERNAL_PLAYING
} PlayerInternalState;

// Module state
static BrowserContext browser = {0};
static bool shuffle_enabled = false;
static bool repeat_enabled = false;
static PlaylistContext playlist = {0};
static bool playlist_active = false;
static bool initialized = false;

// Delete confirmation state
static bool show_delete_confirm = false;
static char delete_target_path[512] = "";
static char delete_target_name[256] = "";

// Screen off state (module-local)
static bool screen_off = false;

// Clear all player GPU overlay layers
static void clear_gpu_layers(void) {
    GFX_clearLayers(LAYER_SCROLLTEXT);
    PLAT_clearLayers(LAYER_SPECTRUM);
    PLAT_clearLayers(LAYER_PLAYTIME);
    PLAT_GPU_Flip();
}

// Helper to load directory
static void load_directory(const char* path) {
    Browser_loadDirectory(&browser, path, MUSIC_PATH);
}

// Initialize player module
static void init_player(void) {
    if (initialized) return;
    mkdir(MUSIC_PATH, 0755);
    load_directory(MUSIC_PATH);
    initialized = true;
}

// Try to load and play a track, returns true on success
static bool try_load_and_play(const char *path) {
    if (Player_load(path) == 0) {
        Player_play();
        return true;
    }
    return false;
}

// Try to play a playlist track by index (-1 means current). Returns true on success.
static bool playlist_try_play(int idx) {
    const PlaylistTrack* track = (idx < 0)
        ? Playlist_getCurrentTrack(&playlist)
        : Playlist_getTrack(&playlist, idx);
    return track && try_load_and_play(track->path);
}

// Pick a random audio file from the browser (excluding current). Returns true on success.
static bool browser_pick_random(void) {
    int audio_count = Browser_countAudioFiles(&browser);
    if (audio_count <= 1) return false;

    int random_idx = rand() % (audio_count - 1);
    int count = 0;
    for (int i = 0; i < browser.entry_count; i++) {
        if (!browser.entries[i].is_dir && i != browser.selected) {
            if (count == random_idx) {
                browser.selected = i;
                return try_load_and_play(browser.entries[i].path);
            }
            count++;
        }
    }
    return false;
}

// Pick the next audio file in the browser after current. Returns true on success.
static bool browser_pick_next(void) {
    for (int i = browser.selected + 1; i < browser.entry_count; i++) {
        if (!browser.entries[i].is_dir) {
            browser.selected = i;
            return try_load_and_play(browser.entries[i].path);
        }
    }
    return false;
}

// Handle next track logic
static bool handle_track_ended(void) {
    if (repeat_enabled) {
        if (playlist_active) return playlist_try_play(-1);
        return try_load_and_play(browser.entries[browser.selected].path);
    }

    if (shuffle_enabled) {
        if (playlist_active) return playlist_try_play(Playlist_shuffle(&playlist));
        return browser_pick_random();
    }

    if (playlist_active) return playlist_try_play(Playlist_next(&playlist));
    return browser_pick_next();
}

// Start playback of a track (load + play + init spectrum)
static bool start_playback(const char* path) {
    if (try_load_and_play(path)) {
        Spectrum_init();
        ModuleCommon_recordInputTime();
        ModuleCommon_setAutosleepDisabled(true);
        return true;
    }
    return false;
}

// Clean up playback state
static void cleanup_playback(bool quit_spectrum) {
    clear_gpu_layers();
    PlayTime_clear();
    if (quit_spectrum) {
        Spectrum_quit();
    }
    Playlist_free(&playlist);
    playlist_active = false;
    ModuleCommon_setAutosleepDisabled(false);
}

// Build a playlist from a directory and start playing the first track
static bool build_and_start_playlist(const char* dir_path, const char* start_file) {
    Playlist_free(&playlist);
    int track_count = Playlist_buildFromDirectory(&playlist, dir_path, start_file);
    if (track_count > 0) {
        playlist_active = true;
        const PlaylistTrack* track = Playlist_getCurrentTrack(&playlist);
        if (track && start_playback(track->path)) {
            return true;
        }
    }
    return false;
}

// Render delete confirmation dialog
static void render_delete_dialog(SDL_Surface* screen) {
    GFX_clear(screen);
    render_delete_confirm(screen, delete_target_name);
    GFX_flip(screen);
}

// Handle USB/Bluetooth media button events
static void handle_hid_events(void) {
    USBHIDEvent hid_event;
    while ((hid_event = Player_pollUSBHID()) != USB_HID_EVENT_NONE) {
        if (hid_event == USB_HID_EVENT_PLAY_PAUSE) {
            Player_togglePause();
        } else if (hid_event == USB_HID_EVENT_NEXT_TRACK) {
            PlayerModule_nextTrack();
        } else if (hid_event == USB_HID_EVENT_PREV_TRACK) {
            PlayerModule_prevTrack();
        } else {
            ModuleCommon_handleHIDVolume(hid_event);
        }
    }
}

// Try to start playback from a browser entry (play-all or single file). Returns true on success.
static bool browser_play_entry(FileEntry *entry) {
    if (entry->is_play_all)
        return build_and_start_playlist(entry->path, "");
    if (build_and_start_playlist(browser.current_path, entry->path))
        return true;
    playlist_active = false;
    return start_playback(entry->path);
}

// Handle input in browser state. Returns true if module should exit to menu.
static bool handle_browser_input(PlayerInternalState *state, int *dirty) {
    if (PAD_justPressed(BTN_B)) {
        if (strcmp(browser.current_path, MUSIC_PATH) != 0) {
            char* last_slash = strrchr(browser.current_path, '/');
            if (last_slash) {
                *last_slash = '\0';
                load_directory(browser.current_path);
                *dirty = 1;
            }
        } else {
            GFX_clearLayers(LAYER_SCROLLTEXT);
            Spectrum_quit();
            Browser_freeEntries(&browser);
            return true;
        }
    }
    else if (browser.entry_count > 0) {
        if (PAD_justRepeated(BTN_UP)) {
            browser.selected = (browser.selected > 0) ? browser.selected - 1 : browser.entry_count - 1;
            *dirty = 1;
        }
        else if (PAD_justRepeated(BTN_DOWN)) {
            browser.selected = (browser.selected < browser.entry_count - 1) ? browser.selected + 1 : 0;
            *dirty = 1;
        }
        else if (PAD_justPressed(BTN_A)) {
            FileEntry* entry = &browser.entries[browser.selected];
            if (entry->is_dir) {
                char path_copy[512];
                snprintf(path_copy, sizeof(path_copy), "%s", entry->path);
                load_directory(path_copy);
                *dirty = 1;
            } else if (browser_play_entry(entry)) {
                *state = PLAYER_INTERNAL_PLAYING;
                *dirty = 1;
            }
        }
        else if (PAD_justPressed(BTN_X)) {
            FileEntry* entry = &browser.entries[browser.selected];
            if (!entry->is_dir && !entry->is_play_all) {
                snprintf(delete_target_path, sizeof(delete_target_path), "%s", entry->path);
                snprintf(delete_target_name, sizeof(delete_target_name), "%s", entry->name);
                show_delete_confirm = true;
                GFX_clearLayers(LAYER_SCROLLTEXT);
                *dirty = 1;
            }
        }
    }

    // Animate browser scroll
    if (browser_needs_scroll_refresh()) {
        browser_animate_scroll();
    }
    if (browser_scroll_needs_render()) *dirty = 1;

    return false;
}

// Handle input in playing state. Returns true when main loop should continue (skip render).
static bool handle_playing_input(SDL_Surface *screen, PlayerInternalState *state, int *dirty) {
    // Handle screen off hint timeout
    if (ModuleCommon_isScreenOffHintActive()) {
        if (ModuleCommon_processScreenOffHintTimeout()) {
            screen_off = true;
            GFX_clear(screen);
            GFX_flip(screen);
        }
        Player_update();
        GFX_sync();
        return true;
    }

    // Handle screen off mode
    if (screen_off) {
        // Wake screen with SELECT+A
        if (PAD_isPressed(BTN_SELECT) && PAD_isPressed(BTN_A)) {
            screen_off = false;
            PLAT_enableBacklight(1);
            ModuleCommon_recordInputTime();
            *dirty = 1;
        }
        // Handle USB/Bluetooth media and volume buttons even with screen off
        handle_hid_events();
        ModuleCommon_handleHardwareVolume();
        Player_update();

        if (Player_getState() == PLAYER_STATE_STOPPED) {
            if (!handle_track_ended() && Player_getState() == PLAYER_STATE_STOPPED) {
                screen_off = false;
                PLAT_enableBacklight(1);
                cleanup_playback(false);
                load_directory(MUSIC_PATH);
                *state = PLAYER_INTERNAL_BROWSER;
                *dirty = 1;
            }
        }
        GFX_sync();
        return true;
    }

    // Normal input handling
    if (PAD_anyPressed()) {
        ModuleCommon_recordInputTime();
    }

    if (PAD_justPressed(BTN_A)) {
        Player_togglePause();
        *dirty = 1;
    }
    else if (PAD_justPressed(BTN_B)) {
        Player_stop();
        cleanup_album_art_background();
        cleanup_playback(true);
        *state = PLAYER_INTERNAL_BROWSER;
        *dirty = 1;
        return true;  // Skip track-ended check to prevent auto-advance
    }
    else if (PAD_justRepeated(BTN_LEFT)) {
        Player_seek(Player_getPosition() - 5000);
        *dirty = 1;
    }
    else if (PAD_justRepeated(BTN_RIGHT)) {
        Player_seek(Player_getPosition() + 5000);
        *dirty = 1;
    }
    else if (PAD_justPressed(BTN_DOWN) || PAD_justPressed(BTN_L1)) {
        PlayerModule_prevTrack();
        *dirty = 1;
    }
    else if (PAD_justPressed(BTN_UP) || PAD_justPressed(BTN_R1)) {
        PlayerModule_nextTrack();
        *dirty = 1;
    }
    else if (PAD_justPressed(BTN_X)) {
        shuffle_enabled = !shuffle_enabled;
        *dirty = 1;
    }
    else if (PAD_justPressed(BTN_Y)) {
        repeat_enabled = !repeat_enabled;
        *dirty = 1;
    }
    else if (PAD_justPressed(BTN_L3) || PAD_justPressed(BTN_L2)) {
        Spectrum_toggleVisibility();
        *dirty = 1;
    }
    else if (PAD_justPressed(BTN_R3) || PAD_justPressed(BTN_R2)) {
        Spectrum_cycleStyle();
        *dirty = 1;
    }
    else if (PAD_tappedSelect(SDL_GetTicks())) {
        ModuleCommon_startScreenOffHint();
        clear_gpu_layers();
        *dirty = 1;
    }

    // Check if track ended
    Player_update();
    if (Player_getState() == PLAYER_STATE_STOPPED) {
        if (!handle_track_ended() && Player_getState() == PLAYER_STATE_STOPPED) {
            cleanup_playback(false);
            load_directory(MUSIC_PATH);
            *state = PLAYER_INTERNAL_BROWSER;
        }
        *dirty = 1;
    }

    // Auto screen-off after inactivity
    if (Player_getState() == PLAYER_STATE_PLAYING && ModuleCommon_checkAutoScreenOffTimeout()) {
        clear_gpu_layers();
        *dirty = 1;
    }

    // Animate player GPU layers (skip if screen-off hint just activated)
    if (!ModuleCommon_isScreenOffHintActive()) {
        if (player_needs_scroll_refresh()) {
            player_animate_scroll();
        }
        if (player_title_scroll_needs_render()) *dirty = 1;
        if (Spectrum_needsRefresh()) {
            Spectrum_renderGPU();
        }
        if (PlayTime_needsRefresh()) {
            PlayTime_renderGPU();
        }
    }

    return false;
}

ModuleExitReason PlayerModule_run(SDL_Surface* screen) {
    init_player();
    load_directory(browser.current_path[0] ? browser.current_path : MUSIC_PATH);

    PlayerInternalState state = PLAYER_INTERNAL_BROWSER;
    int dirty = 1;
    int show_setting = 0;

    screen_off = false;
    ModuleCommon_resetScreenOffHint();
    ModuleCommon_recordInputTime();

    while (1) {
        PAD_poll();

        // Handle delete confirmation dialog (module-specific)
        if (show_delete_confirm) {
            if (PAD_justPressed(BTN_A)) {
                if (unlink(delete_target_path) == 0) {
                    load_directory(browser.current_path);
                    if (browser.selected >= browser.entry_count) {
                        browser.selected = browser.entry_count > 0 ? browser.entry_count - 1 : 0;
                    }
                }
            }
            if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B)) {
                delete_target_path[0] = '\0';
                delete_target_name[0] = '\0';
                show_delete_confirm = false;
                dirty = 1;
                continue;
            }
            // Render delete dialog
            render_delete_dialog(screen);
            GFX_sync();
            continue;
        }

        // Handle global input (skip if screen off or hint active)
        if (!screen_off && !ModuleCommon_isScreenOffHintActive()) {
            int app_state_for_help = (state == PLAYER_INTERNAL_BROWSER) ? 1 : 2;  // STATE_BROWSER=1, STATE_PLAYING=2
            GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, app_state_for_help);
            if (global.should_quit) {
                cleanup_playback(true);
                Browser_freeEntries(&browser);
                return MODULE_EXIT_QUIT;
            }
            if (global.input_consumed) {
                if (global.dirty) dirty = 1;
                GFX_sync();
                continue;
            }
        }

        if (state == PLAYER_INTERNAL_BROWSER) {
            if (handle_browser_input(&state, &dirty)) {
                return MODULE_EXIT_TO_MENU;
            }
        }
        else if (state == PLAYER_INTERNAL_PLAYING) {
            if (handle_playing_input(screen, &state, &dirty)) {
                continue;
            }
        }

        // Handle power management
        if (!screen_off && !ModuleCommon_isScreenOffHintActive()) {
            ModuleCommon_PWR_update(&dirty, &show_setting);
        }

        // Render
        if (dirty && !screen_off) {
            if (ModuleCommon_isScreenOffHintActive()) {
                GFX_clear(screen);
                render_screen_off_hint(screen);
            } else if (state == PLAYER_INTERNAL_BROWSER) {
                render_browser(screen, show_setting, &browser);
            } else {
                int pl_track = playlist_active ? Playlist_getCurrentIndex(&playlist) + 1 : 0;
                int pl_total = playlist_active ? Playlist_getCount(&playlist) : 0;
                render_playing(screen, show_setting, &browser, shuffle_enabled, repeat_enabled, pl_track, pl_total);
            }

            if (show_setting) {
                GFX_blitHardwareHints(screen, show_setting);
            }

            GFX_flip(screen);
            dirty = 0;
        } else if (!screen_off) {
            GFX_sync();
        }
    }
}

// Check if music player module is active
bool PlayerModule_isActive(void) {
    PlayerState state = Player_getState();
    return (state == PLAYER_STATE_PLAYING || state == PLAYER_STATE_PAUSED);
}

// Play next track (for USB HID button support)
void PlayerModule_nextTrack(void) {
    if (!initialized) return;

    if (playlist_active) {
        int new_idx = Playlist_next(&playlist);
        if (new_idx >= 0) {
            Player_stop();
            playlist_try_play(new_idx);
        }
    } else {
        for (int i = browser.selected + 1; i < browser.entry_count; i++) {
            if (!browser.entries[i].is_dir) {
                Player_stop();
                browser.selected = i;
                try_load_and_play(browser.entries[i].path);
                break;
            }
        }
    }
}

// Play previous track (for USB HID button support)
void PlayerModule_prevTrack(void) {
    if (!initialized) return;

    if (playlist_active) {
        int new_idx = Playlist_prev(&playlist);
        if (new_idx >= 0) {
            Player_stop();
            playlist_try_play(new_idx);
        }
    } else {
        for (int i = browser.selected - 1; i >= 0; i--) {
            if (!browser.entries[i].is_dir) {
                Player_stop();
                browser.selected = i;
                try_load_and_play(browser.entries[i].path);
                break;
            }
        }
    }
}
