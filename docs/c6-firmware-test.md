# C6-Firmware-Test

Ziel: Bevor ein Release eine neue ESP32-C6-Firmware (esp-hosted-Slave) ausliefert,
läuft genau diese Firmware zusammen mit dem P4-Image auf dem Board.

## Wie Kanal und C6-Version zusammenhängen

| Workflow `channel` | C6-Firmware, die gebaut wird |
|---|---|
| `test` | das **neueste** `v3.0.*`-Tag von `espressif/esp-hosted-mcu` |
| `release` | genau das Tag aus `c6_release_version.txt`, die zuletzt **getestete** Version |

Der Release-Lauf bricht ab, wenn das Tag nicht existiert, nicht zur Einschränkung
in `main/idf_component.yml` passt, oder die gebaute Version von der Datei abweicht.
Ein neues Espressif-Tag gelangt also nie ungetestet in ein Release.

## Ablauf

Voraussetzungen: Board per USB am Rechner, Board läuft eine normale Release-Firmware,
WLAN ist eingerichtet. Actions-Läufe startet der Mensch (oder Claude nach Rückfrage).

1. **Testbuild erzeugen.** Workflow `Manual Dual-Chip Release Build` mit
   `channel=test` starten. Im Log des Schritts „Resolve C6 esp_hosted Version“
   steht die gebaute C6-Version, sie steht auch im Release unter `manifest.json`
   (`c6_version`). Beispiel: `3.0.9`.
2. **Auf dem Board updaten.** Einstellungen → Netzwerk → Update: „Testkanal“
   **und** „WLAN-Koprozessor mit Anwendung aktualisieren“ einschalten,
   „Nach Update suchen“. Ablauf: P4-Download und -Flash, Neustart, C6-Update,
   noch ein Neustart. Das dauert einige Minuten, das Board nicht trennen.
3. **Boot prüfen.**
   ```sh
   ./scripts/c6-compat-test.sh <c6_version> /dev/ttyACM0 90
   ```
   Das Skript setzt das Board per USB zurück (ohne zu flashen), liest 90 s Boot-Log
   nach `.logs/c6-test.log` und wertet ihn aus. PASS heißt:
   - kein Panic, kein Reboot, `>>> BOOT_OK <<<`, `got ip`
   - `esp-hosted fw versions` meldet die erwartete C6-Version, Host und C6 sind
     kompatibel (`(match)` oder `patch version differs (compatible)`),
     kein `major version mismatch`
   - `clock set` (DNS und SNTP über den C6) und `forecast ok` (HTTPS über den C6)
4. **WLAN-Wiederherstellung.** `./scripts/hil-outage-test.sh /dev/ttyACM0`, der
   Mensch löst den Ausfall aus (siehe Kopf des Skripts). Erwartung: `HIL_OUTAGE_OK`.
5. **Zweites Update unter Last.** Das P4-Update läuft über SDIO; der RPC/SDIO-Hänger
   zum C6 trat bisher unter hoher Last auf. Mit der neuen C6-Firmware auf dem Board
   noch einmal „Nach Update suchen“ ausführen (nach dem nächsten Testbuild oder
   mit demselben, der Dialog meldet dann „Aktuell“) und prüfen, dass Anzeige und
   Touch währenddessen reagieren. Danach Schritt 3 wiederholen.
6. **Beobachten.** Das Board 15 Minuten laufen lassen (ein Auto-Refresh-Zyklus).
   `./scripts/c6-compat-test.sh <c6_version> /dev/ttyACM0 900` muss PASS liefern.

## Bestanden: C6-Version für Releases freigeben

Alle Schritte grün → die getestete Version in `c6_release_version.txt` eintragen
(nur die Nummer, z. B. `3.0.9`), committen. Danach baut `channel=release` genau
diese C6-Firmware. Nicht bestanden → die Datei bleibt unverändert, Releases
behalten die letzte getestete Version. Hängt das Board nach einem fehlgeschlagenen
C6-Update, flasht der Mensch den C6 über UART mit
`scripts/c6_firmware_via_UART/flash_c6_via_uart.sh` zurück
(`binaries_v3.0.7` liegt im Repo).

## Grenze des Tests

`c6_release_version.txt` legt nur die **C6**-Seite fest. Die Host-Seite
(`espressif/esp_hosted` im P4-Image) löst die CI zum Zeitpunkt des Builds
auf das neueste `3.0.*` auf, weil `dependencies.lock` nicht im Repo liegt. Das
Skript gibt `Host x.y.z, C6 x.y.z` aus; notiere beide Werte. Ein Release kann
einen neueren Host enthalten als der, mit dem der C6 getestet wurde.
