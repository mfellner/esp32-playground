idf_component_register(SRCS "main.cpp" "launcher_ui.cpp" "slot_model.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES board app_switch nvs_flash esp_system esp_app_format app_update esp_timer esp_driver_gpio esp_driver_usb_serial_jtag)
