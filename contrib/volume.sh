#!/bin/sh
# Control the PulseAudio/PipeWire volume with pactl and refresh i3status.
#
# Usage: volume.sh up|down|mute|get [step]
#   up|down   change the default sink volume by `step` percent (default 5),
#             never going above MAX_VOLUME percent
#   mute      toggle mute on the default sink
#   get       print the current volume in percent
#
# Example i3 config:
#   bindsym XF86AudioRaiseVolume exec --no-startup-id /path/to/volume.sh up
#   bindsym XF86AudioLowerVolume exec --no-startup-id /path/to/volume.sh down
#   bindsym XF86AudioMute        exec --no-startup-id /path/to/volume.sh mute

set -eu

SINK="@DEFAULT_SINK@"
MAX_VOLUME="${MAX_VOLUME:-100}"
STEP="${2:-5}"

case "$STEP" in
    ''|*[!0-9]*) echo "volume.sh: step must be a positive integer" >&2; exit 2 ;;
esac

get_volume() {
    pactl get-sink-volume "$SINK" | sed -n 's/.* \([0-9][0-9]*\)%.*/\1/p' | head -n 1
}

refresh_i3status() {
    # i3status refreshes immediately on SIGUSR1.
    pkill -USR1 -x i3status 2>/dev/null || true
}

command -v pactl >/dev/null 2>&1 || { echo "volume.sh: pactl not found" >&2; exit 1; }

case "${1:-}" in
    up)
        current=$(get_volume)
        target=$((current + STEP))
        [ "$target" -gt "$MAX_VOLUME" ] && target=$MAX_VOLUME
        pactl set-sink-mute "$SINK" 0
        pactl set-sink-volume "$SINK" "${target}%"
        refresh_i3status
        ;;
    down)
        current=$(get_volume)
        target=$((current - STEP))
        [ "$target" -lt 0 ] && target=0
        pactl set-sink-volume "$SINK" "${target}%"
        refresh_i3status
        ;;
    mute)
        pactl set-sink-mute "$SINK" toggle
        refresh_i3status
        ;;
    get)
        get_volume
        ;;
    *)
        echo "Usage: $0 up|down|mute|get [step]" >&2
        exit 2
        ;;
esac
