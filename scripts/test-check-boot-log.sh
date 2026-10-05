#!/usr/bin/env bash
#
# Prueft scripts/check-boot-log.sh gegen die synthetischen Logs in
# scripts/testdata/ — ohne Hardware.
#
# Exit 0 = alle Faelle wie erwartet, Exit 1 = mindestens ein Fall falsch.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHECK="$SCRIPT_DIR/check-boot-log.sh"
DATA="$SCRIPT_DIR/testdata"

failures=0
total=0

# expect <require_ip> <logname> <expected exit>
expect() {
    local require_ip="$1" name="$2" want="$3" got
    total=$((total + 1))
    REQUIRE_IP="$require_ip" "$CHECK" "$DATA/$name.log" >/dev/null 2>&1
    got=$?
    if [ "$got" -eq "$want" ]; then
        echo "ok   REQUIRE_IP=$require_ip $name -> $got"
    else
        echo "FAIL REQUIRE_IP=$require_ip $name -> $got (erwartet $want)"
        failures=$((failures + 1))
    fi
}

expect 0 ok            0
expect 1 ok            0
expect 0 panic         1
expect 1 panic         1
expect 0 reboot-loop   1
expect 1 reboot-loop   1
expect 0 no-marker     1
expect 1 no-marker     1
expect 0 ok-without-ip 0
expect 1 ok-without-ip 1

# Call errors: missing argument / missing file.
total=$((total + 1))
"$CHECK" >/dev/null 2>&1
if [ $? -eq 2 ]; then echo "ok   ohne Argument -> 2"; else echo "FAIL ohne Argument"; failures=$((failures + 1)); fi
total=$((total + 1))
"$CHECK" "$DATA/does-not-exist.log" >/dev/null 2>&1
if [ $? -eq 2 ]; then echo "ok   fehlende Datei -> 2"; else echo "FAIL fehlende Datei"; failures=$((failures + 1)); fi

if [ "$failures" -ne 0 ]; then
    echo "CHECK_BOOT_LOG_TESTS_FAILED: $failures von $total"
    exit 1
fi
echo "CHECK_BOOT_LOG_TESTS_PASSED: $total Faelle"
