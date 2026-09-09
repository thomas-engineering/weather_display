#!/usr/bin/env bash
#
# Host-Unit-Tests auf dem ESP-IDF Linux-Target.
# Baut host_test/ nativ und fuehrt die Unity-Suite aus.
#
# Exit 0  = alle Tests gruen
# Exit !=0 = Testfehler, Buildfehler oder fehlende Umgebung (127)
#
# Braucht keine Hardware und keinen Emulator.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf}"

PROJECT_DIR="$REPO_ROOT/host_test"
BUILD_DIR="$PROJECT_DIR/build-linux"
LOG_DIR="$REPO_ROOT/.logs"
LOG="$LOG_DIR/host-test.log"
TEST_TIMEOUT="${TEST_TIMEOUT:-120}"

mkdir -p "$LOG_DIR"

# --- ESP-IDF-Umgebung -------------------------------------------------------
# Der Agent startet pro Kommando eine frische Shell, deshalb sourcen wir hier
# selbst statt uns auf eine vorbereitete Shell zu verlassen.
if ! command -v idf.py >/dev/null 2>&1; then
    if [ ! -f "$IDF_PATH/export.sh" ]; then
        echo "FEHLER: ESP-IDF nicht gefunden. IDF_PATH=$IDF_PATH" >&2
        echo "Setze IDF_PATH oder installiere ESP-IDF." >&2
        exit 127
    fi
    # shellcheck disable=SC1091
    source "$IDF_PATH/export.sh" >/dev/null
fi

cd "$PROJECT_DIR"

# --- Build ------------------------------------------------------------------
# set-target loescht das Build-Verzeichnis, also nur beim allerersten Lauf.
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "== Erstkonfiguration: Linux-Target =="
    idf.py -B "$BUILD_DIR" --preview set-target linux
fi

echo "== Build =="
idf.py -B "$BUILD_DIR" build

BIN="$(find "$BUILD_DIR" -maxdepth 1 -name '*.elf' -print -quit)"
if [ -z "$BIN" ]; then
    echo "FEHLER: kein Test-Binary in $BUILD_DIR" >&2
    exit 1
fi

# --- Ausfuehren -------------------------------------------------------------
echo "== Tests: $(basename "$BIN") =="
set +e
timeout "$TEST_TIMEOUT" "$BIN" 2>&1 | tee "$LOG"
status=${PIPESTATUS[0]}
set -e

if [ "$status" -eq 124 ]; then
    echo "HOST_TESTS_FAILED: Timeout nach ${TEST_TIMEOUT}s (Deadlock?). Log: $LOG" >&2
    exit 124
fi

if [ "$status" -ne 0 ]; then
    echo "HOST_TESTS_FAILED (exit=$status). Volles Log: $LOG" >&2
    exit "$status"
fi

echo "HOST_TESTS_PASSED"
