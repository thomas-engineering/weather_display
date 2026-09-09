#include "app_prefs.h"
#include "sdkconfig.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "prefs";
static const char *NS = "weather";

static app_prefs_t s_prefs;

const app_prefs_t *app_prefs_get(void) { return &s_prefs; }

static void get_str(nvs_handle_t h, const char *key, char *dst, size_t cap) {
    size_t len = cap;
    if (nvs_get_str(h, key, dst, &len) != ESP_OK) dst[0] = '\0';
}

static void get_u8(nvs_handle_t h, const char *key, void *dst) {
    uint8_t v;
    if (nvs_get_u8(h, key, &v) == ESP_OK) *(int *)dst = v;
}

void app_prefs_load(app_prefs_t *out) {
    /* Kconfig defaults first, so a blank NVS still yields a usable city. */
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

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        char buf[64];
        get_str(h, "city", buf, sizeof buf);
        if (buf[0]) memcpy(s_prefs.name, buf, sizeof buf);
        get_str(h, "country", buf, sizeof buf);
        if (buf[0]) memcpy(s_prefs.country, buf, sizeof buf);

        int32_t v;
        if (nvs_get_i32(h, "lat_e4", &v) == ESP_OK) s_prefs.lat = (float)v / 10000.0f;
        if (nvs_get_i32(h, "lon_e4", &v) == ESP_OK) s_prefs.lon = (float)v / 10000.0f;

        int tmp;
        tmp = s_prefs.lang;      get_u8(h, "lang", &tmp); s_prefs.lang = (weather_lang_t)tmp;
        tmp = s_prefs.temp_unit; get_u8(h, "tunit", &tmp); s_prefs.temp_unit = (wx_temp_unit_t)tmp;
        tmp = s_prefs.wind_unit; get_u8(h, "wunit", &tmp); s_prefs.wind_unit = (wx_wind_unit_t)tmp;
        tmp = s_prefs.time_fmt;  get_u8(h, "tfmt", &tmp);  s_prefs.time_fmt = (wx_time_fmt_t)tmp;
        tmp = s_prefs.brightness; get_u8(h, "bright", &tmp); s_prefs.brightness = tmp;
        tmp = s_prefs.brightness_adaptive; get_u8(h, "bright_auto", &tmp); s_prefs.brightness_adaptive = tmp != 0;
        nvs_close(h);
    }

    /* Guard against a corrupt or downgraded NVS blob feeding an out-of-range enum
     * straight into weather_strings[] / weather_cond_text[]. */
    if (s_prefs.lang < 0 || s_prefs.lang >= LANG_COUNT) s_prefs.lang = LANG_EN;
    if (s_prefs.temp_unit > WX_UNIT_F) s_prefs.temp_unit = WX_UNIT_C;
    if (s_prefs.wind_unit > WX_WIND_MS) s_prefs.wind_unit = WX_WIND_KMH;
    if (s_prefs.time_fmt > WX_TIME_12) s_prefs.time_fmt = WX_TIME_24;
    if (s_prefs.brightness < 10 || s_prefs.brightness > 100) s_prefs.brightness = 100;

    ESP_LOGI(TAG, "city=%s,%s (%.4f,%.4f) lang=%d", s_prefs.name, s_prefs.country,
             s_prefs.lat, s_prefs.lon, (int)s_prefs.lang);
    if (out) *out = s_prefs;
}

void app_prefs_save_city(const char *name, const char *country, float lat, float lon) {
    snprintf(s_prefs.name, sizeof s_prefs.name, "%s", name ? name : "");
    snprintf(s_prefs.country, sizeof s_prefs.country, "%s", country ? country : "");
    s_prefs.lat = lat;
    s_prefs.lon = lon;

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "city", s_prefs.name);
    nvs_set_str(h, "country", s_prefs.country);
    nvs_set_i32(h, "lat_e4", (int32_t)(lat * 10000.0f));
    nvs_set_i32(h, "lon_e4", (int32_t)(lon * 10000.0f));
    nvs_commit(h);
    nvs_close(h);
}

void app_prefs_save_settings(weather_lang_t lang, wx_temp_unit_t t, wx_wind_unit_t w, wx_time_fmt_t tf) {
    s_prefs.lang = lang; s_prefs.temp_unit = t; s_prefs.wind_unit = w; s_prefs.time_fmt = tf;

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "lang", (uint8_t)lang);
    nvs_set_u8(h, "tunit", (uint8_t)t);
    nvs_set_u8(h, "wunit", (uint8_t)w);
    nvs_set_u8(h, "tfmt", (uint8_t)tf);
    nvs_commit(h);
    nvs_close(h);
}

void app_prefs_save_brightness(int percent) {
    s_prefs.brightness = percent;

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "bright", (uint8_t)percent);
    nvs_commit(h);
    nvs_close(h);
}

void app_prefs_save_brightness_adaptive(bool enabled) {
    s_prefs.brightness_adaptive = enabled;

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "bright_auto", enabled ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}
