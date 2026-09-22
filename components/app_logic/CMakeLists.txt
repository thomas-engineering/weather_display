# Hardware-free application logic. No IDF headers, no driver/, no freertos/ —
# see CLAUDE.md's "Codeorganisation". Host-tested by host_test/, linked into
# the firmware and the emulator build like any other component.
idf_component_register(
    SRCS "light_policy.c" "storage_record.c" "weather_forecast_parse.c" "favorites.c"
         "ota_manifest_parse.c" "ota_version_compare.c" "network_status_policy.c"
         "wifi_reconnect_policy.c" "link_health_policy.c" "heap_watch.c"
         "coprocessor_health_policy.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES espressif__cjson
)
