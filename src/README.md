# NextUI Music Player - Developer Guide

## Project Location

This project should be placed in the NextUI workspace structure:

```
workspace/
├── all/
│   ├── common/          # Shared utilities (utils.c, api.c, config.c, scaler.c)
│   └── minarch/         # Libretro common includes
├── tg5040/
│   ├── platform/        # Platform-specific code (platform.c)
│   ├── libmsettings/    # Settings library
│   ├── wifimanager/     # WiFi management
│   ├── btmanager/       # Bluetooth management
│   └── nextui-music-player/  # <-- This project
│       └── src/
```

If you move this project to a different location, update the paths in `Makefile`.

## Dependencies

The project depends on:
- **Shared code**: `workspace/all/common/` (utils, api, config, scaler)
- **Platform code**: `workspace/tg5040/platform/`
- **Libraries**: SDL2, SDL2_image, SDL2_ttf, GLESv2, EGL, libsamplerate, mbedTLS

### Bundled Libraries (bins/)

The `bins/` folder contains runtime dependencies:
- `libsamplerate.so.0` - High-quality audio resampling
- `ffmpeg` - Audio extraction/conversion
- `yt-dlp` - YouTube downloading
- `wget` - HTTP downloads
- `keyboard` - On-screen keyboard

## Building

### Prerequisites

- NextUI repository with Docker toolchain

### Compile

1. From the NextUI root directory, start the build shell:
   ```bash
   make shell PLATFORM=tg5040
   ```

2. Inside the Docker container, navigate to the project:
   ```bash
   cd ~/workspace/tg5040/nextui-music-player/src
   ```

3. Build:
   ```bash
   make clean && make
   ```

Default platform is `tg5040`. To build for a different platform:
```bash
make PLATFORM=tg5050
```

The output binary is created directly at the project root: `../musicplayer.elf`

## Project Structure

```
src/
├── musicplayer.c        # Main entry point, event loop, mode switching
│
├── player.c/h           # Local audio playback (MP3, WAV, FLAC, OGG)
├── browser.c/h          # File browser navigation
│
├── radio.c/h            # Internet radio streaming core
├── radio_hls.c/h        # HLS stream handling (AAC segments)
├── radio_net.c/h        # Network operations for radio
├── radio_album_art.c/h  # Album art fetching from external APIs
├── radio_curated.c/h    # Curated radio station list
│
├── youtube.c/h          # YouTube Music downloading
├── selfupdate.c/h       # Self-update functionality
│
├── ui_fonts.c/h         # Font loading and management
├── ui_utils.c/h         # Shared UI utilities (text, colors, layout)
├── ui_album_art.c/h     # Album art rendering with background blur
├── ui_music.c/h         # Music player UI (playback, progress, controls)
├── ui_radio.c/h         # Radio UI (station list, now playing)
├── ui_youtube.c/h       # YouTube UI (search, downloads)
├── ui_system.c/h        # System UI (settings, about)
│
├── audio/               # Audio decoder libraries
│   ├── dr_mp3.h         # MP3 decoder
│   ├── dr_wav.h         # WAV decoder
│   ├── dr_flac.h        # FLAC decoder
│   └── stb_vorbis.h     # OGG Vorbis decoder
│
├── include/
│   ├── helix-aac/       # AAC decoder for HLS streams
│   ├── mbedtls/         # TLS support for HTTPS
│   └── parson/          # JSON parser
│
└── Makefile
```

## Audio Processing

### Local File Playback (player.c)
- Decodes entire file to PCM in memory
- Resamples to 48kHz using libsamplerate (SRC_SINC_MEDIUM_QUALITY)
- Supports: MP3, WAV, FLAC, OGG

### Radio Streaming (radio.c, radio_*.c)
- **radio.c**: Core streaming logic, ring buffer, audio callback
- **radio_hls.c**: HLS playlist parsing, segment prefetching, AAC decoding
- **radio_net.c**: HTTP/HTTPS connections, ICY metadata parsing
- **radio_album_art.c**: Fetches album art from iTunes/Deezer APIs
- **radio_curated.c**: Built-in curated station list
- Reconfigures audio device to match stream sample rate (no resampling)

### YouTube Downloads (youtube.c)
- Uses yt-dlp + ffmpeg to download and convert to MP3
- Validates MP3 integrity before saving
- Downloads to `.downloading_*.mp3` temp file, then renames on success

## UI Architecture

The UI is split into modular components for maintainability:

- **ui_fonts.c**: Loads and manages TTF fonts at various sizes
- **ui_utils.c**: Common rendering helpers (text, rectangles, colors)
- **ui_album_art.c**: Album art display with blurred background effect
- **ui_music.c**: Music player screen (progress bar, track info, controls)
- **ui_radio.c**: Radio screen (station list, now playing, metadata)
- **ui_youtube.c**: YouTube screen (search interface, download queue)
- **ui_system.c**: System screen (settings, about, self-update)

## Debugging

Logs are written to `log.txt` in the project root when launched via `launch.sh`.

To enable more verbose logging, check `LOG_error()` calls throughout the code.
