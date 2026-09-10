# Hardware-free application logic. No IDF headers, no driver/, no freertos/ —
# see CLAUDE.md's "Codeorganisation". Host-tested by host_test/, linked into
# the firmware and the emulator build like any other component.
idf_component_register(
    SRCS "light_policy.c" "storage_record.c" "weather_forecast_parse.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES espressif__cjson
)
