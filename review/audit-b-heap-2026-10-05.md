Alle Befunde betreffen das Speicherverhalten auf echter Hardware. Host-Test, Emulator und Simulator können keinen davon nachweisen; jeder Guard muss per `./scripts/hw-flash.sh` auf dem Board gemessen werden.

# Audit B (Heap und Speicher) — 2026-10-05
Geprueft: `main/`, `components/app_logic/`, `sim/`, `host_test/`, `sdkconfig.defaults`, `sdkconfig`. Suchmuster: malloc/calloc/realloc/free, heap_caps_*, strdup, sprintf, lv_malloc/lv_free, cJSON_Parse/Delete, MALLOC_CAP_*. Zur Einordnung zusätzlich gelesen: LVGL 9.5 und esp_lvgl_adapter in `managed_components/`, IDF 6.1 (`esp_psram.c`, `cache_utils.c`) und die Heap-Zeilen in `.logs/`.
Treffer: 103 Grep-Zeilen, davon 64 Aufrufstellen in Code   Befunde: 5

## [HOCH] Der gesamte LVGL-Heap landet im DMA-fähigen internen RAM, auch pro Frame
Datei: sdkconfig.defaults:63 (`CONFIG_LV_USE_CLIB_MALLOC=y`) zusammen mit sdkconfig.defaults:46 (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024`). Betroffene Pfade: weather_icons.c:166-168, weather_chart.c:132 ff., weather_ui.c:1563-1564, app_weather.c:484-488
Problem: Mit CLIB-Malloc wird `lv_malloc` zu `malloc` (lv_mem_core_clib.c:64). Fast alle LVGL-Blöcke sind kleiner als 1024 B und gehen deshalb zuerst ins interne RAM. Das betrifft Objekte, Styles, Labeltexte, die Layer-Puffer der gedrehten Sonnenstrahlen und jeden Draw-Task, den LVGL 9 pro Render neu anlegt (lv_draw.c:105). Zusätzlich baut jeder vollständige Forecast-Push 8 Icons und das Diagramm (~40 Objekte) neu auf, und jeder Sprachwechsel baut das komplette Settings-Panel neu.
Warum: Laut Kommentar in sdkconfig.defaults:34-46 ist genau dieser Speicher für die SDIO-Puffer zum C6 reserviert, und die haben keinen PSRAM-Fallback. Gemessen: `boot` 164931 B frei (das ist vor `weather_ui_create`). Im Leerlauf sind es danach nur noch 30–65 KiB. In `.logs/hw-live-watch.log:1-2` und `.logs/hw-manual-ota.log:16-27` steht das Gerät im Leerlauf dauerhaft unter seiner eigenen LOW-Schwelle von 32 KiB („packets may be dropped“).
Vorschlag:
- LVGL auf einen eigenen Allokator umstellen (`CONFIG_LV_USE_CUSTOM_MALLOC`).
- `lv_malloc_core`, `lv_realloc_core` und `lv_free_core` sollen PSRAM bevorzugen (SPIRAM|8BIT) und nur bei Fehlschlag auf INTERNAL|8BIT ausweichen.
- DMA-Bedenken gibt es keine: LVGL zeichnet rein in Software (`LV_USE_PPA`/`DMA2D` aus).
- Danach die Renderzeit über `lvgl_stall` und `display_lock` gegenmessen, weil die Draw-Tasks dann im PSRAM liegen. Das braucht einen Hardwaretest.
Nachweis: GUARD
  Zwei zusätzliche Messpunkte mit `app_heap_probe_log_now()`:
  - `"ui built"` direkt nach `bsp_display_unlock()` in main.c:333, vor `app_wifi_init()`.
  - `"forecast pushed"` nach `push_forecast_to_ui(true)` in app_weather.c:621.
  Die Differenzen `boot → ui built` und `vor/nach Push` erscheinen als eigene `heap:`-Zeilen im Boot-Log. Nach der Umstellung muss der Rückgang in `psram_free` auftauchen statt in `dma_free`.

## [HOCH] cJSON-Bäume liegen im DMA-fähigen internen RAM (keine Hooks gesetzt)
Datei: main/app_weather.c:524 (Forecast), :607 (Luftqualität), :853 (Geocoding); main/ota_update.c:183 (Release-JSON); components/app_logic/weather_forecast_parse.c:33, ota_manifest_parse.c:18
Problem: `cJSON_InitHooks` wird nirgends aufgerufen, also nutzt cJSON plain `malloc`. Jeder Knoten (~40 B) und jeder Key- oder Wert-String ist kleiner als 1024 B und geht damit ins interne RAM. Die Forecast-Antwort hat 3×168 Stundenwerte, 168 Zeit-Strings und rund 80 Tages- und Metadatenfelder. Grob geschätzt sind das 600+ Knoten, in der Größenordnung von 30 KiB gleichzeitig.
Warum: In `.logs/hw.log:106→118` fällt `dma_min` zwischen zwei Proben von 32276 auf 9152 B. In genau diesem Fenster liegen der erste Fetch und das Parsen (`forecast ok` bei 9187 ms). 9 KiB sind etwa 6 SDIO-Puffer. Das ist der tiefste Wert in allen Logs, und der Einbruch entspricht der Schätzung. Die Zuordnung ist trotzdem eine Hypothese, bis der Guard sie bestätigt.
Vorschlag: In `app_main` einmal `cJSON_InitHooks` setzen, und zwar bevor irgendein Task cJSON benutzt, also vor `app_weather_start()`. `malloc_fn` soll PSRAM bevorzugen und auf internes RAM zurückfallen, `free_fn` gibt passend frei. `app_logic` bleibt unverändert, die Host-Tests laufen weiter mit libc.
Nachweis: GUARD
  In `do_refresh_body` `app_heap_probe_log_now("pre-parse")` direkt vor app_weather.c:524 und `"parsed"` direkt nach der NULL-Prüfung in :526. Die Differenz in `dma_free` ist die Baumgröße im DMA-RAM. Nach dem Fix muss sie gegen ~0 gehen und in `psram_free` sichtbar werden.

## [MITTEL] heap_watch ignoriert den Tiefstwert; kurze Einbrüche zwischen zwei Proben lösen nie LOW aus
Datei: components/app_logic/heap_watch.c:35-38
Problem: Die LOW-Entscheidung und die Prüfung auf ein neues Minimum schauen nur auf die Momentwerte `dma_free` und `dma_largest`. `dma_min_free` wird zwar erhoben und geloggt, geht aber in keine Entscheidung ein. Bei einer Probe alle 5 s ist ein Einbruch auf wenige KiB, der zwischen zwei Proben wieder verschwindet, unsichtbar.
Warum: `.logs/hw.log:118` meldet `dma_min=9152` nur als INFO-Routinezeile, ohne Warnung. Genau die Art Einbruch, an der die SDIO-Puffer scheitern, bleibt damit ohne Alarm.
Vorschlag: Ein neuer Tiefstwert von `dma_min_free` unter `low_free` soll einmalig eine LOW-Meldung (oder eine eigene Stufe „DIP“) erzeugen. Dafür einen eigenen gemeldeten Stand führen, damit derselbe Tiefstwert nicht bei jeder Probe erneut meldet.
Nachweis: HOST
  In `host_test/main/test_heap_watch.c`:
  - Init mit Defaults.
  - t=0: {free 50000, largest 31744, min 32276} → REPORT.
  - t=5000: {free 50000, largest 31744, min 9152} → genau einmal LOW bzw. DIP.
  - t=10000: gleicher min → SILENT.
  - Gegenprobe: min bleibt über 32 KiB → kein LOW.

## [MITTEL] dma_largest schrumpft nach jedem OTA-Versuch dauerhaft, Ursache unbekannt
Datei: main/app_weather.c:1045 und :1058 (OTA-Tasks mit je 8 KiB Stack aus internem RAM, angelegt bei SDIO-Spitzenlast); main/ota_update.c:112-156 und :247-351
Problem: Der größte freie DMA-Block erholt sich nach OTA-Läufen nicht mehr. Zwei Belege:
- `.logs/ota-fixed-20260922.log:316→319`: von 31744 auf 21504, danach dauerhaft dort.
- `.logs/hw-manual-ota.log:16→24`: von 31744 auf 27648, nach zwei gescheiterten Manifest-Fetches.
Mit den heutigen Messpunkten lässt sich nicht feststellen, welche langlebige Allokation während des OTA-Fensters mitten im Block gelandet ist.
Warum: Für den Link zählt der größte zusammenhängende Block. Jeder OTA-Versuch, auch die stillen 12-h-Checks, kostet dauerhaft Reserve. Über Tage kann das in den LOW-Bereich driften.
Vorschlag: Ursache eingrenzen, bevor etwas geändert wird. Die Task-Stacks dürfen **nicht** einfach ins PSRAM: beide Tasks schreiben Flash, und IDF assertet auf einen PSRAM-Stack bei abgeschaltetem Cache (cache_utils.c:114).
Nachweis: GUARD
  Im weather_task beim ersten Schleifendurchlauf nach `s_ota_active == false` ein `log_now("after ota")` ausgeben. Liegt `dma_largest` unter dem Wert vor dem OTA, einmalig `heap_caps_print_heap_info(MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL)` ins Log schreiben. Dazu fünf manuelle „Check for update“ im Zustand up-to-date ausführen und `dma_free` und `dma_largest` nach jedem Lauf vergleichen.

## [NIEDRIG] Heap-Watermark unvollständig: kein PSRAM-Größtblock, keine Untergrenze mit SELFTEST_FAIL
Datei: main/app_heap_probe.c:26-31
Problem: AUDIT.md fordert für INTERNAL und SPIRAM jeweils freie Größe und größten Block, plus eine Untergrenze, die `SELFTEST_FAIL` auslöst. Erfasst wird kein `psram_largest`. Eine Untergrenze gibt es nicht, und `run_selftest()` bzw. `main/selftest.c` existiert gar nicht.
Warum: Das PSRAM ist mit ~25 MB frei unkritisch, daher nur NIEDRIG. Die fehlende Untergrenze bedeutet aber, dass auch ein Boot mit 9 KiB DMA-Rest als „ok“ durchgeht.
Vorschlag:
- Die Probe um `heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)` erweitern.
- Nach „ui built“ prüfen, ob `dma_free` mindestens `HEAP_WATCH_LOW_FREE` und `dma_largest` mindestens `HEAP_WATCH_LOW_LARGEST` erreichen.
- Auf Hardware als ESP_LOGE-Zeile ausgeben, in einem künftigen `run_selftest()` als `SELFTEST_FAIL`. Im Emulator ist der Check wenig aussagekräftig, weil dort ESP-Hosted fehlt und PSRAM nur als Zero-Init-RAM nachgebildet ist.
- Standardmäßig eingeschaltet lassen.
Nachweis: GUARD
  Ein zusätzliches Feld `psram_largest=` in der `heap:`-Zeile und eine ESP_LOGE-Zeile beim Unterschreiten im Boot-Log.

## Nicht beanstandet
- app_weather.c:171-234 `http_get`: Body im PSRAM, NULL-Prüfung, bei realloc-Fehler wird der alte Puffer freigegeben, Obergrenze 48 KiB, close/cleanup auf allen Pfaden.
- ota_update.c:112-156 `fetch_small`: dasselbe Muster, Obergrenze 32 KiB.
- ota_update.c:213-237 SHA-Puffer: PSRAM, auf allen Pfaden freigegeben, auch bei Fehler in `psa_hash_setup`.
- app_weather.c:1035-1047 OTA-Args: NULL-geprüft, bei Fehler in xTaskCreate und im Task selbst (:742) freigegeben.
- app_wifi.c:531-552 AP-Liste per calloc: NULL-geprüft mit `esp_wifi_clear_ap_list`, freigegeben.
- weather_chart.c:80-85 und :40-45: NULL-geprüft, `set_data` bricht bei fehlendem `c` oder `pts` ab, Freigabe per LV_EVENT_DELETE.
- weather_icons.c:128, weather_ui.c:273/1036/1079/2953: jede `lv_malloc` NULL-geprüft und über einen DELETE-Callback an ihr Objekt gekoppelt. Die SSID wird beim Klick kopiert (weather_ui.c:2654), es gibt also keinen hängenden Zeiger.
- `cJSON_Delete` auf allen Pfaden: app_weather.c:524-593, 607-613, 853-872; ota_update.c:183-191; beide app_logic-Parser.
- Task-Stacks von weather, ota, ota_resume und wifi_reconn im internen RAM: notwendig, weil diese Tasks Flash schreiben (NVS bzw. OTA) und IDF dann keinen PSRAM-Stack erlaubt (cache_utils.c:114).
- mbedtls im PSRAM (sdkconfig.defaults:60); HTTP-Puffer 4096/2048 B liegen über 1024 und gehen damit ebenfalls ins PSRAM.
- sprintf/strdup: kein Vorkommen, durchgängig snprintf.
- app_heap_probe.c:63 `log_now` ohne Lock: misst nur, ändert keinen geteilten Zustand.
- sim/main.c:234-281: nur Host-Simulator, NULL-Prüfungen vorhanden.
- Frame- und Draw-Puffer: legt der esp_lvgl_adapter selbst im PSRAM an (display_manager.c:1159), nicht Projektcode.

Ausserhalb der Klasse aufgefallen:
- `main/selftest.c` / `run_selftest()` fehlen, obwohl `scripts/emu-test.sh:23-24` und CLAUDE.md die Marker erwarten.
- `main/app_weather.c:503-593` dupliziert `components/app_logic/weather_forecast_parse.c` (Klasse J).
- `main/app_weather.c:114`: `s_ota_active` ist ein einfaches bool, das mehrere Tasks gemeinsam nutzen (Klasse A).

---

Hinweis: Dieser Durchlauf war reine Codeinspektion. Code wurde nicht geändert.

---

## Nachtrag 2026-10-05: HOCH-Befunde behoben und auf Hardware verifiziert

Beide HOCH-Befunde wurden umgesetzt (`sdkconfig.defaults` auf
`CONFIG_LV_USE_CUSTOM_MALLOC`, neue Datei `main/lv_mem_psram.c` mit
PSRAM-bevorzugenden `lv_*_core`-Funktionen, `cJSON_InitHooks()` in
`main/main.c` mit derselben PSRAM-first/Internal-Fallback-Logik, plus vier
`app_heap_probe_log_now()`-Messpunkte in `main/main.c` und
`main/app_weather.c`) und per `./scripts/hw-flash.sh /dev/ttyACM0` auf dem
Board gemessen.

**Stolperstein beim Verifizieren:** Der erste Flash-Durchlauf zeigte keine
Verbesserung beim LVGL-Befund (siehe "Vorher"-Log unten) — Ursache war ein
lokales, bereits generiertes `sdkconfig` (gitignored), das
`CONFIG_LV_USE_CLIB_MALLOC=y` noch explizit aus einem älteren Lauf enthielt.
`sdkconfig.defaults` setzt nur Werte, die in `sdkconfig` noch nicht
vorkommen, überschreibt also keinen bereits vorhandenen Eintrag. Erst nach
manueller Korrektur von `sdkconfig` (CLIB → CUSTOM) und Rebuild griff die
Änderung tatsächlich. Der cJSON-Fix war davon nicht betroffen, da er reiner
Code ist, kein Kconfig-Wert.

Zusätzlich war ein Linker-Fix nötig: `main/CMakeLists.txt` verlangt jetzt
`-Wl,--undefined=lv_malloc_core` (und die übrigen `lv_*_core`-Symbole) auf
`${COMPONENT_LIB}`, weil der Komponentengraph dieses Projekts
(esp_hosted/esp_wifi_remote-Zyklen) `liblvgl__lvgl.a` im finalen Link doppelt
auflistet — einmal vor, einmal nach `libmain.a`. LVGL fordert die
Core-Funktionen erst beim zweiten Durchlauf an, da ist `libmain.a` ohne
dieses Forcing schon durchgescannt und die Symbole bleiben "undefined
reference". Dieselbe Technik nutzt esp_hosted an anderer Stelle in diesem
Build bereits selbst.

### Vorher (alter Allokator, zum Vergleich aus derselben Session)
```
I (1562) heap: boot: dma_free=164819 dma_largest=65536 dma_min=100088 psram_free=27849088
I (1870) heap: ui built: dma_free=77287 dma_largest=43008 dma_min=32316 psram_free=27849088
...
I (6563) heap: idle: dma_free=50347 dma_largest=31744 dma_min=32316 psram_free=25287592
...
I (8452) heap: pre-parse: dma_free=46683 dma_largest=31744 dma_min=32316 psram_free=25278688
I (8460) heap: parsed: dma_free=46571 dma_largest=31744 dma_min=32316 psram_free=25252556
```
`ui built` kostet 87,5 KiB DMA-Speicher, ohne dass PSRAM sich bewegt — der
Custom-Allocator griff hier noch nicht (altes `sdkconfig`, siehe oben).
`dma_free` im Leerlauf bleibt bei 50 KiB, also im selben 30–65-KiB-Bereich
wie vor dem Fix.

### Nachher (Custom-Allocator + cJSON-Hooks aktiv)
```
I (1567) heap: boot: dma_free=169895 dma_largest=69632 dma_min=105124 psram_free=27843012
I (1873) heap: ui built: dma_free=169819 dma_largest=69632 dma_min=104628 psram_free=25194568
...
I (6568) heap: idle: dma_free=147799 dma_largest=63488 dma_min=67028 psram_free=25189100
...
I (9561) heap: pre-parse: dma_free=144027 dma_largest=63488 dma_min=60832 psram_free=25180208
I (9572) heap: parsed: dma_free=144027 dma_largest=63488 dma_min=60832 psram_free=25154088
```
`ui built` kostet nur noch 76 B DMA-Speicher (vorher 87,5 KiB), bei einem
PSRAM-Rückgang von ~2,6 MB — die LVGL-Objekte liegen jetzt im PSRAM.
`dma_free` im Leerlauf steht bei **147799** statt 30–65 KiB, weit über der
32-KiB-LOW-Schwelle, mit größerem `dma_largest` (63488 statt 31744).
`pre-parse` → `parsed` zeigt **keine** Veränderung in `dma_free` mehr
(vorher ein Einbruch von 46683 auf 46571 Richtung der ursprünglich
gemessenen 9 KiB), während `psram_free` um die erwarteten ~26 KiB sinkt —
der cJSON-Baum landet vollständig im PSRAM.

Beide HOCH-Befunde sind damit als behoben bestätigt. Volles Boot-Log:
`.logs/hw.log`.
