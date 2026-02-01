#ifndef __YOUTUBE_H__
#define __YOUTUBE_H__

#include <stdint.h>
#include <stdbool.h>

#define YOUTUBE_MAX_RESULTS 30
#define YOUTUBE_MAX_QUEUE 100
#define YOUTUBE_MAX_TITLE 256
#define YOUTUBE_MAX_ARTIST 128
#define YOUTUBE_VIDEO_ID_LEN 16

// YouTube search result
typedef struct {
    char video_id[YOUTUBE_VIDEO_ID_LEN];
    char title[YOUTUBE_MAX_TITLE];
    char artist[YOUTUBE_MAX_ARTIST];
    int duration_sec;
} YouTubeResult;

// Queue item status
typedef enum {
    YOUTUBE_STATUS_PENDING = 0,
    YOUTUBE_STATUS_DOWNLOADING,
    YOUTUBE_STATUS_COMPLETE,
    YOUTUBE_STATUS_FAILED
} YouTubeItemStatus;

// Download queue item
typedef struct {
    char video_id[YOUTUBE_VIDEO_ID_LEN];
    char title[YOUTUBE_MAX_TITLE];
    YouTubeItemStatus status;
    int progress_percent;  // 0-100 during download
} YouTubeQueueItem;

// Module states
typedef enum {
    YOUTUBE_STATE_IDLE = 0,
    YOUTUBE_STATE_SEARCHING,
    YOUTUBE_STATE_DOWNLOADING,
    YOUTUBE_STATE_UPDATING,
    YOUTUBE_STATE_ERROR
} YouTubeState;

// Download status info
typedef struct {
    YouTubeState state;
    int current_index;           // Currently downloading item index
    int total_items;             // Total items in queue
    int completed_count;         // Number completed
    int failed_count;            // Number failed
    char current_title[YOUTUBE_MAX_TITLE];
    char error_message[256];
} YouTubeDownloadStatus;

// Update status info
typedef struct {
    bool update_available;
    char current_version[32];
    char latest_version[32];
    bool updating;
    int progress_percent;
    long download_bytes;        // Bytes downloaded so far
    long download_total;        // Total bytes to download (0 if unknown)
    char status_detail[64];     // Detailed status (e.g., "2.5 MB / 5.0 MB")
    char error_message[256];
} YouTubeUpdateStatus;

// Search status info
typedef struct {
    bool searching;             // True while search is in progress
    bool completed;             // True when search finished (success or error)
    int result_count;           // Number of results found (-1 on error)
    char error_message[256];    // Error message if failed
} YouTubeSearchStatus;

// Initialize YouTube module
// Returns 0 on success, -1 if yt-dlp not found
int YouTube_init(void);

// Cleanup resources
void YouTube_cleanup(void);

// Check if yt-dlp binary exists
bool YouTube_isAvailable(void);

// Check network connectivity (quick ping test)
// Returns true if network is available, false otherwise
bool YouTube_checkNetwork(void);

// Get yt-dlp version
const char* YouTube_getVersion(void);

// Search YouTube for music (DEPRECATED - use async version)
// query: search string
// results: array to fill with results
// max_results: maximum number of results (up to YOUTUBE_MAX_RESULTS)
// Returns number of results found, or -1 on error
int YouTube_search(const char* query, YouTubeResult* results, int max_results);

// Async search functions (preferred - won't block UI)
// Start a background search
int YouTube_startSearch(const char* query);

// Get search status (call in main loop to check progress)
const YouTubeSearchStatus* YouTube_getSearchStatus(void);

// Get search results after search completes
// Returns pointer to internal results array, count is set via status->result_count
YouTubeResult* YouTube_getSearchResults(void);

// Cancel ongoing search
void YouTube_cancelSearch(void);

// Queue management
int YouTube_queueAdd(const char* video_id, const char* title);
int YouTube_queueRemove(int index);
int YouTube_queueRemoveById(const char* video_id);
int YouTube_queueClear(void);
int YouTube_queueCount(void);
YouTubeQueueItem* YouTube_queueGet(int* count);

// Check if video is already in queue or downloaded
bool YouTube_isInQueue(const char* video_id);
bool YouTube_isDownloaded(const char* video_id);

// Start downloading queue items (runs in background)
int YouTube_downloadStart(void);

// Stop/cancel current download
void YouTube_downloadStop(void);

// Get download status
const YouTubeDownloadStatus* YouTube_getDownloadStatus(void);

// yt-dlp update functions
int YouTube_checkForUpdate(void);  // Check if new version available
int YouTube_startUpdate(void);     // Start updating yt-dlp
void YouTube_cancelUpdate(void);   // Cancel update
const YouTubeUpdateStatus* YouTube_getUpdateStatus(void);

// Get current state
YouTubeState YouTube_getState(void);

// Get last error message
const char* YouTube_getError(void);

// Update function (call in main loop)
void YouTube_update(void);

// Save/load queue (persistence)
void YouTube_saveQueue(void);
void YouTube_loadQueue(void);

// Get download directory path
const char* YouTube_getDownloadPath(void);

// Open keyboard for search input
// Returns allocated string that caller must free, or NULL if cancelled
char* YouTube_openKeyboard(const char* prompt);

#endif
