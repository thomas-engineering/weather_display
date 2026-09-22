# OTA-Abbruch durch SDIO-Puffermangel zum C6 — 2026-09-22

Ausgangspunkt: der fehlgeschlagene manuelle P4-OTA 0.0.3 -> 0.0.4 und der
darauf folgende RPC-Wedge (`.logs/ota-download-failure-sdio-oom-20260922.log`,
`.logs/rpc-wedge-after-failed-ota-20260922.log`). Offener Punkt aus dem
WLAN-Stall-Review: "Pufferpool-Dimensionierung pruefen".

Geprueft: `managed_components/espressif__esp_hosted/` (SDIO-Transport und
Port-Layer), `sdkconfig` (`SPIRAM_*`, `MBEDTLS_*`, `ESP_HOSTED_*`),
`main/ota_update.c`, `main/app_weather.c`, sowie die Heap-Registrierung in
ESP-IDF 6.1 (`components/heap/port/esp32p4/memory_layout.c`,
`components/esp_psram/system_layer/esp_psram.c`,
`components/heap/heap_caps.c`).

Ergebnis: **eine** Ursachenkette, aus der sowohl der abgebrochene Download
als auch der RPC-Wedge folgen. Aus Code, Konfiguration und Log abgeleitet,
am Geraet noch nicht gemessen — Stufe 0 unten schliesst genau diese Luecke.

## Die Kette

### 1. Es gibt keinen SDIO-Pufferpool

`eh_host_bus_sdio.c:306` enthaelt zwar `sdio_mempool_create()`, aber das
gesamte Pool-Konstrukt haengt an `#if EH_HOST_USE_MEMPOOL`. Dieses Makro ist
**nirgends definiert** — weder in einem Kconfig, noch in CMake, noch in
`build/config/sdkconfig.h`. Es evaluiert zu 0, der Pool-Code ist tot.

Kompiliert wird der `#else`-Zweig in `sdio_buffer_alloc()`
(`eh_host_bus_sdio.c:334`):

    void *p = eh_host_port_dma_alloc(MAX_SDIO_BUFFER_SIZE);   /* 1536 B */

also `heap_caps_malloc(1536, MALLOC_CAP_DMA)`
(`port/os/idf/src/eh_host_port_dma.c:12`) — **pro Paket, in beide
Richtungen**, aus dem normalen Heap.

`mempool OOM start (TX)` im Log heisst damit nicht "Pool leer", sondern:
*es waren keine 1536 zusammenhaengenden Bytes DMA-faehiges internes RAM zu
bekommen*. Im OTA-Log 9-13 s am Stueck, im Zyklus von ~20 s.

(Nebenbefund: selbst mit gesetztem Makro wuerde es nicht laufen —
`hosted_mempool_create()` gibt ohne `CONFIG_ESP_CACHE_MALLOC` NULL zurueck
und `sdio_mempool_create()` haengt an einem `assert`; ausserdem passt die
Aufrufsignatur `hosted_mempool_create(&config)` in `:318` nicht zur
Definition in `common/eh_mempool/src/eh_mempool.c:12`.)

### 2. Warum das interne RAM leer ist

Auf dem P4 ist PSRAM **nicht** `MALLOC_CAP_DMA`: `esp_psram.c:528`
registriert die Regionen als `SPIRAM|DEFAULT|8BIT|32BIT|SIMD`, DMA-faehig
ist nur L2MEM (`memory_layout.c:48`). Fuer DMA stehen also die ~384 KiB aus
`heap_init` zur Verfuegung (110 KiB RETENT_RAM + 18 KiB + 256 KiB, siehe
`.logs/hw.log`).

Und `sdkconfig:2367` steht auf dem IDF-Default:

    CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384

In `heap_caps_malloc_default()` (`heap_caps.c:117`) bedeutet das: **jedes
malloc <= 16 KB geht zuerst ins interne RAM**, PSRAM nur als Notnagel, wenn
intern nichts mehr frei ist. lwip-pbufs, die vielen mbedtls-Kleinpuffer, die
4-KB-HTTP-Puffer, cJSON-Baeume — alles draengt in genau den Pool, aus dem
der SDIO-Treiber seine DMA-Puffer zieht.

Der entscheidende Unterschied: der normale Heap **weicht bei Vollstand
still nach PSRAM aus**. `heap_caps_malloc(..., MALLOC_CAP_DMA)` hat diesen
Ausweg nicht und scheitert hart. Der Link verliert das Rennen gegen den
Rest des Systems, und zwar immer.

Dazu `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC is not set` (`sdkconfig:3593`) bei
`CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384` — genau die Hypothese, die seit
den intermittierenden TLS-Fehlern geparkt war.

### 3. Warum daraus ein Abbruch wird

Bei OOM wird nicht gebremst, sondern **verworfen**:

| Stelle | Verhalten bei OOM |
|---|---|
| `eh_host_bus_sdio.c:1384` (RX) | Paket wird uebersprungen, Stream laeuft weiter |
| `eh_host_bus_sdio.c:979` (TX) | Segment wird verworfen (`goto done`) |
| `eh_host_bus_sdio.c:1329` | ganzer Read-Block faellt aus (`dma_alloc(2560) failed`) |

Verworfene ACKs -> TCP-RTO -> Durchsatz bricht ein. Im Log passt das exakt:
das **Gesamtbudget von 600 s** wurde gerissen, das Stall-Budget von 60 s
(`main/ota_update.c:67-68`) dagegen nie — der Download lief also die ganze
Zeit, nur im Schneckentempo. Am Ende kippte die TLS-Sitzung
(`esp-tls-mbedtls: read error :-0x004C`).

### 4. Warum daraus der RPC-Wedge wird

Dieselbe TX-Queue traegt die **RPC-Requests zum C6**. Ein dort verworfener
Request wird nie beantwortet -> `eh_host_feat_rpc: request: no response` ->
`esp_wifi_connect refused: ESP_FAIL` in Endlosschleife, jede DNS-Aufloesung
scheitert identisch (`getaddrinfo() returns 202`), Power-Cycle noetig.

Damit sind der Puffermangel und der RPC-Wedge **nicht zwei Befunde, sondern
Ursache und Folge**. Das stuetzt auch die Arbeitshypothese aus dem
WLAN-Review ("schwere SDIO-Last loest den Wedge aus") und erklaert den
ersten Fall (schnelle Reconnect-Retries im HIL-Outage-Test) mit.

### 5. Chronisch, nicht OTA-spezifisch

Das OOM-Zyklen steht ab der ersten erfassten Logzeile (t=160 s) im Log, der
OTA-Download beginnt erst bei t=665 s. Der Puffermangel ist der
Normalzustand dieses Boards; der Download kippt ihn nur endgueltig.

Nicht die Ursache, entgegen erster Vermutung: Task-Prioritaeten. Die
OTA-Tasks laufen auf Prio 3 (`app_weather.c:1006`, `:1019`), die
SDIO-Tasks auf 22 (`CONFIG_ESP_HOSTED_HOST_DEFLT_TASK_PRIORITY`).

## Massnahmen

| Stufe | Inhalt | Art | Status |
|---|---|---|---|
| 0 | Heap-Instrumentierung (es gab bisher *keine* im Projekt) | Code | umgesetzt 2026-09-22 |
| 1 | `SPIRAM_MALLOC_ALWAYSINTERNAL`, `MBEDTLS_EXTERNAL_MEM_ALLOC`, `SPIRAM_MALLOC_RESERVE_INTERNAL` | Config | umgesetzt 2026-09-22 |
| 2 | Reservierter SDIO-Pufferblock statt reiner Per-Paket-Allokation | Code | umgesetzt 2026-09-22 |
| 3 | Backpressure statt Drop bei OOM, Drops zaehlen, Kontroll-Queue bevorzugt | Code | umgesetzt 2026-09-22 |
| 4 | RPC-Wedge-Guard: anhaltende Transportfehler -> C6-Reset per Neustart | Code | umgesetzt 2026-09-22 |

Alle fuenf Stufen sind damit im Code und auf dem Board. Was fehlt, ist in
jedem Fall dasselbe: ein Release neuer als die installierte Version, um den
Lastfall ueberhaupt auszuloesen. Bis dahin ist nur belegt, dass nichts
kaputtgegangen ist und dass der Leerlauf gesund aussieht.

### Stufe 0 — messen

`components/app_logic/heap_watch.c` entscheidet hostgetestet, *wann* eine
Heap-Zeile faellig ist (Schwellen, Flankenwechsel, Ratenbegrenzung, neue
Tiefstwerte); `main/app_heap_probe.c` ist der Adapter, der `heap_caps_*`
abfragt und loggt. Aufgerufen aus dem 5-s-Heartbeat-Timer in `main.c` und
zusaetzlich aus der OTA-Download-Schleife, damit genau waehrend der
kritischen Phase dicht abgetastet wird.

Logzeile, greppbar:

    heap: dma_free=... dma_largest=... dma_min=... psram_free=...

Dazu ein **lokaler Patch** an `managed_components/` (nicht in git,
`.gitignore:2`), der den OOM-Zeilen Zahlen und Drop-Zaehler mitgibt. Er
liegt als `patches/esp_hosted_sdio_oom_diag.patch` im Repo und muss nach
jedem `idf.py reconfigure` mit neu aufgeloesten Abhaengigkeiten erneut
angewandt werden — sonst sagen die OOM-Zeilen wieder nichts ueber die Lage.

### Stufe 1 — Luft schaffen

| Option | vorher | nachher | Wirkung |
|---|---|---|---|
| `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` | 16384 | 1024 | groesster Hebel: Alltags-Heap (u.a. lwip-pbufs ~1.5 KB) wandert ins PSRAM, intern bleibt Platz fuer DMA |
| `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC` | nicht gesetzt | y | mbedtls-Sitzungspuffer explizit ins PSRAM (`SPIRAM_USE_MALLOC=y` erfuellt die Kconfig-Abhaengigkeit) |
| `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL` | 32768 | 65536 | DMA-Polster; wirkt allerdings **nur zusammen mit** Zeile 1, weil die Reserve-Region an Prio 2 haengt und normaler malloc sie sonst trotzdem abgreift |

Der Schwellwert 1024 ist bewusst unter der typischen pbuf-Groesse gewaehlt.
Das ist auf diesem Board unbedenklich, weil der WLAN-Treiber auf dem C6
laeuft: der Host braucht internes DMA-RAM nur fuer die SDIO-Puffer selbst,
in die lwip ohnehin umkopiert wird.

Beide Stufen zusammen sind die Voraussetzung dafuer, Stufe 2-4 ueberhaupt
bewerten zu koennen — ohne Zahlen laesst sich nicht unterscheiden, ob das
interne RAM erschoepft oder nur fragmentiert war.

## Testlage

Emulator und Simulator decken SDIO/ESP-Hosted nicht ab. Verifizierbar ist
das nur per `./scripts/hw-flash.sh` plus einem echten Download gegen ein
Release, das neuer ist als die installierte Version. Damit haengt dieser
Punkt am selben fehlenden Fixture wie die drei offenen OTA-Punkte
(C6-Pfad ungetestet, Cancel/Idle-Wait/Nebenlaeufigkeit unbeobachtet).

## Erste Messung auf Hardware — 2026-09-22, Stufe 0+1 aktiv

`./scripts/hw-flash.sh /dev/ttyACM0` mit `MONITOR_SECONDS=80`, installierte
Version 0.0.4, Log in `.logs/hw.log`. Board bootet sauber, WLAN oben,
Forecast geholt.

| Zeitpunkt | dma_free | dma_largest | dma_min |
|---|---|---|---|
| Boot (vor WLAN) | 177 067 | 65 536 | 112 336 |
| 6,5 s (SDIO/WLAN oben) | 74 535 | 43 008 | 44 788 |
| 11,5 s (nach erstem TLS-Fetch) | 62 439 | 43 008 | 34 212 |
| 41,5 s / 71,5 s (Leerlauf) | 62 435 / 62 439 | 43 008 | 34 212 |

Lesart: allein das Hochfahren des SDIO-Links und ein einziger
TLS-Forecast-Abruf kosten **115 KiB** des DMA-faehigen internen RAMs. Im
Leerlauf bleiben ~62 KiB frei, groesster Block 42 KiB — also rund 40
SDIO-Puffer Luft. Der Tiefstwert seit Boot liegt bei 34 KiB und stammt aus
dem ersten TLS-Handshake; das ist bereits nahe an der Warnschwelle von
32 KiB. Keine einzige `mempool OOM`-Zeile in 80 s.

Was diese Messung **nicht** sagt: ob Stufe 1 etwas gebracht hat. Es gibt
keinen Vergleichswert aus dem alten Build, weil die Instrumentierung vorher
nicht existierte — und der Lastfall, der das Problem ausloest (der mehrere
MB grosse OTA-Download), laesst sich nicht ausloesen, solange kein Release
neuer als die installierte 0.0.4 existiert. Der Vorher-Nachher-Vergleich
haengt damit am selben fehlenden Fixture wie alles andere an der
OTA-Funktion.

Nebenbefund aus demselben Log, fuer den C6-OTA-Pfad relevant:

    E (2985) eh_init_evt: major version mismatch — OTA coprocessor from host
    esp-hosted fw versions: host=3.0.7 coprocessor=2.6.7

esp-hosted selbst haelt die C6-Firmware fuer eine Hauptversion zu alt und
fordert genau das Update an, dessen Pfad noch nie gelaufen ist.

## Stufe 2 und 3 — 2026-09-22

Beides in `patches/esp_hosted_sdio_reserve.patch` (ersetzt den reinen
Diagnose-Patch von vorhin), auf Hardware gebaut, geflasht und im Leerlauf
beobachtet.

**Stufe 2, reservierter Pufferblock.** Ein Slab von `SDIO_RESERVE_BLOCKS`
Puffern wird einmalig beim Transport-Init geholt — bevor die Anwendung das
interne RAM fuellen kann — und dient dem Transport als *primaerer*
Allokator; der Heap bleibt der Overflow-Pfad fuer tiefere Bursts. Der Slab
wird nie freigegeben: ein an eine obere Schicht gereichter Puffer kann lange
nach einem Transport-Neustart zurueckkommen (genau das macht
`esp_hosted_deinit()` im C6-OTA), und eine Reserve, die den Neustart
ueberlebt, ist sicherer als eine, die aus einem womoeglich vollen Heap neu
aufgebaut werden muss.

Die Reihenfolge war die eigentliche Entscheidung, und die erste Fassung lag
daneben. Zuerst implementiert: Heap zuerst, Slab nur als letzte Rettung. Das
misst sich schlechter — die Pakete zirkulieren weiter durch den allgemeinen
Heap, und der Slab ist aus demselben Vorrat herausgeschnitten, den der
TLS-Handshake braucht. Danach umgedreht.

Die Groesse ebenfalls per Messung korrigiert:

| Variante | dma_free (Leerlauf) | dma_largest | dma_min |
|---|---|---|---|
| ohne Slab (nur Stufe 1) | 62 439 | 43 008 | 34 212 |
| 16 Bloecke, Heap zuerst | 39 731 | 19 456 | 11 504 |
| 16 Bloecke, Slab zuerst | 37 739 | 19 456 | 8 440 |
| **8 Bloecke, Slab zuerst** | **52 055** | **31 744** | **23 792** |

16 Bloecke (24 KiB) liessen im schlechtesten Moment — dem ersten
TLS-Handshake — nur noch 8-11 KiB fuer alles andere uebrig. Das ist duenner
als der 2560-Byte-Staging-Puffer, der im Fehlerlog schon einmal nicht
alloziert werden konnte. 8 Bloecke (12 KiB) lassen ~24 KiB stehen und decken
den Zweck weiterhin ab: RPC-Request samt Antwort und ein paar Frames, keine
Dauerlast. Tiefere Bursts laufen wie bisher ueber den Heap, jetzt mit
Zaehler (`overflows`) und einmaliger Warnung, damit die naechste Messung
sagt, ob 8 reicht.

**Stufe 3, Backpressure statt Drop.** Findet der Transport keinen Puffer,
wartet er jetzt begrenzt, statt das Paket sofort zu verwerfen: TX 3 x 10 ms
auf den Daten-Queues, **20 x 10 ms auf der Kontroll-Queue**, RX 2 x 5 ms.
Die Asymmetrie ist der Kern: ein verworfenes TCP-Segment holt die Gegenseite
per Retransmit zurueck, ein verworfener RPC-Request zum C6 dagegen nie — der
Host wartet 5 s auf eine Antwort, die nicht mehr kommen kann, und wiederholt
das endlos. Genau das war der Wedge. Schlafen ist hier der Zweck, nicht ein
Nebeneffekt: die Transport-Tasks laufen auf Prioritaet 22, das Abgeben der
CPU ist das, was die speicherhaltenden Tasks ueberhaupt laufen laesst. An
beiden Aufrufstellen wird kein Bus-Lock gehalten.

## Stufe 4 — RPC-Wedge-Guard, 2026-09-22

`components/app_logic/coprocessor_health_policy.c` (hostgetestet, 10 Tests),
Adapter in `main/app_wifi.c`. Dritte und letzte Schicht ueber
`wifi_reconnect_policy` (der Treiber meldet ein Problem) und
`link_health_policy` (der Treiber meldet nichts, aber nichts funktioniert):
**die Aufrufe, mit denen die beiden anderen nachfragen oder reparieren
wuerden, kommen selbst nicht zurueck.**

Das unterscheidende Merkmal ist schmal und muss es bleiben: bei einem
normalen Ausfall — AP aus, ausser Reichweite, Router-Neustart — *gelingen*
die RPC-Aufrufe, und die Assoziation scheitert ueber Events, nicht ueber
Rueckgabewerte. Nur ein transportfoermiger Fehler (`ESP_FAIL`,
`ESP_ERR_TIMEOUT`) zaehlt hier; alles andere ist kein Beleg und wird in
keine Richtung gewertet.

Da die Massnahme ein Neustart ist, sind die Schwellen bewusst traege:
5 aufeinanderfolgende Fehler, mindestens 2 Minuten anhaltend, 10 Minuten
Abstand zwischen zwei Massnahmen, in den ersten 10 Minuten Uptime gar nicht,
und nach 3 Massnahmen ohne bleibenden Erfolg gibt das Geraet auf und bleibt
oben, damit der Fehler sichtbar ist statt in einer Neustartschleife zu
verschwinden. Der Zaehler liegt in NVS (`wifi/cprec`) — die Massnahme ist ein
Neustart, ein Zaehler im RAM waere danach wieder null. Ein laufendes Update
hat Vorrang: waehrend `HB_OTA`/`HB_OTA_RESUME` aktiv sind, wird nicht
neugestartet.

Warum ein P4-Neustart und nicht `esp_hosted_deinit()`/`_init()`: der Neustart
ist der einzige C6-Reset, den dieses Board nachweislich zuverlaessig
ausfuehrt — jeder Boot zieht dessen Reset-Leitung
(`eh_sdio: Reset co-processor using GPIO[54]`). `esp_hosted_deinit()` redet
mit dem Coprozessor, also genau mit dem, was in diesem Moment nicht
funktioniert; ein unbeaufsichtigter Wiederherstellungspfad, der blockieren
kann, ist schlechter als keiner.

**Ungetestet im Fehlerfall.** Verifiziert ist nur, dass nichts faelschlich
ausloest: 90 s Betrieb inklusive Boot, Verbindungsaufbau und
`esp_wifi_connect refused: ESP_ERR_WIFI_SSID` ohne eine einzige
`coprocessor`-Zeile. Der Wedge selbst laesst sich nicht auf Kommando
herbeifuehren; die einzige bekannte Methode, es zu versuchen, ist ein
grosser OTA-Download — und der braucht wieder ein Release neuer als die
installierte Version.

## Der Beweis — A/B auf Hardware, 2026-09-22

Release v0.0.5 aus dem Branch `ota-sdio-buffer` gebaut (der Patch-Schritt im
Workflow greift, das `grep` auf die Binary bestaetigt ihn im Image). Danach
zwei Durchlaeufe von je 20 Minuten, identische Bedingungen: dieselbe
Firmware-Version 0.0.4 auf dem Board, derselbe Auto-Check, der sich von
selbst meldet. Logs: `.logs/ota-baseline-prefix-20260922.log` und
`.logs/ota-fixed-20260922.log`.

| | Baseline (vor Stufe 1-3) | mit Stufe 1-3 |
|---|---|---|
| Verbindung zu api.github.com | Timeout, 4x in Folge | steht |
| `dma_free` waehrend des Handshakes | 4 319 | 43 083 |
| groesster freier Block | 1 408 | 31 744 |
| Tiefstwert ueber den Lauf | 76 | 23 712 |
| `mempool OOM`-Zeilen | Dauerzyklus | 0 |
| Download | kam nie zustande | 30,5 s fuer 2,1 MB (~69 KB/s) |
| Ergebnis | nichts | 0.0.4 -> 0.0.5 geflasht, Reboot, laeuft |

Der Baseline-Lauf korrigiert die urspruengliche Analyse in einem Punkt: es
ist nicht erst der grosse Download, der den Speicher erschoepft. Schon der
**TLS-Handshake allein** drueckt das DMA-faehige interne RAM auf 4,3 KiB und
den groessten Block auf 1 408 Byte — also unter die 1 536, die ein
SDIO-Puffer braucht. Deshalb beginnt exakt mit dem Handshake das
`mempool OOM (RX)`: der Link kann keine Pakete mehr annehmen, und der
Handshake scheitert daran, dass keine Pakete mehr ankommen. Das Geraet
konnte auf diesem Stand nicht einmal *pruefen*, ob ein Update existiert.
Dass der gescheiterte Versuch vom Mittag ueberhaupt bis zum Download kam,
war die guenstigere Haelfte eines Muenzwurfs.

Im Fix-Lauf blieb `dma_min` waehrend des gesamten Downloads bei 23 712 — der
Tiefstwert stammt noch vom Handshake davor, der Download hat ihn nicht
einmal beruehrt. Der Reserve-Pool wurde nie angefasst (keine
`spilling to the heap`-Warnung), 8 Bloecke reichen also mit Abstand. Damit
ist auch Stufe 3 im Fehlerfall nicht belegt: es gab keinen Fehlerfall mehr.

## Der C6-Pfad ist zum ersten Mal gelaufen — und scheitert am Abschluss

Der Boot nach dem erfolgreichen P4-Update hat den nie getesteten
Coprozessor-Pfad ausgefuehrt (der Schalter war an):

    I (12769) ota_update: first boot after a P4 update; ... checking coprocessor
    I (18172) heap: ota c6 begin: dma_free=38647 dma_largest=26624
    E (25838) ota_update: esp_hosted_slave_ota_end failed
    E (25839) ota_update: C6 update failed; keeping the coprocessor's current firmware

**Diese Stelle enthielt zunaechst eine falsche Schlussfolgerung**, hier
korrigiert stehen gelassen, weil der Irrtum lehrreich ist: notiert war, das
Image sei vollstaendig uebertragen worden und habe gegen den Manifest-Hash
gestimmt. Belegt war das nicht. `hash_ok` bedeutete nur, dass
`psa_hash_finish()` erfolgreich war — der Vergleich gegen
`expected_sha256` stand **hinter** `esp_hosted_slave_ota_end()` und lief
deshalb nie. Ueber den Zustand des Images sagte der Fehlschlag damit gar
nichts.

Und diese Reihenfolge war selbst ein Fehler: die Verifikation lag hinter
dem Finalisieren, war also wirkungslos, und ein abgeschnittenes Image wurde
dem Coprozessor als vollstaendig uebergeben. Dazu brach die
Download-Schleife bei jedem Null-Byte-Read als sauberem Dateiende ab, ohne
die Gesamtmenge je gegen `Content-Length` zu pruefen. Beides behoben:
Laengen- und Hash-Pruefung laufen jetzt vor `end()`, ein zu kurzer Body
wird erkannt und das Image gar nicht erst finalisiert.

Und es ist **gutartig** gescheitert: der Coprozessor behielt 2.6.7, das
Board blieb online, Wetterabruf und der naechste Update-Check danach liefen
normal. Genau das Risiko, das vorher als "koennte das Board ohne WLAN
zuruecklassen" markiert war, ist eingetreten und hat sich harmlos verhalten.

Offen: warum `esp_hosted_slave_ota_end()` fehlschlaegt. Kandidaten, die noch
niemand geprueft hat — eine Groessen-/Partitionsgrenze auf dem C6, ein
RPC-Timeout im Abschluss (der laenger dauern kann als die uebrigen Aufrufe),
oder eine Inkompatibilitaet zwischen dem 2.6.7-Slave und einem
3.0.7-Image. Der Rueckgabewert wird bisher nicht ausgewertet, nur auf
"!= ESP_OK" geprueft; ihn zu loggen waere der erste Schritt.

## Nebenbefund: der 12-Stunden-Check laeuft alle 4,2 Minuten

`main/app_weather.c:1072` vergleicht gegen
`pdMS_TO_TICKS(OTA_AUTO_CHECK_INTERVAL_MS)`. Das Makro castet **vor** der
Multiplikation auf `TickType_t` (uint32):

    43.200.000 ms x 1000 Hz = 43.200.000.000 -> als uint32: 250.327.040
    -> / 1000 = 250.327 Ticks = 4,17 Minuten

Der Hinweistext im Dialog verspricht 12 Stunden. Beide Messlaeufe zeigen den
Check sauber alle ~250 s (250, 519, 769, 1020). Das erklaert auch, warum der
gescheiterte Versuch vom Mittag als `silent OTA auto-check` bei 21 Minuten
Uptime im Log steht. Noch nicht behoben — waehrend der Messungen war es die
einzige Moeglichkeit, einen Download ohne Fingertipp am Geraet auszuloesen.

## C6, zweiter Anlauf mit Diagnose — 2026-09-22

Release v0.0.6 gebaut, weil der C6-Pfad beim ersten Boot **nach** dem
P4-Update laeuft: die Diagnose muss im Image stecken, das installiert wird,
nicht in dem, das installiert. Ergebnis:

    I (27108) ota_update: C6 image streamed: 1193600 of 1193600 bytes
    E (27122) ota_update: esp_hosted_slave_ota_end failed on a verified
              image: ESP_ERR_OTA_VALIDATE_FAILED (0x1503)

Damit ist die Abschneide-Hypothese erledigt: 1 193 600 von 1 193 600 Byte,
exakt die Asset-Groesse, und der Manifest-Hash stimmt — das Erreichen
dieser Zeile beweist beides, weil beide Pruefungen jetzt davor liegen. Was
zum Coprozessor geht, ist vollstaendig und korrekt. Der C6 lehnt es in
seiner eigenen `esp_ota_end()`-Verifikation ab.

Offen, zwei Hypothesen, keine geprueft:

1. **Das Image ist fuer diesen C6 ungueltig.** Gebaut aus
   `examples/network_split/station/cp` von esp-hosted-mcu v3.0.7, waehrend
   der Coprozessor 2.6.7 faehrt und esp_hosted bei jedem Boot
   `major version mismatch — OTA coprocessor from host` meldet. Der Host
   hat ausserdem `CONFIG_ESP_HOSTED_HOST_FEAT_NW_SPLIT` gar nicht gesetzt —
   eine Network-Split-Coprozessor-Firmware koennte schlicht die falsche
   Anwendung sein.
2. **Was der Slave speichert, ist nicht was wir senden.** Unser Hash deckt
   die HTTP-Bytes ab, nie den Flash des C6. Ein Defekt im RPC-Schreibpfad —
   2.6.7-Slave gegen einen Host, der RPC ext v2 spricht — saehe exakt so
   aus: jeder Write meldet OK, unsere Byte-Bilanz stimmt, die Verifikation
   auf der anderen Seite scheitert. Von hier aus ist der Unterschied nicht
   sichtbar.

Naechster Schritt morgen: v1-gegen-v2-Unterschiede im OTA-Write-Pfad lesen,
`EH_RPC_OTA_CHUNK_MAX` gegen die 1536 Byte `C6_OTA_CHUNK_SIZE` pruefen, und
den C6 einmal direkt flashen, um Hypothese 1 aus der Gleichung zu nehmen.

Nicht vergessen: der Fehlschlag ist gutartig. Der Coprozessor behaelt seine
Firmware, das Board bleibt online.
