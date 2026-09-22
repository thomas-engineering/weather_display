#ifndef HEAP_WATCH_H
#define HEAP_WATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Hardware-free "is there still DMA-capable memory left" reporting policy. No
 * IDF headers, no driver/, no freertos/ — see CLAUDE.md's "Codeorganisation".
 * main/app_heap_probe.c samples the heap and emits the lines; when a line is
 * worth emitting is decided here so it can be tested without a board.
 *
 * Why this exists: on this board every SDIO packet to and from the ESP32-C6
 * is a separate heap_caps_malloc(1536, MALLOC_CAP_DMA) — see
 * review/ota-sdio-buffer-2026-09-22.md. PSRAM is not DMA-capable on the P4,
 * so those allocations compete with the whole application for the same ~384
 * KiB of internal RAM, and unlike an ordinary malloc they cannot fall back
 * to PSRAM: they simply fail, the packet is dropped, and TCP (or an RPC call
 * to the co-processor) pays for it. The two numbers that decide whether that
 * happens are the free DMA-capable bytes and the largest contiguous block —
 * a heap with 100 KiB free in 1 KiB fragments still cannot hand out a
 * 1536-byte buffer, so both are tracked separately.
 *
 * Reporting rules: both edges (entering and leaving the low state) always
 * produce a line, so an episode has a start and an end in the log; inside an
 * episode lines are rate-limited; and a new all-time minimum that is
 * meaningfully below the last reported one produces a line as well, so a slow
 * drift between two periodic reports is not invisible. */

typedef struct {
    size_t dma_free;     /* free bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL */
    size_t dma_largest;  /* largest free block, same caps */
    size_t dma_min_free; /* low-water mark since boot, same caps */
    size_t psram_free;   /* diagnostics only, never part of a decision */
} heap_watch_sample_t;

typedef enum {
    HEAP_WATCH_SILENT,  /* nothing to say right now */
    HEAP_WATCH_REPORT,  /* routine line */
    HEAP_WATCH_LOW,     /* headroom below what the SDIO transport needs */
} heap_watch_level_t;

typedef struct {
    /* thresholds */
    size_t low_free;
    size_t low_largest;
    int64_t report_period_ms;
    int64_t low_period_ms;
    size_t new_min_margin;
    /* state */
    bool started;
    bool low;
    int64_t last_report_ms;
    size_t reported_min_free;
    size_t reported_min_largest;
    /* diagnostics */
    int episodes;       /* how many times the low state was entered */
} heap_watch_t;

/* One SDIO buffer is 1536 bytes; a link that cannot hold several of those in
 * flight is already dropping packets, so the warning has to come well before
 * that. 32 KiB free / 8 KiB largest is roughly "twenty buffers away". */
#define HEAP_WATCH_LOW_FREE        (32u * 1024u)
#define HEAP_WATCH_LOW_LARGEST     (8u * 1024u)
#define HEAP_WATCH_REPORT_PERIOD_MS 30000
#define HEAP_WATCH_LOW_PERIOD_MS    2000
#define HEAP_WATCH_NEW_MIN_MARGIN   4096

/* Non-positive periods and zero thresholds fall back to the defaults above. */
void heap_watch_init(heap_watch_t *w, size_t low_free, size_t low_largest,
                     int64_t report_period_ms, int64_t low_period_ms);

/* Call as often as convenient — the policy does the rate limiting. */
heap_watch_level_t heap_watch_observe(heap_watch_t *w, const heap_watch_sample_t *s,
                                      int64_t now_ms);

/* True while the last observed sample was below either threshold. */
bool heap_watch_is_low(const heap_watch_t *w);

#endif
