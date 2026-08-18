cmake_minimum_required(VERSION 3.15)

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

set(WIFI_TCP_COMMON_DIR ${CMAKE_CURRENT_LIST_DIR}/../wifi_tcp)

sdk_add_compile_definitions(-DCONFIG_CLI_CMD_ENABLE)
sdk_add_compile_definitions(-DHIGH_PERFORMANCE_PBUF=1)

target_sources(app PRIVATE
                   ${WIFI_TCP_COMMON_DIR}/wifi_tcp_client.c
                   ${WIFI_TCP_COMMON_DIR}/wifi_tcp_server.c)

sdk_add_include_directories(.)

sdk_set_main_file(${WIFI_TCP_COMMON_DIR}/main.c)

project(wifi_tcp_high_perf)
