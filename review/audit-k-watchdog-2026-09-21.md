# Audit K (Watchdog und lange Schleifen) — 2026-09-21

Geprueft: `main/**`, `components/app_logic/**` (ohne `main/fonts/`), Muster
`while`, `for(;;)`, `vTaskDelay`, `portMAX_DELAY`, `xSemaphoreTake`,
`xEventGroupWaitBits`, `ulTaskNotifyTake`, `esp_task_wdt*`,
`portENTER_CRITICAL`, Init-Sequenzen. `managed_components/`, `build*/`,
`.logs/` ausgeschlossen. Der DSI-/Panel-Pfad liegt vollstaendig in
`managed_components/` und war damit ausserhalb des Suchraums — die in AUDIT.md
erwaehnte DSI-Busy-Wait-Stelle ist in diesem Baum nicht pruefbar.
Treffer: 31 Schleifen/Wartestellen   Befunde: 5

## [HOCH] Unbegrenztes Warten auf `s_network_mutex` ohne Timeout und ohne Fehlerpfad
Datei: main/app_weather.c:582, :674, :726, :834, :845
Problem: Alle fuenf Nutzer des Netzwerk-Mutex nehmen ihn mit `portMAX_DELAY`.
`weather_task` blockiert damit in `do_refresh()` bzw. bei CMD_WIFI_SCAN/
CMD_WIFI_CONNECT beliebig lange, solange eine OTA-Task den Mutex haelt — und
die haelt ihn ueber einen kompletten Mehr-MB-Download (siehe naechster Befund,
ohne eigene Obergrenze).
Warum: Waehrend dieser Zeit verarbeitet `weather_task` keine Kommandos mehr.
`post()` sendet mit `xQueueSend(..., 0)` in eine Queue der Tiefe 8
(app_weather.c:977-979, :982), also gehen ab dem neunten Tastendruck Suche,
Refresh, Favoriten und Wi-Fi-Aktionen still verloren; die Uhr im Header und
`weather_ui_set_network_status()` frieren ebenfalls ein, weil beide am Ende
derselben Schleife haengen. Die UI zeigt dabei keinerlei Hinweis — sie
reagiert auf Taps, es passiert nur nichts mehr. Wedgt eine OTA-Task
tatsaechlich (esp-hosted-RPC ueber SDIO, siehe die „mempool OOM (RX)"-Historie
im Kommentar bei :60), bleibt das Geraet dauerhaft in diesem Zustand; niemand
erkennt es (siehe Befund „Kein Task am Task-Watchdog").
Zweitwirkung an derselben Stelle: `ota_worker_task` wartet bei :674 auf den
Mutex, bevor `ota_update_run()` laeuft — und `ota_update_run()` setzt
`s_cancel_requested = false` als erste Anweisung (ota_update.c:312). Ein
„Abbrechen"-Tap waehrend dieser Wartezeit wird also verworfen, und der Download
startet danach trotzdem.
Vorschlag: Mutex-Nahme mit endlichem Timeout (Vorschlag: 60 s fuer
weather_task, das ist deutlich mehr als jede legitime Netzwerkoperation) plus
definiertem Fehlerpfad: Kommando verwerfen, Zaehler hochzaehlen, dem Nutzer
den Zustand zeigen (z. B. „Update laeuft, bitte warten") statt still zu
haengen. Zusaetzlich vor `ota_update_run()` nach gewonnenem Mutex den
Cancel-Wunsch erneut pruefen, statt ihn beim Eintritt zu loeschen; das Loeschen
gehoert an die Stelle, wo der Auftrag angenommen wird, nicht an den Start des
Laufs.
Nachweis: GUARD
  Jede Mutex-Nahme laeuft ueber einen Wrapper, der die Wartezeit misst.
  Ueberschreitet sie ein Budget (Vorschlag 5 s), wird eine WARN-Zeile mit
  Aufrufer-Tag und Wartezeit ausgegeben; beim Erreichen des harten Timeouts
  gibt es eine ERROR-Zeile plus einen Zaehler `net_mutex_timeouts`, der in der
  PERF-/Heartbeat-Zeile mitgefuehrt wird. Damit ist sowohl die haeufige
  Kurzblockade als auch der Dauerhaenger im Log sichtbar, statt nur am
  stehenden Bild erkennbar.

## [HOCH] Kein eigener Task ist beim Task-Watchdog angemeldet, Panic ist aus
Datei: sdkconfig:2509-2519 (Konfiguration);
Tasks: main/app_weather.c:986, :900, :913 und main/app_light.c:359
Problem: `CONFIG_ESP_TASK_WDT_INIT=y` mit 5 s und beiden Idle-Tasks als
Abonnenten, aber `esp_task_wdt_add()` kommt im gesamten Projekt nicht vor
(Suche ueber `main/` und `components/` ergab null Treffer). Ueberwacht wird
damit ausschliesslich „laeuft der Idle-Task noch", nicht „kommen meine vier
eigenen Tasks noch voran". Zusaetzlich ist `CONFIG_ESP_TASK_WDT_PANIC` nicht
gesetzt, d. h. selbst ein Idle-Starvation-Fall druckt nur einen Backtrace und
laeuft weiter.
Warum: Genau die Haenger, die dieses Projekt real hat (blockierende
SDIO-/RPC-Aufrufe zum C6, V4L2-Stream-Restart, unbegrenzte Mutex-Wartezeit
oben), blockieren einen Task *schlafend*. Der Idle-Task laeuft dabei
weiter, der Watchdog schweigt, und das Geraet bleibt stundenlang mit einem
veralteten Bild stehen, ohne Log, ohne Reset, ohne Hinweis. Ein
Feldgeraet, das am Wohnzimmer haengt, sieht dann aus wie „funktioniert",
zeigt aber Wetterdaten von gestern.
Vorschlag: Liveness-Ueberwachung fuer die vier eigenen Tasks („weather",
„light_sensor", „ota", „ota_resume"). Nicht per reinem `esp_task_wdt_add()` —
`weather_task` darf legitim 15-40 s in einem HTTP-Timeout stehen und die
OTA-Tasks Minuten — sondern mit pro Task definiertem, grosszuegigem Budget.
Ob daraus ein Reset oder nur eine Meldung wird, ist eine Produktentscheidung;
erkannt und gemeldet werden muss es in jedem Fall.
Nachweis: GUARD
  Jeder der vier Tasks schreibt am Schleifenkopf (bzw. die OTA-Tasks an ihren
  Fortschrittspunkten) einen `esp_timer_get_time()`-Zeitstempel in einen
  Heartbeat-Slot. Ein esp_timer-Callback bzw. der bereits existierende
  `lvgl_stall_probe_cb` (main/main.c:118) prueft alle 5 s alle Slots gegen ihr
  Budget und gibt bei Ueberschreitung eine ERROR-Zeile mit Taskname und Alter
  aus; die Alterswerte gehoeren zusaetzlich in die PERF-Zeile, damit man den
  Normalbereich kennt. Ein Slot, der als „nicht laufend" markiert ist (OTA-Task
  existiert nicht), wird uebersprungen.

## [MITTEL] OTA-Download-Schleifen ohne Gesamtzeit- oder Stillstandsgrenze
Datei: main/ota_update.c:256-271 (P4) und :368-374 (C6)
Problem: Beide Schleifen laufen, bis der Server EOF oder einen Fehler liefert.
Begrenzt ist nur der einzelne Socket-Lesevorgang (`timeout_ms = 15000`). Ein
Server, der alle 14 s ein paar Bytes nachschiebt, haelt die Schleife beliebig
lange am Leben; eine Stillstandserkennung („seit N Sekunden kein Byte mehr")
und eine Gesamtobergrenze fehlen. Die C6-Schleife prueft ausserdem
`s_cancel_requested` ueberhaupt nicht, obwohl sie ein komplettes Image ueber
RPC-Chunks von 1536 B schiebt.
Warum: Waehrend dieser Zeit haelt die aufrufende Task `s_network_mutex`
(app_weather.c:674 bzw. :726), also steht gleichzeitig die gesamte
Wetter-/Wi-Fi-Funktion still (siehe erster Befund). Aus einem
haengengebliebenen Download wird so ein Geraet, das gar nichts mehr tut — und
zwar ohne Zeitgrenze.
Vorschlag: Beide Schleifen bekommen eine Startzeit, eine harte Gesamtgrenze
(Vorschlag 10 min) und eine Stillstandsgrenze (Vorschlag: kein
Fortschritt in 60 s). Bei Verletzung: `esp_https_ota_abort()` bzw.
`esp_hosted_slave_ota_end()`-Abbruch, Rueckgabe `OTA_ERR_NETWORK`,
Log-Zeile mit dem erreichten Byte-Stand. Die C6-Schleife pruefe
`s_cancel_requested` je Chunk wie die P4-Schleife.
Nachweis: GUARD
  Die Abbruchbedingung selbst ist der Guard: bei Ueberschreitung wird
  `OTA_ERR_NETWORK` gemeldet und eine ERROR-Zeile mit Gesamtdauer, gelesenen
  Bytes und Grund („total budget" / „stalled") ausgegeben, sodass der Fall im
  Log von einem normalen Netzwerkfehler unterscheidbar ist. Ergaenzend ein
  Zaehler der Abbrueche in der PERF-Zeile.

## [MITTEL] Boot-Sequenz blockiert bis zu ~45 s, bevor das Wi-Fi-Setup erreichbar ist
Datei: main/main.c:255-291
Problem: `app_main()` haengt nacheinander an `app_wifi_init()` (feste 500 ms
`vTaskDelay`, app_wifi.c:180), `app_wifi_connect()` (bis 30 s
`xEventGroupWaitBits`, app_wifi.c:233-234) und `app_wifi_sync_time(15000)`
(bis 15 s, app_wifi.c:335). Erst danach oeffnet Zeile 287-290 das
Wi-Fi-Setup. Das `WIFI_FAIL_BIT` wird zudem erst nach
`CONFIG_WEATHER_WIFI_MAX_RETRY=8` Disconnect-Events gesetzt, sodass der
30 s-Fall bei einem abwesenden AP der Normalfall ist, nicht der Ausnahmefall.
Warum: Wer das Geraet in ein neues WLAN umziehen will (alte Credentials
gespeichert, alter AP weg), sieht bis zu ~45 s lang nur den Ladezustand und
kommt in dieser Zeit an kein Bedienelement, das ihm weiterhilft. Das sieht wie
ein Absturz aus. Der LVGL-Task selbst laeuft weiter, der Eindruck entsteht
allein aus der seriellen Init-Kette im Main-Task.
Vorschlag: Die Reihenfolge umdrehen — Netzwerkstatus („Verbinde...") sofort
anzeigen und das Wi-Fi-Setup erreichbar machen, den Verbindungsversuch und
den SNTP-Sync in den Worker verlagern (die Callback-Wege dorthin existieren
bereits) statt sie im Main-Task zu serialisieren. Alternativ, falls die
Reihenfolge bleiben soll: SNTP nicht blockierend abwarten, sondern nach dem
ersten erfolgreichen Refresh nachziehen.
Nachweis: GUARD
  Pro Boot-Phase (`nvs`, `display`, `light_probe`, `ui_create`, `wifi_init`,
  `wifi_connect`, `sntp`, `worker_start`) die Dauer per
  `esp_timer_get_time()` messen und am Ende eine einzelne
  `BOOT_PHASES`-Zeile mit allen Werten loggen; eine WARN-Zeile, sobald die
  Summe bis „UI bedienbar" ein Budget (Vorschlag 5 s) ueberschreitet. Damit
  ist im Boot-Log sofort sichtbar, welche Phase die Zeit frisst, statt es aus
  Zeitstempeln rekonstruieren zu muessen.

## [NIEDRIG] Endlose Wiederholung des Kamera-Streamstarts ohne Backoff und ohne Aufgeben
Datei: main/app_light.c:284-288
Problem: Schlaegt `start_streaming()` fehl, macht die Schleife mit `continue`
weiter und versucht es beim naechsten Tick — alle 5 s, unbegrenzt oft, mit
identischen Parametern und je einer WARN-Zeile. Jeder Versuch oeffnet das
CSI-Geraet, mappt die Puffer und schliesst wieder (`open_and_configure()`,
:93-167).
Warum: Eine Kamera, die sich zwar meldet, aber kein RGB565 liefert oder deren
Stream nicht anlaeuft, erzeugt dauerhaft alle 5 s einen kompletten
Open/mmap/Close-Zyklus samt LDO-Nutzung und flutet das Log — auf einem Geraet,
das monatelang laeuft, dauerhaft und ohne dass jemand etwas davon hat.
`app_light_available()` meldet weiterhin „verfuegbar", die UI bietet den
Schalter also weiter an.
Vorschlag: Aufeinanderfolgende Fehlversuche zaehlen, das Intervall staffeln
(z. B. 5 s / 30 s / 5 min) und nach einer Obergrenze aufgeben: adaptive Mode
abschalten, `s_available` auf false, den Schalter in der UI deaktivieren und
einmal ERROR loggen, statt die Meldung ewig zu wiederholen.
Nachweis: GUARD
  Der Fehlversuchszaehler ist selbst der Guard: sein aktueller Stand gehoert
  in die PERF-/Heartbeat-Zeile (`light_stream_fails`), und das Aufgeben wird
  mit einer einmaligen ERROR-Zeile plus dem deaktivierten UI-Schalter sichtbar.
  So unterscheidet sich „einmalig geholpert" von „gibt es nicht mehr".

## Nicht beanstandet
- `main/ota_update.c:81` — Redirect-Schleife ist hart auf 5 Durchlaeufe begrenzt und hat einen Fehlerrueckgabewert.
- `main/ota_update.c:123` und `main/app_weather.c:175` — HTTP-Leseschleifen sind durch `JSON_BUF_MAX` (32 KB) bzw. `HTTP_BUF_MAX` (48 KB) und den 15 s-Socket-Timeout beidseitig begrenzt; sie blockieren schlafend, nicht per Busy-Wait.
- `main/ota_update.c:208` — SHA-256 ueber die Partition, 4-KB-Chunks, Laufindex streng monoton, Abbruch bei Lesefehler.
- `main/app_light.c:188-205` — DQBUF-Retry ist auf 20 Versuche x 50 ms mit `vTaskDelay` begrenzt und hat einen Timeout-Fehlerpfad, also genau das von AUDIT.md K geforderte Muster.
- `main/app_weather.c:693-701` — Die Reboot-Wartezeit ist auf 5 min begrenzt, enthaelt `vTaskDelay(5000)` und startet danach in jedem Fall neu; ein haengender Display-Lock verlaengert sie nur bis zur selben Obergrenze.
- `main/app_weather.c:807-809` — Die Worker-Hauptschleife blockiert in `xQueueReceive` mit 500 ms Timeout, gibt also in jedem Durchlauf die CPU ab.
- `main/app_light.c:261-267` — Sensor-Schleife wartet in `ulTaskNotifyTake` mit 5 s Timeout, kein Busy-Wait.
- `main/app_light.c:402` — `xSemaphoreTake(s_deinit_done, portMAX_DELAY)` ohne Timeout, aber `app_light_deinit()` hat im gesamten Baum keinen Aufrufer; die wartende Seite wird ausserdem per Notify geweckt und der Task erreicht den Checkpoint spaetestens nach ~1,3 s.
- `main/app_wifi.c:180` und `:233` — Feste 500 ms Settle-Delay mit begruendetem Kommentar bzw. 30 s begrenztes Event-Warten; beides hat eine Obergrenze (die Gesamtwirkung auf den Boot steht als eigener Befund oben).
- `main/weather_chart.c`, `main/weather_ui.c`, `main/weather_icons.c`, `components/app_logic/**` — alle Schleifen sind durch feste Array-/Tagesgrenzen (max. 24 Durchlaeufe) begrenzt, ohne Wartezustand.
- Im gesamten geprueften Baum gibt es kein `while`, kein `portENTER_CRITICAL`/`taskENTER_CRITICAL`, kein `vTaskSuspendAll` und kein Pollen eines Hardware-Flags — die klassische K-Busy-Wait-Form kommt hier nicht vor.

Ausserhalb der Klasse aufgefallen: `main/app_prefs.c:260-275` schreibt NVS
(Flash) aus einem esp_timer-Callback, also aus dem esp_timer-Task; und
`main/app_weather.c:185` behandelt `esp_http_client_read() == 0` wie EOF, was
auch ein Lese-Timeout sein kann (stillschweigend abgeschnittener Body).
