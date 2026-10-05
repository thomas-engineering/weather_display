# Audit A (Nebenlaeufigkeit und LVGL-Threadsicherheit) — 2026-10-05
Geprueft: `main/`, `components/app_logic/`, `sim/`, `host_test/main`. Gelesen habe ich zum Verständnis auch den Lock und die LVGL-Task im Adapter (`managed_components/espressif__esp_lvgl_adapter/src/adapter/esp_lv_adapter.c`) und die Hintergrundbeleuchtung im BSP; bewertet werden sie nicht.
Gesucht nach: `xTaskCreate*`, `bsp_display_lock`/`display_lock_timed`, `weather_ui_*` und `lv_*` außerhalb von `weather_ui.c`/`weather_chart.c`/`weather_icons.c`, `lv_timer_handler`, `esp_timer_create`, Event-Handler, Daten, die mehrere Tasks teilen.
Treffer: 104 (5 Task-Erzeugungen, 25 Lock-Stellen, 59 `weather_ui_*`-Aufrufe in `main.c`/`app_weather.c`, 7 `lv_*` in `main.c`, 8 Timer/Handler/`lv_timer_handler`)   Befunde: 7

Hintergrund: Der Adapter-Lock ist ein rekursiver Mutex (`xSemaphoreTakeRecursive`). Die Task `"lvgl"` hält ihn während `lv_timer_handler()` und läuft auf Core 1. `weather`, `ota`, `ota_resume`, `light_sensor` und esp_timer laufen auf Core 0. Daten, die diese Tasks teilen, werden also wirklich parallel gelesen und geschrieben.

## [MITTEL] Abbrechen vor Annahme des OTA-Kommandos geht verloren, Update läuft trotzdem samt Reboot
Datei: main/app_weather.c:1033-1047 (Reset in 1044), main/app_weather.c:1178-1180, main/ota_update.c:43-51, main/weather_ui.c:2053-2057
Problem: `ota_update_request_cancel()` setzt das Flag sofort in der LVGL-Task. `ota_update_reset_cancel()` löscht es erst, wenn `weather_task` das schon früher eingereihte `CMD_OTA_START` annimmt. Ein Abbrechen zwischen dem Tippen auf „Prüfen“ und dieser Annahme wird deshalb überschrieben.
Warum: `weather_task` ist oft viele Sekunden blockiert (Refresh, 30 s Wi-Fi-Connect, bis zu 60 s Warten auf `s_network_mutex`). Tippt der Nutzer in dieser Zeit auf Abbrechen, schließt sich der Dialog. Danach lädt das Gerät trotzdem, flasht und startet nach 1,5 s neu (`app_weather.c:784-786`). Für den Nutzer sieht das wie ein unerklärlicher Neustart aus, und sein Abbruch wurde ignoriert.
Vorschlag: Abbrechen an eine Anforderung binden, nicht an einen globalen Zustand. Jedes Start-Tippen bekommt in der LVGL-Task eine fortlaufende Nummer. Abbrechen merkt sich die höchste Nummer, die bis dahin vergeben war. Die Annahme in `weather_task` verwirft eine Anforderung, deren Nummer kleiner oder gleich dieser Grenze ist. Das laufende `ota_update_run()` prüft dieselbe Grenze gegen die eigene Nummer, statt ein nacktes Bool zu lesen. Kein Reset bei der Annahme mehr.
Nachweis: HOST
  Die Logik aus Nummer und Grenze kommt als kleines Modul nach `components/app_logic` (z. B. `ota_cancel_gate`), der Test nach `host_test/main/test_ota_cancel_gate.c`.
  Fall 1: start → Nummer 1; cancel; accept(1) → „verworfen“.
  Fall 2: cancel; start → 2; accept(2) → „läuft“.
  Fall 3: start → 3; accept(3); cancel; is_cancelled(3) → true.
  Fall 4: start → 4; start → 5; cancel; accept(4) und accept(5) → beide verworfen.

## [MITTEL] Suchtreffer werden per Index gegen Worker-Daten aufgelöst, die sich inzwischen geändert haben können
Datei: main/weather_ui.c:1007-1011, 1370, 1395; main/app_weather.c:821-878 (`s_hit_count = 0` in 824 und 851), 948-955, 1010-1017
Problem: Die Zeilen der Ergebnisliste tragen nur ihren Index i. `CMD_SELECT` und `CMD_FAV_TOGGLE` lösen i in `weather_task` gegen das aktuelle `s_hits` auf. Das ist aber nicht unbedingt der Stand, den der Nutzer gerade sah.
Warum: Fall 1: Der Nutzer tippt weiter, während eine neuere Suche noch lädt, und tippt dann auf eine alte Zeile. Das Kommando liegt hinter `do_search()` in der Queue und trifft danach Index i der neuen Trefferliste. Eine falsche Stadt wird gewählt und per `app_prefs_save_city` dauerhaft gespeichert. Fall 2: Wird die Eingabe unter 2 Zeichen gekürzt, setzt `do_search` `s_hit_count = 0`, die alte Liste bleibt aber sichtbar. Ein Tippen schließt dann die Suche, und sonst passiert nichts.
Vorschlag: Jede Trefferliste bekommt eine Generationsnummer. Die UI legt diese Nummer zusammen mit dem Index in den Nutzdaten jeder Zeile ab und gibt beides im Kommando mit. Der Worker verwirft das Kommando, wenn die Generation nicht stimmt. Ergänzend leert der Worker die sichtbare Liste unter dem Lock, sobald eine neue Suche beginnt oder eine zu kurze Eingabe sie ungültig macht.
Nachweis: HOST
  Die Trefferverwaltung (Liste ersetzen, Generation zählen, `resolve(gen, idx)`) kommt nach `components/app_logic`, der Test nach `host_test/main/test_search_results.c`.
  Fall 1: set(gen1, [A,B,C]); set(gen2, [X,Y]); resolve(gen1, 1) → abgelehnt; resolve(gen2, 1) → Y.
  Fall 2: clear() → resolve(aktuelle Generation, 0) → abgelehnt.

## [MITTEL] Jeder Tastendruck in der Suche belegt einen Platz in der Command-Queue; volle Queue verwirft Kommandos still
Datei: main/app_weather.c:1129-1131 (`xQueueSend(..., 0)`, Rückgabewert ignoriert), 1134 (Tiefe 8), 941-947; main/weather_ui.c:1002-1005, 1235
Problem: `LV_EVENT_VALUE_CHANGED` schickt pro Zeichen ein `CMD_SEARCH`. Zusammengefasst wird erst beim Empfang. Steckt `weather_task` in einer langen Operation, ist die Queue nach 8 Zeichen voll. Alle weiteren Kommandos verfallen ohne Meldung.
Warum: Das passiert beim Booten (Connect bis 30 s, SNTP 8 s, Refresh mit Wiederholungen), bei jedem HTTPS-Abruf und während ein OTA den Netzwerk-Mutex hält. Die zuletzt getippten Zeichen gehen verloren, und die Suche läuft mit einem abgeschnittenen Begriff. Ebenso still verloren gehen ein danach getipptes `CMD_SELECT`, `CMD_REFRESH`, `CMD_WIFI_CONNECT` oder `CMD_OTA_START`. Die UI hat dann schon reagiert, etwa die Suche geschlossen, aber das Kommando kommt nie an.
Vorschlag: Suchbegriffe belegen keine Plätze in der Command-Queue mehr. Der neueste Begriff liegt in einem eigenen Ein-Platz-Postfach, in dem der jüngste Wert gewinnt (z. B. `xQueueOverwrite` auf eine eigene Queue der Länge 1). `weather_task` wartet auf beide. `post()` wertet den Rückgabewert aus.
Nachweis: GUARD
  In `post()` bei `errQUEUE_FULL` einen Zähler je `cmd_kind_t` erhöhen und sofort `ESP_LOGW("weather", "cmd %d dropped, queue full (total n)")` ausgeben. Den Gesamtzähler im 5-s-Takt von `heartbeat_check_cb` (main/main.c:179) mit ausgeben, solange er nicht 0 ist. Erwartung nach der Korrektur: Der Zähler bleibt auch dann 0, wenn man während eines Refreshs einen langen Ortsnamen tippt.

## [MITTEL] Die Regel „LVGL nur unter Lock oder aus der LVGL-Task“ wird nirgends geprüft
Datei: main/display_lock_probe.c:21-42; alle 25 Lock-Stellen in main/app_weather.c und main/main.c; die öffentlichen `weather_ui_*`-Funktionen in main/weather_ui.c
Problem: Alle 59 Aufrufe von `weather_ui_*` außerhalb der LVGL-Task sitzen heute korrekt unter `bsp_display_lock()` (jede Stelle einzeln geprüft, siehe unten). Abgesichert ist das aber nur durch Kommentare. `display_lock_probe` misst die Haltedauer, merkt sich aber nicht, welche Task den Lock hält. 22 von 25 Stellen rufen `bsp_display_lock` ohnehin direkt auf.
Warum: Die nächste Änderung, die etwa ein `weather_ui_set_*` in `ota_on_progress`, in einen Wi-Fi-Event-Handler oder in einen esp_timer-Callback setzt, kompiliert und läuft meistens. Sie führt dann gelegentlich zu beschädigten Objektbäumen oder Abstürzen in `lv_obj_*` auf Core 1. Das sieht dann wie ein DSI- oder Treiberfehler aus.
Vorschlag: Ein Wrapper wird zum einzigen Weg zum Lock. Bei Erfolg merkt er sich die haltende Task und erhöht einen Tiefenzähler (der Mutex ist rekursiv); bei Tiefe 0 vergisst er den Halter wieder. Den Handle der LVGL-Task holt man einmal nach `bsp_display_start_with_config()` per `xTaskGetHandle("lvgl")`. Jede öffentliche `weather_ui_*`-Funktion bekommt am Eingang ein Makro `UI_ASSERT_LOCKED()`. Es ist erfüllt, wenn die aktuelle Task die LVGL-Task ist (sie hält den Lock in `lv_timer_handler`) oder der eingetragene Halter. Im Simulator ist das Makro leer, weil `weather_ui.c` dort ohne FreeRTOS gebaut wird. Abschaltbar per Kconfig, Standard an.
Nachweis: GUARD
  Bei einer Verletzung `ESP_LOGE("ui_lock", "<funktion> from task '<pcTaskGetName>' without display lock")` und `assert` (per Kconfig auf „nur loggen“ abstufbar). Zusätzlich ein Verletzungszähler in der 5-s-Ausgabe aus `heartbeat_check_cb`. Hinweis: Die PERF-Zeile und `main/selftest.c`/`run_selftest()`, auf die `CLAUDE.md` verweist, gibt es im Baum derzeit nicht. Soll ein `SELFTEST_FAIL` daran hängen, muss der Selbsttest erst angelegt werden.

## [NIEDRIG] Ein Sensorwert, der noch unterwegs ist, überschreibt die manuelle Helligkeit; Duplikat-Cache weiß nichts von der anderen Task
Datei: main/app_light.c:297, 328-337, 403-413; main/main.c:61-68, 97-101; main/weather_ui.c:1529-1540
Problem: `light_sensor_task` liest `s_adaptive` nur am Anfang jeder Runde. Danach läuft die Aufnahme bis zu 1 s (`DQBUF_RETRIES` × 50 ms, beim Streamstart dazu 300 ms Einschwingzeit). Anschließend ruft die Task `s_cb(pct)` auf, ohne `s_adaptive` erneut zu prüfen. Außerdem verwirft `on_brightness` in der LVGL-Task Werte gleich `s_last_applied`, obwohl die Sensor-Task die Beleuchtung inzwischen anders gesetzt haben kann.
Warum: Zieht der Nutzer den Regler (das schaltet adaptiv aus) oder schaltet er adaptiv ab, während eine Messung läuft, springen Beleuchtung und Regler kurz danach auf den Sensorwert. In den Einstellungen bleibt der manuelle Wert gespeichert. Und tippt der Nutzer auf dem Regler genau den alten manuellen Wert an, wird `bsp_display_brightness_set` übersprungen. Die Beleuchtung bleibt dann auf dem Sensorwert, während der Regler etwas anderes zeigt.
Vorschlag: Eine Generationsnummer für den adaptiven Modus, die `app_light_set_adaptive` bei jedem Aufruf erhöht. Die Sensor-Task merkt sich die Generation zu Beginn einer Messung und wendet das Ergebnis nur an, wenn sie unverändert und adaptiv noch an ist. Den Duplikat-Cache in `on_brightness` beim Ausschalten von adaptiv und bei jedem Sensor-Update ungültig machen (z. B. auf -1 setzen), oder die Prüfung ganz entfernen.
Nachweis: HOST
  Die Anwenden-Entscheidung kommt zu `light_policy` (`components/app_logic/light_policy.c`), Test in `host_test/main/test_light_policy.c`.
  Fall 1: Messung beginnt (Generation 1); set_adaptive(false) → Generation 2; Messung endet → kein Callback.
  Fall 2: Messung beginnt; nichts ändert sich; Messung endet → Callback mit dem Wert.
  Fall 3: Ausschalten und wieder Einschalten während der Messung → kein Callback, weil die Generation nicht mehr stimmt.

## [NIEDRIG] Nach einem Lock-Timeout gehen einmalige UI-Zustandswechsel ohne Meldung verloren
Datei: main/app_weather.c:373, 506, 676, 692, 699, 717, 825, 829, 876, 907, 920, 971, 980, 988, 1005, 1077; daneben `display_lock_timed` in 387, 432, 484
Problem: Schlägt `bsp_display_lock(1000)` fehl, wird das Update ohne Log und ohne Wiederholung verworfen (anders als in main.c:87-88 und 102-108). Einige Updates korrigieren sich nicht von selbst: `weather_ui_set_wifi_connect_result`, `push_ota_state_to_ui(WX_OTA_DONE)`/`push_ota_error_to_ui`, `apply_toast_state`, `weather_ui_set_loading(false)`, `weather_ui_set_favorites`.
Warum: Nach einem Stillstand von über 1 s, den es auf diesem Board schon gab, bleibt „Verbinde…“ stehen und der Wi-Fi-Dialog offen, obwohl die Verbindung steht. Oder der Refresh-Spinner dreht bis zum nächsten Abruf weiter. Oder die Favoritenleiste weicht von `s_favs` ab, sodass Slot-Indizes auf andere Einträge zeigen.
Vorschlag: Bei einem Timeout an jeder Stelle eine Warnung mit dem Ortsnamen ausgeben. Für Zustandswechsel, die sich nicht selbst korrigieren, das Update vormerken (Flag in `weather_task`) und in der nächsten Schleifenrunde erneut versuchen.
Nachweis: GUARD
  Im Lock-Wrapper aus dem vierten Befund (Regel nicht geprüft) einen Timeout-Zähler mit Ortsnamen führen. Bei jedem Timeout `ESP_LOGW("display_lock", "timeout at '<ort>' (n total)")` ausgeben und den Gesamtzähler in die 5-s-Ausgabe von `heartbeat_check_cb` aufnehmen.

## [NIEDRIG] Die verzögerten Speicher-Callbacks lesen `s_prefs`/`s_favs` auf der esp_timer-Task ohne Synchronisation
Datei: main/app_prefs.c:260-275 (Leser), 281-309 (Schreiber in LVGL-, weather- und light_sensor-Task); main/app_favorites.c:24-27, 33-42; main/save_debounce.c:3-14
Problem: `persist_prefs_cb`/`persist_favorites_cb` kopieren die gemeinsamen Strukturen, während z. B. `app_prefs_save_city` (weather) gerade `name`/`country` per `snprintf` überschreibt oder `app_favorites_toggle` einen Slot neu füllt. Der CRC wird über diese halb geschriebene Kopie berechnet und passt deshalb trotzdem.
Warum: Ein gemischter Datensatz (neue Stadt mit alten Koordinaten oder abgeschnittener Name) landet im NVS. Weil jeder Schreiber danach erneut eine Speicherung anstößt, überschreibt die nächste Speicherung 50 ms später den Fehler. Bleibend wird er nur, wenn genau in diesem Fenster der Strom ausfällt.
Vorschlag: Einen gemeinsamen Mutex für Schreiber und Speicher-Callback, oder eine Kopie, die der Schreiber unter dem Mutex anlegt und der Callback nur noch wegschreibt.
Nachweis: INSPEKTION
  Das Zeitfenster liegt im Mikrosekundenbereich und hängt von der Planung auf zwei Cores ab. Ein Host-Test würde nur die Kopierlogik prüfen, nicht die Überschneidung.

## Nicht beanstandet
- Alle 59 `weather_ui_*`-Aufrufe in main/app_weather.c und main/main.c: Jeder liegt in einem erfolgreichen Lock-Block oder in `push_device_info_to_ui`, das nur aus gesperrten Blöcken gerufen wird (392, 436, 492).
- main/main.c:296-333: Erstaufbau unter `bsp_display_lock(-1)`, einschließlich der Callbacks, die beim Aufbau ausgelöst werden (`on_settings_changed` → `post` mit NULL-Prüfung auf `s_q`).
- main/main.c:123-136, 158-173 (`touch_probe_cb`, `lvgl_stall_probe_cb`): `lv_timer`-Callbacks laufen in der LVGL-Task unter dem Adapter-Lock; ihre `static`-Variablen haben nur einen Besitzer.
- Alle UI-Callbacks in main/main.c:32-48: kopieren Strings sofort in `cmd_t` (`app_weather_search`, `app_weather_wifi_connect`) und reihen ohne Blockieren ein.
- main/main.c:81-109 (`on_light_unavailable`, `on_ambient_brightness` aus `light_sensor_task`): korrekt unter Lock mit endlichem Timeout und Log-Zweig.
- main/app_wifi.c:219-285 (Event-Handler in der Event-Loop-Task): fasst die UI nicht an; Zustand nur über `policy_event` unter `s_policy_lock`.
- Lock-Reihenfolge: `s_network_mutex` → Display-Lock (weather/ota), Display-Lock → `s_policy_lock` (`net_status()`), Display-Lock → `s_task_mutex` (`app_light_set_adaptive` aus der LVGL-Task). Es gibt keinen umgekehrten Pfad, also keinen Deadlock-Zyklus.
- main/app_weather.c:114, 799, 817, 1034-1060 (`s_ota_active`, nicht volatile): Nur `weather_task` setzt und liest das Flag, die OTA-Task löscht es nur. Zwischen den Lesezugriffen liegen externe Aufrufe; ein veralteter Wert verzögert höchstens um eine Runde.
- main/ota_update.c:43 (`s_cancel_requested`, volatile): Einzelne Bool-Zugriffe sind in Ordnung; das eigentliche Problem ist die Reset-Stelle (erster Befund).
- main/app_light.c:403-444: `s_task` wird über `s_task_mutex` gelesen, Deinit über Signal und Warten; die Handle-Zuweisung durch `xTaskCreatePinnedToCore` erfolgt, bevor die Task laufen kann.
- main/app_light.c:393 vs. main/main.c:282-283, 326: Callbacks sind vor dem Start der Task registriert; adaptiv wird erst nach `weather_ui_create` eingeschaltet, die Sensor-Callbacks treffen also nie eine unfertige UI.
- main/save_debounce.c:5-10 (Timer wird bei Bedarf angelegt, ohne Synchronisation): Der Prefs-Timer entsteht schon in `app_main` (über `weather_ui_set_language` → `on_settings_changed`), bevor andere Schreiber laufen; den Favoriten-Timer benutzt nur `weather_task`.
- main/app_prefs.c: Einzelne Enum- und Bool-Felder (`lang`, `time_fmt`, `auto_refresh_minutes`, `ota_*`) werden aus anderen Tasks gelesen. Diese Zugriffe sind einzeln atomar; gemischte Sprachen in einem Push korrigiert das folgende `CMD_RELANG`.
- main/task_heartbeat.c:25-31 (`int64_t` ohne Lock): Ein zerrissener Lesezugriff kann höchstens eine falsche Diagnosezeile erzeugen, sonst keine Auswirkung.
- main/display_lock_probe.c:16-19: Wird nur geschrieben, während der Lock gehalten wird, und nur von `weather_task` benutzt.
- main/app_heap_probe.c: eigener Mutex mit Try-Take, keine LVGL-Zugriffe.
- main/app_wifi.c:112, 155-158 (`s_coproc_persisted` ohne Lock): Schlimmstenfalls wird derselbe Zählerstand doppelt nach NVS geschrieben.
- sim/main.c:1143: `lv_timer_handler` läuft im Simulator in einer einzigen Task; dort gibt es keine Nebenläufigkeit.
- `components/app_logic/`: keine `lv_*`-, Task- oder Lock-Aufrufe.

Ausserhalb der Klasse aufgefallen: main/app_wifi.c:456 `ESP_ERROR_CHECK(esp_wifi_set_config(...))` im Nutzer-Connect-Pfad (Reboot bei RPC-Fehler, Klasse D).

---

Hinweis: Dieser Durchlauf war reine Codeinspektion. Ich habe nichts gebaut, nicht getestet und nichts auf dem Board laufen lassen. Die Befunde zu Überschneidungen zwischen Core 0 und Core 1 lassen sich nur mit den genannten Guards auf der Hardware bestätigen.
