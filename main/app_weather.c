#include "app_weather.h"
#include "app_prefs.h"
#include "app_format.h"
#include "app_wifi.h"
#include "sdkconfig.h"
#include "weather_ui.h"

#include "bsp/esp-bsp.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "cJSON.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "weather";

#define HTTP_BUF_MAX     (48 * 1024)
#define SEARCH_DEBOUNCE_MS 450

typedef enum { CMD_SEARCH, CMD_SELECT, CMD_REFRESH, CMD_RELANG, CMD_WIFI_SCAN, CMD_WIFI_CONNECT, CMD_WIFI_FORGET } cmd_kind_t;

typedef struct {
    cmd_kind_t kind;
    char query[64];
    int index;
    weather_lang_t lang;
    char ssid[33];
    char pass[65];
} cmd_t;

typedef struct {
    char  name[64];
    char  label[96];   /* "admin1, country" — the result row's subtitle */
    char  country[64];
    float lat, lon;
} geo_hit_t;

static QueueHandle_t s_q;
static geo_hit_t s_hits[APP_WEATHER_MAX_RESULTS];
static int s_hit_count;

/* Last successful forecast, kept so a language switch can re-render without
 * another round-trip. */
static bool  s_have_forecast;
static int   s_utc_offset;
static int   s_cur_code, s_cur_hum, s_cur_precip;
static float s_cur_temp, s_cur_feel, s_cur_wind;
static struct { char iso[12]; int code, precip; float tmax, tmin, fmax, fmin, wmax; } s_days[WEATHER_UI_DAYS];
static int s_day_count;

static weather_hourly_t s_hourly[WEATHER_UI_DAYS];
static bool s_hourly_valid[WEATHER_UI_DAYS];
static time_t s_last_success;      /* for the header's "data may be outdated" flag */

#define STALE_AFTER_SEC (60 * 60)

/* ---- HTTP ---------------------------------------------------------------- */

/* GETs `url` into a NUL-terminated heap buffer (PSRAM). Caller frees. */
static char *http_get(const char *url) {
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 4096,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return NULL;

    char *body = NULL;
    esp_err_t err = esp_http_client_open(c, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open failed: %s", esp_err_to_name(err));
        goto done;
    }
    int64_t clen = esp_http_client_fetch_headers(c);
    int status = esp_http_client_get_status_code(c);
    if (status != 200) {
        ESP_LOGE(TAG, "HTTP %d for %.80s", status, url);
        goto done;
    }
    /* Open-Meteo answers chunked, so fetch_headers() often reports -1; grow to a cap. */
    size_t cap = (clen > 0 && clen < HTTP_BUF_MAX) ? (size_t)clen + 1 : 8192;
    body = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) { ESP_LOGE(TAG, "no memory for %u B body", (unsigned)cap); goto done; }

    size_t len = 0;
    for (;;) {
        if (len + 1 >= cap) {
            if (cap >= HTTP_BUF_MAX) { ESP_LOGE(TAG, "response over %d B", HTTP_BUF_MAX); free(body); body = NULL; goto done; }
            size_t ncap = cap * 2 > HTTP_BUF_MAX ? HTTP_BUF_MAX : cap * 2;
            char *nb = heap_caps_realloc(body, ncap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!nb) { free(body); body = NULL; goto done; }
            body = nb; cap = ncap;
        }
        int r = esp_http_client_read(c, body + len, cap - len - 1);
        if (r < 0) { free(body); body = NULL; goto done; }
        if (r == 0) break;
        len += r;
    }
    body[len] = '\0';

done:
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return body;
}

/* ---- JSON helpers -------------------------------------------------------- */

static float jnum(const cJSON *o, const char *k, float dflt) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? (float)v->valuedouble : dflt;
}

static const char *jstr(const cJSON *o, const char *k) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : NULL;
}

/* Reads element `i` of a numeric array, or `dflt` if absent/not a number. */
static float jarr(const cJSON *o, const char *k, int i, float dflt) {
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(o, k);
    if (!cJSON_IsArray(a)) return dflt;
    const cJSON *v = cJSON_GetArrayItem(a, i);
    return cJSON_IsNumber(v) ? (float)v->valuedouble : dflt;
}

/* ---- UI push (always under the display lock) ----------------------------- */

static void ui_error(const char *msg) {
    bool stale = s_last_success == 0 || (time(NULL) - s_last_success) > STALE_AFTER_SEC;
    if (!bsp_display_lock(1000)) return;
    weather_ui_set_loading(false);
    weather_ui_set_error(msg);
    weather_ui_set_network_status(app_wifi_is_connected() ? WX_NET_ONLINE : WX_NET_OFFLINE);
    weather_ui_set_data_stale(stale);
    bsp_display_unlock();
}

/* Renders the cached forecast in the current language/units. */
static void push_forecast_to_ui(void) {
    if (!s_have_forecast) return;
    const app_prefs_t *p = app_prefs_get();
    weather_lang_t lang = p->lang;

    /* Open-Meteo's utc_offset_seconds already encodes the city's DST state, so
     * local wall-clock time is just UTC plus that offset. */
    time_t now_utc = time(NULL);
    time_t local = now_utc + s_utc_offset;
    struct tm lt;
    gmtime_r(&local, &lt);

    weather_current_t cur = {0};
    snprintf(cur.location_name, sizeof cur.location_name, "%s", p->name);
    snprintf(cur.location_country, sizeof cur.location_country, "%s", p->country);
    fmt_time(cur.time_str, sizeof cur.time_str, &lt, p->time_fmt);
    fmt_date(cur.date_str, sizeof cur.date_str, &lt, lang);
    cur.weather_code = s_cur_code;
    cur.temp_c = s_cur_temp;
    cur.feels_like_c = s_cur_feel;
    cur.humidity_pct = s_cur_hum;
    cur.wind_kmh = s_cur_wind;
    cur.precip_pct = s_cur_precip;
    fmt_real_feel(cur.real_feel_text, sizeof cur.real_feel_text,
                  s_cur_feel, s_cur_temp, s_cur_wind, s_cur_precip, lang);

    weather_day_t days[WEATHER_UI_DAYS] = {0};
    for (int i = 0; i < WEATHER_UI_DAYS; i++) {
        if (i >= s_day_count) { snprintf(days[i].day_label, sizeof days[i].day_label, "--"); continue; }
        struct tm dt;
        if (parse_iso_date(s_days[i].iso, &dt)) {
            fmt_day_label(days[i].day_label, sizeof days[i].day_label, &dt, i, lang);
            fmt_day_date(days[i].date_label, sizeof days[i].date_label, &dt, lang);
        }
        days[i].weather_code = s_days[i].code;
        days[i].temp_max_c = s_days[i].tmax;
        days[i].temp_min_c = s_days[i].tmin;
        days[i].feels_max_c = s_days[i].fmax;
        days[i].feels_min_c = s_days[i].fmin;
        days[i].wind_max_kmh = s_days[i].wmax;
        days[i].precip_pct = s_days[i].precip;
    }

    const weather_hourly_t *day_ptrs[WEATHER_UI_DAYS];
    for (int i = 0; i < WEATHER_UI_DAYS; i++)
        day_ptrs[i] = s_hourly_valid[i] ? &s_hourly[i] : NULL;

    bool stale = s_last_success == 0 || (now_utc - s_last_success) > STALE_AFTER_SEC;

    if (!bsp_display_lock(1000)) return;
    weather_ui_set_error(NULL);
    weather_ui_set_current(&cur);
    weather_ui_set_days(days);
    weather_ui_set_hourly(day_ptrs[0], day_ptrs);
    weather_ui_set_network_status(app_wifi_is_connected() ? WX_NET_ONLINE : WX_NET_OFFLINE);
    weather_ui_set_data_stale(stale);
    weather_ui_set_loading(false);
    bsp_display_unlock();
}

/* ---- forecast ------------------------------------------------------------ */

static void do_refresh(void) {
    const app_prefs_t *p = app_prefs_get();

    if (bsp_display_lock(1000)) { weather_ui_set_loading(true); bsp_display_unlock(); }

    char url[512];
    snprintf(url, sizeof url,
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
             "precipitation_probability,wind_speed_10m,weather_code"
             "&hourly=temperature_2m,precipitation"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
             "apparent_temperature_max,apparent_temperature_min,"
             "precipitation_probability_max,wind_speed_10m_max"
             "&timezone=auto&forecast_days=%d",
             p->lat, p->lon, WEATHER_UI_DAYS);

    char *body = http_get(url);
    if (!body) { ui_error(weather_strings[p->lang].error); return; }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) { ui_error(weather_strings[p->lang].error); return; }

    const cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "current");
    const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    if (!cJSON_IsObject(cur) || !cJSON_IsObject(daily)) {
        cJSON_Delete(root);
        ui_error(weather_strings[p->lang].error);
        return;
    }

    s_utc_offset = (int)jnum(root, "utc_offset_seconds", 0);
    s_cur_temp   = jnum(cur, "temperature_2m", 0);
    s_cur_hum    = (int)jnum(cur, "relative_humidity_2m", 0);
    s_cur_feel   = jnum(cur, "apparent_temperature", s_cur_temp);
    s_cur_precip = (int)jnum(cur, "precipitation_probability", 0);
    s_cur_wind   = jnum(cur, "wind_speed_10m", 0);
    s_cur_code   = (int)jnum(cur, "weather_code", 0);

    const cJSON *times = cJSON_GetObjectItemCaseSensitive(daily, "time");
    int n = cJSON_IsArray(times) ? cJSON_GetArraySize(times) : 0;
    if (n > WEATHER_UI_DAYS) n = WEATHER_UI_DAYS;
    s_day_count = n;
    for (int i = 0; i < n; i++) {
        const cJSON *t = cJSON_GetArrayItem(times, i);
        snprintf(s_days[i].iso, sizeof s_days[i].iso, "%s",
                 (cJSON_IsString(t) && t->valuestring) ? t->valuestring : "");
        s_days[i].code   = (int)jarr(daily, "weather_code", i, 0);
        s_days[i].tmax   = jarr(daily, "temperature_2m_max", i, 0);
        s_days[i].tmin   = jarr(daily, "temperature_2m_min", i, 0);
        s_days[i].fmax   = jarr(daily, "apparent_temperature_max", i, s_days[i].tmax);
        s_days[i].fmin   = jarr(daily, "apparent_temperature_min", i, s_days[i].tmin);
        s_days[i].precip = (int)jarr(daily, "precipitation_probability_max", i, 0);
        s_days[i].wmax   = jarr(daily, "wind_speed_10m_max", i, 0);
    }
    /* Hourly arrives as one flat 7x24 series; slice it per day. */
    const cJSON *hourly = cJSON_GetObjectItemCaseSensitive(root, "hourly");
    for (int d = 0; d < WEATHER_UI_DAYS; d++) s_hourly_valid[d] = false;
    if (cJSON_IsObject(hourly)) {
        const cJSON *ht = cJSON_GetObjectItemCaseSensitive(hourly, "time");
        int hn = cJSON_IsArray(ht) ? cJSON_GetArraySize(ht) : 0;
        for (int d = 0; d < n; d++) {
            int base = d * 24;
            if (base >= hn) break;
            int cnt = hn - base;
            if (cnt > WEATHER_UI_CHART_POINTS) cnt = WEATHER_UI_CHART_POINTS;
            s_hourly[d].count = cnt;
            for (int i = 0; i < cnt; i++) {
                s_hourly[d].temp_c[i]    = jarr(hourly, "temperature_2m", base + i, 0);
                s_hourly[d].precip_mm[i] = jarr(hourly, "precipitation", base + i, 0);
                /* "time" is local ISO ("2026-09-08T14:00"); the hour is at offset 11. */
                const cJSON *tv = cJSON_GetArrayItem(ht, base + i);
                const char *ts = (cJSON_IsString(tv) && tv->valuestring) ? tv->valuestring : NULL;
                s_hourly[d].hour[i] = (ts && strlen(ts) >= 13) ? atoi(ts + 11) : i;
            }
            s_hourly_valid[d] = cnt > 1;
        }
    }
    cJSON_Delete(root);

    s_last_success = time(NULL);
    s_have_forecast = true;
    ESP_LOGI(TAG, "forecast ok: %.1fC code=%d, %d days", s_cur_temp, s_cur_code, n);
    push_forecast_to_ui();
}

/* ---- geocoding ----------------------------------------------------------- */

static void do_search(const char *query) {
    const app_prefs_t *p = app_prefs_get();
    if (!query || strlen(query) < 2) {
        s_hit_count = 0;
        if (bsp_display_lock(1000)) { weather_ui_set_searching(false); bsp_display_unlock(); }
        return;
    }

    if (bsp_display_lock(1000)) { weather_ui_set_searching(true); bsp_display_unlock(); }

    /* Percent-encode: city names carry spaces and non-ASCII (München, Nîmes). */
    char esc[192];
    size_t o = 0;
    for (const unsigned char *s = (const unsigned char *)query; *s && o + 4 < sizeof esc; s++) {
        if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
            *s == '-' || *s == '_' || *s == '.' || *s == '~') {
            esc[o++] = (char)*s;
        } else {
            static const char hex[] = "0123456789ABCDEF";
            esc[o++] = '%'; esc[o++] = hex[*s >> 4]; esc[o++] = hex[*s & 0xF];
        }
    }
    esc[o] = '\0';

    char url[384];
    snprintf(url, sizeof url,
             "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=%d&language=%s&format=json",
             esc, APP_WEATHER_MAX_RESULTS, weather_lang_tag[p->lang]);

    char *body = http_get(url);
    s_hit_count = 0;
    if (body) {
        cJSON *root = cJSON_Parse(body);
        free(body);
        if (root) {
            const cJSON *results = cJSON_GetObjectItemCaseSensitive(root, "results");
            int n = cJSON_IsArray(results) ? cJSON_GetArraySize(results) : 0;
            if (n > APP_WEATHER_MAX_RESULTS) n = APP_WEATHER_MAX_RESULTS;
            for (int i = 0; i < n; i++) {
                const cJSON *r = cJSON_GetArrayItem(results, i);
                const char *nm = jstr(r, "name");
                const char *a1 = jstr(r, "admin1");
                const char *co = jstr(r, "country");
                snprintf(s_hits[i].name, sizeof s_hits[i].name, "%s", nm ? nm : "?");
                snprintf(s_hits[i].country, sizeof s_hits[i].country, "%s", co ? co : "");
                if (a1 && co) snprintf(s_hits[i].label, sizeof s_hits[i].label, "%s, %s", a1, co);
                else          snprintf(s_hits[i].label, sizeof s_hits[i].label, "%s", co ? co : "");
                s_hits[i].lat = jnum(r, "latitude", 0);
                s_hits[i].lon = jnum(r, "longitude", 0);
            }
            s_hit_count = n;
            cJSON_Delete(root);
        }
    }

    const char *names[APP_WEATHER_MAX_RESULTS];
    const char *subs[APP_WEATHER_MAX_RESULTS];
    for (int i = 0; i < s_hit_count; i++) { names[i] = s_hits[i].name; subs[i] = s_hits[i].label; }

    if (bsp_display_lock(1000)) {
        weather_ui_set_searching(false);
        weather_ui_set_search_results(names, subs, s_hit_count);
        bsp_display_unlock();
    }
}

/* ---- worker -------------------------------------------------------------- */

static void weather_task(void *arg) {
    LV_UNUSED(arg);
    cmd_t cmd;
    cmd_t pending_search;
    bool have_pending = false;
    TickType_t search_due = 0;

    TickType_t last_auto = xTaskGetTickCount();
    if (app_wifi_is_connected()) do_refresh();

    for (;;) {
        TickType_t wait = pdMS_TO_TICKS(500);
        if (xQueueReceive(s_q, &cmd, wait) == pdTRUE) {
            switch (cmd.kind) {
                case CMD_SEARCH:
                    /* Coalesce keystrokes: the textarea fires per character, and each
                     * one would otherwise be its own TLS handshake. */
                    pending_search = cmd;
                    have_pending = true;
                    search_due = xTaskGetTickCount() + pdMS_TO_TICKS(SEARCH_DEBOUNCE_MS);
                    break;
                case CMD_SELECT:
                    if (cmd.index >= 0 && cmd.index < s_hit_count) {
                        geo_hit_t *h = &s_hits[cmd.index];
                        app_prefs_save_city(h->name, h->country, h->lat, h->lon);
                        do_refresh();
                        last_auto = xTaskGetTickCount();
                    }
                    break;
                case CMD_REFRESH:
                    do_refresh();
                    last_auto = xTaskGetTickCount();
                    break;
                case CMD_RELANG:
                    push_forecast_to_ui();
                    break;
                case CMD_WIFI_SCAN: {
                    wx_wifi_network_t nets[APP_WIFI_MAX_SCAN];
                    int n = app_wifi_scan(nets, APP_WIFI_MAX_SCAN);
                    if (bsp_display_lock(1000)) {
                        weather_ui_set_wifi_scan_results(nets, n);
                        bsp_display_unlock();
                    }
                    break;
                }
                case CMD_WIFI_CONNECT: {
                    bool ok = app_wifi_connect_with(cmd.ssid, cmd.pass);
                    if (bsp_display_lock(1000)) {
                        weather_ui_set_wifi_connect_result(ok);
                        weather_ui_set_network_status(ok ? WX_NET_ONLINE : WX_NET_OFFLINE);
                        bsp_display_unlock();
                    }
                    if (ok) {
                        app_wifi_sync_time(15000);
                        do_refresh();
                        last_auto = xTaskGetTickCount();
                    }
                    break;
                }
                case CMD_WIFI_FORGET:
                    app_wifi_forget();
                    if (bsp_display_lock(1000)) {
                        weather_ui_set_network_status(WX_NET_OFFLINE);
                        bsp_display_unlock();
                    }
                    break;
            }
        }

        if (have_pending && (int32_t)(xTaskGetTickCount() - search_due) >= 0) {
            have_pending = false;
            do_search(pending_search.query);
        }

        /* Periodic refresh, and a per-minute clock tick so the header stays live. */
        TickType_t now = xTaskGetTickCount();
        if ((now - last_auto) >= pdMS_TO_TICKS(CONFIG_WEATHER_REFRESH_MINUTES * 60 * 1000)) {
            do_refresh();
            last_auto = now;
        } else if (s_have_forecast) {
            static int tick = 0;
            if (++tick >= 60) { tick = 0; push_forecast_to_ui(); }  /* ~30 s */
        }
    }
}

/* ---- public API ---------------------------------------------------------- */

static void post(cmd_t *c) {
    if (s_q) xQueueSend(s_q, c, 0);
}

void app_weather_start(void) {
    s_q = xQueueCreate(8, sizeof(cmd_t));
    /* TLS needs a roomy stack; the JSON parse runs on this task too. */
    xTaskCreatePinnedToCore(weather_task, "weather", 8192, NULL, 4, NULL, 0);
}

void app_weather_search(const char *query) {
    cmd_t c = { .kind = CMD_SEARCH };
    snprintf(c.query, sizeof c.query, "%s", query ? query : "");
    post(&c);
}

void app_weather_select_city(int idx) {
    cmd_t c = { .kind = CMD_SELECT, .index = idx };
    post(&c);
}

void app_weather_refresh(void) {
    cmd_t c = { .kind = CMD_REFRESH };
    post(&c);
}

void app_weather_set_language(weather_lang_t lang) {
    cmd_t c = { .kind = CMD_RELANG, .lang = lang };
    post(&c);
}

void app_weather_wifi_scan(void) {
    cmd_t c = { .kind = CMD_WIFI_SCAN };
    post(&c);
}

void app_weather_wifi_connect(const char *ssid, const char *password) {
    cmd_t c = { .kind = CMD_WIFI_CONNECT };
    snprintf(c.ssid, sizeof c.ssid, "%s", ssid ? ssid : "");
    snprintf(c.pass, sizeof c.pass, "%s", password ? password : "");
    post(&c);
}

void app_weather_wifi_forget(void) {
    cmd_t c = { .kind = CMD_WIFI_FORGET };
    post(&c);
}
