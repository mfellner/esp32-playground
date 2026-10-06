# Platform bootloader: stock ESP-IDF 5.5.3 flow plus the KEY recovery gesture and crash guard.
idf_component_register(SRCS "bootloader_start.c"
                       INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../../../components/app_switch/include"
                       REQUIRES bootloader bootloader_support esp_rom hal soc)

# Use the default linker scripts
idf_build_get_property(scripts BOOTLOADER_LINKER_SCRIPT)
target_linker_script(${COMPONENT_LIB} INTERFACE "${scripts}")
