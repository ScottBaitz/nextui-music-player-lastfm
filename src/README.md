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
├── musicplayer.c    # Main entry point, UI rendering
├── player.c         # Audio playback (MP3, WAV, FLAC, OGG)
├── player.h
├── radio.c          # Internet radio streaming (MP3, AAC/HLS)
├── radio.h
├── youtube.c        # YouTube Music downloading
├── youtube.h
├── audio/           # Audio decoder libraries (dr_mp3, dr_wav, dr_flac, stb_vorbis)
├── include/
│   ├── helix-aac/   # AAC decoder for HLS streams
│   ├── mbedtls_lib/ # TLS support for HTTPS
│   └── parson/      # JSON parser
└── Makefile
```

## Audio Processing

### Local File Playback (player.c)
- Decodes entire file to PCM in memory
- Resamples to 48kHz using libsamplerate (SRC_SINC_MEDIUM_QUALITY)
- Supports: MP3, WAV, FLAC, OGG

### Radio Streaming (radio.c)
- Real-time streaming with ring buffer
- Supports: MP3 streams, AAC/HLS streams
- Reconfigures audio device to match stream sample rate (no resampling)

### YouTube Downloads (youtube.c)
- Uses yt-dlp + ffmpeg to download and convert to MP3
- Validates MP3 integrity before saving
- Downloads to `.downloading_*.mp3` temp file, then renames on success

## Debugging

Logs are written to `log.txt` in the project root when launched via `launch.sh`.

To enable more verbose logging, check `LOG_error()` calls throughout the code.
