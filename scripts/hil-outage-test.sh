#!/usr/bin/env bash
#
# HIL-Test fuer die WLAN-Wiederherstellung: liest den Board-Log ueber einen
# festen Zeitraum mit und prueft danach, ob jede Trennung auch wieder in einer
# Verbindung endet.
#
# Der Ausfall selbst wird NICHT von diesem Skript erzeugt — dafuer fehlt hier
# ein schaltbarer AP (siehe README-Abschnitt unten). Das Skript ist bewusst
# unabhaengig davon, WIE der Ausfall entsteht:
#
#   * FRITZ!Box-Gast-WLAN in der Router-Oberflaeche aus- und wieder einschalten
#     (Display vorher auf das Gast-WLAN legen — trifft das Hauptnetz nicht),
#   * Board kurz in eine Blechdose / aus der Reichweite tragen,
#   * MAC des Boards in die WLAN-Zugangskontrolle aufnehmen und wieder loesen,
#   * zweiter USB-Stick am HIL-Host mit hostapd.
#
# Wie bei hw-flash.sh bewusst ohne `idf.py monitor`: der Port wird roh mit
# stty/cat unter `timeout` gelesen, das terminiert garantiert von selbst.
#
# Nutzung:  ./scripts/hil-outage-test.sh [/dev/ttyACM0] [--seconds 180] [--deadline 90]
#
#   --seconds   Beobachtungsdauer (Standard 180)
#   --deadline  Sekunden, die eine Wiederherstellung nach einer Trennung
#               hoechstens dauern darf (Standard 90 — der schnelle Burst plus
#               mehrere 15s-Takte des periodischen Retrys)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="$REPO_ROOT/.logs"
LOG="$LOG_DIR/hil-outage.log"

PORT="${ESPPORT:-/dev/ttyACM0}"
BAUD="${MONITOR_BAUD:-115200}"
SECONDS_TO_WATCH=180
DEADLINE=90

while [ $# -gt 0 ]; do
    case "$1" in
        --seconds)  SECONDS_TO_WATCH="$2"; shift 2 ;;
        --deadline) DEADLINE="$2"; shift 2 ;;
        /dev/*)     PORT="$1"; shift ;;
        *) echo "Unbekanntes Argument: $1" >&2; exit 2 ;;
    esac
done

if [ ! -e "$PORT" ]; then
    echo "FEHLER: $PORT nicht gefunden. Board angeschlossen?" >&2
    exit 2
fi

mkdir -p "$LOG_DIR"

cat <<EOF
== HIL-Ausfalltest ==
Port:      $PORT
Dauer:     ${SECONDS_TO_WATCH}s
Deadline:  ${DEADLINE}s pro Wiederherstellung

Jetzt den Ausfall ausloesen (Gast-WLAN aus, Board abschirmen, ...) und das
Netz vor Ablauf der Zeit wieder verfuegbar machen.
EOF

stty -F "$PORT" "$BAUD" raw -echo -echoe -echok
set +e
timeout "$SECONDS_TO_WATCH" cat "$PORT" | tee "$LOG"
set -e

echo
echo "== Auswertung =="
python3 - "$LOG" "$DEADLINE" <<'PY'
import re, sys

log, deadline_s = sys.argv[1], int(sys.argv[2])
deadline_ms = deadline_s * 1000

# ESP-IDF log lines look like "W (12345) wifi: disconnected (reason 200)".
line_re = re.compile(r'^[VDIWE]\s+\((\d+)\)\s+(\S+?):\s*(.*)$')

EVENTS = [
    ('disconnected',  re.compile(r'^disconnected \(reason (\d+)\)')),
    ('lost_ip',       re.compile(r'^lost ip')),
    ('refused',       re.compile(r'^esp_wifi_connect refused')),
    ('forced',        re.compile(r'^forcing reconnect to')),
    ('got_ip',        re.compile(r'^got ip')),
]

events = []
for raw in open(log, errors='replace'):
    m = line_re.match(raw.strip())
    if not m:
        continue
    t, tag, msg = int(m.group(1)), m.group(2), m.group(3)
    if tag != 'wifi':
        continue
    for name, pat in EVENTS:
        mm = pat.match(msg)
        if mm:
            events.append((t, name, mm.groups()))
            break

if not events:
    print("Keine wifi-Ereignisse im Log. Laeuft die Firmware und ist der Port richtig?")
    sys.exit(2)

counts = {}
for _, name, _ in events:
    counts[name] = counts.get(name, 0) + 1
print("Ereignisse:", ", ".join(f"{k}={v}" for k, v in sorted(counts.items())))

# Every outage (disconnected / lost ip) must be followed by a got ip within
# the deadline. An outage still open at the end of the capture only counts as
# a failure if the deadline already elapsed inside the capture window.
last_t = events[-1][0]
failures, recoveries = [], []
open_outage = None
for t, name, args in events:
    if name in ('disconnected', 'lost_ip'):
        if open_outage is None:
            open_outage = (t, name)
    elif name == 'got_ip':
        if open_outage is not None:
            recoveries.append(t - open_outage[0])
            open_outage = None

if open_outage is not None:
    waited = last_t - open_outage[0]
    if waited > deadline_ms:
        failures.append(f"{open_outage[1]} bei {open_outage[0]}ms ohne Wiederherstellung "
                        f"({waited/1000:.0f}s > {deadline_s}s)")
    else:
        print(f"Hinweis: Ausfall bei {open_outage[0]}ms war bei Aufnahmeende erst "
              f"{waited/1000:.0f}s alt — Deadline noch nicht erreicht, nicht gewertet.")

for d in recoveries:
    if d > deadline_ms:
        failures.append(f"Wiederherstellung dauerte {d/1000:.0f}s > {deadline_s}s")

if recoveries:
    print("Wiederherstellungen:", ", ".join(f"{d/1000:.1f}s" for d in recoveries))
if counts.get('refused'):
    print(f"Hinweis: {counts['refused']}x 'esp_wifi_connect refused' — genau der Fall, "
          "der die Retry-Kette frueher beendet hat; entscheidend ist, dass danach "
          "wieder ein 'got ip' kommt.")
if counts.get('forced'):
    print(f"Hinweis: {counts['forced']}x erzwungener Reconnect durch link_health_policy.")

if failures:
    print("\nFEHLGESCHLAGEN:")
    for f in failures:
        print("  -", f)
    sys.exit(1)

if not recoveries:
    print("\nKein vollstaendiger Ausfall-und-Rueckkehr-Zyklus aufgezeichnet — "
          "Test unschluessig (wurde der Ausfall ausgeloest?).")
    sys.exit(2)

print("\nHIL_OUTAGE_OK")
PY
