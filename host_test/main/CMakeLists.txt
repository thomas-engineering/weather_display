idf_component_register(
    SRCS "test_main.c" "test_light_policy.c" "test_storage_record.c" "fake_storage_backend.c"
         "test_weather_forecast_parse.c" "test_favorites.c"
         "test_ota_manifest_parse.c" "test_ota_version_compare.c" "test_network_status_policy.c"
         "test_wifi_reconnect_policy.c" "test_link_health_policy.c"
    INCLUDE_DIRS "."
    REQUIRES unity app_logic
)
