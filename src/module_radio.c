#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "defines.h"
#include "api.h"
#include "config.h"
#include "settings.h"
#include "module_common.h"
#include "module_radio.h"
#include "player.h"
#include "radio.h"
#include "radio_curated.h"
#include "ui_radio.h"
#include "ui_album_art.h"
#include "ui_main.h"
#include "wifi.h"

// Screen off hint duration
#define SCREEN_OFF_HINT_DURATION_MS 4000

// Toast duration
#define TOAST_DURATION 3000

// Internal states
typedef enum {
    RADIO_INTERNAL_LIST,
    RADIO_INTERNAL_PLAYING,
    RADIO_INTERNAL_ADD_COUNTRY,
    RADIO_INTERNAL_ADD_STATIONS,
    RADIO_INTERNAL_HELP
} RadioInternalState;

// Module state
static int radio_selected = 0;
static int radio_scroll = 0;
static char radio_toast_message[128] = "";
static uint32_t radio_toast_time = 0;

// Add stations UI state
static int add_country_selected = 0;
static int add_country_scroll = 0;
static int add_station_selected = 0;
static int add_station_scroll = 0;
static const char* add_selected_country_code = NULL;
static bool add_station_checked[256];
static int help_scroll = 0;

// Screen off state
static bool screen_off = false;
static bool screen_off_hint_active = false;
static uint32_t screen_off_hint_start = 0;
static time_t screen_off_hint_start_wallclock = 0;
static uint32_t last_input_time = 0;

ModuleExitReason RadioModule_run(SDL_Surface* screen) {
    Radio_init();

    RadioInternalState state = RADIO_INTERNAL_LIST;
    int dirty = 1;
    int show_setting = 0;

    screen_off = false;
    screen_off_hint_active = false;
    last_input_time = SDL_GetTicks();
    radio_toast_message[0] = '\0';
    memset(add_station_checked, 0, sizeof(add_station_checked));

    while (1) {
        uint32_t frame_start = SDL_GetTicks();
        PAD_poll();

        // Handle global input (skip if screen off or hint active)
        if (!screen_off && !screen_off_hint_active) {
            // Map internal state to app state for controls help context
            int app_state_for_help;
            switch (state) {
                case RADIO_INTERNAL_LIST: app_state_for_help = 3; break;  // STATE_RADIO_LIST
                case RADIO_INTERNAL_PLAYING: app_state_for_help = 4; break;  // STATE_RADIO_PLAYING
                case RADIO_INTERNAL_ADD_COUNTRY: app_state_for_help = 5; break;  // STATE_RADIO_ADD
                case RADIO_INTERNAL_ADD_STATIONS: app_state_for_help = 6; break;  // STATE_RADIO_ADD_STATIONS
                case RADIO_INTERNAL_HELP: app_state_for_help = 7; break;  // STATE_RADIO_HELP
                default: app_state_for_help = 3; break;
            }

            GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, app_state_for_help);
            if (global.should_quit) {
                Radio_quit();
                return MODULE_EXIT_QUIT;
            }
            if (global.input_consumed) {
                if (global.dirty) dirty = 1;
                GFX_sync();
                continue;
            }
        }

        // =========================================
        // RADIO LIST STATE
        // =========================================
        if (state == RADIO_INTERNAL_LIST) {
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
                if (!Wifi_ensureConnected(screen, show_setting)) {
                    snprintf(radio_toast_message, sizeof(radio_toast_message), "Internet connection required");
                    radio_toast_time = SDL_GetTicks();
                    dirty = 1;
                } else if (Radio_play(stations[radio_selected].url) == 0) {
                    last_input_time = SDL_GetTicks();
                    state = RADIO_INTERNAL_PLAYING;
                    dirty = 1;
                }
            }
            else if (PAD_justPressed(BTN_B)) {
                Radio_quit();
                return MODULE_EXIT_TO_MENU;
            }
            else if (PAD_justPressed(BTN_Y)) {
                add_country_selected = 0;
                add_country_scroll = 0;
                state = RADIO_INTERNAL_ADD_COUNTRY;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_X)) {
                state = RADIO_INTERNAL_HELP;
                dirty = 1;
            }
        }
        // =========================================
        // RADIO PLAYING STATE
        // =========================================
        else if (state == RADIO_INTERNAL_PLAYING) {
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
                Radio_update();
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
                    RadioStation* stations;
                    int station_count = Radio_getStations(&stations);
                    if (hid_event == USB_HID_EVENT_PLAY_PAUSE) {
                        RadioState rstate = Radio_getState();
                        if (rstate == RADIO_STATE_PLAYING || rstate == RADIO_STATE_BUFFERING || rstate == RADIO_STATE_CONNECTING) {
                            Radio_stop();
                        } else {
                            const char* url = Radio_getCurrentUrl();
                            if (url && url[0] != '\0') {
                                Radio_play(url);
                            }
                        }
                    } else if (hid_event == USB_HID_EVENT_NEXT_TRACK || hid_event == USB_HID_EVENT_PREV_TRACK) {
                        if (station_count > 1) {
                            const char* current_url = Radio_getCurrentUrl();
                            int current_idx = 0;
                            for (int i = 0; i < station_count; i++) {
                                if (strcmp(stations[i].url, current_url) == 0) {
                                    current_idx = i;
                                    break;
                                }
                            }
                            int new_idx = (hid_event == USB_HID_EVENT_NEXT_TRACK)
                                ? (current_idx + 1) % station_count
                                : (current_idx - 1 + station_count) % station_count;
                            Radio_stop();
                            Radio_play(stations[new_idx].url);
                        }
                    }
                }
                Radio_update();
                GFX_sync();
                continue;
            }

            // Reset input timer on any button press
            if (PAD_anyPressed()) {
                last_input_time = SDL_GetTicks();
            }

            // Station switching
            RadioStation* stations;
            int station_count = Radio_getStations(&stations);

            if (PAD_justPressed(BTN_UP) || PAD_justPressed(BTN_R1)) {
                if (station_count > 1) {
                    radio_selected = (radio_selected + 1) % station_count;
                    Radio_stop();
                    Radio_play(stations[radio_selected].url);
                    dirty = 1;
                }
            }
            else if (PAD_justPressed(BTN_DOWN) || PAD_justPressed(BTN_L1)) {
                if (station_count > 1) {
                    radio_selected = (radio_selected - 1 + station_count) % station_count;
                    Radio_stop();
                    Radio_play(stations[radio_selected].url);
                    dirty = 1;
                }
            }
            else if (PAD_justPressed(BTN_B)) {
                // B always goes back to list (stop radio first if playing)
                RadioState rstate = Radio_getState();
                if (rstate == RADIO_STATE_PLAYING || rstate == RADIO_STATE_BUFFERING || rstate == RADIO_STATE_CONNECTING) {
                    Radio_stop();
                }
                cleanup_album_art_background();
                RadioStatus_clear();
                ModuleCommon_setAutosleepDisabled(false);
                state = RADIO_INTERNAL_LIST;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A)) {
                // A toggles play/pause
                RadioState rstate = Radio_getState();
                if (rstate == RADIO_STATE_PLAYING || rstate == RADIO_STATE_BUFFERING || rstate == RADIO_STATE_CONNECTING) {
                    // Playing - stop it
                    Radio_stop();
                    dirty = 1;
                } else {
                    // Stopped - resume playing
                    const char* url = Radio_getCurrentUrl();
                    if (url && url[0] != '\0') {
                        Radio_play(url);
                        dirty = 1;
                    }
                }
            }
            else if (PAD_tappedSelect(SDL_GetTicks())) {
                screen_off_hint_active = true;
                screen_off_hint_start = SDL_GetTicks();
                screen_off_hint_start_wallclock = time(NULL);
                GFX_clearLayers(LAYER_SCROLLTEXT);
                PLAT_clearLayers(LAYER_BUFFER);
                PLAT_GPU_Flip();
                dirty = 1;
            }

            Radio_update();

            // Auto screen-off after inactivity
            if (Radio_getState() == RADIO_STATE_PLAYING && !screen_off_hint_active) {
                uint32_t screen_timeout_ms = Settings_getScreenOffTimeout() * 1000;
                if (screen_timeout_ms > 0 && last_input_time > 0) {
                    uint32_t now = SDL_GetTicks();
                    if (now - last_input_time >= screen_timeout_ms) {
                        screen_off_hint_active = true;
                        screen_off_hint_start = SDL_GetTicks();
                        screen_off_hint_start_wallclock = time(NULL);
                        GFX_clearLayers(LAYER_SCROLLTEXT);
                        PLAT_clearLayers(LAYER_BUFFER);
                        PLAT_GPU_Flip();
                        dirty = 1;
                    }
                }
            }

            // Animate radio GPU layer
            if (!screen_off && !screen_off_hint_active) {
                if (RadioStatus_needsRefresh()) {
                    RadioStatus_renderGPU();
                }
            }
        }
        // =========================================
        // ADD COUNTRY STATE
        // =========================================
        else if (state == RADIO_INTERNAL_ADD_COUNTRY) {
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
                const CuratedCountry* countries = Radio_getCuratedCountries();
                add_selected_country_code = countries[add_country_selected].code;
                add_station_selected = 0;
                add_station_scroll = 0;
                memset(add_station_checked, 0, sizeof(add_station_checked));
                int sc = 0;
                const CuratedStation* cs = Radio_getCuratedStations(add_selected_country_code, &sc);
                for (int i = 0; i < sc && i < 256; i++) {
                    add_station_checked[i] = Radio_stationExists(cs[i].url);
                }
                state = RADIO_INTERNAL_ADD_STATIONS;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                state = RADIO_INTERNAL_LIST;
                dirty = 1;
            }
        }
        // =========================================
        // ADD STATIONS STATE
        // =========================================
        else if (state == RADIO_INTERNAL_ADD_STATIONS) {
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
                if (add_station_selected < 256) {
                    add_station_checked[add_station_selected] = !add_station_checked[add_station_selected];
                    dirty = 1;
                }
            }
            else if (PAD_justPressed(BTN_X)) {
                // Apply changes
                int added = 0;
                int removed = 0;
                for (int i = 0; i < station_count && i < 256; i++) {
                    bool exists = Radio_stationExists(stations[i].url);
                    if (add_station_checked[i] && !exists) {
                        if (Radio_addStation(stations[i].name, stations[i].url, stations[i].genre, stations[i].slogan) >= 0) {
                            added++;
                        }
                    } else if (!add_station_checked[i] && exists) {
                        if (Radio_removeStationByUrl(stations[i].url)) {
                            removed++;
                        }
                    }
                }
                if (added > 0 || removed > 0) {
                    Radio_saveStations();
                }
                memset(add_station_checked, 0, sizeof(add_station_checked));
                state = RADIO_INTERNAL_LIST;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                state = RADIO_INTERNAL_ADD_COUNTRY;
                dirty = 1;
            }
        }
        // =========================================
        // HELP STATE
        // =========================================
        else if (state == RADIO_INTERNAL_HELP) {
            int scroll_step = SCALE1(18);

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
                help_scroll = 0;
                state = RADIO_INTERNAL_ADD_COUNTRY;
                dirty = 1;
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
            } else {
                switch (state) {
                    case RADIO_INTERNAL_LIST:
                        render_radio_list(screen, show_setting, radio_selected, &radio_scroll,
                                          radio_toast_message, radio_toast_time);
                        break;
                    case RADIO_INTERNAL_PLAYING:
                        render_radio_playing(screen, show_setting, radio_selected);
                        break;
                    case RADIO_INTERNAL_ADD_COUNTRY:
                        render_radio_add(screen, show_setting, add_country_selected, &add_country_scroll);
                        break;
                    case RADIO_INTERNAL_ADD_STATIONS:
                        render_radio_add_stations(screen, show_setting, add_selected_country_code,
                                                  add_station_selected, &add_station_scroll, add_station_checked);
                        break;
                    case RADIO_INTERNAL_HELP:
                        render_radio_help(screen, show_setting, &help_scroll);
                        break;
                }
            }

            if (show_setting) {
                GFX_blitHardwareHints(screen, show_setting);
            }

            GFX_flip(screen);
            dirty = 0;

            // Keep refreshing while toast is visible
            if (state == RADIO_INTERNAL_LIST && radio_toast_message[0] != '\0') {
                if (SDL_GetTicks() - radio_toast_time < TOAST_DURATION) {
                    dirty = 1;
                } else {
                    radio_toast_message[0] = '\0';
                }
            }
        } else if (!screen_off) {
            GFX_sync();
        }
    }
}
