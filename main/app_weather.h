/* Open-Meteo client: geocoding search + 7-day forecast, off the LVGL task.
 *
 * Every entry point here is asynchronous — it posts a command to an internal
 * worker task and returns immediately, so it is safe to call straight from an
 * LVGL event callback. Results are pushed back into the UI by the worker, which
 * takes bsp_display_lock() around every LVGL call.
 */
#ifndef APP_WEATHER_H
#define APP_WEATHER_H

#include <stdbool.h>
#include "weather_i18n.h"

#define APP_WEATHER_MAX_RESULTS 6

/* Starts the worker task. Call once, after the UI exists and Wi-Fi is up. */
void app_weather_start(void);

/* Debounced city search; `query` is copied. Empty/short queries clear results. */
void app_weather_search(const char *query);

/* Picks result `idx` from the last search, persists it, and re-fetches. */
void app_weather_select_city(int idx);

/* Favorites, addressed the same way weather_ui.h documents: `result_index`
 * is an index into the last search results, `slot_index` into the
 * WEATHER_UI_FAVORITES_MAX favorite slots. All asynchronous, like every
 * other entry point here. */
void app_weather_toggle_favorite(int result_index);
void app_weather_select_favorite(int slot_index);
void app_weather_remove_favorite(int slot_index);

/* Re-fetches the forecast for the currently selected city. */
void app_weather_refresh(void);

/* Re-renders already-fetched data in a new language (no network round-trip). */
void app_weather_set_language(weather_lang_t lang);

/* Wi-Fi setup, driven from the UI's network screen. All asynchronous. */
void app_weather_wifi_scan(void);
void app_weather_wifi_connect(const char *ssid, const char *password);
void app_weather_wifi_forget(void);

/* Firmware update dialog (2026-09-19 sync, real download/flash added
 * 2026-09-16, moved off the shared weather worker task and given a real
 * cancel path 2026-09-17 per lvgl-reviewer's findings). app_weather_ota_start()
 * runs the real check-and-update flow from main/ota_update.c on its own
 * dedicated task and pushes progress/result back via weather_ui_set_ota_state();
 * the two toggle functions persist immediately via app_prefs.
 * app_weather_ota_cancel() requests a cooperative abort of whatever's
 * in-flight (a no-op if nothing is). Call app_weather_ota_resume_after_boot()
 * once at startup, after Wi-Fi is up (see main/main.c) — it's a no-op unless
 * this boot follows a P4 update, in which case it optionally flashes the C6
 * and marks the app valid. */
void app_weather_ota_start(void);
void app_weather_ota_cancel(void);
void app_weather_ota_toggle_auto_update(bool on);
void app_weather_ota_toggle_update_coprocessor(bool on);
void app_weather_ota_resume_after_boot(void);

#endif
