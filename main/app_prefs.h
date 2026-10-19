/* Persisted user settings and the selected city (NVS). */
#ifndef APP_PREFS_H
#define APP_PREFS_H

#include "ota_update.h"
#include "weather_ui.h"
#include "weather_i18n.h"
#include "screensaver_policy.h"

typedef struct {
    char  name[64];
    char  country[64];
    float lat, lon;
    weather_lang_t lang;
    wx_temp_unit_t temp_unit;
    wx_wind_unit_t wind_unit;
    wx_time_fmt_t  time_fmt;
    int brightness; /* LIGHT_POLICY_BRIGHTNESS_MIN..MAX percent, display backlight */
    bool brightness_adaptive; /* true: camera-driven, ignored if no camera was found */
    int auto_refresh_minutes; /* 0/15/30/60; 0 disables the periodic background refresh */
    /* Firmware update dialog (2026-09-19 sync). Defaults match the design's
     * own initial state (otaAutoUpdate: true, otaUpdateCoprocessor: false). */
    bool ota_auto_update;
    bool ota_update_coprocessor;
    /* Screen saver (2026-10-15 sync). Kept in its own small record, see
     * app_prefs_save_screensaver(). */
    int screensaver_minutes;      /* 0/15/30/60; 0 = never */
    saver_style_t screensaver_style;
} app_prefs_t;

/* Loads prefs from storage (NVS today, see main/storage_backend_nvs.c),
 * falling back to the Kconfig defaults. Never fails. */
void app_prefs_load(app_prefs_t *out);

/* All four save_* calls below write into the same on-disk record (see
 * prefs_payload_t in app_prefs.c) through a single shared debounce timer, so
 * a burst of changes across several of them within ~50ms becomes one flash
 * write. They persist asynchronously on the esp_timer task, not the
 * caller's: app_prefs_save_brightness_adaptive() in particular is called
 * from the LVGL task the instant the adaptive switch flips (and from the
 * brightness slider's drag-start, which auto-disables adaptive mode), and a
 * synchronous flash write there would block the touch gesture that
 * triggered it. */
void app_prefs_save_city(const char *name, const char *country, float lat, float lon);
void app_prefs_save_settings(weather_lang_t lang, wx_temp_unit_t t, wx_wind_unit_t w, wx_time_fmt_t tf, int auto_refresh_minutes);
void app_prefs_save_brightness(int percent);
void app_prefs_save_brightness_adaptive(bool enabled);
void app_prefs_save_ota_settings(bool auto_update, bool update_coprocessor);

/* Screen saver timeout and colour style. Own record (like the update channel
 * below), so the main prefs record needs no migration and an older firmware
 * simply ignores it. Debounced like the other saves: it is called from the
 * LVGL task on a Settings tap. */
void app_prefs_save_screensaver(int minutes, saver_style_t style);

/* Update channel (release vs. test). Kept in its own small record, not in
 * prefs_payload_t, so adding it needs no migration of the main prefs record
 * and a downgrade to older firmware simply ignores it. Loaded by
 * app_prefs_load(); defaults to OTA_CHANNEL_RELEASE. Saved asynchronously on the
 * esp_timer task like the other settings: it is called from the LVGL task, whose stack is in
 * PSRAM and which must not touch flash. */
ota_channel_t app_prefs_get_ota_channel(void);
void app_prefs_save_ota_channel(ota_channel_t channel);

/* Live copy, kept in sync by the save_* calls above. */
const app_prefs_t *app_prefs_get(void);

#endif
