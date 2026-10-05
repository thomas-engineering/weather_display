/* LVGL's LV_STDLIB_CUSTOM core allocator (CONFIG_LV_USE_CUSTOM_MALLOC).
 *
 * LVGL's own builtin/CLIB backends have no PSRAM awareness: with CLIB
 * malloc, every block under CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL (1024 B) --
 * i.e. almost every LVGL object, style and per-frame draw task -- lands in
 * internal RAM, competing with the SDIO link to the C6 for the same memory
 * (see sdkconfig.defaults and review/audit-b-heap-2026-10-05.md). This
 * backend asks for PSRAM first and only falls back to internal RAM if PSRAM
 * is exhausted.
 */

#include "esp_heap_caps.h"
#include "stdlib/lv_mem.h"

void lv_mem_init(void) {
    /* Nothing to init: heap_caps already owns the pools. */
}

void lv_mem_deinit(void) {
    /* Nothing to deinit. */
}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes) {
    (void)mem;
    (void)bytes;
    return NULL; /* Not supported, same as the CLIB backend. */
}

void lv_mem_remove_pool(lv_mem_pool_t pool) {
    (void)pool;
}

void *lv_malloc_core(size_t size) {
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) return p;
    return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void *lv_realloc_core(void *p, size_t new_size) {
    void *np = heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (np) return np;
    return heap_caps_realloc(p, new_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void lv_free_core(void *p) {
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p) {
    (void)mon_p; /* Not supported, same as the CLIB backend. */
}

lv_result_t lv_mem_test_core(void) {
    return LV_RESULT_OK;
}
