# Plan: LVGL-Stack und freier DMA-Speicher

Stand 2026-10-15, Branch `feat/screensaver`. Noch nichts davon umgesetzt.

## Ausgangslage

Der Bildschirmschoner brauchte einen größeren LVGL-Stack (8 KB überlief, jetzt
16 KB in `main/main.c`, `LVGL_TASK_STACK_BYTES`). Der Stack liegt im internen RAM,
den auch der SDIO-Link zum C6 für seine DMA-Puffer braucht
(`components/app_logic/include/heap_watch.h`).

| Messwert | Wert | Quelle |
|---|---|---|
| Warnschwelle (`LOW`) | 32 KiB frei / 8 KiB größter Block | `heap_watch.h` |
| Leerlauf bei 11 s, normale Firmware | `dma_free` ≈ 132 KB, `dma_min` ≈ 43 KB | `.logs/hw-saver6.log` |
| niedrigster Wert nach Refresh | `dma_min` ≈ 38 KB, nur ≈ 6 KB über der Warnschwelle | `.logs/hw-saver*.log` |
| Leerlauf bei 6,5 s mit 8-KB-Stack | `dma_free` 147 KB, `dma_min` 66 KB | erster Flash |
| Leerlauf bei 6,5 s mit 16-KB-Stack | `dma_free` 138 KB, `dma_min` 57 KB | `.logs/hw-saver6.log` |
| LVGL-Stack belegt | ≈ 8,2 KB (Reserve 8156 von 16384) | Probe in `main.c` |

Der Stack erklärt etwa 8 bis 9 KB der Differenz. Der Rest ist ungeklärt. Eine
Referenz vom Stand vor dem Schoner (`master`) liegt nicht vor.

## Schritt 1: Messen statt raten (ca. 10 Minuten)

1. **Referenz:** `master` in einem Worktree bauen, flashen und dieselben Phasen
   aus dem Log ablesen (Idle bei 6 s und 11 s, nach Prognose, nach Refresh).
   Zeigt, wie viel der Schoner insgesamt kostet.
2. **Stacks aller Tasks:** einmaliges Log mit `uxTaskGetStackHighWaterMark` für
   alle Tasks, um überdimensionierte Stacks zu finden. Die zwei LVGL-Zeichen-
   Threads haben je 8 KB (`CONFIG_LV_DRAW_THREAD_STACK_SIZE`).
3. **Schoner-Speicher:** prüfen, ob die vorgerechneten Bilder (Hintergrund,
   Käfig, Balkenschatten, Ziffern) im PSRAM liegen. Der benutzerdefinierte
   Allokator (`main/lv_mem_psram.c`) nimmt zuerst PSRAM, das Log zeigt
   `psram_free`.

## Schritt 2: Maßnahmen, in dieser Reihenfolge

**A. LVGL-Task-Stack in PSRAM** (größter Gewinn, geringster Aufwand)

- Der Adapter hat `stack_in_psram` (`esp_lv_adapter_config_t`), und
  `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y` ist schon gesetzt. Das gibt die
  16 KB im internen RAM komplett frei, also 8 KB mehr als vor dem Schoner.
- Risiken: PSRAM ist langsamer; ein Flash-Schreibzugriff aus diesem Task wäre
  verboten (der Testkanal-Schalter tat es doch, Audit B vom 2026-10-19; jetzt über den Timer-Task); DMA-Puffer auf dem
  Stack gehen nicht. Braucht einen Test auf der Hardware.
- Prüfung: HIL-Boot, Schoner-Zyklus, Touch-Latenz (`lvgl_stall`-Zeilen) mit und
  ohne die Änderung.

**B. Verlauf-Einstellung zurücknehmen** (wenn A nicht trägt oder der Stack klein
bleiben soll; nimmt die komplexen Verläufe zurück, die bewusst gewählt wurden,
daher nur mit Zustimmung)

- Den Balken ebenfalls als vorgerechnetes Bild backen, wie Käfig und Hintergrund.
  Das behebt auch das Banding. Danach können
  `CONFIG_LV_USE_DRAW_SW_COMPLEX_GRADIENTS` und `CONFIG_LV_GRADIENT_MAX_STOPS`
  zurück auf den Standard.
- Vermutung, nicht belegt: `MAX_STOPS=10` vergrößert jede Zeichenstruktur auf dem
  Stack und löste den Überlauf aus. Reicht der Stack danach mit 8 KB, spart das
  8 KB internen RAM, und die globale Konfig-Änderung entfällt.
- Prüfung: erst die Stack-Reserve messen, dann den Stack wieder verkleinern.
- Kosten: ein Vollbuild (`sdkconfig.h` ändert sich).

**C. Weitere Sparpunkte** (nur, wenn Schritt 1 sie zeigt): Zeichen-Threads von
8 KB auf das Gemessene kürzen, eventuell ein Zeichen-Thread weniger.

## Abnahme

- `dma_min` im Dauerlauf (Refresh, Schoner an, Einstellungen öffnen, OTA-Check)
  bleibt deutlich über 32 KiB, Ziel ≥ 50 KB.
- Kein `Stack protection fault`; `lvgl_stall` nicht schlechter als jetzt.
- `./scripts/hw-flash.sh` mit `REQUIRE_IP=1` grün und ein Lauf von
  `./scripts/hil-outage-test.sh` unauffällig (die WLAN-Anbindung hängt am
  DMA-Speicher).

## Reihenfolge und Aufwand

Schritt 1, dann A, bei Bedarf B. Alles zusammen etwa 30 bis 45 Minuten mit
mehreren Flash-Zyklen.
