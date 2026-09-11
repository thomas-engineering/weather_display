#ifndef WEATHER_I18N_H
#define WEATHER_I18N_H

typedef enum {
    LANG_EN = 0,
    LANG_DE,
    LANG_ES,
    LANG_FR,
    LANG_COUNT
} weather_lang_t;

typedef struct {
    const char *cancel, *retry, *search_placeholder, *no_cities, *searching, *settings;
    const char *temperature, *wind_speed, *time_format, *language, *h24, *h12, *brightness, *brightness_adaptive;
    const char *humidity, *feels_like, *wind, *precip, *forecast, *loading;
    const char *today, *tomorrow, *error, *space, *done;
    const char *humid_dry, *humid_comfortable, *humid_humid, *humid_very_humid;
    const char *network, *configure_network, *forget_network, *wifi_title, *scan, *scanning, *no_networks, *secured, *open_net;
    const char *enter_password, *connect, *back, *connecting, *connected, *show, *hide;
    const char *enter_manually, *enter_ssid, *next, *cancel2;
    const char *online, *offline, *data_outdated, *data_updated;
    const char *device_info, *device_name, *hardware_version, *firmware_version;
    const char *ip_address, *dns, *gateway, *not_connected;
    const char *last_update, *no_update_yet;
    /* Header "last synced" text (2026-09-12 sync). data_updated_ago_min/hour
     * take one %d (minutes/hours) — app_format.c's fmt_time_ago() snprintf's
     * directly into them, matching the design's "{n}" placeholder. */
    const char *data_updated_now, *data_updated_ago_min, *data_updated_ago_hour;
    const char *sunrise, *sunset, *uv_index;
    const char *uv_low, *uv_moderate, *uv_high, *uv_very_high, *uv_extreme;
    const char *air_quality;
    const char *aqi_good, *aqi_moderate, *aqi_unhealthy_sensitive, *aqi_unhealthy, *aqi_very_unhealthy, *aqi_hazardous;
    const char *auto_refresh, *auto_off, *auto_15, *auto_30, *auto_60;
    const char *forget_confirm_title, *forget_confirm_body;
} weather_strings_t;

/* Weather condition text, indexed by WMO weather code buckets used elsewhere in this port
 * (see weather_ui_wmo_to_cond() in weather_ui.c) — 0=clear,1=mainly clear,2=partly cloudy,
 * 3=overcast,4=fog,5=drizzle/rain,6=snow,7=storm */
extern const char *weather_cond_text[LANG_COUNT][8];

extern const weather_strings_t weather_strings[LANG_COUNT];

/* Short weekday names (Sunday-first, matching struct tm's tm_wday) and short
 * month names — used by app_format.c's fmt_date()/fmt_day_date()/fmt_day_label(). */
extern const char *weather_wday_short[LANG_COUNT][7];
extern const char *weather_mon_short[LANG_COUNT][12];

/* "Real feel" sentence fragments — app_format.c's fmt_real_feel() picks `same`/
 * `cooler`/`warmer` by the apparent-vs-actual temperature delta, then appends
 * `rain` or `breezy` (or neither). Mirrors the design's getStrings().rf. */
typedef struct {
    const char *same, *cooler, *warmer, *rain, *breezy;
} weather_real_feel_t;
extern const weather_real_feel_t weather_real_feel[LANG_COUNT];

/* IETF language tag, for reference / hyphenation dictionaries if the platform's text
 * shaping supports it (LVGL itself does not hyphenate). */
extern const char *weather_lang_tag[LANG_COUNT];

#endif
