# U2B_NETWORK selects the development profile (Wi-Fi, OTA, HTTP diagnostics).
# Defined in main/Kconfig.projbuild, default y. The user build sets
# CONFIG_U2B_NETWORK=n via sdkconfig.release.
#
# Note: IDF collects component REQUIRES in an early expansion phase before
# sdkconfig is generated, so CONFIG_U2B_NETWORK may be undefined when this
# file is first evaluated. Fall back to SDKCONFIG_DEFAULTS in that case:
# release builds pass SDKCONFIG_DEFAULTS containing "sdkconfig.release".
# Once sdkconfig exists, CONFIG_U2B_NETWORK is authoritative.
if(DEFINED CONFIG_U2B_NETWORK)
    set(U2B_NETWORK_ON ${CONFIG_U2B_NETWORK})
elseif("${SDKCONFIG_DEFAULTS}" MATCHES "sdkconfig\\.release")
    set(U2B_NETWORK_ON OFF)
else()
    # Kconfig default is y (development build).
    set(U2B_NETWORK_ON ON)
endif()

if(U2B_NETWORK_ON)
    idf_component_register(SRCS "app_main.c" "ble_hogp.c" "usb_touch.c" "netsvc.c" "logring.c" "serial_cfg.c"
                           INCLUDE_DIRS "."
                           REQUIRES touchcore bt nvs_flash esp_wifi esp_netif esp_event esp_http_server
                                    app_update esp_driver_usb_serial_jtag esp_driver_gpio usb)
else()
    idf_component_register(SRCS "app_main.c" "ble_hogp.c" "usb_touch.c" "logring.c" "serial_cfg.c"
                           INCLUDE_DIRS "."
                           REQUIRES touchcore bt nvs_flash
                                    app_update esp_driver_usb_serial_jtag esp_driver_gpio usb)
endif()
