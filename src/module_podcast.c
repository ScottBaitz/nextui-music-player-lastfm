#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "defines.h"
#include "api.h"
#include "config.h"
#include "settings.h"
#include "module_common.h"
#include "module_podcast.h"
#include "podcast.h"
#include "player.h"
#include "keyboard.h"
#include "ui_podcast.h"
#include "ui_radio.h"
#include "ui_main.h"
#include "ui_utils.h"
#include "wifi.h"

// Screen off hint duration
#define SCREEN_OFF_HINT_DURATION_MS 4000

// Toast duration
#define TOAST_DURATION 3000

// Internal states
typedef enum {
    PODCAST_INTERNAL_MENU,
    PODCAST_INTERNAL_MANAGE,
    PODCAST_INTERNAL_SUBSCRIPTIONS,
    PODCAST_INTERNAL_TOP_SHOWS,
    PODCAST_INTERNAL_SEARCH_RESULTS,
    PODCAST_INTERNAL_EPISODES,
    PODCAST_INTERNAL_BUFFERING,
    PODCAST_INTERNAL_SEEKING,
    PODCAST_INTERNAL_PLAYING
} PodcastInternalState;

// Module state
static int podcast_menu_selected = 0;
static int podcast_menu_scroll = 0;
static int podcast_manage_selected = 0;
static int podcast_subscriptions_selected = 0;
static int podcast_subscriptions_scroll = 0;
static int podcast_top_shows_selected = 0;
static int podcast_top_shows_scroll = 0;
static int podcast_search_selected = 0;
static int podcast_search_scroll = 0;
static char podcast_search_query[256] = "";
static int podcast_episodes_selected = 0;
static int podcast_episodes_scroll = 0;
static int podcast_current_feed_index = -1;
static int podcast_current_episode_index = -1;
static char podcast_toast_message[128] = "";
static uint32_t podcast_toast_time = 0;

// Periodic progress saving
static uint32_t last_progress_save_time = 0;
#define PROGRESS_SAVE_INTERVAL_MS 30000  // 30 seconds

// Confirmation dialog state
static bool show_confirm = false;
static int confirm_target_index = -1;
static char confirm_podcast_name[256] = "";
static int confirm_return_state = 0;  // 0 = menu, 1 = top_shows, 2 = search_results

// Screen off state
static bool screen_off = false;
static bool screen_off_hint_active = false;
static uint32_t screen_off_hint_start = 0;
static time_t screen_off_hint_start_wallclock = 0;
static uint32_t last_input_time = 0;

ModuleExitReason PodcastModule_run(SDL_Surface* screen) {
    Podcast_init();
    Keyboard_init();

    PodcastInternalState state = PODCAST_INTERNAL_MENU;
    int dirty = 1;
    int show_setting = 0;

    screen_off = false;
    screen_off_hint_active = false;
    last_input_time = SDL_GetTicks();
    podcast_toast_message[0] = '\0';
    show_confirm = false;
    podcast_menu_selected = 0;
    podcast_menu_scroll = 0;

    while (1) {
        uint32_t frame_start = SDL_GetTicks();
        PAD_poll();

        // Handle confirmation dialog
        if (show_confirm) {
            if (PAD_justPressed(BTN_A)) {
                // Confirm unsubscribe
                Podcast_unsubscribe(confirm_target_index);
                if (confirm_return_state == 0) {
                    // From main menu
                    int count = Podcast_getSubscriptionCount();
                    if (podcast_menu_selected >= count && count > 0) {
                        podcast_menu_selected = count - 1;
                    } else if (count == 0) {
                        podcast_menu_selected = 0;
                    }
                }
                snprintf(podcast_toast_message, sizeof(podcast_toast_message), "Unsubscribed");
                podcast_toast_time = SDL_GetTicks();
                show_confirm = false;
                dirty = 1;
                GFX_sync();
                continue;
            } else if (PAD_justPressed(BTN_B)) {
                show_confirm = false;
                dirty = 1;
                GFX_sync();
                continue;
            }
            // Render confirmation dialog overlay
            GFX_clear(screen);
            render_podcast_confirm(screen, confirm_podcast_name);
            GFX_flip(screen);
            GFX_sync();
            continue;
        }

        // Handle global input (skip if screen off or hint active)
        if (!screen_off && !screen_off_hint_active) {
            int app_state_for_help;
            switch (state) {
                case PODCAST_INTERNAL_MENU: app_state_for_help = 30; break;
                case PODCAST_INTERNAL_MANAGE: app_state_for_help = 31; break;
                case PODCAST_INTERNAL_SUBSCRIPTIONS: app_state_for_help = 32; break;
                case PODCAST_INTERNAL_TOP_SHOWS: app_state_for_help = 33; break;
                case PODCAST_INTERNAL_SEARCH_RESULTS: app_state_for_help = 34; break;
                case PODCAST_INTERNAL_EPISODES: app_state_for_help = 35; break;
                case PODCAST_INTERNAL_BUFFERING: app_state_for_help = 36; break;
                case PODCAST_INTERNAL_SEEKING: app_state_for_help = 37; break;
                case PODCAST_INTERNAL_PLAYING: app_state_for_help = 37; break;
                default: app_state_for_help = 30; break;
            }

            GlobalInputResult global = ModuleCommon_handleGlobalInput(screen, &show_setting, app_state_for_help);
            if (global.should_quit) {
                Podcast_cleanup();
                return MODULE_EXIT_QUIT;
            }
            if (global.input_consumed) {
                if (global.dirty) dirty = 1;
                GFX_sync();
                continue;
            }
        }

        // =========================================
        // PODCAST MENU STATE
        // =========================================
        if (state == PODCAST_INTERNAL_MENU) {
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
                podcast_current_feed_index = podcast_menu_selected;
                podcast_episodes_selected = 0;
                podcast_episodes_scroll = 0;
                Podcast_clearTitleScroll();
                podcast_toast_message[0] = '\0';
                PLAT_clearLayers(5);
                state = PODCAST_INTERNAL_EPISODES;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_X) && count > 0) {
                PodcastFeed* feed = Podcast_getSubscription(podcast_menu_selected);
                if (feed) {
                    strncpy(confirm_podcast_name, feed->title, sizeof(confirm_podcast_name) - 1);
                    confirm_podcast_name[sizeof(confirm_podcast_name) - 1] = '\0';
                    confirm_target_index = podcast_menu_selected;
                    confirm_return_state = 0;
                    show_confirm = true;
                    dirty = 1;
                }
            }
            else if (PAD_justPressed(BTN_Y)) {
                podcast_manage_selected = 0;
                podcast_toast_message[0] = '\0';
                PLAT_clearLayers(5);
                state = PODCAST_INTERNAL_MANAGE;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                podcast_toast_message[0] = '\0';
                PLAT_clearLayers(5);
                Podcast_cleanup();
                return MODULE_EXIT_TO_MENU;
            }
        }
        // =========================================
        // MANAGE STATE
        // =========================================
        else if (state == PODCAST_INTERNAL_MANAGE) {
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
                        if (!Wifi_ensureConnected(screen, show_setting)) {
                            strncpy(podcast_toast_message, "Internet connection required", sizeof(podcast_toast_message) - 1);
                            podcast_toast_time = SDL_GetTicks();
                            dirty = 1;
                            break;
                        }
                        char* query = Keyboard_open("Search podcasts");
                        PAD_poll(); PAD_reset();
                        SDL_Delay(100);
                        PAD_poll(); PAD_reset();
                        if (query && query[0]) {
                            strncpy(podcast_search_query, query, sizeof(podcast_search_query) - 1);
                            Podcast_startSearch(podcast_search_query);
                            podcast_search_selected = 0;
                            podcast_search_scroll = 0;
                            podcast_toast_message[0] = '\0';
                            state = PODCAST_INTERNAL_SEARCH_RESULTS;
                        }
                        if (query) free(query);
                        dirty = 1;
                        break;
                    }
                    case PODCAST_MANAGE_TOP_SHOWS:
                        if (!Wifi_ensureConnected(screen, show_setting)) {
                            strncpy(podcast_toast_message, "Internet connection required", sizeof(podcast_toast_message) - 1);
                            podcast_toast_time = SDL_GetTicks();
                            dirty = 1;
                            break;
                        }
                        Podcast_loadCharts(NULL);
                        podcast_top_shows_selected = 0;
                        podcast_top_shows_scroll = 0;
                        podcast_toast_message[0] = '\0';
                        state = PODCAST_INTERNAL_TOP_SHOWS;
                        dirty = 1;
                        break;
                }
            }
            else if (PAD_justPressed(BTN_B)) {
                state = PODCAST_INTERNAL_MENU;
                dirty = 1;
            }
        }
        // =========================================
        // SUBSCRIPTIONS STATE
        // =========================================
        else if (state == PODCAST_INTERNAL_SUBSCRIPTIONS) {
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
                podcast_current_feed_index = podcast_subscriptions_selected;
                podcast_episodes_selected = 0;
                podcast_episodes_scroll = 0;
                Podcast_clearTitleScroll();
                state = PODCAST_INTERNAL_EPISODES;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_X) && count > 0) {
                Podcast_unsubscribe(podcast_subscriptions_selected);
                if (podcast_subscriptions_selected >= Podcast_getSubscriptionCount()) {
                    podcast_subscriptions_selected = Podcast_getSubscriptionCount() - 1;
                    if (podcast_subscriptions_selected < 0) podcast_subscriptions_selected = 0;
                }
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                state = PODCAST_INTERNAL_MANAGE;
                dirty = 1;
            }
        }
        // =========================================
        // TOP SHOWS STATE
        // =========================================
        else if (state == PODCAST_INTERNAL_TOP_SHOWS) {
            Podcast_update();
            const PodcastChartsStatus* chart_status = Podcast_getChartsStatus();

            if (chart_status->loading || chart_status->completed) dirty = 1;
            if (podcast_toast_message[0] && (SDL_GetTicks() - podcast_toast_time < TOAST_DURATION)) dirty = 1;
            if (Podcast_isTitleScrolling()) Podcast_animateTitleScroll();

            if (!chart_status->loading) {
                int count = 0;
                Podcast_getTopShows(&count);

                if (PAD_justRepeated(BTN_UP) && count > 0) {
                    podcast_top_shows_selected = (podcast_top_shows_selected > 0) ? podcast_top_shows_selected - 1 : count - 1;
                    Podcast_clearTitleScroll();
                    dirty = 1;
                }
                else if (PAD_justRepeated(BTN_DOWN) && count > 0) {
                    podcast_top_shows_selected = (podcast_top_shows_selected < count - 1) ? podcast_top_shows_selected + 1 : 0;
                    Podcast_clearTitleScroll();
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_A) && count > 0) {
                    PodcastChartItem* items = Podcast_getTopShows(&count);
                    if (podcast_top_shows_selected < count) {
                        bool already_subscribed = Podcast_isSubscribedByItunesId(items[podcast_top_shows_selected].itunes_id);
                        if (already_subscribed) {
                            // Find subscription index and show confirm dialog
                            int sub_count = 0;
                            PodcastFeed* feeds = Podcast_getSubscriptions(&sub_count);
                            for (int si = 0; si < sub_count; si++) {
                                if (feeds[si].itunes_id[0] && strcmp(feeds[si].itunes_id, items[podcast_top_shows_selected].itunes_id) == 0) {
                                    strncpy(confirm_podcast_name, items[podcast_top_shows_selected].title, sizeof(confirm_podcast_name) - 1);
                                    confirm_podcast_name[sizeof(confirm_podcast_name) - 1] = '\0';
                                    confirm_target_index = si;
                                    confirm_return_state = 1;
                                    show_confirm = true;
                                    break;
                                }
                            }
                        } else {
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
                else if (PAD_justPressed(BTN_X)) {
                    // Refresh charts - clear cache and reload
                    if (!Wifi_ensureConnected(screen, show_setting)) {
                        strncpy(podcast_toast_message, "Internet connection required", sizeof(podcast_toast_message) - 1);
                        podcast_toast_time = SDL_GetTicks();
                    } else {
                        Podcast_clearChartsCache();
                        Podcast_loadCharts(NULL);
                        podcast_top_shows_selected = 0;
                        podcast_top_shows_scroll = 0;
                        strncpy(podcast_toast_message, "Refreshing...", sizeof(podcast_toast_message) - 1);
                        podcast_toast_time = SDL_GetTicks();
                    }
                    dirty = 1;
                }
            }

            if (PAD_justPressed(BTN_B)) {
                Podcast_clearTitleScroll();
                podcast_toast_message[0] = '\0';
                PLAT_clearLayers(5);
                state = PODCAST_INTERNAL_MANAGE;
                dirty = 1;
            }
        }
        // =========================================
        // SEARCH RESULTS STATE
        // =========================================
        else if (state == PODCAST_INTERNAL_SEARCH_RESULTS) {
            Podcast_update();
            const PodcastSearchStatus* search_status = Podcast_getSearchStatus();

            if (search_status->searching || search_status->completed) dirty = 1;
            if (podcast_toast_message[0] && (SDL_GetTicks() - podcast_toast_time < TOAST_DURATION)) dirty = 1;
            if (Podcast_isTitleScrolling()) Podcast_animateTitleScroll();

            if (!search_status->searching) {
                int count = 0;
                Podcast_getSearchResults(&count);

                if (PAD_justRepeated(BTN_UP) && count > 0) {
                    podcast_search_selected = (podcast_search_selected > 0) ? podcast_search_selected - 1 : count - 1;
                    Podcast_clearTitleScroll();
                    dirty = 1;
                }
                else if (PAD_justRepeated(BTN_DOWN) && count > 0) {
                    podcast_search_selected = (podcast_search_selected < count - 1) ? podcast_search_selected + 1 : 0;
                    Podcast_clearTitleScroll();
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_A) && count > 0) {
                    PodcastSearchResult* results = Podcast_getSearchResults(&count);
                    if (podcast_search_selected < count) {
                        bool already_subscribed = results[podcast_search_selected].feed_url[0] &&
                                                   Podcast_isSubscribed(results[podcast_search_selected].feed_url);
                        if (already_subscribed) {
                            // Find subscription index and show confirm dialog
                            int sub_count = 0;
                            PodcastFeed* feeds = Podcast_getSubscriptions(&sub_count);
                            for (int si = 0; si < sub_count; si++) {
                                if (strcmp(feeds[si].feed_url, results[podcast_search_selected].feed_url) == 0) {
                                    strncpy(confirm_podcast_name, results[podcast_search_selected].title, sizeof(confirm_podcast_name) - 1);
                                    confirm_podcast_name[sizeof(confirm_podcast_name) - 1] = '\0';
                                    confirm_target_index = si;
                                    confirm_return_state = 2;
                                    show_confirm = true;
                                    break;
                                }
                            }
                        } else {
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
                Podcast_clearTitleScroll();
                Podcast_cancelSearch();
                podcast_toast_message[0] = '\0';
                PLAT_clearLayers(5);
                state = PODCAST_INTERNAL_MANAGE;
                dirty = 1;
            }
        }
        // =========================================
        // EPISODES STATE
        // =========================================
        else if (state == PODCAST_INTERNAL_EPISODES) {
            PodcastFeed* feed = Podcast_getSubscription(podcast_current_feed_index);
            int count = feed ? feed->episode_count : 0;

            // Force redraw when downloads active
            int queue_count = 0;
            PodcastDownloadItem* queue = Podcast_getDownloadQueue(&queue_count);
            for (int i = 0; i < queue_count; i++) {
                if (queue[i].status == PODCAST_DOWNLOAD_DOWNLOADING || queue[i].status == PODCAST_DOWNLOAD_PENDING) {
                    dirty = 1;
                    break;
                }
            }

            if (Podcast_isTitleScrolling()) dirty = 1;
            if (podcast_toast_message[0] && (SDL_GetTicks() - podcast_toast_time < TOAST_DURATION)) dirty = 1;

            if (PAD_justRepeated(BTN_UP) && count > 0) {
                podcast_episodes_selected = (podcast_episodes_selected > 0) ? podcast_episodes_selected - 1 : count - 1;
                Podcast_clearTitleScroll();
                dirty = 1;
            }
            else if (PAD_justRepeated(BTN_DOWN) && count > 0) {
                podcast_episodes_selected = (podcast_episodes_selected < count - 1) ? podcast_episodes_selected + 1 : 0;
                Podcast_clearTitleScroll();
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_A) && count > 0 && feed) {
                podcast_current_episode_index = podcast_episodes_selected;
                PodcastEpisode* ep = Podcast_getEpisode(podcast_current_feed_index, podcast_current_episode_index);

                if (ep) {
                    int dl_progress = 0;
                    int dl_status = Podcast_getEpisodeDownloadStatus(feed->feed_url, ep->guid, &dl_progress);

                    if (dl_status == PODCAST_DOWNLOAD_DOWNLOADING || dl_status == PODCAST_DOWNLOAD_PENDING) {
                        // Downloading - ignore
                    } else if (Podcast_episodeFileExists(feed, podcast_current_episode_index)) {
                        int load_result = Podcast_loadAndSeek(feed, podcast_current_episode_index);
                        if (load_result >= 0) {
                            Podcast_clearTitleScroll();
                            last_input_time = SDL_GetTicks();
                            last_progress_save_time = SDL_GetTicks();
                            if (load_result == 1) {
                                // Has saved progress — seeking, show player UI while waiting
                                state = PODCAST_INTERNAL_SEEKING;
                            } else {
                                // No saved progress — play immediately
                                Player_play();
                                state = PODCAST_INTERNAL_PLAYING;
                            }
                        } else {
                            snprintf(podcast_toast_message, sizeof(podcast_toast_message), "Failed to play");
                            podcast_toast_time = SDL_GetTicks();
                        }
                    } else {
                        if (!Wifi_ensureConnected(screen, show_setting)) {
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
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_X) && count > 0 && feed) {
                PodcastEpisode* ep = Podcast_getEpisode(podcast_current_feed_index, podcast_episodes_selected);
                if (ep) {
                    int dl_progress = 0;
                    int dl_status = Podcast_getEpisodeDownloadStatus(feed->feed_url, ep->guid, &dl_progress);
                    if (dl_status == PODCAST_DOWNLOAD_DOWNLOADING || dl_status == PODCAST_DOWNLOAD_PENDING) {
                        if (Podcast_cancelEpisodeDownload(feed->feed_url, ep->guid) == 0) {
                            snprintf(podcast_toast_message, sizeof(podcast_toast_message), "Download cancelled");
                        } else {
                            snprintf(podcast_toast_message, sizeof(podcast_toast_message), "Cancel failed");
                        }
                    } else {
                        // Toggle played status
                        if (ep->progress_sec == -1) {
                            ep->progress_sec = 0;
                            Podcast_saveProgress(feed->feed_url, ep->guid, 0);
                            snprintf(podcast_toast_message, sizeof(podcast_toast_message), "Marked as unplayed");
                        } else {
                            ep->progress_sec = -1;
                            Podcast_markAsPlayed(feed->feed_url, ep->guid);
                            snprintf(podcast_toast_message, sizeof(podcast_toast_message), "Marked as played");
                        }
                        Podcast_flushProgress();
                    }
                    podcast_toast_time = SDL_GetTicks();
                }
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                Podcast_clearTitleScroll();
                podcast_toast_message[0] = '\0';
                PLAT_clearLayers(5);
                state = PODCAST_INTERNAL_MENU;
                dirty = 1;
            }
        }
        // =========================================
        // BUFFERING STATE
        // =========================================
        else if (state == PODCAST_INTERNAL_BUFFERING) {
            ModuleCommon_setAutosleepDisabled(true);
            Podcast_update();

            if (!Podcast_isBuffering() && Podcast_isActive()) {
                last_input_time = SDL_GetTicks();
                state = PODCAST_INTERNAL_PLAYING;
                dirty = 1;
            }

            if (PAD_justPressed(BTN_B)) {
                Podcast_stop();
                Podcast_clearArtwork();
                GFX_clearLayers(LAYER_SCROLLTEXT);
                PLAT_clearLayers(LAYER_BUFFER);
                PLAT_GPU_Flip();
                ModuleCommon_setAutosleepDisabled(false);
                state = PODCAST_INTERNAL_EPISODES;
                dirty = 1;
            }

            dirty = 1;  // Keep refreshing
        }
        // =========================================
        // SEEKING STATE (resuming to saved position)
        // =========================================
        else if (state == PODCAST_INTERNAL_SEEKING) {
            ModuleCommon_setAutosleepDisabled(true);

            if (!Player_resume()) {
                // Seek complete — start playback
                Player_play();
                render_toast(screen, "", 0);  // Clear the "Resuming..." toast
                last_input_time = SDL_GetTicks();
                last_progress_save_time = SDL_GetTicks();
                state = PODCAST_INTERNAL_PLAYING;
                dirty = 1;
            }
            else if (PAD_justPressed(BTN_B)) {
                // Cancel — stop and go back
                Podcast_stop();
                Podcast_flushProgress();
                Podcast_clearArtwork();
                GFX_clearLayers(LAYER_SCROLLTEXT);
                PLAT_clearLayers(LAYER_BUFFER);
                PLAT_clearLayers(LAYER_PODCAST_PROGRESS);
                PLAT_GPU_Flip();
                ModuleCommon_setAutosleepDisabled(false);
                podcast_episodes_selected = podcast_current_episode_index;
                state = PODCAST_INTERNAL_EPISODES;
                dirty = 1;
                continue;
            }

            dirty = 1;  // Keep refreshing to show seeking status
        }
        // =========================================
        // PLAYING STATE
        // =========================================
        else if (state == PODCAST_INTERNAL_PLAYING) {
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
                Podcast_update();
                GFX_sync();
                continue;
            }
            else if (screen_off) {
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
                        if (Player_getState() == PLAYER_STATE_PAUSED) Player_play();
                        else Player_pause();
                    }
                }
                Podcast_update();
                GFX_sync();
                continue;
            }
            else {
                if (PAD_justPressed(BTN_A)) {
                    if (Player_getState() == PLAYER_STATE_PAUSED) Player_play();
                    else Player_pause();
                    last_input_time = SDL_GetTicks();
                    dirty = 1;
                }
                else if (PAD_justPressed(BTN_B)) {
                    Podcast_stop();
                    Podcast_flushProgress();
                    Podcast_clearArtwork();
                    GFX_clearLayers(LAYER_SCROLLTEXT);
                    PLAT_clearLayers(LAYER_BUFFER);
                    PLAT_GPU_Flip();
                    ModuleCommon_setAutosleepDisabled(false);
                    if (screen_off) {
                        screen_off = false;
                        PLAT_enableBacklight(1);
                    }
                    podcast_episodes_selected = podcast_current_episode_index;
                    state = PODCAST_INTERNAL_EPISODES;
                    dirty = 1;
                    continue;  // Skip episode end detection below
                }
                else if (PAD_tappedSelect(SDL_GetTicks())) {
                    screen_off_hint_active = true;
                    screen_off_hint_start = SDL_GetTicks();
                    screen_off_hint_start_wallclock = time(NULL);
                    GFX_clearLayers(LAYER_SCROLLTEXT);
                    PLAT_clearLayers(LAYER_BUFFER);
                    PLAT_clearLayers(LAYER_PODCAST_PROGRESS);
                    PLAT_GPU_Flip();
                    // Render screen off hint immediately and skip rest of loop
                    // to prevent scroll animation from re-rendering to GPU layer
                    GFX_clear(screen);
                    render_screen_off_hint(screen);
                    GFX_flip(screen);
                    continue;
                }
                else if (PAD_justRepeated(BTN_LEFT)) {
                    int pos_ms = Player_getPosition();
                    Player_seek(pos_ms - 10000 < 0 ? 0 : pos_ms - 10000);
                    last_input_time = SDL_GetTicks();
                    dirty = 1;
                }
                else if (PAD_justRepeated(BTN_RIGHT)) {
                    int pos_ms = Player_getPosition();
                    int dur_ms = Player_getDuration();
                    Player_seek(pos_ms + 30000 > dur_ms ? dur_ms : pos_ms + 30000);
                    last_input_time = SDL_GetTicks();
                    dirty = 1;
                }

                Podcast_update();
                if (Podcast_isTitleScrolling()) Podcast_animateTitleScroll();

                // Periodic progress saving (every 30 seconds)
                {
                    uint32_t now = SDL_GetTicks();
                    if (Podcast_isActive() && now - last_progress_save_time >= PROGRESS_SAVE_INTERVAL_MS) {
                        PodcastFeed* feed = Podcast_getSubscription(podcast_current_feed_index);
                        if (feed) {
                            PodcastEpisode* ep = Podcast_getEpisode(podcast_current_feed_index, podcast_current_episode_index);
                            if (ep) {
                                int position = Player_getPosition();
                                if (position > 0) {
                                    ep->progress_sec = position / 1000;
                                    Podcast_saveProgress(feed->feed_url, ep->guid, ep->progress_sec);
                                    Podcast_flushProgress();
                                }
                            }
                        }
                        last_progress_save_time = now;
                    }
                }

                // Detect episode end (player stopped naturally)
                if (Player_getState() == PLAYER_STATE_STOPPED) {
                    PodcastFeed* feed = Podcast_getSubscription(podcast_current_feed_index);
                    PodcastEpisode* ep = Podcast_getEpisode(podcast_current_feed_index, podcast_current_episode_index);
                    char saved_feed_url[PODCAST_MAX_URL] = "";
                    char saved_guid[PODCAST_MAX_GUID] = "";
                    if (feed && ep) {
                        strncpy(saved_feed_url, feed->feed_url, PODCAST_MAX_URL - 1);
                        strncpy(saved_guid, ep->guid, PODCAST_MAX_GUID - 1);
                    }

                    Podcast_stop();

                    if (saved_feed_url[0] && saved_guid[0]) {
                        Podcast_markAsPlayed(saved_feed_url, saved_guid);
                    }
                    if (ep) ep->progress_sec = -1;

                    Podcast_flushProgress();

                    Podcast_clearArtwork();
                    GFX_clearLayers(LAYER_SCROLLTEXT);
                    PLAT_clearLayers(LAYER_BUFFER);
                    PLAT_clearLayers(LAYER_PODCAST_PROGRESS);
                    PLAT_GPU_Flip();
                    ModuleCommon_setAutosleepDisabled(false);
                    if (screen_off) {
                        screen_off = false;
                        PLAT_enableBacklight(1);
                    }
                    podcast_episodes_selected = podcast_current_episode_index;
                    state = PODCAST_INTERNAL_EPISODES;
                    dirty = 1;
                    continue;
                }

                // GPU progress bar update (updates every second without full redraw)
                if (PodcastProgress_needsRefresh()) {
                    PodcastProgress_renderGPU();
                }

                // Auto screen-off
                if (Podcast_isActive() && !screen_off_hint_active) {
                    uint32_t screen_timeout_ms = Settings_getScreenOffTimeout() * 1000;
                    if (screen_timeout_ms > 0 && last_input_time > 0) {
                        uint32_t now = SDL_GetTicks();
                        if (now - last_input_time >= screen_timeout_ms) {
                            screen_off_hint_active = true;
                            screen_off_hint_start = SDL_GetTicks();
                            screen_off_hint_start_wallclock = time(NULL);
                            GFX_clearLayers(LAYER_SCROLLTEXT);
                            PLAT_clearLayers(LAYER_BUFFER);
                            PLAT_clearLayers(LAYER_PODCAST_PROGRESS);
                            PLAT_GPU_Flip();
                            // Render screen off hint immediately and skip rest of loop
                            GFX_clear(screen);
                            render_screen_off_hint(screen);
                            GFX_flip(screen);
                            continue;
                        }
                    }
                }
            }
        }

        // Handle power management
        if (!screen_off && !screen_off_hint_active) {
            ModuleCommon_PWR_update(&dirty, &show_setting);
        }

        // Render
        if (dirty && !screen_off) {
            if (screen_off_hint_active) {
                GFX_clear(screen);
                render_screen_off_hint(screen);
            } else {
                switch (state) {
                    case PODCAST_INTERNAL_MENU:
                        render_podcast_list(screen, show_setting, podcast_menu_selected, &podcast_menu_scroll);
                        break;
                    case PODCAST_INTERNAL_MANAGE:
                        render_podcast_manage(screen, show_setting, podcast_manage_selected, Podcast_getSubscriptionCount());
                        break;
                    case PODCAST_INTERNAL_SUBSCRIPTIONS:
                        // Legacy - redirect to menu
                        break;
                    case PODCAST_INTERNAL_TOP_SHOWS:
                        render_podcast_top_shows(screen, show_setting, podcast_top_shows_selected, &podcast_top_shows_scroll,
                                                  podcast_toast_message, podcast_toast_time);
                        break;
                    case PODCAST_INTERNAL_SEARCH_RESULTS:
                        render_podcast_search_results(screen, show_setting, podcast_search_selected, &podcast_search_scroll,
                                                       podcast_toast_message, podcast_toast_time);
                        break;
                    case PODCAST_INTERNAL_EPISODES:
                        render_podcast_episodes(screen, show_setting, podcast_current_feed_index, podcast_episodes_selected,
                                                &podcast_episodes_scroll, podcast_toast_message, podcast_toast_time);
                        break;
                    case PODCAST_INTERNAL_BUFFERING: {
                        int buffer_pct = (int)(Radio_getBufferLevel() * 100);
                        render_podcast_buffering(screen, show_setting, podcast_current_feed_index, podcast_current_episode_index, buffer_pct);
                        break;
                    }
                    case PODCAST_INTERNAL_SEEKING:
                        render_podcast_playing(screen, show_setting, podcast_current_feed_index, podcast_current_episode_index);
                        // Overlay "Resuming..." text
                        {
                            PodcastEpisode* seek_ep = Podcast_getEpisode(podcast_current_feed_index, podcast_current_episode_index);
                            char seek_msg[64];
                            if (seek_ep && seek_ep->progress_sec > 0) {
                                int m = seek_ep->progress_sec / 60;
                                int s = seek_ep->progress_sec % 60;
                                snprintf(seek_msg, sizeof(seek_msg), "Resuming at %d:%02d...", m, s);
                            } else {
                                snprintf(seek_msg, sizeof(seek_msg), "Resuming...");
                            }
                            render_toast(screen, seek_msg, SDL_GetTicks());
                        }
                        break;
                    case PODCAST_INTERNAL_PLAYING:
                        render_podcast_playing(screen, show_setting, podcast_current_feed_index, podcast_current_episode_index);
                        break;
                }
            }

            if (show_setting) {
                GFX_blitHardwareHints(screen, show_setting);
            }

            GFX_flip(screen);
            dirty = 0;

            // Toast refresh
            if (podcast_toast_message[0] != '\0') {
                if (SDL_GetTicks() - podcast_toast_time < TOAST_DURATION) {
                    dirty = 1;
                } else {
                    podcast_toast_message[0] = '\0';
                    dirty = 1;  // One more render to clear GPU toast layer
                }
            }
        } else if (!screen_off) {
            GFX_sync();
        }
    }
}

// Check if podcast module is active (playing)
bool PodcastModule_isActive(void) {
    return Podcast_isActive();
}

