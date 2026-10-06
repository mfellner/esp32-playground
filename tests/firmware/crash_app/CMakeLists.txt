# Validation fixture: an app that panics during startup, installed temporarily into the reserved
# `hermes` slot to exercise the platform bootloader's crash guard. Never ship it.
cmake_minimum_required(VERSION 3.16)
set(PLATFORM_DIR ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(EXTRA_COMPONENT_DIRS ${PLATFORM_DIR}/components/app_switch)
set(SDKCONFIG_DEFAULTS
    "${PLATFORM_DIR}/components/app_switch/layout/sdkconfig.defaults.platform;sdkconfig.defaults")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "0.0.1")
project(crashtest)
platform_app_slot(hermes)
