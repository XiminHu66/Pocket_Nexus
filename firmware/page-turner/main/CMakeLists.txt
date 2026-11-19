idf_component_register(SRCS "app.cpp" "controls.c" "page_ble.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES bt esp_driver_gpio esp_driver_usb_serial_jtag esp_timer nvs_flash)
