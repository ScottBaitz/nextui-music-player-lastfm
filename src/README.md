# Music Player

A comprehensive music playback application for embedded systems, featuring local file playback, internet radio streaming, and YouTube audio downloading.

## Overview

Music Player is a C-based audio application designed for the TG5040 handheld device and other embedded Linux platforms. It provides a complete music experience with support for multiple audio formats, internet radio stations, and YouTube integration via yt-dlp.

## Features

### Local Music Playback
- Supports WAV, MP3, OGG, and FLAC formats
- File browser for navigating music libraries
- Shuffle and repeat modes
- Volume control
- Waveform overview for track progress

### Internet Radio
- Preset station management (add, remove, save)
- Curated station browser organized by country
- Support for MP3 and AAC streams
- Direct streaming (Shoutcast/Icecast) and HLS (m3u8) support
- HTTPS support via mbedTLS
- ICY metadata display (song title, artist, station info)

### YouTube Audio
- Search YouTube for music
- Download queue management
- Batch downloading with progress tracking
- yt-dlp version management and updates
- Downloaded files integrate with local music library

### Audio Visualization
- Real-time spectrum analyzer (32 frequency bars)
- Waveform display mode
- FFT-based frequency analysis
- Smooth animations with peak detection

## Dependencies

### System Libraries
- SDL2, SDL2_image, SDL2_ttf
- OpenGL ES 2.0, EGL
- libsamplerate
- pthreads

### Included Libraries
- mbedTLS (HTTPS/SSL support)
- Helix AAC Decoder
- dr_libs (dr_mp3, dr_flac, dr_wav)
- stb_vorbis
- Parson (JSON parsing)

### External Tools
- yt-dlp binary (for YouTube features)

## Building

### Prerequisites
Set up your cross-compilation toolchain and ensure the required libraries are available.

### Compilation
```bash
# Set cross-compiler (optional, for cross-compilation)
export CROSS_COMPILE=/path/to/toolchain/bin/arm-linux-

# Build for specific platform
make PLATFORM=tg5040

# Or for trimui
make PLATFORM=trimui

# Clean build
make clean
```

The compiled binary will be output to `build/$(PLATFORM)/musicplayer.elf`.

## Usage

### Navigation Controls
- **D-Pad**: Navigate menus and file browser
- **A Button**: Select/Confirm
- **B Button**: Back/Cancel
- **Start**: Access options menu
- **Select**: Toggle visualization mode
- **L/R Shoulders**: Adjust volume

### Main Menu Options
1. **Music** - Browse and play local music files
2. **Radio** - Stream internet radio stations
3. **YouTube** - Search and download audio from YouTube

### Playing Music
- Navigate to your music folder using the file browser
- Select a file to start playback
- Use L/R to skip tracks in shuffle mode

### Internet Radio
- Select from preset stations or browse curated lists
- Add custom stations by URL
- Metadata displays automatically when available

### YouTube Downloads
- Enter search query using on-screen keyboard
- Select tracks to add to download queue
- Monitor download progress in the queue view

## Project Structure

```
musicplayer/
├── Makefile           # Build configuration
├── musicplayer.c      # Main application & UI
├── player.c/h         # Audio playback engine
├── radio.c/h          # Internet radio streaming
├── visualizer.c/h     # Audio visualization
├── youtube.c/h        # YouTube/yt-dlp integration
├── audio/             # Audio decoder headers
│   ├── dr_mp3.h
│   ├── dr_flac.h
│   ├── dr_wav.h
│   └── stb_vorbis.h
├── include/           # Third-party libraries
│   ├── mbedtls/
│   ├── helix-aac/
│   └── parson/
└── Music Player.pak/  # Resources (fonts, binaries)
```

## License

MIT License

Copyright (c) 2024

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
