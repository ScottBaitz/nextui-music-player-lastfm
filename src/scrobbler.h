#ifndef __SCROBBLER_H__
#define __SCROBBLER_H__

#include <stdbool.h>
#include "player.h"

// Rockbox-style Last.fm scrobbler log file support
// Creates a .scrobbler.log file that can be submitted to Last.fm
// using tools like QTScrobbler, Universal Scrobbler, etc.

// Initialize scrobbler (call once at startup)
void Scrobbler_init(void);

// Cleanup scrobbler (call at shutdown)
void Scrobbler_quit(void);

// Call when a track starts playing
// Records the start timestamp for later scrobble calculation
void Scrobbler_trackStarted(const TrackInfo* info, const char* filepath);

// Call when a track completes naturally (not skipped)
// Writes to log if track was played for >50% of duration
void Scrobbler_trackCompleted(void);

// Call when track is manually stopped/skipped (before completion)
// Marks current track as skipped (won't scrobble)
void Scrobbler_trackSkipped(void);

// Check if scrobbler has pending entries in the log
bool Scrobbler_hasPendingScrobbles(void);

// Get the path to the scrobbler log file
const char* Scrobbler_getLogPath(void);

#endif
