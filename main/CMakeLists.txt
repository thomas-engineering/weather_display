# The generated Inter faces are one .c per size; glob so adding a size to
# tools/gen_fonts.sh does not also mean editing this list.
file(GLOB WEATHER_FONT_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/fonts/*.c)

idf_component_register(
    SRCS
        "main.c"
        "app_wifi.c"
        "app_weather.c"
        "app_prefs.c"
        "app_format.c"
        "app_light.c"
        "storage_backend_nvs.c"
        "save_debounce.c"
        "weather_ui.c"
        "weather_chart.c"
        "weather_icons.c"
        "weather_i18n.c"
        ${WEATHER_FONT_SRCS}
    INCLUDE_DIRS "."
    REQUIRES esp32_p4_wifi6_touch_lcd_7b lvgl__lvgl
    PRIV_REQUIRES esp_wifi esp_driver_sdmmc nvs_flash esp_http_client esp-tls
                  espressif__cjson lwip esp_netif espressif__esp_video app_logic esp_timer
                  esp_app_format)

# NOTE: no esp_hosted/SDMMC link workaround here. The Waveshare examples carry one
# for esp_hosted 1.4.x, which relied on the IDF 5 umbrella driver's transitive SDMMC
# include. This project is on esp_hosted 3.0.x, which needs no such patch — and its
# component library is an INTERFACE target, so target_link_libraries(... PRIVATE ...)
# against it is a hard CMake error.
