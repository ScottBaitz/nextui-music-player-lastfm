#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <math.h>
#include <time.h>
#include <msettings.h>

#include "defines.h"
#include "api.h"
#include "utils.h"
#include "config.h"
#include "player.h"
#include "spectrum.h"
#include "radio.h"
#include "radio_album_art.h"
#include "youtube.h"
#include "selfupdate.h"

// UI modules
#include "ui_fonts.h"
#include "ui_utils.h"
#include "browser.h"
#include "ui_album_art.h"
#include "ui_main.h"
#include "ui_music.h"
#include "ui_radio.h"
#include "ui_youtube.h"
#include "ui_system.h"
#include "ui_icons.h"
#include "playlist.h"

// App states
typedef enum {
    STATE_MENU = 0,         // Main menu (Files / Radio / YouTube / Settings)
    STATE_BROWSER,          // File browser
    STATE_PLAYING,          // Playing local file
    STATE_RADIO_LIST,       // Radio station list
    STATE_RADIO_PLAYING,    // Playing radio stream
    STATE_RADIO_ADD,        // Add stations - country selection
    STATE_RADIO_ADD_STATIONS, // Add stations - station selection
    STATE_RADIO_HELP,       // Help/instructions screen
    STATE_YOUTUBE_MENU,     // YouTube sub-menu
    STATE_YOUTUBE_SEARCHING, // Searching in progress
    STATE_YOUTUBE_RESULTS,  // YouTube search results
    STATE_YOUTUBE_QUEUE,    // Download queue view
    STATE_YOUTUBE_DOWNLOADING, // Downloading progress
    STATE_YOUTUBE_UPDATING, // yt-dlp update progress
    STATE_APP_UPDATING,     // App self-update progress
    STATE_ABOUT             // About screen
} AppState;

// FileEntry and BrowserContext are now in browser.h

// Menu item count (menu_items moved to ui_main.c)
#define MENU_ITEM_COUNT 4

// YouTube menu count (youtube_menu_items moved to ui_youtube.c)
#define YOUTUBE_MENU_COUNT 3

// YouTube state
static int youtube_menu_selected = 0;
static int youtube_results_selected = 0;
static int youtube_results_scroll = 0;
static int youtube_queue_selected = 0;
static int youtube_queue_scroll = 0;
static YouTubeResult youtube_results[YOUTUBE_MAX_RESULTS];
static int youtube_result_count = 0;
static bool youtube_searching = false;
static char youtube_search_query[256] = "";
static char youtube_toast_message[128] = "";
static uint32_t youtube_toast_time = 0;
#define YOUTUBE_TOAST_DURATION 3000  // 3 seconds

// Global state
static bool quit = false;
static AppState app_state = STATE_MENU;
static SDL_Surface* screen;
static BrowserContext browser = {0};
static int menu_selected = 0;
static int radio_selected = 0;
static int radio_scroll = 0;
static char radio_toast_message[128] = "";
static uint32_t radio_toast_time = 0;
#define RADIO_TOAST_DURATION 3000  // 3 seconds

// Add stations UI state
static int add_country_selected = 0;
static int add_country_scroll = 0;
static int add_station_selected = 0;
static int add_station_scroll = 0;
static const char* add_selected_country_code = NULL;
static bool add_station_checked[256];  // Track selected stations for adding
static int help_scroll = 0;  // Scroll position for help page

// Screen off mode (screen off but audio keeps playing)
static bool screen_off = false;
static bool autosleep_disabled = false;
static uint32_t last_input_time = 0;  // For auto screen-off after inactivity

// Screen off hint (shows wake instructions before turning off)
static bool screen_off_hint_active = false;
static uint32_t screen_off_hint_start = 0;
static time_t screen_off_hint_start_wallclock = 0;  // Wall-clock time for detecting device sleep
#define SCREEN_OFF_HINT_DURATION_MS 4000  // Show hint for 4 seconds

// Quit confirmation dialog
static bool show_quit_confirm = false;

// Controls help dialog
static bool show_controls_help = false;

// Delete confirmation dialog
static bool show_delete_confirm = false;
static char delete_target_path[512] = "";
static char delete_target_name[256] = "";

// START button long press detection
static uint32_t start_press_time = 0;
static bool start_was_pressed = false;
#define START_LONG_PRESS_MS 500  // Long press threshold

// Shuffle and repeat modes
static bool shuffle_enabled = false;
static bool repeat_enabled = false;

// Playlist context for recursive playback
static PlaylistContext playlist = {0};
static bool playlist_active = false;

// Music folder
#define MUSIC_PATH SDCARD_PATH "/Music"

static void sigHandler(int sig) {
    switch (sig) {
    case SIGINT:
    case SIGTERM:
        quit = true;
        break;
    default:
        break;
    }
}

// Helper function to load directory using Browser module
static void load_directory(const char* path) {
    Browser_loadDirectory(&browser, path, MUSIC_PATH);
}

// Render functions are now in UI modules (ui_music.h, ui_radio.h, ui_youtube.h, ui_system.h)
// See: ui_music.c, ui_radio.c, ui_youtube.c, ui_system.c

int main(int argc, char* argv[]) {
    InitSettings();
    screen = GFX_init(MODE_MAIN);
    PAD_init();
    PWR_init();
    WIFI_init();

    // Load custom fonts (if available)
    load_custom_fonts();

    // Load icons (if available)
    Icons_init();

    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    // Seed random number generator for shuffle
    srand((unsigned int)time(NULL));

    // Initialize player and radio
    if (Player_init() != 0) {
        LOG_error("Failed to initialize audio player\n");
        goto cleanup;
    }
    // At startup, set software volume based on output device
    // For speaker/USB DAC: software volume at 1.0, system mixer controls volume
    // For Bluetooth: software volume controls (system mixer doesn't work well)
    if (Player_isBluetoothActive()) {
        Player_setVolume(GetVolume() / 20.0f);
    } else {
        Player_setVolume(1.0f);
    }

    Spectrum_init();
    Radio_init();
    YouTube_init();

    // Initialize self-update module (current directory is pak root)
    // Version is read from state/app_version.txt
    SelfUpdate_init(".");

    // Auto-check for updates on startup (non-blocking)
    SelfUpdate_checkForUpdate();

    // Create Music folder if it doesn't exist
    mkdir(MUSIC_PATH, 0755);

    // Load initial directory
    load_directory(MUSIC_PATH);

    int dirty = 1;
    int show_setting = 0;

    while (!quit) {
        uint32_t frame_start = SDL_GetTicks();
        PAD_poll();

        // Handle volume buttons - works in all states
        // System volume is 0-20, software volume is 0.0-1.0
        if (PAD_justRepeated(BTN_PLUS)) {
            int vol = GetVolume();
            vol = (vol < 20) ? vol + 1 : 20;
            if (Player_isBluetoothActive()) {
                // Bluetooth: use software volume only (system mixer doesn't work well)
                Player_setVolume(vol / 20.0f);
            } else {
                // Speaker/USB DAC: use system volume, keep software at 1.0
                SetVolume(vol);
                Player_setVolume(1.0f);
            }
        }
        else if (PAD_justRepeated(BTN_MINUS)) {
            int vol = GetVolume();
            vol = (vol > 0) ? vol - 1 : 0;
            if (Player_isBluetoothActive()) {
                // Bluetooth: use software volume only (system mixer doesn't work well)
                Player_setVolume(vol / 20.0f);
            } else {
                // Speaker/USB DAC: use system volume, keep software at 1.0
                SetVolume(vol);
                Player_setVolume(1.0f);
            }
        }

        // Handle quit confirmation dialog
        if (show_quit_confirm) {
            if (PAD_justPressed(BTN_A)) {
                // Confirm quit
                quit = true;
            }
            else if (PAD_justPressed(BTN_B) || PAD_justPressed(BTN_START)) {
                // Cancel quit
                show_quit_confirm = false;
                dirty = 1;
            }
            // Skip other input handling while dialog is shown
        }
        // Handle controls help dialog
        else if (show_controls_help) {
            if (PAD_justPressed(BTN_A) || PAD_justPressed(BTN_B) || PAD_justPressed(BTN_START)) {
                // Close controls help
                show_controls_help = false;
                dirty = 1;
            }
            // Skip other input handling while dialog is shown
        }
        // Handle delete confirmation dialog
        else if (show_delete_confirm) {
            if (PAD_justPressed(BTN_A)) {
                // Confirm delete - remove the file
                if (remove(delete_target_path) == 0) {
                    // File deleted successfully - save selection and reload directory
                    int saved_selected = browser.selected;
                    load_directory(browser.current_path);
                    // Restore selection, adjusted if at end of list
                    if (saved_selected >= browser.entry_count) {
                        browser.selected = browser.entry_count > 0 ? browser.entry_count - 1 : 0;
                    } else {
                        browser.selected = saved_selected;
                    }
                }
                show_delete_confirm = false;
                delete_target_path[0] = '\0';
                delete_target_name[0] = '\0';
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                // Cancel delete
                show_delete_confirm = false;
                delete_target_path[0] = '\0';
                delete_target_name[0] = '\0';
                dirty = 1;
            }
            // Skip other input handling while dialog is shown
        }
        // Handle START button - track press time for short/long press detection
        else if (PAD_justPressed(BTN_START)) {
            start_press_time = SDL_GetTicks();
            start_was_pressed = true;
        }
        else if (start_was_pressed && PAD_isPressed(BTN_START)) {
            // Check for long press threshold while button is held
            uint32_t hold_time = SDL_GetTicks() - start_press_time;
            if (hold_time >= START_LONG_PRESS_MS) {
                // Long press - show quit confirmation
                start_was_pressed = false;  // Reset to prevent re-trigger
                show_quit_confirm = true;
                // Clear all GPU layers so dialog is not obscured
                GFX_clearLayers(LAYER_SCROLLTEXT);
                PLAT_clearLayers(LAYER_SPECTRUM);
                PLAT_clearLayers(LAYER_PLAYTIME);
                PLAT_GPU_Flip();  // Apply layer clears
                PlayTime_clear();  // Reset playtime state
                dirty = 1;
            }
        }
        else if (start_was_pressed && PAD_justReleased(BTN_START)) {
            // Short press - show controls help
            start_was_pressed = false;
            show_controls_help = true;
            // Clear all GPU layers so dialog is not obscured
            GFX_clearLayers(LAYER_SCROLLTEXT);
            PLAT_clearLayers(LAYER_SPECTRUM);
            PLAT_clearLayers(LAYER_PLAYTIME);
            PLAT_GPU_Flip();  // Apply layer clears
            PlayTime_clear();  // Reset playtime state
            dirty = 1;
        }
        // Handle input based on state
        else if (app_state == STATE_MENU) {
            if (PAD_justRepeated(BTN_UP)) {
                menu_selected = (menu_selected > 0) ? menu_selected - 1 : MENU_ITEM_COUNT - 1;
                dirty = 1;
            }
            else if (PAD_justRepeated(BTN_DOWN)) {
                menu_selected = (menu_selected < MENU_ITEM_COUNT - 1) ? menu_selected + 1 : 0;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A)) {
                if (menu_selected == 0) {
                    // Music Files - reload directory to pick up new downloads
                    load_directory(browser.current_path[0] ? browser.current_path : MUSIC_PATH);
                    app_state = STATE_BROWSER;
                    dirty = 1;
                } else if (menu_selected == 1) {
                    // Internet Radio
                    app_state = STATE_RADIO_LIST;
                    dirty = 1;
                } else if (menu_selected == 2) {
                    // Music Downloader
                    if (YouTube_isAvailable()) {
                        app_state = STATE_YOUTUBE_MENU;
                        youtube_menu_selected = 0;
                        dirty = 1;
                    }
                } else if (menu_selected == 3) {
                    // About
                    app_state = STATE_ABOUT;
                    dirty = 1;
                }
            }
            else if (PAD_justPressed(BTN_B)) {
                quit = true;
            }
        }
        else if (app_state == STATE_BROWSER) {
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
                    // Copy path before load_directory frees browser.entries
                    char path_copy[512];
                    strncpy(path_copy, entry->path, sizeof(path_copy) - 1);
                    path_copy[sizeof(path_copy) - 1] = '\0';
                    load_directory(path_copy);
                    dirty = 1;
                } else if (entry->is_play_all) {
                    // "Play All" entry - build playlist from this folder recursively
                    Playlist_free(&playlist);
                    int track_count = Playlist_buildFromDirectory(&playlist, entry->path, "");
                    if (track_count > 0) {
                        playlist_active = true;
                        const PlaylistTrack* track = Playlist_getCurrentTrack(&playlist);
                        if (track && Player_load(track->path) == 0) {
                            Player_play();
                            app_state = STATE_PLAYING;
                            last_input_time = SDL_GetTicks();
                            dirty = 1;
                        }
                    }
                } else {
                    // Build playlist from current directory (recursively includes subfolders)
                    Playlist_free(&playlist);
                    int track_count = Playlist_buildFromDirectory(&playlist, browser.current_path, entry->path);
                    if (track_count > 0) {
                        playlist_active = true;
                        const PlaylistTrack* track = Playlist_getCurrentTrack(&playlist);
                        if (track && Player_load(track->path) == 0) {
                            Player_play();
                            app_state = STATE_PLAYING;
                            last_input_time = SDL_GetTicks();  // Start screen-off timer
                            dirty = 1;
                        }
                    } else {
                        // Fallback: just play the single file if playlist build failed
                        playlist_active = false;
                        if (Player_load(entry->path) == 0) {
                            Player_play();
                            app_state = STATE_PLAYING;
                            last_input_time = SDL_GetTicks();
                            dirty = 1;
                        }
                    }
                }
            }
            else if (PAD_justPressed(BTN_B)) {
                // Go up a directory or back to menu
                if (strcmp(browser.current_path, MUSIC_PATH) != 0) {
                    char* last_slash = strrchr(browser.current_path, '/');
                    if (last_slash) {
                        *last_slash = '\0';
                        load_directory(browser.current_path);
                        dirty = 1;
                    }
                } else {
                    GFX_clearLayers(LAYER_SCROLLTEXT);  // Clear scroll layer when leaving browser
                    app_state = STATE_MENU;
                    dirty = 1;
                }
            }
            else if (PAD_justPressed(BTN_X) && browser.entry_count > 0) {
                // Delete selected file (not directories or special entries)
                FileEntry* entry = &browser.entries[browser.selected];
                if (!entry->is_dir && !entry->is_play_all) {
                    // Store the file info for deletion
                    strncpy(delete_target_path, entry->path, sizeof(delete_target_path) - 1);
                    delete_target_path[sizeof(delete_target_path) - 1] = '\0';
                    strncpy(delete_target_name, entry->name, sizeof(delete_target_name) - 1);
                    delete_target_name[sizeof(delete_target_name) - 1] = '\0';
                    // Show delete confirmation dialog
                    show_delete_confirm = true;
                    GFX_clearLayers(LAYER_SCROLLTEXT);  // Clear scroll layer so dialog is not obscured
                    dirty = 1;
                }
            }

            // Animate scroll without full redraw (GPU mode)
            if (browser_needs_scroll_refresh()) {
                browser_animate_scroll();
            }
        }
        else if (app_state == STATE_PLAYING) {
            // Disable autosleep while playing
            if (!autosleep_disabled) {
                PWR_disableAutosleep();
                autosleep_disabled = true;
            }

            // Handle screen off hint timeout - turn off screen after hint displays
            if (screen_off_hint_active) {
                uint32_t now = SDL_GetTicks();
                time_t now_wallclock = time(NULL);
                // Check both SDL ticks and wall-clock time (wall-clock continues during device sleep)
                bool timeout_sdl = (now - screen_off_hint_start >= SCREEN_OFF_HINT_DURATION_MS);
                bool timeout_wallclock = (now_wallclock - screen_off_hint_start_wallclock >= (SCREEN_OFF_HINT_DURATION_MS / 1000));
                if (timeout_sdl || timeout_wallclock) {
                    screen_off_hint_active = false;
                    screen_off = true;
                    // Clear the framebuffer before turning off backlight
                    // This prevents the hint from showing if device wakes via power button
                    GFX_clear(screen);
                    GFX_flip(screen);
                    PLAT_enableBacklight(0);
                }
                // Still update player while hint is showing
                Player_update();
            }
            // Handle screen off mode - require SELECT + A to wake (prevents accidental wake in pocket)
            else if (screen_off) {
                if (PAD_isPressed(BTN_SELECT) && PAD_isPressed(BTN_A)) {
                    screen_off = false;
                    PLAT_enableBacklight(1);
                    last_input_time = SDL_GetTicks();  // Reset timer on wake
                    dirty = 1;
                }
                // Still update player and process audio while screen is off
                Player_update();

                // Check if track ended while screen off
                if (Player_getState() == PLAYER_STATE_STOPPED) {
                    bool found_next = false;

                    if (repeat_enabled) {
                        // Repeat current track
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
                        // Pick a random track
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
                        // Normal: advance to next track (no wrap-around)
                        if (playlist_active) {
                            int new_idx = Playlist_next(&playlist);
                            if (new_idx >= 0) {
                                const PlaylistTrack* track = Playlist_getTrack(&playlist, new_idx);
                                if (track && Player_load(track->path) == 0) {
                                    Player_play();
                                    found_next = true;
                                }
                            }
                            // If new_idx < 0, end of playlist - found_next stays false
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

                    // If no next track, wake screen and go back to root Music folder
                    if (!found_next && Player_getState() == PLAYER_STATE_STOPPED) {
                        screen_off = false;
                        PLAT_enableBacklight(1);
                        // Clear all GPU layers when leaving player
                        GFX_clearLayers(LAYER_SCROLLTEXT);
                        PLAT_clearLayers(LAYER_SPECTRUM);
                        PLAT_clearLayers(LAYER_PLAYTIME);
                        PLAT_GPU_Flip();
                        PlayTime_clear();  // Reset playtime state
                        // Reset to root Music folder
                        Playlist_free(&playlist);
                        playlist_active = false;
                        load_directory(MUSIC_PATH);
                        app_state = STATE_BROWSER;
                        if (autosleep_disabled) {
                            PWR_enableAutosleep();
                            autosleep_disabled = false;
                        }
                        dirty = 1;
                    }
                }
            }
            else {
                // Normal input handling when screen is on
                // Reset input timer on any button press
                if (PAD_anyPressed()) {
                    last_input_time = SDL_GetTicks();
                }

                if (PAD_justPressed(BTN_A)) {
                    Player_togglePause();
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_B)) {
                    Player_stop();
                    cleanup_album_art_background();  // Clear cached background when stopping
                    // Clear all GPU layers when leaving player
                    GFX_clearLayers(LAYER_SCROLLTEXT);
                    PLAT_clearLayers(LAYER_SPECTRUM);
                    PLAT_clearLayers(LAYER_PLAYTIME);
                    PLAT_GPU_Flip();  // Apply layer clears
                    // Reset playtime state
                    PlayTime_clear();
                    // Reset playlist state
                    Playlist_free(&playlist);
                    playlist_active = false;
                    app_state = STATE_BROWSER;
                    // Re-enable autosleep when leaving playing state
                    if (autosleep_disabled) {
                        PWR_enableAutosleep();
                        autosleep_disabled = false;
                    }
                    dirty = 1;
                }
                else if (PAD_justRepeated(BTN_LEFT)) {
                    // Seek backward 5 seconds
                    int pos = Player_getPosition();
                    Player_seek(pos - 5000);
                    dirty = 1;
                }
                else if (PAD_justRepeated(BTN_RIGHT)) {
                    // Seek forward 5 seconds
                    int pos = Player_getPosition();
                    Player_seek(pos + 5000);
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_DOWN) || PAD_justPressed(BTN_L1)) {
                    // Previous track (Down or L1)
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
                        // Fallback to old behavior if playlist not active
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
                    // Next track (Up or R1)
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
                        // Fallback to old behavior if playlist not active
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
                    // Toggle shuffle
                    shuffle_enabled = !shuffle_enabled;
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_Y)) {
                    // Toggle repeat
                    repeat_enabled = !repeat_enabled;
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_L3) || PAD_justPressed(BTN_L2)) {
                    // Toggle spectrum visibility (L3 or L2)
                    Spectrum_toggleVisibility();
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_R3) || PAD_justPressed(BTN_R2)) {
                    // Cycle spectrum color style (R3 or R2)
                    Spectrum_cycleStyle();
                    dirty = 1;
                }
                else if (PAD_tappedSelect(SDL_GetTicks())) {
                    // Show screen off hint before turning off
                    screen_off_hint_active = true;
                    screen_off_hint_start = SDL_GetTicks();
                    screen_off_hint_start_wallclock = time(NULL);
                    // Clear all GPU layers so hint is not obscured
                    GFX_clearLayers(LAYER_SCROLLTEXT);
                    PLAT_clearLayers(LAYER_SPECTRUM);
                    PLAT_clearLayers(LAYER_PLAYTIME);
                    PLAT_GPU_Flip();
                    dirty = 1;
                }

                // Check if track ended (only if still in playing state - not if user pressed back)
                if (app_state == STATE_PLAYING) {
                    Player_update();
                    if (Player_getState() == PLAYER_STATE_STOPPED) {
                        bool found_next = false;

                        if (repeat_enabled) {
                            // Repeat current track
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
                            // Pick a random track
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
                            // Normal: advance to next track (no wrap-around)
                            if (playlist_active) {
                                int new_idx = Playlist_next(&playlist);
                                if (new_idx >= 0) {
                                    const PlaylistTrack* track = Playlist_getTrack(&playlist, new_idx);
                                    if (track && Player_load(track->path) == 0) {
                                        Player_play();
                                        found_next = true;
                                    }
                                }
                                // If new_idx < 0, end of playlist - found_next stays false
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

                        dirty = 1;

                        // If no next track, go back to browser at root Music folder
                        if (!found_next && Player_getState() == PLAYER_STATE_STOPPED) {
                            // Clear all GPU layers when leaving player
                            GFX_clearLayers(LAYER_SCROLLTEXT);
                            PLAT_clearLayers(LAYER_SPECTRUM);
                            PLAT_clearLayers(LAYER_PLAYTIME);
                            PLAT_GPU_Flip();
                            PlayTime_clear();  // Reset playtime state
                            // Reset to root Music folder
                            Playlist_free(&playlist);
                            playlist_active = false;
                            load_directory(MUSIC_PATH);
                            app_state = STATE_BROWSER;
                            if (autosleep_disabled) {
                                PWR_enableAutosleep();
                                autosleep_disabled = false;
                            }
                        }
                    }

                    // Auto screen-off after inactivity (only while playing)
                    if (Player_getState() == PLAYER_STATE_PLAYING && !screen_off_hint_active) {
                        uint32_t screen_timeout_ms = CFG_getScreenTimeoutSecs() * 1000;
                        if (screen_timeout_ms > 0 && last_input_time > 0) {
                            uint32_t now = SDL_GetTicks();
                            if (now - last_input_time >= screen_timeout_ms) {
                                // Show screen off hint before turning off
                                screen_off_hint_active = true;
                                screen_off_hint_start = SDL_GetTicks();
                                screen_off_hint_start_wallclock = time(NULL);
                                // Clear all GPU layers so hint is not obscured
                                GFX_clearLayers(LAYER_SCROLLTEXT);
                                PLAT_clearLayers(LAYER_SPECTRUM);
                                PLAT_clearLayers(LAYER_PLAYTIME);
                                PLAT_GPU_Flip();
                                dirty = 1;
                            }
                        }
                    }

                }
            }

            // Animate player title scroll (GPU mode, no screen redraw needed)
            // Skip animations when screen is off or hint is showing
            if (!screen_off && !screen_off_hint_active) {
                if (player_needs_scroll_refresh()) {
                    player_animate_scroll();
                }

                // Animate spectrum visualizer (GPU mode)
                if (Spectrum_needsRefresh()) {
                    Spectrum_renderGPU();
                }

                // Update playtime display (GPU mode, updates once per second)
                if (PlayTime_needsRefresh()) {
                    PlayTime_renderGPU();
                }
            }
        }
        else if (app_state == STATE_RADIO_LIST) {
            RadioStation* stations;
            int station_count = Radio_getStations(&stations);

            if (PAD_justRepeated(BTN_UP) && station_count > 0) {
                radio_selected = (radio_selected > 0) ? radio_selected - 1 : station_count - 1;
                dirty = 1;
            }
            else if (PAD_justRepeated(BTN_DOWN) && station_count > 0) {
                radio_selected = (radio_selected < station_count - 1) ? radio_selected + 1 : 0;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A) && station_count > 0) {
                // Check network first
                if (!YouTube_checkNetwork()) {
                    // No network - show toast and stay on list
                    strncpy(radio_toast_message, "No internet connection", sizeof(radio_toast_message) - 1);
                    radio_toast_message[sizeof(radio_toast_message) - 1] = '\0';
                    radio_toast_time = SDL_GetTicks();
                    dirty = 1;
                } else {
                    // Start playing the selected station
                    if (Radio_play(stations[radio_selected].url) == 0) {
                        app_state = STATE_RADIO_PLAYING;
                        last_input_time = SDL_GetTicks();  // Start screen-off timer
                        dirty = 1;
                    }
                }
            }
            else if (PAD_justPressed(BTN_B)) {
                app_state = STATE_MENU;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_Y)) {
                // Open Add Stations screen
                add_country_selected = 0;
                add_country_scroll = 0;
                app_state = STATE_RADIO_ADD;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_X)) {
                // Open Help/Instructions screen
                app_state = STATE_RADIO_HELP;
                dirty = 1;
            }
        }
        else if (app_state == STATE_RADIO_PLAYING) {
            // Disable autosleep while playing radio
            if (!autosleep_disabled) {
                PWR_disableAutosleep();
                autosleep_disabled = true;
            }

            // Handle screen off hint timeout - turn off screen after hint displays
            if (screen_off_hint_active) {
                uint32_t now = SDL_GetTicks();
                time_t now_wallclock = time(NULL);
                // Check both SDL ticks and wall-clock time (wall-clock continues during device sleep)
                bool timeout_sdl = (now - screen_off_hint_start >= SCREEN_OFF_HINT_DURATION_MS);
                bool timeout_wallclock = (now_wallclock - screen_off_hint_start_wallclock >= (SCREEN_OFF_HINT_DURATION_MS / 1000));
                if (timeout_sdl || timeout_wallclock) {
                    screen_off_hint_active = false;
                    screen_off = true;
                    // Clear the framebuffer before turning off backlight
                    // This prevents the hint from showing if device wakes via power button
                    GFX_clear(screen);
                    GFX_flip(screen);
                    PLAT_enableBacklight(0);
                }
                // Still update radio while hint is showing
                Radio_update();
            }
            // Handle screen off mode - require SELECT + A to wake (prevents accidental wake in pocket)
            else if (screen_off) {
                if (PAD_isPressed(BTN_SELECT) && PAD_isPressed(BTN_A)) {
                    screen_off = false;
                    PLAT_enableBacklight(1);
                    last_input_time = SDL_GetTicks();  // Reset timer on wake
                    dirty = 1;
                }
                // Still update radio while screen is off
                Radio_update();
            }
            else {
                // Reset input timer on any button press
                if (PAD_anyPressed()) {
                    last_input_time = SDL_GetTicks();
                }

                // Station switching with UP/DOWN
                if (PAD_justPressed(BTN_UP) || PAD_justPressed(BTN_R1)) {
                    // Next station
                    RadioStation* stations;
                    int station_count = Radio_getStations(&stations);
                    if (station_count > 1) {
                        radio_selected = (radio_selected + 1) % station_count;
                        Radio_stop();
                        Radio_play(stations[radio_selected].url);
                        dirty = 1;
                    }
                }
                else if (PAD_justPressed(BTN_DOWN) || PAD_justPressed(BTN_L1)) {
                    // Previous station
                    RadioStation* stations;
                    int station_count = Radio_getStations(&stations);
                    if (station_count > 1) {
                        radio_selected = (radio_selected - 1 + station_count) % station_count;
                        Radio_stop();
                        Radio_play(stations[radio_selected].url);
                        dirty = 1;
                    }
                }
                else if (PAD_justPressed(BTN_B)) {
                    Radio_stop();
                    cleanup_album_art_background();  // Clear cached background when stopping
                    app_state = STATE_RADIO_LIST;
                    if (autosleep_disabled) {
                        PWR_enableAutosleep();
                        autosleep_disabled = false;
                    }
                    dirty = 1;
                }
                else if (PAD_tappedSelect(SDL_GetTicks())) {
                    // Show screen off hint before turning off
                    screen_off_hint_active = true;
                    screen_off_hint_start = SDL_GetTicks();
                    screen_off_hint_start_wallclock = time(NULL);
                    // Clear all GPU layers so hint is not obscured
                    GFX_clearLayers(LAYER_SCROLLTEXT);
                    PLAT_clearLayers(LAYER_BUFFER);
                    PLAT_GPU_Flip();
                    dirty = 1;
                }

                // Update radio state
                Radio_update();

                // Auto screen-off after inactivity (only while playing)
                if (Radio_getState() == RADIO_STATE_PLAYING && !screen_off_hint_active) {
                    uint32_t screen_timeout_ms = CFG_getScreenTimeoutSecs() * 1000;
                    if (screen_timeout_ms > 0 && last_input_time > 0) {
                        uint32_t now = SDL_GetTicks();
                        if (now - last_input_time >= screen_timeout_ms) {
                            // Show screen off hint before turning off
                            screen_off_hint_active = true;
                            screen_off_hint_start = SDL_GetTicks();
                            screen_off_hint_start_wallclock = time(NULL);
                            // Clear all GPU layers so hint is not obscured
                            GFX_clearLayers(LAYER_SCROLLTEXT);
                            PLAT_clearLayers(LAYER_BUFFER);
                            PLAT_GPU_Flip();
                            dirty = 1;
                        }
                    }
                }

            }

            // Radio GPU layer - rendered BEFORE if(dirty), same as Spectrum/PlayTime in player
            if (!screen_off && !screen_off_hint_active) {
                if (RadioStatus_needsRefresh()) {
                    RadioStatus_renderGPU();
                }
            }
        }
        else if (app_state == STATE_RADIO_ADD) {
            // Country selection screen
            int country_count = Radio_getCuratedCountryCount();

            if (PAD_justRepeated(BTN_UP) && country_count > 0) {
                add_country_selected = (add_country_selected > 0) ? add_country_selected - 1 : country_count - 1;
                dirty = 1;
            }
            else if (PAD_justRepeated(BTN_DOWN) && country_count > 0) {
                add_country_selected = (add_country_selected < country_count - 1) ? add_country_selected + 1 : 0;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A) && country_count > 0) {
                // Select country and go to station selection
                const CuratedCountry* countries = Radio_getCuratedCountries();
                add_selected_country_code = countries[add_country_selected].code;
                add_station_selected = 0;
                add_station_scroll = 0;
                // Initialize checked states based on existing stations
                memset(add_station_checked, 0, sizeof(add_station_checked));
                int sc = 0;
                const CuratedStation* cs = Radio_getCuratedStations(add_selected_country_code, &sc);
                for (int i = 0; i < sc && i < 256; i++) {
                    add_station_checked[i] = Radio_stationExists(cs[i].url);
                }
                app_state = STATE_RADIO_ADD_STATIONS;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                app_state = STATE_RADIO_LIST;
                dirty = 1;
            }
        }
        else if (app_state == STATE_RADIO_ADD_STATIONS) {
            // Station selection screen
            int station_count = 0;
            const CuratedStation* stations = Radio_getCuratedStations(add_selected_country_code, &station_count);

            if (PAD_justRepeated(BTN_UP) && station_count > 0) {
                add_station_selected = (add_station_selected > 0) ? add_station_selected - 1 : station_count - 1;
                dirty = 1;
            }
            else if (PAD_justRepeated(BTN_DOWN) && station_count > 0) {
                add_station_selected = (add_station_selected < station_count - 1) ? add_station_selected + 1 : 0;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A) && station_count > 0) {
                // Toggle station selection (allow toggling all stations)
                if (add_station_selected < 256) {
                    add_station_checked[add_station_selected] = !add_station_checked[add_station_selected];
                    dirty = 1;
                }
            }
            else if (PAD_justPressed(BTN_X)) {
                // Add/remove stations based on checked state
                int added = 0;
                int removed = 0;
                for (int i = 0; i < station_count && i < 256; i++) {
                    bool exists = Radio_stationExists(stations[i].url);
                    if (add_station_checked[i] && !exists) {
                        // Add new station
                        if (Radio_addStation(stations[i].name, stations[i].url, stations[i].genre, stations[i].slogan) >= 0) {
                            added++;
                        }
                    } else if (!add_station_checked[i] && exists) {
                        // Remove unchecked station
                        if (Radio_removeStationByUrl(stations[i].url)) {
                            removed++;
                        }
                    }
                }
                if (added > 0 || removed > 0) {
                    Radio_saveStations();
                }
                // Clear selections and go back
                memset(add_station_checked, 0, sizeof(add_station_checked));
                app_state = STATE_RADIO_LIST;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                app_state = STATE_RADIO_ADD;
                dirty = 1;
            }
        }
        else if (app_state == STATE_RADIO_HELP) {
            // Help screen - scrolling and back button
            int scroll_step = SCALE1(18);  // Same as line_h
            if (PAD_justRepeated(BTN_UP)) {
                if (help_scroll > 0) {
                    help_scroll -= scroll_step;
                    if (help_scroll < 0) help_scroll = 0;
                    dirty = 1;
                }
            }
            else if (PAD_justRepeated(BTN_DOWN)) {
                help_scroll += scroll_step;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                help_scroll = 0;  // Reset scroll when leaving
                app_state = STATE_RADIO_ADD;
                dirty = 1;
            }
        }
        else if (app_state == STATE_YOUTUBE_MENU) {
            if (PAD_justRepeated(BTN_UP)) {
                youtube_menu_selected = (youtube_menu_selected > 0) ? youtube_menu_selected - 1 : YOUTUBE_MENU_COUNT - 1;
                dirty = 1;
            }
            else if (PAD_justRepeated(BTN_DOWN)) {
                youtube_menu_selected = (youtube_menu_selected < YOUTUBE_MENU_COUNT - 1) ? youtube_menu_selected + 1 : 0;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A)) {
                if (youtube_menu_selected == 0) {
                    // Search Music - check network first
                    if (!YouTube_checkNetwork()) {
                        // No network - show toast and stay on menu
                        strncpy(youtube_toast_message, "No internet connection", sizeof(youtube_toast_message) - 1);
                        youtube_toast_message[sizeof(youtube_toast_message) - 1] = '\0';
                        youtube_toast_time = SDL_GetTicks();
                        dirty = 1;
                    } else {
                        // Network OK - open keyboard
                        char* query = YouTube_openKeyboard("Search:");
                        // Reset button state and re-poll to prevent keyboard B press from triggering menu back
                        PAD_reset();
                        PAD_poll();
                        PAD_reset();
                        if (query && strlen(query) > 0) {
                            strncpy(youtube_search_query, query, sizeof(youtube_search_query) - 1);
                            youtube_search_query[sizeof(youtube_search_query) - 1] = '\0';
                            youtube_results_selected = -1;  // No selection initially
                            youtube_results_scroll = 0;
                            youtube_result_count = 0;
                            // Start async search (won't block UI)
                            if (YouTube_startSearch(query) == 0) {
                                youtube_searching = true;
                                app_state = STATE_YOUTUBE_SEARCHING;
                            }
                        }
                        if (query) free(query);
                        dirty = 1;
                    }
                } else if (youtube_menu_selected == 1) {
                    // Download Queue
                    youtube_queue_selected = 0;
                    youtube_queue_scroll = 0;
                    app_state = STATE_YOUTUBE_QUEUE;
                    dirty = 1;
                } else if (youtube_menu_selected == 2) {
                    // Update yt-dlp
                    YouTube_startUpdate();
                    app_state = STATE_YOUTUBE_UPDATING;
                    dirty = 1;
                }
            }
            else if (PAD_justPressed(BTN_B)) {
                app_state = STATE_MENU;
                dirty = 1;
            }
        }
        else if (app_state == STATE_YOUTUBE_SEARCHING) {
            // Handle search cancellation
            if (PAD_justPressed(BTN_B)) {
                YouTube_cancelSearch();
                youtube_searching = false;
                app_state = STATE_YOUTUBE_MENU;
                dirty = 1;
            }
            // Keep screen refreshing during search
            dirty = 1;
        }
        else if (app_state == STATE_YOUTUBE_RESULTS) {
            if (PAD_justRepeated(BTN_UP) && youtube_result_count > 0) {
                if (youtube_results_selected < 0) {
                    youtube_results_selected = youtube_result_count - 1;  // From no selection, go to last
                } else {
                    youtube_results_selected = (youtube_results_selected > 0) ? youtube_results_selected - 1 : youtube_result_count - 1;
                }
                dirty = 1;
            }
            else if (PAD_justRepeated(BTN_DOWN) && youtube_result_count > 0) {
                if (youtube_results_selected < 0) {
                    youtube_results_selected = 0;  // From no selection, go to first
                } else {
                    youtube_results_selected = (youtube_results_selected < youtube_result_count - 1) ? youtube_results_selected + 1 : 0;
                }
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A) && youtube_result_count > 0 && youtube_results_selected >= 0) {
                // Toggle add/remove from queue
                YouTubeResult* result = &youtube_results[youtube_results_selected];
                if (YouTube_isInQueue(result->video_id)) {
                    // Remove from queue
                    if (YouTube_queueRemoveById(result->video_id) == 0) {
                        snprintf(youtube_toast_message, sizeof(youtube_toast_message), "Removed from queue");
                    } else {
                        snprintf(youtube_toast_message, sizeof(youtube_toast_message), "Failed to remove");
                    }
                } else {
                    // Add to queue
                    int added = YouTube_queueAdd(result->video_id, result->title);
                    if (added == 1) {
                        snprintf(youtube_toast_message, sizeof(youtube_toast_message), "Added to queue!");
                    } else if (added == -1) {
                        snprintf(youtube_toast_message, sizeof(youtube_toast_message), "Queue is full");
                    }
                }
                youtube_toast_time = SDL_GetTicks();
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                youtube_toast_message[0] = '\0';  // Clear toast
                GFX_clearLayers(LAYER_SCROLLTEXT);  // Clear scroll layer when leaving
                app_state = STATE_YOUTUBE_MENU;
                dirty = 1;
            }

            // Animate scroll without full redraw (GPU mode)
            if (youtube_results_needs_scroll_refresh()) {
                youtube_results_animate_scroll();
            }
        }
        else if (app_state == STATE_YOUTUBE_QUEUE) {
            int qcount = YouTube_queueCount();
            if (PAD_justRepeated(BTN_UP) && qcount > 0) {
                youtube_queue_selected = (youtube_queue_selected > 0) ? youtube_queue_selected - 1 : qcount - 1;
                dirty = 1;
            }
            else if (PAD_justRepeated(BTN_DOWN) && qcount > 0) {
                youtube_queue_selected = (youtube_queue_selected < qcount - 1) ? youtube_queue_selected + 1 : 0;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A) && qcount > 0) {
                // Start downloading
                if (YouTube_downloadStart() == 0) {
                    app_state = STATE_YOUTUBE_DOWNLOADING;
                }
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_X) && qcount > 0) {
                // Remove selected item
                YouTube_queueRemove(youtube_queue_selected);
                youtube_queue_clear_scroll();  // Clear scroll state when item removed
                if (youtube_queue_selected >= YouTube_queueCount() && youtube_queue_selected > 0) {
                    youtube_queue_selected--;
                }
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                GFX_clearLayers(LAYER_SCROLLTEXT);  // Clear scroll layer when leaving
                app_state = STATE_YOUTUBE_MENU;
                dirty = 1;
            }

            // Animate scroll without full redraw (GPU mode)
            if (youtube_queue_needs_scroll_refresh()) {
                youtube_queue_animate_scroll();
            }
        }
        else if (app_state == STATE_YOUTUBE_DOWNLOADING) {
            YouTube_update();
            const YouTubeDownloadStatus* status = YouTube_getDownloadStatus();
            if (status->state != YOUTUBE_STATE_DOWNLOADING) {
                // Download finished
                youtube_queue_clear_scroll();  // Clear scroll state as queue changed
                app_state = STATE_YOUTUBE_QUEUE;
            }
            if (PAD_justPressed(BTN_B)) {
                // Cancel download
                YouTube_downloadStop();
                youtube_queue_clear_scroll();  // Clear scroll state as queue may have changed
                app_state = STATE_YOUTUBE_QUEUE;
            }
            dirty = 1;  // Always redraw during download
        }
        else if (app_state == STATE_YOUTUBE_UPDATING) {
            YouTube_update();
            const YouTubeUpdateStatus* status = YouTube_getUpdateStatus();
            if (PAD_justPressed(BTN_B)) {
                if (status->updating) {
                    // Cancel update
                    YouTube_cancelUpdate();
                }
                app_state = STATE_YOUTUBE_MENU;
                dirty = 1;
            }
            dirty = 1;  // Always redraw during update
        }
        else if (app_state == STATE_APP_UPDATING) {
            // Disable autosleep during update to prevent screen turning off
            if (!autosleep_disabled) {
                PWR_disableAutosleep();
                autosleep_disabled = true;
            }

            SelfUpdate_update();
            const SelfUpdateStatus* status = SelfUpdate_getStatus();
            SelfUpdateState state = status->state;

            if (state == SELFUPDATE_STATE_COMPLETED) {
                if (PAD_justPressed(BTN_A)) {
                    // Restart app - autosleep will be re-enabled in cleanup
                    quit = true;
                }
                // No "Later" option - force restart after successful update
            }
            else if (PAD_justPressed(BTN_B)) {
                if (state == SELFUPDATE_STATE_DOWNLOADING) {
                    // Cancel update
                    SelfUpdate_cancelUpdate();
                }
                // Re-enable autosleep when leaving update screen
                if (autosleep_disabled) {
                    PWR_enableAutosleep();
                    autosleep_disabled = false;
                }
                app_state = STATE_ABOUT;
                dirty = 1;
            }
            dirty = 1;  // Always redraw during update
        }
        else if (app_state == STATE_ABOUT) {
            if (PAD_justPressed(BTN_A)) {
                // Start update if available
                const SelfUpdateStatus* status = SelfUpdate_getStatus();
                if (status->update_available) {
                    SelfUpdate_startUpdate();
                    app_state = STATE_APP_UPDATING;
                    dirty = 1;
                }
            }
            else if (PAD_justPressed(BTN_B)) {
                app_state = STATE_MENU;
                dirty = 1;
            }
        }

        // Skip PWR_update when dialogs are shown to prevent button hint flickering
        if (!show_quit_confirm && !show_controls_help && !show_delete_confirm && !screen_off_hint_active) {
            int dirty_before = dirty;
            PWR_update(&dirty, &show_setting, NULL, NULL);

            // If screen should be off but system woke it (e.g., power button wake), turn it back off
            // Only check when PWR_update signaled an event (dirty changed)
            if (screen_off && dirty && !dirty_before) {
                PLAT_enableBacklight(0);
            }
        }

        // Skip rendering when screen is off to save power (but allow rendering during hint)
        if (dirty && (!screen_off || screen_off_hint_active)) {
            // Clear scroll layer on any full redraw - states with scrolling will re-render it
            GFX_clearLayers(LAYER_SCROLLTEXT);

            // Render screen off hint (takes priority over other dialogs)
            if (screen_off_hint_active) {
                GFX_clear(screen);
                render_screen_off_hint(screen);
                GFX_flip(screen);
                dirty = 0;
            }
            // Skip state rendering when dialog is shown (GPU layers already cleared)
            else if (show_quit_confirm) {
                // Just render the quit dialog overlay on black background
                GFX_clear(screen);
                render_quit_confirm(screen);
                GFX_flip(screen);
                dirty = 0;
            }
            else if (show_controls_help) {
                // Just render the controls help dialog overlay on black background
                GFX_clear(screen);
                render_controls_help(screen, app_state);
                GFX_flip(screen);
                dirty = 0;
            }
            else if (show_delete_confirm) {
                // Render the delete confirmation dialog overlay on black background
                GFX_clear(screen);
                render_delete_confirm(screen, delete_target_name);
                GFX_flip(screen);
                dirty = 0;
            }
            else switch (app_state) {
                case STATE_MENU:
                    render_menu(screen, show_setting, menu_selected);
                    break;
                case STATE_BROWSER:
                    render_browser(screen, show_setting, &browser);
                    break;
                case STATE_PLAYING: {
                    // Pass playlist track info if playlist is active
                    int pl_track = playlist_active ? Playlist_getCurrentIndex(&playlist) + 1 : 0;
                    int pl_total = playlist_active ? Playlist_getCount(&playlist) : 0;
                    render_playing(screen, show_setting, &browser, shuffle_enabled, repeat_enabled, pl_track, pl_total);
                    break;
                }
                case STATE_RADIO_LIST:
                    render_radio_list(screen, show_setting, radio_selected, &radio_scroll,
                                      radio_toast_message, radio_toast_time);
                    break;
                case STATE_RADIO_PLAYING:
                    render_radio_playing(screen, show_setting, radio_selected);
                    break;
                case STATE_RADIO_ADD:
                    render_radio_add(screen, show_setting, add_country_selected, &add_country_scroll);
                    break;
                case STATE_RADIO_ADD_STATIONS:
                    render_radio_add_stations(screen, show_setting, add_selected_country_code,
                                              add_station_selected, &add_station_scroll, add_station_checked);
                    break;
                case STATE_RADIO_HELP:
                    render_radio_help(screen, show_setting, &help_scroll);
                    break;
                case STATE_YOUTUBE_MENU:
                    render_youtube_menu(screen, show_setting, youtube_menu_selected,
                                        youtube_toast_message, youtube_toast_time);
                    break;
                case STATE_YOUTUBE_SEARCHING:
                    render_youtube_searching(screen, show_setting, youtube_search_query);
                    break;
                case STATE_YOUTUBE_RESULTS:
                    render_youtube_results(screen, show_setting, youtube_search_query,
                                           youtube_results, youtube_result_count,
                                           youtube_results_selected, &youtube_results_scroll,
                                           youtube_toast_message, youtube_toast_time, youtube_searching);
                    break;
                case STATE_YOUTUBE_QUEUE:
                    render_youtube_queue(screen, show_setting, youtube_queue_selected, &youtube_queue_scroll);
                    break;
                case STATE_YOUTUBE_DOWNLOADING:
                    render_youtube_downloading(screen, show_setting);
                    break;
                case STATE_YOUTUBE_UPDATING:
                    render_youtube_updating(screen, show_setting);
                    break;
                case STATE_APP_UPDATING:
                    render_app_updating(screen, show_setting);
                    break;
                case STATE_ABOUT:
                    render_about(screen, show_setting);
                    break;
            }

            if (show_setting) {
                GFX_blitHardwareHints(screen, show_setting);
            }

            GFX_flip(screen);
            dirty = 0;

            // Keep refreshing while toast is visible (YouTube)
            if ((app_state == STATE_YOUTUBE_RESULTS || app_state == STATE_YOUTUBE_MENU) && youtube_toast_message[0] != '\0') {
                if (SDL_GetTicks() - youtube_toast_time < YOUTUBE_TOAST_DURATION) {
                    dirty = 1;
                } else {
                    // Clear toast after duration
                    youtube_toast_message[0] = '\0';
                }
            }

            // Keep refreshing while toast is visible (Radio)
            if (app_state == STATE_RADIO_LIST && radio_toast_message[0] != '\0') {
                if (SDL_GetTicks() - radio_toast_time < RADIO_TOAST_DURATION) {
                    dirty = 1;
                } else {
                    // Clear toast after duration
                    radio_toast_message[0] = '\0';
                }
            }

            // Poll async search status
            if (app_state == STATE_YOUTUBE_SEARCHING && youtube_searching) {
                const YouTubeSearchStatus* search_status = YouTube_getSearchStatus();
                if (search_status->completed) {
                    youtube_searching = false;
                    if (search_status->result_count > 0) {
                        // Copy results from search module
                        YouTubeResult* results = YouTube_getSearchResults();
                        youtube_result_count = search_status->result_count;
                        for (int i = 0; i < youtube_result_count && i < YOUTUBE_MAX_RESULTS; i++) {
                            youtube_results[i] = results[i];
                        }
                        app_state = STATE_YOUTUBE_RESULTS;
                        // Reset button state to prevent auto-add from lingering keyboard button press
                        PAD_reset();
                    } else {
                        // No results or error - go back to menu
                        // Show error in toast if available
                        if (search_status->error_message[0] != '\0') {
                            strncpy(youtube_toast_message, search_status->error_message, sizeof(youtube_toast_message) - 1);
                            youtube_toast_message[sizeof(youtube_toast_message) - 1] = '\0';
                            youtube_toast_time = SDL_GetTicks();
                        }
                        app_state = STATE_YOUTUBE_MENU;
                    }
                    dirty = 1;
                } else {
                    // Still searching - keep refreshing to show animation
                    dirty = 1;
                }
            }
        } else if (!screen_off) {
            GFX_sync();
        }
    }

cleanup:
    // Ensure screen is back on and autosleep is re-enabled
    if (screen_off) {
        PLAT_enableBacklight(1);
        screen_off = false;
    }
    if (autosleep_disabled) {
        PWR_enableAutosleep();
        autosleep_disabled = false;
    }

    // Clear all GPU layers on exit
    GFX_clearLayers(LAYER_SCROLLTEXT);
    PLAT_clearLayers(LAYER_SPECTRUM);
    PLAT_clearLayers(LAYER_PLAYTIME);
    PLAT_clearLayers(LAYER_BUFFER);

    SelfUpdate_cleanup();
    YouTube_cleanup();
    Radio_quit();
    cleanup_album_art_background();  // Clean up cached background surface
    Spectrum_quit();
    Player_quit();
    Playlist_free(&playlist);  // Clean up playlist
    Browser_freeEntries(&browser);
    Icons_quit();
    unload_custom_fonts();

    QuitSettings();
    PWR_quit();
    PAD_quit();
    GFX_quit();

    return EXIT_SUCCESS;
}
