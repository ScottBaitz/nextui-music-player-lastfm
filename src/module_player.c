#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include "defines.h"
#include "api.h"
#include "config.h"
#include "settings.h"
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

// Screen off hint duration
#define SCREEN_OFF_HINT_DURATION_MS 4000

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
static bool screen_off_hint_active = false;
static uint32_t screen_off_hint_start = 0;
static time_t screen_off_hint_start_wallclock = 0;
static uint32_t last_input_time = 0;

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

// Handle next track logic
static bool handle_track_ended(void) {
    bool found_next = false;

    if (repeat_enabled) {
        if (playlist_active) {
            const PlaylistTrack* track = Playlist_getCurrentTrack(&playlist);
            if (track && Player_load(track->path) == 0) {
                Player_play();
                found_next = true;
            }
        } else if (Player_load(browser.entries[browser.selected].path) == 0) {
            Player_play();
            found_next = true;
        }
    } else if (shuffle_enabled) {
        if (playlist_active) {
            int new_idx = Playlist_shuffle(&playlist);
            if (new_idx >= 0) {
                const PlaylistTrack* track = Playlist_getTrack(&playlist, new_idx);
                if (track && Player_load(track->path) == 0) {
                    Player_play();
                    found_next = true;
                }
            }
        } else {
            int audio_count = Browser_countAudioFiles(&browser);
            if (audio_count > 1) {
                int random_idx = rand() % audio_count;
                int count = 0;
                for (int i = 0; i < browser.entry_count; i++) {
                    if (!browser.entries[i].is_dir) {
                        if (count == random_idx && i != browser.selected) {
                            browser.selected = i;
                            if (Player_load(browser.entries[i].path) == 0) {
                                Player_play();
                                found_next = true;
                            }
                            break;
                        }
                        count++;
                    }
                }
            }
        }
    } else {
        if (playlist_active) {
            int new_idx = Playlist_next(&playlist);
            if (new_idx >= 0) {
                const PlaylistTrack* track = Playlist_getTrack(&playlist, new_idx);
                if (track && Player_load(track->path) == 0) {
                    Player_play();
                    found_next = true;
                }
            }
        } else {
            for (int i = browser.selected + 1; i < browser.entry_count; i++) {
                if (!browser.entries[i].is_dir) {
                    browser.selected = i;
                    if (Player_load(browser.entries[i].path) == 0) {
                        Player_play();
                        found_next = true;
                    }
                    break;
                }
            }
        }
    }

    return found_next;
}

// Clean up playback state
static void cleanup_playback(void) {
    GFX_clearLayers(LAYER_SCROLLTEXT);
    PLAT_clearLayers(LAYER_SPECTRUM);
    PLAT_clearLayers(LAYER_PLAYTIME);
    PLAT_GPU_Flip();
    PlayTime_clear();
    Playlist_free(&playlist);
    playlist_active = false;
    ModuleCommon_setAutosleepDisabled(false);
}

// Render delete confirmation dialog
static void render_delete_dialog(SDL_Surface* screen) {
    GFX_clear(screen);
    render_delete_confirm(screen, delete_target_name);
    GFX_flip(screen);
}

ModuleExitReason PlayerModule_run(SDL_Surface* screen) {
    init_player();
    load_directory(browser.current_path[0] ? browser.current_path : MUSIC_PATH);

    PlayerInternalState state = PLAYER_INTERNAL_BROWSER;
    int dirty = 1;
    int show_setting = 0;

    screen_off = false;
    screen_off_hint_active = false;
    last_input_time = SDL_GetTicks();

    while (1) {
        uint32_t frame_start = SDL_GetTicks();
        PAD_poll();

        // Handle delete confirmation dialog (module-specific)
        if (show_delete_confirm) {
            if (PAD_justPressed(BTN_A)) {
                // Confirm delete
                if (delete_target_path[0]) {
                    if (unlink(delete_target_path) == 0) {
                        load_directory(browser.current_path);
                        if (browser.selected >= browser.entry_count) {
                            browser.selected = browser.entry_count - 1;
                            if (browser.selected < 0) browser.selected = 0;
                        }
                    }
                }
                delete_target_path[0] = '\0';
                delete_target_name[0] = '\0';
                show_delete_confirm = false;
                dirty = 1;
                continue;
            }
            else if (PAD_justPressed(BTN_B)) {
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
        if (!screen_off && !screen_off_hint_active) {
            int app_state_for_help = (state == PLAYER_INTERNAL_BROWSER) ? 1 : 2;  // STATE_BROWSER=1, STATE_PLAYING=2
            GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, app_state_for_help);
            if (global.should_quit) {
                cleanup_playback();
                Spectrum_quit();
                Browser_freeEntries(&browser);
                return MODULE_EXIT_QUIT;
            }
            if (global.input_consumed) {
                if (global.dirty) dirty = 1;
                GFX_sync();
                continue;
            }
        }

        // =========================================
        // BROWSER STATE
        // =========================================
        if (state == PLAYER_INTERNAL_BROWSER) {
            if (PAD_justRepeated(BTN_UP) && browser.entry_count > 0) {
                browser.selected = (browser.selected > 0) ? browser.selected - 1 : browser.entry_count - 1;
                dirty = 1;
            }
            else if (PAD_justRepeated(BTN_DOWN) && browser.entry_count > 0) {
                browser.selected = (browser.selected < browser.entry_count - 1) ? browser.selected + 1 : 0;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A) && browser.entry_count > 0) {
                FileEntry* entry = &browser.entries[browser.selected];
                if (entry->is_dir) {
                    char path_copy[512];
                    strncpy(path_copy, entry->path, sizeof(path_copy) - 1);
                    path_copy[sizeof(path_copy) - 1] = '\0';
                    load_directory(path_copy);
                    dirty = 1;
                } else if (entry->is_play_all) {
                    Playlist_free(&playlist);
                    int track_count = Playlist_buildFromDirectory(&playlist, entry->path, "");
                    if (track_count > 0) {
                        playlist_active = true;
                        const PlaylistTrack* track = Playlist_getCurrentTrack(&playlist);
                        if (track && Player_load(track->path) == 0) {
                            Player_play();
                            Spectrum_init();
                            last_input_time = SDL_GetTicks();
                            state = PLAYER_INTERNAL_PLAYING;
                            dirty = 1;
                        }
                    }
                } else {
                    Playlist_free(&playlist);
                    int track_count = Playlist_buildFromDirectory(&playlist, browser.current_path, entry->path);
                    if (track_count > 0) {
                        playlist_active = true;
                        const PlaylistTrack* track = Playlist_getCurrentTrack(&playlist);
                        if (track && Player_load(track->path) == 0) {
                            Player_play();
                            Spectrum_init();
                            last_input_time = SDL_GetTicks();
                            state = PLAYER_INTERNAL_PLAYING;
                            dirty = 1;
                        }
                    } else {
                        playlist_active = false;
                        if (Player_load(entry->path) == 0) {
                            Player_play();
                            Spectrum_init();
                            last_input_time = SDL_GetTicks();
                            state = PLAYER_INTERNAL_PLAYING;
                            dirty = 1;
                        }
                    }
                }
            }
            else if (PAD_justPressed(BTN_B)) {
                if (strcmp(browser.current_path, MUSIC_PATH) != 0) {
                    char* last_slash = strrchr(browser.current_path, '/');
                    if (last_slash) {
                        *last_slash = '\0';
                        load_directory(browser.current_path);
                        dirty = 1;
                    }
                } else {
                    GFX_clearLayers(LAYER_SCROLLTEXT);
                    Spectrum_quit();
                    Browser_freeEntries(&browser);
                    return MODULE_EXIT_TO_MENU;
                }
            }
            else if (PAD_justPressed(BTN_X) && browser.entry_count > 0) {
                FileEntry* entry = &browser.entries[browser.selected];
                if (!entry->is_dir && !entry->is_play_all) {
                    strncpy(delete_target_path, entry->path, sizeof(delete_target_path) - 1);
                    delete_target_path[sizeof(delete_target_path) - 1] = '\0';
                    strncpy(delete_target_name, entry->name, sizeof(delete_target_name) - 1);
                    delete_target_name[sizeof(delete_target_name) - 1] = '\0';
                    show_delete_confirm = true;
                    GFX_clearLayers(LAYER_SCROLLTEXT);
                    dirty = 1;
                }
            }

            // Animate browser scroll
            if (browser_needs_scroll_refresh()) {
                browser_animate_scroll();
            }
        }
        // =========================================
        // PLAYING STATE
        // =========================================
        else if (state == PLAYER_INTERNAL_PLAYING) {
            ModuleCommon_setAutosleepDisabled(true);

            // Handle screen off hint timeout
            if (screen_off_hint_active) {
                uint32_t now = SDL_GetTicks();
                time_t now_wallclock = time(NULL);
                bool timeout_sdl = (now - screen_off_hint_start >= SCREEN_OFF_HINT_DURATION_MS);
                bool timeout_wallclock = (now_wallclock - screen_off_hint_start_wallclock >= (SCREEN_OFF_HINT_DURATION_MS / 1000));
                if (timeout_sdl || timeout_wallclock) {
                    screen_off_hint_active = false;
                    screen_off = true;
                    GFX_clear(screen);
                    GFX_flip(screen);
                    PLAT_enableBacklight(0);
                }
                Player_update();
                GFX_sync();
                continue;
            }

            // Handle screen off mode
            if (screen_off) {
                // Wake screen with SELECT+A
                if (PAD_isPressed(BTN_SELECT) && PAD_isPressed(BTN_A)) {
                    screen_off = false;
                    PLAT_enableBacklight(1);
                    last_input_time = SDL_GetTicks();
                    dirty = 1;
                }
                // Handle USB/Bluetooth media buttons even with screen off
                USBHIDEvent hid_event;
                while ((hid_event = Player_pollUSBHID()) != USB_HID_EVENT_NONE) {
                    if (hid_event == USB_HID_EVENT_PLAY_PAUSE) {
                        Player_togglePause();
                    } else if (hid_event == USB_HID_EVENT_NEXT_TRACK) {
                        PlayerModule_nextTrack();
                    } else if (hid_event == USB_HID_EVENT_PREV_TRACK) {
                        PlayerModule_prevTrack();
                    }
                }
                Player_update();

                if (Player_getState() == PLAYER_STATE_STOPPED) {
                    bool found_next = handle_track_ended();
                    if (!found_next && Player_getState() == PLAYER_STATE_STOPPED) {
                        screen_off = false;
                        PLAT_enableBacklight(1);
                        cleanup_playback();
                        load_directory(MUSIC_PATH);
                        state = PLAYER_INTERNAL_BROWSER;
                        dirty = 1;
                    }
                }
                GFX_sync();
                continue;
            }

            // Normal input handling
            if (PAD_anyPressed()) {
                last_input_time = SDL_GetTicks();
            }

            if (PAD_justPressed(BTN_A)) {
                Player_togglePause();
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                Player_stop();
                cleanup_album_art_background();
                cleanup_playback();
                Spectrum_quit();
                state = PLAYER_INTERNAL_BROWSER;
                dirty = 1;
                continue;  // Skip track-ended check to prevent auto-advance
            }
            else if (PAD_justRepeated(BTN_LEFT)) {
                int pos = Player_getPosition();
                Player_seek(pos - 5000);
                dirty = 1;
            }
            else if (PAD_justRepeated(BTN_RIGHT)) {
                int pos = Player_getPosition();
                Player_seek(pos + 5000);
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_DOWN) || PAD_justPressed(BTN_L1)) {
                // Previous track
                if (playlist_active) {
                    int new_idx = Playlist_prev(&playlist);
                    if (new_idx >= 0) {
                        Player_stop();
                        const PlaylistTrack* track = Playlist_getTrack(&playlist, new_idx);
                        if (track && Player_load(track->path) == 0) {
                            Player_play();
                        }
                        dirty = 1;
                    }
                } else {
                    for (int i = browser.selected - 1; i >= 0; i--) {
                        if (!browser.entries[i].is_dir) {
                            Player_stop();
                            browser.selected = i;
                            if (Player_load(browser.entries[i].path) == 0) {
                                Player_play();
                            }
                            dirty = 1;
                            break;
                        }
                    }
                }
            }
            else if (PAD_justPressed(BTN_UP) || PAD_justPressed(BTN_R1)) {
                // Next track
                if (playlist_active) {
                    int new_idx = Playlist_next(&playlist);
                    if (new_idx >= 0) {
                        Player_stop();
                        const PlaylistTrack* track = Playlist_getTrack(&playlist, new_idx);
                        if (track && Player_load(track->path) == 0) {
                            Player_play();
                        }
                        dirty = 1;
                    }
                } else {
                    for (int i = browser.selected + 1; i < browser.entry_count; i++) {
                        if (!browser.entries[i].is_dir) {
                            Player_stop();
                            browser.selected = i;
                            if (Player_load(browser.entries[i].path) == 0) {
                                Player_play();
                            }
                            dirty = 1;
                            break;
                        }
                    }
                }
            }
            else if (PAD_justPressed(BTN_X)) {
                shuffle_enabled = !shuffle_enabled;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_Y)) {
                repeat_enabled = !repeat_enabled;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_L3) || PAD_justPressed(BTN_L2)) {
                Spectrum_toggleVisibility();
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_R3) || PAD_justPressed(BTN_R2)) {
                Spectrum_cycleStyle();
                dirty = 1;
            }
            else if (PAD_tappedSelect(SDL_GetTicks())) {
                screen_off_hint_active = true;
                screen_off_hint_start = SDL_GetTicks();
                screen_off_hint_start_wallclock = time(NULL);
                GFX_clearLayers(LAYER_SCROLLTEXT);
                PLAT_clearLayers(LAYER_SPECTRUM);
                PLAT_clearLayers(LAYER_PLAYTIME);
                PLAT_GPU_Flip();
                dirty = 1;
            }

            // Check if track ended
            Player_update();
            if (Player_getState() == PLAYER_STATE_STOPPED) {
                bool found_next = handle_track_ended();
                if (!found_next && Player_getState() == PLAYER_STATE_STOPPED) {
                    cleanup_playback();
                    load_directory(MUSIC_PATH);
                    state = PLAYER_INTERNAL_BROWSER;
                }
                dirty = 1;
            }

            // Auto screen-off after inactivity
            if (Player_getState() == PLAYER_STATE_PLAYING && !screen_off_hint_active) {
                uint32_t screen_timeout_ms = Settings_getScreenOffTimeout() * 1000;
                if (screen_timeout_ms > 0 && last_input_time > 0) {
                    uint32_t now = SDL_GetTicks();
                    if (now - last_input_time >= screen_timeout_ms) {
                        screen_off_hint_active = true;
                        screen_off_hint_start = SDL_GetTicks();
                        screen_off_hint_start_wallclock = time(NULL);
                        GFX_clearLayers(LAYER_SCROLLTEXT);
                        PLAT_clearLayers(LAYER_SPECTRUM);
                        PLAT_clearLayers(LAYER_PLAYTIME);
                        PLAT_GPU_Flip();
                        dirty = 1;
                    }
                }
            }

            // Animate player GPU layers
            if (!screen_off && !screen_off_hint_active) {
                if (player_needs_scroll_refresh()) {
                    player_animate_scroll();
                }
                if (Spectrum_needsRefresh()) {
                    Spectrum_renderGPU();
                }
                if (PlayTime_needsRefresh()) {
                    PlayTime_renderGPU();
                }
            }
        }

        // Handle power management
        if (!screen_off && !screen_off_hint_active) {
            PWR_update(&dirty, &show_setting, NULL, NULL);
        }

        // Render
        if (dirty && !screen_off) {
            if (screen_off_hint_active) {
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
            const PlaylistTrack* track = Playlist_getTrack(&playlist, new_idx);
            if (track && Player_load(track->path) == 0) {
                Player_play();
            }
        }
    } else {
        for (int i = browser.selected + 1; i < browser.entry_count; i++) {
            if (!browser.entries[i].is_dir) {
                Player_stop();
                browser.selected = i;
                if (Player_load(browser.entries[i].path) == 0) {
                    Player_play();
                }
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
            const PlaylistTrack* track = Playlist_getTrack(&playlist, new_idx);
            if (track && Player_load(track->path) == 0) {
                Player_play();
            }
        }
    } else {
        for (int i = browser.selected - 1; i >= 0; i--) {
            if (!browser.entries[i].is_dir) {
                Player_stop();
                browser.selected = i;
                if (Player_load(browser.entries[i].path) == 0) {
                    Player_play();
                }
                break;
            }
        }
    }
}
