# PLAN_MADCTL.md — MADCTL-Test und Compileroptimierung

Zwei kleine, unabhaengige Experimente. **M1 zuerst**, weil sein Ergebnis
darueber entscheidet, ob die Experimente E6 bis E8 aus `PLAN.md` ueberhaupt
noch gebraucht werden.

Hardware: ESP32-P4-Function-EV-Board, EK79007 (1024×600), ESP-IDF v6.0.2.

---

## Rollenverteilung und Regeln

- Claude Code aendert Code und baut. **Es flasht nicht.** Es nennt dir den
  Flash-Befehl, du flasht und gibst das Log oder deine Beobachtung zurueck.
- Ein Experiment pro Session, dazwischen `/clear`.
- Ein eigener git-Branch je Experiment, ausgehend vom aktuellen Stand.
- Ergebnis nach `experiments/RESULTS.md`, auch wenn es negativ ist.
- Nichts als Erfolg melden, was nicht beobachtet wurde. Bei M1 ist das
  Erfolgskriterium ein Bild, das auf dem Kopf steht — das kann nur der Mensch
  bestaetigen.

---

## M1 — MADCTL 0x36: dreht das Panel?

### Hintergrund fuer den Agenten

Die Espressif-Portierungsanleitung fuer MIPI-DSI sagt, dass `0x36` (MADCTL)
und `0x3A` (COLMOD) **nicht** in die Vendor-Init-Arrays gehoeren, weil die
Treiberkomponente sie selbst verwaltet. Im EK79007-Beispiel steht bei
`rgb_ele_order` der Kommentar „Implemented by LCD command 36h".

Der Treiber schreibt `0x36` also bereits an dieses Panel. Getestet wird, ob das
Panel auch die Spiegelbits auswertet.

Bitbelegung von MADCTL:

| Bit | Wert | Bedeutung |
|-----|------|-----------|
| MY  | 0x80 | Zeilenreihenfolge invertieren |
| MX  | 0x40 | Spaltenreihenfolge invertieren |
| MV  | 0x20 | Achsen tauschen (fuer 180° nicht noetig) |
| RGB | 0x08 | 0 = RGB, 1 = BGR |

**180° = MY | MX = 0xC0.**

### Prompt 1 — Bestandsaufnahme, noch nichts aendern

> Branch `exp/m1-madctl`. Finde und zeige mir:
> 1. wo `esp_lcd_new_panel_io_dbi()` aufgerufen wird und wie das
>    `esp_lcd_panel_io_handle_t` heisst
> 2. den Wert von `rgb_ele_order` in der `esp_lcd_panel_dev_config_t`
> 3. ob im Vendor-Init-Array des EK79007-Treibers irgendwo `0x36` gesendet
>    wird, und mit welchem Parameter
> 4. die genaue Stelle zwischen `esp_lcd_panel_init()` und
>    `esp_lcd_panel_disp_on_off()`
>
> Aendere noch nichts.

Punkt 2 ist wichtig: steht dort BGR, muss Bit 3 (`0x08`) erhalten bleiben,
sonst kippen die Farben und das Ergebnis ist nicht mehr interpretierbar.

### Prompt 2 — Einbau

> Baue einen Kconfig-Schalter `APP_MADCTL_OVERRIDE` (bool, default n) und
> `APP_MADCTL_VALUE` (hex, default 0xC0) in `main/Kconfig.projbuild`.
>
> Wenn aktiviert: nach `esp_lcd_panel_init()` und vor
> `esp_lcd_panel_disp_on_off()` genau einen Befehl senden:
>
> ```c
> uint8_t madctl = CONFIG_APP_MADCTL_VALUE | MADCTL_RGB_BIT;
> esp_err_t err = esp_lcd_panel_io_tx_param(<io_handle>, 0x36, &madctl, 1);
> ESP_LOGI(TAG, "MADCTL write 0x%02X -> %s", madctl, esp_err_to_name(err));
> ```
>
> `MADCTL_RGB_BIT` setzt du auf `0x08` oder `0x00`, passend zu dem
> `rgb_ele_order`, das du in Prompt 1 gefunden hast. Begruende die Wahl.
>
> Der Rueckgabewert muss geloggt werden. Ein Fehler beim Senden ist ein
> anderes Ergebnis als ein erfolgreich gesendeter Befehl ohne Wirkung, und
> die beiden duerfen nicht verwechselt werden.
>
> Danach bauen und mir den Flash-Befehl nennen.

### Testmatrix

Jeweils `APP_MADCTL_VALUE` setzen, neu bauen, flashen, **Kaltstart**,
Bild ansehen:

| Wert | Erwartung wenn das Panel MADCTL auswertet |
|------|-------------------------------------------|
| 0x00 | unveraendert (Kontrolle) |
| 0x40 | horizontal gespiegelt |
| 0x80 | vertikal gespiegelt |
| 0xC0 | um 180° gedreht — **das Ziel** |

Die Zwischenwerte 0x40 und 0x80 sind nicht optional. Wenn nur einer von beiden
wirkt, ist das eine wichtige Information: dann hast du eine Achse, aber keine
180°-Drehung, und musst die zweite anders loesen.

### Prompt 3 — Auswertung

> Ich habe folgendes beobachtet: <Beobachtung je Wert>. Trage das in
> `experiments/RESULTS.md` ein. Interpretiere nicht ueber das hinaus, was ich
> beobachtet habe.

### M1b — Rueckfallposition, falls 0xC0 nichts bewirkt

Manche Panels uebernehmen MADCTL nur vor dem Verlassen des Sleep-Modus.

> Verschiebe den MADCTL-Schreibvorgang: sende ihn direkt vor `Sleep Out`
> (`0x11`) statt nach `esp_lcd_panel_init()`. Wenn das bedeutet, in die
> Vendor-Init-Sequenz des Treibers eingreifen zu muessen, kopiere den Treiber
> nach `components/` statt `managed_components/` zu veraendern, und sag mir,
> dass du das getan hast.

Wenn auch das nichts bewirkt, ist M1 negativ. Dann gelten E6 bis E8 aus
`PLAN.md` weiter, und die Suche nach Scan-Direction-Bits im
EK79007AD-Datenblatt (E7) bleibt der naechste Schritt.

### Wenn M1 erfolgreich ist

> Stelle die Anzeige auf `ESP_LV_ADAPTER_ROTATE_0` und
> `ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE` um. Ermittle `num_fbs` ueber
> `esp_lv_adapter_get_required_frame_buffer_count()` und logge den Wert.
> Setze `APP_MADCTL_OVERRIDE` als Default auf `y`.
>
> Spiegle das Touch-Mapping im Read-Callback:
> ```c
> x = (hor_res - 1) - x;
> y = (ver_res - 1) - y;
> ```
> Pruefe dabei, ob das Mapping nicht schon an anderer Stelle gedreht wird —
> zweimal gespiegelt ist wieder unveraendert.

Erst danach die Kalibrierung des Touch pruefen: alle vier Ecken antippen und
gegen die erwarteten Koordinaten halten.

---

## M2 — Compileroptimierung

### Aenderung

```
CONFIG_COMPILER_OPTIMIZATION_PERF=y
```

Genau diese eine Option, nichts sonst. Die uebrigen Punkte
(`SPIRAM_XIP_FROM_PSRAM`, `CACHE_L2_*`, `LV_MEMCPY_MEMSET_STD`,
`LV_MEM_CUSTOM`, `SPIRAM_SPEED=200M`) gehoeren nach E1 in `PLAN.md`.

### Prompt

> Branch `exp/m2-optperf`. Sag mir zuerst, welche
> `CONFIG_COMPILER_OPTIMIZATION_*`-Option aktuell gesetzt ist. Miss die
> Groesse der Anwendungsbinary vor der Aenderung
> (`idf.py size` oder `ls -l build/*.bin`).
>
> Setze dann `CONFIG_COMPILER_OPTIMIZATION_PERF=y` in `sdkconfig.defaults`,
> baue neu und melde:
> - die neue Binary-Groesse und die Differenz in Prozent
> - ob die App-Partition noch ausreicht, mit dem verbleibenden Spielraum
> - alle neuen Compiler-Warnungen gegenueber dem vorherigen Build

### Worauf zu achten ist

`-O2` inlined und rollt Schleifen aus, die Binary wird groesser. Wenn deine
App-Partition knapp bemessen ist, faellt das hier auf, und zwar erst beim
Flashen. Deshalb die Groesse vorher pruefen.

Zweitens verschiebt sich das Timing. Code, der bisher nur durch Glueck
funktioniert hat — fehlendes `volatile`, ungeschuetzter Zugriff aus zwei Tasks,
zu knapper Stack — kann bei `-O2` anders scheitern. Wenn nach dieser Aenderung
neue, scheinbar unzusammenhaengende Fehler auftreten, ist das kein Zufall,
sondern ein vorher verdeckter Fehler. Nicht zurueckdrehen, sondern suchen.

Drittens: neue Compiler-Warnungen ernst nehmen. `-O2` aktiviert Analysen, die
bei niedrigerer Optimierungsstufe nicht laufen.

### Erfolgskriterium

`render_p50` und `render_p95` aus der PERF-Zeile sinken, Binary passt in die
Partition, keine neuen Fehler nach Kaltstart und fuenf Minuten Laufzeit.

---

## Reihenfolge

1. **M1** bis zur Entscheidung. Fuenf Minuten Einbau, vier Flash-Durchgaenge.
2. Bei Erfolg: Umstellung auf `ROTATE_0` + `NONE`, Touch spiegeln, neuen
   Baseline-Commit setzen.
3. **M2** auf dem daraus entstandenen Stand, damit die Messung gegen die
   richtige Baseline laeuft.
4. Danach zurueck zu `PLAN.md`, E1 mit den ergaenzten Flags.

M1 und M2 sind unabhaengig, aber M1 kann die Konfiguration so stark aendern,
dass eine vor M1 gemessene Baseline fuer M2 wertlos waere.

---

## Ergebnistabelle

| Exp | Aenderung | Beobachtung / Messwert | Bleibt? |
|-----|-----------|------------------------|---------|
| M1 0x00 | Kontrolle | | — |
| M1 0x40 | MX | | — |
| M1 0x80 | MY | | — |
| M1 0xC0 | MY\|MX | | |
| M1b | MADCTL vor Sleep Out | | |
| M2 | `-O2` | Binary +__%, render_p50 __, render_p95 __ | |
