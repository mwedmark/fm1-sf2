#!/bin/sh
# Makes the FM1-SF2 walk-through video from scratch (README.md in this folder).
#   video/make_video.sh [work_dir] [out.mp4]
# Needs gcc, Node.js with Playwright, Python 3 with Pillow and numpy, and ffmpeg with libx264.
set -e
cd "$(dirname "$0")/.."
WORK=${1:-video/work}
OUT=${2:-$WORK/fm1-sf2-walkthrough.mp4}
mkdir -p "$WORK"
video/build.sh
node make_bank.js bfp8 32000 soundfonts/1mgm.sf2 "$WORK/gm.fm1b"
node fm1_song.js MIDIs/Eine-Kleine-Nachtmusik-2.mid "$WORK/song.fm1s"
python3 -m http.server 8765 --bind 127.0.0.1 >"$WORK/http.log" 2>&1 &
SERVER=$!
trap 'kill $SERVER' EXIT
sleep 1
node video/screenshots.js "$WORK"
python3 video/make_video.py "$WORK" "$OUT"
