#!/usr/bin/env bash
#
# Baut die Hardware-Firmware nach build/, ohne zu flashen.
#
# Exit 0   = Build erfolgreich
# Exit !=0 = Buildfehler (Exit-Code von idf.py) oder fehlende Umgebung (127)
#
# Volles Log: .logs/fw-build.log; auf der Konsole nur die letzten Zeilen.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"
LOG_DIR="$REPO_ROOT/.logs"
LOG="$LOG_DIR/fw-build.log"
TAIL_LINES="${TAIL_LINES:-25}"

mkdir -p "$LOG_DIR"

# shellcheck source=lib/idf-env.sh
source "$REPO_ROOT/scripts/lib/idf-env.sh"

cd "$REPO_ROOT"

set +e
{
    # set-target loescht das Build-Verzeichnis, also nur beim allerersten Lauf.
    if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
        echo "== Erstkonfiguration: esp32p4 =="
        idf.py -B "$BUILD_DIR" set-target esp32p4 || exit $?
    fi
    echo "== Build =="
    idf.py -B "$BUILD_DIR" build
} >"$LOG" 2>&1
status=$?
set -e

tail -n "$TAIL_LINES" "$LOG"

if [ "$status" -ne 0 ]; then
    echo "FW_BUILD_FAILED (exit=$status). Volles Log: $LOG" >&2
    exit "$status"
fi

echo "FW_BUILD_PASSED"
