#!/bin/sh
# Warzone 2126: turn a recorded menu footage shot into a clip for the main menu.
#   encode.sh <frames dir> <output .webm>
# The frames come from a visual bench run in record mode (see README.md): f00000.png, f00001.png ...
# VP9 at constant quality, no audio, 30 fps.
set -e
[ $# -eq 2 ] || { echo "usage: $0 <frames dir> <output.webm>" >&2; exit 2; }
ffmpeg -loglevel error -y -framerate 30 -i "$1/f%05d.png" \
	-c:v libvpx-vp9 -b:v 0 -crf 33 -pix_fmt yuv420p -row-mt 1 -an "$2"
ls -l "$2"
