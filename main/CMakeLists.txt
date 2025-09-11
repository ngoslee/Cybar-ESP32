set(srcs)

if(CONFIG_EXAMPLE_ENABLE_RF_TESTING_CONFIGURATION_COMMAND)
list(APPEND srcs
     "rf_tesing_configuration_cmd.c")
endif()

idf_component_register(SRCS "ble_spp_server.c" "main.c" "hardware.c" "diag_port.c" "lin_truck.c" "user.c" "lin.c" "lin_bar.c" "patterns.c" "egg.c" "${srcs}"
                    PRIV_REQUIRES bt nvs_flash esp_driver_uart console driver led_strip
                    INCLUDE_DIRS ".")
