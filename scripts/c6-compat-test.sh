#!/usr/bin/env bash
#
# Liest den Boot-Log des Boards nach einem Reset mit und wertet ihn mit
# scripts/c6-compat-check.sh aus: laeuft die erwartete C6-Firmware sauber mit
# dem aktuellen P4-Image?
#
# Nutzung:  ./scripts/c6-compat-test.sh <erwartete-c6-version> [/dev/ttyACM0] [sekunden]
#           z. B. ./scripts/c6-compat-test.sh 3.0.9 /dev/ttyACM0 90
#
# Der Reset kommt per USB (esptool, --after hard-reset), das Board wird dabei
# nicht neu geflasht. Wie hw-flash.sh ohne `idf.py monitor`: der Port wird roh
# mit stty/cat unter `timeout` gelesen. Log: .logs/c6-test.log
#
# Exit 0 = PASS, 1 = FAIL, 2 = Aufruffehler, 127 = ESP-IDF fehlt.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="$REPO_ROOT/.logs"
LOG="$LOG_DIR/c6-test.log"
EXPECTED="${1:-}"
PORT="${2:-${ESPPORT:-/dev/ttyACM0}}"
SECONDS_TO_READ="${3:-90}"
BAUD="${MONITOR_BAUD:-115200}"

if [ -z "$EXPECTED" ]; then
    echo "Nutzung: $0 <erwartete-c6-version> [/dev/ttyACM0] [sekunden]" >&2
    exit 2
fi
if [ ! -e "$PORT" ]; then
    echo "FEHLER: $PORT nicht gefunden. Board angeschlossen?" >&2
    exit 2
fi

mkdir -p "$LOG_DIR"
# shellcheck source=lib/idf-env.sh
source "$REPO_ROOT/scripts/lib/idf-env.sh"

# Reset first, then read: esptool and a reader on the same port interfere
# (the reader's stty breaks esptool's reset sequence). The reset leaves about
# a second of ROM start-up before the application's first log line, so a
# reader started right afterwards still sees the whole application log.
echo "== Reset per USB, dann ${SECONDS_TO_READ}s Boot-Log =="
if ! python -m esptool --chip esp32p4 -p "$PORT" --after hard-reset chip-id >/dev/null 2>&1; then
    echo "FEHLER: Reset per esptool fehlgeschlagen" >&2
    exit 2
fi

# The USB serial port disappears and comes back during the reset, which ends
# a single `cat`. Reopen it in a loop so the log spans the reboot.
read_port() {
    while :; do
        if [ -e "$PORT" ] && stty -F "$PORT" "$BAUD" raw -echo -echoe -echok 2>/dev/null; then
            cat "$PORT" 2>/dev/null
        fi
        sleep 0.1
    done
}

: > "$LOG"
export -f read_port
export PORT BAUD
timeout "$SECONDS_TO_READ" bash -c read_port > "$LOG" &
reader=$!
wait "$reader" 2>/dev/null
echo "Log: $LOG"
echo "== Auswertung (erwartet C6 $EXPECTED) =="
exec "$REPO_ROOT/scripts/c6-compat-check.sh" "$LOG" "$EXPECTED"
