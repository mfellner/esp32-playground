idf_component_register(SRCS "board.cpp" "orientation.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES lvgl__lvgl
                       PRIV_REQUIRES esp_driver_i2c esp_driver_spi esp_lcd esp_timer espressif__esp_lvgl_adapter espressif__esp_lcd_sh8601 waveshare__esp_lcd_touch_cst9217)
