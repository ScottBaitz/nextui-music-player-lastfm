#!/bin/sh
DIR="$(dirname "$0")"
cd "$DIR"

# Use system PLATFORM variable, fallback to tg5040 if not set
[ -z "$PLATFORM" ] && PLATFORM="tg5040"

export LD_LIBRARY_PATH="$DIR:$DIR/bin:$DIR/bin/$PLATFORM:$LD_LIBRARY_PATH:/usr/bin"

# Set HOME so ALSA can find .asoundrc for Bluetooth/USB DAC audio routing
export HOME="/mnt/SDCARD/.userdata/$PLATFORM"

# Set CPU frequency for music player (power saving: 408-1000 MHz)
echo ondemand > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
echo 408000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq
echo 1000000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq

# Run the platform-specific binary
"$DIR/bin/$PLATFORM/musicplayer.elf" &> "$LOGS_PATH/music-player.txt"

# Restore default CPU settings on exit
echo 408000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq
echo 2000000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq
