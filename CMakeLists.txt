cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options(-Wno-format)
add_compile_options(-Wno-ignored-qualifiers)
add_compile_options(-Wno-attributes)
add_compile_options(-Wno-unused-function)
project(weather_display)

# The C6 coprocessor (esp-hosted 3.0.x) splits incoming station traffic by
# destination port: only 49152-61439 reaches the P4's lwIP, everything else is
# kept by the C6 and silently dropped. Proven on hardware 2026-09-23: UDP
# replies to local ports 2000/20000/40000/61440/63000/65000 never arrived,
# 49152/55000/61439 answered within milliseconds. Stock lwIP picks ephemeral
# UDP/TCP ports from 49152-65535 (so ~25% of connections could never be
# answered) and the DNS client binds a random port in 1024-65535 (so ~80% of
# lookups timed out with EAI_FAIL, which also starved SNTP of pool.ntp.org).
# Keep every locally chosen port inside the range the C6 forwards to us.
set(HOST_PORT_FIRST 0xC000)   # 49152
set(HOST_PORT_COUNT 0x3000)   # up to and including 61439 (0xEFFF)
idf_component_get_property(lwip_lib lwip COMPONENT_LIB)
target_compile_definitions(${lwip_lib} PRIVATE
    "UDP_LOCAL_PORT_RANGE_START=${HOST_PORT_FIRST}"
    "UDP_LOCAL_PORT_RANGE_END=(${HOST_PORT_FIRST}+${HOST_PORT_COUNT}-1)"
    "TCP_LOCAL_PORT_RANGE_START=${HOST_PORT_FIRST}"
    "TCP_LOCAL_PORT_RANGE_END=(${HOST_PORT_FIRST}+${HOST_PORT_COUNT}-1)")
# Function-like macros: target_compile_definitions() silently drops these,
# so they go in as raw -D options.
target_compile_options(${lwip_lib} PRIVATE
    "-DUDP_ENSURE_LOCAL_PORT_RANGE(port)=((u16_t)(${HOST_PORT_FIRST}+((port)%${HOST_PORT_COUNT})))"
    "-DTCP_ENSURE_LOCAL_PORT_RANGE(port)=((u16_t)(${HOST_PORT_FIRST}+((port)%${HOST_PORT_COUNT})))"
    "-DDNS_PORT_ALLOWED(port)=((port)>=${HOST_PORT_FIRST}&&(port)<${HOST_PORT_FIRST}+${HOST_PORT_COUNT})")
