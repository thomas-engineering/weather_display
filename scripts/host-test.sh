#!/usr/bin/env bash
#
# Host-Unit-Tests auf dem ESP-IDF Linux-Target.
# Baut host_test/ nativ und fuehrt die Unity-Suite aus.
#
# Exit 0  = alle Tests gruen
# Exit !=0 = Testfehler, Buildfehler oder fehlende Umgebung (127)
#
# Braucht keine Hardware. Build und Testlauf landen in .logs/host-test.log;
# auf der Konsole nur die letzten Zeilen und das Ergebnis.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

PROJECT_DIR="$REPO_ROOT/host_test"
BUILD_DIR="$PROJECT_DIR/build-linux"
LOG_DIR="$REPO_ROOT/.logs"
LOG="$LOG_DIR/host-test.log"
TEST_TIMEOUT="${TEST_TIMEOUT:-120}"
TAIL_LINES="${TAIL_LINES:-25}"

mkdir -p "$LOG_DIR"

# shellcheck source=lib/idf-env.sh
source "$REPO_ROOT/scripts/lib/idf-env.sh"

# The Linux target is built with the host compiler. The ESP toolchain's own
# `as` shadows the system one once IDF is on PATH and rejects --64.
PATH="/usr/bin:/bin:$PATH"

cd "$PROJECT_DIR"

: >"$LOG"

# --- Build ------------------------------------------------------------------
set +e
{
    # set-target loescht das Build-Verzeichnis, also nur beim allerersten Lauf.
    if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
        echo "== Erstkonfiguration: Linux-Target =="
        idf.py -B "$BUILD_DIR" --preview set-target linux || exit $?
    fi
    echo "== Build =="
    idf.py -B "$BUILD_DIR" build
} >>"$LOG" 2>&1
build_status=$?
set -e

if [ "$build_status" -ne 0 ]; then
    tail -n "$TAIL_LINES" "$LOG"
    echo "HOST_TESTS_FAILED: Build (exit=$build_status). Volles Log: $LOG" >&2
    exit "$build_status"
fi

BIN="$(find "$BUILD_DIR" -maxdepth 1 -name '*.elf' -print -quit)"
if [ -z "$BIN" ]; then
    echo "FEHLER: kein Test-Binary in $BUILD_DIR. Volles Log: $LOG" >&2
    exit 1
fi

# --- Ausfuehren -------------------------------------------------------------
echo "== Tests: $(basename "$BIN") ==" >>"$LOG"
set +e
timeout "$TEST_TIMEOUT" "$BIN" >>"$LOG" 2>&1
status=$?
set -e

tail -n "$TAIL_LINES" "$LOG"

if [ "$status" -eq 124 ]; then
    echo "HOST_TESTS_FAILED: Timeout nach ${TEST_TIMEOUT}s (Deadlock?). Volles Log: $LOG" >&2
    exit 124
fi

if [ "$status" -ne 0 ]; then
    echo "HOST_TESTS_FAILED (exit=$status). Volles Log: $LOG" >&2
    exit "$status"
fi

echo "HOST_TESTS_PASSED"
