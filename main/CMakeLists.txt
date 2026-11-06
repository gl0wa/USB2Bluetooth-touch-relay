idf_component_register(SRCS "app_main.c" "ble_hogp.c" "usb_touch.c" "netsvc.c" "logring.c"
                       INCLUDE_DIRS "."
                       REQUIRES touchcore bt nvs_flash esp_wifi esp_netif esp_event esp_http_server
                                app_update esp_driver_usb_serial_jtag esp_driver_gpio usb)
