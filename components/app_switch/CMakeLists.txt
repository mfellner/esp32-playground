idf_component_register(SRCS "app_switch.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES app_update bootloader_support esp_driver_gpio esp_partition esp_system esp_timer log spi_flash)
