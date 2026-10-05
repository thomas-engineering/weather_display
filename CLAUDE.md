# weather_display — ESP32-P4 / ESP-IDF

Das Gerät holt Wetterdaten von Open-Meteo und zeigt sie auf einem 7-Zoll-Touchdisplay (1024×600).

## Plattform

- Board: https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B; WLAN über
  einen ESP32-C6-Coprozessor per SDIO (ESP-Hosted)
- ESP32-P4 Hardware-Revision 1.3 (`rev1_3`-Profil in `sdkconfig.defaults`),
  ESP-IDF v6.1 (README, `.github/workflows/Manual_Dual-Chip_Release_Build.yml`)

## Testleiter — welcher Befehl wann

| Wenn du das geändert hast | Führe das aus |
|---|---|
| `components/app_logic/**` | `./scripts/host-test.sh` |
| UI-Darstellung (`weather_ui.c`, `weather_chart.c`, `weather_icons.c`, `weather_i18n.c`, `app_format.c`, `main/fonts/`) | `./scripts/sim.sh --shots <verzeichnis>`, Screenshots ansehen |
| alles andere in `main/`, sdkconfig, Abhängigkeiten, CMake | `./scripts/fw-build.sh` |
| Peripherie, Startup, WLAN, OTA, Timing, Speicherlayout | zusätzlich `./scripts/hw-flash.sh /dev/ttyACM0` (bei WLAN/OTA mit `REQUIRE_IP=1`) |
| WLAN-Wiederherstellung (`app_wifi.c`, `wifi_reconnect_policy`, `link_health_policy`) | zusätzlich `./scripts/hil-outage-test.sh` (Mensch löst den Ausfall aus) |
| `scripts/check-boot-log.sh` | `./scripts/test-check-boot-log.sh` |

Nach jeder inhaltlichen Änderung mindestens `./scripts/host-test.sh` laufen
lassen, bevor du die Aufgabe als erledigt meldest. Alle Skripte beenden sich
von selbst und liefern Exit-Code 0 nur bei Erfolg. `sim.sh --shots` nimmt acht
Zustände auf (sieben Screens plus `first-boot`); einzeln:
`./scripts/sim.sh --screen <name> --screenshot <datei.bmp>`.

`hw-flash.sh` baut über `fw-build.sh`, flasht, liest den Boot-Log
(`MONITOR_SECONDS`, Standard 20 s) nach `.logs/hw.log` und prüft ihn mit
`check-boot-log.sh`: Panic, Reboot oder fehlendes `>>> BOOT_OK <<<` (Ende von
`app_main()`) ergeben Exit 1; mit `REQUIRE_IP=1` auch fehlendes `got ip`.

## Verbotene Befehle

| Befehl | Grund | Stattdessen |
|---|---|---|
| `idf.py monitor` | interaktiv, bricht ohne TTY ab, blockiert die Session | `./scripts/hw-flash.sh` |
| `idf.py flash` direkt | kein Timeout beim anschließenden Lesen | `./scripts/hw-flash.sh` |
| `idf.py set-target` | löscht das Build-Verzeichnis | die Skripte machen das einmal selbst |
| `idf.py fullclean`, `rm -rf build*` | erzwingt Full-Rebuild | nur auf ausdrückliche Ansage |
| `./scripts/sim.sh` ohne `--shots`/`--screenshot` | öffnet ein Fenster, läuft bis zum Schließen | `--shots` oder `--screenshot` |
| `git push`, `scripts/gh-push.sh` | Push und Merge macht der Mensch | auf dem Feature-Branch committen |

## Build-Verzeichnisse

| Pfad | Zweck | Wer baut |
|---|---|---|
| `host_test/build-linux/` | Host-Unit-Tests, Linux-Target | `host-test.sh` |
| `sim/build-sim/` | LVGL-Simulator, natives CMake ohne IDF | `sim.sh` |
| `build/` | flashbare Firmware | ausschließlich `fw-build.sh` und `hw-flash.sh` |

Nicht mischen, kein `-B` von Hand. Das generierte `sdkconfig` (gitignored)
überschreibt `sdkconfig.defaults`: Nach einer Änderung dort prüfen, ob der Wert
im `sdkconfig` angekommen ist, und ihn sonst dort von Hand nachziehen.

## Logs und Umgebung

Alle Läufe schreiben nach `.logs/` (`host-test.log`, `fw-build.log`, `hw.log`,
jeweils mit Build). Bei einem Fehlschlag zuerst das Log lesen, nicht blind neu
starten.

Die Skripte richten ESP-IDF selbst ein (`scripts/lib/idf-env.sh`): zuerst
`~/.espressif/tools/activate_idf_v6.1.sh` (überschreibbar per `IDF_ACTIVATE`),
sonst `$IDF_PATH/export.sh`. Kein `source` in Befehle einbauen. Exit-Code 127
heißt: ESP-IDF fehlt — melde das, statt Workarounds zu bauen.

## Codeorganisation

`components/app_logic/` ist hardwarefrei: keine IDF-Header, kein `driver/`, kein
`freertos/`, keine Register. Zeit und I/O kommen über Callback-Strukturen herein
(z. B. `storage_backend_t`). Alles hier ist auf dem Host testbar und wird dort
getestet. `main/` fasst Hardware an und wird nicht host-getestet. Warnungen in
`main/` und `components/app_logic/` sind Fehler (`-Werror`).

**Neue Logik gehört nach `app_logic`.** Zustandslogik, Parsing,
Protokollbehandlung oder Fehlerentscheidungen nicht in `main/` schreiben,
sondern nach `app_logic` ziehen und in `main/` nur Adapter lassen. Geht das
nicht, sag warum, bevor du es anders machst.

**Kommentare im Quellcode sind immer auf Englisch**, auch beim Überarbeiten
bestehender Zeilen. Einen bestehenden deutschen Kommentar nicht extra suchen
und umschreiben, wenn die Datei sonst nicht angefasst wird.

## Konstanten

- Keine nackten Zahlenwerte in neuem Code außer 0, 1, -1 und offensichtlichen
  Einheitenumrechnungen. Bestehende Literale nur in eigenen Refactoring-Paketen
  benennen, nicht nebenbei in einem fachlichen Commit.
- Name nach Bedeutung, nicht nach Wert; Einheit als Suffix (`_MS`, `_US`, `_S`,
  `_BYTES`, `_HZ`, `_DBM`, `_PCT`).
- Werte, die eine API schon definiert (Puffergrößen, HTTP-Status), ableiten
  statt neu tippen.
- Nur in einer Datei genutzt → oben in der `.c`; mehrfach genutzt →
  gemeinsamer Header, nie doppelt definieren.
- Bei unklarer Bedeutung nicht raten, sondern fragen.

## Projektregeln

- **UI-Werte:** Farben, Abstände und Radien kommen aus dem Nocturne-Designsystem
  (`design/src/nocturne/`, extrahiert mit `tools/extract-design.mjs`). Keine
  Werte frei erfinden.
- **`managed_components/`** ist gitignored und wird nie direkt geändert. Nötige
  Änderungen als Patch in `patches/` mit Eintrag in `patches/README.md`; nach
  jeder Neuauflösung der Abhängigkeiten erneut anwenden (aktuell
  `esp_hosted_sdio_reserve.patch`).
- **C6 / ESP-Hosted:** Host `espressif/esp_hosted` `3.0.*`
  (`main/idf_component.yml`), Slave-Firmware 3.0.7
  (`scripts/c6_firmware_via_UART/binaries_v3.0.7`). Beide Versionen müssen
  zusammenpassen (siehe Release-Workflow). Den C6 flasht nur der Mensch.
  Änderungen an der ESP-Hosted-Anbindung immer als „braucht Hardwaretest"
  melden.
- **`version.txt`** wird von Hand gepflegt und vom Release-Workflow gelesen;
  nicht ungefragt ändern.

## Grenzen der Testebenen

Der Host-Test (Linux-Target) ist **keine P4-Simulation**. Er deckt nicht ab:
Timing, Interrupts, Cache- und PSRAM-Verhalten, DMA, Stackgrößen,
Speicherausrichtung, echte Nebenläufigkeit. Der FreeRTOS-POSIX-Simulator läuft
single-core, auch mit SMP-Konfiguration.

Der Simulator (`sim/`) zeigt echtes LVGL-Rendering derselben UI-Quellen in
derselben Auflösung und Farbtiefe, aber gegen einen SDL-Softwarepfad. Er deckt
**nicht** ab: DSI-Timing, PPA-Rotation, Tear-Avoid-Modus, LVGL-Puffergrößen,
PSRAM-Durchsatz und das Farbverhalten des echten Panels. Layout, Typografie,
Farben, Zustandslogik der Screens und die Callback-Reihenfolge deckt er ab.

Es gibt keinen Emulator, der DSI, PPA, Touch, PSRAM-Timing oder ESP-Hosted
abbildet. Keinen vorschlagen oder einrichten.

Änderungen aus den Testleiter-Zeilen „Peripherie …" und „WLAN-Wiederherstellung"
gelten erst mit grünem `hw-flash.sh` bzw. `hil-outage-test.sh` als erledigt.
Ohne Board: „ungetestet auf Hardware" ausdrücklich in die Zusammenfassung.

## Reviews, Git und Sprache

- Reviews nur vorschlagen, erst auf Ansage starten: `lvgl-reviewer` nach einem
  fertigen Screen oder Treiber, `firmware-auditor` mit genau einer Fehlerklasse
  aus `experiments/AUDIT.md` pro Lauf, Skill `comments-cleanup` für gezielte
  Dateien. Berichte als `review/<thema>-<JJJJ-MM-TT>.md`.
- Auf Feature-Branches selbst committen, kein Push, kein Merge. Commits englisch,
  kurz, im Imperativ („Name HTTP timeouts"); Berichte an den Menschen deutsch.
