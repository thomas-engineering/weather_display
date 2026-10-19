# LVGL-Review Bildschirmschoner — 2026-10-19

Geprüft von `lvgl-reviewer` (Opus, hoher Aufwand): `git diff master...HEAD` auf `feat/screensaver`,
beschränkt auf `weather_saver.*`, Schoner-Teile von `weather_ui.*`, Schoner-Record in `app_prefs.*`,
`main.c` (Stack, Probe), `app_weather.c` (`app_weather_get_local_time`) und
`components/app_logic/` (`screensaver_policy`, `hold_gesture`) samt Host-Tests.
Herangezogen: `.logs/hw-ram.log`, Disassembly von `weather_saver.c.obj`, ESP-IDF 6.1 und LVGL 9.5.
`./scripts/host-test.sh` lief grün (106 Tests). Kein Code geändert, nichts auf der Hardware ausgeführt.
Ein HOCH-Befund, zwei MITTEL, vier NIEDRIG. Speicherlecks oder Use-after-free im Lebenszyklus der
Bilder wurden nicht gefunden.

## [HOCH] LVGL-Stack im PSRAM, aber der LVGL-Task schreibt NVS
Datei: `main/main.c:329-331`; ausgelöst über `main/weather_ui.c:2337-2341` → `main/main.c:76` →
`main/app_weather.c:1250-1251` → `main/app_prefs.c:370-373`.
Problem: Der Kommentar an `stack_in_psram = true` sagt "no flash writes". Der Testkanal-Schalter im
Update-Dialog ruft aber im LVGL-Event `app_prefs_save_ota_channel()` auf, und das schreibt synchron
`storage_record_save()` (NVS), ohne Entprellung über den esp_timer-Task wie die übrigen Prefs.
Warum: Mit `CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y` läuft in `spi_flash/cache_utils.c:114`
`assert(esp_task_stack_is_sane_cache_disabled())`, sobald ein Flash-Schreibzugriff den Cache abschalten
muss. Wegen XIP aus PSRAM ist das nur der Fall, wenn gerade eine Flash-mmap-Seite gemappt ist
(`spi_flash_os_func_app.c:237-249`), zum Beispiel während `esp_ota_end()` das Image prüft. Schaltet
jemand genau dann den Schalter um, bricht das Gerät mit Assert ab und startet neu. Sonst blockiert der
Schreibzugriff (bei Seitenwechsel inklusive Erase) trotzdem den LVGL-Task, also Rendering und Touch.
Der Schalter wird während eines Updates nicht deaktiviert (keine `LV_STATE_DISABLED`-Behandlung für
`ota_test_sw`). Aus dem Code hergeleitet, nicht auf dem Board ausgelöst.
Vorschlag: Kanal wie die anderen Prefs über `save_debounce_fire()` im esp_timer-Task (interner Stack)
speichern. Danach darf im LVGL-Pfad kein direkter `storage_record_save`/`nvs_*`-Aufruf übrig sein.
Den Kommentar in `main.c` zu einer prüfbaren Regel machen ("flash access only via debounced esp_timer
callbacks"). Optional in `storage_backend_nvs.c` eine Debug-Prüfung
`esp_task_stack_is_sane_cache_disabled()` mit Log.
Verifikation: `./scripts/fw-build.sh`, dann `./scripts/hw-flash.sh /dev/ttyACM0` mit Umschalten des
Testkanals (einmal auch während eines Test-Updates): kein `assert failed`, kein `lvgl_stall` beim
Umschalten, Kanal nach Neustart erhalten.

## [MITTEL] Vorrechnen der Schoner-Bilder blockiert Boot und Stilwechsel, auch bei Schoner "Aus"
Datei: `main/weather_ui.c:3420`, `:1693`, `:1680`; `main/weather_saver.c:395-407`, `:156-186`.
Problem: `weather_saver_build()` rechnet bei jedem Aufruf sofort in Float pro Pixel: Hintergrund
(614 400 px, je ein `sqrtf` plus Dithering), Balkenschatten (984×126), Käfig, Punkte, Geisterziffern per
Canvas und einmalig das Gitter. Aufgerufen wird es (a) in `weather_ui_create()` unter
`bsp_display_lock(-1)`, auch bei "Aus", (b) ein zweites Mal beim Boot bei gespeichertem Stil "orange",
(c) direkt im Event-Handler beim Tippen auf den Stil. Im Gitter stehen `sinf`/`cosf` in der innersten
Subpixel-Schleife (Disassembly: echte Aufrufe pro Subsample, mehrere hunderttausend für drei feste
Winkel).
Warum: (c) ist ein Stall genau dann, wenn jemand bedient; steht nicht im Stall-Plan, ungemessen. (a)
und (b) verlängern die Lock-Haltezeit beim Boot. Dazu rund 2,06 MB PSRAM (Hintergrund 1 228 800 B,
Balkenschatten 495 936 B, Käfig 185 920 B, Punkte 54 756 B, Geister 83 104 B, Gitter 16 072 B), auch
bei "Aus" dauerhaft belegt; passt zum `psram_free`-Rückgang `boot` → `ui built` (-2,1 MB).
Vorschlag: Beim Boot und beim Stilwechsel nur den gewünschten Stil merken (Dirty-Flag) und die Bilder
erst bauen, wenn der Schoner tatsächlich startet; `weather_ui_set_screensaver()` beim Boot darf nichts
doppelt bauen. Im Gitter Sinus und Kosinus der drei Winkel einmal vor der Pixelschleife rechnen. Bildinhalt
bitgleich.
Verifikation: `./scripts/sim.sh --shots <verzeichnis>` (Screenshots `screensaver`,
`screensaver-orange` unverändert, `SAVER_HOLD OK`, `SAVER_DISMISS OK`); auf dem Board Zeitstempel und
`psram_free` von `heap: ui built` vorher/nachher, kein `lvgl_stall` beim Tippen auf den Stil.

## [MITTEL] Sekundenröhre überspringt Werte und hinkt bis zu 1 s hinterher
Datei: `main/weather_ui.c:1557`, `:3421` (mit `lvgl__lvgl/src/misc/lv_timer.c:347`).
Problem: `saver_tick_cb` läuft mit Periode 1000 ms. LVGL setzt `last_run` beim Ausführen neu, jede
Verspätung summiert sich (Jitter, 150-ms-Stalls zur vollen Minute, 100-145-ms-Stalls bei neuen Ziffern).
Die Phase gegenüber der Wanduhr wandert und rutscht regelmäßig über eine Sekundengrenze.
Warum: Die Sekundenröhre springt sichtbar von x auf x+2, jede Ziffer wechselt bis fast eine Sekunde zu
spät. Bei einer Sekundenuhr ist das die Kernfunktion.
Vorschlag: Mit einem Bruchteil einer Sekunde ticken (billig, `set_digit` und Datum/Temperatur zeichnen nur
bei Änderung neu) oder den nächsten Tick auf die Sekundengrenze ausrichten (Zeitquelle in
Millisekunden). Die Rechnung "Verzögerung bis zur nächsten Sekundengrenze plus Marge" als reine
Funktion nach `components/app_logic/screensaver_policy`, mit Host-Test.
Verifikation: `./scripts/host-test.sh`; auf dem Board mit `SAVER_FAST_TEST=1
./scripts/hw-flash.sh /dev/ttyACM0` mehrere Minuten beobachten, ob die Sekunden lückenlos laufen.

## [NIEDRIG] Nackte Zahlen, doppelt definierte Werte und von Hand gerechnete Bildstrides im Schoner-Code
Datei: `main/weather_saver.c:240` und `417`, `219-227`, `298-306`, `443-461`, `192-200` und `287-296`,
`125-133`, `568`, `570`, `601`, `603-604`, `694`, `759`; `main/weather_ui.c:2139-2141`, `1615`, `1617`.
Problem: `DECOR_BYTES` und `ARGB8888_BYTES` sind zweimal als 4 definiert. Stride und `data_size` werden
von Hand gerechnet statt aus der LVGL-API abgeleitet (`lv_color_format_get_size`,
`lv_draw_buf_width_to_stride`). RGB565-Quantisierung (31/63) steht fast gleich in zwei Funktionen.
Nocturne-Farben der Theme-Palette werden neu getippt, obwohl es sie als `C_*` in `weather_ui.c:33-48`
gibt. Nackte Werte: `0xD2E1DC`/`0xDCE6E4`, `-5`, Radien `4`/`8`/`3`/`2`, `orange ? 36 : 46`, Glow `8`,
`pad_row 5` / `pad_ver 8` / `pad_hor 11`, Puffer `date[48]`/`temp[16]` statt aus
`sv.date_text`/`sv.temp_text` abgeleitet. Die oklch-Palettenwerte aus dem Design sind nicht gemeint.
Warum: Verstößt gegen CLAUDE.md ("keine nackten Zahlenwerte", "API-Werte ableiten", "nie doppelt
definieren"). Auf dem Gerät: Wird `CONFIG_LV_DRAW_BUF_STRIDE_ALIGN` ungleich 1 gesetzt (etwa für PPA),
passen Canvas-Stride und Bild-Header in `image_render()` nicht mehr zusammen, die Ziffern erscheinen still
verschert.
Vorschlag: Eine gemeinsame, aus LVGL abgeleitete Byte-pro-Pixel-Konstante; Stride über die LVGL-Funktion
für Canvas und Header identisch; eine einzige Quantisierungsfunktion; die Nocturne-Tokens in einen
gemeinsamen Header (`weather_ui.c`, `weather_chart.c`, `weather_saver.c`); übrige Werte nach Bedeutung
benennen, Puffergrößen per `sizeof`.
Verifikation: `./scripts/fw-build.sh` und `./scripts/sim.sh --shots <verzeichnis>` (Schoner und
Einstellungen pixelgleich).

## [NIEDRIG] Erzwungener Start umgeht die Policy, `screensaver_policy_setting_changed()` ist toter Code
Datei: `main/weather_ui.c:1700-1706`, `:1669-1674`; `components/app_logic/screensaver_policy.c:45-48`.
Problem: `weather_ui_screensaver_force(true)` setzt `ui.saver_policy.active = true` direkt in `main/`, an
der host-getesteten Policy vorbei. Umgekehrt wird `screensaver_policy_setting_changed()` getestet,
aber nirgends aufgerufen.
Warum: Der 3-s-Trigger (auch im Release) und der Simulator-Hook nutzen den ungetesteten Pfad. Ein
späteres Feld in `screensaver_policy_t` (etwa ein Startzeitpunkt) würde dort still nicht gesetzt.
Widerspricht "Zustandslogik nach app_logic".
Vorschlag: Eine Policy-Funktion für den erzwungenen Start (liefert START nur, wenn nicht aktiv), von
`weather_ui.c` benutzt, mit Host-Test. `setting_changed` entweder in `on_seg_saver` verdrahten oder
entfernen.
Verifikation: `./scripts/host-test.sh` und `./scripts/sim.sh --screen screensaver-hold --screenshot
<datei.bmp>` (gibt `SAVER_HOLD OK` aus).

## [NIEDRIG] `app_weather_get_local_time()` hält den Vertrag "false, solange die Uhr nicht gestellt ist" nicht
Datei: `main/app_weather.c:449-456` (Vertrag in `main/weather_ui.h:162-166`).
Problem: Geprüft wird nur `s_have_forecast`. Nach dem offenen SNTP-Folgepunkt kann eine Prognose
vorliegen, während die Uhr noch ungestellt ist. Außerdem steht die Funktion zwischen dem langen Kommentar
zu `push_forecast_to_ui()` und dieser Funktion; der Kommentar beschreibt jetzt die falsche.
Warum: Der Schoner zeigt dann eine Uhrzeit von 1970, und die Drift wird aus einer falschen Unix-Zeit
berechnet.
Vorschlag: Plausibilität der Wanduhr (etwa "nicht vor Build-Zeitpunkt") als reine Funktion in
`app_logic`, mit Host-Test; `app_weather_get_local_time()` gibt bei ungültiger Uhr false zurück.
Funktion unter `push_forecast_to_ui()` verschieben oder dem Kommentar seine Funktion zurückgeben.
Verifikation: `./scripts/host-test.sh`.

## [NIEDRIG] Stack-Messung nur einmal nach 60 s: verfehlt Standard-Flash-Läufe und NVS-Saves im esp_timer-Task
Datei: `main/app_heap_probe.c:71-82`, `main/app_prefs.c:348-357`.
Problem: `app_heap_probe_log_task_stacks_once()` misst genau einmal nach 60 s. `hw-flash.sh` liest
standardmäßig nur 20 s (`MONITOR_SECONDS=20`), die Zeile erscheint dort nie. `hw-ram.log` zeigt für
`esp_timer` nur noch 1028 B Reserve (von 3584). Dort laufen alle entprellten NVS-Schreibzugriffe, jetzt
auch `persist_saver_cb`. Ein Save nach 60 s geht in keine Messung ein.
Warum: Ein Stacküberlauf im esp_timer-Task beim Speichern wäre Panic mit Neustart; die Sonde würde ihn
nicht ankündigen.
Vorschlag: Wie bei der LVGL-Sonde jedes neue Minimum pro Task protokollieren, periodisch aus dem
Heartbeat. Den esp_timer-Wert ausdrücklich nach einem Einstellungs-Save prüfen, Stack bei Bedarf über
die bestehende sdkconfig-Option anheben.
Verifikation: `MONITOR_SECONDS=90 ./scripts/hw-flash.sh /dev/ttyACM0` mit einem Stil- oder Timeout-Wechsel
innerhalb der ersten 60 s: Zeile `heap: stack esp_timer` zeigt die Reserve nach dem Save.

## Ohne Befund geprüft
- **Bild-Cache:** Statische `lv_image_dsc_t` mit neu allozierten Puffern sind sicher, weil
  `CONFIG_LV_CACHE_DEF_SIZE=0` und `LV_IMAGE_HEADER_CACHE_DEF_CNT=0` gelten (auch in
  `sim/lv_conf.h:570-574`). Wird der Cache aktiviert, braucht `s_stroke_img`, `s_digit_img` usw. ein
  `lv_image_cache_drop()`. Ein Kommentar dazu wäre sinnvoll.
- **Lebenszyklus:** Beim Stilwechsel wird das Overlay vor dem Freigeben der Puffer gelöscht, die
  Stroke-Puffer nach dem Ziffernrender freigegeben. Keine Lecks, kein Use-after-free. Spitze beim
  Ziffernrender vorübergehend rund 0,5 MB PSRAM; alle Allokationen gehen zuerst ins PSRAM.
- **Korrektur zum Stall-Plan:** Das Overlay wird beim Boot gebaut (`weather_ui.c:3420`), nicht beim Start.
  Die rund 500 ms beim Start bestehen aus dem ersten Vollbild plus dem synchronen Rendern aller
  aktuellen Ziffern in `saver_render` → `set_digit` → `image_render`.
- **Fehlerpfad `set_digit`:** `shown` wird vor dem Render gesetzt (`weather_saver.c:785-796`); schlägt die
  Allokation fehl, bleibt die alte Ziffer sichtbar. Bei ~22 MB freiem PSRAM praktisch irrelevant.
- **Veraltete Kommentare:** `screensaver_policy.h` und `test_screensaver_policy.c` nennen noch
  `CONFIG_WEATHER_SCREENSAVER_FAST_TEST` (jetzt CMake `WEATHER_SAVER_FAST_TEST`); der Kommentar zu
  `app_heap_probe_log_now()` in `app_heap_probe.h` steht jetzt über der neuen Funktion.
- **Nebenläufigkeit:** Zugriff auf `s_utc_offset` aus dem LVGL-Task ohne Lock ist ein ausgerichteter `int`,
  unkritisch. Alle anderen Schoner-Aufrufe laufen im LVGL-Task oder beim Boot unter dem Display-Lock.
