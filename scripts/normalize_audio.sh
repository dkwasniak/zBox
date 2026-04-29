#!/usr/bin/env bash
# normalize_audio.sh — konwertuje istniejące MP3 do 44100 Hz stereo
#
# Użycie (na RPi):
#   ./scripts/normalize_audio.sh                     # domyślnie ~/musicbox/music
#   ./scripts/normalize_audio.sh /inna/sciezka/music
#   ./scripts/normalize_audio.sh --no-backup         # bez backupu (szybciej)
#
# Backupy oryginałów trafiają do <music_dir>/../music_backup_orig/
# Pliki już w 44100 Hz są pomijane bez konwersji.

set -euo pipefail

# --- Argumenty ---
MUSIC_DIR=""
NO_BACKUP=false

for arg in "$@"; do
    case "$arg" in
        --no-backup) NO_BACKUP=true ;;
        *) MUSIC_DIR="$arg" ;;
    esac
done

MUSIC_DIR="${MUSIC_DIR:-$HOME/musicbox/music}"
MUSIC_DIR="$(realpath "$MUSIC_DIR")"
BACKUP_DIR="$(dirname "$MUSIC_DIR")/music_backup_orig"

# --- Sprawdź narzędzia ---
if ! command -v ffmpeg &>/dev/null; then
    echo "ERROR: ffmpeg nie znaleziony. Zainstaluj: sudo apt-get install ffmpeg"
    exit 1
fi
if ! command -v ffprobe &>/dev/null; then
    echo "ERROR: ffprobe nie znaleziony (powinien być razem z ffmpeg)"
    exit 1
fi

echo "=== normalize_audio.sh ==="
echo "Katalog: $MUSIC_DIR"
echo "Backup:  $($NO_BACKUP && echo 'WYŁĄCZONY' || echo "$BACKUP_DIR")"
echo ""

if [ ! -d "$MUSIC_DIR" ]; then
    echo "ERROR: Katalog nie istnieje: $MUSIC_DIR"
    exit 1
fi

# --- Zbierz wszystkie MP3 (rekurencyjnie, w tym music/system/) ---
mapfile -t FILES < <(find "$MUSIC_DIR" -name "*.mp3" -type f | sort)

TOTAL=${#FILES[@]}
if [ "$TOTAL" -eq 0 ]; then
    echo "Brak plików MP3 w $MUSIC_DIR"
    exit 0
fi

echo "Znaleziono $TOTAL plików MP3"
echo ""

# --- Statystyki ---
COUNT_SKIP=0
COUNT_CONVERT=0
COUNT_ERROR=0

for FILE in "${FILES[@]}"; do
    REL="$(realpath --relative-to="$MUSIC_DIR" "$FILE")"

    # Sprawdź aktualny sample rate
    SR=$(ffprobe -v error \
        -select_streams a:0 \
        -show_entries stream=sample_rate \
        -of csv=p=0 \
        "$FILE" 2>/dev/null || echo "0")

    SR="${SR// /}"  # usuń ewentualne spacje

    if [ "$SR" = "44100" ]; then
        echo "[SKIP]    $REL (już 44100 Hz)"
        COUNT_SKIP=$((COUNT_SKIP + 1))
        continue
    fi

    echo "[CONVERT] $REL ($SR Hz → 44100 Hz)"

    # Backup oryginału
    if [ "$NO_BACKUP" = false ]; then
        BACKUP_FILE="$BACKUP_DIR/$REL"
        mkdir -p "$(dirname "$BACKUP_FILE")"
        cp "$FILE" "$BACKUP_FILE"
    fi

    # Konwersja do pliku tymczasowego (in-place przez tmp)
    TMP_FILE="${FILE}.tmp_normalize.mp3"
    if ffmpeg -i "$FILE" \
        -ar 44100 \
        -ac 2 \
        -q:a 2 \
        -y \
        "$TMP_FILE" \
        -loglevel error 2>/dev/null; then
        mv "$TMP_FILE" "$FILE"
        COUNT_CONVERT=$((COUNT_CONVERT + 1))
    else
        echo "  ERROR: konwersja nieudana, plik niezmieniony"
        rm -f "$TMP_FILE"
        COUNT_ERROR=$((COUNT_ERROR + 1))
    fi
done

echo ""
echo "=== Wyniki ==="
echo "Pominięte (już 44100 Hz): $COUNT_SKIP"
echo "Skonwertowane:            $COUNT_CONVERT"
echo "Błędy:                    $COUNT_ERROR"
if [ "$NO_BACKUP" = false ] && [ "$COUNT_CONVERT" -gt 0 ]; then
    echo "Backupy oryginałów:       $BACKUP_DIR"
fi
echo ""
if [ "$COUNT_ERROR" -gt 0 ]; then
    echo "UWAGA: $COUNT_ERROR plików nie udało się skonwertować."
    exit 1
fi
echo "Gotowe. Uruchom sync na ESP32 (BTN_A + BTN_B przez 2s) żeby pobrać zaktualizowane pliki."
