#!/bin/bash
# Flash the ESP32-C6 coprocessor directly via its own UART, bypassing the
# ESP32-P4 host and the SDIO OTA path entirely.
#
# This requires a physical USB-to-serial adapter wired directly to the C6's
# UART0 pins (TX/RX/GND) plus its EN (reset) and GPIO9 (boot strap) pins,
# NOT the P4's own USB port. On most P4+C6 boards this means soldering to
# test pads, since the C6 UART is normally only used at the factory and is
# not broken out to an accessible connector.
#
# Firmware flashed: our own build of examples/ota/coprocessor_ota/cp for
# esp32c6, from the same esp_hosted commit as the P4 host (app version
# 288a7f8-dirty) - NOT the vendored network_adapter.bin.2.6.7 or the
# (currently wrong) slave_firmware/network_adapter.bin.
#
# Usage: ./flash_c6_via_uart.sh /dev/ttyUSBx [baud]

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="$SCRIPT_DIR/binaries_v3.0.7"

PORT="${1:-}"
BAUD="${2:-460800}"

if [ -z "$PORT" ]; then
    echo -e "${RED}Error: serial port not specified${NC}"
    echo "Usage: $0 /dev/ttyUSBx [baud]"
    exit 1
fi

if [ ! -c "$PORT" ]; then
    echo -e "${RED}Error: $PORT does not exist or is not a serial device${NC}"
    exit 1
fi

for f in bootloader.bin partition-table.bin ota_data_initial.bin eh_cp_ota_coprocessor_ota.bin; do
    if [ ! -f "$BIN_DIR/$f" ]; then
        echo -e "${RED}Error: missing $BIN_DIR/$f${NC}"
        exit 1
    fi
done

if ! python -m esptool version &> /dev/null; then
    echo -e "${RED}Error: esptool (python -m esptool) not available. Source the ESP-IDF export.sh first.${NC}"
    exit 1
fi

echo -e "${GREEN}=== ESP32-C6 direct UART flash ===${NC}"
echo "Port:     $PORT"
echo "Baud:     $BAUD"
echo "Firmware: eh_cp_ota_coprocessor_ota.bin (app version 288a7f8-dirty, matches P4 host)"
echo ""
echo -e "${YELLOW}Put the C6 into download mode now if auto-reset (EN/GPIO9 via DTR/RTS)${NC}"
echo -e "${YELLOW}is not wired: hold BOOT, tap RESET, release BOOT.${NC}"
echo ""

python -m esptool \
    --chip esp32c6 \
    -p "$PORT" \
    -b "$BAUD" \
    --before default-reset \
    --after hard-reset \
    write-flash \
    --flash-mode dio \
    --flash-freq 80m \
    --flash-size 4MB \
    0x0     "$BIN_DIR/bootloader.bin" \
    0x8000  "$BIN_DIR/partition-table.bin" \
    0xd000  "$BIN_DIR/ota_data_initial.bin" \
    0x10000 "$BIN_DIR/eh_cp_ota_coprocessor_ota.bin"

echo ""
echo -e "${GREEN}✅ Flash complete.${NC}"
echo "Power-cycle the board (or reset both P4 and C6) and check the P4 host's"
echo "serial log: the 'esp-hosted fw versions' line should now show a"
echo "matching major version and no 'major version mismatch' warning."
