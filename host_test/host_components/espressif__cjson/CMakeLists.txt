# Host-test shim for the managed "espressif__cjson" component: same name (so
# app_logic's PRIV_REQUIRES resolves to this instead), same vendored source,
# but no idf_component.yml — the real package's manifest declares it
# incompatible with the "linux" target, which this project builds for. See
# host_test/CMakeLists.txt for why this can't just add managed_components/
# wholesale.
set(CJSON_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../managed_components/espressif__cjson/cJSON")

idf_component_register(
    SRCS "${CJSON_SRC_DIR}/cJSON.c"
    INCLUDE_DIRS "${CJSON_SRC_DIR}"
)
