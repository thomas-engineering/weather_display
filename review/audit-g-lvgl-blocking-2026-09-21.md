# Audit G (Blockierendes im LVGL-Task) — 2026-09-21
Geprueft: main/, components/app_logic/, sim/ (Muster: vTaskDelay, xSemaphoreTake,
ESP_LOG* im Callbackpfad, I2C/SPI, NVS/Flash, esp_netif/esp_http/esp_hosted,
lv_timer_handler-Aufrufstellen, alle in main.c registrierten UI-Callbacks)
Treffer: 41   Befunde: 4

## [HOCH] UART-Logzeile pro Slider-Tick im LVGL-Task
Datei: main/main.c:49-54 (on_brightness), Aufrufpfad main/weather_ui.c:1529-1531
und 1925-1926
Problem: `on_brightness()` ruft bei **jedem** LV_EVENT_VALUE_CHANGED des
Helligkeitssliders `bsp_display_brightness_set()`. Diese BSP-Funktion ist nicht
der im Kommentar behauptete "cheap PWM duty write": sie gibt vor dem
`ledc_set_duty()` ein `ESP_LOGI(TAG, "Setting LCD backlight: %d%%")` aus
(managed_components/waveshare__esp32_p4_wifi6_touch_lcd_7b/esp32_p4_wifi6_touch_lcd_7b.c:367).
Der Konsolenpfad ist UART0 @115200 (`CONFIG_ESP_CONSOLE_UART_DEFAULT`,
`CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200`) plus USB-Serial-JTAG als Secondary,
Default-Loglevel INFO (`CONFIG_LOG_DEFAULT_LEVEL=3`), und fuer den BSP-Tag
("ESP32_P4_EV") wird nirgends `esp_log_level_set()` gesetzt.
Warum: Die Zeile ist rund 45 Zeichen lang, das sind ~4 ms blockierender
FIFO-Wait pro Ereignis — mitten im Event-Dispatch von `lv_timer_handler()`.
LVGL erzeugt pro Indev-Poll (~30 ms) ein Value-Changed-Ereignis, also ~33
Zeilen/s waehrend des Ziehens: ~130 ms UART-Blockade pro Sekunde, genau in dem
Moment, in dem der Finger auf dem Display liegt. Das ist derselbe Task, der auch
den GT911 pollt — der Slider fuehlt sich dadurch zaeh an und die
Renderzeitmessung des Projekts bekommt einen Offset, der nichts mit DSI oder PPA
zu tun hat.
Vorschlag: Zwei voneinander unabhaengige Aenderungen, beide gehoeren in unseren
Code, nicht in die Managed Component. (a) Den BSP-Tag beim Start auf WARN
herunterstufen, damit keine Managed-Component-INFO-Zeile jemals in einem
Widget-Callback landet. (b) Zusaetzlich im Callback selbst entkoppeln: den
Duty-Write nur ausfuehren, wenn sich der Prozentwert gegenueber dem zuletzt
geschriebenen tatsaechlich geaendert hat, und die Regel festschreiben, dass aus
LVGL-Callbacks keine Funktion mit unbekanntem Logverhalten direkt gerufen wird.
Der persistente NVS-Teil ist bereits korrekt auf `final` beschraenkt und
zusaetzlich entprellt — der ist nicht gemeint.
Nachweis: GUARD
  Zeitmessung direkt um den Aufruf in `on_brightness()`: `esp_timer_get_time()`
  vor und nach `bsp_display_brightness_set()`, Maximum in einer statischen
  Variable halten und in der PERF-Zeile als `bl_set_max_us` ausgeben. Ergaenzend
  ein Budget-Check im selben Callback: ueberschreitet eine einzelne Ausfuehrung
  1000 us, eine Logzeile *ausserhalb* des Callbacks (ueber ein Flag, das die
  PERF-Ausgabe liest) — nicht im Callback selbst, sonst misst der Guard sich
  selbst. Vorher/Nachher ist damit zahlenmaessig belegbar, ohne auf das Gefuehl
  beim Ziehen angewiesen zu sein.

## [MITTEL] esp_netif-IPC mit unbegrenzter Wartezeit unter dem Display-Lock
Datei: main/app_weather.c:266 (in push_device_info_to_ui), Sperren bei
main/app_weather.c:347, 390 und 442; blockierender Aufruf in main/app_wifi.c:206
Problem: `push_device_info_to_ui()` laeuft an allen drei Stellen innerhalb von
`bsp_display_lock()` und ruft dort `app_wifi_get_ip_info()`, das
`esp_netif_get_dns_info()` benutzt. Anders als `esp_netif_get_ip_info()` (das
laut esp_netif_lwip.c:1980 direkt aus der lwip-Struktur liest) ist
`esp_netif_get_dns_info()` ein IPC-Call an den TCP/IP-Task
(esp_netif_lwip.c:2286, `esp_netif_lwip_ipc_call`). Mit
`CONFIG_LWIP_TCPIP_CORE_LOCKING` **nicht** gesetzt (sdkconfig:3236) laeuft das
ueber `sys_arch_sem_wait(&api_lock_sem, 0)` plus `tcpip_send_msg_wait_sem` — beide
ohne Timeout.
Warum: Das ist strukturell derselbe Fehler wie der bereits behobene
esp_hosted-RPC an gleicher Stelle (siehe den Kommentar bei
main/app_weather.c:112-120): ein Aufruf mit prinzipiell unbegrenzter Wartezeit,
ausgefuehrt von einem Task, der den LVGL-Lock haelt. Solange der TCP/IP-Task
(Prio 18) frei ist, kostet das nur Mikrosekunden; waehrend eines OTA-Downloads
ueber die SDIO-Strecke zum C6 oder bei vollem tcpip-Mbox wartet der Weather-Task
darunter, und der LVGL-Task wartet auf den Lock — ohne dass der Nutzer sieht,
warum. Ausgeloest wird das mindestens alle 30 s (Clock-Tick,
main/app_weather.c:1025), zusaetzlich bei jedem Refresh und jedem Fehler.
Vorschlag: Die Netzwerk-Abfrage aus dem gesperrten Abschnitt herausziehen: IP,
DNS und Gateway vor `bsp_display_lock()` in lokale Puffer holen und unter dem
Lock nur noch die fertigen Strings an die Widgets geben. Als Regel formuliert:
unter `bsp_display_lock()` steht ausschliesslich `lv_*`/`weather_ui_*`, kein
Aufruf, der einen anderen Task oder eine andere CPU um eine Antwort bittet.
Nachweis: GUARD
  Ein Lock-Hold-Zaehler: in einem duennen Wrapper um `bsp_display_lock()`/
  `bsp_display_unlock()` die Haltedauer messen, Maximum und die Datei/Zeile des
  Sperrorts des Maximums merken und in der PERF-Zeile ausgeben
  (`lock_hold_max_us` + Ort). Ueberschreitet eine Haltedauer 50 ms, eine
  WARN-Zeile nach dem Entsperren. Der Befund ist damit vor und nach der
  Umstellung an derselben Zahl ablesbar, und jeder kuenftige Rueckfall in
  dasselbe Muster faellt von selbst auf.

## [NIEDRIG] I2C-Touchlesung im lv_timer_handler teilt den Bus mit der Kamera — der Kommentar erklaert das Problem für geloest
Datei: main/main.c:219-226
Problem: Der Kommentar begruendet `task_core_id = 1` damit, dass LVGL und der
Kamera-Task sich sonst Core 0 *und* den I2C-Bus teilen, und schliesst mit
"Pinning LVGL to core 1 removes that contention entirely". Die Haelfte stimmt
nicht: die Touchlesung laeuft als Indev-Read-Callback innerhalb von
`lv_timer_handler()` (bestaetigt in
managed_components/espressif__esp_lvgl_adapter/src/input/esp_lv_adapter_input_touch.c:407ff,
`with_irq` ist auf diesem Board false, weil die INT-Leitung nicht verdrahtet
ist), und der Bus-Mutex des I2C-Masters ist core-unabhaengig. Eine GT911-Lesung
auf Core 1 wartet weiterhin auf eine laufende SCCB-Transaktion des
Kamera-Sensors auf Core 0.
Warum: Der reale Schaden ist klein — `CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER`
ist nicht gesetzt (sdkconfig:5038), im laufenden Stream gibt es also keinen
Per-Frame-SCCB-Verkehr; Bursts entstehen nur in `start_streaming()`
(main/app_light.c:229ff, VIDIOC_S_FMT programmiert die Sensor-Registertabelle)
und bei dessen Wiederholversuchen, im ungünstigsten Fall bis zu 40-mal alle 5 s.
Pro Touch-Poll sind das Bruchteile einer Millisekunde. Der Befund ist der
Kommentar, nicht die Millisekunde: eine real verbliebene Latenzquelle steht im
Code als "entirely removed", und der naechste Durchlauf wird dort nicht mehr
nachsehen.
Vorschlag: Den Kommentar auf das beschraenken, was das Pinning leistet
(CPU-Konkurrenz weg, Buskonkurrenz bleibt), und die verbleibende Buskonkurrenz
messbar machen statt sie wegzuschreiben.
Nachweis: GUARD
  Im Indev-Read-Pfad (ueber den `custom_touch_read`-Callback des Adapters, den
  wir selbst stellen koennen) die Dauer der Lesung messen und Maximum plus
  Anzahl der Lesungen ueber 2 ms in der PERF-Zeile ausgeben
  (`touch_read_max_us`, `touch_read_slow_n`). Beim Umschalten der adaptiven
  Helligkeit muss der Zaehler sichtbar anspringen, sonst ist die Bus-These
  widerlegt — beides ist ein verwertbares Ergebnis.

## [NIEDRIG] Die vorhandene Stall-Messung kann Lockwartezeit und Renderzeit nicht trennen
Datei: main/main.c:136-151
Problem: `lvgl_stall_probe_cb()` misst ausschliesslich den Abstand zwischen zwei
eigenen Timer-Aufrufen. Ein gemeldeter Wert von z.B. 260 ms sagt nicht, ob der
LVGL-Task auf `bsp_display_lock()` wartete (ein anderer Task hielt ihn), ob ein
Widget-Callback lange lief, oder ob Flush/PPA-Rotation die Zeit verbrauchte.
Warum: Genau diese Unterscheidung ist fuer die offenen Punkte des Projekts
entscheidend (PPA-ROTATE_180-Kosten, TRIPLE_PARTIAL, IDF-6.1-Callback). Jeder
kuenftige Fix in dieser Klasse — auch die drei Befunde oben — laesst sich mit
dem heutigen Probe nicht als wirksam oder unwirksam belegen, weil dieselbe Zahl
aus drei verschiedenen Ursachen entstehen kann.
Vorschlag: Den Probe um zwei getrennt gemessene Anteile ergaenzen, beide im
LVGL-Task erhoben und in einer PERF-Zeile ausgegeben statt pro Ereignis
geloggt: (1) Wartezeit auf den Lock (Zeit zwischen Anforderung und Erhalt),
(2) Dauer des `lv_timer_handler()`-Durchlaufs selbst. Zusammen mit dem
`lock_hold_max_us` aus dem zweiten Befund ergibt das eine vollstaendige
Zuordnung: Wartezeit + eigene Laufzeit + Rest = der heute gemeldete Abstand.
Die bestehende WARN-Zeile bleibt, bekommt aber die Aufteilung mit.
Nachweis: GUARD
  Ist selbst der Guard. Akzeptanzkriterium: bei einem kuenstlich erzeugten
  Stall (Testkommando, das den Display-Lock 300 ms haelt) muss die Ausgabe
  diesen Stall vollstaendig dem Lock-Wait zuordnen und nicht der eigenen
  Laufzeit; tut sie das nicht, misst die Instrumentierung an der falschen
  Stelle.

## Nicht beanstandet
- main/main.c:31-41 — alle UI-Callbacks (Suche, Stadtwahl, Refresh, WLAN,
  Favoriten, OTA-Start/Auto-Update) gehen ueber `post()` in
  main/app_weather.c:1046 mit `xQueueSend(..., 0)`. Timeout 0, kein Blockieren
  im LVGL-Task. Genau richtig.
- main/main.c:39 -> main/app_weather.c:1094 -> main/ota_update.c:44 — der
  OTA-Cancel setzt nur ein `volatile bool`. Keine Sperre, kein RPC.
- main/app_prefs.c:277-279 mit main/save_debounce.c — alle vier `save_*`-
  Einstiegspunkte schreiben nicht selbst, sondern feuern einen 50-ms-esp_timer.
  Der NVS-Write (und damit die cache-abschaltende Flash-Operation) laeuft im
  esp_timer-Task, nicht im LVGL-Task, und faellt bei einem Slider-Zug einmal an
  statt pro Tick.
- main/app_light.c:418 `xSemaphoreTake(s_task_mutex, portMAX_DELAY)` wird vom
  LVGL-Task aus erreicht (on_brightness_adaptive). Unbegrenzter Timeout, aber
  unter dem Mutex stehen an allen drei Haltestellen (290, 418, 434) nur
  Zuweisungen, kein blockierender Aufruf; die Haltedauer ist konstant und
  kuerzer als der Kontextwechsel, der sie ausloesen wuerde.
- main/main.c:51-54 `app_prefs_save_brightness(percent)` nur bei `final` — der
  Flash-Pfad ist korrekt vom Drag-Pfad getrennt.
- main/weather_ui.c insgesamt — die Datei ruft keine `esp_*`-, `bsp_*`- oder
  `app_*`-Funktion ausser den registrierten Callbacks; kein I2C, kein NVS, kein
  Netzwerk, kein `vTaskDelay`. Der Aufbau ist genau so gedacht und haelt.
- main/weather_ui.c:977-998 (day_card_click_cb) und main/weather_chart.c:124ff —
  der Detail-Chart wird im Touch-Callback komplett neu aufgebaut, inklusive
  zweier `lv_obj_update_layout()`. Das ist Zeichenarbeit, kein Warten: sie
  gehoert in den LVGL-Task und liesse sich nur durch weniger Objekte, nicht
  durch Verlagerung verkuerzen. Kein Befund dieser Klasse.
- main/main.c:249-286 — `bsp_display_lock(-1)` haelt den Lock waehrend des
  gesamten UI-Aufbaus. Einmalig beim Start, bevor der LVGL-Task etwas
  darstellen koennte; keine Laufzeitwirkung.
- main/app_weather.c:719, 722 und main/app_wifi.c:180 — `vTaskDelay` laeuft dort
  jeweils im OTA-Worker bzw. in app_main, nie im LVGL-Task.
- main/app_weather.c:442-451 — der volle Rebuild (set_current/set_days/
  set_hourly) unter einem einzigen Lock ist korrekt: die Arbeit *muss* unter dem
  Lock laufen, und die Aufteilung in `full`/nicht-`full` (Zeile 371ff) haelt sie
  bereits von den 30-s-Ticks fern.
- main/task_heartbeat.c — wird ausschliesslich aus Worker-Tasks und dem
  esp_timer-Task getouched, nie aus dem LVGL-Task.

Ausserhalb der Klasse aufgefallen: `app_light_deinit()` (main/app_light.c:424)
hat keinen Aufrufer im gesamten Baum.
