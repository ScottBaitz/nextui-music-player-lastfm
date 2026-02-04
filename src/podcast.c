#define _GNU_SOURCE
#include "podcast.h"
#include "radio_net.h"
#include "radio.h"
#include "player.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <locale.h>
#include <ctype.h>

#include "defines.h"
#include "api.h"

// SDCARD_PATH is defined in platform.h via api.h

// JSON library
#include "include/parson/parson.h"

// Timezone to country code mapping for Apple Podcast charts
typedef struct {
    const char* timezone;  // Timezone name or city
    const char* country;   // ISO 3166-1 alpha-2 country code
} TimezoneCountryMap;

static const TimezoneCountryMap tz_country_map[] = {
    // Asia
    {"Kuala_Lumpur", "my"}, {"Singapore", "sg"}, {"Jakarta", "id"},
    {"Bangkok", "th"}, {"Ho_Chi_Minh", "vn"}, {"Saigon", "vn"},
    {"Manila", "ph"}, {"Tokyo", "jp"}, {"Seoul", "kr"},
    {"Shanghai", "cn"}, {"Hong_Kong", "hk"}, {"Taipei", "tw"},
    {"Kolkata", "in"}, {"Calcutta", "in"}, {"Mumbai", "in"},
    {"Dubai", "ae"}, {"Riyadh", "sa"}, {"Jerusalem", "il"},
    {"Tel_Aviv", "il"},
    // Europe
    {"London", "gb"}, {"Paris", "fr"}, {"Berlin", "de"},
    {"Rome", "it"}, {"Madrid", "es"}, {"Amsterdam", "nl"},
    {"Brussels", "be"}, {"Vienna", "at"}, {"Zurich", "ch"},
    {"Stockholm", "se"}, {"Oslo", "no"}, {"Copenhagen", "dk"},
    {"Helsinki", "fi"}, {"Warsaw", "pl"}, {"Prague", "cz"},
    {"Budapest", "hu"}, {"Athens", "gr"}, {"Moscow", "ru"},
    {"Dublin", "ie"}, {"Lisbon", "pt"},
    // Americas
    {"New_York", "us"}, {"Los_Angeles", "us"}, {"Chicago", "us"},
    {"Denver", "us"}, {"Phoenix", "us"}, {"Anchorage", "us"},
    {"Honolulu", "us"}, {"Toronto", "ca"}, {"Vancouver", "ca"},
    {"Montreal", "ca"}, {"Mexico_City", "mx"}, {"Sao_Paulo", "br"},
    {"Buenos_Aires", "ar"}, {"Lima", "pe"}, {"Bogota", "co"},
    {"Santiago", "cl"},
    // Oceania
    {"Sydney", "au"}, {"Melbourne", "au"}, {"Brisbane", "au"},
    {"Perth", "au"}, {"Adelaide", "au"}, {"Auckland", "nz"},
    // Africa
    {"Cairo", "eg"}, {"Johannesburg", "za"}, {"Lagos", "ng"},
    {"Nairobi", "ke"}, {"Casablanca", "ma"},
    {NULL, NULL}
};

// Apple Podcast supported countries (subset of most common ones)
// Full list: https://rss.marketingtools.apple.com/
static const char* apple_podcast_countries[] = {
    "us", "gb", "ca", "au", "nz", "ie",  // English-speaking
    "de", "fr", "es", "it", "nl", "be", "at", "ch", "pt",  // Western Europe
    "se", "no", "dk", "fi",  // Nordic
    "pl", "cz", "hu", "gr", "ru",  // Eastern Europe
    "jp", "kr", "cn", "hk", "tw", "sg", "my", "th", "id", "ph", "vn", "in",  // Asia
    "ae", "sa", "il",  // Middle East
    "br", "mx", "ar", "cl", "co", "pe",  // Latin America
    "za", "eg", "ng", "ke", "ma",  // Africa
    NULL
};

// Check if country code is supported by Apple Podcast
static bool is_apple_podcast_country(const char* country) {
    if (!country) return false;
    for (int i = 0; apple_podcast_countries[i] != NULL; i++) {
        if (strcasecmp(country, apple_podcast_countries[i]) == 0) {
            return true;
        }
    }
    return false;
}

// Get country code from timezone path like /usr/share/zoneinfo/Asia/Kuala_Lumpur
static const char* get_country_from_timezone(const char* tz_path) {
    if (!tz_path) return NULL;

    // Find the last component (city name)
    const char* city = strrchr(tz_path, '/');
    if (city) city++; else city = tz_path;

    // Look up in mapping table
    for (int i = 0; tz_country_map[i].timezone != NULL; i++) {
        if (strcmp(city, tz_country_map[i].timezone) == 0) {
            return tz_country_map[i].country;
        }
    }
    return NULL;
}

// Paths
static char subscriptions_file[512] = "";
static char progress_file[512] = "";
static char downloads_file[512] = "";
static char charts_cache_file[512] = "";
static char download_dir[512] = "";

// Module state
static PodcastState podcast_state = PODCAST_STATE_IDLE;
static char error_message[256] = "";

// Subscriptions
static PodcastFeed subscriptions[PODCAST_MAX_SUBSCRIPTIONS];
static int subscription_count = 0;
static pthread_mutex_t subscriptions_mutex = PTHREAD_MUTEX_INITIALIZER;

// Search
static pthread_t search_thread;
static volatile bool search_running = false;
static volatile bool search_should_stop = false;
static PodcastSearchResult search_results[PODCAST_MAX_SEARCH_RESULTS];
static int search_result_count = 0;
static PodcastSearchStatus search_status = {0};
static char search_query_copy[256] = "";

// Charts
static pthread_t charts_thread;
static volatile bool charts_running = false;
static volatile bool charts_should_stop = false;
static PodcastChartItem top_shows[PODCAST_MAX_CHART_ITEMS];
static int top_shows_count = 0;
static PodcastChartsStatus charts_status = {0};
static char charts_country_code[8] = "us";

// Downloads
static PodcastDownloadItem download_queue[PODCAST_MAX_DOWNLOAD_QUEUE];
static int download_queue_count = 0;
static pthread_mutex_t download_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t download_thread;
static volatile bool download_running = false;
static volatile bool download_should_stop = false;
static PodcastDownloadProgress download_progress = {0};

// Streaming (will reuse radio streaming infrastructure)
static PodcastStreamingStatus streaming_status = {0};
static PodcastFeed* current_feed = NULL;
static int current_feed_index = -1;
static int current_episode_index = -1;

// Progress tracking - simple in-memory cache (will be persisted to JSON)
#define MAX_PROGRESS_ENTRIES 500
typedef struct {
    char feed_url[PODCAST_MAX_URL];
    char episode_guid[PODCAST_MAX_GUID];
    int position_sec;
} ProgressEntry;
static ProgressEntry progress_entries[MAX_PROGRESS_ENTRIES];
static int progress_entry_count = 0;

// Episode cache - only load PODCAST_EPISODE_PAGE_SIZE episodes at a time
static PodcastEpisode episode_cache[PODCAST_EPISODE_PAGE_SIZE];
static int episode_cache_feed_index = -1;
static int episode_cache_offset = 0;
static int episode_cache_count = 0;
static pthread_mutex_t episode_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

// Base data directory for podcast data
static char podcast_data_dir[512] = "";

// Forward declarations
static void* search_thread_func(void* arg);
static void* charts_thread_func(void* arg);
static void* download_thread_func(void* arg);
static void save_charts_cache(void);
static bool load_charts_cache(void);

// Chunked download function - downloads directly to file with progress tracking
// Returns: bytes downloaded, or -1 on error
static int podcast_download_to_file(const char* url, const char* filepath,
                                    volatile int* progress_percent,
                                    volatile bool* should_stop, int redirect_depth);

// External WiFi connection function from musicplayer.c
// Can be called with NULL screen from background threads
extern bool ensure_wifi_connected(SDL_Surface* scr, int show_setting);

// Socket includes for chunked download
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

// mbedTLS for HTTPS support (same as radio_net.c)
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"

// SSL context for podcast download (same structure as radio_net.c)
typedef struct {
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    bool initialized;
} PodcastSSLContext;

// ============================================================================
// Feed ID and Path Helpers
// ============================================================================

// Generate a 16-char hex hash from feed URL for folder naming
void Podcast_generateFeedId(const char* feed_url, char* feed_id, int feed_id_size) {
    if (!feed_url || !feed_id || feed_id_size < 17) {
        if (feed_id && feed_id_size > 0) feed_id[0] = '\0';
        return;
    }

    // Simple hash (djb2)
    unsigned long hash1 = 5381;
    unsigned long hash2 = 0;
    const char* p = feed_url;
    while (*p) {
        hash1 = ((hash1 << 5) + hash1) + (unsigned char)*p;
        hash2 = hash2 * 31 + (unsigned char)*p;
        p++;
    }

    snprintf(feed_id, feed_id_size, "%08lx%08lx", hash1 & 0xFFFFFFFF, hash2 & 0xFFFFFFFF);
}

// Get path to feed's data directory
void Podcast_getFeedDataPath(const char* feed_id, char* path, int path_size) {
    if (!feed_id || !path || path_size <= 0) return;
    snprintf(path, path_size, "%s/%s", podcast_data_dir, feed_id);
}

// Helper: find feed index from feed pointer
static int get_feed_index(PodcastFeed* feed) {
    if (!feed) return -1;
    for (int i = 0; i < subscription_count; i++) {
        if (&subscriptions[i] == feed) {
            return i;
        }
    }
    return -1;
}

// Get path to feed's episodes JSON file
static void get_episodes_file_path(const char* feed_id, char* path, int path_size) {
    if (!feed_id || !path || path_size <= 0) return;
    snprintf(path, path_size, "%s/%s/episodes.json", podcast_data_dir, feed_id);
}

// Create directory recursively
static void mkdir_recursive(const char* path) {
    char tmp[512];
    char* p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

// ============================================================================
// Episode Storage (JSON on disk)
// ============================================================================

// Save episodes to JSON file for a feed
int Podcast_saveEpisodes(int feed_index, PodcastEpisode* episodes, int count) {
    if (feed_index < 0 || feed_index >= subscription_count || !episodes || count < 0) {
        return -1;
    }

    PodcastFeed* feed = &subscriptions[feed_index];

    // Generate feed_id if not set
    if (!feed->feed_id[0]) {
        Podcast_generateFeedId(feed->feed_url, feed->feed_id, sizeof(feed->feed_id));
    }

    // Create feed directory
    char feed_dir[512];
    Podcast_getFeedDataPath(feed->feed_id, feed_dir, sizeof(feed_dir));
    mkdir_recursive(feed_dir);

    // Build episodes JSON
    JSON_Value* root = json_value_init_array();
    JSON_Array* arr = json_value_get_array(root);

    for (int i = 0; i < count; i++) {
        PodcastEpisode* ep = &episodes[i];
        JSON_Value* ep_val = json_value_init_object();
        JSON_Object* ep_obj = json_value_get_object(ep_val);

        json_object_set_string(ep_obj, "guid", ep->guid);
        json_object_set_string(ep_obj, "title", ep->title);
        json_object_set_string(ep_obj, "url", ep->url);
        json_object_set_string(ep_obj, "description", ep->description);
        json_object_set_number(ep_obj, "duration", ep->duration_sec);
        json_object_set_number(ep_obj, "pub_date", ep->pub_date);
        json_object_set_number(ep_obj, "progress", ep->progress_sec);
        json_object_set_boolean(ep_obj, "downloaded", ep->downloaded);
        if (ep->local_path[0]) {
            json_object_set_string(ep_obj, "local_path", ep->local_path);
        }

        json_array_append_value(arr, ep_val);
    }

    // Save to file
    char episodes_path[512];
    get_episodes_file_path(feed->feed_id, episodes_path, sizeof(episodes_path));
    int result = json_serialize_to_file_pretty(root, episodes_path);
    json_value_free(root);

    if (result == JSONSuccess) {
        feed->episode_count = count;
        LOG_info("[Podcast] Saved %d episodes to %s\n", count, episodes_path);
        return 0;
    }

    LOG_error("[Podcast] Failed to save episodes to %s\n", episodes_path);
    return -1;
}

// Load a page of episodes from JSON file into cache
int Podcast_loadEpisodePage(int feed_index, int offset) {
    if (feed_index < 0 || feed_index >= subscription_count || offset < 0) {
        return 0;
    }

    PodcastFeed* feed = &subscriptions[feed_index];

    // Generate feed_id if not set
    if (!feed->feed_id[0]) {
        Podcast_generateFeedId(feed->feed_url, feed->feed_id, sizeof(feed->feed_id));
    }

    char episodes_path[512];
    get_episodes_file_path(feed->feed_id, episodes_path, sizeof(episodes_path));

    JSON_Value* root = json_parse_file(episodes_path);
    if (!root) {
        LOG_error("[Podcast] Failed to load episodes from %s\n", episodes_path);
        return 0;
    }

    JSON_Array* arr = json_value_get_array(root);
    if (!arr) {
        json_value_free(root);
        return 0;
    }

    int total = json_array_get_count(arr);
    feed->episode_count = total;  // Update total count

    pthread_mutex_lock(&episode_cache_mutex);

    episode_cache_feed_index = feed_index;
    episode_cache_offset = offset;
    episode_cache_count = 0;

    for (int i = offset; i < total && episode_cache_count < PODCAST_EPISODE_PAGE_SIZE; i++) {
        JSON_Object* ep_obj = json_array_get_object(arr, i);
        if (!ep_obj) continue;

        PodcastEpisode* ep = &episode_cache[episode_cache_count];
        memset(ep, 0, sizeof(PodcastEpisode));

        const char* str;
        str = json_object_get_string(ep_obj, "guid");
        if (str) strncpy(ep->guid, str, PODCAST_MAX_GUID - 1);
        str = json_object_get_string(ep_obj, "title");
        if (str) strncpy(ep->title, str, PODCAST_MAX_TITLE - 1);
        str = json_object_get_string(ep_obj, "url");
        if (str) strncpy(ep->url, str, PODCAST_MAX_URL - 1);
        str = json_object_get_string(ep_obj, "description");
        if (str) strncpy(ep->description, str, PODCAST_MAX_DESCRIPTION - 1);
        str = json_object_get_string(ep_obj, "local_path");
        if (str) strncpy(ep->local_path, str, PODCAST_MAX_URL - 1);

        ep->duration_sec = (int)json_object_get_number(ep_obj, "duration");
        ep->pub_date = (uint32_t)json_object_get_number(ep_obj, "pub_date");
        ep->progress_sec = (int)json_object_get_number(ep_obj, "progress");
        ep->downloaded = json_object_get_boolean(ep_obj, "downloaded");

        episode_cache_count++;
    }

    pthread_mutex_unlock(&episode_cache_mutex);
    json_value_free(root);

    LOG_info("[Podcast] Loaded %d episodes (offset %d) from %s\n", episode_cache_count, offset, episodes_path);
    return episode_cache_count;
}

// Get episode by index (loads from cache, auto-loads page if needed)
PodcastEpisode* Podcast_getEpisode(int feed_index, int episode_index) {
    if (feed_index < 0 || feed_index >= subscription_count || episode_index < 0) {
        return NULL;
    }

    PodcastFeed* feed = &subscriptions[feed_index];
    if (episode_index >= feed->episode_count) {
        return NULL;
    }

    pthread_mutex_lock(&episode_cache_mutex);

    // Check if we need to load a different page
    if (episode_cache_feed_index != feed_index ||
        episode_index < episode_cache_offset ||
        episode_index >= episode_cache_offset + episode_cache_count) {

        pthread_mutex_unlock(&episode_cache_mutex);

        // Calculate page offset (align to page boundaries for better caching)
        int page_offset = (episode_index / PODCAST_EPISODE_PAGE_SIZE) * PODCAST_EPISODE_PAGE_SIZE;
        Podcast_loadEpisodePage(feed_index, page_offset);

        pthread_mutex_lock(&episode_cache_mutex);
    }

    // Get from cache
    int cache_index = episode_index - episode_cache_offset;
    PodcastEpisode* result = NULL;
    if (cache_index >= 0 && cache_index < episode_cache_count) {
        result = &episode_cache[cache_index];
    }

    pthread_mutex_unlock(&episode_cache_mutex);
    return result;
}

// Get episode cache info
int Podcast_getEpisodeCacheOffset(void) {
    return episode_cache_offset;
}

int Podcast_getEpisodeCacheCount(void) {
    return episode_cache_count;
}

void Podcast_invalidateEpisodeCache(void) {
    pthread_mutex_lock(&episode_cache_mutex);
    episode_cache_feed_index = -1;
    episode_cache_offset = 0;
    episode_cache_count = 0;
    pthread_mutex_unlock(&episode_cache_mutex);
}

int Podcast_getEpisodeCount(int feed_index) {
    if (feed_index < 0 || feed_index >= subscription_count) {
        return 0;
    }
    return subscriptions[feed_index].episode_count;
}

#define PODCAST_DOWNLOAD_TIMEOUT_SECONDS 30
#define PODCAST_MAX_REDIRECTS 10
#define PODCAST_DOWNLOAD_CHUNK_SIZE 32768  // 32KB chunks

// Chunked download implementation - downloads directly to file with progress
static int podcast_download_to_file(const char* url, const char* filepath,
                                    volatile int* progress_percent,
                                    volatile bool* should_stop, int redirect_depth) {
    if (!url || !filepath) {
        LOG_error("[Podcast] download_to_file: invalid parameters\n");
        return -1;
    }

    if (redirect_depth >= PODCAST_MAX_REDIRECTS) {
        LOG_error("[Podcast] download_to_file: too many redirects\n");
        return -1;
    }

    // Parse URL
    char* host = (char*)malloc(256);
    char* path = (char*)malloc(512);
    if (!host || !path) {
        free(host);
        free(path);
        return -1;
    }

    int port;
    bool is_https;

    if (radio_net_parse_url(url, host, 256, &port, path, 512, &is_https) != 0) {
        LOG_error("[Podcast] download_to_file: failed to parse URL: %s\n", url);
        free(host);
        free(path);
        return -1;
    }

    int sock_fd = -1;
    PodcastSSLContext* ssl_ctx = NULL;
    char* header_buf = NULL;
    FILE* outfile = NULL;
    int result = -1;

    if (is_https) {
        ssl_ctx = (PodcastSSLContext*)calloc(1, sizeof(PodcastSSLContext));
        if (!ssl_ctx) {
            LOG_error("[Podcast] download_to_file: failed to allocate SSL context\n");
            free(host);
            free(path);
            return -1;
        }

        const char* pers = "podcast_download";
        mbedtls_net_init(&ssl_ctx->net);
        mbedtls_ssl_init(&ssl_ctx->ssl);
        mbedtls_ssl_config_init(&ssl_ctx->conf);
        mbedtls_entropy_init(&ssl_ctx->entropy);
        mbedtls_ctr_drbg_init(&ssl_ctx->ctr_drbg);

        if (mbedtls_ctr_drbg_seed(&ssl_ctx->ctr_drbg, mbedtls_entropy_func, &ssl_ctx->entropy,
                                   (const unsigned char*)pers, strlen(pers)) != 0) {
            goto cleanup;
        }

        if (mbedtls_ssl_config_defaults(&ssl_ctx->conf, MBEDTLS_SSL_IS_CLIENT,
                                         MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
            goto cleanup;
        }

        mbedtls_ssl_conf_authmode(&ssl_ctx->conf, MBEDTLS_SSL_VERIFY_NONE);
        mbedtls_ssl_conf_rng(&ssl_ctx->conf, mbedtls_ctr_drbg_random, &ssl_ctx->ctr_drbg);

        if (mbedtls_ssl_setup(&ssl_ctx->ssl, &ssl_ctx->conf) != 0) {
            goto cleanup;
        }

        mbedtls_ssl_set_hostname(&ssl_ctx->ssl, host);

        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", port);

        LOG_info("[Podcast] download_to_file: connecting to %s:%s (HTTPS)\n", host, port_str);
        int connect_ret = mbedtls_net_connect(&ssl_ctx->net, host, port_str, MBEDTLS_NET_PROTO_TCP);
        if (connect_ret != 0) {
            LOG_error("[Podcast] download_to_file: mbedtls_net_connect failed: %d (host=%s, port=%s)\n",
                      connect_ret, host, port_str);
            goto cleanup;
        }
        LOG_info("[Podcast] download_to_file: connected, starting SSL handshake\n");

        // Set socket timeout
        int ssl_sock_fd = ssl_ctx->net.fd;
        struct timeval tv = {PODCAST_DOWNLOAD_TIMEOUT_SECONDS, 0};
        setsockopt(ssl_sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(ssl_sock_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        mbedtls_ssl_set_bio(&ssl_ctx->ssl, &ssl_ctx->net, mbedtls_net_send, mbedtls_net_recv, NULL);

        // SSL handshake
        int ret;
        int handshake_retries = 0;
        const int max_handshake_retries = 100;
        while ((ret = mbedtls_ssl_handshake(&ssl_ctx->ssl)) != 0) {
            if (should_stop && *should_stop) goto cleanup;
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                LOG_error("[Podcast] download_to_file: SSL handshake failed: %d\n", ret);
                goto cleanup;
            }
            if (++handshake_retries > max_handshake_retries) {
                LOG_error("[Podcast] download_to_file: SSL handshake timeout\n");
                goto cleanup;
            }
            usleep(100000);
        }

        ssl_ctx->initialized = true;
        sock_fd = ssl_ctx->net.fd;
        LOG_info("[Podcast] download_to_file: SSL handshake complete\n");
    } else {
        // Plain HTTP
        struct addrinfo hints, *ai_result = NULL;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", port);

        if (getaddrinfo(host, port_str, &hints, &ai_result) != 0 || !ai_result) {
            LOG_error("[Podcast] download_to_file: getaddrinfo failed for %s\n", host);
            if (ai_result) freeaddrinfo(ai_result);
            free(host);
            free(path);
            return -1;
        }

        sock_fd = socket(ai_result->ai_family, ai_result->ai_socktype, ai_result->ai_protocol);
        if (sock_fd < 0) {
            freeaddrinfo(ai_result);
            free(host);
            free(path);
            return -1;
        }

        struct timeval tv = {PODCAST_DOWNLOAD_TIMEOUT_SECONDS, 0};
        setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(sock_fd, ai_result->ai_addr, ai_result->ai_addrlen) < 0) {
            LOG_error("[Podcast] download_to_file: connect failed: %s\n", strerror(errno));
            close(sock_fd);
            freeaddrinfo(ai_result);
            free(host);
            free(path);
            return -1;
        }
        freeaddrinfo(ai_result);
    }

    // Send HTTP request
    char request[512];
    snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: Mozilla/5.0 (Linux) AppleWebKit/537.36\r\n"
        "Accept: */*\r\n"
        "Accept-Encoding: identity\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host);

    int sent;
    if (is_https) {
        sent = mbedtls_ssl_write(&ssl_ctx->ssl, (unsigned char*)request, strlen(request));
    } else {
        sent = send(sock_fd, request, strlen(request), 0);
    }

    if (sent < 0) {
        LOG_error("[Podcast] download_to_file: failed to send request\n");
        goto cleanup;
    }

    // Read headers
    #define HEADER_BUF_SIZE 4096
    header_buf = (char*)malloc(HEADER_BUF_SIZE);
    if (!header_buf) goto cleanup;

    int header_pos = 0;
    bool headers_done = false;

    while (header_pos < HEADER_BUF_SIZE - 1) {
        if (should_stop && *should_stop) goto cleanup;

        char c;
        int r;
        if (is_https) {
            r = mbedtls_ssl_read(&ssl_ctx->ssl, (unsigned char*)&c, 1);
            if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
                usleep(10000);
                continue;
            }
        } else {
            r = recv(sock_fd, &c, 1, 0);
        }
        if (r != 1) break;

        header_buf[header_pos++] = c;
        if (header_pos >= 4 &&
            header_buf[header_pos-4] == '\r' && header_buf[header_pos-3] == '\n' &&
            header_buf[header_pos-2] == '\r' && header_buf[header_pos-1] == '\n') {
            headers_done = true;
            break;
        }
    }
    header_buf[header_pos] = '\0';

    if (!headers_done) {
        LOG_error("[Podcast] download_to_file: failed to read headers\n");
        goto cleanup;
    }

    // Check for redirect
    char* first_line_end = strstr(header_buf, "\r\n");
    bool is_redirect = false;
    if (first_line_end) {
        char* status_start = strstr(header_buf, "HTTP/");
        if (status_start && status_start < first_line_end) {
            is_redirect = (strstr(status_start, " 301 ") && strstr(status_start, " 301 ") < first_line_end) ||
                         (strstr(status_start, " 302 ") && strstr(status_start, " 302 ") < first_line_end) ||
                         (strstr(status_start, " 303 ") && strstr(status_start, " 303 ") < first_line_end) ||
                         (strstr(status_start, " 307 ") && strstr(status_start, " 307 ") < first_line_end) ||
                         (strstr(status_start, " 308 ") && strstr(status_start, " 308 ") < first_line_end);
        }
    }

    if (is_redirect) {
        char* loc = strcasestr(header_buf, "\nLocation:");
        if (loc) {
            loc += 10;
            while (*loc == ' ') loc++;
            char* end = loc;
            while (*end && *end != '\r' && *end != '\n') end++;

            char redirect_url[1024];
            int rlen = end - loc;
            if (rlen >= (int)sizeof(redirect_url)) rlen = sizeof(redirect_url) - 1;
            strncpy(redirect_url, loc, rlen);
            redirect_url[rlen] = '\0';

            LOG_info("[Podcast] download_to_file: redirecting to %s\n", redirect_url);

            // Cleanup current connection
            if (ssl_ctx) {
                mbedtls_ssl_close_notify(&ssl_ctx->ssl);
                mbedtls_net_free(&ssl_ctx->net);
                mbedtls_ssl_free(&ssl_ctx->ssl);
                mbedtls_ssl_config_free(&ssl_ctx->conf);
                mbedtls_ctr_drbg_free(&ssl_ctx->ctr_drbg);
                mbedtls_entropy_free(&ssl_ctx->entropy);
                free(ssl_ctx);
            } else if (sock_fd >= 0) {
                close(sock_fd);
            }
            free(header_buf);
            free(host);
            free(path);

            // Follow redirect
            return podcast_download_to_file(redirect_url, filepath, progress_percent, should_stop, redirect_depth + 1);
        }
        goto cleanup;
    }

    // Parse Content-Length
    long content_length = -1;
    char* cl = strcasestr(header_buf, "\nContent-Length:");
    if (cl) {
        cl += 16;
        while (*cl == ' ') cl++;
        content_length = atol(cl);
    }
    LOG_info("[Podcast] download_to_file: Content-Length=%ld\n", content_length);

    // Check for chunked transfer encoding
    bool is_chunked = (strcasestr(header_buf, "Transfer-Encoding: chunked") != NULL);

    // Open output file
    outfile = fopen(filepath, "wb");
    if (!outfile) {
        LOG_error("[Podcast] download_to_file: failed to open file: %s\n", filepath);
        goto cleanup;
    }

    // Download body in chunks
    uint8_t* chunk_buf = (uint8_t*)malloc(PODCAST_DOWNLOAD_CHUNK_SIZE);
    if (!chunk_buf) {
        fclose(outfile);
        outfile = NULL;
        goto cleanup;
    }

    long total_read = 0;
    int read_retries = 0;
    const int max_read_retries = 50;

    if (is_chunked) {
        // Handle chunked transfer encoding
        char chunk_size_buf[20];
        int chunk_size_pos = 0;

        while (1) {
            if (should_stop && *should_stop) break;

            // Read chunk size
            chunk_size_pos = 0;
            while (chunk_size_pos < (int)sizeof(chunk_size_buf) - 1) {
                char c;
                int r;
                if (is_https) {
                    r = mbedtls_ssl_read(&ssl_ctx->ssl, (unsigned char*)&c, 1);
                    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
                        if (++read_retries > max_read_retries) break;
                        usleep(10000);
                        continue;
                    }
                    read_retries = 0;
                } else {
                    r = recv(sock_fd, &c, 1, 0);
                }
                if (r != 1) goto chunked_done;
                if (c == '\r') continue;
                if (c == '\n') break;
                chunk_size_buf[chunk_size_pos++] = c;
            }
            chunk_size_buf[chunk_size_pos] = '\0';

            long chunk_size = strtol(chunk_size_buf, NULL, 16);
            if (chunk_size <= 0) break;

            // Read chunk data
            long chunk_read = 0;
            while (chunk_read < chunk_size) {
                if (should_stop && *should_stop) goto chunked_done;

                int to_read = (chunk_size - chunk_read) < PODCAST_DOWNLOAD_CHUNK_SIZE ?
                              (int)(chunk_size - chunk_read) : PODCAST_DOWNLOAD_CHUNK_SIZE;
                int r;
                if (is_https) {
                    r = mbedtls_ssl_read(&ssl_ctx->ssl, chunk_buf, to_read);
                    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
                        if (++read_retries > max_read_retries) goto chunked_done;
                        usleep(10000);
                        continue;
                    }
                    read_retries = 0;
                } else {
                    r = recv(sock_fd, chunk_buf, to_read, 0);
                }
                if (r <= 0) goto chunked_done;

                fwrite(chunk_buf, 1, r, outfile);
                chunk_read += r;
                total_read += r;
            }

            // Skip trailing CRLF
            char crlf[2];
            int crlf_read = 0;
            while (crlf_read < 2) {
                int r;
                if (is_https) {
                    r = mbedtls_ssl_read(&ssl_ctx->ssl, (unsigned char*)&crlf[crlf_read], 1);
                    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
                        usleep(10000);
                        continue;
                    }
                } else {
                    r = recv(sock_fd, &crlf[crlf_read], 1, 0);
                }
                if (r != 1) goto chunked_done;
                crlf_read++;
            }
        }
        chunked_done:;
    } else {
        // Non-chunked: read directly with progress
        while (1) {
            if (should_stop && *should_stop) break;

            int r;
            if (is_https) {
                r = mbedtls_ssl_read(&ssl_ctx->ssl, chunk_buf, PODCAST_DOWNLOAD_CHUNK_SIZE);
                if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
                    if (++read_retries > max_read_retries) break;
                    usleep(10000);
                    continue;
                }
                read_retries = 0;
            } else {
                r = recv(sock_fd, chunk_buf, PODCAST_DOWNLOAD_CHUNK_SIZE, 0);
            }
            if (r <= 0) break;

            fwrite(chunk_buf, 1, r, outfile);
            total_read += r;

            // Update progress
            if (progress_percent && content_length > 0) {
                int pct = (int)((total_read * 100) / content_length);
                if (pct > 100) pct = 100;
                *progress_percent = pct;
            }
        }
    }

    free(chunk_buf);
    fclose(outfile);
    outfile = NULL;

    if (total_read > 0) {
        result = (int)total_read;
        if (progress_percent) *progress_percent = 100;
        LOG_info("[Podcast] download_to_file: completed %ld bytes\n", total_read);
    }

cleanup:
    if (outfile) fclose(outfile);
    if (ssl_ctx) {
        if (ssl_ctx->initialized) {
            mbedtls_ssl_close_notify(&ssl_ctx->ssl);
        }
        mbedtls_net_free(&ssl_ctx->net);
        mbedtls_ssl_free(&ssl_ctx->ssl);
        mbedtls_ssl_config_free(&ssl_ctx->conf);
        mbedtls_ctr_drbg_free(&ssl_ctx->ctr_drbg);
        mbedtls_entropy_free(&ssl_ctx->entropy);
        free(ssl_ctx);
    } else if (sock_fd >= 0) {
        close(sock_fd);
    }
    free(header_buf);
    free(host);
    free(path);
    return result;
}

// External RSS parser functions (in podcast_rss.c)
extern int podcast_rss_parse(const char* xml_data, int xml_len, PodcastFeed* feed);

// External search functions (in podcast_search.c)
extern int podcast_search_itunes(const char* query, PodcastSearchResult* results, int max_results);
extern int podcast_search_lookup(const char* itunes_id, char* feed_url, int feed_url_size);
extern int podcast_search_lookup_full(const char* itunes_id, char* feed_url, int feed_url_size,
                                      char* artwork_url, int artwork_url_size);
extern int podcast_charts_fetch(const char* country_code, PodcastChartItem* top, int* top_count,
                                 PodcastChartItem* new_items, int* new_count, int max_items);

// ============================================================================
// Initialization
// ============================================================================

int Podcast_init(void) {
    snprintf(podcast_data_dir, sizeof(podcast_data_dir), "%s/" PODCAST_DATA_DIR, SHARED_USERDATA_PATH);
    snprintf(subscriptions_file, sizeof(subscriptions_file), "%s/" PODCAST_SUBSCRIPTIONS_FILE, podcast_data_dir);
    snprintf(progress_file, sizeof(progress_file), "%s/progress.json", podcast_data_dir);
    snprintf(downloads_file, sizeof(downloads_file), "%s/downloads.json", podcast_data_dir);
    snprintf(charts_cache_file, sizeof(charts_cache_file), "%s/charts.json", podcast_data_dir);
    snprintf(download_dir, sizeof(download_dir), "%s/Podcasts", SDCARD_PATH);

    // Create podcast data directory
    mkdir_recursive(podcast_data_dir);

    // Create download directory
    mkdir(download_dir, 0755);

    // Detect country code from system timezone
    // /etc/localtime symlinks to /tmp/localtime which resolves to the actual timezone
    char tz_path[256] = {0};
    ssize_t len = readlink("/tmp/localtime", tz_path, sizeof(tz_path) - 1);
    if (len > 0) {
        tz_path[len] = '\0';
        const char* country = get_country_from_timezone(tz_path);
        if (country) {
            strncpy(charts_country_code, country, sizeof(charts_country_code) - 1);
            charts_country_code[sizeof(charts_country_code) - 1] = '\0';
            LOG_info("[Podcast] Detected country '%s' from timezone: %s\n", charts_country_code, tz_path);
        } else {
            LOG_info("[Podcast] Unknown timezone '%s', using default country 'us'\n", tz_path);
        }
    } else {
        // Fallback: try LANG environment variable
        const char* lang = getenv("LANG");
        if (lang && strlen(lang) >= 5 && lang[2] == '_') {
            charts_country_code[0] = tolower(lang[3]);
            charts_country_code[1] = tolower(lang[4]);
            charts_country_code[2] = '\0';
            LOG_info("[Podcast] Detected country '%s' from LANG: %s\n", charts_country_code, lang);
        } else {
            LOG_info("[Podcast] Could not detect country, using default 'us'\n");
        }
    }

    // Validate country code - if not supported by Apple Podcast, fallback to US
    if (!is_apple_podcast_country(charts_country_code)) {
        LOG_info("[Podcast] Country '%s' not supported by Apple Podcast, falling back to 'us'\n", charts_country_code);
        strcpy(charts_country_code, "us");
    }

    // Load saved data
    Podcast_loadSubscriptions();
    Podcast_loadDownloadQueue();

    // Load progress entries
    JSON_Value* root = json_parse_file(progress_file);
    if (root) {
        JSON_Array* arr = json_value_get_array(root);
        if (arr) {
            int count = json_array_get_count(arr);
            for (int i = 0; i < count && progress_entry_count < MAX_PROGRESS_ENTRIES; i++) {
                JSON_Object* obj = json_array_get_object(arr, i);
                if (obj) {
                    const char* feed = json_object_get_string(obj, "feed_url");
                    const char* guid = json_object_get_string(obj, "guid");
                    int pos = (int)json_object_get_number(obj, "position");
                    if (feed && guid) {
                        strncpy(progress_entries[progress_entry_count].feed_url, feed, PODCAST_MAX_URL - 1);
                        strncpy(progress_entries[progress_entry_count].episode_guid, guid, PODCAST_MAX_GUID - 1);
                        progress_entries[progress_entry_count].position_sec = pos;
                        progress_entry_count++;
                    }
                }
            }
        }
        json_value_free(root);
    }

    LOG_info("[Podcast] Initialized with %d subscriptions\n", subscription_count);
    return 0;
}

void Podcast_cleanup(void) {
    // Stop any running operations
    Podcast_cancelSearch();
    Podcast_stopDownloads();
    Podcast_stop();

    // Save state
    Podcast_saveSubscriptions();
    Podcast_saveDownloadQueue();

    // Save progress
    JSON_Value* root = json_value_init_array();
    JSON_Array* arr = json_value_get_array(root);
    for (int i = 0; i < progress_entry_count; i++) {
        JSON_Value* item = json_value_init_object();
        JSON_Object* obj = json_value_get_object(item);
        json_object_set_string(obj, "feed_url", progress_entries[i].feed_url);
        json_object_set_string(obj, "guid", progress_entries[i].episode_guid);
        json_object_set_number(obj, "position", progress_entries[i].position_sec);
        json_array_append_value(arr, item);
    }
    json_serialize_to_file_pretty(root, progress_file);
    json_value_free(root);

    LOG_info("[Podcast] Cleanup complete\n");
}

PodcastState Podcast_getState(void) {
    return podcast_state;
}

const char* Podcast_getError(void) {
    return error_message;
}

void Podcast_update(void) {
    // Check for completed async operations
    if (search_status.searching && !search_running) {
        search_status.searching = false;
        search_status.completed = true;
    }
    if (charts_status.loading && !charts_running) {
        charts_status.loading = false;
        charts_status.completed = true;
    }
}

// ============================================================================
// Subscription Management
// ============================================================================

int Podcast_getSubscriptionCount(void) {
    return subscription_count;
}

PodcastFeed* Podcast_getSubscriptions(int* count) {
    if (count) *count = subscription_count;
    return subscriptions;
}

PodcastFeed* Podcast_getSubscription(int index) {
    if (index < 0 || index >= subscription_count) return NULL;
    return &subscriptions[index];
}

int Podcast_subscribe(const char* feed_url) {
    if (!feed_url || subscription_count >= PODCAST_MAX_SUBSCRIPTIONS) {
        return -1;
    }

    // Check if already subscribed
    if (Podcast_isSubscribed(feed_url)) {
        return 0;  // Already subscribed, not an error
    }

    // Fetch the feed
    uint8_t* buffer = (uint8_t*)malloc(5 * 1024 * 1024);  // 5MB buffer for large RSS feeds
    if (!buffer) {
        snprintf(error_message, sizeof(error_message), "Out of memory");
        return -1;
    }

    int bytes = radio_net_fetch(feed_url, buffer, 5 * 1024 * 1024, NULL, 0);
    if (bytes <= 0) {
        LOG_error("[Podcast] Failed to fetch feed: %s\n", feed_url);
        free(buffer);
        snprintf(error_message, sizeof(error_message), "Failed to fetch feed");
        return -1;
    }

    // Allocate temporary episodes array (parse all episodes from feed)
    // Using 2000 as a reasonable max - most podcasts have far fewer
    int max_episodes = 2000;
    PodcastEpisode* temp_episodes = (PodcastEpisode*)malloc(max_episodes * sizeof(PodcastEpisode));
    if (!temp_episodes) {
        free(buffer);
        snprintf(error_message, sizeof(error_message), "Out of memory");
        return -1;
    }

    // Parse RSS into feed and episodes array
    PodcastFeed temp_feed;
    memset(&temp_feed, 0, sizeof(PodcastFeed));
    strncpy(temp_feed.feed_url, feed_url, PODCAST_MAX_URL - 1);

    int episode_count = 0;
    int result = podcast_rss_parse_with_episodes((const char*)buffer, bytes, &temp_feed,
                                                  temp_episodes, max_episodes, &episode_count);
    free(buffer);

    if (result != 0) {
        LOG_error("[Podcast] Failed to parse feed: %s\n", feed_url);
        free(temp_episodes);
        snprintf(error_message, sizeof(error_message), "Invalid RSS feed");
        return -1;
    }

    // Generate feed_id
    Podcast_generateFeedId(feed_url, temp_feed.feed_id, sizeof(temp_feed.feed_id));
    temp_feed.last_updated = (uint32_t)time(NULL);
    temp_feed.episode_count = episode_count;

    // Add to subscriptions
    pthread_mutex_lock(&subscriptions_mutex);
    int feed_index = subscription_count;
    memcpy(&subscriptions[feed_index], &temp_feed, sizeof(PodcastFeed));
    subscription_count++;
    pthread_mutex_unlock(&subscriptions_mutex);

    // Save episodes to disk
    if (episode_count > 0) {
        Podcast_saveEpisodes(feed_index, temp_episodes, episode_count);
    }
    free(temp_episodes);

    Podcast_saveSubscriptions();
    LOG_info("[Podcast] Subscribed to: %s (%d episodes)\n", temp_feed.title, episode_count);
    return 0;
}

int Podcast_subscribeFromItunes(const char* itunes_id) {
    if (!itunes_id) {
        LOG_error("[Podcast] subscribeFromItunes: null itunes_id\n");
        return -1;
    }

    LOG_info("[Podcast] subscribeFromItunes: itunes_id=%s\n", itunes_id);

    // Check if already subscribed by iTunes ID
    if (Podcast_isSubscribedByItunesId(itunes_id)) {
        LOG_info("[Podcast] subscribeFromItunes: already subscribed\n");
        return 0;  // Already subscribed
    }

    char feed_url[PODCAST_MAX_URL];
    char artwork_url[PODCAST_MAX_URL] = {0};

    // Get both feed URL and artwork URL from iTunes lookup
    if (podcast_search_lookup_full(itunes_id, feed_url, sizeof(feed_url),
                                   artwork_url, sizeof(artwork_url)) != 0) {
        LOG_error("[Podcast] subscribeFromItunes: lookup failed for itunes_id=%s\n", itunes_id);
        snprintf(error_message, sizeof(error_message), "Failed to lookup podcast");
        return -1;
    }

    LOG_info("[Podcast] subscribeFromItunes: got feed_url=%s\n", feed_url);

    int result = Podcast_subscribe(feed_url);
    LOG_info("[Podcast] subscribeFromItunes: Podcast_subscribe returned %d\n", result);

    if (result == 0 && subscription_count > 0) {
        PodcastFeed* feed = &subscriptions[subscription_count - 1];
        // Store iTunes ID
        strncpy(feed->itunes_id, itunes_id, sizeof(feed->itunes_id) - 1);
        // Always prefer iTunes artwork (guaranteed 400x400) over RSS artwork
        if (artwork_url[0]) {
            strncpy(feed->artwork_url, artwork_url, sizeof(feed->artwork_url) - 1);
            LOG_info("[Podcast] Using iTunes artwork (400x400): %s\n", artwork_url);
        }
        Podcast_saveSubscriptions();  // Save again with iTunes ID and artwork
    }
    return result;
}

int Podcast_unsubscribe(int index) {
    if (index < 0 || index >= subscription_count) return -1;

    pthread_mutex_lock(&subscriptions_mutex);

    // Shift remaining subscriptions
    for (int i = index; i < subscription_count - 1; i++) {
        memcpy(&subscriptions[i], &subscriptions[i + 1], sizeof(PodcastFeed));
    }
    subscription_count--;

    pthread_mutex_unlock(&subscriptions_mutex);

    Podcast_saveSubscriptions();
    return 0;
}

bool Podcast_isSubscribed(const char* feed_url) {
    if (!feed_url) return false;
    for (int i = 0; i < subscription_count; i++) {
        if (strcmp(subscriptions[i].feed_url, feed_url) == 0) {
            return true;
        }
    }
    return false;
}

bool Podcast_isSubscribedByItunesId(const char* itunes_id) {
    if (!itunes_id || !itunes_id[0]) return false;
    for (int i = 0; i < subscription_count; i++) {
        if (subscriptions[i].itunes_id[0] && strcmp(subscriptions[i].itunes_id, itunes_id) == 0) {
            return true;
        }
    }
    return false;
}

int Podcast_refreshFeed(int index) {
    if (index < 0 || index >= subscription_count) return -1;

    PodcastFeed* feed = &subscriptions[index];

    uint8_t* buffer = (uint8_t*)malloc(5 * 1024 * 1024);  // 5MB buffer for large RSS feeds
    if (!buffer) return -1;

    int bytes = radio_net_fetch(feed->feed_url, buffer, 5 * 1024 * 1024, NULL, 0);
    if (bytes <= 0) {
        free(buffer);
        return -1;
    }

    // Allocate temporary episodes array
    int max_episodes = 2000;
    PodcastEpisode* new_episodes = (PodcastEpisode*)malloc(max_episodes * sizeof(PodcastEpisode));
    if (!new_episodes) {
        free(buffer);
        return -1;
    }

    // Parse into temporary feed
    PodcastFeed temp_feed;
    memset(&temp_feed, 0, sizeof(temp_feed));
    strncpy(temp_feed.feed_url, feed->feed_url, PODCAST_MAX_URL - 1);

    int new_episode_count = 0;
    if (podcast_rss_parse_with_episodes((const char*)buffer, bytes, &temp_feed,
                                        new_episodes, max_episodes, &new_episode_count) == 0) {
        // Load existing episodes to preserve progress/downloaded status
        char episodes_path[512];
        if (!feed->feed_id[0]) {
            Podcast_generateFeedId(feed->feed_url, feed->feed_id, sizeof(feed->feed_id));
        }
        get_episodes_file_path(feed->feed_id, episodes_path, sizeof(episodes_path));

        JSON_Value* old_root = json_parse_file(episodes_path);
        if (old_root) {
            JSON_Array* old_arr = json_value_get_array(old_root);
            if (old_arr) {
                int old_count = json_array_get_count(old_arr);
                // Preserve progress for matching episodes
                for (int i = 0; i < new_episode_count; i++) {
                    for (int j = 0; j < old_count; j++) {
                        JSON_Object* old_ep = json_array_get_object(old_arr, j);
                        const char* old_guid = json_object_get_string(old_ep, "guid");
                        if (old_guid && strcmp(new_episodes[i].guid, old_guid) == 0) {
                            new_episodes[i].progress_sec = (int)json_object_get_number(old_ep, "progress");
                            new_episodes[i].downloaded = json_object_get_boolean(old_ep, "downloaded");
                            const char* local = json_object_get_string(old_ep, "local_path");
                            if (local) strncpy(new_episodes[i].local_path, local, PODCAST_MAX_URL - 1);
                            break;
                        }
                    }
                }
            }
            json_value_free(old_root);
        }

        // Update feed metadata
        pthread_mutex_lock(&subscriptions_mutex);
        strncpy(feed->title, temp_feed.title, PODCAST_MAX_TITLE - 1);
        strncpy(feed->author, temp_feed.author, PODCAST_MAX_AUTHOR - 1);
        strncpy(feed->description, temp_feed.description, PODCAST_MAX_DESCRIPTION - 1);
        // Only update artwork if we didn't have it from iTunes
        if (!feed->artwork_url[0] && temp_feed.artwork_url[0]) {
            strncpy(feed->artwork_url, temp_feed.artwork_url, PODCAST_MAX_URL - 1);
        }
        feed->episode_count = new_episode_count;
        feed->last_updated = (uint32_t)time(NULL);
        pthread_mutex_unlock(&subscriptions_mutex);

        // Save new episodes to disk
        Podcast_saveEpisodes(index, new_episodes, new_episode_count);

        // Invalidate cache if this feed was cached
        if (episode_cache_feed_index == index) {
            Podcast_invalidateEpisodeCache();
        }
    }

    free(new_episodes);
    free(buffer);
    return 0;
}

int Podcast_refreshAllFeeds(void) {
    int success = 0;
    for (int i = 0; i < subscription_count; i++) {
        if (Podcast_refreshFeed(i) == 0) success++;
    }
    Podcast_saveSubscriptions();
    return success;
}

void Podcast_saveSubscriptions(void) {
    JSON_Value* root = json_value_init_array();
    JSON_Array* arr = json_value_get_array(root);

    pthread_mutex_lock(&subscriptions_mutex);
    for (int i = 0; i < subscription_count; i++) {
        PodcastFeed* feed = &subscriptions[i];

        // Generate feed_id if not set
        if (!feed->feed_id[0]) {
            Podcast_generateFeedId(feed->feed_url, feed->feed_id, sizeof(feed->feed_id));
        }

        JSON_Value* feed_val = json_value_init_object();
        JSON_Object* feed_obj = json_value_get_object(feed_val);

        json_object_set_string(feed_obj, "feed_url", feed->feed_url);
        json_object_set_string(feed_obj, "feed_id", feed->feed_id);
        json_object_set_string(feed_obj, "itunes_id", feed->itunes_id);
        json_object_set_string(feed_obj, "title", feed->title);
        json_object_set_string(feed_obj, "author", feed->author);
        json_object_set_string(feed_obj, "description", feed->description);
        json_object_set_string(feed_obj, "artwork_url", feed->artwork_url);
        json_object_set_number(feed_obj, "last_updated", feed->last_updated);
        json_object_set_number(feed_obj, "episode_count", feed->episode_count);
        // Note: episodes are stored separately in <feed_id>/episodes.json

        json_array_append_value(arr, feed_val);
    }
    pthread_mutex_unlock(&subscriptions_mutex);

    json_serialize_to_file_pretty(root, subscriptions_file);
    json_value_free(root);
}

void Podcast_loadSubscriptions(void) {
    JSON_Value* root = json_parse_file(subscriptions_file);
    if (!root) return;

    JSON_Array* arr = json_value_get_array(root);
    if (!arr) {
        json_value_free(root);
        return;
    }

    pthread_mutex_lock(&subscriptions_mutex);
    subscription_count = 0;

    int count = json_array_get_count(arr);
    for (int i = 0; i < count && subscription_count < PODCAST_MAX_SUBSCRIPTIONS; i++) {
        JSON_Object* feed_obj = json_array_get_object(arr, i);
        if (!feed_obj) continue;

        PodcastFeed* feed = &subscriptions[subscription_count];
        memset(feed, 0, sizeof(PodcastFeed));

        const char* str;
        str = json_object_get_string(feed_obj, "feed_url");
        if (str) strncpy(feed->feed_url, str, PODCAST_MAX_URL - 1);
        str = json_object_get_string(feed_obj, "feed_id");
        if (str) strncpy(feed->feed_id, str, sizeof(feed->feed_id) - 1);
        str = json_object_get_string(feed_obj, "itunes_id");
        if (str) strncpy(feed->itunes_id, str, sizeof(feed->itunes_id) - 1);
        str = json_object_get_string(feed_obj, "title");
        if (str) strncpy(feed->title, str, PODCAST_MAX_TITLE - 1);
        str = json_object_get_string(feed_obj, "author");
        if (str) strncpy(feed->author, str, PODCAST_MAX_AUTHOR - 1);
        str = json_object_get_string(feed_obj, "description");
        if (str) strncpy(feed->description, str, PODCAST_MAX_DESCRIPTION - 1);
        str = json_object_get_string(feed_obj, "artwork_url");
        if (str) strncpy(feed->artwork_url, str, PODCAST_MAX_URL - 1);

        feed->last_updated = (uint32_t)json_object_get_number(feed_obj, "last_updated");
        feed->episode_count = (int)json_object_get_number(feed_obj, "episode_count");

        // Generate feed_id if not loaded (for backward compatibility)
        if (!feed->feed_id[0]) {
            Podcast_generateFeedId(feed->feed_url, feed->feed_id, sizeof(feed->feed_id));
        }

        // Note: episodes are loaded on-demand via Podcast_getEpisode()

        subscription_count++;
    }
    pthread_mutex_unlock(&subscriptions_mutex);

    json_value_free(root);
}

// ============================================================================
// Search API
// ============================================================================

int Podcast_startSearch(const char* query) {
    if (!query || search_running) {
        return -1;
    }

    // Reset status
    memset(&search_status, 0, sizeof(search_status));
    search_status.searching = true;
    search_result_count = 0;

    strncpy(search_query_copy, query, sizeof(search_query_copy) - 1);
    search_query_copy[sizeof(search_query_copy) - 1] = '\0';

    search_should_stop = false;
    search_running = true;

    if (pthread_create(&search_thread, NULL, search_thread_func, NULL) != 0) {
        search_running = false;
        search_status.searching = false;
        snprintf(search_status.error_message, sizeof(search_status.error_message), "Failed to start search");
        return -1;
    }

    pthread_detach(search_thread);
    podcast_state = PODCAST_STATE_SEARCHING;
    return 0;
}

static void* search_thread_func(void* arg) {
    (void)arg;

    int count = podcast_search_itunes(search_query_copy, search_results, PODCAST_MAX_SEARCH_RESULTS);

    if (search_should_stop) {
        search_running = false;
        return NULL;
    }

    if (count < 0) {
        search_status.result_count = -1;
        snprintf(search_status.error_message, sizeof(search_status.error_message), "Search failed");
    } else {
        search_result_count = count;
        search_status.result_count = count;
    }

    search_running = false;
    podcast_state = PODCAST_STATE_IDLE;
    return NULL;
}

const PodcastSearchStatus* Podcast_getSearchStatus(void) {
    return &search_status;
}

PodcastSearchResult* Podcast_getSearchResults(int* count) {
    if (count) *count = search_result_count;
    return search_results;
}

void Podcast_cancelSearch(void) {
    if (search_running) {
        search_should_stop = true;
        // Wait briefly for thread to notice
        for (int i = 0; i < 10 && search_running; i++) {
            usleep(50000);  // 50ms
        }
    }
    search_status.searching = false;
}

// ============================================================================
// Charts API
// ============================================================================

int Podcast_loadCharts(const char* country_code) {
    if (charts_running) {
        return -1;
    }

    if (country_code) {
        strncpy(charts_country_code, country_code, sizeof(charts_country_code) - 1);
        charts_country_code[sizeof(charts_country_code) - 1] = '\0';
    }

    memset(&charts_status, 0, sizeof(charts_status));

    // Try to load from cache first (daily cache)
    if (load_charts_cache()) {
        // Cache hit - no need to fetch from network
        charts_status.top_shows_count = top_shows_count;
        charts_status.loading = false;
        charts_status.completed = true;
        LOG_info("[Podcast] Using cached charts data\n");
        return 0;
    }

    // Cache miss or expired - fetch from network
    charts_status.loading = true;
    charts_should_stop = false;
    charts_running = true;

    if (pthread_create(&charts_thread, NULL, charts_thread_func, NULL) != 0) {
        charts_running = false;
        charts_status.loading = false;
        snprintf(charts_status.error_message, sizeof(charts_status.error_message), "Failed to load charts");
        return -1;
    }

    pthread_detach(charts_thread);
    podcast_state = PODCAST_STATE_LOADING_CHARTS;
    return 0;
}

// Save charts to cache file
static void save_charts_cache(void) {
    JSON_Value* root = json_value_init_object();
    JSON_Object* obj = json_value_get_object(root);

    // Save timestamp and country
    json_object_set_number(obj, "timestamp", (double)time(NULL));
    json_object_set_string(obj, "country", charts_country_code);

    // Save top shows
    JSON_Value* top_arr_val = json_value_init_array();
    JSON_Array* top_arr = json_value_get_array(top_arr_val);
    for (int i = 0; i < top_shows_count; i++) {
        JSON_Value* item_val = json_value_init_object();
        JSON_Object* item = json_value_get_object(item_val);
        json_object_set_string(item, "itunes_id", top_shows[i].itunes_id);
        json_object_set_string(item, "title", top_shows[i].title);
        json_object_set_string(item, "author", top_shows[i].author);
        json_object_set_string(item, "artwork_url", top_shows[i].artwork_url);
        json_object_set_string(item, "genre", top_shows[i].genre);
        json_object_set_string(item, "feed_url", top_shows[i].feed_url);
        json_array_append_value(top_arr, item_val);
    }
    json_object_set_value(obj, "top_shows", top_arr_val);

    json_serialize_to_file_pretty(root, charts_cache_file);
    json_value_free(root);
    LOG_info("[Podcast] Saved charts cache with %d top shows\n", top_shows_count);
}

// Load charts from cache if valid (within 24 hours and same country)
// Returns true if cache was loaded successfully
static bool load_charts_cache(void) {
    JSON_Value* root = json_parse_file(charts_cache_file);
    if (!root) {
        LOG_info("[Podcast] No charts cache found\n");
        return false;
    }

    JSON_Object* obj = json_value_get_object(root);
    if (!obj) {
        json_value_free(root);
        return false;
    }

    // Check timestamp - cache valid for 24 hours
    double timestamp = json_object_get_number(obj, "timestamp");
    time_t now = time(NULL);
    time_t cache_age = now - (time_t)timestamp;
    if (cache_age > 24 * 60 * 60) {  // 24 hours in seconds
        LOG_info("[Podcast] Charts cache expired (age: %ld seconds)\n", (long)cache_age);
        json_value_free(root);
        return false;
    }

    // Check country matches
    const char* cached_country = json_object_get_string(obj, "country");
    if (!cached_country || strcmp(cached_country, charts_country_code) != 0) {
        LOG_info("[Podcast] Charts cache country mismatch (cached: %s, current: %s)\n",
                 cached_country ? cached_country : "null", charts_country_code);
        json_value_free(root);
        return false;
    }

    // Load top shows
    JSON_Array* top_arr = json_object_get_array(obj, "top_shows");
    if (top_arr) {
        top_shows_count = 0;
        int count = json_array_get_count(top_arr);
        for (int i = 0; i < count && top_shows_count < PODCAST_MAX_CHART_ITEMS; i++) {
            JSON_Object* item = json_array_get_object(top_arr, i);
            if (!item) continue;

            PodcastChartItem* show = &top_shows[top_shows_count];
            memset(show, 0, sizeof(PodcastChartItem));

            const char* s;
            if ((s = json_object_get_string(item, "itunes_id"))) strncpy(show->itunes_id, s, sizeof(show->itunes_id) - 1);
            if ((s = json_object_get_string(item, "title"))) strncpy(show->title, s, PODCAST_MAX_TITLE - 1);
            if ((s = json_object_get_string(item, "author"))) strncpy(show->author, s, PODCAST_MAX_AUTHOR - 1);
            if ((s = json_object_get_string(item, "artwork_url"))) strncpy(show->artwork_url, s, PODCAST_MAX_URL - 1);
            if ((s = json_object_get_string(item, "genre"))) strncpy(show->genre, s, PODCAST_MAX_GENRE - 1);
            if ((s = json_object_get_string(item, "feed_url"))) strncpy(show->feed_url, s, PODCAST_MAX_URL - 1);

            top_shows_count++;
        }
    }

    json_value_free(root);
    LOG_info("[Podcast] Loaded charts from cache: %d top shows (age: %ld seconds)\n",
             top_shows_count, (long)cache_age);
    return (top_shows_count > 0);
}

static void* charts_thread_func(void* arg) {
    (void)arg;

    int top_count = 0;
    int result = podcast_charts_fetch(charts_country_code, top_shows, &top_count,
                                       NULL, NULL, PODCAST_MAX_CHART_ITEMS);

    if (charts_should_stop) {
        charts_running = false;
        return NULL;
    }

    if (result < 0) {
        snprintf(charts_status.error_message, sizeof(charts_status.error_message), "Failed to fetch charts");
    } else {
        top_shows_count = top_count;
        charts_status.top_shows_count = top_count;

        // Save to cache after successful fetch
        save_charts_cache();
    }

    charts_running = false;
    charts_status.loading = false;
    charts_status.completed = true;
    podcast_state = PODCAST_STATE_IDLE;
    return NULL;
}

const PodcastChartsStatus* Podcast_getChartsStatus(void) {
    return &charts_status;
}

PodcastChartItem* Podcast_getTopShows(int* count) {
    if (count) *count = top_shows_count;
    return top_shows;
}

const char* Podcast_getCountryCode(void) {
    return charts_country_code;
}

// ============================================================================
// Playback (local files only - streaming removed)
// ============================================================================

int Podcast_play(PodcastFeed* feed, int episode_index) {
    if (!feed || episode_index < 0 || episode_index >= feed->episode_count) {
        return -1;
    }

    int feed_idx = get_feed_index(feed);
    if (feed_idx < 0) {
        return -1;
    }

    PodcastEpisode* ep = Podcast_getEpisode(feed_idx, episode_index);
    if (!ep) {
        return -1;
    }

    // Check if local file exists
    char local_path[PODCAST_MAX_URL];
    Podcast_getEpisodeLocalPath(feed, episode_index, local_path, sizeof(local_path));

    if (access(local_path, F_OK) != 0) {
        snprintf(error_message, sizeof(error_message), "Episode not downloaded");
        return -1;  // File doesn't exist - caller should start download
    }

    // Store current feed and episode for later reference
    current_feed = feed;
    current_feed_index = feed_idx;
    current_episode_index = episode_index;

    if (Player_load(local_path) == 0) {
        Player_play();
        streaming_status.streaming = true;
        streaming_status.buffering = false;
        streaming_status.buffer_percent = 100;
        streaming_status.duration_sec = ep->duration_sec;

        // Seek to saved position
        if (ep->progress_sec > 0) {
            // Player_seek would need to be implemented
        }
        LOG_info("[Podcast] Playing local file: %s\n", ep->title);
        return 0;
    }

    snprintf(error_message, sizeof(error_message), "Failed to load local file");
    return -1;
}

void Podcast_stop(void) {
    if (current_feed && current_feed_index >= 0 && current_episode_index >= 0) {
        // Save progress
        PodcastEpisode* ep = Podcast_getEpisode(current_feed_index, current_episode_index);
        if (ep) {
            int position = Podcast_getPosition();
            if (position > 0) {
                ep->progress_sec = position / 1000;  // Convert ms to sec
                Podcast_saveProgress(current_feed->feed_url, ep->guid, ep->progress_sec);
            }
        }
    }

    Player_stop();

    streaming_status.streaming = false;
    streaming_status.buffering = false;
    streaming_status.buffer_percent = 0;
    podcast_state = PODCAST_STATE_IDLE;
    current_feed = NULL;
    current_feed_index = -1;
    current_episode_index = -1;
}

void Podcast_pause(void) {
    Player_pause();
}

void Podcast_resume(void) {
    Player_play();  // Player_play() resumes if paused
}

bool Podcast_isPaused(void) {
    return Player_getState() == PLAYER_STATE_PAUSED;
}

void Podcast_seek(int position_ms) {
    // Only seek for local files, not streams
    if (!streaming_status.streaming && Player_getState() != PLAYER_STATE_STOPPED) {
        Player_seek(position_ms);
    }
}

const PodcastStreamingStatus* Podcast_getStreamingStatus(void) {
    return &streaming_status;
}

int Podcast_getPosition(void) {
    return Player_getPosition();
}

int Podcast_getDuration(void) {
    // Return stored duration from episode metadata if available
    if (streaming_status.duration_sec > 0) {
        return streaming_status.duration_sec * 1000;  // Convert to ms
    }
    return Player_getDuration();
}

float Podcast_getBufferLevel(void) {
    return 1.0f;  // For local files, always fully buffered
}

int Podcast_getAudioSamples(int16_t* buffer, int max_samples) {
    // Not used for local playback - Player handles audio
    (void)buffer;
    (void)max_samples;
    return 0;
}

bool Podcast_isActive(void) {
    if (streaming_status.streaming && Player_getState() != PLAYER_STATE_STOPPED) {
        return true;
    }
    return false;
}

bool Podcast_isStreaming(void) {
    // Streaming removed - always returns false
    return false;
}

bool Podcast_isBuffering(void) {
    return streaming_status.buffering;
}

// ============================================================================
// Progress Tracking
// ============================================================================

void Podcast_saveProgress(const char* feed_url, const char* episode_guid, int position_sec) {
    if (!feed_url || !episode_guid) return;

    // Find existing entry or add new one
    for (int i = 0; i < progress_entry_count; i++) {
        if (strcmp(progress_entries[i].feed_url, feed_url) == 0 &&
            strcmp(progress_entries[i].episode_guid, episode_guid) == 0) {
            progress_entries[i].position_sec = position_sec;
            return;
        }
    }

    // Add new entry
    if (progress_entry_count < MAX_PROGRESS_ENTRIES) {
        strncpy(progress_entries[progress_entry_count].feed_url, feed_url, PODCAST_MAX_URL - 1);
        strncpy(progress_entries[progress_entry_count].episode_guid, episode_guid, PODCAST_MAX_GUID - 1);
        progress_entries[progress_entry_count].position_sec = position_sec;
        progress_entry_count++;
    }
}

int Podcast_getProgress(const char* feed_url, const char* episode_guid) {
    if (!feed_url || !episode_guid) return 0;

    for (int i = 0; i < progress_entry_count; i++) {
        if (strcmp(progress_entries[i].feed_url, feed_url) == 0 &&
            strcmp(progress_entries[i].episode_guid, episode_guid) == 0) {
            return progress_entries[i].position_sec;
        }
    }
    return 0;
}

void Podcast_markAsPlayed(const char* feed_url, const char* episode_guid) {
    // Mark as played by setting progress to -1 (special value)
    Podcast_saveProgress(feed_url, episode_guid, -1);
}

// ============================================================================
// Download Queue
// ============================================================================

// Helper to sanitize string for filesystem (removes problematic chars)
static void sanitize_for_filename(char* str) {
    for (char* p = str; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ':' || *p == '*' ||
            *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|') {
            *p = '_';
        }
    }
}

// Generate local file path for an episode
void Podcast_getEpisodeLocalPath(PodcastFeed* feed, int episode_index, char* buf, int buf_size) {
    if (!feed || episode_index < 0 || episode_index >= feed->episode_count || !buf) {
        if (buf && buf_size > 0) buf[0] = '\0';
        return;
    }

    int feed_idx = get_feed_index(feed);
    PodcastEpisode* ep = (feed_idx >= 0) ? Podcast_getEpisode(feed_idx, episode_index) : NULL;
    if (!ep) {
        if (buf_size > 0) buf[0] = '\0';
        return;
    }

    char safe_title[256];
    strncpy(safe_title, ep->title, sizeof(safe_title) - 1);
    safe_title[sizeof(safe_title) - 1] = '\0';
    sanitize_for_filename(safe_title);

    char safe_feed[256];
    strncpy(safe_feed, feed->title, sizeof(safe_feed) - 1);
    safe_feed[sizeof(safe_feed) - 1] = '\0';
    sanitize_for_filename(safe_feed);

    snprintf(buf, buf_size, "%s/%s/%s.mp3", download_dir, safe_feed, safe_title);
}

// Check if episode file exists locally
bool Podcast_episodeFileExists(PodcastFeed* feed, int episode_index) {
    char local_path[PODCAST_MAX_URL];
    Podcast_getEpisodeLocalPath(feed, episode_index, local_path, sizeof(local_path));
    if (local_path[0] == '\0') return false;
    return access(local_path, F_OK) == 0;
}

// Get download status for a specific episode
int Podcast_getEpisodeDownloadStatus(const char* feed_url, const char* episode_guid, int* progress_out) {
    if (!feed_url || !episode_guid) return -1;

    pthread_mutex_lock(&download_mutex);
    for (int i = 0; i < download_queue_count; i++) {
        if (strcmp(download_queue[i].feed_url, feed_url) == 0 &&
            strcmp(download_queue[i].episode_guid, episode_guid) == 0) {
            int status = download_queue[i].status;
            int progress = download_queue[i].progress_percent;
            if (progress_out) {
                *progress_out = progress;
            }
            pthread_mutex_unlock(&download_mutex);
            // Debug: log when we find a downloading item
            if (status == PODCAST_DOWNLOAD_DOWNLOADING) {
                LOG_info("[Podcast] getEpisodeDownloadStatus: found DOWNLOADING, progress=%d%%\n", progress);
            }
            return status;
        }
    }
    pthread_mutex_unlock(&download_mutex);

    if (progress_out) *progress_out = 0;
    return -1;  // Not in queue
}

// Cancel a specific episode download
int Podcast_cancelEpisodeDownload(const char* feed_url, const char* episode_guid) {
    if (!feed_url || !episode_guid) return -1;

    pthread_mutex_lock(&download_mutex);
    for (int i = 0; i < download_queue_count; i++) {
        if (strcmp(download_queue[i].feed_url, feed_url) == 0 &&
            strcmp(download_queue[i].episode_guid, episode_guid) == 0) {
            // If currently downloading, signal stop
            if (download_queue[i].status == PODCAST_DOWNLOAD_DOWNLOADING) {
                download_should_stop = true;
            }
            // Remove from queue by shifting
            for (int j = i; j < download_queue_count - 1; j++) {
                memcpy(&download_queue[j], &download_queue[j + 1], sizeof(PodcastDownloadItem));
            }
            download_queue_count--;
            pthread_mutex_unlock(&download_mutex);
            Podcast_saveDownloadQueue();
            return 0;
        }
    }
    pthread_mutex_unlock(&download_mutex);
    return -1;  // Not found
}

int Podcast_queueDownload(PodcastFeed* feed, int episode_index) {
    if (!feed || episode_index < 0 || episode_index >= feed->episode_count) {
        return -1;
    }
    if (download_queue_count >= PODCAST_MAX_DOWNLOAD_QUEUE) {
        return -1;
    }

    int feed_idx = get_feed_index(feed);
    PodcastEpisode* ep = (feed_idx >= 0) ? Podcast_getEpisode(feed_idx, episode_index) : NULL;
    if (!ep) {
        return -1;
    }
    LOG_info("[Podcast] queueDownload: episode=%s, guid=%s\n", ep->title, ep->guid);

    // Check if already in download queue (only block if PENDING or DOWNLOADING)
    pthread_mutex_lock(&download_mutex);
    for (int i = 0; i < download_queue_count; i++) {
        if (strcmp(download_queue[i].episode_guid, ep->guid) == 0) {
            if (download_queue[i].status == PODCAST_DOWNLOAD_PENDING ||
                download_queue[i].status == PODCAST_DOWNLOAD_DOWNLOADING) {
                LOG_info("[Podcast] queueDownload: already in queue (status=%d)\n", download_queue[i].status);
                pthread_mutex_unlock(&download_mutex);
                return 0;  // Already queued and active
            }
            // Remove completed/failed item to allow re-download
            LOG_info("[Podcast] queueDownload: removing old item (status=%d)\n", download_queue[i].status);
            for (int j = i; j < download_queue_count - 1; j++) {
                memcpy(&download_queue[j], &download_queue[j + 1], sizeof(PodcastDownloadItem));
            }
            download_queue_count--;
            break;
        }
    }

    PodcastDownloadItem* item = &download_queue[download_queue_count];
    memset(item, 0, sizeof(PodcastDownloadItem));

    strncpy(item->feed_title, feed->title, PODCAST_MAX_TITLE - 1);
    strncpy(item->feed_url, feed->feed_url, PODCAST_MAX_URL - 1);
    strncpy(item->episode_title, ep->title, PODCAST_MAX_TITLE - 1);
    strncpy(item->episode_guid, ep->guid, PODCAST_MAX_GUID - 1);
    strncpy(item->url, ep->url, PODCAST_MAX_URL - 1);

    // Generate local path
    Podcast_getEpisodeLocalPath(feed, episode_index, item->local_path, sizeof(item->local_path));

    item->status = PODCAST_DOWNLOAD_PENDING;
    item->progress_percent = 0;
    download_queue_count++;
    LOG_info("[Podcast] queueDownload: added to queue, count=%d, status=%d\n",
             download_queue_count, item->status);

    pthread_mutex_unlock(&download_mutex);

    Podcast_saveDownloadQueue();

    // Auto-start downloads if not already running
    if (!download_running) {
        LOG_info("[Podcast] queueDownload: auto-starting downloads\n");
        Podcast_startDownloads();
    } else {
        LOG_info("[Podcast] queueDownload: downloads already running\n");
    }

    return 0;
}

// Download episode immediately (convenience wrapper)
int Podcast_downloadEpisode(PodcastFeed* feed, int episode_index) {
    LOG_info("[Podcast] downloadEpisode called: feed=%s, index=%d\n",
             feed ? feed->title : "NULL", episode_index);
    int result = Podcast_queueDownload(feed, episode_index);
    LOG_info("[Podcast] downloadEpisode result: %d, queue_count=%d\n", result, download_queue_count);
    return result;
}

int Podcast_removeDownload(int index) {
    if (index < 0 || index >= download_queue_count) return -1;

    pthread_mutex_lock(&download_mutex);
    for (int i = index; i < download_queue_count - 1; i++) {
        memcpy(&download_queue[i], &download_queue[i + 1], sizeof(PodcastDownloadItem));
    }
    download_queue_count--;
    pthread_mutex_unlock(&download_mutex);

    Podcast_saveDownloadQueue();
    return 0;
}

void Podcast_clearDownloadQueue(void) {
    pthread_mutex_lock(&download_mutex);
    download_queue_count = 0;
    pthread_mutex_unlock(&download_mutex);
    Podcast_saveDownloadQueue();
}

PodcastDownloadItem* Podcast_getDownloadQueue(int* count) {
    if (count) *count = download_queue_count;
    return download_queue;
}

int Podcast_startDownloads(void) {
    LOG_info("[Podcast] startDownloads called: running=%d, queue_count=%d\n",
             download_running, download_queue_count);
    if (download_running || download_queue_count == 0) {
        LOG_info("[Podcast] startDownloads skipped (already running or empty)\n");
        return -1;
    }

    memset(&download_progress, 0, sizeof(download_progress));
    download_progress.total_items = download_queue_count;

    download_should_stop = false;
    download_running = true;

    if (pthread_create(&download_thread, NULL, download_thread_func, NULL) != 0) {
        LOG_error("[Podcast] Failed to create download thread\n");
        download_running = false;
        return -1;
    }
    LOG_info("[Podcast] Download thread started\n");

    pthread_detach(download_thread);
    podcast_state = PODCAST_STATE_DOWNLOADING;
    return 0;
}


static void* download_thread_func(void* arg) {
    (void)arg;

    for (int i = 0; i < download_queue_count && !download_should_stop; i++) {
        PodcastDownloadItem* item = &download_queue[i];

        if (item->status != PODCAST_DOWNLOAD_PENDING) {
            continue;
        }

        // Ensure WiFi is connected before each download
        if (!ensure_wifi_connected(NULL, 0)) {
            LOG_error("[Podcast] No network connection, skipping download: %s\n", item->episode_title);
            item->status = PODCAST_DOWNLOAD_FAILED;
            download_progress.failed_count++;
            snprintf(download_progress.error_message, sizeof(download_progress.error_message),
                     "No network connection");
            continue;
        }

        download_progress.current_index = i;
        strncpy(download_progress.current_title, item->episode_title, PODCAST_MAX_TITLE - 1);
        item->status = PODCAST_DOWNLOAD_DOWNLOADING;
        item->progress_percent = 0;

        // Create directory for podcast (sanitize feed title for directory name)
        char safe_feed[256];
        strncpy(safe_feed, item->feed_title, sizeof(safe_feed) - 1);
        safe_feed[sizeof(safe_feed) - 1] = '\0';
        sanitize_for_filename(safe_feed);

        char dir_path[512];
        snprintf(dir_path, sizeof(dir_path), "%s/%s", download_dir, safe_feed);
        mkdir(dir_path, 0755);

        LOG_info("[Podcast] Downloading: %s\n", item->episode_title);

        // Use chunked download that writes directly to file with progress tracking
        int bytes = podcast_download_to_file(item->url, item->local_path,
                                             &item->progress_percent,
                                             &download_should_stop, 0);

        if (download_should_stop) {
            // Remove partial file if cancelled
            unlink(item->local_path);
            break;
        }

        if (bytes > 0) {
            item->status = PODCAST_DOWNLOAD_COMPLETE;
            item->progress_percent = 100;
            download_progress.completed_count++;
            LOG_info("[Podcast] Downloaded: %s (%d bytes)\n", item->episode_title, bytes);
        } else {
            item->status = PODCAST_DOWNLOAD_FAILED;
            download_progress.failed_count++;
            // Remove partial file on failure
            unlink(item->local_path);
            LOG_error("[Podcast] Failed to download: %s\n", item->url);
        }
    }

    // Remove completed and failed items from queue
    pthread_mutex_lock(&download_mutex);
    int write_idx = 0;
    for (int i = 0; i < download_queue_count; i++) {
        if (download_queue[i].status == PODCAST_DOWNLOAD_PENDING) {
            // Keep pending items (in case download was stopped mid-way)
            if (write_idx != i) {
                memcpy(&download_queue[write_idx], &download_queue[i], sizeof(PodcastDownloadItem));
            }
            write_idx++;
        }
        // COMPLETE and FAILED items are removed (not copied)
    }
    download_queue_count = write_idx;
    pthread_mutex_unlock(&download_mutex);

    download_running = false;
    podcast_state = PODCAST_STATE_IDLE;
    Podcast_saveDownloadQueue();
    LOG_info("[Podcast] Download thread finished, %d items remaining in queue\n", download_queue_count);
    return NULL;
}

void Podcast_stopDownloads(void) {
    if (download_running) {
        download_should_stop = true;
        for (int i = 0; i < 20 && download_running; i++) {
            usleep(100000);  // 100ms, wait up to 2 seconds
        }
    }

    // Reset any DOWNLOADING items to PENDING so they resume on next app start
    pthread_mutex_lock(&download_mutex);
    for (int i = 0; i < download_queue_count; i++) {
        if (download_queue[i].status == PODCAST_DOWNLOAD_DOWNLOADING) {
            download_queue[i].status = PODCAST_DOWNLOAD_PENDING;
            download_queue[i].progress_percent = 0;
            LOG_info("[Podcast] Reset interrupted download to pending: %s\n",
                     download_queue[i].episode_title);
        }
    }
    pthread_mutex_unlock(&download_mutex);
}

const PodcastDownloadProgress* Podcast_getDownloadProgress(void) {
    return &download_progress;
}

bool Podcast_isDownloaded(const char* feed_url, const char* episode_guid) {
    for (int i = 0; i < subscription_count; i++) {
        if (strcmp(subscriptions[i].feed_url, feed_url) == 0) {
            for (int j = 0; j < subscriptions[i].episode_count; j++) {
                PodcastEpisode* ep = Podcast_getEpisode(i, j);
                if (ep && strcmp(ep->guid, episode_guid) == 0) {
                    return ep->downloaded;
                }
            }
        }
    }
    return false;
}

// Static buffer for returning downloaded path (not ideal but matches original behavior)
static char downloaded_path_buf[PODCAST_MAX_URL];

const char* Podcast_getDownloadedPath(const char* feed_url, const char* episode_guid) {
    for (int i = 0; i < subscription_count; i++) {
        if (strcmp(subscriptions[i].feed_url, feed_url) == 0) {
            for (int j = 0; j < subscriptions[i].episode_count; j++) {
                PodcastEpisode* ep = Podcast_getEpisode(i, j);
                if (ep && strcmp(ep->guid, episode_guid) == 0) {
                    if (ep->downloaded && ep->local_path[0]) {
                        strncpy(downloaded_path_buf, ep->local_path, sizeof(downloaded_path_buf) - 1);
                        downloaded_path_buf[sizeof(downloaded_path_buf) - 1] = '\0';
                        return downloaded_path_buf;
                    }
                }
            }
        }
    }
    return NULL;
}

void Podcast_saveDownloadQueue(void) {
    JSON_Value* root = json_value_init_array();
    JSON_Array* arr = json_value_get_array(root);

    pthread_mutex_lock(&download_mutex);
    for (int i = 0; i < download_queue_count; i++) {
        PodcastDownloadItem* item = &download_queue[i];
        JSON_Value* val = json_value_init_object();
        JSON_Object* obj = json_value_get_object(val);

        json_object_set_string(obj, "feed_title", item->feed_title);
        json_object_set_string(obj, "feed_url", item->feed_url);
        json_object_set_string(obj, "episode_title", item->episode_title);
        json_object_set_string(obj, "episode_guid", item->episode_guid);
        json_object_set_string(obj, "url", item->url);
        json_object_set_string(obj, "local_path", item->local_path);
        json_object_set_number(obj, "status", item->status);
        json_object_set_number(obj, "progress", item->progress_percent);

        json_array_append_value(arr, val);
    }
    pthread_mutex_unlock(&download_mutex);

    json_serialize_to_file_pretty(root, downloads_file);
    json_value_free(root);
}

void Podcast_loadDownloadQueue(void) {
    JSON_Value* root = json_parse_file(downloads_file);
    if (!root) return;

    JSON_Array* arr = json_value_get_array(root);
    if (!arr) {
        json_value_free(root);
        return;
    }

    pthread_mutex_lock(&download_mutex);
    download_queue_count = 0;

    int count = json_array_get_count(arr);
    for (int i = 0; i < count && download_queue_count < PODCAST_MAX_DOWNLOAD_QUEUE; i++) {
        JSON_Object* obj = json_array_get_object(arr, i);
        if (!obj) continue;

        PodcastDownloadItem* item = &download_queue[download_queue_count];
        memset(item, 0, sizeof(PodcastDownloadItem));

        const char* str;
        str = json_object_get_string(obj, "feed_title");
        if (str) strncpy(item->feed_title, str, PODCAST_MAX_TITLE - 1);
        str = json_object_get_string(obj, "feed_url");
        if (str) strncpy(item->feed_url, str, PODCAST_MAX_URL - 1);
        str = json_object_get_string(obj, "episode_title");
        if (str) strncpy(item->episode_title, str, PODCAST_MAX_TITLE - 1);
        str = json_object_get_string(obj, "episode_guid");
        if (str) strncpy(item->episode_guid, str, PODCAST_MAX_GUID - 1);
        str = json_object_get_string(obj, "url");
        if (str) strncpy(item->url, str, PODCAST_MAX_URL - 1);
        str = json_object_get_string(obj, "local_path");
        if (str) strncpy(item->local_path, str, PODCAST_MAX_URL - 1);

        item->status = (PodcastDownloadStatus)(int)json_object_get_number(obj, "status");
        item->progress_percent = (int)json_object_get_number(obj, "progress");

        // Reset downloading status to pending
        if (item->status == PODCAST_DOWNLOAD_DOWNLOADING) {
            item->status = PODCAST_DOWNLOAD_PENDING;
            item->progress_percent = 0;
        }

        // Skip completed/failed items (don't load them into queue)
        if (item->status == PODCAST_DOWNLOAD_COMPLETE ||
            item->status == PODCAST_DOWNLOAD_FAILED) {
            LOG_info("[Podcast] loadDownloadQueue: skipping item with status %d\n", item->status);
            continue;  // Don't increment download_queue_count
        }

        download_queue_count++;
    }
    pthread_mutex_unlock(&download_mutex);

    LOG_info("[Podcast] loadDownloadQueue: loaded %d pending items\n", download_queue_count);
    json_value_free(root);
}

// ============================================================================
// Batch Download Functions
// ============================================================================

bool Podcast_isEpisodeDownloaded(PodcastFeed* feed, int episode_index) {
    if (!feed || episode_index < 0 || episode_index >= feed->episode_count) {
        return false;
    }
    int feed_idx = get_feed_index(feed);
    PodcastEpisode* ep = (feed_idx >= 0) ? Podcast_getEpisode(feed_idx, episode_index) : NULL;
    return ep ? ep->downloaded : false;
}

int Podcast_downloadLatest(int feed_index, int count) {
    if (feed_index < 0 || feed_index >= subscription_count) {
        return -1;
    }
    if (count <= 0 || count > 50) {
        count = 10;  // Default to 10 if invalid
    }

    PodcastFeed* feed = &subscriptions[feed_index];
    int queued = 0;

    // Queue the latest N episodes (episodes are typically sorted newest first in RSS)
    for (int i = 0; i < feed->episode_count && i < count; i++) {
        PodcastEpisode* ep = Podcast_getEpisode(feed_index, i);
        if (ep && !ep->downloaded) {
            if (Podcast_queueDownload(feed, i) == 0) {
                queued++;
            }
        }
    }

    if (queued > 0) {
        LOG_info("[Podcast] Queued %d episodes for download from: %s\n", queued, feed->title);
    }

    return queued;
}

int Podcast_autoDownloadNew(int feed_index) {
    if (feed_index < 0 || feed_index >= subscription_count) {
        return -1;
    }

    PodcastFeed* feed = &subscriptions[feed_index];

    // Get the last update time - episodes newer than this are "new"
    uint32_t last_check = feed->last_updated;
    if (last_check == 0) {
        // First time - don't auto-download all episodes
        return 0;
    }

    int queued = 0;

    // Queue episodes that are newer than our last update
    for (int i = 0; i < feed->episode_count; i++) {
        PodcastEpisode* ep = Podcast_getEpisode(feed_index, i);

        // Check if episode is newer than last check and not downloaded
        if (ep && ep->pub_date > last_check && !ep->downloaded) {
            if (Podcast_queueDownload(feed, i) == 0) {
                queued++;
            }
        }
    }

    if (queued > 0) {
        LOG_info("[Podcast] Auto-queued %d new episodes from: %s\n", queued, feed->title);
    }

    return queued;
}

// ============================================================================
// Episode Cache Persistence
// ============================================================================

// The episode cache is implemented via the subscription save/load system.
// Episodes are stored as part of each subscription in podcasts.json.
// This provides persistence across app restarts without needing to re-fetch RSS.

void Podcast_saveEpisodeCache(int feed_index) {
    if (feed_index < 0 || feed_index >= subscription_count) {
        return;
    }

    // Save all subscriptions (which includes episode data)
    // This is called after subscribe/refresh to persist episode list
    Podcast_saveSubscriptions();

    LOG_info("[Podcast] Saved episode cache for feed %d (%d episodes)\n",
             feed_index, subscriptions[feed_index].episode_count);
}

bool Podcast_loadEpisodeCache(int feed_index) {
    if (feed_index < 0 || feed_index >= subscription_count) {
        return false;
    }

    // Episodes are already loaded as part of subscriptions during init
    // Just check if we have cached episodes
    PodcastFeed* feed = &subscriptions[feed_index];

    if (feed->episode_count > 0) {
        LOG_info("[Podcast] Using cached episodes for: %s (%d episodes)\n",
                 feed->title, feed->episode_count);
        return true;
    }

    // No cached episodes - need to refresh from network
    return false;
}

bool Podcast_hasEpisodeCache(int feed_index) {
    if (feed_index < 0 || feed_index >= subscription_count) {
        return false;
    }

    return subscriptions[feed_index].episode_count > 0;
}
