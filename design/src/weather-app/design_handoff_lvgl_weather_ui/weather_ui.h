#ifndef WEATHER_UI_H
#define WEATHER_UI_H

#include "lvgl.h"
#include "weather_i18n.h"
#include <stdbool.h>

#define WEATHER_UI_DAYS 7

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

typedef enum { WX_NET_ONLINE, WX_NET_OFFLINE } wx_net_status_t;

typedef struct {
    char ssid[33];
    int strength;   /* 0-3 bars */
    bool secured;
} wx_wifi_network_t;

typedef void (*weather_ui_search_cb_t)(const char *query);
typedef void (*weather_ui_select_city_cb_t)(int result_index);
typedef void (*weather_ui_refresh_cb_t)(void);
typedef void (*weather_ui_settings_changed_cb_t)(weather_lang_t lang, wx_temp_unit_t temp, wx_wind_unit_t wind, wx_time_fmt_t time_fmt);
typedef void (*weather_ui_wifi_scan_cb_t)(void);
typedef void (*weather_ui_wifi_connect_cb_t)(const char *ssid, const char *password);

/* Builds the whole 1024x600 screen as a child of `parent` (typically lv_scr_act()). */
void weather_ui_create(lv_obj_t *parent);

void weather_ui_set_callbacks(weather_ui_search_cb_t on_search, weather_ui_select_city_cb_t on_select_city,
                               weather_ui_refresh_cb_t on_refresh, weather_ui_settings_changed_cb_t on_settings_changed);
void weather_ui_set_wifi_callbacks(weather_ui_wifi_scan_cb_t on_scan, weather_ui_wifi_connect_cb_t on_connect);

void weather_ui_set_language(weather_lang_t lang);
void weather_ui_set_units(wx_temp_unit_t temp, wx_wind_unit_t wind, wx_time_fmt_t time_fmt);

void weather_ui_set_current(const weather_current_t *cur);
void weather_ui_set_days(const weather_day_t days[WEATHER_UI_DAYS]);
/* Hourly series for "today" (shown inline on the current-weather card) and one
 * per forecast day (shown when that day's detail panel is opened), same order
 * as weather_ui_set_days(). Pass NULL entries for days you have no hourly data for. */
void weather_ui_set_hourly(const weather_hourly_t *today, const weather_hourly_t *const days[WEATHER_UI_DAYS]);
void weather_ui_set_loading(bool loading);
void weather_ui_set_error(const char *msg_or_null); /* NULL hides the error bar */

/* Header indicators. `stale` should be true once more than ~1 hour has passed
 * without a successful weather fetch — the app owns that timing, this just displays it. */
void weather_ui_set_network_status(wx_net_status_t status);
void weather_ui_set_data_stale(bool stale);

/* Called by the app once its geocoding-API fetch (triggered via the on_search
 * callback) resolves; parallel arrays, `count` entries, 0 clears/shows "no cities". */
void weather_ui_set_search_results(const char *const names[], const char *const subs[], int count);

/* Called once the app's Wi-Fi scan (triggered via on_scan) resolves. */
void weather_ui_set_wifi_scan_results(const wx_wifi_network_t *networks, int count);
/* Called once the app's Wi-Fi connect attempt (triggered via on_connect) resolves. */
void weather_ui_set_wifi_connect_result(bool success);

#endif
