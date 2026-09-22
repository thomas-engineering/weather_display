# Lokale Patches gegen `managed_components/`

`managed_components/` steht in `.gitignore` — Aenderungen dort ueberleben
weder einen Clone noch ein `idf.py reconfigure` mit neu aufgeloesten
Abhaengigkeiten. Was dort trotzdem noetig ist, liegt hier als Patch und wird
von Hand angewandt:

    patch -p1 < patches/<name>.patch

| Patch | Zweck |
|---|---|
| `esp_hosted_sdio_reserve.patch` | Drei Dinge am SDIO-Transport: (1) Diagnose — die `mempool OOM`-Zeilen bekommen Zahlen (freies DMA-RAM, groesster freier Block) und Drop-Zaehler pro Richtung; (2) ein einmalig reservierter Pufferblock (8 x 1536 B), aus dem der Transport primaer bedient wird, mit dem Heap als Overflow; (3) begrenztes Warten statt sofortigem Verwerfen, wenn kein Puffer da ist — auf der Kontroll-Queue (RPC zum C6) deutlich laenger als auf den Daten-Queues. Hintergrund und Messwerte: `review/ota-sdio-buffer-2026-09-22.md`. |

Vor dem Anwenden pruefen, ob der Patch noch passt — laeuft er ins Leere,
hat Espressif die Stelle geaendert, und dann ist die Frage, ob der Patch
ueberhaupt noch gebraucht wird.
