#ifndef __PODCAST_H__
#define __PODCAST_H__

#include <stdint.h>
#include <stdbool.h>

// Limits
#define PODCAST_MAX_SUBSCRIPTIONS 50
#define PODCAST_MAX_SEARCH_RESULTS 30
#define PODCAST_MAX_CHART_ITEMS 25
#define PODCAST_MAX_DOWNLOAD_QUEUE 50
#define PODCAST_MAX_URL 512
#define PODCAST_MAX_TITLE 256
#define PODCAST_MAX_AUTHOR 128
#define PODCAST_MAX_DESCRIPTION 1024
#define PODCAST_MAX_GUID 128
#define PODCAST_MAX_GENRE 64

// Episode pagination - only load this many into memory at a time
#define PODCAST_EPISODE_PAGE_SIZE 50

// Data paths (relative to SDCARD_PATH/.userdata/tg5040/)
#define PODCAST_DATA_DIR "music-player/podcast"
#define PODCAST_SUBSCRIPTIONS_FILE "subscriptions.json"

// Podcast episode
typedef struct {
    char guid[PODCAST_MAX_GUID];
    char title[PODCAST_MAX_TITLE];
    char url[PODCAST_MAX_URL];           // Audio file URL
    char description[PODCAST_MAX_DESCRIPTION];
    int duration_sec;
    uint32_t pub_date;                   // Unix timestamp
    int progress_sec;                    // Resume position
    bool downloaded;
    char local_path[PODCAST_MAX_URL];
} PodcastEpisode;

// Podcast feed (subscription) - episodes stored separately on disk
typedef struct {
    char feed_url[PODCAST_MAX_URL];
    char feed_id[17];                    // 16-char hex hash of feed_url + null
    char itunes_id[32];                  // iTunes ID for subscription tracking
    char title[PODCAST_MAX_TITLE];
    char author[PODCAST_MAX_AUTHOR];
    char description[PODCAST_MAX_DESCRIPTION];
    char artwork_url[PODCAST_MAX_URL];
    int episode_count;                   // Total episodes (stored on disk)
    uint32_t last_updated;               // Unix timestamp
} PodcastFeed;

// iTunes search result
typedef struct {
    char itunes_id[32];
    char title[PODCAST_MAX_TITLE];
    char author[PODCAST_MAX_AUTHOR];
    char artwork_url[PODCAST_MAX_URL];
    char feed_url[PODCAST_MAX_URL];
    char genre[PODCAST_MAX_GENRE];
} PodcastSearchResult;

// Chart item (for Top Shows)
typedef struct {
    char itunes_id[32];                  // Apple ID for lookup
    char title[PODCAST_MAX_TITLE];
    char author[PODCAST_MAX_AUTHOR];
    char artwork_url[PODCAST_MAX_URL];
    char genre[PODCAST_MAX_GENRE];
    char feed_url[PODCAST_MAX_URL];      // Populated via lookup API
} PodcastChartItem;

// Download queue item
typedef enum {
    PODCAST_DOWNLOAD_PENDING = 0,
    PODCAST_DOWNLOAD_DOWNLOADING,
    PODCAST_DOWNLOAD_COMPLETE,
    PODCAST_DOWNLOAD_FAILED
} PodcastDownloadStatus;

typedef struct {
    char feed_title[PODCAST_MAX_TITLE];
    char feed_url[PODCAST_MAX_URL];        // For updating episode status
    char episode_title[PODCAST_MAX_TITLE];
    char episode_guid[PODCAST_MAX_GUID];   // For updating episode status
    char url[PODCAST_MAX_URL];
    char local_path[PODCAST_MAX_URL];
    PodcastDownloadStatus status;
    int progress_percent;
} PodcastDownloadItem;

// Podcast module states
typedef enum {
    PODCAST_STATE_IDLE = 0,
    PODCAST_STATE_LOADING,
    PODCAST_STATE_SEARCHING,
    PODCAST_STATE_LOADING_CHARTS,
    PODCAST_STATE_BUFFERING,      // Streaming is buffering
    PODCAST_STATE_STREAMING,
    PODCAST_STATE_DOWNLOADING,
    PODCAST_STATE_ERROR
} PodcastState;

// Search status
typedef struct {
    bool searching;
    bool completed;
    int result_count;
    char error_message[256];
} PodcastSearchStatus;

// Charts status
typedef struct {
    bool loading;
    bool completed;
    int top_shows_count;
    char error_message[256];
} PodcastChartsStatus;

// Streaming status
typedef struct {
    bool streaming;
    bool buffering;
    int buffer_percent;
    int current_position_sec;
    int duration_sec;
    char error_message[256];
} PodcastStreamingStatus;

// Download progress
typedef struct {
    PodcastState state;
    int current_index;
    int total_items;
    int completed_count;
    int failed_count;
    char current_title[PODCAST_MAX_TITLE];
    char error_message[256];
} PodcastDownloadProgress;

// ============================================================================
// Core API
// ============================================================================

// Initialize podcast module
int Podcast_init(void);

// Cleanup resources
void Podcast_cleanup(void);

// Get current state
PodcastState Podcast_getState(void);

// Get last error message
const char* Podcast_getError(void);

// Update function (call in main loop)
void Podcast_update(void);

// ============================================================================
// Subscription Management
// ============================================================================

// Get list of subscribed feeds
int Podcast_getSubscriptionCount(void);
PodcastFeed* Podcast_getSubscriptions(int* count);
PodcastFeed* Podcast_getSubscription(int index);

// Add subscription by RSS URL
int Podcast_subscribe(const char* feed_url);

// Subscribe from search/chart result (uses iTunes ID to lookup RSS)
int Podcast_subscribeFromItunes(const char* itunes_id);

// Unsubscribe by index
int Podcast_unsubscribe(int index);

// Check if already subscribed (by feed URL)
bool Podcast_isSubscribed(const char* feed_url);

// Check if already subscribed (by iTunes ID - for chart items)
bool Podcast_isSubscribedByItunesId(const char* itunes_id);

// Refresh a feed (fetch latest episodes)
int Podcast_refreshFeed(int index);

// Refresh all feeds
int Podcast_refreshAllFeeds(void);

// Save/load subscriptions
void Podcast_saveSubscriptions(void);
void Podcast_loadSubscriptions(void);

// ============================================================================
// Search API (iTunes)
// ============================================================================

// Start async search
int Podcast_startSearch(const char* query);

// Get search status
const PodcastSearchStatus* Podcast_getSearchStatus(void);

// Get search results (valid after search completes)
PodcastSearchResult* Podcast_getSearchResults(int* count);

// Cancel ongoing search
void Podcast_cancelSearch(void);

// ============================================================================
// Charts API (Apple Podcast Charts)
// ============================================================================

// Start loading charts (Top Shows)
int Podcast_loadCharts(const char* country_code);

// Get charts status
const PodcastChartsStatus* Podcast_getChartsStatus(void);

// Get Top Shows
PodcastChartItem* Podcast_getTopShows(int* count);

// Get country code (uses device locale or default)
const char* Podcast_getCountryCode(void);

// ============================================================================
// Playback (Streaming)
// ============================================================================

// Start streaming an episode
int Podcast_play(PodcastFeed* feed, int episode_index);

// Stop playback
void Podcast_stop(void);

// Pause/resume
void Podcast_pause(void);
void Podcast_resume(void);
bool Podcast_isPaused(void);

// Seek to position (in milliseconds)
void Podcast_seek(int position_ms);

// Get streaming status
const PodcastStreamingStatus* Podcast_getStreamingStatus(void);

// Get current position for progress bar
int Podcast_getPosition(void);
int Podcast_getDuration(void);

// Get buffer level (0.0 to 1.0)
float Podcast_getBufferLevel(void);

// Get audio samples for playback (called by audio callback)
int Podcast_getAudioSamples(int16_t* buffer, int max_samples);

// Check if podcast audio is active
bool Podcast_isActive(void);

// Check if currently streaming (vs playing local file)
bool Podcast_isStreaming(void);

// Check if buffering (connecting/buffering stream)
bool Podcast_isBuffering(void);

// ============================================================================
// Progress Tracking
// ============================================================================

// Save episode progress
void Podcast_saveProgress(const char* feed_url, const char* episode_guid, int position_sec);

// Get episode progress
int Podcast_getProgress(const char* feed_url, const char* episode_guid);

// Mark episode as played
void Podcast_markAsPlayed(const char* feed_url, const char* episode_guid);

// ============================================================================
// Download Queue
// ============================================================================

// Add episode to download queue (auto-starts download if not running)
int Podcast_queueDownload(PodcastFeed* feed, int episode_index);

// Download episode immediately (convenience wrapper - queues and starts)
int Podcast_downloadEpisode(PodcastFeed* feed, int episode_index);

// Remove from queue
int Podcast_removeDownload(int index);

// Cancel a specific episode download (by feed URL and episode GUID)
int Podcast_cancelEpisodeDownload(const char* feed_url, const char* episode_guid);

// Get download status for a specific episode
// Returns: -1 if not in queue, otherwise PodcastDownloadStatus enum
// progress_out: set to 0-100 if downloading, 0 otherwise
int Podcast_getEpisodeDownloadStatus(const char* feed_url, const char* episode_guid, int* progress_out);

// Generate local file path for an episode (does not check if file exists)
void Podcast_getEpisodeLocalPath(PodcastFeed* feed, int episode_index, char* buf, int buf_size);

// Check if episode file exists locally
bool Podcast_episodeFileExists(PodcastFeed* feed, int episode_index);

// Clear queue
void Podcast_clearDownloadQueue(void);

// Get queue items
PodcastDownloadItem* Podcast_getDownloadQueue(int* count);

// Start downloading
int Podcast_startDownloads(void);

// Stop/cancel downloads
void Podcast_stopDownloads(void);

// Get download progress
const PodcastDownloadProgress* Podcast_getDownloadProgress(void);

// Check if episode is downloaded
bool Podcast_isDownloaded(const char* feed_url, const char* episode_guid);

// Get downloaded episode path
const char* Podcast_getDownloadedPath(const char* feed_url, const char* episode_guid);

// Save/load download queue
void Podcast_saveDownloadQueue(void);
void Podcast_loadDownloadQueue(void);

// Batch download latest N episodes (skips already downloaded)
// count: 5, 10, 20, or 50
int Podcast_downloadLatest(int feed_index, int count);

// Auto-download new episodes after feed refresh
// Returns number of new episodes queued
int Podcast_autoDownloadNew(int feed_index);

// Check if episode is already downloaded (for skip logic)
bool Podcast_isEpisodeDownloaded(PodcastFeed* feed, int episode_index);

// ============================================================================
// Episode Management (on-disk storage)
// ============================================================================

// Get episode by index (loads from disk cache if needed)
// Returns pointer to episode in internal cache, or NULL if invalid
PodcastEpisode* Podcast_getEpisode(int feed_index, int episode_index);

// Load a page of episodes into cache
// Returns number of episodes loaded
int Podcast_loadEpisodePage(int feed_index, int offset);

// Get current episode cache info
int Podcast_getEpisodeCacheOffset(void);
int Podcast_getEpisodeCacheCount(void);

// Invalidate episode cache (call when switching feeds)
void Podcast_invalidateEpisodeCache(void);

// Save episodes to disk (called after RSS parse)
int Podcast_saveEpisodes(int feed_index, PodcastEpisode* episodes, int count);

// Get total episode count for a feed (from metadata, no disk read)
int Podcast_getEpisodeCount(int feed_index);

// Generate feed_id from feed_url (16-char hex hash)
void Podcast_generateFeedId(const char* feed_url, char* feed_id, int feed_id_size);

// Get path to feed's data directory
void Podcast_getFeedDataPath(const char* feed_id, char* path, int path_size);

// ============================================================================
// RSS Parser (podcast_rss.c)
// ============================================================================

// Parse RSS feed (simple - just feed metadata)
int podcast_rss_parse(const char* xml_data, int xml_len, PodcastFeed* feed);

// Parse RSS feed with episodes output
int podcast_rss_parse_with_episodes(const char* xml_data, int xml_len, PodcastFeed* feed,
                                     PodcastEpisode* episodes_out, int max_episodes,
                                     int* episode_count_out);

#endif // __PODCAST_H__
