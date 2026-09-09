#!/usr/bin/env bash
#
# Firmware-Smoketest im Emulator (esp-emu, Chip esp32p4).
# Baut mit der Emulator-sdkconfig, mergt das Flash-Image und startet den
# Emulator mit hartem Timeout.
#
# Exit 0  = PASS_MARKER in der UART-Ausgabe gefunden
# Exit 1  = FAIL_MARKER, Panic, oder Marker nicht innerhalb des Timeouts
# Exit 127 = ESP-IDF oder esp-emu fehlt
#
# Bewertet wird die UART-Ausgabe, nicht der Exit-Code des Emulators:
# ein Emulator, der nach --timeout sauber beendet, hat nichts bewiesen.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf}"

BUILD_DIR="$REPO_ROOT/build-emu"
LOG_DIR="$REPO_ROOT/.logs"
LOG="$LOG_DIR/emu-test.log"

PASS_MARKER="${PASS_MARKER:-SELFTEST_OK}"
FAIL_MARKER="${FAIL_MARKER:-SELFTEST_FAIL}"
EMU_TIMEOUT="${EMU_TIMEOUT:-30s}"

mkdir -p "$LOG_DIR"

# --- Umgebung ---------------------------------------------------------------
if ! command -v esp-emu >/dev/null 2>&1; then
    echo "FEHLER: esp-emu nicht im PATH." >&2
    echo "Installation: curl -fsSL https://raw.githubusercontent.com/espressif/esp-emulator/main/install.sh | sh" >&2
    exit 127
fi

if ! command -v idf.py >/dev/null 2>&1; then
    if [ ! -f "$IDF_PATH/export.sh" ]; then
        echo "FEHLER: ESP-IDF nicht gefunden. IDF_PATH=$IDF_PATH" >&2
        exit 127
    fi
    # shellcheck disable=SC1091
    source "$IDF_PATH/export.sh" >/dev/null
fi

cd "$REPO_ROOT"

# --- Build ------------------------------------------------------------------
# Eigene sdkconfig: der Emulator zielt auf ROM-Revision 0, echte Boards nicht.
# Deshalb getrenntes Build-Verzeichnis, damit build/ flashbar bleibt.
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "== Erstkonfiguration: esp32p4 (Emulator-Profil) =="
    idf.py -B "$BUILD_DIR" \
        -D SDKCONFIG="$BUILD_DIR/sdkconfig" \
        -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.emu" \
        set-target esp32p4
fi

echo "== Build =="
idf.py -B "$BUILD_DIR" build

echo "== Flash-Image mergen =="
idf.py -B "$BUILD_DIR" merge-bin -o "$BUILD_DIR/merged_flash.bin"

# --- Emulator ---------------------------------------------------------------
echo "== Emulator (Timeout $EMU_TIMEOUT, Marker '$PASS_MARKER') =="
set +e
esp-emu \
    --chip esp32p4 \
    --firmware "$BUILD_DIR/merged_flash.bin" \
    --net user \
    --timeout "$EMU_TIMEOUT" \
    --exit-on "$PASS_MARKER" \
    >"$LOG" 2>&1
set -e

echo "--- letzte 40 Zeilen ---"
tail -n 40 "$LOG"
echo "------------------------"

# --- Auswertung -------------------------------------------------------------
if grep -qE "Guru Meditation|panic'ed|abort\(\) was called|StoreProhibited|LoadProhibited" "$LOG"; then
    echo "EMU_TEST_FAILED: Panic oder Abort in der Ausgabe. Volles Log: $LOG" >&2
    exit 1
fi

if grep -q "$FAIL_MARKER" "$LOG"; then
    echo "EMU_TEST_FAILED: '$FAIL_MARKER' gemeldet. Volles Log: $LOG" >&2
    exit 1
fi

if grep -q "$PASS_MARKER" "$LOG"; then
    echo "EMU_TEST_PASSED"
    exit 0
fi

echo "EMU_TEST_FAILED: '$PASS_MARKER' nicht innerhalb von $EMU_TIMEOUT erschienen." >&2
echo "Haengt die Firmware, oder fehlt der Marker im Selbsttest? Volles Log: $LOG" >&2
exit 1
