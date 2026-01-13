#!/bin/sh
DIR="$(dirname "$0")"
cd "$DIR"
export LD_LIBRARY_PATH="$DIR:$DIR/bins:$LD_LIBRARY_PATH:/usr/bin"
# Set HOME so ALSA can find .asoundrc for Bluetooth/USB DAC audio routing
export HOME="/mnt/SDCARD/.userdata/tg5040"

# Set CPU frequency for music player (power saving: 408-1000 MHz)
echo ondemand > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
echo 408000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq
echo 1000000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq

# Run the app
"$DIR/musicplayer.elf" &> "$DIR/log.txt"

# Restore default CPU settings on exit
echo 408000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq
echo 2000000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq
echo userspace > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
echo 2000000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed
