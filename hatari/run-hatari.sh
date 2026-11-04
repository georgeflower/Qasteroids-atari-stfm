#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGET=${1:-"$ROOT_DIR/build/ASTROIDS.PRG"}
TOS_IMAGE=${HATARI_TOS:-${2:-}}

if [ ! -f "$TARGET" ]; then
    echo "Missing Atari artifact: $TARGET" >&2
    echo "Run 'make atari-st' or 'make disk-image' first." >&2
    exit 1
fi

if [ -z "$TOS_IMAGE" ]; then
    echo "Set HATARI_TOS to an EmuTOS/TOS image path or pass it as the second argument." >&2
    exit 1
fi

case "$TARGET" in
    *.st|*.ST)
        exec hatari \
            --machine st \
            --memsize 512 \
            --sound off \
            --tos "$TOS_IMAGE" \
            --disk-a "$TARGET"
        ;;
    *)
        TARGET_DIR=$(CDPATH= cd -- "$(dirname -- "$TARGET")" && pwd)
        TARGET_NAME=$(basename -- "$TARGET")
        exec hatari \
            --machine st \
            --memsize 512 \
            --sound off \
            --tos "$TOS_IMAGE" \
            --harddrive "$TARGET_DIR" \
            --auto "C:\\$TARGET_NAME"
        ;;
esac
