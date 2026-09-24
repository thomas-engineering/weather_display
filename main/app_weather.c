#include "app_weather.h"
#include "app_prefs.h"
#include "app_favorites.h"
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
#include "freertos/semphr.h"
#include "cJSON.h"
#include "mbedtls/error.h"
#include "esp_app_desc.h"
#include "esp_hosted.h"
#include "esp_system.h"
#include "ota_update.h"
#include "network_status_policy.h"
#include "link_health_policy.h"
#include "startup_retry_policy.h"
#include "task_heartbeat.h"
#include "display_lock_probe.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "weather";

#define HTTP_BUF_MAX     (48 * 1024)
#define SEARCH_DEBOUNCE_MS 450
/* Longest the boot-time fetch waits for the clock. Leaves room inside the
 * 30s "time and weather after Wi-Fi connect" budget for the fetch itself
 * and one retry. */
#define STARTUP_SNTP_WAIT_MS 8000

typedef enum { CMD_SEARCH, CMD_SELECT, CMD_REFRESH, CMD_RELANG, CMD_WIFI_SCAN, CMD_WIFI_CONNECT, CMD_WIFI_FORGET,
               CMD_FAV_TOGGLE, CMD_FAV_SELECT, CMD_FAV_REMOVE, CMD_OTA_START, CMD_OTA_RESUME } cmd_kind_t;

typedef struct {
    cmd_kind_t kind;
    char query[64];
    int index;
    weather_lang_t lang;
    char ssid[33];
    char pass[65];
    bool ota_silent; /* CMD_OTA_START only: true for the periodic auto-check, so a
                       * WX_OTA_IDLE/DOWNLOADING flicker doesn't show up on a dialog
                       * the user hasn't opened when nothing turns out to be new. */
} cmd_t;

typedef struct {
    char  name[64];
    char  label[96];   /* "admin1, country" — the result row's subtitle */
    char  country[64];
    float lat, lon;
} geo_hit_t;

static QueueHandle_t s_q;

/* Serializes do_refresh()/CMD_WIFI_SCAN/CMD_WIFI_CONNECT (all on weather_task)
 * against the OTA tasks (ota_worker_task/ota_resume_task, running
 * independently since 2026-09-17's concurrency fix). Before that fix, OTA
 * ran inline in weather_task, so it could never overlap with this task's own
 * network calls; now that it deliberately doesn't block weather_task, an OTA
 * check and a forecast auto-refresh can coincide and both try to use the
 * ESP32-C6's SDIO link at once. Found on hardware: repeated "mempool OOM
 * (RX)" warnings from esp-hosted's SDIO transport cascading into a full
 * connection timeout on the manifest fetch — its RX buffer pool is shared
 * and fixed-size, not per-connection. This mutex keeps at most one real
 * network operation running at a time, regardless of which task requested
 * it, trading a brief wait for one of them over risking both failing. */
static SemaphoreHandle_t s_network_mutex;
/* Finite instead of portMAX_DELAY: an OTA task holding this mutex for a
 * genuinely stuck download (SDIO/RPC wedge) used to block every other
 * network-shaped command (refresh, Wi-Fi scan/connect) forever, with no
 * feedback to the UI (found by firmware-auditor Category K). 60s is well
 * above any legitimate single network operation guarded by this mutex. */
#define NETWORK_MUTEX_TIMEOUT_MS 60000
static geo_hit_t s_hits[APP_WEATHER_MAX_RESULTS];
static int s_hit_count;

/* Last successful forecast, kept so a language switch can re-render without
 * another round-trip. */
static bool  s_have_forecast;
static int   s_utc_offset;

/* Decides the header error bar / refresh toast state from fetch outcomes —
 * see components/app_logic/network_status_policy.h for why this can't just
 * be "if (is_manual) show a toast" at each call site. */
static network_status_policy_t s_net_status;

/* Escalation for the failure mode no Wi-Fi event reports: associated, with an
 * address, and every request failing anyway (half-open association, wedged
 * SDIO/RPC path). See components/app_logic/link_health_policy.h — the header
 * error bar alone used to be the *entire* response to that, so the device
 * could sit in "Online, nothing works" until someone rebooted it. */
static link_health_policy_t s_link_health;

/* Guards against a second "Check for update" tap (or the periodic
 * auto-check, or CMD_OTA_RESUME) spawning a second ota_worker_task/
 * ota_resume_task while one is already running — those run on their own
 * dedicated tasks now (2026-09-17), not inline in weather_task, but only
 * one may run at a time since they share ota_update.c's cancel flag and
 * both talk to the same network/flash. Set when a task is spawned, cleared
 * by that task right before it deletes itself. */
static bool s_ota_active;
static TickType_t s_last_ota_check;
static bool s_ota_checked_once;
#define OTA_AUTO_CHECK_INTERVAL_MS (12 * 60 * 60 * 1000) /* matches the dialog's own hint text */

/* First check shortly after boot, then on the long interval. The comparison
 * below is against an uptime that starts at zero, so without this the first
 * check would be a full interval away and a device switched off overnight
 * would never check at all. */
#define OTA_FIRST_CHECK_DELAY_MS (2 * 60 * 1000)

/* pdMS_TO_TICKS() casts to TickType_t *before* multiplying by the tick rate,
 * so at 1000 Hz anything past ~71 minutes silently wraps. The 12 hours above
 * came out as 4.17 minutes, and the device really did check that often --
 * measured on hardware at 250s intervals, which is also why the OTA that
 * exposed the SDIO buffer starvation appears in the logs as a "silent
 * auto-check" nobody had asked for (review/ota-sdio-buffer-2026-09-22.md).
 * Doing the multiplication in 64 bits is the whole fix. */
#define APP_MS_TO_TICKS(ms) ((TickType_t)(((uint64_t)(ms) * configTICK_RATE_HZ) / 1000ULL))

_Static_assert(APP_MS_TO_TICKS(OTA_AUTO_CHECK_INTERVAL_MS) >= 3600ULL * configTICK_RATE_HZ,
               "auto-check interval collapsed to under an hour -- tick conversion overflowed");
static int   s_cur_code, s_cur_hum, s_cur_precip;
static float s_cur_temp, s_cur_feel, s_cur_wind;
static struct { char iso[12]; int code, precip; float tmax, tmin, fmax, fmin, wmax; } s_days[WEATHER_UI_DAYS];
static int s_day_count;

static weather_hourly_t s_hourly[WEATHER_UI_DAYS];
static bool s_hourly_valid[WEATHER_UI_DAYS];
static time_t s_last_success;      /* for the header's "data may be outdated" flag */

/* Queried once at worker-task startup instead of on every Device Info push —
 * found by firmware-auditor's Category G pass: esp_hosted_get_coprocessor_fwversion()
 * is a synchronous cross-chip RPC (default 5s timeout), and push_device_info_to_ui()
 * used to call it from inside every push_forecast_to_ui()/ui_error(), both
 * already holding bsp_display_lock() — so a slow/stuck RPC blocked LVGL's
 * renderer for up to 5s on every refresh. The coprocessor's version can only
 * change via ota_update.c's C6 phase, which always esp_restart()s the P4
 * afterward (see ota_update_resume_after_boot()), so a boot-time read is
 * never stale. */
static char s_cp_version[24] = "-";

/* Sunrise/sunset/UV/air-quality row (2026-09-12 sync) — "today" only, same as
 * the design's current.sunriseStr/sunsetStr/uvValue. Air quality comes from a
 * separate API (different host), so it gets its own valid flag: a failed
 * air-quality fetch shouldn't blank out an otherwise-successful forecast. */
static char s_sunrise_iso[20], s_sunset_iso[20];
static float s_uv_max;
static bool  s_uv_valid;
static int   s_aqi;
static bool  s_aqi_valid;

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
        int tls_err = 0, tls_flags = 0;
        esp_http_client_get_and_clear_last_tls_error(c, &tls_err, &tls_flags);
        if (tls_err != 0) {
            /* esp-tls itself only logs the raw hex code (e.g. "-0x008D"); decode it
             * here so an intermittent TLS handshake failure is diagnosable from the
             * log alone. Needs CONFIG_MBEDTLS_ERROR_STRINGS=y (on by default).
             * esp-tls stores the *positive* magnitude (see esp_tls_mbedtls.c:
             * ESP_INT_EVENT_TRACKER_CAPTURE(..., -ret) where ret is mbedtls's own
             * negative return code) - mbedtls_strerror() wants that negative code
             * back, so negate tls_err again here. */
            char tls_msg[128];
            mbedtls_strerror(-tls_err, tls_msg, sizeof(tls_msg));
            ESP_LOGE(TAG, "open failed: %s (tls -0x%04x: %s, verify flags 0x%x)",
                     esp_err_to_name(err), tls_err, tls_msg, tls_flags);
        } else {
            ESP_LOGE(TAG, "open failed: %s", esp_err_to_name(err));
        }
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

/* Reads element `i` of a string array (sunrise/sunset), or NULL if absent. */
static const char *jarr_str(const cJSON *o, const char *k, int i) {
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(o, k);
    if (!cJSON_IsArray(a)) return NULL;
    const cJSON *v = cJSON_GetArrayItem(a, i);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : NULL;
}

/* ---- UI push (always under the display lock) ----------------------------- */

/* Settings > Device information dialog (Claude Design, 2026-09-10; "Last
 * update" row added 2026-09-11). Called from both UI-push points below
 * (display already locked there) rather than on its own schedule — IP/DNS/
 * gateway only change on the same connect/disconnect transitions that
 * already drive weather_ui_set_network_status(), so piggybacking here keeps
 * it current without a separate poll.
 *
 * Hardware version is this board's fixed silicon revision (see CLAUDE.md /
 * the P4 board memory note), not something firmware can read back at
 * runtime. Application version (Coprocessor version's sibling row, "Firmware
 * version" before the 2026-09-18 sync) comes from esp_app_get_description(),
 * which ESP-IDF fills from version.txt at the project root (falls back to
 * `git describe` if that file doesn't exist — see version.txt's own history
 * for why this project keeps one). Coprocessor version is read from s_cp_version,
 * queried once from the ESP32-C6 over the esp_hosted RPC link
 * (esp_hosted_get_coprocessor_fwversion() — same source as the
 * "coprocessor=X.Y.Z" line esp_hosted itself logs at boot when checking
 * host/coprocessor compatibility) at worker-task startup rather than on every
 * call here: that RPC is synchronous with a 5s default timeout, and every
 * call site below already holds bsp_display_lock() — see s_cp_version's own
 * comment for why a boot-time read is never stale anyway. */
/* Called once from weather_task()'s startup, off any display lock — see
 * s_cp_version's own comment for why this doesn't need to run again. */
static void query_coprocessor_version(void) {
    esp_hosted_coprocessor_fwver_t cp_ver;
    if (esp_hosted_get_coprocessor_fwversion(&cp_ver) == ESP_OK) {
        snprintf(s_cp_version, sizeof s_cp_version, "%lu.%lu.%lu",
                 (unsigned long)cp_ver.major1, (unsigned long)cp_ver.minor1, (unsigned long)cp_ver.patch1);
    }
}

/* ip/dns/gw/online are gathered by the caller *before* taking
 * bsp_display_lock() — app_wifi_get_ip_info() -> esp_netif_get_dns_info() is
 * an IPC call to the TCP/IP task with no timeout on this project's lwip
 * config (CONFIG_LWIP_TCPIP_CORE_LOCKING is off), the same shape of bug as
 * the esp_hosted RPC call this file's own comment already flagged and moved
 * off the display lock once before. Every call site below now does the same
 * (found by firmware-auditor Category G): only lv_ and weather_ui_ calls
 * happen while the lock is held. */
static void push_device_info_to_ui(bool online, const char *ip, const char *dns, const char *gw) {
    /* s_last_success is the last successful *forecast* fetch, same value the
     * staleness check above uses — exactly "last update" from the design's
     * own lastFetchSuccessAt. Formatted with the same date/time helpers the
     * header uses, rather than chasing the design's JS Date.toLocaleString()
     * output format. */
    const app_prefs_t *p = app_prefs_get();
    char last_update[48];
    if (s_last_success == 0) {
        snprintf(last_update, sizeof last_update, "%s", weather_strings[p->lang].no_update_yet);
    } else {
        time_t local = s_last_success + s_utc_offset;
        struct tm lt;
        gmtime_r(&local, &lt);
        char date_buf[32], time_buf[16];
        fmt_date(date_buf, sizeof date_buf, &lt, p->lang);
        fmt_time(time_buf, sizeof time_buf, &lt, p->time_fmt);
        snprintf(last_update, sizeof last_update, "%s, %s", date_buf, time_buf);
    }

    weather_device_info_t info = {
        .device_name = "Weather Display",
        .hardware_version = "ESP32-P4 Rev 1.3",
        .firmware_version = esp_app_get_description()->version,
        .coprocessor_version = s_cp_version,
        .online = online,
        .ip = online ? ip : NULL,
        .dns = online ? dns : NULL,
        .gateway = online ? gw : NULL,
        /* FIX: sync from Claude Design 2026-09-12 — unlike ip/dns/gateway,
         * last_update is not gated on being online. */
        .last_update = last_update,
        /* Copied verbatim from the design's deviceInfoNote field. Re-synced
         * 2026-09-19: added "for non-commercial use" before the CC BY 4.0
         * URL, and the URL's own line merged into the "Using Data" line
         * above it (was its own third line) — the design's text keeps
         * churning between syncs, so this is copied fresh each time rather
         * than patched. */
        .note = "Created by M. Thomas using Claude Design and Claude Code.\n"
        "Using Data from Open-Meteo.com and OpenAQ.org. Data Licensed under CC BY 4.0 "
        "for non-commercial use (https://creativecommons.org/licenses/by/4.0/).",
    };
    weather_ui_set_device_info(&info);

    /* Synced from Claude Design 2026-09-12: header "Updated N min ago" text.
     * Piggybacks on the same call sites/cadence as the device-info push above
     * (every successful refresh, every failure, and the ~30s idle tick) —
     * it's a pure function of s_last_success and wall-clock time, no reason
     * for its own schedule. */
    char ago[48];
    fmt_time_ago(ago, sizeof ago, s_last_success, time(NULL), p->lang);
    weather_ui_set_last_sync_ago(ago);
}

/* Maps the Wi-Fi driver's connection state to the header indicator's three
 * states: connected, actively retrying (fast burst or the slower periodic
 * retry that follows it — see app_wifi_is_reconnecting()), or nothing left
 * to retry (no credentials, or the network was forgotten). */
static wx_net_status_t net_status(void) {
    if (app_wifi_is_connected()) return WX_NET_ONLINE;
    return app_wifi_is_reconnecting() ? WX_NET_RECONNECTING : WX_NET_OFFLINE;
}

/* Applies s_net_status.toast to the UI. Must be called right after every
 * network_status_policy_on_fetch(), never on its own — NSP_TOAST_SUCCESS/
 * ERROR are one-shot transitions, not steady states, so replaying a stale
 * value later would re-show a toast nothing just triggered. */
static void apply_toast_state(void) {
    if (!bsp_display_lock(1000)) return;
    switch (s_net_status.toast) {
        case NSP_TOAST_SUCCESS: weather_ui_show_refresh_toast(true); break;
        case NSP_TOAST_ERROR:   weather_ui_show_refresh_toast(false); break;
        case NSP_TOAST_HIDDEN:  weather_ui_hide_refresh_toast(); break;
    }
    bsp_display_unlock();
}

static void ui_error(const char *msg, bool is_manual) {
    network_status_policy_on_fetch(&s_net_status, false, is_manual);
    bool stale = s_last_success == 0 || (time(NULL) - s_last_success) > STALE_AFTER_SEC;
    char ip[16] = "", dns[16] = "", gw[16] = "";
    bool online = app_wifi_get_ip_info(ip, sizeof ip, dns, sizeof dns, gw, sizeof gw);
    if (display_lock_timed(1000, "ui_error")) {
        weather_ui_set_loading(false);
        weather_ui_set_error(msg);
        weather_ui_set_network_status(net_status());
        weather_ui_set_data_stale(stale);
        push_device_info_to_ui(online, ip, dns, gw);
        display_unlock_timed();
    }
    apply_toast_state();
}

/* Renders the cached forecast in the current language/units. */
/* `full`: true rebuilds everything (current conditions, 7-day row, hourly
 * chart) — needed whenever the underlying forecast data or the display
 * language actually changed. false only refreshes the header clock and the
 * "updated N min ago"/stale/network bits, all of which are pure functions
 * of wall-clock time. Split out 2026-09-17 (firmware-auditor Category G):
 * the ~30s idle tick that exists solely to keep the clock live used to call
 * this with full rebuilds every time even though nothing had changed —
 * weather_ui_set_current()/set_days() tear down and rebuild every weather
 * icon (1 + 7, each lv_obj_clean() + rebuilt from scratch) and
 * weather_ui_set_hourly() rebuilds the whole chart (~40 objects, several
 * lv_obj_update_layout() calls), all inside the same bsp_display_lock() the
 * LVGL renderer task also needs. */
static void push_forecast_to_ui(bool full) {
    if (!s_have_forecast) return;
    const app_prefs_t *p = app_prefs_get();
    weather_lang_t lang = p->lang;

    /* Open-Meteo's utc_offset_seconds already encodes the city's DST state, so
     * local wall-clock time is just UTC plus that offset. */
    time_t now_utc = time(NULL);
    time_t local = now_utc + s_utc_offset;
    struct tm lt;
    gmtime_r(&local, &lt);

    char time_str[16], date_str[32]; /* match weather_current_t's time_str/date_str sizes exactly */
    fmt_time(time_str, sizeof time_str, &lt, p->time_fmt);
    fmt_date(date_str, sizeof date_str, &lt, lang);

    bool stale = s_last_success == 0 || (now_utc - s_last_success) > STALE_AFTER_SEC;
    char ip[16] = "", dns[16] = "", gw[16] = "";
    bool online = app_wifi_get_ip_info(ip, sizeof ip, dns, sizeof dns, gw, sizeof gw);

    if (!full) {
        if (!display_lock_timed(1000, "push_forecast_to_ui/!full")) return;
        weather_ui_set_clock(time_str, date_str);
        weather_ui_set_network_status(net_status());
        weather_ui_set_data_stale(stale);
        push_device_info_to_ui(online, ip, dns, gw);
        display_unlock_timed();
        return;
    }

    weather_current_t cur = {0};
    snprintf(cur.location_name, sizeof cur.location_name, "%s", p->name);
    snprintf(cur.location_country, sizeof cur.location_country, "%s", p->country);
    snprintf(cur.time_str, sizeof cur.time_str, "%s", time_str);
    snprintf(cur.date_str, sizeof cur.date_str, "%s", date_str);
    cur.weather_code = s_cur_code;
    cur.temp_c = s_cur_temp;
    cur.feels_like_c = s_cur_feel;
    cur.humidity_pct = s_cur_hum;
    cur.wind_kmh = s_cur_wind;
    cur.precip_pct = s_cur_precip;
    fmt_real_feel(cur.real_feel_text, sizeof cur.real_feel_text,
                  s_cur_feel, s_cur_temp, s_cur_wind, s_cur_precip, lang);
    fmt_iso_time(cur.sunrise_str, sizeof cur.sunrise_str, s_sunrise_iso, p->time_fmt);
    fmt_iso_time(cur.sunset_str, sizeof cur.sunset_str, s_sunset_iso, p->time_fmt);
    if (s_uv_valid) snprintf(cur.uv_display, sizeof cur.uv_display, "%d", (int)lroundf(s_uv_max));
    else            snprintf(cur.uv_display, sizeof cur.uv_display, "\xE2\x80\x93"); /* "–" */
    snprintf(cur.uv_cat, sizeof cur.uv_cat, "%s", uv_category(s_uv_valid, s_uv_max, lang));
    if (s_aqi_valid) snprintf(cur.aqi_display, sizeof cur.aqi_display, "%d", s_aqi);
    else             snprintf(cur.aqi_display, sizeof cur.aqi_display, "\xE2\x80\x93");
    snprintf(cur.aqi_cat, sizeof cur.aqi_cat, "%s", aqi_category(s_aqi_valid, s_aqi, lang));

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

    if (!display_lock_timed(1000, "push_forecast_to_ui/full")) return;
    weather_ui_set_error(NULL);
    weather_ui_set_current(&cur);
    weather_ui_set_days(days);
    weather_ui_set_hourly(day_ptrs[0], day_ptrs);
    weather_ui_set_network_status(net_status());
    weather_ui_set_data_stale(stale);
    weather_ui_set_loading(false);
    push_device_info_to_ui(online, ip, dns, gw);
    display_unlock_timed();
}

/* ---- forecast ------------------------------------------------------------ */

/* is_manual: only the refresh icon / error-bar retry button (both funnel
 * through app_weather_refresh() -> CMD_REFRESH) show the toast Claude Design
 * added for this — periodic auto-refresh, city selection and the post-Wi-Fi-
 * connect refresh all call this same function but stay silent, matching the
 * design's own refresh() handler being the only place refreshToast is set. */
static bool do_refresh_body(bool is_manual) {
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
             "precipitation_probability_max,wind_speed_10m_max,"
             "sunrise,sunset,uv_index_max"
             "&timezone=auto&forecast_days=%d",
             p->lat, p->lon, WEATHER_UI_DAYS);

    char *body = http_get(url);
    if (!body) { ui_error(weather_strings[p->lang].error, is_manual); return false; }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) { ui_error(weather_strings[p->lang].error, is_manual); return false; }

    const cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "current");
    const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    if (!cJSON_IsObject(cur) || !cJSON_IsObject(daily)) {
        cJSON_Delete(root);
        ui_error(weather_strings[p->lang].error, is_manual);
        return false;
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

    /* Sunrise/sunset/UV: "today" only (index 0), matching the design's
     * current.sunriseStr/sunsetStr/uvValue. */
    const char *sunrise = jarr_str(daily, "sunrise", 0);
    const char *sunset  = jarr_str(daily, "sunset", 0);
    snprintf(s_sunrise_iso, sizeof s_sunrise_iso, "%s", sunrise ? sunrise : "");
    snprintf(s_sunset_iso, sizeof s_sunset_iso, "%s", sunset ? sunset : "");
    s_uv_valid = cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(daily, "uv_index_max")) && n > 0;
    s_uv_max = jarr(daily, "uv_index_max", 0, 0);

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

    /* Air quality: separate host, separate request. Non-fatal on failure —
     * an otherwise-good forecast shouldn't turn into an error screen just
     * because this one extra call didn't land; the UI shows "–" instead
     * (see aqi_category()'s has_value=false case). */
    s_aqi_valid = false;
    char aq_url[192];
    snprintf(aq_url, sizeof aq_url,
             "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f"
             "&current=us_aqi&timezone=auto",
             p->lat, p->lon);
    char *aq_body = http_get(aq_url);
    if (aq_body) {
        cJSON *aq_root = cJSON_Parse(aq_body);
        free(aq_body);
        if (aq_root) {
            const cJSON *aq_cur = cJSON_GetObjectItemCaseSensitive(aq_root, "current");
            const cJSON *aqi_val = cJSON_IsObject(aq_cur) ? cJSON_GetObjectItemCaseSensitive(aq_cur, "us_aqi") : NULL;
            if (cJSON_IsNumber(aqi_val)) { s_aqi = (int)aqi_val->valuedouble; s_aqi_valid = true; }
            cJSON_Delete(aq_root);
        }
    }
    if (!s_aqi_valid) ESP_LOGW(TAG, "air-quality fetch failed, showing '-' for it");

    s_last_success = time(NULL);
    s_have_forecast = true;
    ESP_LOGI(TAG, "forecast ok: %.1fC code=%d, %d days", s_cur_temp, s_cur_code, n);
    push_forecast_to_ui(true);
    network_status_policy_on_fetch(&s_net_status, true, is_manual);
    apply_toast_state();
    return true;
}

/* Serializes do_refresh_body() against the OTA tasks (see s_network_mutex's
 * own comment) — do_refresh_body() has several early returns, so wrapping it
 * here rather than taking/releasing inline at each one avoids a forgotten
 * release on some future edit. */
static bool do_refresh(bool is_manual) {
    if (xSemaphoreTake(s_network_mutex, pdMS_TO_TICKS(NETWORK_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "network mutex busy for %ds, skipping refresh", NETWORK_MUTEX_TIMEOUT_MS / 1000);
        ui_error(weather_strings[app_prefs_get()->lang].error, is_manual);
        return false;
    }
    bool ok = do_refresh_body(is_manual);
    xSemaphoreGive(s_network_mutex);

    /* Escalate a run of failures that Wi-Fi itself never reported. Both
     * actions are cheap and run outside the mutex above: app_wifi's own
     * worker task owns the blocking part. */
    switch (link_health_policy_on_fetch(&s_link_health, ok, app_wifi_is_connected())) {
    case LHP_ACT_VERIFY_LINK:
        ESP_LOGW(TAG, "%d failed fetches while Wi-Fi reports Online; checking the link",
                 s_link_health.consecutive_failures);
        app_wifi_verify_link();
        break;
    case LHP_ACT_FORCE_RECONNECT:
        ESP_LOGE(TAG, "link still dead after a check; forcing a reconnect (escalation %d)",
                 s_link_health.escalations);
        app_wifi_force_reconnect();
        break;
    case LHP_ACT_NONE:
        break;
    }
    return ok;
}

/* ---- geocoding ----------------------------------------------------------- */

/* Re-renders the search-results list from s_hits/s_hit_count, recomputing
 * each row's favorite-star color from the current favorites set. Called
 * both when a search resolves and whenever the favorites set itself
 * changes (toggle/remove) while results are still on screen. */
static void push_search_results_to_ui(void) {
    const char *names[APP_WEATHER_MAX_RESULTS];
    const char *subs[APP_WEATHER_MAX_RESULTS];
    bool is_fav[APP_WEATHER_MAX_RESULTS];
    const app_favorite_t *favs = app_favorites_get();
    for (int i = 0; i < s_hit_count; i++) {
        names[i] = s_hits[i].name;
        subs[i] = s_hits[i].label;
        is_fav[i] = app_favorites_contains(favs, s_hits[i].lat, s_hits[i].lon);
    }
    if (bsp_display_lock(1000)) {
        weather_ui_set_search_results(names, subs, is_fav, s_hit_count);
        bsp_display_unlock();
    }
}

/* Re-renders the favorite-slot row from app_favorites_get(). Called at
 * worker startup and after every toggle/remove. */
static void push_favorites_to_ui(void) {
    const app_favorite_t *favs = app_favorites_get();
    const char *names[APP_FAVORITES_MAX];
    bool used[APP_FAVORITES_MAX];
    for (int i = 0; i < APP_FAVORITES_MAX; i++) {
        names[i] = favs[i].name;
        used[i] = favs[i].used;
    }
    if (bsp_display_lock(1000)) {
        weather_ui_set_favorites(names, used);
        bsp_display_unlock();
    }
}

static void push_ota_state_to_ui(wx_ota_status_t status, int progress) {
    if (bsp_display_lock(1000)) {
        weather_ui_set_ota_state(status, progress);
        bsp_display_unlock();
    }
}

static void push_ota_error_to_ui(ota_error_t err) {
    const weather_strings_t *s = &weather_strings[app_prefs_get()->lang];
    const char *msg;
    switch (err) {
        case OTA_ERR_NETWORK:     msg = s->ota_error_network; break;
        case OTA_ERR_MANIFEST:    msg = s->ota_error_manifest; break;
        case OTA_ERR_CHECKSUM:    msg = s->ota_error_checksum; break;
        case OTA_ERR_COPROCESSOR: msg = s->ota_error_coprocessor; break;
        case OTA_ERR_FLASH:
        case OTA_ERR_NONE:
        default:                  msg = s->ota_error_flash; break;
    }
    if (bsp_display_lock(1000)) {
        weather_ui_set_ota_error(msg);
        bsp_display_unlock();
    }
}

/* ota_update_run()'s progress callback — `ctx` points at the bool guarding
 * whether this run is the silent periodic auto-check (see cmd_t.ota_silent). */
static void ota_on_progress(int percent, void *ctx) {
    task_heartbeat_touch(HB_OTA);
    bool silent = *(bool *)ctx;
    if (!silent) push_ota_state_to_ui(WX_OTA_DOWNLOADING, percent);
}

/* Runs ota_update_run() on its own task instead of inline in weather_task —
 * a multi-MB download can take tens of seconds to minutes, and weather_task
 * is the only consumer of the command queue that also serves search,
 * refresh, Wi-Fi and favorites; blocking it there silently starved those
 * (found by lvgl-reviewer 2026-09-17). `arg` is a heap-allocated
 * ota_task_args_t, freed here. */
typedef struct { bool silent; } ota_task_args_t;

static void ota_worker_task(void *arg) {
    ota_task_args_t *args = (ota_task_args_t *)arg;
    bool silent = args->silent;
    free(args);
    task_heartbeat_touch(HB_OTA);

    if (!silent) push_ota_state_to_ui(WX_OTA_DOWNLOADING, 0);

    /* Held only for the actual network/flash work, not the reboot-idle-wait
     * below — see s_network_mutex's own comment for why this exists. */
    ota_outcome_t res;
    if (xSemaphoreTake(s_network_mutex, pdMS_TO_TICKS(NETWORK_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "network mutex busy for %ds, aborting OTA check", NETWORK_MUTEX_TIMEOUT_MS / 1000);
        res = (ota_outcome_t){ .status = OTA_RESULT_ERROR, .error = OTA_ERR_NETWORK };
    } else {
        res = ota_update_run(ota_on_progress, &silent);
        xSemaphoreGive(s_network_mutex);
    }

    switch (res.status) {
        case OTA_RESULT_UP_TO_DATE:
            if (!silent) push_ota_state_to_ui(WX_OTA_IDLE, 0);
            break;
        case OTA_RESULT_UPDATED_REBOOTING:
            push_ota_state_to_ui(WX_OTA_DONE, 100);
            if (silent) {
                /* A silent background check found and flashed an update —
                 * don't reboot into it while the user is mid-interaction
                 * (typing a Wi-Fi password, searching, ...) with no visible
                 * warning (found by lvgl-reviewer 2026-09-17). Wait for no
                 * modal to be open, but bounded: the rollback safety net
                 * only starts once the new image actually boots, so this
                 * can't be allowed to wait forever on a device that always
                 * has something open. */
                for (int waited_ms = 0; waited_ms < 5 * 60 * 1000; waited_ms += 5000) {
                    task_heartbeat_touch(HB_OTA);
                    bool busy = true;
                    if (bsp_display_lock(1000)) {
                        busy = weather_ui_is_modal_open();
                        bsp_display_unlock();
                    }
                    if (!busy) break;
                    vTaskDelay(pdMS_TO_TICKS(5000));
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(1500)); /* let the UI actually show "rebooting" */
            }
            esp_restart();
            break; /* unreachable */
        case OTA_RESULT_ERROR:
            if (res.error == OTA_ERR_CANCELLED) {
                push_ota_state_to_ui(WX_OTA_IDLE, 0);
            } else if (!silent) {
                push_ota_error_to_ui(res.error);
            } else {
                ESP_LOGW(TAG, "silent OTA auto-check failed (error %d)", res.error);
            }
            break;
    }
    task_heartbeat_mark_idle(HB_OTA);
    s_ota_active = false;
    vTaskDelete(NULL);
}

/* Companion to ota_worker_task, for CMD_OTA_RESUME — same reasoning: this
 * runs a full manifest fetch and potentially the C6 flash + reboot, which
 * must not block weather_task either. */
static void ota_resume_task(void *arg) {
    LV_UNUSED(arg);
    task_heartbeat_touch(HB_OTA_RESUME);
    if (xSemaphoreTake(s_network_mutex, pdMS_TO_TICKS(NETWORK_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        ota_update_resume_after_boot(app_prefs_get()->ota_update_coprocessor);
        xSemaphoreGive(s_network_mutex);
    } else {
        ESP_LOGE(TAG, "network mutex busy for %ds, skipping post-update coprocessor check",
                 NETWORK_MUTEX_TIMEOUT_MS / 1000);
    }
    task_heartbeat_mark_idle(HB_OTA_RESUME);
    s_ota_active = false;
    vTaskDelete(NULL);
}

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

    if (bsp_display_lock(1000)) { weather_ui_set_searching(false); bsp_display_unlock(); }
    push_search_results_to_ui();
}

/* ---- worker -------------------------------------------------------------- */

static void weather_task(void *arg) {
    LV_UNUSED(arg);
    cmd_t cmd;
    cmd_t pending_search;
    bool have_pending = false;
    TickType_t search_due = 0;

    TickType_t last_auto = xTaskGetTickCount();
    query_coprocessor_version();
    push_favorites_to_ui();

    /* Initial connect attempt, moved here from a blocking call in app_main()
     * (main/main.c) — see that call site's own comment. Uses the same
     * mutex/timeout as every other network op this task runs, so a wedged
     * OTA task can delay this by at most NETWORK_MUTEX_TIMEOUT_MS instead of
     * blocking it forever. */
    if (app_wifi_have_credentials() && !app_wifi_is_connected()) {
        bool connect_ok = false;
        if (xSemaphoreTake(s_network_mutex, pdMS_TO_TICKS(NETWORK_MUTEX_TIMEOUT_MS)) == pdTRUE) {
            connect_ok = app_wifi_connect();
            xSemaphoreGive(s_network_mutex);
        } else {
            ESP_LOGE(TAG, "network mutex busy for %ds, skipping initial Wi-Fi connect",
                     NETWORK_MUTEX_TIMEOUT_MS / 1000);
        }
        if (bsp_display_lock(1000)) {
            weather_ui_set_network_status(net_status());
            bsp_display_unlock();
        }
        if (connect_ok) {
            /* Clock first: do_refresh() stamps s_last_success with time(NULL),
             * so fetching before SNTP would mark fresh data as decades old.
             * The wait is bounded so a missing NTP answer can't hold the
             * weather back — SNTP keeps retrying in the background and the
             * idle tick picks the clock up whenever it lands. With DNS
             * working (see the lwIP port-range note in the top-level
             * CMakeLists.txt) this normally returns within ~1s. */
            app_wifi_sync_time(STARTUP_SNTP_WAIT_MS);
        } else if (bsp_display_lock(1000)) {
            weather_ui_open_wifi_setup();
            bsp_display_unlock();
        }
    }

    bool wifi_was_connected = app_wifi_is_connected();
    if (wifi_was_connected) {
        startup_retry_policy_t refresh_retry;
        startup_retry_policy_init(&refresh_retry, 0);
        int delay_ms;
        while (!do_refresh(false) && startup_retry_policy_next(&refresh_retry, &delay_ms)) {
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
    }

    for (;;) {
        task_heartbeat_touch(HB_WEATHER);
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
                        do_refresh(false);
                        last_auto = xTaskGetTickCount();
                    }
                    break;
                case CMD_REFRESH:
                    do_refresh(true);
                    last_auto = xTaskGetTickCount();
                    break;
                case CMD_RELANG:
                    push_forecast_to_ui(true); /* language changed: day labels/captions must re-render */
                    break;
                case CMD_WIFI_SCAN: {
                    if (xSemaphoreTake(s_network_mutex, pdMS_TO_TICKS(NETWORK_MUTEX_TIMEOUT_MS)) != pdTRUE) {
                        ESP_LOGE(TAG, "network mutex busy for %ds, skipping Wi-Fi scan", NETWORK_MUTEX_TIMEOUT_MS / 1000);
                        break;
                    }
                    wx_wifi_network_t nets[APP_WIFI_MAX_SCAN];
                    int n = app_wifi_scan(nets, APP_WIFI_MAX_SCAN);
                    xSemaphoreGive(s_network_mutex);
                    if (bsp_display_lock(1000)) {
                        weather_ui_set_wifi_scan_results(nets, n);
                        bsp_display_unlock();
                    }
                    break;
                }
                case CMD_WIFI_CONNECT: {
                    if (xSemaphoreTake(s_network_mutex, pdMS_TO_TICKS(NETWORK_MUTEX_TIMEOUT_MS)) != pdTRUE) {
                        ESP_LOGE(TAG, "network mutex busy for %ds, skipping Wi-Fi connect", NETWORK_MUTEX_TIMEOUT_MS / 1000);
                        if (bsp_display_lock(1000)) {
                            weather_ui_set_wifi_connect_result(false);
                            bsp_display_unlock();
                        }
                        break;
                    }
                    bool ok = app_wifi_connect_with(cmd.ssid, cmd.pass);
                    xSemaphoreGive(s_network_mutex);
                    if (bsp_display_lock(1000)) {
                        weather_ui_set_wifi_connect_result(ok);
                        /* Not a plain `ok ? ONLINE : OFFLINE`: a failed manual attempt
                         * may still have left the driver's own retry burst/timer armed
                         * (see app_wifi_is_reconnecting()), which net_status() reflects. */
                        weather_ui_set_network_status(net_status());
                        bsp_display_unlock();
                    }
                    if (ok) {
                        app_wifi_sync_time(STARTUP_SNTP_WAIT_MS);
                        do_refresh(false);
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
                case CMD_FAV_TOGGLE:
                    if (cmd.index >= 0 && cmd.index < s_hit_count) {
                        geo_hit_t *h = &s_hits[cmd.index];
                        app_favorites_toggle_save(h->name, h->country, h->lat, h->lon);
                        push_favorites_to_ui();
                        push_search_results_to_ui();
                    }
                    break;
                case CMD_FAV_SELECT: {
                    const app_favorite_t *favs = app_favorites_get();
                    if (cmd.index >= 0 && cmd.index < APP_FAVORITES_MAX && favs[cmd.index].used) {
                        const app_favorite_t *f = &favs[cmd.index];
                        app_prefs_save_city(f->name, f->country, f->lat, f->lon);
                        do_refresh(false);
                        last_auto = xTaskGetTickCount();
                    }
                    break;
                }
                case CMD_FAV_REMOVE:
                    app_favorites_remove_save(cmd.index);
                    push_favorites_to_ui();
                    push_search_results_to_ui();
                    break;
                case CMD_OTA_START: {
                    if (s_ota_active) break;
                    ota_task_args_t *args = malloc(sizeof *args);
                    if (!args) break;
                    args->silent = cmd.ota_silent;
                    s_ota_active = true;
                    s_last_ota_check = xTaskGetTickCount();
                    /* Clear here, at acceptance, not inside ota_update_run(): a
                     * Cancel tap arriving while a prior run was still waiting on
                     * the network mutex must survive to be seen by that run's
                     * own s_cancel_requested check (firmware-auditor Category K). */
                    ota_update_reset_cancel();
                    if (xTaskCreatePinnedToCore(ota_worker_task, "ota", 8192, args, 3, NULL, 0) != pdPASS) {
                        free(args);
                        s_ota_active = false;
                        /* No dedicated "couldn't even start" category; OTA_ERR_FLASH
                         * is the closest existing one and this should be vanishingly
                         * rare (task-creation failure, e.g. out of memory). */
                        if (!cmd.ota_silent) push_ota_error_to_ui(OTA_ERR_FLASH);
                    }
                    break;
                }
                case CMD_OTA_RESUME:
                    if (s_ota_active) break;
                    s_ota_active = true;
                    if (xTaskCreatePinnedToCore(ota_resume_task, "ota_resume", 8192, NULL, 3, NULL, 0) != pdPASS) {
                        s_ota_active = false;
                    }
                    break;
            }
        }

        if (have_pending && (int32_t)(xTaskGetTickCount() - search_due) >= 0) {
            have_pending = false;
            do_search(pending_search.query);
        }

        /* Wi-Fi came back on its own (app_wifi.c's own retry burst/periodic timer,
         * not a user action here) — refresh right away instead of waiting for the
         * next scheduled auto-refresh or clock tick, and update the header
         * immediately either way so "Reconnecting..."/"Offline" isn't stale for up
         * to ~30s (the idle clock tick's own cadence). */
        bool wifi_now_connected = app_wifi_is_connected();
        if (wifi_now_connected != wifi_was_connected) {
            if (bsp_display_lock(1000)) {
                weather_ui_set_network_status(net_status());
                bsp_display_unlock();
            }
            if (wifi_now_connected) {
                do_refresh(false);
                last_auto = xTaskGetTickCount();
            }
            wifi_was_connected = wifi_now_connected;
        }

        /* Periodic refresh, and a per-minute clock tick so the header stays live.
         * Synced from Claude Design 2026-09-12: the interval is now the user's
         * auto_refresh_minutes setting (Settings > Auto-refresh), not the fixed
         * CONFIG_WEATHER_REFRESH_MINUTES Kconfig value — 0 disables periodic
         * refresh entirely (the ~30s tick below still keeps the clock/"updated
         * N min ago" text live either way). */
        TickType_t now = xTaskGetTickCount();
        int auto_refresh_min = app_prefs_get()->auto_refresh_minutes;
        /* APP_MS_TO_TICKS, not pdMS_TO_TICKS: 60 minutes is only 19% below the
         * value where the stock macro wraps, so adding a longer option to the
         * Settings dialog would silently shorten the interval instead. */
        if (auto_refresh_min > 0 && (now - last_auto) >= APP_MS_TO_TICKS(auto_refresh_min * 60 * 1000)) {
            do_refresh(false);
            last_auto = now;
        } else if (s_have_forecast) {
            static int tick = 0;
            if (++tick >= 60) { tick = 0; push_forecast_to_ui(false); }  /* ~30 s, clock-only */
        }

        /* Periodic silent OTA check, gated by the dialog's own auto-update
         * switch (app_prefs_get()->ota_auto_update — its hint text already
         * promises "checks every 12 hours"). Posting a command here rather
         * than calling ota_update_run() inline keeps CMD_OTA_START the only
         * place that actually runs it, so a manual tap and the timer can't
         * race each other (s_ota_active guards both). */
        TickType_t ota_check_due = s_ota_checked_once
                ? APP_MS_TO_TICKS(OTA_AUTO_CHECK_INTERVAL_MS)
                : APP_MS_TO_TICKS(OTA_FIRST_CHECK_DELAY_MS);
        if (!s_ota_active && app_prefs_get()->ota_auto_update &&
            (now - s_last_ota_check) >= ota_check_due) {
            cmd_t c = { .kind = CMD_OTA_START, .ota_silent = true };
            if (s_q) xQueueSend(s_q, &c, 0);
            s_ota_checked_once = true;
            s_last_ota_check = now; /* also set on entry to CMD_OTA_START; set here too so a
                                      * slow-to-process queue doesn't fire this every loop tick */
        }
    }
}

/* ---- public API ---------------------------------------------------------- */

static void post(cmd_t *c) {
    if (s_q) xQueueSend(s_q, c, 0);
}

void app_weather_start(void) {
    s_q = xQueueCreate(8, sizeof(cmd_t));
    s_network_mutex = xSemaphoreCreateMutex();
    network_status_policy_reset(&s_net_status);
    link_health_policy_init(&s_link_health, LINK_HEALTH_VERIFY_AFTER, LINK_HEALTH_RECONNECT_AFTER);
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

void app_weather_toggle_favorite(int result_index) {
    cmd_t c = { .kind = CMD_FAV_TOGGLE, .index = result_index };
    post(&c);
}

void app_weather_select_favorite(int slot_index) {
    cmd_t c = { .kind = CMD_FAV_SELECT, .index = slot_index };
    post(&c);
}

void app_weather_remove_favorite(int slot_index) {
    cmd_t c = { .kind = CMD_FAV_REMOVE, .index = slot_index };
    post(&c);
}

void app_weather_ota_start(void) {
    cmd_t c = { .kind = CMD_OTA_START };
    post(&c);
}

/* Fire-and-forget, no queue round-trip: sets ota_update.c's cooperative
 * cancel flag directly, same reasoning as the toggle functions below — it's
 * safe to call from any task (see ota_update_request_cancel()'s doc
 * comment), and the OTA task polls it on its own between HTTP
 * reads/esp_https_ota_perform() iterations. */
void app_weather_ota_cancel(void) {
    ota_update_request_cancel();
}

/* Persist immediately, same as app_prefs_save_brightness_adaptive() being
 * called straight from the LVGL task's switch handler — no worker-queue
 * round-trip needed for a plain settings write. */
void app_weather_ota_toggle_auto_update(bool on) {
    app_prefs_save_ota_settings(on, app_prefs_get()->ota_update_coprocessor);
}

void app_weather_ota_toggle_update_coprocessor(bool on) {
    app_prefs_save_ota_settings(app_prefs_get()->ota_auto_update, on);
}

void app_weather_ota_resume_after_boot(void) {
    cmd_t c = { .kind = CMD_OTA_RESUME };
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
