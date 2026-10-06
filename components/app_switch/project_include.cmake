# Platform layout helpers. Included by ESP-IDF while project() runs; call these after project().

# Fails configuration when the project deviates from the shared flash layout or bootloader contract.
function(platform_check_layout)
    set(layout_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/layout")
    idf_build_get_property(project_dir PROJECT_DIR)
    idf_build_get_property(sdkconfig_cmake SDKCONFIG_CMAKE)
    include(${sdkconfig_cmake})

    file(SHA256 "${layout_dir}/partitions.csv" expected_table)
    file(SHA256 "${PARTITION_CSV_PATH}" actual_table)
    if(NOT expected_table STREQUAL actual_table)
        message(FATAL_ERROR "${PARTITION_CSV_PATH} differs from the platform layout "
                            "${layout_dir}/partitions.csv. Copy the platform file unchanged.")
    endif()

    set(problems "")
    macro(_platform_require name expected)
        if(NOT "${${name}}" STREQUAL "${expected}")
            list(APPEND problems "${name}=${${name}} (expected ${expected})")
        endif()
    endmacro()
    _platform_require(CONFIG_IDF_TARGET "esp32c6")
    _platform_require(CONFIG_ESPTOOLPY_FLASHSIZE "16MB")
    _platform_require(CONFIG_ESPTOOLPY_FLASHMODE "dio")
    _platform_require(CONFIG_ESPTOOLPY_FLASHFREQ "80m")
    _platform_require(CONFIG_PARTITION_TABLE_OFFSET "0x8000")
    _platform_require(CONFIG_PARTITION_TABLE_MD5 "y")
    _platform_require(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE "")
    _platform_require(CONFIG_BOOTLOADER_FACTORY_RESET "")
    _platform_require(CONFIG_BOOTLOADER_APP_TEST "")
    _platform_require(CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC "y")
    _platform_require(CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC_SIZE "0x10")
    _platform_require(CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC_IN_CRC "")
    _platform_require(CONFIG_SECURE_BOOT "")
    _platform_require(CONFIG_SECURE_FLASH_ENC_ENABLED "")
    _platform_require(IDF_VERSION_MAJOR "5")
    _platform_require(IDF_VERSION_MINOR "5")
    _platform_require(IDF_VERSION_PATCH "3")
    if(problems)
        string(REPLACE ";" "\n  " problems "${problems}")
        message(FATAL_ERROR "Platform contract violated (include "
                            "${layout_dir}/sdkconfig.defaults.platform):\n  ${problems}")
    endif()
endfunction()

# Declares that this project is the app installed in OTA slot <label>:
#  - adds `idf.py <label>-flash`, writing only the app binary to that partition, and
#    build/<label>-flash_args generated from this project's own partition table;
#  - fails the build if the binary exceeds that partition;
#  - disables `idf.py flash` / `app-flash`, which would overwrite the launcher and reset otadata.
function(platform_app_slot label)
    platform_check_layout()
    idf_build_get_property(build_dir BUILD_DIR)
    set(project_bin "${PROJECT_BIN}") # set by esptool_py's project_include.cmake
    if(NOT project_bin)
        message(FATAL_ERROR "PROJECT_BIN is undefined; call platform_app_slot() after project()")
    endif()

    partition_table_get_partition_info(type "--partition-name ${label}" "type")
    partition_table_get_partition_info(subtype "--partition-name ${label}" "subtype")
    if(NOT type STREQUAL "0" OR subtype LESS 16 OR subtype GREATER 31)
        message(FATAL_ERROR "Partition '${label}' is not an OTA app slot in the platform layout")
    endif()

    idf_component_get_property(main_args esptool_py FLASH_ARGS)
    idf_component_get_property(sub_args esptool_py FLASH_SUB_ARGS)
    esptool_py_flash_target(${label}-flash "${main_args}" "${sub_args}" ALWAYS_PLAINTEXT)
    esptool_py_flash_to_partition(${label}-flash ${label} "${build_dir}/${project_bin}")
    add_dependencies(${label}-flash app)

    partition_table_add_check_size_target(${label}_slot_check_size
        DEPENDS gen_project_binary
        BINARY_PATH "${build_dir}/${project_bin}"
        PARTITION_TYPE app
        PARTITION_SUBTYPE ${subtype})
    add_dependencies(app ${label}_slot_check_size)

    fail_target(platform_flash_guard
        "This app lives in the '${label}' slot of the platform layout."
        "'idf.py flash' and 'app-flash' would overwrite the launcher and reset the boot selection."
        "Use 'idf.py -p PORT ${label}-flash' or 'tools/device.py install ${label} BUILD_DIR'.")
    add_dependencies(flash platform_flash_guard)
    add_dependencies(app-flash platform_flash_guard)
endfunction()
