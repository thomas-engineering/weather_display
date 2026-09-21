#ifndef WEATHER_UI_H
#define WEATHER_UI_H

#include "lvgl.h"
#include "weather_i18n.h"
#include <stdbool.h>

#define WEATHER_UI_DAYS 7

/* Matches APP_FAVORITES_MAX (components/app_logic/include/favorites.h) by
 * convention, not by #include — weather_ui.c stays hardware/app_logic-free
 * per CLAUDE.md's Codeorganisation, so it only knows a plain slot count. */
#define WEATHER_UI_FAVORITES_MAX 6

typedef enum { WX_UNIT_C, WX_UNIT_F } wx_temp_unit_t;
typedef enum { WX_WIND_KMH, WX_WIND_MPH, WX_WIND_MS } wx_wind_unit_t;
typedef enum { WX_TIME_24, WX_TIME_12 } wx_time_fmt_t;

/* Canonical units in: Celsius, km/h. Display conversion happens inside weather_ui.c
 * based on the unit the user picked in Settings — feed raw Open-Meteo values here. */
typedef struct {
    char location_name[64];
    char location_country[64];
    char time_str[16];   /* pre-formatted by the app, e.g. "14:05" or "2:05 PM" */
    char date_str[32];   /* pre-formatted by the app, e.g. "Thu, Sep 3" */
    int weather_code;    /* raw Open-Meteo WMO weather_code */
    float temp_c;
    float feels_like_c;
    int humidity_pct;
    float wind_kmh;
    int precip_pct;
    char real_feel_text[160]; /* pre-composed sentence, already localized by the app */
    /* Sunrise/sunset/UV/air-quality row (2026-09-12 sync). All pre-formatted
     * by the app; empty string ("") hides that item's value (no reading yet,
     * e.g. air quality fetch failed). */
    char sunrise_str[8];  /* "06:45" or "6:45 AM" */
    char sunset_str[8];
    char uv_display[8];   /* rounded UV index, e.g. "5" */
    char uv_cat[24];      /* localized category, e.g. "Moderate" */
    char aqi_display[8];  /* rounded US AQI, e.g. "42" */
    char aqi_cat[24];
} weather_current_t;

typedef struct {
    char day_label[16];  /* "Today" / "Tue" — pre-localized by the app */
    char date_label[16]; /* "Sep 3" — pre-localized by the app */
    int weather_code;
    float temp_max_c, temp_min_c;
    float feels_max_c, feels_min_c;
    float wind_max_kmh;
    int precip_pct;
} weather_day_t;

#define WEATHER_UI_CHART_POINTS 24

/* One day's hourly series (from Open-Meteo's `hourly` block) for the temperature
 * line + precipitation bar chart shown on the current-weather card and in the
 * day-detail panel. `count` may be < WEATHER_UI_CHART_POINTS for the last partial day. */
typedef struct {
    int count;
    float temp_c[WEATHER_UI_CHART_POINTS];       /* canonical Celsius; converted for display internally */
    float precip_mm[WEATHER_UI_CHART_POINTS];
    int hour[WEATHER_UI_CHART_POINTS];            /* 0-23, local time */
} weather_hourly_t;

typedef enum { WX_NET_ONLINE, WX_NET_OFFLINE, WX_NET_RECONNECTING } wx_net_status_t;

typedef struct {
    char ssid[33];
    int strength;   /* 0-3 bars */
    bool secured;
} wx_wifi_network_t;

typedef void (*weather_ui_search_cb_t)(const char *query);
typedef void (*weather_ui_select_city_cb_t)(int result_index);
typedef void (*weather_ui_refresh_cb_t)(void);
/* auto_refresh_minutes: 0/15/30/60, added 2026-09-12 sync. */
typedef void (*weather_ui_settings_changed_cb_t)(weather_lang_t lang, wx_temp_unit_t temp, wx_wind_unit_t wind, wx_time_fmt_t time_fmt, int auto_refresh_minutes);
typedef void (*weather_ui_wifi_scan_cb_t)(void);
typedef void (*weather_ui_wifi_connect_cb_t)(const char *ssid, const char *password);
typedef void (*weather_ui_wifi_forget_cb_t)(void);
/* `percent` is 10-100 (the slider's own range, matching the design). `final` is
 * false while the user is still dragging (apply the level but don't wear NVS
 * out writing every intermediate tick) and true once on release (persist it). */
typedef void (*weather_ui_brightness_cb_t)(int percent, bool final);
/* `on` is the switch's new state. Fires both when the user taps the switch
 * directly and when dragging the manual slider turns adaptive mode off (the
 * two controls fight over the same backlight, so a manual drag wins). */
typedef void (*weather_ui_brightness_adaptive_cb_t)(bool on);
/* Favorites, added for the location-selection screen (Claude Design's
 * favorites/favoriteSlots state, "Weather App.dc.html"). `result_index` /
 * `slot_index` are indices into the arrays most recently passed to
 * weather_ui_set_search_results() / weather_ui_set_favorites() respectively
 * — the app resolves them back to a city (it already holds the lat/lon,
 * the UI never does). Toggling and removing don't close the search screen;
 * selecting a favorite does, same as picking a plain search result. */
typedef void (*weather_ui_favorite_toggle_cb_t)(int result_index);
typedef void (*weather_ui_favorite_select_cb_t)(int slot_index);
typedef void (*weather_ui_favorite_remove_cb_t)(int slot_index);

/* Firmware update dialog (Settings > Network > "Update", 2026-09-19 sync,
 * real download/flash added 2026-09-16). on_start fires the "Check for
 * update"/"Downloading"/"Update complete — rebooting" button — the app owns
 * what that actually does (main/ota_update.c) and drives the visible state
 * back in via weather_ui_set_ota_state()/weather_ui_set_ota_error(). The two
 * switches persist immediately on toggle, same one-way pattern as
 * brightness_adaptive. on_cancel fires only from the dialog's "Cancel"
 * button (added 2026-09-17 alongside the real download/flash) — it requests
 * a real, cooperative abort of an in-flight check/download, unlike tapping
 * the backdrop or the "X" close icon, which just hide the dialog and let
 * anything in flight keep running in the background. */
typedef void (*weather_ui_ota_start_cb_t)(void);
typedef void (*weather_ui_ota_cancel_cb_t)(void);
typedef void (*weather_ui_ota_toggle_auto_update_cb_t)(bool on);
typedef void (*weather_ui_ota_toggle_update_coprocessor_cb_t)(bool on);

/* Builds the whole 1024x600 screen as a child of `parent` (typically lv_screen_active()). */
void weather_ui_create(lv_obj_t *parent);

void weather_ui_set_callbacks(weather_ui_search_cb_t on_search, weather_ui_select_city_cb_t on_select_city,
                               weather_ui_refresh_cb_t on_refresh, weather_ui_settings_changed_cb_t on_settings_changed);
void weather_ui_set_wifi_callbacks(weather_ui_wifi_scan_cb_t on_scan, weather_ui_wifi_connect_cb_t on_connect,
                                    weather_ui_wifi_forget_cb_t on_forget);
void weather_ui_set_brightness_callback(weather_ui_brightness_cb_t on_brightness);
void weather_ui_set_brightness_adaptive_callback(weather_ui_brightness_adaptive_cb_t on_adaptive);
void weather_ui_set_favorite_callbacks(weather_ui_favorite_toggle_cb_t on_toggle,
                                        weather_ui_favorite_select_cb_t on_select,
                                        weather_ui_favorite_remove_cb_t on_remove);
void weather_ui_set_ota_callbacks(weather_ui_ota_start_cb_t on_start,
                                   weather_ui_ota_cancel_cb_t on_cancel,
                                   weather_ui_ota_toggle_auto_update_cb_t on_toggle_auto_update,
                                   weather_ui_ota_toggle_update_coprocessor_cb_t on_toggle_update_coprocessor);

void weather_ui_set_language(weather_lang_t lang);
void weather_ui_set_units(wx_temp_unit_t temp, wx_wind_unit_t wind, wx_time_fmt_t time_fmt);
/* Sets the Settings panel's brightness slider (10-100) without firing on_brightness
 * — call once at startup with the persisted value. */
void weather_ui_set_brightness(int percent);
/* Sets the adaptive-brightness switch without firing on_adaptive — call once
 * at startup with the persisted value (already ANDed with camera presence by
 * the caller) and again whenever app_light's own sampling loop needs the UI
 * to reflect a state it didn't originate (it never does today, but this is
 * the same one-way sync pattern as weather_ui_set_brightness()). */
void weather_ui_set_brightness_adaptive(bool on);
/* Greys the switch out (and forces it visually off) when no camera was
 * found — call once at startup, before weather_ui_set_brightness_adaptive(). */
void weather_ui_set_brightness_adaptive_available(bool available);
/* Sets the Settings panel's auto-refresh segmented control (0/15/30/60)
 * without firing on_settings_changed — call once at startup with the
 * persisted value, same one-way pattern as weather_ui_set_brightness(). */
void weather_ui_set_auto_refresh(int minutes);

/* Firmware update dialog state. WX_OTA_ERROR was added alongside the real
 * download/flash logic (main/ota_update.c) — the design's own fidelity only
 * had the three original states (no real network activity behind it), which
 * can't fail this way. */
typedef enum { WX_OTA_IDLE, WX_OTA_DOWNLOADING, WX_OTA_DONE, WX_OTA_ERROR } wx_ota_status_t;
/* progress is 0-100, only meaningful (shown) while status is WX_OTA_DOWNLOADING. */
void weather_ui_set_ota_state(wx_ota_status_t status, int progress);
/* Puts the dialog in the WX_OTA_ERROR state with `message` as the status
 * line (already localized by the caller — see weather_i18n.h's ota_error_*
 * strings) and re-enables the start button so the user can retry. */
void weather_ui_set_ota_error(const char *message);
/* One-way sync for the two switches, same pattern as
 * weather_ui_set_brightness_adaptive() — call once at startup with the
 * persisted value; toggling in the UI fires the on_toggle_* callback rather
 * than looping back through these. */
void weather_ui_set_ota_auto_update(bool on);
void weather_ui_set_ota_update_coprocessor(bool on);
/* True while any modal dialog (Settings, Wi-Fi setup/forget-confirm, Device
 * info, day detail, city search, or this dialog itself) is open. Added for
 * the silent 12h OTA auto-check (see app_weather.c) to avoid rebooting into
 * a freshly-flashed P4 image while the user is mid-interaction — e.g.
 * typing a Wi-Fi password — with no visible warning. Call under
 * bsp_display_lock() like any other weather_ui_* read from the worker
 * task. */
bool weather_ui_is_modal_open(void);

/* Header "last synced" text, next to the online/offline indicator — pass ""
 * to hide it. The app owns the timing (recompute periodically, same as the
 * data_stale flag), this just displays whatever string it's given. */
void weather_ui_set_last_sync_ago(const char *text);

/* Just the header clock (top-right time/date labels) — added 2026-09-17 so
 * the ~30s idle tick that exists purely to keep the clock live doesn't have
 * to go through weather_ui_set_current(), which also tears down and
 * rebuilds the current-conditions weather icon on every call even though
 * the weather itself hasn't changed (found by firmware-auditor's Category G
 * pass: this and weather_ui_set_days()/set_hourly()'s icon/chart rebuilds
 * ran unconditionally on every render, all under bsp_display_lock). */
void weather_ui_set_clock(const char *time_str, const char *date_str);

void weather_ui_set_current(const weather_current_t *cur);
void weather_ui_set_days(const weather_day_t days[WEATHER_UI_DAYS]);
/* Hourly series for "today" (shown inline on the current-weather card) and one
 * per forecast day (shown when that day's detail panel is opened), same order
 * as weather_ui_set_days(). Pass NULL entries for days you have no hourly data for. */
void weather_ui_set_hourly(const weather_hourly_t *today, const weather_hourly_t *const days[WEATHER_UI_DAYS]);
void weather_ui_set_loading(bool loading);
void weather_ui_set_error(const char *msg_or_null); /* NULL hides the error bar */
/* Toast for a user-triggered refresh (header icon or the error bar's retry
 * button) completing: true shows "data updated" and auto-dismisses after 1s,
 * false shows the same message as weather_ui_set_error() and stays until
 * tapped away. Periodic/background refreshes don't call this. */
void weather_ui_show_refresh_toast(bool ok);
/* Dismisses a stuck error toast once a later, silent refresh (one that never
 * calls weather_ui_show_refresh_toast() itself) resolves the error it was
 * reporting. Driven by network_status_policy's NSP_TOAST_HIDDEN transition. */
void weather_ui_hide_refresh_toast(void);

/* Header indicators. `stale` should be true once more than ~1 hour has passed
 * without a successful weather fetch — the app owns that timing, this just displays it.
 * WX_NET_RECONNECTING covers a Wi-Fi outage the driver is still actively retrying
 * (see app_wifi_is_reconnecting()), distinct from WX_NET_OFFLINE, which means it has
 * given up (no stored credentials, or the user forgot the network). */
void weather_ui_set_network_status(wx_net_status_t status);
void weather_ui_set_data_stale(bool stale);

/* Settings > Device information dialog (Claude Design, 2026-09-10; last_update
 * added 2026-09-11, layout re-synced 2026-09-12; coprocessor_version added
 * 2026-09-18 — the ESP32-C6's own esp_hosted firmware version, shown right
 * after firmware_version, which the same sync relabeled "Application
 * version"). ip/dns/gateway/note are only shown while online — pass NULL for
 * any of them while offline, the dialog falls back to "not connected" text
 * instead. last_update is the exception: it always shows (you should still
 * be able to see when data was last fetched while currently offline), so
 * always pass a real string for it, not NULL. Strings are copied in, safe to
 * pass stack buffers. last_update is pre-formatted by the app (already
 * localized), same as every other display string here — pass the design's
 * "No update yet" fallback text yourself if there has never been a
 * successful fetch. coprocessor_version has no such fallback text in the
 * design; pass "-" or similar yourself if the version can't be read. */
typedef struct {
    const char *device_name, *hardware_version, *firmware_version, *coprocessor_version;
    bool online;
    const char *ip, *dns, *gateway, *last_update, *note;
} weather_device_info_t;
void weather_ui_set_device_info(const weather_device_info_t *info);

/* Current settings, so the app can persist them / re-fetch in the right language. */
weather_lang_t weather_ui_get_language(void);
wx_time_fmt_t  weather_ui_get_time_fmt(void);

/* Called by the app once its geocoding-API fetch (triggered via the on_search
 * callback) resolves; parallel arrays, `count` entries, 0 clears/shows "no cities".
 * is_fav[i] colors that result's favorite-toggle star — pass NULL to leave
 * every star unfavorited (e.g. while the app hasn't computed it yet). */
void weather_ui_set_search_results(const char *const names[], const char *const subs[],
                                    const bool *const is_fav, int count);
void weather_ui_set_searching(bool searching);

/* Favorite-city slots shown above the search results (always
 * WEATHER_UI_FAVORITES_MAX entries; used[i] false renders an empty
 * placeholder, names[i] is only read where used[i] is true). Call once at
 * startup with the persisted set and again after every toggle/remove. */
void weather_ui_set_favorites(const char *const names[WEATHER_UI_FAVORITES_MAX],
                               const bool used[WEATHER_UI_FAVORITES_MAX]);

/* Called once the app's Wi-Fi scan (triggered via on_scan) resolves. */
void weather_ui_set_wifi_scan_results(const wx_wifi_network_t *networks, int count);
/* Called once the app's Wi-Fi connect attempt (triggered via on_connect) resolves. */
void weather_ui_set_wifi_connect_result(bool success);

/* Opens the Wi-Fi setup screen directly — used at first boot when no credentials
 * are stored yet, so the device is configurable without a serial cable. */
void weather_ui_open_wifi_setup(void);

#endif
