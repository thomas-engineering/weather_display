#!/usr/bin/env bash
#
# Wertet einen Boot-Log nach einem C6-Firmware-Test aus: laeuft die erwartete
# ESP32-C6-Firmware zusammen mit dem P4-Image, und arbeitet die Firmware
# dabei normal (WLAN, DNS/SNTP, Wetterabruf)?
#
# Nutzung:  ./scripts/c6-compat-check.sh <logdatei> <erwartete-c6-version>
#           z. B. ./scripts/c6-compat-check.sh .logs/c6-test.log 3.0.9
#
# Prueft zusaetzlich zu check-boot-log.sh (mit REQUIRE_IP=1: kein Panic, kein
# Reboot, BOOT_OK, got ip):
#   - "esp-hosted fw versions: host=H coprocessor=C" mit C = erwartet, und
#     entweder "(match)" oder "patch version differs (compatible)"; eine
#     abweichende Minor/Major-Version ist ein Fehler
#   - kein "major version mismatch"
#   - "clock set" (SNTP ueber den C6 hat geklappt)
#   - "forecast ok" (HTTPS-Abruf ueber den C6 hat geklappt)
#
# Exit 0 = PASS, Exit 1 = FAIL, Exit 2 = Aufruffehler.
# Siehe docs/c6-firmware-test.md fuer den ganzen Ablauf.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOGFILE="${1:-}"
EXPECTED="${2:-}"

if [ -z "$LOGFILE" ] || [ -z "$EXPECTED" ]; then
    echo "Nutzung: $0 <logdatei> <erwartete-c6-version>" >&2
    exit 2
fi
if [ ! -f "$LOGFILE" ]; then
    echo "FEHLER: $LOGFILE nicht gefunden" >&2
    exit 2
fi

fail=0

REQUIRE_IP=1 "$SCRIPT_DIR/check-boot-log.sh" "$LOGFILE" | grep -v '^PASS$' | grep -v '^FAIL$'
if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    fail=1
fi

# The raw serial log carries CR line endings.
clean_log="$(mktemp)"
trap 'rm -f "$clean_log"' EXIT
tr -d '\r' < "$LOGFILE" > "$clean_log"

versions_line=$(grep -F "esp-hosted fw versions:" "$clean_log" | tail -n1)
if [ -z "$versions_line" ]; then
    echo "FEHLER: keine Zeile 'esp-hosted fw versions:' im Log"
    fail=1
else
    host=$(sed -n 's/.*host=\([^ ]*\).*/\1/p' <<<"$versions_line")
    c6=$(sed -n 's/.*coprocessor=\([^ ]*\).*/\1/p' <<<"$versions_line")
    echo "Host $host, C6 $c6"
    if ! grep -qF "(match)" <<<"$versions_line" &&
       ! grep -qF "patch version differs (compatible)" "$clean_log"; then
        echo "FEHLER: Host und C6 sind nicht kompatibel: $versions_line"
        fail=1
    fi
    if [ "$c6" != "$EXPECTED" ]; then
        echo "FEHLER: C6 meldet $c6, erwartet $EXPECTED"
        fail=1
    fi
fi

while IFS=: read -r lineno _; do
    echo "FEHLER Zeile $lineno: 'major version mismatch'"
    fail=1
done < <(grep -nF "major version mismatch" "$clean_log")

for marker in "clock set" "forecast ok"; do
    if ! grep -qF -- "$marker" "$clean_log"; then
        echo "FEHLER: '$marker' fehlt im Log"
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "FAIL"
    exit 1
fi
echo "PASS"
exit 0
