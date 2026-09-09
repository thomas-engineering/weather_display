# Host-Simulator: das LVGL-UI der Firmware in einem SDL-Fenster auf dem Desktop.
#
# Bewusst kein ESP-IDF-Projekt. Gebaut wird nur der Teil von main/, der ohnehin
# hardwarefrei ist — die UI-Dateien inkludieren ausser lvgl.h, libc und
# sdkconfig.h (siehe sim/sdkconfig.h) nichts. Alles was Treiber anfasst
# (main.c, app_wifi.c, app_weather.c, app_prefs.c) bleibt draussen.
#
# Nicht abgedeckt: MIPI-DSI-Timing, PPA-Rotation, Tear-Avoid-Modus, echtes
# Panelfarbverhalten. Dafuer gibt es nur die Hardware.

cmake_minimum_required(VERSION 3.16)
project(weather_sim C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(APP_DIR   "${REPO_ROOT}/main")

# --- LVGL ------------------------------------------------------------------
# Dieselbe Quelle wie die Firmware: die vom Component Manager geholte 9.5.0.
# Kein zweiter Checkout, damit Simulator und Geraet nicht auseinanderlaufen.
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS       OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES    OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${REPO_ROOT}/managed_components/lvgl__lvgl" lvgl EXCLUDE_FROM_ALL)

# --- SDL2 ------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
# Der SDL-Treiber liegt in LVGL selbst (src/drivers/sdl), also braucht schon die
# lvgl-Bibliothek die SDL-Header, nicht erst das Programm.
target_link_libraries(lvgl PUBLIC PkgConfig::SDL2)

# --- UI-Quellen aus main/ ---------------------------------------------------
# Wie in main/CMakeLists.txt geglobbt, damit eine neue Schriftgroesse aus
# tools/gen_fonts.sh hier nicht auch noch eingetragen werden muss.
file(GLOB WEATHER_FONT_SRCS "main.c" "${APP_DIR}/fonts/*.c")

add_executable(weather_sim
    main.c
    "${APP_DIR}/weather_ui.c"
    "${APP_DIR}/weather_chart.c"
    "${APP_DIR}/weather_icons.c"
    "${APP_DIR}/weather_i18n.c"
    "${APP_DIR}/app_format.c"
    ${WEATHER_FONT_SRCS}
)

# sim/ zuerst: von hier kommt der sdkconfig.h-Ersatz.
target_include_directories(weather_sim PRIVATE "${CMAKE_CURRENT_LIST_DIR}" "${APP_DIR}")
target_compile_options(weather_sim PRIVATE -Wall -Wextra)
target_link_libraries(weather_sim PRIVATE lvgl m)
