cmake_minimum_required(VERSION 3.16)

# Pflicht fuer das Linux-Target: nur "main" und dessen Abhaengigkeiten bauen.
# Ohne diese Zeile versucht der Build alle Komponenten zu uebersetzen und
# scheitert an den chipspezifischen.
set(COMPONENTS main)

# Die zu testenden Komponenten aus dem Hauptprojekt einhaengen.
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")

# app_logic's weather_forecast_parse.c depends on cJSON. The real package
# (managed_components/espressif__cjson) carries an idf_component.yml that
# declares it incompatible with the "linux" target this project builds for —
# adding managed_components/ wholesale also pulls in e.g. esp_cam_sensor,
# which fails the same compatibility check. host_components/ instead holds a
# manifest-free shim component of the same name ("espressif__cjson", so
# app_logic's PRIV_REQUIRES resolves unchanged) that just compiles the same
# vendored cJSON.c — no manifest means no target-compatibility check.
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/host_components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(host_test)
