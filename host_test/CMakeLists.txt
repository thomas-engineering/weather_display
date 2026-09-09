cmake_minimum_required(VERSION 3.16)

# Pflicht fuer das Linux-Target: nur "main" und dessen Abhaengigkeiten bauen.
# Ohne diese Zeile versucht der Build alle Komponenten zu uebersetzen und
# scheitert an den chipspezifischen.
set(COMPONENTS main)

# Die zu testenden Komponenten aus dem Hauptprojekt einhaengen.
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(host_test)
