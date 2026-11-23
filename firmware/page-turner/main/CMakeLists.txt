idf_component_register(SRCS "app.cpp" "controls.c" "hosts.c" "page_ble.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES bt esp_app_format esp_driver_gpio esp_driver_usb_serial_jtag esp_timer nvs_flash)
