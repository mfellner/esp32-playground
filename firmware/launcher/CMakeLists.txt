cmake_minimum_required(VERSION 3.16)
set(PLATFORM_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
set(EXTRA_COMPONENT_DIRS ${PLATFORM_DIR}/components)
set(SDKCONFIG_DEFAULTS
    "${PLATFORM_DIR}/components/app_switch/layout/sdkconfig.defaults.platform;sdkconfig.defaults")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "0.1.0")
project(launcher)
# The launcher owns the factory slot and the device bootloader; its normal `idf.py flash` writes the
# bootloader, partition table, blank otadata (boot selection -> launcher) and the launcher itself.
platform_check_layout()
