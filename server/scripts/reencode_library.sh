#!/usr/bin/env bash
#
# Re-encode every MP3 in a directory through LAME so the ESP32's libhelix
# decoder gets a clean, sync-stable bitstream.
#
# Background: some files in the library are perfectly valid MP3 (ffmpeg decodes
# them with zero errors) yet make libhelix on the device lose frame sync mid-
# track — it transiently mis-reads the sample rate and emits garbage samples,
# heard as a "cofnięta płyta" stutter. This was confirmed on
# b8bb84fa_W_ukladzie_slonecznym.mp3: deeper buffering (PCM ring) did NOT help
# (ring stayed full, 0 underruns), but re-encoding the file made it play clean.
#
# The re-encode keeps the same filename, so NFC mappings stay valid. It is
# atomic per file (temp file + mv), and the original is left untouched if the
# encode fails.
#
# Usage:
#   server/scripts/reencode_library.sh [DIR]      # default DIR = ./music
#   DRY_RUN=1 server/scripts/reencode_library.sh  # list what would change
#
# Run it on the host that owns the library (the RPi), then re-sync to the
# device. Lossy->lossy at 64k adds negligible quality loss; the stored files
# are already 64k.

set -euo pipefail

DIR="${1:-music}"
BITRATE="${BITRATE:-64k}"
DRY_RUN="${DRY_RUN:-0}"

if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "ffmpeg not found in PATH" >&2
    exit 1
fi
if [ ! -d "$DIR" ]; then
    echo "directory not found: $DIR" >&2
    exit 1
fi

shopt -s nullglob
files=("$DIR"/*.mp3 "$DIR"/*.MP3)
if [ ${#files[@]} -eq 0 ]; then
    echo "no .mp3 files in $DIR"
    exit 0
fi

echo "Re-encoding ${#files[@]} file(s) in '$DIR' at $BITRATE (libmp3lame, 44100/2)..."
ok=0; fail=0
for f in "${files[@]}"; do
    if [ "$DRY_RUN" = "1" ]; then
        echo "  would re-encode: $f"
        continue
    fi
    tmp="$f.reenc.tmp.mp3"
    if ffmpeg -v error -y -i "$f" -c:a libmp3lame -b:a "$BITRATE" -ar 44100 -ac 2 "$tmp" \
       && [ -s "$tmp" ]; then
        mv -f "$tmp" "$f"
        echo "  ok: $(basename "$f")"
        ok=$((ok+1))
    else
        rm -f "$tmp"
        echo "  FAILED (left original untouched): $(basename "$f")" >&2
        fail=$((fail+1))
    fi
done

echo "Done. re-encoded=$ok failed=$fail"
[ "$fail" -eq 0 ]
