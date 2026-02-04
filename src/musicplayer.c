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
#include "podcast.h"
#include "ui_podcast.h"

// App states
typedef enum {
    STATE_MENU = 0,         // Main menu (Files / Radio / Podcasts / YouTube / Settings)
    STATE_BROWSER,          // File browser
    STATE_PLAYING,          // Playing local file
    STATE_RADIO_LIST,       // Radio station list
    STATE_RADIO_PLAYING,    // Playing radio stream
    STATE_RADIO_ADD,        // Add stations - country selection
    STATE_RADIO_ADD_STATIONS, // Add stations - station selection
    STATE_RADIO_HELP,       // Help/instructions screen
    STATE_PODCAST_MENU,     // Podcast main menu (shows subscribed podcasts)
    STATE_PODCAST_MANAGE,   // Podcast management menu (Y button)
    STATE_PODCAST_SUBSCRIPTIONS, // Podcast subscriptions list (from manage menu)
    STATE_PODCAST_TOP_SHOWS, // Top Shows chart
    STATE_PODCAST_SEARCH_RESULTS, // Podcast search results
    STATE_PODCAST_EPISODES, // Episode list for a feed
    STATE_PODCAST_BUFFERING, // Buffering podcast stream
    STATE_PODCAST_PLAYING,  // Playing podcast episode
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
#define MENU_ITEM_COUNT 5

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

// Podcast state
static int podcast_menu_selected = 0;   // Selection in main podcast list (subscribed podcasts)
static int podcast_menu_scroll = 0;      // Scroll in main podcast list
static int podcast_manage_selected = 0;  // Selection in management menu
static int podcast_subscriptions_selected = 0;
static int podcast_subscriptions_scroll = 0;
static int podcast_top_shows_selected = 0;
static int podcast_top_shows_scroll = 0;
static int podcast_search_selected = 0;
static int podcast_search_scroll = 0;
static int podcast_episodes_selected = 0;
static int podcast_episodes_scroll = 0;
static int podcast_current_feed_index = -1;  // Index of currently viewed feed
static int podcast_current_episode_index = -1;  // Index of currently playing episode
static char podcast_search_query[256] = "";
static char podcast_toast_message[128] = "";
static uint32_t podcast_toast_time = 0;
#define PODCAST_TOAST_DURATION 3000  // 3 seconds

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

// Main menu toast (for WiFi connection messages)
static char menu_toast_message[128] = "";
static uint32_t menu_toast_time = 0;
#define MENU_TOAST_DURATION 3000  // 3 seconds

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

// WiFi connection timeout (in 500ms intervals)
#define WIFI_CONNECT_TIMEOUT_INTERVALS 10  // 5 seconds total

// Render a simple "Connecting..." screen
static void render_connecting_screen(SDL_Surface* scr, int show_setting) {
    // Clear GPU scroll text layer to prevent bleeding through
    Podcast_clearTitleScroll();
    GFX_clear(scr);

    int hw = scr->w;
    int hh = scr->h;

    // Center the message
    const char* msg = "Connecting to WiFi...";
    SDL_Surface* text = TTF_RenderUTF8_Blended(get_font_medium(), msg, COLOR_WHITE);
    if (text) {
        SDL_BlitSurface(text, NULL, scr, &(SDL_Rect){(hw - text->w) / 2, (hh - text->h) / 2});
        SDL_FreeSurface(text);
    }

    GFX_blitHardwareGroup(scr, show_setting);
    GFX_flip(scr);
}

// Ensure WiFi is connected, enabling if necessary
// Returns true if connected, false otherwise
// Shows "Connecting..." screen while waiting (if scr is not NULL)
// Can be called from background threads with scr=NULL to skip UI rendering
bool ensure_wifi_connected(SDL_Surface* scr, int show_setting) {
    // Already connected?
    if (PLAT_wifiEnabled() && PLAT_wifiConnected()) {
        return true;
    }

    // Only render if screen is provided (not from background thread)
    if (scr) {
        render_connecting_screen(scr, show_setting);
    }

    // If WiFi is disabled, enable it
    if (!PLAT_wifiEnabled()) {
        PLAT_wifiEnable(true);
        // Give wpa_supplicant time to start
        usleep(1000000);  // 1 second
    }

    // Enable all saved networks (they may be disabled by select_network)
    system("wpa_cli -p /etc/wifi/sockets -i wlan0 enable_network all > /dev/null 2>&1");

    // Trigger reconnect to saved networks
    system("wpa_cli -p /etc/wifi/sockets -i wlan0 reconnect > /dev/null 2>&1");

    // Wait for connection to a known network
    for (int i = 0; i < WIFI_CONNECT_TIMEOUT_INTERVALS; i++) {
        if (PLAT_wifiConnected()) {
            // Request IP via DHCP
            system("killall udhcpc 2>/dev/null; udhcpc -i wlan0 -b 2>/dev/null &");
            // Wait briefly for DHCP to complete
            usleep(1500000);  // 1.5 seconds
            return true;
        }
        usleep(500000);  // 500ms

        // Keep rendering and processing events while waiting (only if screen provided)
        if (scr) {
            PAD_poll();
            render_connecting_screen(scr, show_setting);
        }
    }

    // Final check
    if (PLAT_wifiConnected()) {
        // Request IP via DHCP
        system("killall udhcpc 2>/dev/null; udhcpc -i wlan0 -b 2>/dev/null &");
        usleep(1500000);  // 1.5 seconds
        return true;
    }
    return false;
}

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
    Podcast_init();

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
                    // Internet Radio - ensure WiFi connected first
                    if (ensure_wifi_connected(screen, show_setting)) {
                        app_state = STATE_RADIO_LIST;
                        dirty = 1;
                    } else {
                        // No connection - show toast on main menu
                        strncpy(menu_toast_message, "Internet connection required", sizeof(menu_toast_message) - 1);
                        menu_toast_message[sizeof(menu_toast_message) - 1] = '\0';
                        menu_toast_time = SDL_GetTicks();
                        dirty = 1;
                    }
                } else if (menu_selected == 2) {
                    // Podcasts - no WiFi check needed, only for search/top shows
                    app_state = STATE_PODCAST_MENU;
                    podcast_menu_selected = 0;
                    dirty = 1;
                } else if (menu_selected == 3) {
                    // Downloader - ensure WiFi connected first
                    if (YouTube_isAvailable()) {
                        if (ensure_wifi_connected(screen, show_setting)) {
                            app_state = STATE_YOUTUBE_MENU;
                            youtube_menu_selected = 0;
                            dirty = 1;
                        } else {
                            // No connection - show toast on main menu
                            strncpy(menu_toast_message, "Internet connection required", sizeof(menu_toast_message) - 1);
                            menu_toast_message[sizeof(menu_toast_message) - 1] = '\0';
                            menu_toast_time = SDL_GetTicks();
                            dirty = 1;
                        }
                    }
                } else if (menu_selected == 4) {
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
                // Start playing the selected station
                if (Radio_play(stations[radio_selected].url) == 0) {
                    app_state = STATE_RADIO_PLAYING;
                    last_input_time = SDL_GetTicks();  // Start screen-off timer
                    dirty = 1;
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
                    // Search Music - open keyboard
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
        // ============================================================================
        // Podcast States
        // ============================================================================
        else if (app_state == STATE_PODCAST_MENU) {
            // Main podcast screen - shows subscribed podcasts (like radio stations list)
            Podcast_update();
            int count = Podcast_getSubscriptionCount();

            if (PAD_justRepeated(BTN_UP) && count > 0) {
                podcast_menu_selected = (podcast_menu_selected > 0) ? podcast_menu_selected - 1 : count - 1;
                dirty = 1;
            }
            else if (PAD_justRepeated(BTN_DOWN) && count > 0) {
                podcast_menu_selected = (podcast_menu_selected < count - 1) ? podcast_menu_selected + 1 : 0;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A) && count > 0) {
                // Open episode list for selected podcast
                podcast_current_feed_index = podcast_menu_selected;
                app_state = STATE_PODCAST_EPISODES;
                podcast_episodes_selected = 0;
                podcast_episodes_scroll = 0;
                Podcast_clearTitleScroll();  // Reset scroll state for fresh start
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_Y)) {
                // Open management menu
                app_state = STATE_PODCAST_MANAGE;
                podcast_manage_selected = 0;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                app_state = STATE_MENU;
                dirty = 1;
            }
        }
        else if (app_state == STATE_PODCAST_MANAGE) {
            // Management menu (Search, Top Shows, Subscriptions, Add URL, Downloads)
            Podcast_update();
            if (PAD_justRepeated(BTN_UP)) {
                podcast_manage_selected = (podcast_manage_selected > 0) ? podcast_manage_selected - 1 : PODCAST_MANAGE_COUNT - 1;
                dirty = 1;
            }
            else if (PAD_justRepeated(BTN_DOWN)) {
                podcast_manage_selected = (podcast_manage_selected < PODCAST_MANAGE_COUNT - 1) ? podcast_manage_selected + 1 : 0;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A)) {
                switch (podcast_manage_selected) {
                    case PODCAST_MANAGE_SEARCH: {
                        // Ensure WiFi connected first
                        if (!ensure_wifi_connected(screen, show_setting)) {
                            strncpy(podcast_toast_message, "Internet connection required", sizeof(podcast_toast_message) - 1);
                            podcast_toast_message[sizeof(podcast_toast_message) - 1] = '\0';
                            podcast_toast_time = SDL_GetTicks();
                            break;
                        }
                        // Open keyboard directly
                        char* query = YouTube_openKeyboard("Search podcasts");
                        // Clear input state after external keyboard process
                        PAD_poll();
                        PAD_reset();
                        SDL_Delay(100);  // Small delay to let button state settle
                        PAD_poll();
                        PAD_reset();
                        if (query && query[0]) {
                            strncpy(podcast_search_query, query, sizeof(podcast_search_query) - 1);
                            podcast_search_query[sizeof(podcast_search_query) - 1] = '\0';
                            Podcast_startSearch(podcast_search_query);
                            app_state = STATE_PODCAST_SEARCH_RESULTS;
                            podcast_search_selected = 0;
                            podcast_search_scroll = 0;
                            podcast_toast_message[0] = '\0';  // Clear toast
                        }
                        if (query) free(query);
                        break;
                    }
                    case PODCAST_MANAGE_TOP_SHOWS:
                        // Ensure WiFi connected first
                        if (!ensure_wifi_connected(screen, show_setting)) {
                            strncpy(podcast_toast_message, "Internet connection required", sizeof(podcast_toast_message) - 1);
                            podcast_toast_message[sizeof(podcast_toast_message) - 1] = '\0';
                            podcast_toast_time = SDL_GetTicks();
                            break;
                        }
                        Podcast_loadCharts(NULL);  // Use default country code
                        app_state = STATE_PODCAST_TOP_SHOWS;
                        podcast_top_shows_selected = 0;
                        podcast_top_shows_scroll = 0;
                        podcast_toast_message[0] = '\0';  // Clear toast
                        break;
                    case PODCAST_MANAGE_SUBSCRIPTIONS:
                        app_state = STATE_PODCAST_SUBSCRIPTIONS;
                        podcast_subscriptions_selected = 0;
                        podcast_subscriptions_scroll = 0;
                        break;
                }
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                app_state = STATE_PODCAST_MENU;
                dirty = 1;
            }
        }
        else if (app_state == STATE_PODCAST_SUBSCRIPTIONS) {
            int count = Podcast_getSubscriptionCount();
            if (PAD_justRepeated(BTN_UP) && count > 0) {
                podcast_subscriptions_selected = (podcast_subscriptions_selected > 0) ? podcast_subscriptions_selected - 1 : count - 1;
                dirty = 1;
            }
            else if (PAD_justRepeated(BTN_DOWN) && count > 0) {
                podcast_subscriptions_selected = (podcast_subscriptions_selected < count - 1) ? podcast_subscriptions_selected + 1 : 0;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A) && count > 0) {
                // Open episode list for selected feed
                podcast_current_feed_index = podcast_subscriptions_selected;
                app_state = STATE_PODCAST_EPISODES;
                podcast_episodes_selected = 0;
                podcast_episodes_scroll = 0;
                Podcast_clearTitleScroll();  // Reset scroll state for fresh start
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_X) && count > 0) {
                // Unsubscribe
                Podcast_unsubscribe(podcast_subscriptions_selected);
                if (podcast_subscriptions_selected >= Podcast_getSubscriptionCount()) {
                    podcast_subscriptions_selected = Podcast_getSubscriptionCount() - 1;
                    if (podcast_subscriptions_selected < 0) podcast_subscriptions_selected = 0;
                }
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                app_state = STATE_PODCAST_MANAGE;
                dirty = 1;
            }
        }
        else if (app_state == STATE_PODCAST_TOP_SHOWS) {
            Podcast_update();
            const PodcastChartsStatus* chart_status = Podcast_getChartsStatus();

            if (chart_status->loading) {
                dirty = 1;  // Keep refreshing while loading
            } else if (chart_status->completed) {
                dirty = 1;  // Refresh once when loading completes
            }
            // Keep refreshing while toast is visible
            if (podcast_toast_message[0] && (SDL_GetTicks() - podcast_toast_time < PODCAST_TOAST_DURATION)) {
                dirty = 1;
            }
            // Animate scroll when not dirty but scrolling is active
            if (Podcast_isTitleScrolling()) {
                Podcast_animateTitleScroll();
            }
            if (!chart_status->loading) {
                int count = 0;
                Podcast_getTopShows(&count);

                if (PAD_justRepeated(BTN_UP) && count > 0) {
                    podcast_top_shows_selected = (podcast_top_shows_selected > 0) ? podcast_top_shows_selected - 1 : count - 1;
                    Podcast_clearTitleScroll();  // Clear scroll state on selection change
                    dirty = 1;
                }
                else if (PAD_justRepeated(BTN_DOWN) && count > 0) {
                    podcast_top_shows_selected = (podcast_top_shows_selected < count - 1) ? podcast_top_shows_selected + 1 : 0;
                    Podcast_clearTitleScroll();  // Clear scroll state on selection change
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_A) && count > 0) {
                    // Subscribe from chart item (only if not already subscribed)
                    PodcastChartItem* items = Podcast_getTopShows(&count);
                    if (podcast_top_shows_selected < count) {
                        bool already_subscribed = Podcast_isSubscribedByItunesId(items[podcast_top_shows_selected].itunes_id);
                        if (!already_subscribed) {
                            // Show "Subscribing..." loading screen
                            render_podcast_loading(screen, "Subscribing...");
                            GFX_flip(screen);

                            int sub_result = Podcast_subscribeFromItunes(items[podcast_top_shows_selected].itunes_id);
                            if (sub_result == 0) {
                                strncpy(podcast_toast_message, "Subscribed!", sizeof(podcast_toast_message) - 1);
                            } else {
                                const char* err = Podcast_getError();
                                strncpy(podcast_toast_message, err && err[0] ? err : "Subscribe failed", sizeof(podcast_toast_message) - 1);
                            }
                            podcast_toast_time = SDL_GetTicks();
                        }
                    }
                    dirty = 1;
                }
            }
            if (PAD_justPressed(BTN_B)) {
                app_state = STATE_PODCAST_MANAGE;
                dirty = 1;
            }
        }
        else if (app_state == STATE_PODCAST_SEARCH_RESULTS) {
            Podcast_update();
            const PodcastSearchStatus* search_status = Podcast_getSearchStatus();

            if (search_status->searching) {
                dirty = 1;
            } else if (search_status->completed) {
                dirty = 1;  // Redraw when search completes to show results
            }
            // Keep refreshing while toast is visible
            if (podcast_toast_message[0] && (SDL_GetTicks() - podcast_toast_time < PODCAST_TOAST_DURATION)) {
                dirty = 1;
            }
            // Animate scroll when not dirty but scrolling is active
            if (Podcast_isTitleScrolling()) {
                Podcast_animateTitleScroll();
            }
            if (!search_status->searching) {
                int count = 0;
                Podcast_getSearchResults(&count);

                if (PAD_justRepeated(BTN_UP) && count > 0) {
                    podcast_search_selected = (podcast_search_selected > 0) ? podcast_search_selected - 1 : count - 1;
                    Podcast_clearTitleScroll();  // Clear scroll state on selection change
                    dirty = 1;
                }
                else if (PAD_justRepeated(BTN_DOWN) && count > 0) {
                    podcast_search_selected = (podcast_search_selected < count - 1) ? podcast_search_selected + 1 : 0;
                    Podcast_clearTitleScroll();  // Clear scroll state on selection change
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_A) && count > 0) {
                    // Subscribe (only if not already subscribed)
                    PodcastSearchResult* results = Podcast_getSearchResults(&count);
                    if (podcast_search_selected < count) {
                        // Check if already subscribed
                        bool already_subscribed = results[podcast_search_selected].feed_url[0] &&
                                                   Podcast_isSubscribed(results[podcast_search_selected].feed_url);
                        if (!already_subscribed) {
                            // Show "Subscribing..." loading screen
                            render_podcast_loading(screen, "Subscribing...");
                            GFX_flip(screen);

                            int sub_result;
                            if (results[podcast_search_selected].feed_url[0]) {
                                sub_result = Podcast_subscribe(results[podcast_search_selected].feed_url);
                            } else {
                                sub_result = Podcast_subscribeFromItunes(results[podcast_search_selected].itunes_id);
                            }
                            if (sub_result == 0) {
                                strncpy(podcast_toast_message, "Subscribed!", sizeof(podcast_toast_message) - 1);
                            } else {
                                const char* err = Podcast_getError();
                                strncpy(podcast_toast_message, err && err[0] ? err : "Subscribe failed", sizeof(podcast_toast_message) - 1);
                            }
                            podcast_toast_time = SDL_GetTicks();
                        }
                    }
                    dirty = 1;
                }
            }
            if (PAD_justPressed(BTN_B)) {
                Podcast_cancelSearch();
                app_state = STATE_PODCAST_MANAGE;
                dirty = 1;
            }
        }
        else if (app_state == STATE_PODCAST_EPISODES) {
            PodcastFeed* feed = Podcast_getSubscription(podcast_current_feed_index);
            int count = feed ? feed->episode_count : 0;

            // Force redraw when downloads are active to show progress
            int queue_count = 0;
            PodcastDownloadItem* queue = Podcast_getDownloadQueue(&queue_count);
            for (int i = 0; i < queue_count; i++) {
                if (queue[i].status == PODCAST_DOWNLOAD_DOWNLOADING ||
                    queue[i].status == PODCAST_DOWNLOAD_PENDING) {
                    dirty = 1;
                    break;
                }
            }

            // Force redraw while title is scrolling to animate
            if (Podcast_isTitleScrolling()) {
                dirty = 1;
            }

            if (PAD_justRepeated(BTN_UP) && count > 0) {
                podcast_episodes_selected = (podcast_episodes_selected > 0) ? podcast_episodes_selected - 1 : count - 1;
                Podcast_clearTitleScroll();  // Clear scroll state on selection change
                dirty = 1;
            }
            else if (PAD_justRepeated(BTN_DOWN) && count > 0) {
                podcast_episodes_selected = (podcast_episodes_selected < count - 1) ? podcast_episodes_selected + 1 : 0;
                Podcast_clearTitleScroll();  // Clear scroll state on selection change
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A)) {
                if (count > 0 && feed) {
                    podcast_current_episode_index = podcast_episodes_selected;
                    PodcastEpisode* ep = Podcast_getEpisode(podcast_current_feed_index, podcast_current_episode_index);

                    if (ep) {
                        // Check if episode is currently downloading/queued
                        int dl_progress = 0;
                        int dl_status = Podcast_getEpisodeDownloadStatus(feed->feed_url, ep->guid, &dl_progress);

                        if (dl_status == PODCAST_DOWNLOAD_DOWNLOADING || dl_status == PODCAST_DOWNLOAD_PENDING) {
                            // Episode is downloading/queued - ignore A button (X CANCEL shown instead)
                            // Do nothing
                        } else if (Podcast_episodeFileExists(feed, podcast_current_episode_index)) {
                            // File exists locally - play it
                            if (Podcast_play(feed, podcast_current_episode_index) == 0) {
                                Podcast_clearTitleScroll();  // Clear episode list scroll before transition
                                app_state = STATE_PODCAST_PLAYING;
                                last_input_time = SDL_GetTicks();  // Start screen-off timer
                            } else {
                                snprintf(podcast_toast_message, sizeof(podcast_toast_message), "Failed to play");
                                podcast_toast_time = SDL_GetTicks();
                            }
                        } else {
                            // File doesn't exist - ensure WiFi connected then start download
                            if (!ensure_wifi_connected(screen, show_setting)) {
                                snprintf(podcast_toast_message, sizeof(podcast_toast_message), "No network connection");
                                podcast_toast_time = SDL_GetTicks();
                            } else if (Podcast_downloadEpisode(feed, podcast_current_episode_index) == 0) {
                                snprintf(podcast_toast_message, sizeof(podcast_toast_message), "Downloading...");
                                podcast_toast_time = SDL_GetTicks();
                            } else {
                                snprintf(podcast_toast_message, sizeof(podcast_toast_message), "Download failed");
                                podcast_toast_time = SDL_GetTicks();
                            }
                        }
                    }
                }
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_X)) {
                if (count > 0 && feed) {
                    PodcastEpisode* ep = Podcast_getEpisode(podcast_current_feed_index, podcast_episodes_selected);

                    if (ep) {
                        // Check if episode is downloading/queued - if so, cancel it
                        int dl_progress = 0;
                        int dl_status = Podcast_getEpisodeDownloadStatus(feed->feed_url, ep->guid, &dl_progress);

                        if (dl_status == PODCAST_DOWNLOAD_DOWNLOADING || dl_status == PODCAST_DOWNLOAD_PENDING) {
                            if (Podcast_cancelEpisodeDownload(feed->feed_url, ep->guid) == 0) {
                                snprintf(podcast_toast_message, sizeof(podcast_toast_message), "Download cancelled");
                                podcast_toast_time = SDL_GetTicks();
                            } else {
                                snprintf(podcast_toast_message, sizeof(podcast_toast_message), "Cancel failed");
                                podcast_toast_time = SDL_GetTicks();
                            }
                        }
                        // X button does nothing if not downloading/queued
                    }
                }
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                app_state = STATE_PODCAST_MENU;
                dirty = 1;
            }
        }
        else if (app_state == STATE_PODCAST_BUFFERING) {
            // Disable autosleep while buffering/playing podcast
            if (!autosleep_disabled) {
                PWR_disableAutosleep();
                autosleep_disabled = true;
            }

            Podcast_update();

            // Check if buffering is complete - transition to playing
            if (!Podcast_isBuffering() && Podcast_isActive()) {
                app_state = STATE_PODCAST_PLAYING;
                last_input_time = SDL_GetTicks();  // Start screen-off timer
                dirty = 1;
            }

            // Cancel button - stop and go back
            if (PAD_justPressed(BTN_B)) {
                Podcast_stop();
                Podcast_clearArtwork();
                // Re-enable autosleep
                if (autosleep_disabled) {
                    PWR_enableAutosleep();
                    autosleep_disabled = false;
                }
                app_state = STATE_PODCAST_EPISODES;
                dirty = 1;
            }

            // Keep refreshing to update buffer progress
            dirty = 1;
        }
        else if (app_state == STATE_PODCAST_PLAYING) {
            // Disable autosleep while playing podcast
            if (!autosleep_disabled) {
                PWR_disableAutosleep();
                autosleep_disabled = true;
            }

            // Handle screen off hint timeout - turn off screen after hint displays
            if (screen_off_hint_active) {
                uint32_t now = SDL_GetTicks();
                time_t now_wallclock = time(NULL);
                bool timeout_sdl = (now - screen_off_hint_start >= SCREEN_OFF_HINT_DURATION_MS);
                bool timeout_wallclock = (now_wallclock - screen_off_hint_start_wallclock >= (SCREEN_OFF_HINT_DURATION_MS / 1000));
                if (timeout_sdl || timeout_wallclock) {
                    screen_off_hint_active = false;
                    screen_off = true;
                    PLAT_enableBacklight(0);
                    dirty = 1;
                }
            }
            // Handle screen off mode - require SELECT + A to wake (prevents accidental wake in pocket)
            else if (screen_off) {
                if (PAD_isPressed(BTN_SELECT) && PAD_isPressed(BTN_A)) {
                    screen_off = false;
                    PLAT_enableBacklight(1);
                    last_input_time = SDL_GetTicks();
                    dirty = 1;
                }
                // Still update podcast while screen is off
                Podcast_update();

                // Check if streaming stopped while screen is off
                if (Podcast_isStreaming() && !Podcast_isActive()) {
                    Podcast_clearArtwork();
                    if (autosleep_disabled) {
                        PWR_enableAutosleep();
                        autosleep_disabled = false;
                    }
                    screen_off = false;
                    PLAT_enableBacklight(1);
                    app_state = STATE_PODCAST_EPISODES;
                    dirty = 1;
                }
            }
            else {
                // Normal input handling when screen is on
                if (PAD_justPressed(BTN_A)) {
                    // Toggle pause (only works for local files)
                    if (!Podcast_isStreaming()) {
                        if (Podcast_isPaused()) {
                            Podcast_resume();
                        } else {
                            Podcast_pause();
                        }
                    }
                    last_input_time = SDL_GetTicks();
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_B)) {
                    Podcast_stop();
                    Podcast_clearArtwork();
                    // Re-enable autosleep and backlight
                    if (autosleep_disabled) {
                        PWR_enableAutosleep();
                        autosleep_disabled = false;
                    }
                    if (screen_off) {
                        screen_off = false;
                        PLAT_enableBacklight(1);
                    }
                    app_state = STATE_PODCAST_EPISODES;
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
                else if (PAD_justRepeated(BTN_LEFT)) {
                    // Rewind 10 seconds (only for local files)
                    if (!Podcast_isStreaming()) {
                        int pos_ms = Podcast_getPosition();
                        int new_pos_ms = pos_ms - 10000;
                        if (new_pos_ms < 0) new_pos_ms = 0;
                        Podcast_seek(new_pos_ms);
                    }
                    last_input_time = SDL_GetTicks();
                    dirty = 1;
                }
                else if (PAD_justRepeated(BTN_RIGHT)) {
                    // Fast forward 30 seconds (only for local files)
                    if (!Podcast_isStreaming()) {
                        int pos_ms = Podcast_getPosition();
                        int dur_ms = Podcast_getDuration();
                        int new_pos_ms = pos_ms + 30000;
                        if (new_pos_ms > dur_ms) new_pos_ms = dur_ms;
                        Podcast_seek(new_pos_ms);
                    }
                    last_input_time = SDL_GetTicks();
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_UP) || PAD_justPressed(BTN_R1)) {
                    // Next episode (older - higher index) - Up or R1
                    if (!Podcast_isStreaming()) {
                        PodcastFeed* feed = Podcast_getSubscription(podcast_current_feed_index);
                        if (feed && podcast_current_episode_index < feed->episode_count - 1) {
                            Podcast_stop();
                            podcast_current_episode_index++;
                            if (Podcast_episodeFileExists(feed, podcast_current_episode_index)) {
                                Podcast_clearArtwork();
                                Podcast_play(feed, podcast_current_episode_index);
                            } else {
                                // Episode not downloaded, go back to list
                                Podcast_clearArtwork();
                                app_state = STATE_PODCAST_EPISODES;
                                podcast_episodes_selected = podcast_current_episode_index;
                            }
                        }
                    }
                    last_input_time = SDL_GetTicks();
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_DOWN) || PAD_justPressed(BTN_L1)) {
                    // Previous episode (newer - lower index) - Down or L1
                    if (!Podcast_isStreaming()) {
                        PodcastFeed* feed = Podcast_getSubscription(podcast_current_feed_index);
                        if (feed && podcast_current_episode_index > 0) {
                            Podcast_stop();
                            podcast_current_episode_index--;
                            if (Podcast_episodeFileExists(feed, podcast_current_episode_index)) {
                                Podcast_clearArtwork();
                                Podcast_play(feed, podcast_current_episode_index);
                            } else {
                                // Episode not downloaded, go back to list
                                Podcast_clearArtwork();
                                app_state = STATE_PODCAST_EPISODES;
                                podcast_episodes_selected = podcast_current_episode_index;
                            }
                        }
                    }
                    last_input_time = SDL_GetTicks();
                    dirty = 1;
                }

                // Update podcast state
                Podcast_update();

                // Animate title scroll if needed (only when screen is on)
                if (Podcast_isTitleScrolling()) {
                    Podcast_animateTitleScroll();
                }

                // Check if streaming stopped unexpectedly
                if (Podcast_isStreaming() && !Podcast_isActive()) {
                    // Stream ended or errored - re-enable autosleep
                    Podcast_clearArtwork();
                    if (autosleep_disabled) {
                        PWR_enableAutosleep();
                        autosleep_disabled = false;
                    }
                    app_state = STATE_PODCAST_EPISODES;
                    dirty = 1;
                }

                // Auto screen-off after inactivity (only while playing)
                if (Podcast_isActive() && !screen_off_hint_active) {
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

            // Keep refreshing to update progress (but skip when screen is off)
            if (!screen_off && !screen_off_hint_active) {
                dirty = 1;
            }
        }
        // ============================================================================
        // End Podcast States
        // ============================================================================
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
            // Process async update check
            SelfUpdate_update();
            const SelfUpdateStatus* status = SelfUpdate_getStatus();

            // Keep refreshing while checking for updates
            if (status->state == SELFUPDATE_STATE_CHECKING) {
                dirty = 1;
            }

            if (PAD_justPressed(BTN_A)) {
                if (status->update_available) {
                    // Start update
                    SelfUpdate_startUpdate();
                    app_state = STATE_APP_UPDATING;
                    dirty = 1;
                } else if (status->state != SELFUPDATE_STATE_CHECKING) {
                    // No update detected - try to connect WiFi and check for updates
                    if (ensure_wifi_connected(screen, show_setting)) {
                        SelfUpdate_checkForUpdate();
                    }
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
                    render_menu(screen, show_setting, menu_selected,
                                menu_toast_message, menu_toast_time);
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
                // Podcast states
                case STATE_PODCAST_MENU:
                    render_podcast_list(screen, show_setting,
                                        podcast_menu_selected, &podcast_menu_scroll);
                    break;
                case STATE_PODCAST_MANAGE:
                    render_podcast_manage(screen, show_setting, podcast_manage_selected,
                                          Podcast_getSubscriptionCount());
                    break;
                case STATE_PODCAST_SUBSCRIPTIONS:
                    render_podcast_subscriptions(screen, show_setting,
                                                  podcast_subscriptions_selected,
                                                  &podcast_subscriptions_scroll);
                    break;
                case STATE_PODCAST_TOP_SHOWS:
                    render_podcast_top_shows(screen, show_setting,
                                              podcast_top_shows_selected,
                                              &podcast_top_shows_scroll,
                                              podcast_toast_message, podcast_toast_time);
                    break;
                case STATE_PODCAST_SEARCH_RESULTS:
                    render_podcast_search_results(screen, show_setting,
                                                   podcast_search_selected,
                                                   &podcast_search_scroll,
                                                   podcast_toast_message, podcast_toast_time);
                    break;
                case STATE_PODCAST_EPISODES: {
                    render_podcast_episodes(screen, show_setting, podcast_current_feed_index,
                                            podcast_episodes_selected,
                                            &podcast_episodes_scroll,
                                            podcast_toast_message, podcast_toast_time);
                    break;
                }
                case STATE_PODCAST_BUFFERING: {
                    int buffer_pct = (int)(Radio_getBufferLevel() * 100);
                    render_podcast_buffering(screen, show_setting, podcast_current_feed_index,
                                              podcast_current_episode_index, buffer_pct);
                    break;
                }
                case STATE_PODCAST_PLAYING: {
                    render_podcast_playing(screen, show_setting, podcast_current_feed_index,
                                           podcast_current_episode_index);
                    break;
                }
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

            // Keep refreshing while toast is visible (Main Menu)
            if (app_state == STATE_MENU && menu_toast_message[0] != '\0') {
                if (SDL_GetTicks() - menu_toast_time < MENU_TOAST_DURATION) {
                    dirty = 1;
                } else {
                    // Clear toast after duration
                    menu_toast_message[0] = '\0';
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
    Podcast_cleanup();
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
