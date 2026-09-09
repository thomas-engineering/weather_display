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

/* Re-fetches the forecast for the currently selected city. */
void app_weather_refresh(void);

/* Re-renders already-fetched data in a new language (no network round-trip). */
void app_weather_set_language(weather_lang_t lang);

/* Wi-Fi setup, driven from the UI's network screen. All asynchronous. */
void app_weather_wifi_scan(void);
void app_weather_wifi_connect(const char *ssid, const char *password);
void app_weather_wifi_forget(void);

#endif
