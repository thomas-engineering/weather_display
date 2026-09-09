/* Persisted user settings and the selected city (NVS). */
#ifndef APP_PREFS_H
#define APP_PREFS_H

#include "weather_ui.h"
#include "weather_i18n.h"

typedef struct {
    char  name[64];
    char  country[64];
    float lat, lon;
    weather_lang_t lang;
    wx_temp_unit_t temp_unit;
    wx_wind_unit_t wind_unit;
    wx_time_fmt_t  time_fmt;
    int brightness; /* 10-100, display backlight */
} app_prefs_t;

/* Loads prefs from NVS, falling back to the Kconfig defaults. Never fails. */
void app_prefs_load(app_prefs_t *out);

void app_prefs_save_city(const char *name, const char *country, float lat, float lon);
void app_prefs_save_settings(weather_lang_t lang, wx_temp_unit_t t, wx_wind_unit_t w, wx_time_fmt_t tf);
void app_prefs_save_brightness(int percent);

/* Live copy, kept in sync by the save_* calls above. */
const app_prefs_t *app_prefs_get(void);

#endif
