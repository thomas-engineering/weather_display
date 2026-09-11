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
} weather_strings_t;

/* Weather condition text, indexed by WMO weather code buckets used elsewhere in this port
 * (see weather_ui_wmo_to_cond() in weather_ui.c) — 0=clear,1=mainly clear,2=partly cloudy,
 * 3=overcast,4=fog,5=drizzle/rain,6=snow,7=storm */
extern const char *weather_cond_text[LANG_COUNT][8];

extern const weather_strings_t weather_strings[LANG_COUNT];

/* IETF language tag, for reference / hyphenation dictionaries if the platform's text
 * shaping supports it (LVGL itself does not hyphenate). */
extern const char *weather_lang_tag[LANG_COUNT];

#endif
