#include "heap_watch.h"

void heap_watch_init(heap_watch_t *w, size_t low_free, size_t low_largest,
                     int64_t report_period_ms, int64_t low_period_ms) {
    if (!w) return;
    w->low_free = low_free ? low_free : HEAP_WATCH_LOW_FREE;
    w->low_largest = low_largest ? low_largest : HEAP_WATCH_LOW_LARGEST;
    w->report_period_ms = report_period_ms > 0 ? report_period_ms : HEAP_WATCH_REPORT_PERIOD_MS;
    w->low_period_ms = low_period_ms > 0 ? low_period_ms : HEAP_WATCH_LOW_PERIOD_MS;
    w->new_min_margin = HEAP_WATCH_NEW_MIN_MARGIN;
    w->started = false;
    w->low = false;
    w->last_report_ms = 0;
    w->reported_min_free = (size_t)-1;
    w->reported_min_largest = (size_t)-1;
    w->episodes = 0;
}

bool heap_watch_is_low(const heap_watch_t *w) {
    return w && w->low;
}

/* A drop of at least new_min_margin below the lowest value already reported.
 * Guards the subtraction against the initial (size_t)-1 sentinel, which is
 * deliberately larger than any real sample. */
static bool is_new_low(size_t value, size_t reported, size_t margin) {
    if (reported == (size_t)-1) return false;
    return value + margin <= reported;
}

heap_watch_level_t heap_watch_observe(heap_watch_t *w, const heap_watch_sample_t *s,
                                      int64_t now_ms) {
    if (!w || !s) return HEAP_WATCH_SILENT;

    bool low = (s->dma_free < w->low_free) || (s->dma_largest < w->low_largest);
    bool edge = (low != w->low) || !w->started;
    bool new_low = is_new_low(s->dma_free, w->reported_min_free, w->new_min_margin) ||
                   is_new_low(s->dma_largest, w->reported_min_largest, w->new_min_margin);

    int64_t period = low ? w->low_period_ms : w->report_period_ms;
    bool due = (now_ms - w->last_report_ms) >= period;

    if (low && !w->low) w->episodes++;
    w->low = low;

    if (!edge && !new_low && !due) return HEAP_WATCH_SILENT;

    /* Only a line that is actually emitted moves the reported minimums and the
     * rate-limit clock — otherwise a silent sample could consume the margin
     * that would have triggered the next report. */
    w->started = true;
    w->last_report_ms = now_ms;
    if (s->dma_free < w->reported_min_free) w->reported_min_free = s->dma_free;
    if (s->dma_largest < w->reported_min_largest) w->reported_min_largest = s->dma_largest;

    return low ? HEAP_WATCH_LOW : HEAP_WATCH_REPORT;
}
