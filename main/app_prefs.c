#include "app_prefs.h"
#include "sdkconfig.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "storage_record.h"
#include "storage_backend_nvs.h"
#include "save_debounce.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "prefs";
static const char *NS = "weather";
#define RECORD_KEY "prefs"
#define RECORD_VERSION 1

static app_prefs_t s_prefs;

const app_prefs_t *app_prefs_get(void) { return &s_prefs; }

/* On-disk layout of the "prefs" record (components/app_logic/storage_record.c
 * wraps this in a {version, len, crc32} header and writes it as one blob —
 * see main/app_prefs.c's module comment in storage_record.h for why this is
 * one struct/one record instead of one NVS key per field). Packed so the
 * layout (and therefore the CRC) doesn't depend on compiler padding choices.
 * Bump RECORD_VERSION on any layout change; storage_record_load() then
 * rejects the old layout instead of misreading it, and app_prefs_load()
 * falls back to Kconfig defaults exactly as it does for a missing record. */
typedef struct __attribute__((packed)) {
    char name[64];
    char country[64];
    float lat, lon;
    uint8_t lang, temp_unit, wind_unit, time_fmt;
    uint8_t brightness;
    uint8_t brightness_adaptive;
} prefs_payload_t;

static void get_str(nvs_handle_t h, const char *key, char *dst, size_t cap) {
    size_t len = cap;
    if (nvs_get_str(h, key, dst, &len) != ESP_OK) dst[0] = '\0';
}

static void get_u8(nvs_handle_t h, const char *key, void *dst) {
    uint8_t v;
    if (nvs_get_u8(h, key, &v) == ESP_OK) *(int *)dst = v;
}

/* One-time upgrade path: this project's first NVS layout kept each field as
 * its own scalar key (city/country/lat_e4/lon_e4/lang/tunit/wunit/tfmt/
 * bright/bright_auto). Devices already in the field have that data and
 * nothing else — read it once, fold it into s_prefs (which the Kconfig
 * defaults have already seeded), and save the new combined record so this
 * runs at most once. The legacy keys are left in place, not erased: erasing
 * them buys nothing (they're simply never read again once the new record
 * exists) and a failed erase mid-way would be one more thing that could go
 * wrong for no benefit. */
static void migrate_from_legacy_keys(void) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;

    char buf[64];
    get_str(h, "city", buf, sizeof buf);
    if (buf[0]) memcpy(s_prefs.name, buf, sizeof buf);
    get_str(h, "country", buf, sizeof buf);
    if (buf[0]) memcpy(s_prefs.country, buf, sizeof buf);

    int32_t v;
    if (nvs_get_i32(h, "lat_e4", &v) == ESP_OK) s_prefs.lat = (float)v / 10000.0f;
    if (nvs_get_i32(h, "lon_e4", &v) == ESP_OK) s_prefs.lon = (float)v / 10000.0f;

    int tmp;
    tmp = s_prefs.lang;       get_u8(h, "lang", &tmp);       s_prefs.lang = (weather_lang_t)tmp;
    tmp = s_prefs.temp_unit;  get_u8(h, "tunit", &tmp);      s_prefs.temp_unit = (wx_temp_unit_t)tmp;
    tmp = s_prefs.wind_unit;  get_u8(h, "wunit", &tmp);      s_prefs.wind_unit = (wx_wind_unit_t)tmp;
    tmp = s_prefs.time_fmt;   get_u8(h, "tfmt", &tmp);       s_prefs.time_fmt = (wx_time_fmt_t)tmp;
    tmp = s_prefs.brightness; get_u8(h, "bright", &tmp);     s_prefs.brightness = tmp;
    tmp = s_prefs.brightness_adaptive; get_u8(h, "bright_auto", &tmp); s_prefs.brightness_adaptive = tmp != 0;
    nvs_close(h);
}

void app_prefs_load(app_prefs_t *out) {
    /* Kconfig defaults first, so a blank/corrupt record still yields a usable city. */
    snprintf(s_prefs.name, sizeof s_prefs.name, "%s", CONFIG_WEATHER_DEFAULT_CITY_NAME);
    snprintf(s_prefs.country, sizeof s_prefs.country, "%s", CONFIG_WEATHER_DEFAULT_CITY_COUNTRY);
    s_prefs.lat = (float)CONFIG_WEATHER_DEFAULT_LAT_MILLIDEG / 1000.0f;
    s_prefs.lon = (float)CONFIG_WEATHER_DEFAULT_LON_MILLIDEG / 1000.0f;
    s_prefs.lang = LANG_EN;
    s_prefs.temp_unit = WX_UNIT_C;
    s_prefs.wind_unit = WX_WIND_KMH;
    s_prefs.time_fmt = WX_TIME_24;
    s_prefs.brightness = 100;
    s_prefs.brightness_adaptive = false;

    prefs_payload_t p;
    if (storage_record_load(&storage_backend_nvs, NS, RECORD_KEY, RECORD_VERSION, &p, sizeof p)) {
        memcpy(s_prefs.name, p.name, sizeof p.name);
        memcpy(s_prefs.country, p.country, sizeof p.country);
        s_prefs.lat = p.lat;
        s_prefs.lon = p.lon;
        s_prefs.lang = (weather_lang_t)p.lang;
        s_prefs.temp_unit = (wx_temp_unit_t)p.temp_unit;
        s_prefs.wind_unit = (wx_wind_unit_t)p.wind_unit;
        s_prefs.time_fmt = (wx_time_fmt_t)p.time_fmt;
        s_prefs.brightness = p.brightness;
        s_prefs.brightness_adaptive = p.brightness_adaptive != 0;
    } else {
        /* No valid "prefs" record yet — either a fresh device, or one still
         * on the pre-storage-record NVS layout. Either way the Kconfig
         * defaults above are already in s_prefs; layer the legacy scalar
         * keys on top if present, then persist the result as the new
         * record so this check is unnecessary from the next boot on. */
        migrate_from_legacy_keys();
        prefs_payload_t np = {
            .lat = s_prefs.lat, .lon = s_prefs.lon,
            .lang = (uint8_t)s_prefs.lang, .temp_unit = (uint8_t)s_prefs.temp_unit,
            .wind_unit = (uint8_t)s_prefs.wind_unit, .time_fmt = (uint8_t)s_prefs.time_fmt,
            .brightness = (uint8_t)s_prefs.brightness,
            .brightness_adaptive = s_prefs.brightness_adaptive ? 1 : 0,
        };
        memcpy(np.name, s_prefs.name, sizeof np.name);
        memcpy(np.country, s_prefs.country, sizeof np.country);
        storage_record_save(&storage_backend_nvs, NS, RECORD_KEY, RECORD_VERSION, &np, sizeof np);
    }

    /* Guard against a corrupt/downgraded record (or an out-of-range legacy
     * value) feeding an out-of-range enum straight into weather_strings[] /
     * weather_cond_text[] — storage_record_load() already rejects a
     * checksum or version mismatch wholesale, this is the cheaper defense
     * for a value that's individually out of range despite a valid record. */
    if (s_prefs.lang < 0 || s_prefs.lang >= LANG_COUNT) s_prefs.lang = LANG_EN;
    if (s_prefs.temp_unit > WX_UNIT_F) s_prefs.temp_unit = WX_UNIT_C;
    if (s_prefs.wind_unit > WX_WIND_MS) s_prefs.wind_unit = WX_WIND_KMH;
    if (s_prefs.time_fmt > WX_TIME_12) s_prefs.time_fmt = WX_TIME_24;
    if (s_prefs.brightness < 10 || s_prefs.brightness > 100) s_prefs.brightness = 100;

    ESP_LOGI(TAG, "city=%s,%s (%.4f,%.4f) lang=%d", s_prefs.name, s_prefs.country,
             s_prefs.lat, s_prefs.lon, (int)s_prefs.lang);
    if (out) *out = s_prefs;
}

/* All four save_* entry points below write the same combined record, so
 * they share one debounced writer: whichever setting changed is already
 * live in s_prefs by the time this fires, and a burst of changes within the
 * debounce window (e.g. picking a city right after changing units) becomes
 * one flash write instead of several. */
static esp_timer_handle_t s_save_timer;

static void persist_prefs_cb(void *arg) {
    (void)arg;
    prefs_payload_t p = {
        .lat = s_prefs.lat, .lon = s_prefs.lon,
        .lang = (uint8_t)s_prefs.lang, .temp_unit = (uint8_t)s_prefs.temp_unit,
        .wind_unit = (uint8_t)s_prefs.wind_unit, .time_fmt = (uint8_t)s_prefs.time_fmt,
        .brightness = (uint8_t)s_prefs.brightness,
        .brightness_adaptive = s_prefs.brightness_adaptive ? 1 : 0,
    };
    memcpy(p.name, s_prefs.name, sizeof p.name);
    memcpy(p.country, s_prefs.country, sizeof p.country);
    storage_record_save(&storage_backend_nvs, NS, RECORD_KEY, RECORD_VERSION, &p, sizeof p);
}

static void request_save(void) {
    save_debounce_fire(&s_save_timer, "prefs_save", persist_prefs_cb, 50 * 1000 /* 50ms, in us */);
}

void app_prefs_save_city(const char *name, const char *country, float lat, float lon) {
    snprintf(s_prefs.name, sizeof s_prefs.name, "%s", name ? name : "");
    snprintf(s_prefs.country, sizeof s_prefs.country, "%s", country ? country : "");
    s_prefs.lat = lat;
    s_prefs.lon = lon;
    request_save();
}

void app_prefs_save_settings(weather_lang_t lang, wx_temp_unit_t t, wx_wind_unit_t w, wx_time_fmt_t tf) {
    s_prefs.lang = lang; s_prefs.temp_unit = t; s_prefs.wind_unit = w; s_prefs.time_fmt = tf;
    request_save();
}

void app_prefs_save_brightness(int percent) {
    s_prefs.brightness = percent;
    request_save();
}

void app_prefs_save_brightness_adaptive(bool enabled) {
    s_prefs.brightness_adaptive = enabled;
    request_save();
}
