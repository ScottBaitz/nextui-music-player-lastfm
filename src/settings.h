#ifndef __SETTINGS_H__
#define __SETTINGS_H__

// Music Player app-specific settings
// These are separate from the global NextUI settings (CFG_*)

// Initialize settings (loads from file if exists)
void Settings_init(void);

// Cleanup settings (saves and frees resources)
void Settings_quit(void);

// Screen off timeout setting (in seconds)
// Values: 60, 90, 120, 0 (off)
int Settings_getScreenOffTimeout(void);
void Settings_setScreenOffTimeout(int seconds);

// Cycle through screen off timeout values
void Settings_cycleScreenOffNext(void);  // 60 -> 90 -> 120 -> Off -> 60
void Settings_cycleScreenOffPrev(void);  // 60 -> Off -> 120 -> 90 -> 60

// Get display string for current screen off timeout
// Returns: "60s", "90s", "120s", or "Off"
const char* Settings_getScreenOffDisplayStr(void);

// Save settings to file (auto-called on change)
void Settings_save(void);

#endif
