#!/bin/sh
# Headless Hatari screenshot: hatari/shot.sh [PRG] [out.png] [seconds-before-start] [seconds-in-game]
# Env: TITLE=1 shoots the title screen instead of starting, HATARI_MEM=512|1024|... in KB (512), PRESS="F2" keys tapped after start, HOLD="w space" keys held until the shot.
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
hatari --machine ${HATARI_MACHINE:-st} --memsize ${HATARI_MEM:-512} --sound off --statusbar off --borders off --zoom 2 \\
  --screenshot-dir $SHOTS --tos $TOS --harddrive $DIR --auto 'C:\\$(basename -- "$PRG")' >/tmp/hatari.log 2>&1 &
sleep $T1
[ \"\${TITLE:-}\" ] || xdotool key space
sleep 1
[ -n "\${PRESS:-}" ] && xdotool keydown \$PRESS; sleep 0.4; xdotool keyup \$PRESS
[ -n "\${HOLD:-}" ] && xdotool keydown \$HOLD
sleep $T2
xdotool key ISO_Level3_Shift+g
sleep 1
pkill hatari || true
" || true
f=$(ls -t "$SHOTS"/* 2>/dev/null | head -1)
[ -n "$f" ] && convert "$f" "$OUT" && echo "wrote $OUT"
