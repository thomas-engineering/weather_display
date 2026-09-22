#ifndef APP_HEAP_PROBE_H
#define APP_HEAP_PROBE_H

/* Heap instrumentation for the DMA-capable internal RAM the ESP-Hosted SDIO
 * link runs on. Adapter around components/app_logic/heap_watch.c: this file
 * reads heap_caps_*, the policy decides when a line is worth emitting.
 *
 * Background in review/ota-sdio-buffer-2026-09-22.md. Short version: every
 * SDIO packet to and from the C6 is its own heap_caps_malloc(1536,
 * MALLOC_CAP_DMA), PSRAM is not DMA-capable on the P4, and a failed one is
 * silently dropped — so "how much DMA-capable memory is left" is the number
 * that decides whether the Wi-Fi link works, and until now nothing in this
 * firmware ever printed it.
 *
 * Safe to call from any task: sampling is non-blocking and two callers
 * racing simply means one of them skips its sample. */

void app_heap_probe_init(void);

/* Rate-limited by the policy. `phase` is a short tag for the log line
 * ("idle", "ota"), so a line can be traced back to what was running. */
void app_heap_probe_tick(const char *phase);

/* Unconditional single line, for milestones worth pinning down exactly
 * (boot, OTA start/end) regardless of what the rate limiter thinks. */
void app_heap_probe_log_now(const char *phase);

#endif
