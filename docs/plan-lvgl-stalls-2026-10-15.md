# Plan: `lvgl_stall`-Meldungen verringern

Stand 2026-10-15, Branch `feat/screensaver`. Noch nichts davon umgesetzt.

Hintergrund: Die frühen Ursachen (Cross-Chip-RPC unter dem Display-Lock,
unnötige Chart- und Icon-Neubauten) sind seit 2026-09-17 behoben, siehe
`review/audit-g-lvgl-blocking-2026-09-21.md`. Übrig sind Zeichenkosten.

Ein "Stall" heißt hier: Zwischen zwei Läufen des Prüf-Timers (`lvgl_stall_probe_cb`
in `main/main.c`, Periode 20 ms) lagen mehr als 60 ms. Das ist meist ein Bild,
das lange zum Zeichnen braucht, kein hängender Task.

## Bestandsaufnahme (Dauerlauf ohne Berührung, `.logs/hw-ram.log`, plus frühere Läufe)

| Stall | Dauer | Auslöser |
|---|---|---|
| Prognose-Push beim Boot (Lock gehalten 99 ms) | 132 + 73 ms | `push_forecast_to_ui/full` |
| Schoner startet | 505 ms | Oberfläche ausblenden, Overlay aufbauen, Vollbild-Hintergrund |
| erstes Anzeigen jeder Ziffer | 109 bis 145 ms | Zifferbild wird gezeichnet |
| **jede volle Minute im Schoner** | **150 ms** (bei 48,7 / 108,7 / 168,7 s) | Drift verschiebt den ganzen Uhrblock |
| Rückkehr aus dem Schoner (früher gemessen) | 376 bis 466 ms | volle Neuzeichnung der Oberfläche |
| Einstellungen öffnen (früher gemessen) | 100 bis 470 ms | Dialog mit Halbtransparenz über dem ganzen Bild |

## Vorschläge, nach Nutzen

### 1. Statische Schicht des Schoners als ein Bild
Löst die Minutenhänger und den Start.

- Alles außer Ziffern und Datum (Glas, Käfig, Gitter, Balken, Schatten,
  Doppelpunkte) wird einmal pro Stil zu einem Bild gerendert (`lv_snapshot`).
  Zeichnen heißt dann: ein Bild verschieben plus sechs Ziffernbilder. Die
  Zeichenaufgaben sinken von etwa 80 auf etwa 10.
- Kosten: rund 1,7 MB PSRAM und Rechenzeit beim Stilwechsel.
- Erwartung (ungemessen): Minutenhänger von 150 ms auf unter 50 ms.
- Prüfung: `lvgl_stall`-Zeilen im Schoner über mehrere Minuten (Hardware, ohne
  Bedienung, mit `SAVER_FAST_TEST=1`), Speicher in `heap:`-Zeilen, Optik im
  Simulator.

### 2. Hintergrundarbeit nicht in Berührungen legen
Wirkung für den Nutzer.

- Der Prognose-Push (und Refreshes) wartet, solange das Display gerade berührt
  wurde (`lv_display_get_inactive_time() < ~1 s`), höchstens ein paar Sekunden.
  Die Regel kommt als kleine Policy nach `app_logic`, mit Host-Test.
- Den Push in zwei bis drei Schritte teilen (aktuelle Werte, Tage, Diagramm),
  jeder unter eigener, kurzer Lock-Haltezeit.
- Das nimmt dem Stall seine Wirkung, auch wenn die Gesamtkosten gleich bleiben.

### 3. Erst messen, was ein Bild wirklich kostet
Ohne Risiko, vor allem Größeren.

- Kleine Messung mit den LVGL-Anzeige-Ereignissen: Renderzeit, Flush-Zeit und
  Fläche pro Bild, ausgegeben neben jeder `lvgl_stall`-Zeile.
- Frage, die damit beantwortet wird: Sind die rund 400 ms der Rückkehr
  Rendern oder Warten auf das Display? Der Verdacht: Die Oberfläche wird in
  12 Streifen zu je 50 Zeilen gezeichnet (`buffer_height=50`), und jeder
  Streifen wartet im Tear-Avoid-Modus auf den Display-Takt. Der frühere
  Versuch mit 100 Zeilen (E3b, `experiments/`) blieb ohne Ergebnis, weil damals
  TLS-Fehler das Fenster verfälschten.

### 4. PPA nicht blockierend
Größerer Eingriff, nur falls 3 es belegt.

- Aus dem offenen Eintrag `ppa-rotate180-blocking-todo`: Die Drehung um 180°
  wartet blockierend auf die Hardware (`PPA_TRANS_MODE_BLOCKING` in
  `esp_lvgl_adapter`).
- Umbau betrifft eine verwaltete Komponente (Patch unter `patches/` mit Eintrag
  in `patches/README.md`) und braucht eine saubere Abstimmung mit dem
  Frame-Puffer, sonst kann ein halb gedrehtes Bild angezeigt werden.

## Nicht empfohlen

- Den Schwellwert von 60 ms anheben: versteckt die Meldungen, ändert nichts.
- Den Schoner wieder mit vorab gezeichneten Ziffern: wurde abgelehnt.

## Reihenfolge

3 und 1 zuerst, dann 2; 4 nur bei Befund aus 3. Alle Schritte sind auf der
Hardware prüfbar, ohne dass jemand bedienen muss, bis auf einen Tipp-Test am Ende.
