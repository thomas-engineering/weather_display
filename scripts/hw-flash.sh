#!/usr/bin/env bash
#
# Flasht die Firmware auf echte Hardware und liest den Boot-Log mit Timeout.
# Gedacht fuer den Menschen, nicht fuer den Agenten (siehe .claude/settings.json).
#
# Nutzung:  ./scripts/hw-flash.sh [/dev/ttyACM0]
#
# Baut bewusst aus build/ mit der Produktionskonfiguration, nicht aus build-emu/.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf}"
BUILD_DIR="$REPO_ROOT/build"
LOG_DIR="$REPO_ROOT/.logs"
LOG="$LOG_DIR/hw.log"
PORT="${1:-${ESPPORT:-}}"
MONITOR_SECONDS="${MONITOR_SECONDS:-20}"

mkdir -p "$LOG_DIR"

if ! command -v idf.py >/dev/null 2>&1; then
    # shellcheck disable=SC1091
    source "$IDF_PATH/export.sh" >/dev/null
fi

cd "$REPO_ROOT"

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    idf.py -B "$BUILD_DIR" set-target esp32p4
fi

idf.py -B "$BUILD_DIR" build

if [ -z "$PORT" ]; then
    echo "Kein Port angegeben. Nutzung: $0 /dev/ttyACM0" >&2
    exit 2
fi

echo "== Flash auf $PORT =="
idf.py -B "$BUILD_DIR" -p "$PORT" flash

# Monitor mit Zeitlimit, damit auch dieses Skript garantiert endet.
echo "== Boot-Log, ${MONITOR_SECONDS}s =="
set +e
timeout "$MONITOR_SECONDS" idf.py -B "$BUILD_DIR" -p "$PORT" monitor --no-reset 2>&1 | tee "$LOG"
set -e

echo "Log: $LOG"
