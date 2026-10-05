#pragma once

/* Constants shared by more than one non-UI file in main/. Never include this
 * from the UI sources (weather_ui.c, weather_chart.c, weather_icons.c,
 * weather_i18n.c): sim/ builds those without ESP-IDF. */

#include "esp_wifi_types.h"

/* Default wait for the LVGL display lock from outside the LVGL task. */
#define DISPLAY_LOCK_TIMEOUT_MS 1000

/* esp_http_client settings shared by the Open-Meteo and OTA downloads. */
#define HTTP_TIMEOUT_MS 15000
#define HTTP_RX_BUFFER_BYTES 4096
/* Starting size of a growing response buffer when Content-Length is unknown. */
#define HTTP_BODY_INITIAL_CAP_BYTES 8192

/* NUL-terminated copies of the station credentials. */
#define WIFI_SSID_BUF_LEN (sizeof(((wifi_sta_config_t *)0)->ssid) + 1)
#define WIFI_PASS_BUF_LEN (sizeof(((wifi_sta_config_t *)0)->password) + 1)
_Static_assert(WIFI_SSID_BUF_LEN == 33, "SSID buffer size changed");
_Static_assert(WIFI_PASS_BUF_LEN == 65, "password buffer size changed");
