#!/usr/bin/env bash
#
# Prueft scripts/c6-compat-check.sh gegen die synthetischen Logs in
# scripts/testdata/ (c6-*.log) — ohne Hardware.
#
# Exit 0 = alle Faelle wie erwartet, Exit 1 = mindestens ein Fall falsch.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHECK="$SCRIPT_DIR/c6-compat-check.sh"
DATA="$SCRIPT_DIR/testdata"

failures=0
total=0

# expect <logname> <erwartete c6-version> <expected exit>
expect() {
    local name="$1" version="$2" want="$3" got
    total=$((total + 1))
    "$CHECK" "$DATA/$name.log" "$version" >/dev/null 2>&1
    got=$?
    if [ "$got" -eq "$want" ]; then
        echo "ok   $name (erwartet C6 $version) -> $got"
    else
        echo "FAIL $name (erwartet C6 $version) -> $got (erwartet $want)"
        failures=$((failures + 1))
    fi
}

expect c6-ok               3.0.9 0
expect c6-ok               3.0.7 1   # C6 meldet 3.0.9, erwartet war 3.0.7
expect c6-other-version    3.0.7 0
expect c6-other-version    3.0.9 1
expect c6-version-mismatch 3.0.9 1
expect c6-version-mismatch 2.6.7 1   # "mismatch" und "major version mismatch" schlagen an
expect c6-patch-differs    3.0.7 0   # Host 3.0.9 + C6 3.0.7: Patch-Unterschied ist kompatibel
expect c6-patch-differs    3.0.9 1
expect c6-minor-differs    3.0.7 1
expect c6-crlf             3.0.9 0   # roher Seriell-Log mit CR
expect c6-no-forecast      3.0.9 1
expect c6-no-clock         3.0.9 1
expect c6-no-versions      3.0.9 1
expect ok                  3.0.9 1   # normaler Boot-Log ohne C6-Zeile

total=$((total + 1))
"$CHECK" >/dev/null 2>&1
if [ $? -eq 2 ]; then echo "ok   ohne Argumente -> 2"; else echo "FAIL ohne Argumente"; failures=$((failures + 1)); fi
total=$((total + 1))
"$CHECK" "$DATA/does-not-exist.log" 3.0.9 >/dev/null 2>&1
if [ $? -eq 2 ]; then echo "ok   fehlende Datei -> 2"; else echo "FAIL fehlende Datei"; failures=$((failures + 1)); fi

if [ "$failures" -ne 0 ]; then
    echo "C6_COMPAT_CHECK_TESTS_FAILED: $failures von $total"
    exit 1
fi
echo "C6_COMPAT_CHECK_TESTS_PASSED: $total Faelle"
