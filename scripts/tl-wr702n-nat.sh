#!/usr/bin/env bash
#
# Static-IP + NAT-Bruecke fuer den TL-WR702N-Testrouter, der bei den
# Wi-Fi-Ausfalltests (scripts/hil-outage-test.sh) als schaltbarer AP dient.
# Der TL-WR702N laeuft im Access-Point-Modus mit eigenem DHCP-Server auf
# 192.168.0.0/24 und deaktivierter DHCP-Gateway-Option (dieses Firmware hat
# kein eigenes "Gateway"-Feld, siehe review/wifi-stall-2026-09-22.md) —
# er haengt an einem freien Ethernet-Port dieser Workstation, die selbst als
# Gateway auftritt und per NAT auf das echte Internet-Interface routet.
#
# Braucht sudo fuer sysctl/iptables — interaktiv ausfuehren (per eigenem
# Terminal oder `!` im Claude-Prompt), nicht ueber einen Bash-Tool-Aufruf
# ohne TTY, sonst fragt sudo nach einem Passwort, das es nicht bekommt.
#
# Nutzung:
#   ./scripts/tl-wr702n-nat.sh up      # Static IP + NAT einrichten (Default)
#   ./scripts/tl-wr702n-nat.sh down    # Static IP zuruecksetzen, NAT-Regeln entfernen
#
# Interfaces/Adressen ueberschreibbar per Umgebungsvariable:
#   ETH_IF=enp0s25 WAN_IF=wlxac15a2a30f0e TEST_IP=192.168.0.2/24 \
#     ./scripts/tl-wr702n-nat.sh up
#
# WAN_IF wird sonst automatisch aus der Default-Route erraten. ip_forward
# wird bei "down" absichtlich nicht wieder ausgeschaltet — Docker/libvirt
# haben es typischerweise schon vor diesem Skript aktiviert und verlassen
# sich selbst darauf.

set -euo pipefail

ACTION="${1:-up}"
ETH_IF="${ETH_IF:-enp0s25}"
WAN_IF="${WAN_IF:-$(ip route show default 0.0.0.0/0 2>/dev/null | awk '/^default/ {print $5; exit}')}"
TEST_IP="${TEST_IP:-192.168.0.2/24}"
TEST_SUBNET="${TEST_SUBNET:-192.168.0.0/24}"
CONN_NAME="${CONN_NAME:-$(nmcli -t -f NAME,DEVICE connection show | awk -F: -v d="$ETH_IF" '$2==d {print $1; exit}')}"

if [ -z "$WAN_IF" ]; then
    echo "FEHLER: kein Default-Route-Interface gefunden, WAN_IF explizit setzen." >&2
    exit 2
fi
if [ -z "$CONN_NAME" ]; then
    echo "FEHLER: keine NetworkManager-Verbindung fuer $ETH_IF gefunden." >&2
    exit 2
fi

case "$ACTION" in
    up)
        echo "== TL-WR702N NAT-Bruecke einrichten =="
        echo "Ethernet: $ETH_IF ($CONN_NAME) -> $TEST_IP"
        echo "WAN:      $WAN_IF"

        nmcli connection modify "$CONN_NAME" ipv4.method manual ipv4.addresses "$TEST_IP" ipv6.method disabled
        nmcli connection up "$CONN_NAME" >/dev/null

        sudo sysctl -w net.ipv4.ip_forward=1 >/dev/null

        sudo iptables -t nat -C POSTROUTING -s "$TEST_SUBNET" -o "$WAN_IF" -j MASQUERADE 2>/dev/null || \
            sudo iptables -t nat -A POSTROUTING -s "$TEST_SUBNET" -o "$WAN_IF" -j MASQUERADE
        sudo iptables -C FORWARD -i "$ETH_IF" -o "$WAN_IF" -j ACCEPT 2>/dev/null || \
            sudo iptables -I FORWARD -i "$ETH_IF" -o "$WAN_IF" -j ACCEPT
        sudo iptables -C FORWARD -i "$WAN_IF" -o "$ETH_IF" -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || \
            sudo iptables -I FORWARD -i "$WAN_IF" -o "$ETH_IF" -m state --state RELATED,ESTABLISHED -j ACCEPT

        echo "Fertig. $ETH_IF ist auf $TEST_IP fest, NAT nach $WAN_IF aktiv."
        echo "TL-WR702N-Weboberflaeche: http://192.168.0.254 (SSID/Passwort dort setzen)."
        ;;
    down)
        echo "== TL-WR702N NAT-Bruecke abbauen =="

        sudo iptables -t nat -D POSTROUTING -s "$TEST_SUBNET" -o "$WAN_IF" -j MASQUERADE 2>/dev/null || true
        sudo iptables -D FORWARD -i "$ETH_IF" -o "$WAN_IF" -j ACCEPT 2>/dev/null || true
        sudo iptables -D FORWARD -i "$WAN_IF" -o "$ETH_IF" -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || true

        nmcli connection modify "$CONN_NAME" ipv4.method auto ipv6.method auto
        nmcli connection up "$CONN_NAME" >/dev/null 2>&1 || true

        echo "Fertig. $ETH_IF ist wieder auf DHCP, NAT-Regeln entfernt."
        echo "ip_forward bleibt an — wird typischerweise schon von Docker/libvirt gebraucht."
        ;;
    *)
        echo "Unbekannte Aktion: $ACTION (erwartet: up|down)" >&2
        exit 2
        ;;
esac
