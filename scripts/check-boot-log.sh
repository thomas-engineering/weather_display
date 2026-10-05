#!/usr/bin/env bash
#
# Wertet einen rohen Boot-Log (z. B. .logs/hw.log) aus.
#
# Nutzung:  ./scripts/check-boot-log.sh <logdatei>
#           REQUIRE_IP=1 ./scripts/check-boot-log.sh <logdatei>
#
# Exit 0 = PASS, Exit 1 = FAIL, Exit 2 = Aufruffehler.
# Ausgabe: eine Zeile pro Befund mit Zeilennummer im Log, am Ende PASS/FAIL.

set -uo pipefail

LOGFILE="${1:-}"
REQUIRE_IP="${REQUIRE_IP:-0}"
BOOT_MARKER='>>> BOOT_OK <<<'

if [ -z "$LOGFILE" ]; then
    echo "Nutzung: $0 <logdatei>" >&2
    exit 2
fi
if [ ! -f "$LOGFILE" ]; then
    echo "FEHLER: $LOGFILE nicht gefunden" >&2
    exit 2
fi

fail=0

FATAL_PATTERNS=(
    "Guru Meditation"
    "abort() was called"
    "A stack overflow"
    "CORRUPT HEAP"
    "Brownout detector"
    "Task watchdog got triggered"
)

for pat in "${FATAL_PATTERNS[@]}"; do
    while IFS=: read -r lineno _; do
        echo "FEHLER Zeile $lineno: '$pat'"
        fail=1
    done < <(grep -nF -- "$pat" "$LOGFILE")
done

# More than one ROM banner means the board rebooted while we were watching.
rom_lines=$(grep -nF -- "ESP-ROM:" "$LOGFILE" | cut -d: -f1)
rom_count=$(printf '%s\n' "$rom_lines" | grep -c . || true)
if [ "$rom_count" -gt 1 ]; then
    for lineno in $rom_lines; do
        echo "FEHLER Zeile $lineno: 'ESP-ROM:' (Reboot, ${rom_count}x im Log)"
    done
    fail=1
fi

if ! grep -qF -- "$BOOT_MARKER" "$LOGFILE"; then
    echo "FEHLER: '$BOOT_MARKER' fehlt"
    fail=1
fi

if [ "$REQUIRE_IP" = "1" ] && ! grep -qF -- "got ip" "$LOGFILE"; then
    echo "FEHLER: 'got ip' fehlt (REQUIRE_IP=1)"
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "FAIL"
    exit 1
fi
echo "PASS"
exit 0
