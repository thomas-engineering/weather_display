# <weather_display> — ESP32-P4 / ESP-IDF

<Das Gerät holt Wetterdaten von open-meteo und zeigt diese auf einem Display an. Die Hardware ist ein board
mit ESP32-P4, 7-zoll-Display 1024*600, Touchscreen und Wifi. https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B
ESP32-P4 auf diesem board ist noch Hardware-Vesion 1.3>

## Testleiter — welcher Befehl wann

| Wenn du das geändert hast | Führe das aus | Dauer |
|---|---|---|
| `components/app_logic/**` | `./scripts/host-test.sh` | ~5 s |
| `main/**`, Treiber, Startup, sdkconfig | `./scripts/emu-test.sh` | ~60 s |
| UI-Darstellung (`main/weather_ui.c`, `weather_chart.c`, `weather_icons.c`, `weather_i18n.c`, `app_format.c`, `main/fonts/**`) | `./scripts/sim.sh --screen <name> --screenshot <datei.bmp>` und den Screenshot ansehen; `--shots <verzeichnis>` nimmt alle sechs Zustände auf | ~5 s pro Screen, ~20 s für alle |
| Peripherie-Anbindung (Display, Kamera, SDMMC, ESP-Hosted) | nichts automatisch — sag mir, dass ich auf Hardware testen soll |

Nach jeder inhaltlichen Änderung mindestens `./scripts/host-test.sh` laufen
lassen, bevor du die Aufgabe als erledigt meldest. Beide Skripte beenden sich
von selbst und liefern Exit-Code 0 nur bei Erfolg.

## Verbotene Befehle

Diese Kommandos enden nie von selbst und blockieren die Session:

- `idf.py monitor` — stattdessen `./scripts/emu-test.sh`, das Log liegt in `.logs/emu-test.log`
- `esp-emu` direkt ohne `--timeout` — immer über `./scripts/emu-test.sh`
- `./scripts/sim.sh` ohne `--screenshot` — das öffnet ein Fenster und läuft, bis
  ich es schließe. Der interaktive Simulator ist für mich; du nimmst
  `--screenshot <datei.bmp>`, das beendet sich von selbst.
- `idf.py flash` / `./scripts/hw-flash.sh` — Flashen auf echte Hardware mache ich, nicht du. Sag mir, wenn ein Hardwaretest nötig ist.

Ebenfalls nicht selbst ausführen:

- `idf.py set-target` — löscht das Build-Verzeichnis und erzwingt einen
  Full-Rebuild. Die Skripte machen das genau einmal.
- `idf.py fullclean` oder `rm -rf build*` — nur auf ausdrückliche Ansage.

## Build-Verzeichnisse

| Pfad | Zweck | Wer baut |
|---|---|---|
| `host_test/build-linux/` | Host-Unit-Tests, Linux-Target | `host-test.sh` |
| `sim/build-sim/` | LVGL-Simulator, natives CMake ohne IDF | `sim.sh` |
| `build-emu/` | Firmware mit Emulator-sdkconfig (ROM-Rev 0) | `emu-test.sh` |
| `build/` | flashbare Firmware für echte Hardware | Mensch |

Nicht mischen. Kein `-B` von Hand. Die Konfigurationen unterscheiden sich
(`sdkconfig.defaults.emu`), ein gemeinsames Build-Verzeichnis liefert falsche
Ergebnisse.

## Logs

Alle Läufe schreiben nach `.logs/`. Bei einem Fehlschlag zuerst
`.logs/host-test.log` bzw. `.logs/emu-test.log` lesen, nicht blind neu starten.
Die Skripte geben nur die letzten Zeilen aus, das vollständige Log steht in der
Datei.

## Umgebung

Die Skripte sourcen `$IDF_PATH/export.sh` selbst, wenn `idf.py` fehlt. Du musst
das nie manuell tun und keine `source`-Zeile in einen Befehl einbauen. Falls ein
Skript mit Exit-Code 127 abbricht, fehlt ESP-IDF oder `esp-emu` — melde das,
statt Workarounds zu bauen.

## Codeorganisation

`components/app_logic/` ist hardwarefrei: keine IDF-Header, kein `driver/`, kein
`freertos/`, keine Register. Zeit und I/O kommen über die Callback-Struktur
`app_logic_io_t` herein. Alles hier ist auf dem Host testbar und wird auch dort
getestet.

`main/` und die `hal_*`-Komponenten fassen Hardware an und werden nicht
host-getestet.

**Neue Logik gehört nach `app_logic`.** Wenn du Zustandslogik, Parsing,
Protokollbehandlung oder Fehlerentscheidungen in `main/` schreiben willst, zieh
sie stattdessen nach `app_logic` und lass `main/` nur die Adapter halten. Wenn
das nicht geht, sag warum, bevor du es anders machst.

## Grenzen der Testebenen

Der Host-Test (Linux-Target) ist **keine P4-Simulation**. Er deckt nicht ab:
Timing, Interrupts, Cache- und PSRAM-Verhalten, DMA, Stackgrößen,
Speicherausrichtung, echte Nebenläufigkeit. Der FreeRTOS-POSIX-Simulator läuft
single-core, auch mit SMP-Konfiguration.

Der Emulator deckt CPU, Speicher, UART, GPIO, Timer, CLIC, eFuse, SPI-Flash,
GDMA, EMAC und Krypto ab. Er deckt **nicht** ab: LP-Core, PSRAM-Timing (nur als
Zero-Init-RAM hinterlegt), MIPI-DSI, PPA, H.264, PTP und alle angeschlossene
Peripherie.

Alles unterhalb von display_port_dsi.c ist im Emulator nicht testbar und wird dort auch nicht gebaut. Änderungen an DSI-Init, Panel-Timings oder PPA-Nutzung meldest du mir als "braucht Hardwaretest", statt sie über emu-test.sh zu prüfen. Neuer Zeichencode gehört oberhalb von display_port_t, damit er im MEMBUF-Backend läuft.

Der Simulator (`sim/`) zeigt echtes LVGL-Rendering derselben UI-Quellen in
derselben Auflösung und Farbtiefe, aber gegen einen SDL-Softwarepfad. Er deckt
**nicht** ab: DSI-Timing, PPA-Rotation, Tear-Avoid-Modus, LVGL-Puffergrößen,
PSRAM-Durchsatz und das Farbverhalten des echten Panels. Layout, Typografie,
Farben, Zustandslogik der Screens und die Callback-Reihenfolge deckt er ab.

Aus grünen Host- oder Emulator-Tests folgt also nicht, dass es auf dem Board
läuft. Schreib das in deine Zusammenfassung dazu, wenn die Änderung Hardware
berührt.



## Marker

Die Firmware druckt am Ende des Selbsttests `>>> SELFTEST_OK <<<` oder
`>>> SELFTEST_FAIL <<<`. `emu-test.sh` wertet genau diese Strings aus. Erweitere
für neue Integrationstests die Funktion `run_selftest()` in `main/selftest.c` und
lass die Marker unverändert.
