#!/bin/sh
# Headless Hatari screenshot: hatari/shot.sh [PRG] [out.png] [seconds-before-start] [seconds-in-game]
# Needs xvfb, xdotool, hatari, imagemagick and roms/etos512se.img (EmuTOS).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PRG=${1:-$ROOT/build/ubuntu/ASTROIDS.PRG}
OUT=${2:-/tmp/shot.png}
T1=${3:-10}
T2=${4:-6}
TOS=${HATARI_TOS:-$ROOT/roms/etos512se.img}
PRG=$(realpath "$PRG")
DIR=$(dirname -- "$PRG")
SHOTS=$(mktemp -d)
cd "$DIR"
xvfb-run -a -s "-screen 0 1024x768x24" sh -c "
hatari --machine st --memsize 1 --sound off --statusbar off --borders off --zoom 2 \\
  --screenshot-dir $SHOTS --tos $TOS --harddrive $DIR --auto 'C:\\$(basename -- "$PRG")' >/tmp/hatari.log 2>&1 &
sleep $T1
xdotool key space
sleep $T2
xdotool key ISO_Level3_Shift+g
sleep 1
pkill hatari || true
" || true
f=$(ls -t "$SHOTS"/* 2>/dev/null | head -1)
[ -n "$f" ] && convert "$f" "$OUT" && echo "wrote $OUT"
