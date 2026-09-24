# AUDIT.md — Fehlerklassen fuer den Firmware-Sweep

Nachschlagewerk fuer den Subagenten `firmware-auditor`. **Eine Klasse pro
Durchlauf.** Der Agent liest hier den Abschnitt zu seiner Klasse, bevor er
sucht.

Aufruf:

> Lass den firmware-auditor die Klasse B (Heap und Speicher) pruefen.

Reihenfolge-Empfehlung fuer den ersten Durchgang: **A, B, F, E** — das sind
die Klassen, die auf dem P4 mit DSI und PSRAM die haerteste Auswirkung haben.
Danach der Rest.

---

## A — Nebenlaeufigkeit und LVGL-Threadsicherheit

**Warum:** LVGL-APIs sind nicht threadsicher. Ein Zugriff aus einem anderen
Task ohne Lock erzeugt sporadische Abstuerze und beschaedigte Displaylisten,
die wie Treiberfehler aussehen.

**Suchmuster:** `lv_` ausserhalb der LVGL-Task-Datei; `xTaskCreate`;
Callbacks aus Netzwerk-, Sensor- oder Timer-Tasks, die Widgets anfassen;
`lv_timer_handler` Aufrufstellen; fehlendes Lock/Unlock-Paar.

**Nachweis:** GUARD. Ein Wrapper um die LVGL-Zugriffe, der prueft, ob der
aufrufende Task der LVGL-Task ist oder das Lock haelt, und sonst assertet.
Das ist der einzige verlaessliche Nachweis — Timing-abhaengige Fehler lassen
sich nicht unit-testen.

---

## B — Heap und Speicher

**Warum:** Framebuffer und Zeichenpuffer konkurrieren mit allem anderen. Der
`mbedtls_ssl_setup`-Fehler in diesem Projekt kam vermutlich genau daher. Nicht
die Gesamtsumme entscheidet, sondern der **groesste zusammenhaengende freie
Block**.

**Suchmuster:** `malloc`, `calloc`, `heap_caps_*`, `strdup`, `sprintf` in
Schleifen; Allokationen im Flush- oder Renderpfad; fehlende NULL-Pruefung nach
Allokation; fehlende Freigabe beim Screenwechsel; `MALLOC_CAP_INTERNAL` gegen
`MALLOC_CAP_SPIRAM`.

**Nachweis:** GUARD. Watermark in der PERF-Zeile
(`heap_caps_get_free_size` und `heap_caps_get_largest_free_block`, je fuer
INTERNAL und SPIRAM), plus eine Untergrenze, die `SELFTEST_FAIL` ausloest.
Fuer Leck-Verdacht: Screenwechsel n-mal durchlaufen und die Freigroesse
vorher/nachher vergleichen.

---

## C — Stacks

**Warum:** Zu knappe Stacks aeussern sich als Absturz an voellig anderer
Stelle. `-O2` aus M2 verschiebt den Verbrauch zusaetzlich.

**Suchmuster:** `xTaskCreate*` mit Stackgroesse; grosse lokale Arrays;
Rekursion; `snprintf` mit grossen Puffern auf dem Stack; ISR-Handler.

**Nachweis:** GUARD. `uxTaskGetStackHighWaterMark()` fuer jeden eigenen Task
in der PERF-Zeile. Reserve unter 512 Byte ist ein Befund.

---

## D — Fehlerbehandlung

**Warum:** Ein ignorierter `esp_err_t` verwandelt einen klaren Fehler in ein
spaeteres Raetsel.

**Suchmuster:** Aufrufe von `esp_*`-Funktionen, deren Rueckgabewert verworfen
wird; `ESP_ERROR_CHECK` an Stellen, wo ein Reboot die falsche Reaktion ist
(Netzwerk, SD-Karte, Touch); leere `catch`-artige Konstrukte; `(void)`-Casts.

**Nachweis:** HOST fuer Fehlerpfade in `app_logic`. Sonst INSPEKTION, oder
GUARD wenn sich ein Fehlerzaehler einbauen laesst.

---

## E — ISR-Korrektheit

**Warum:** Ein Fehler im Interrupt-Kontext ist schwer zu finden und trifft
dich bei diesem Projekt gleich an drei Stellen: Touch, DPI-Callbacks und der
geplante `notify_frame_buf_complete_from_isr`-Workaround.

**Suchmuster:** `IRAM_ATTR`; `ESP_LOG*` in ISR-Kontext; `malloc` in ISR;
`xQueueSend` statt `xQueueSendFromISR`; fehlendes
`portYIELD_FROM_ISR`; Zugriff auf Daten im Flash aus einer ISR, die waehrend
Flash-Operationen laufen muss; fehlendes `volatile` bei Variablen, die ISR und
Task teilen.

**Nachweis:** INSPEKTION fuer die meisten Punkte. GUARD fuer geteilte
Variablen: Zaehler in ISR und Task vergleichen und Abweichungen melden.

---

## F — DMA, Cache und Ausrichtung

**Warum:** Auf dem P4 mit PSRAM-Framebuffern und PPA sind falsch ausgerichtete
oder nicht zurueckgeschriebene Puffer eine typische Ursache fuer Bildfehler,
die wie Timing-Probleme aussehen.

**Suchmuster:** Puffer, die an `esp_lcd_panel_draw_bitmap`, die PPA oder DMA2D
gehen; `MALLOC_CAP_DMA`; Cache-Zeilengroesse gegen Pufferausrichtung
(`CONFIG_CACHE_L2_CACHE_LINE_128B`); fehlende Cache-Writeback- oder
Invalidate-Aufrufe bei PSRAM-Puffern; Stackpuffer als DMA-Quelle.

**Nachweis:** GUARD. Assert auf Ausrichtung und Capability jedes Puffers beim
Anlegen, nicht bei jedem Flush.

---

## G — Blockierendes im LVGL-Task

**Warum:** Direkte Ursache fuer die 61–80 ms Renderzeit, wenn dort etwas
wartet, das nicht warten muesste.

**Suchmuster:** `vTaskDelay`, `xSemaphoreTake` mit langem Timeout, I2C- oder
SPI-Transfers, Dateisystem- oder Flashzugriffe, Netzwerkaufrufe, Bilddekodierung
— jeweils innerhalb von `lv_timer_handler` oder in Widget-Callbacks.

**Nachweis:** GUARD. Zeitmessung um `lv_timer_handler()`, Assert oder Logzeile
bei Ueberschreitung eines Budgets. Ergaenzend eine Messung um die verdaechtige
Stelle selbst.

---

## H — Ressourcenlebenszyklus

**Warum:** Beim Screenwechsel nicht freigegebene Objekte, Timer und Handles
sind die haeufigste Ursache fuer „laeuft, aber nach zwei Stunden nicht mehr".

**Suchmuster:** `lv_obj_create` ohne zugehoeriges `lv_obj_del`;
`lv_timer_create`; `esp_timer_create`; geoeffnete Dateien und Sockets;
`lv_anim_start`; Event-Callbacks mit `user_data`, das alloziert wurde.

**Nachweis:** GUARD. Screenwechsel-Schleife im Selbsttest, danach Heap und
`lv_obj`-Zaehler gegen den Ausgangswert pruefen.

---

## I — Konfiguration gegen Code

**Warum:** Hartkodierte Werte, die eigentlich aus der Konfiguration kommen
muessten, machen jedes Experiment aus `PLAN.md` unzuverlaessig.

**Suchmuster:** die Zahlen `1024`, `600`, `52`, `50` als Literale; doppelt
gefuehrte Aufloesungskonstanten; Annahmen ueber `LV_COLOR_DEPTH`;
Pixelformat an mehreren Stellen definiert.

**Nachweis:** GUARD. Beim Start einmal die tatsaechlichen Werte aus Panel,
Adapter und LVGL nebeneinander loggen und auf Gleichheit pruefen.

---

## J — Testbarkeit

**Warum:** Logik, die in `main/` liegt, ist nicht host-testbar, und jeder
weitere Befund darin bekommt automatisch die Nachweisart INSPEKTION.

**Suchmuster:** Zustandsautomaten, Parser, Umrechnungen, Grenzwertlogik und
Fehlerentscheidungen in `main/` oder in Treiberdateien.

**Nachweis:** HOST. Fuer jede Fundstelle beschreibt der Agent, welche Funktion
nach `components/app_logic` gehoert und welche Tests danach moeglich waeren.

---

## K — Watchdog und lange Schleifen

**Suchmuster:** Schleifen ohne `vTaskDelay` oder Yield; `esp_task_wdt_reset`;
Init-Sequenzen mit vielen Wartezeiten; Busy-Wait auf Hardware-Flags ohne
Timeout — insbesondere im DSI-Pfad, wo genau das schon einmal den Boot haengen
liess.

**Nachweis:** GUARD. Jeder Busy-Wait bekommt ein Timeout und einen Fehlerpfad.

---

## L — Netzwerk und TLS

**Warum:** Der `-0x008D` aus diesem Projekt. TLS-Puffer konkurrieren mit
Zeichenpuffern im internen SRAM.

**Suchmuster:** `esp_tls`, `esp_http_client`, `esp_mqtt`; TLS-Sessions, die
nicht aufgeraeumt werden; parallele Verbindungen;
`CONFIG_MBEDTLS_SSL_*_CONTENT_LEN`; fehlender `*_cleanup`-Aufruf.

**Nachweis:** GUARD. Vor jedem Verbindungsaufbau den groessten freien
INTERNAL-Block loggen und unter einer Schwelle warnen.


---

 ## M -Ueberlauf — stille Bereichsueberschreitung in Zeit- und Groessenrechnungen

      **Worum es geht:** Arithmetik, die in einem zu schmalen Typ ausgefuehrt wird,
      bevor das Ergebnis in einen breiteren wandert. Kein Compiler-Fehler, keine
      Laufzeitmeldung — der Wert ist einfach falsch, und zwar meist zu klein.

      **Belegter Fall (2026-09-22):** `pdMS_TO_TICKS()` castet sein Argument **vor**
      der Multiplikation mit `configTICK_RATE_HZ` auf `TickType_t` (uint32). Der
      12-Stunden-Auto-Check in `main/app_weather.c` lief dadurch alle **4,17 Minuten**
      — auf Hardware gemessen, Abstaende 250/519/769/1020 s. Der Dialog versprach
      12 Stunden.

---

## Nachbereitung eines Durchlaufs

1. **Du triagierst.** Streich aus `review/audit-<klasse>-*.md`, was du nicht
   willst. Erst danach implementieren lassen.
2. Umsetzung mit Sonnet, ein Befund nach dem anderen:

   > Arbeite die verbliebenen Befunde in `review/audit-b-heap-2026-09-10.md`
   > ab, einen nach dem anderen. Baue zu jedem Befund den unter „Nachweis"
   > beschriebenen HOST-Test oder GUARD mit ein. Nach jedem Befund
   > `./scripts/host-test.sh` ausfuehren.

3. **Guards duerfen abschaltbar sein, aber nicht per Default aus.** Ein Guard,
   der im Auslieferungsbuild fehlt, faengt genau dann nichts, wenn es darauf
   ankommt. Wenn Laufzeitkosten stoeren, dann per Kconfig abstufen, nicht
   entfernen.
4. Der Abschnitt „Nicht beanstandet" aus dem Bericht bleibt stehen. Beim
   naechsten Durchlauf derselben Klasse liest der Agent ihn und wirft die
   Stellen nicht erneut auf.
