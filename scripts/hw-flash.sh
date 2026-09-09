#!/usr/bin/env bash
#
# Flasht die Firmware auf echte Hardware und liest den Boot-Log mit Timeout.
# Seit <policy update> auch fuer den Agenten erlaubt (siehe .claude/settings.json
# und CLAUDE.md) — deshalb bewusst ohne "idf.py monitor": das ist ein
# interaktives Curses-Tool und bricht ohne echtes TTY mit "Monitor requires
# standard input to be attached to TTY" ab. Stattdessen wird der Port roh
# mit `stty`/`cat` unter `timeout` gelesen — das terminiert garantiert von
# selbst und braucht kein TTY.
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
BAUD="${MONITOR_BAUD:-115200}"
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

if [ ! -e "$PORT" ]; then
    echo "FEHLER: $PORT nicht gefunden. Board angeschlossen?" >&2
    exit 2
fi

echo "== Flash auf $PORT =="
idf.py -B "$BUILD_DIR" -p "$PORT" flash

# idf.py flash setzt das Board bereits per Reset neu — direkt danach lesen,
# damit der Boot-Log nicht verpasst wird.
echo "== Boot-Log, ${MONITOR_SECONDS}s (roh, ohne idf_monitor-Dekodierung) =="
stty -F "$PORT" "$BAUD" raw -echo -echoe -echok
set +e
timeout "$MONITOR_SECONDS" cat "$PORT" | tee "$LOG"
set -e

echo "Log: $LOG"
