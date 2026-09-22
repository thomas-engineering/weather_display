# WLAN-Stalls trotz verfuegbarem AP — 2026-09-22

Geprueft: `main/app_wifi.c`, `main/app_weather.c`, `main/main.c`,
`components/app_logic/network_status_policy.c` sowie die darunterliegende
Konfiguration (`sdkconfig`: `ESP_HOSTED_*`, `ESP_WIFI_*`) und der
esp-hosted-/esp_wifi_remote-Pfad in `managed_components/`.
Fragestellung: Zustaende, in denen die Firmware dauerhaft offline bleibt,
obwohl der AP erreichbar ist.
Befunde: 8 — davon 7 behoben, 1 offen (plus 4 offene Testpunkte).

Grundlage fuer fast alle Befunde: `esp_wifi_connect()` ist auf diesem Board
ein **synchroner RPC zum ESP32-C6** (`eh_host_feat_rpc.c`, Default-Timeout
5 s). Der Aufruf kann also allein aus Transportgruenden fehlschlagen. Die
esp-hosted-eigenen Auto-Reconnect-Optionen sind aus
(`sdkconfig:4817`, `:4819`) — `app_wifi.c` ist der einzige Akteur. Reisst
seine Kette, holt sie niemand zurueck.

## Erledigt am 2026-09-22

Die Reconnect-Entscheidungen liegen jetzt in
`components/app_logic/wifi_reconnect_policy.c` (hostgetestet, inkl.
Invarianten-Fuzz ueber 20 000 Seeds), `main/app_wifi.c` ist nur noch der
Adapter dazu.

| # | Befund | Behoben durch |
|---|---|---|
| 1 | Rueckgabewert von `esp_wifi_connect()` ueberall ignoriert; ein abgelehnter Aufruf beendete die Retry-Kette dauerhaft | `WRP_EV_CONNECT_REJECTED` → Timer armieren; Adapter prueft den Rueckgabewert |
| 2 | `IP_EVENT_STA_LOST_IP` nicht behandelt; UI blieb auf „Online", waehrend alles fehlschlug | `ESP_EVENT_ANY_ID` auf `IP_EVENT`, `WRP_EV_LOST_IP` |
| 3 | Wiederholte Fetch-Fehler eskalierten nie zu einer WLAN-Massnahme | `components/app_logic/link_health_policy.c` + `app_wifi_verify_link()` / `app_wifi_force_reconnect()` |
| 4 | Scan schaltete die Reconnect-Logik bis zu 30 s komplett ab und verschluckte Disconnects | `WRP_EV_SCAN_BEGIN`/`_END`, Retry wird aufgeschoben statt verworfen |
| 5 | `connect_locked()` beantwortete den selbst ausgeloesten Disconnect mit einem konkurrierenden Connect | `ignore_disconnects` in der Policy |
| 6 | Blockierender Connect im esp_timer-/Event-Loop-Task | eigener Task `wifi_reconn`; esp_timer entfallen |
| 7 | `app_wifi_init()` brach vor `load_credentials()` ab → „kein Netz gespeichert" trotz gueltiger Credentials | Credentials werden zuerst geladen |

Zwei weitere Defekte fand der Invarianten-Fuzz beim ersten Lauf, bevor sie
von Hand konstruiert waren: `WRP_EV_MANUAL_CONNECT` setzte `connected` nicht
zurueck (ein spaeterer Timer-Tick entwaffnete sich daraufhin selbst), und der
`ignore_disconnects`-Pfad verwarf das Ereignis auch dann, wenn gar kein
Connect mehr unterwegs war. Beide haben eigene Regressionstests.

Auf Hardware belegt: der Boot-Log enthaelt bei **jedem** Start
`W (3453) wifi: esp_wifi_connect refused: ESP_ERR_WIFI_SSID` — der
STA_START-Autoconnect wird abgelehnt, weil die esp_wifi-eigene NVS-Config zu
diesem Zeitpunkt leer ist. Vor dem Fix verschwand genau diese Ablehnung
stillschweigend; harmlos war sie nur, weil `connect_locked()` zufaellig
danach lief.

## [MITTEL] 30-s-Wartezeit in `connect_locked()` ist kuerzer als der Retry-Burst

Datei: `main/app_wifi.c` (Wartezeit in `connect_locked()`), Budget aus
`CONFIG_WEATHER_WIFI_MAX_RETRY=8` (`sdkconfig:1328`).

Problem: Acht Assoziationsversuche hintereinander dauern auf einem schwachen
oder ausgelasteten AP laenger als 30 s. `connect_locked()` liefert dann
`false`, obwohl der Burst noch laeuft und die Verbindung Sekunden spaeter
zustande kommt.

Warum: `weather_task` oeffnet daraufhin das Wi-Fi-Setup
(`main/app_weather.c`, Startup-Pfad) bzw. meldet dem Dialog einen
fehlgeschlagenen Connect — vor dem Nutzer sieht das aus wie ein Fehler,
obwohl das Geraet gleich darauf online geht. Selbstheilend ist es
(`wifi_now_connected != wifi_was_connected` greift), aber es ist ein
sichtbarer Falschalarm. Seit dem Fix ist die Lage etwas besser: `signal_failed`
wird jetzt auch bei einem abgelehnten Connect gesetzt, der Aufruf kehrt also
in dem Fall sofort zurueck statt ins Timeout zu laufen.

Vorschlag: Keine blosse Konstantenaenderung — das ist eine UX-Entscheidung.
Entweder (a) die Wartezeit an das Burst-Budget koppeln
(`MAX_RETRY * typische Versuchsdauer`, grob 60 s), oder (b) bei 30 s bleiben
und dem Dialog statt „fehlgeschlagen" einen dritten Zustand
„versucht weiter" geben, gespeist aus `app_wifi_is_reconnecting()`. (b)
passt besser zur vorhandenen `WX_NET_RECONNECTING`-Anzeige.

Nachweis: Auf Hardware mit einem absichtlich weit entfernten oder stark
ausgelasteten AP verbinden und die Zeit zwischen
`wifi: connecting to "..."` und `wifi: got ip` gegen die 30 s halten.

## [OFFEN, Test] Der Ausfallzyklus ist auf Hardware nie gelaufen

Problem: Alle bisherigen Hardware-Laeufe sind Boot-Smoke-Tests. Ein echter
Zyklus „AP weg → Retry-Burst → periodischer Retry → AP zurueck → `got ip`"
wurde noch kein einziges Mal beobachtet. Die Fixes fuer #1/#2/#4/#5 sind
damit hostgetestet, aber auf dem Board unbelegt.

Nachweis: `./scripts/hil-outage-test.sh [/dev/ttyACM0] [--seconds N]
[--deadline N]`. Das Skript zeichnet den Log auf und prueft, dass auf jede
Trennung innerhalb der Deadline ein `got ip` folgt; es druckt
`HIL_OUTAGE_OK` oder die konkrete Abweichung. Den Ausfall loest ein Mensch
aus. Der Analyseteil ist gegen synthetische Logs in beide Richtungen
geprueft (75-s-Erholung: Fehlschlag bei `--deadline 60`, Erfolg bei `90`).

## [OFFEN, Test] Kein schaltbarer AP am Entwicklungsrechner

Problem: hostapd auf dem Dev-Host ist **nicht** benutzbar. Die einzige
WLAN-Karte (`wlxac15a2a30f0e`) traegt die einzige Internet-Route des Rechners
(`default via 192.168.178.1`), `enp0s25` hat kein Kabel. Ein AP-Betrieb
wuerde den Rechner vom Netz nehmen und dem Board einen AP ohne Uplink
hinstellen — das prueft „kein Internet", nicht „AP-Ausfall".

Vorschlag, nach Aufwand sortiert:
1. **FRITZ!Box-Gast-WLAN** in der Router-Oberflaeche an-/ausschalten, Display
   vorher auf das Gast-WLAN legen. Keine zusaetzliche Hardware, das Hauptnetz
   bleibt unberuehrt. Fuer den naechsten Test der empfohlene Weg.
2. Zweiter USB-WLAN-Stick am HIL-Host: dann traegt Stick A weiter den Uplink
   und Stick B laeuft als hostapd-AP mit NAT — vollstaendig skriptbar und
   CI-faehig, inkl. `hostapd_cli deauthenticate` und kurzer DHCP-Leases fuer
   den LOST_IP-Pfad (#2).
3. MAC des Boards in die WLAN-Zugangskontrolle des Routers aufnehmen:
   erzeugt Deauth **und** anschliessende Abweisung, also den Fall „AP da,
   Station kommt nicht rein".
4. Abschirmung (Blechdose) — grob, aber erzeugt echten Beacon-Timeout.

## [OFFEN, Test] Die Eskalation aus `link_health_policy` hat auf Hardware nie ausgeloest

Problem: Der Pfad „assoziiert, adressiert, trotzdem faellt jeder Fetch aus"
laesst sich weder durch einen AP-Ausfall noch durch eine Deauth herstellen —
genau deshalb gab es ihn vorher nicht. Host-Tests decken die Schwellen ab,
die Verdrahtung in `do_refresh()` ist unbelegt.

Nachweis: Am einfachsten durch Fehlerinjektion — waehrend der Laufzeit die
Open-Meteo-Hosts unerreichbar machen, ohne die Assoziation zu loesen (im
Router eine DNS-/Firewall-Regel fuer die IP des Displays setzen, oder
`api.open-meteo.com` im Router auf 127.0.0.1 umbiegen). Erwartet im Log:
nach drei fehlgeschlagenen Fetches
`weather: ... failed fetches while Wi-Fi reports Online; checking the link`,
nach fuenf `wifi: forcing reconnect to "..."`.

## [OFFEN] `main/selftest.c` existiert nicht, `emu-test.sh` prueft nie erfuellbare Marker

Datei: `scripts/emu-test.sh:23-24`, CLAUDE.md-Abschnitt „Marker".

Problem: `emu-test.sh` wertet `SELFTEST_OK`/`SELFTEST_FAIL` aus, und CLAUDE.md
verweist auf `run_selftest()` in `main/selftest.c`. Diese Datei gibt es im
Baum nicht, und keine Quelle druckt einen der beiden Marker (Suche ueber
`--include=*.c --include=*.h` ergab nur die beiden Zeilen im Skript selbst).

Warum: Das Skript kann auf Markerbasis nie erfolgreich sein. Nicht
WLAN-spezifisch, faellt hier nur auf, weil es die naechstliegende Stelle fuer
einen automatisierten Integrationstest waere. Zusammen mit dem Hinweis in
der Projektmemory (esp-emu faultet derzeit beim ROM-Boot) heisst das: die
Emulatorebene steht fuer WLAN ohnehin nicht zur Verfuegung — esp-hosted,
SDIO und der C6 werden vom Emulator nicht abgedeckt.

Vorschlag: Entweder `main/selftest.c` mit `run_selftest()` und den beiden
Markern anlegen, oder CLAUDE.md und `emu-test.sh` um den Marker-Mechanismus
bereinigen. Die Entscheidung haengt daran, ob der Emulator wieder benutzbar
wird.

## Nicht beanstandet

- `task_heartbeat` meldet einen haengenden `weather_task` korrekt, unternimmt
  aber bewusst nichts — das ist dokumentiert (`main/task_heartbeat.h`) und
  hier kein eigener Befund, sondern Kontext zu #3: vor dem Fix war die
  ERROR-Zeile alle 5 s die einzige Reaktion des Systems.
- `s_network_mutex` mit 60-s-Timeout: deckt den Fall „OTA haelt den Mutex"
  ab, der Initial-Connect wird dadurch hoechstens verzoegert, nicht verhindert
  (der STA_START-Connect laeuft unabhaengig davon).
- Power-Save ist nirgends explizit gesetzt; die esp-hosted-Option
  `ESP_HOSTED_HOST_FEAT_POWER_SAVE` ist aus (`sdkconfig:4853`). Kein Hinweis
  auf beacon-bedingte Aussetzer.
