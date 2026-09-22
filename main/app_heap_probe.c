#include "app_heap_probe.h"

#include "heap_watch.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "heap";

/* The caps the SDIO transport actually asks for. MALLOC_CAP_DMA alone would
 * do on this chip — PSRAM carries no DMA cap on the P4 — but spelling out
 * INTERNAL keeps the number meaningful if that ever changes. */
#define PROBE_CAPS (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)

static heap_watch_t s_watch;
static SemaphoreHandle_t s_lock;

void app_heap_probe_init(void) {
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    heap_watch_init(&s_watch, 0, 0, 0, 0); /* defaults from heap_watch.h */
}

static void sample(heap_watch_sample_t *s) {
    s->dma_free = heap_caps_get_free_size(PROBE_CAPS);
    s->dma_largest = heap_caps_get_largest_free_block(PROBE_CAPS);
    s->dma_min_free = heap_caps_get_minimum_free_size(PROBE_CAPS);
    s->psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

static void emit(const char *phase, const heap_watch_sample_t *s, bool low) {
    /* One line, fixed field order, so a run can be grepped and diffed
     * against another build's run without parsing prose. */
    if (low) {
        ESP_LOGW(TAG, "%s: dma_free=%u dma_largest=%u dma_min=%u psram_free=%u "
                      "-- below the headroom the SDIO link needs, packets may be dropped",
                 phase, (unsigned)s->dma_free, (unsigned)s->dma_largest,
                 (unsigned)s->dma_min_free, (unsigned)s->psram_free);
    } else {
        ESP_LOGI(TAG, "%s: dma_free=%u dma_largest=%u dma_min=%u psram_free=%u",
                 phase, (unsigned)s->dma_free, (unsigned)s->dma_largest,
                 (unsigned)s->dma_min_free, (unsigned)s->psram_free);
    }
}

void app_heap_probe_tick(const char *phase) {
    if (!s_lock) return;
    /* Never wait: this runs on the esp_timer task and inside the OTA download
     * loop, neither of which should block for a diagnostic. */
    if (xSemaphoreTake(s_lock, 0) != pdTRUE) return;

    heap_watch_sample_t s;
    sample(&s);
    heap_watch_level_t level = heap_watch_observe(&s_watch, &s,
                                                  esp_timer_get_time() / 1000);
    xSemaphoreGive(s_lock);

    if (level != HEAP_WATCH_SILENT) emit(phase ? phase : "?", &s, level == HEAP_WATCH_LOW);
}

void app_heap_probe_log_now(const char *phase) {
    heap_watch_sample_t s;
    sample(&s);
    emit(phase ? phase : "?", &s, false);
}
