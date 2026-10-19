# Audit B — Heap und Speicher — 2026-10-19

Geprüft von `firmware-auditor` (Klasse B), Stand Branch `feat/screensaver` (`master...HEAD`,
18 Commits). Schwerpunkt: `weather_saver.c`, Schoner-Teile von `weather_ui.c`, `main.c`
(`stack_in_psram`), `app_heap_probe.c`, `app_prefs.c`, `lv_mem_psram.c`. Reine
Codeinspektion, nichts auf der Hardware ausgeführt, kein Code geändert.
Befunde: 6 (3 neu, 3 offen aus dem Bericht vom 2026-10-05).
Der BLOCKER wurde vom Hauptassistenten am Code nachvollzogen (Aufrufkette, IDF-Assert und
Cache-Pfad gelesen), aber noch nicht auf der Hardware nachgestellt.

Stand der Befunde vom 2026-10-05: Beide HOCH-Befunde bleiben erledigt
(`CONFIG_LV_USE_CUSTOM_MALLOC=y`, `cJSON_InitHooks`). Die drei MITTEL/NIEDRIG-Befunde
sind nicht umgesetzt und stehen unten mit aktuellen Zeilen.

## [BLOCKER] LVGL-Stack im PSRAM, aber der Testkanal-Schalter schreibt NVS aus dem LVGL-Task
Datei: `main/main.c:328-331` (`stack_in_psram = true`), `main/app_prefs.c:370-374`
(`app_prefs_save_ota_channel`). Aufrufkette: `main/weather_ui.c:2337-2341` →
`main/main.c:76` → `main/app_weather.c:1250-1252`.

Problem: Seit `4a1621d` liegt der Stack des LVGL-Tasks im PSRAM. Der Handler des
Schalters "Testkanal" läuft im LVGL-Task und ruft ohne Timer oder Queue
`storage_record_save()` auf (`nvs_get_blob`, `nvs_set_blob`, `nvs_commit`). Alle anderen
Einstellungen laufen über `save_debounce_fire` im esp_timer-Task. Der Kommentar in
`main.c:329-330` ("this task does no flash writes") und der Plan
`docs/plan-stack-und-ram-2026-10-15.md` stimmen für diesen einen Pfad nicht.

Warum (korrigiert nach Nachprüfung am IDF-Quelltext, siehe unten): Mit
`CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y` prüft IDF 6.1 in
`spi_flash_disable_interrupts_caches_and_other_cpu()` per
`assert(esp_task_stack_is_sane_cache_disabled())` (`spi_flash/cache_utils.c:112-115`), dass der
Stack nicht im PSRAM liegt. Dieser Pfad wird aber nur betreten, wenn der Cache für den Flash-Zugriff
wirklich abgeschaltet werden muss. Hier ist `CONFIG_SPIRAM_XIP_FROM_PSRAM=y`: Code und Rodata
laufen aus dem PSRAM, und `spi_flash_os_func_app.c:237-249` lässt den Cache bei einem Schreibzugriff
an, solange keine Flash-Seite gemappt ist. Folge:
- Normalfall: kein Abbruch, aber der NVS-Schreibzugriff (inklusive Erase bei Seitenwechsel)
  blockiert den LVGL-Task, also Rendering und Touch.
- Abbruch mit Assert und Neustart nur, wenn zur selben Zeit Flash-Seiten gemappt sind, zum Beispiel
  während `esp_ota_end()` das Image prüft. Der Schalter wird während eines Updates nicht deaktiviert.
Die erste Fassung dieses Befunds (Auditor) nannte den Abbruch "sicher bei jeder Bedienung"; das war
zu stark. Der Befund bleibt ein BLOCKER-Kandidat wegen der Regel "kein Flash-Zugriff aus einem Task
mit PSRAM-Stack", ist aber auf der Hardware noch nicht ausgelöst worden.

Vorschlag: Aus dem LVGL-Task keinen synchronen Flash- oder NVS-Zugriff.
`app_prefs_save_ota_channel` wie die übrigen Einstellungen persistieren, über einen eigenen
`save_debounce_fire`-Timer (esp_timer-Task, interner Stack) oder die Queue des weather-Tasks.
Kommentar in `main.c:329-330` korrigieren. Weitere synchrone Flash-Zugriffe aus
LVGL-Callbacks und `lv_timer`s wurden nicht gefunden (alle Callbacks in `main.c:65-150`
und die drei `lv_timer_create` in `main/` geprüft).

Nachweis:
- Der IDF-Assert ist einkompiliert. `check-boot-log.sh` schlägt über "abort() was called"
  fehl, aber nur, wenn der Pfad im Monitorfenster läuft.
- Zusätzlich in `main/storage_backend_nvs.c` am Anfang von read, write und commit
  `esp_task_stack_is_sane_cache_disabled()` prüfen. Bei false: `ESP_LOGE` mit
  `pcTaskGetName(NULL)` und dem Schlüssel, `STORAGE_ERR` zurückgeben statt in den Assert zu
  laufen.
- HIL: `MONITOR_SECONDS=60 ./scripts/hw-flash.sh /dev/ttyACM0`, im Fenster Einstellungen →
  Update → Testkanal umschalten, einmal auch während eines Test-Updates. Heute erwartet im
  Normalfall: kein Abbruch, aber eine `lvgl_stall`-Zeile beim Umschalten; während des
  OTA-Image-Checks möglicherweise Assert und Neustart. Nach dem Fix: weder Stall noch Neustart,
  der Schalter steht nach dem nächsten Boot, wie er gesetzt wurde.

## [MITTEL] `heap_watch` wertet `dma_min` nicht aus (offen aus 2026-10-05)
Datei: `components/app_logic/heap_watch.c:35-38`.
Problem: LOW und "neues Minimum" schauen nur auf die Momentwerte `dma_free` und
`dma_largest`. `dma_min_free` wird erhoben, geht aber in keine Entscheidung ein.
Warum: Der Tiefstwert kam der Schwelle nahe (`dma_min` 36408 in `hw-saver5.log`, 38228 in
`hw-saver.log`, Schwelle 32 KiB), nur als INFO-Zeile, während die Momentwerte (~130 KB)
gesund aussehen. Ein kurzer Einbruch unter 32 KiB zwischen zwei 5-s-Proben bliebe ohne
Warnung.
Vorschlag: Ein neuer Tiefstwert von `dma_min_free` unter `low_free` erzeugt einmalig LOW oder
eine eigene Stufe, mit eigenem gemeldeten Stand, damit er sich nicht wiederholt.
Nachweis: `host_test/main/test_heap_watch.c`: Init mit Defaults; t=0 {free 50000, largest
31744, min 32276} → REPORT; t=5000 gleiche Momentwerte, min 9152 → genau einmal LOW;
t=10000 gleiches min → SILENT; Gegenprobe min über 32 KiB → kein LOW.

## [MITTEL] `dma_largest` schrumpft nach OTA-Läufen, Ursache unbelegt (offen aus 2026-10-05)
Datei: `main/app_weather.c:1099` und `:1113`, `main/ota_update.c:294-343` (P4) und `:435-468`
(C6).
Problem: Der Guard fehlt weiter: `ota p4 begin/done` ist da, für C6 nur `begin`; kein
Vorher/Nachher-Vergleich mit `heap_caps_print_heap_info`. In den Logs dieses Branches lief
kein OTA, der Befund ist weder bestätigt noch widerlegt.
Vorschlag: Ursache eingrenzen. OTA-Task-Stacks bleiben intern (sie schreiben Flash, siehe
BLOCKER).
Nachweis: `log_now("ota c6 done")` ergänzen; im weather-Task nach `s_ota_active == false`
`log_now("after ota")`, bei kleinerem `dma_largest` einmal `heap_caps_print_heap_info`. HIL:
fünfmal "Check for update" im Zustand up-to-date, `dma_largest` je Lauf vergleichen.

## [NIEDRIG] PSRAM-first-Allokatoren fallen ohne Größengrenze auf internen (DMA-)RAM zurück
Datei: `main/lv_mem_psram.c:33-43`, `main/main.c:52-56` (cJSON).
Problem: Scheitert die PSRAM-Allokation, geht jede Größe an `MALLOC_CAP_INTERNAL | 8BIT`.
Vom Schoner passen intern noch Ziffern- und Strichbilder (je 83 104 B, `weather_saver.c:444`),
der Halo (54 756 B, `:388`) und das Gitter (16 072 B, `:158`). Mit diesen Caps ist auch der
64-KiB-DMA-Reservepool erreichbar (`CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536`), genau die
Reserve für die SDIO-Puffer.
Warum: Ein Ziffern-Fallback belegt mehr als die Hälfte der ~150 KB freien DMA-RAMs. Zwei
davon drücken unter 32 KiB, der WLAN-Link verliert Pakete, ohne dass die Ursache im Log steht.
Eintrittsbedingung (PSRAM erschöpft oder fragmentiert) ist heute nicht beobachtet (~22 MB frei).
Vorschlag: Fallback nur bis zu einer benannten Größengrenze zulassen (Wert legt der Mensch fest;
Bezugsgröße etwa `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`), darüber NULL zurückgeben. Jeden
Fallback zählen.
Nachweis: Zähler (Anzahl und Bytes) in `lv_malloc_core`, `lv_realloc_core`, `cjson_malloc`,
als `int_fallback=<n>/<bytes>` in der `heap:`-Zeile; erster Fallback über der Grenze als
`ESP_LOGE`. Erwartet nach Boot, Refresh und einem Schonerzyklus mit allen zehn Ziffern
(`SAVER_FAST_TEST=1`): 0.

## [NIEDRIG] Allokationsfehler im Schoner: alte Ziffer bleibt stehen, unvollständige Bilder gecacht
Datei: `main/weather_saver.c:784-797` (`set_digit`), `:482-493` (`fill_lit`), `:379-393`
(`decor_ensure`).
Problem: `set_digit` setzt `t->shown = d` vor der Allokation; liefert `image_render` NULL, bleibt
die vorige Ziffer stehen und wird erst beim nächsten Ziffernwechsel erneut versucht.
`fill_lit` überspringt eine fehlgeschlagene Glühschicht, die unvollständige Ziffer bleibt im
Cache. `decor_ensure` übernimmt `s_decor_style` vor der Allokation; ein fehlender Halo oder
Balkenschatten bleibt für diesen Stil dauerhaft weg.
Warum: Bei Speichermangel zeigt die Uhr eine falsche Zeit (Minuteneiner stehen), ohne
Logzeile. Speichersicherheit ist nicht betroffen: alle Verwendungen prüfen auf NULL.
Vorschlag: `shown` erst nach Erfolg setzen und bei Fehlschlag `t->lit` verstecken; Ziffer nur
cachen, wenn alle `LIT_LAYERS` gezeichnet sind; Deko-Stil erst nach vollständiger Allokation
übernehmen; Fehlschläge zählen.
Nachweis: Fehlerzähler in `weather_saver.c` ohne ESP-Abhängigkeit (damit `sim/` baut),
abfragbar über `weather_saver.h`; `lvgl_stall_probe_cb` gibt ihn bei Änderung als `ESP_LOGE`
aus; erwartet 0. `ESP_LOGE` fällt `check-boot-log.sh` nicht auf (FATAL_PATTERNS), Muster dort
ergänzen und `./scripts/test-check-boot-log.sh` laufen lassen.

## [NIEDRIG] Heap-Probe ohne PSRAM-Größtblock, ohne Untergrenze, gemischtes Messfenster "ui built"
Datei: `main/app_heap_probe.c:27-32`, `main/main.c:352` und `:402`.
Problem: Es fehlen `psram_largest` und eine Untergrenze mit Fehlerzeile. Der Schoner braucht
zusammenhängend 1 228 800 B (Hintergrund, `weather_saver.c:396`) und 495 936 B
(Balkenschatten, `:389`); `psram_free` allein zeigt keine Fragmentierung. Außerdem fällt in
`hw-ram.log` `dma_free` von `boot` bis `ui built` von 176543 auf 163063 und `dma_min` von
111812 auf 90316 (am 2026-10-05 waren es 76 B). Im selben Fenster startet die Kamera
(3230 ms) und der Schoner wird gebaut. Im Schonercode wurde keine interne Allokation gefunden,
seine Bilder liegen nachweislich im PSRAM (`ui built` -2,1 MB `psram_free`).
Vorschlag: `heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)` in die Probe; nach `ui built`
die `HEAP_WATCH_LOW_*` prüfen, sonst `ESP_LOGE`; Messpunkt `log_now("saver built")` direkt
nach `weather_ui_set_screensaver()` und vor `app_light_set_adaptive()`.
Nachweis: Feld `psram_largest=` in jeder `heap:`-Zeile; `ESP_LOGE` bei Unterschreitung (auch hier
Muster in `check-boot-log.sh` ergänzen); HIL-Boot: `saver built` bewegt nur `psram_free`
(~2,1 MB), `dma_free` höchstens um wenige hundert Byte. Der Rest der 13,5 KB taucht dann
zwischen "saver built" und "ui built" auf, also beim Kamerastart.

## Nicht beanstandet
`background_ensure`, `decor_ensure` (Freigabe vor Neuallokation), `digits_reset` (Puffer erst
nach Löschen des Overlays freigegeben), `image_render` (NULL-Prüfung, Canvas gelöscht, Stride
gleich w×4 wegen `CONFIG_LV_DRAW_BUF_STRIDE_ALIGN=1`), Strichpuffer erst nach
`lv_canvas_finish_layer` freigegeben, kein Bildcache (`CONFIG_LV_CACHE_DEF_SIZE=0`, bei
Aktivierung bräuchte jede Neuallokation ein `lv_image_cache_drop`), pro Sekunde keine
Heap-Allokation in `weather_saver_update` und `saver_render`, `clock_hold_timer` und
`saver_timer` sauber, Verlaufsdeskriptoren statisch, Schoner-Record im Timer-Task.
Dauerhafter PSRAM-Bedarf des Schoners: ~2,1 MB beim Boot (auch bei Einstellung "Aus"), plus
~0,83 MB Ziffern nach dem ersten Lauf.

Außerhalb der Klasse aufgefallen: `main/weather_ui.c:3420` zusammen mit `main/main.c:378`: bei
gespeichertem Stil "orange" wird der Schoner beim Boot zweimal komplett im LVGL-Task unter dem
Display-Lock gerechnet (Klasse G). `main/app_heap_probe.h:24-29`: Der Doku-Kommentar zu
`log_now` steht über `log_task_stacks_once`.
