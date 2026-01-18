# NextUI Music Player
A comprehensive music playback application for NextUI featuring local file playback, internet radio streaming, and music downloading. This pak is designed and tested only for the NextUI and TrimUI Brick @ Trimui Brick Hammer:

⭐ If you enjoy using this project, consider supporting me on [Ko-fi](https://ko-fi.com/mohammadsyuhada) ☕

![music_player](https://github.com/user-attachments/assets/de4fe612-1c48-4e98-9537-79504e20f299)


## Installation

### Manual Installation
1. Mount your NextUI SD card to a computer.
2. Download the latest release file named `Music.Player.pak.zip` from Github.
3. Copy the zip file to `/Tools/tg5040/Music.Player.pak.zip`.
4. Extract the zip in place, then delete the zip file.
5. Confirm that there is a `/Tools/tg5040/Music.Player.pak` folder on your SD card.
6. You may rename the `Music.Player.pak` folder to `Music Player.pak` if you wish. 
6. Unmount your SD Card and insert it into your TrimUI device.

### Pak Store Installation

1. Open `Pak Store` application in your TrimUI device. 
2. Navigate to the `Browse` then `Media` menu. 
3. Select `Music Player` to install.

## Update

1) You can update the application directly via `About` page in the application.
2) Or, you can update via `Pak Store`.

## Features

### General
- Support Bluetooth devices for output.
- Volume control for Bluetooth/USB-C/Device Speakers via hardware button.

### Local Music Playback
- Supports `WAV`, `MP3`, `OGG`, `FLAC` and `M4A` formats
- File browser for navigating music libraries (Audio files must be placed in `./Music` folder)
- Shuffle and repeat modes
- Spectrum visualizer with 5 options of color to choose from.
- Album art display (Automatically download album art if track doesn't provide)

### Internet Radio
- Preset station management (add, remove, save)
- Curated station browser organized by country (Only Malaysia for now - others will be added later; please suggest)
- Support for `MP3` and `AAC` streams, direct streaming (Shoutcast/Icecast) and HLS (m3u8).
- HTTPS support via mbedTLS
- Metadata display (song title, artist, station info)
- Album art display (Downloaded from internet based on current song)

### Music Downloader
- Search YouTube Music for music
- Download queue management
- Batch downloading with progress tracking
- yt-dlp version management and updates
- Downloaded files integrate with local music library (Downloaded tracks will be placed in `./Music/Download`)

## Controls

### Main Menu Navigation
- **D-Pad**: Navigate menus and file browser
- **A Button**: Select/Confirm
- **B Button**: Back/Cancel/Exit
- **Start (short press)**: Show Controls Help
- **Start (long press)**: Exit Application

### Music Player
- **A Button**: Play/Pause
- **B Button**: Back/Cancel/Exit
- **X Button**: Toggle Shuffle
- **Y Button**: Toggle Repeat
- **D-Pad Up**: Next Track
- **D-Pad Down**: Prev Track
- **D-Pad Right**: Fast Foward
- **D-Pad Left**: Rewind
- **Select**: Turn Off Screen
- **Start (short press)**: Show Controls Help
- **Start (long press)**: Exit Application
- **L1/R1 Shoulders**: Prev/Next Track
- **L2/L3 Shoulders**: Toggle Visualizer
- **R2/R3 Shoulders**: Cycle Visualizer Colors

### Radio Player
- **B Button**: Back/Stop
- **D-Pad Up**: Next Station
- **D-Pad Down**: Prev Station
- **Select**: Turn Off Screen
- **Start (short press)**: Show Controls Help
- **Start (long press)**: Exit Application
- **L1/R1 Shoulders**: Prev/Next Station

## Usage

### Playing Local Music
- Navigate to your music folder using the `Local File` menu
- Select a file to start playback

### Internet Radio
- Navigate to the stations list using the `Internet Radio` menu
- Select from predefined list of station in the `Manage Stations` menu
- Or add custom stations at `.userdata/shared/radio_stations.txt`
- Metadata displays automatically when available

### MP3 Downloads
- Navigate to the music search page using the `MP3 Downloader` menu
- Enter search query using on-screen keyboard
- Select tracks to add to download queue
- Start the queue in `Download Queue` page.
- Downloaded audio will be available in `Local File` menu
