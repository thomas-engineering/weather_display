#include "app_favorites.h"
#include "storage_record.h"
#include "storage_backend_nvs.h"
#include "save_debounce.h"
#include <string.h>

static const char *NS = "weather";
#define RECORD_KEY "favs"
#define RECORD_VERSION 1

static app_favorite_t s_favs[APP_FAVORITES_MAX];
static esp_timer_handle_t s_save_timer;

const app_favorite_t *app_favorites_get(void) { return s_favs; }

void app_favorites_load(void) {
    memset(s_favs, 0, sizeof s_favs);
    /* storage_record_load() leaves s_favs untouched (still all-zero, i.e.
     * "no favorites") on any failure: missing record (first boot), wrong
     * version, or a corrupt/truncated blob — same fallback app_prefs.c uses. */
    storage_record_load(&storage_backend_nvs, NS, RECORD_KEY, RECORD_VERSION, s_favs, sizeof s_favs);
}

static void persist_favorites_cb(void *arg) {
    (void)arg;
    storage_record_save(&storage_backend_nvs, NS, RECORD_KEY, RECORD_VERSION, s_favs, sizeof s_favs);
}

static void request_save(void) {
    save_debounce_fire(&s_save_timer, "favs_save", persist_favorites_cb, 50 * 1000 /* 50ms, in us */);
}

bool app_favorites_toggle_save(const char *name, const char *country, float lat, float lon) {
    bool changed = app_favorites_toggle(s_favs, name, country, lat, lon);
    if (changed) request_save();
    return changed;
}

void app_favorites_remove_save(int idx) {
    app_favorites_remove(s_favs, idx);
    request_save();
}
